#!/usr/bin/env bash
#
# gpu_run.sh -- one-command capture for a time-boxed session on a GPU instance.
#
# This is free and unencumbered software released into the public domain.
# See LICENSE.
#
# Written for the case where you have an hour on rented hardware and want to
# come away with data rather than a shell history. It captures the environment,
# gates on correctness, runs the measurement matrix in priority order, and
# leaves a single tarball to copy off the box.
#
# Priority order is the point. If the session is cut short, the phases that
# already finished are the ones that mattered most -- the PCIe crossover first,
# because that is the question the GPU path exists to answer, and the nice-to-
# haves last.
#
#   ./tools/gpu_run.sh              full run, roughly 20-30 min
#   ./tools/gpu_run.sh -q           quick pass, roughly 5 min
#   ./tools/gpu_run.sh --skip-check skip the correctness gate (not advised)
#
# Everything lands in results-<host>-<timestamp>/ and is tarred at the end,
# including on failure or interrupt.

set -u

OUT=""
QUICK=0
SKIP_CHECK=0
XFER_WS=262144          # KiB of corpus for the streaming sweep

usage() {
    cat <<EOF
Usage: $0 [options]

  -o DIR          output directory (default: results-<host>-<timestamp>)
  -q, --quick     smaller grids and fewer samples, for a first smoke pass
      --skip-check  skip 'make check' (the correctness gate -- not advised)
      --ws KB     corpus size for the streaming sweep (default $XFER_WS KiB)
  -h, --help      this text

The streaming sweep wants a corpus large enough that the upload takes
milliseconds, or launch overhead swamps the transfer it is trying to measure.
Keep --ws well under the device's max allocation, which --list-devices reports.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -o) OUT="${2:-}"; shift 2 ;;
        -q|--quick) QUICK=1; shift ;;
        --skip-check) SKIP_CHECK=1; shift ;;
        --ws) XFER_WS="${2:-}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "$0: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

cd "$(dirname "$0")/.." || exit 1
REPO=$(pwd)
BIN=$REPO/build/valubench
SWEEP=$REPO/tools/sweep.py

[ -n "$OUT" ] || OUT="$REPO/results-$(hostname -s 2>/dev/null || echo host)-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT" || exit 1
LOG="$OUT/run.log"

START=$(date +%s)
elapsed() { printf '%dm%02ds' $(( ($(date +%s)-START)/60 )) $(( ($(date +%s)-START)%60 )); }

say()  { printf '\n=== [%s] %s\n' "$(elapsed)" "$*" | tee -a "$LOG"; }
note() { printf '    %s\n' "$*" | tee -a "$LOG"; }

# Always leave a tarball, however the run ends.
package() {
    tar czf "$OUT.tar.gz" -C "$(dirname "$OUT")" "$(basename "$OUT")" 2>/dev/null
    printf '\n=== [%s] done\n' "$(elapsed)"
    printf '    %s\n' "$OUT.tar.gz"
    printf '    copy it off with:  scp %s:%s .\n' \
           "$(hostname -s 2>/dev/null || echo HOST)" "$OUT.tar.gz"
}
trap package EXIT INT TERM

if [ "$QUICK" = 1 ]; then
    SAMPLES=3; TIME_MS=100; WARMUP=100
    ITERS="1:6:+1,16,64"
    WS_AXIS="1024,16384,262144"
    XFER_WS=$(( XFER_WS / 4 ))
else
    SAMPLES=7; TIME_MS=200; WARMUP=300
    ITERS="1:8:+1,16:1024:*2"
    WS_AXIS="256:1048576:*4"
fi

SW="python3 $SWEEP --samples $SAMPLES --time-ms $TIME_MS --warmup-ms $WARMUP
    --keep-going -q"

# ---------------------------------------------------------------- environment

say "environment"
{
    echo "# date";        date -u
    echo; echo "# uname";  uname -a
    echo; echo "# cpu";    (lscpu 2>/dev/null || grep -m1 'model name' /proc/cpuinfo)
    echo; echo "# cpu flags of interest"
    grep -m1 '^flags' /proc/cpuinfo 2>/dev/null | tr ' ' '\n' \
        | grep -xE 'sse2|avx2|avx512f|sha_ni' | sort | tr '\n' ' '; echo
    echo; echo "# memory"; free -h 2>/dev/null
    echo; echo "# nvidia-smi"
    nvidia-smi 2>&1 || echo "(no nvidia-smi)"
    echo; echo "# nvidia topology"
    nvidia-smi topo -m 2>&1 || echo "(unavailable)"
    echo; echo "# pcie link"
    nvidia-smi --query-gpu=name,pcie.link.gen.max,pcie.link.gen.current,pcie.link.width.max,pcie.link.width.current \
        --format=csv 2>&1 || echo "(unavailable)"
    echo; echo "# clinfo"
    (clinfo 2>&1 | head -40) || echo "(no clinfo)"
} > "$OUT/environment.txt" 2>&1
note "captured to environment.txt"
grep -m1 'model name' /proc/cpuinfo 2>/dev/null | tee -a "$LOG"
nvidia-smi --query-gpu=name,memory.total --format=csv,noheader 2>/dev/null | tee -a "$LOG"

# ---------------------------------------------------------------------- build

say "build"
make -C "$REPO" -s clean >/dev/null 2>&1
if ! make -C "$REPO" -j"$(nproc 2>/dev/null || echo 2)" > "$OUT/build.log" 2>&1; then
    note "BUILD FAILED -- see build.log"
    tail -20 "$OUT/build.log" | tee -a "$LOG"
    exit 1
fi
make -C "$REPO" config    > "$OUT/make-config.txt" 2>&1
"$BIN" --list             > "$OUT/kernels.txt"     2>&1
"$BIN" --list-devices     > "$OUT/devices.txt"     2>&1
cat "$OUT/make-config.txt" | tee -a "$LOG"

NDEV=$(grep -c '^\[[0-9]' "$OUT/devices.txt" 2>/dev/null || echo 0)
note "OpenCL devices: $NDEV"
if [ "$NDEV" -eq 0 ]; then
    note "no OpenCL device -- GPU phases will be skipped. Reason:"
    sed 's/^/      /' "$OUT/devices.txt" | tee -a "$LOG"
fi

# ------------------------------------------------------------ correctness gate

if [ "$SKIP_CHECK" = 0 ]; then
    say "correctness gate (make check)"
    note "this builds every OpenCL kernel variant; NVIDIA JITs through PTX so"
    note "it can take several minutes. It is the gate -- a fast wrong number"
    note "is worse than no number."
    if make -C "$REPO" check > "$OUT/check.log" 2>&1; then
        note "PASS: $(grep -c '^  ok' "$OUT/check.log") kernels, $(tail -1 "$OUT/check.log")"
    else
        note "*** CHECK FAILED -- every number below is suspect ***"
        grep -E 'FAIL|failures' "$OUT/check.log" | head -20 | tee -a "$LOG"
    fi
else
    say "correctness gate SKIPPED by request"
fi

# ------------------------------------------------- P1: the PCIe crossover

if [ "$NDEV" -gt 0 ]; then
    say "P1  PCIe crossover  (the reason for this session)"
    note "corpus re-uploaded before every launch, iterations swept."
    note "compute/transfer crosses 1.0 at N* -- below it the link binds,"
    note "above it the device does."
    #
    # 64-byte messages, deliberately, and the same length for all three.
    #
    # It is the length at which every algorithm stores exactly 128 bytes per
    # message once padded -- MD5 and SHA-1 spill into a second 64-byte block,
    # SHA-512 still fits one 128-byte block -- so the transfer side of the ratio
    # is identical across the three and only the compute side varies. That is
    # the controlled comparison the crossover needs.
    #
    # It is also the shortest length SHA-512 can iterate at, since the digest is
    # fed back over the head of the message. At the 55-byte default every
    # SHA-512 point with iterations > 1 is skipped and the sweep collapses to a
    # single row.
    #
    XMSG=64
    for alg in md5 sha1 sha512; do
        #
        # The CPU side of break-even is swept at *identical* parameters -- same
        # message length, same working set, same iteration ladder -- because
        # the two curves have to be crossable. They were not: the CPU baseline
        # in P3 runs at the default message length, a different working set and
        # a single iteration, so nothing in a captured session could answer
        # "when does offload beat the machine I already own".
        #
        # The best CPU kernel is found once rather than autotuned per point:
        # probing thirteen kernels at every rung of the ladder would cost more
        # than the ladder does, and the winner does not move with N. --where cpu
        # is what makes that probe a CPU answer on a box whose device kernel
        # would otherwise win it.
        #
        CPUK=$("$BIN" --algorithm "$alg" --where cpu --json \
                   --message-bytes "$XMSG" --working-set-kb 8192 \
                   --samples 3 --time-ms 40 --warmup-ms 100 2>/dev/null \
               | python3 -c 'import json,sys
try:
    print(json.load(sys.stdin)["kernel"]["name"])
except Exception:
    pass' 2>/dev/null)
        [ -n "$CPUK" ] && note "$alg CPU baseline kernel: $CPUK"

        $SW --algorithm "$alg" --kernel "$alg/ocl-s1${CPUK:+,$CPUK}" \
            --transfer stream \
            --message-bytes "$XMSG" \
            --working-set-kb "$XFER_WS" --iterations "$ITERS" \
            --csv "$OUT/p1-crossover-$alg.csv" >> "$LOG" 2>&1
        if [ -s "$OUT/p1-crossover-$alg.csv" ]; then
            note "$alg:"
            python3 - "$OUT/p1-crossover-$alg.csv" "$REPO" <<'PY' | tee -a "$LOG"
import csv, math, sys
sys.path.insert(0, sys.argv[2] + "/tools")
from sweep import balance_point, break_even

all_rows = list(csv.DictReader(open(sys.argv[1])))
# The CSV now carries both sides. N* is a device question; the CPU rows are
# there for break-even below.
rows = [r for r in all_rows if r.get("runs_on") == "device"]
cpu_rows = [r for r in all_rows if r.get("runs_on") == "cpu"]
print("      %6s %10s %10s %8s %s" % ("iters", "MH/s", "GB/s", "c/x", "bound by"))
for r in rows:
    try:
        ratio = float(r["compute_transfer_ratio"] or 0)
    except ValueError:
        ratio = 0.0
    print("      %6s %10.2f %10s %8.2f %s" % (
        r["iterations"], float(r["hashes_per_sec"]) / 1e6,
        r["transfer_gbytes_per_sec"] or "-", ratio, r["bound_by"]))

# Solve rather than bracket. Transfer is constant in the iteration count and
# kernel time is linear in it, so the balance point is an intercept, not a
# search. A geometric sweep alone would only ever answer to within its step.
fit = balance_point(rows)
if fit:
    print("      %s" % ("-" * 52))
    print("      N* = %.2f iterations   (compute-bound from %d up)"
          % (fit["n_star"], max(1, math.ceil(fit["n_star"]))))
    print("      %.3f ms/iteration + %.3f ms launch  vs  %.3f ms transfer"
          % (fit["per_iter_ns"]/1e6, fit["launch_overhead_ns"]/1e6,
             fit["transfer_ns"]/1e6))
    flag = ""
    if fit["r2"] < 0.98:
        flag = "   *** nonlinear -- do not quote N* ***"
    if fit["transfer_spread"] > 0.15:
        flag += "   *** transfer varied %.0f%% ***" % (fit["transfer_spread"]*100)
    print("      fit r2 = %.4f over %d points, transfer flat to %.1f%%%s"
          % (fit["r2"], fit["points"], fit["transfer_spread"]*100, flag))
else:
    print("      (not enough points to solve for N*)")

# Break-even: the purchasing question, which N* does not answer. N* says
# whether the bus is in the way; this says whether the device beats the host
# at all, transfers included.
if cpu_rows:
    def best(rs):
        by_n = {}
        for r in rs:
            try:
                n, hps = int(r["iterations"]), float(r["hashes_per_sec"])
            except (ValueError, KeyError):
                continue
            if hps > by_n.get(n, 0.0):
                by_n[n] = hps
        return by_n

    d, c = best(rows), best(cpu_rows)
    shared = sorted(set(d) & set(c))
    be = break_even([(n, d[n]) for n in shared],
                    [(n, c[n]) for n in shared]) if len(shared) > 1 else None
    if be:
        print("      %s" % ("-" * 52))
        cpuk = cpu_rows[0]["kernel"]
        thr = cpu_rows[0]["threads"]
        if be["verdict"] == "never":
            print("      break-even: never -- the device computes an iteration"
                  " slower than %s CPU threads on %s" % (thr, cpuk))
        elif be["verdict"] == "always":
            print("      break-even: offload wins at every iteration count"
                  " (vs %s on %s threads)" % (cpuk, thr))
        else:
            print("      break-even N = %.2f iterations   (offload pays from"
                  " %d up, vs %s on %s threads)"
                  % (be["n_break_even"], max(1, math.ceil(be["n_break_even"])),
                     cpuk, thr))
        print("      %.3f us/iteration device vs %.3f us CPU  (%.2fx compute"
              " advantage), %.3f us fixed device cost per hash"
              % (be["device_per_iter_s"]*1e6, be["cpu_per_iter_s"]*1e6,
                 be["compute_advantage"], be["device_fixed_s"]*1e6))
        r2d, r2c = be["device_fit"]["r2"], be["cpu_fit"]["r2"]
        print("      fit r2 = %.4f device, %.4f CPU over %d points"
              % (r2d, r2c, len(shared)))
        for w in be["warnings"]:
            print("      *** %s ***" % w)
else:
    print("      (no CPU rows -- break-even not computed)")
PY
        fi
    done
fi

# ------------------------------------- P2: resident baseline + stream counts

if [ "$NDEV" -gt 0 ]; then
    say "P2  resident baseline, all algorithms x all device stream counts"
    note "tests whether 'one stream is right on a device' survives a change of"
    note "vendor and OpenCL compiler, or was an Intel iGPU artifact."
    KS=""
    for alg in md5 sha1 sha512; do
        for s in 1 2 3 4; do KS="$KS${KS:+,}$alg/ocl-s$s"; done
    done
    $SW --algorithm md5,sha1,sha512 --kernel "$KS" --transfer resident \
        --working-set-kb 65536 --csv "$OUT/p2-device-streams.csv" >> "$LOG" 2>&1
    [ -s "$OUT/p2-device-streams.csv" ] && \
        python3 -c "
import csv,sys
for r in csv.DictReader(open('$OUT/p2-device-streams.csv')):
    print('      %-16s %10.2f MH/s  CoV %5s%%' % (r['kernel'], float(r['hashes_per_sec'])/1e6, r['cov_percent']))
" | tee -a "$LOG"
fi

# ------------------------------------------------------ P3: CPU baseline

say "P3  CPU baseline on this host"
note "gives the CPU:GPU advantage ratio on this silicon, and picks up SHA-NI"
note "if the CPU has it."
# --where cpu, or this is not a CPU baseline at all: autotune ranks device and
# CPU kernels together, so on the very machine this script exists for, both
# rows below would have reported the GPU.
$SW --algorithm md5,sha1,sha512 --transfer resident --threads 1 --where cpu \
    --working-set-kb 8192 --csv "$OUT/p3-cpu-1thread.csv" >> "$LOG" 2>&1
$SW --algorithm md5,sha1,sha512 --transfer resident --where cpu \
    --working-set-kb 8192 --csv "$OUT/p3-cpu-allthreads.csv" >> "$LOG" 2>&1
for f in p3-cpu-1thread p3-cpu-allthreads; do
    [ -s "$OUT/$f.csv" ] && python3 -c "
import csv
print('      $f')
for r in csv.DictReader(open('$OUT/$f.csv')):
    print('        %-8s %-16s %10.2f MH/s' % (r['algorithm'], r['kernel'], float(r['hashes_per_sec'])/1e6))
" | tee -a "$LOG"
done

if grep -q sha_ni /proc/cpuinfo 2>/dev/null; then
    note "SHA-NI present -- measuring the fixed-function vs SIMD ratio"
    $SW --algorithm sha1 --threads 1 --working-set-kb 8192 \
        --kernel sha1/shani-s1,sha1/shani-s2,sha1/shani-s3,sha1/shani-s4,sha1/avx2-s3,sha1/avx2-s4,sha1/avx512-s2 \
        --csv "$OUT/p3-shani.csv" >> "$LOG" 2>&1
    [ -s "$OUT/p3-shani.csv" ] && python3 -c "
import csv
for r in csv.DictReader(open('$OUT/p3-shani.csv')):
    print('        %-16s %10.2f MH/s' % (r['kernel'], float(r['hashes_per_sec'])/1e6))
" | tee -a "$LOG"
fi

# ------------------------------------------------- P4: memory / working set

if [ "$NDEV" -gt 0 ]; then
    say "P4  working-set sweep on the device (memory axis)"
    $SW --algorithm md5 --kernel md5/ocl-s1 --transfer resident \
        --working-set-kb "$WS_AXIS" --csv "$OUT/p4-workingset.csv" >> "$LOG" 2>&1
    note "wrote p4-workingset.csv"
fi

# ------------------------------------------------------- P5: multi-device

if [ "$NDEV" -gt 1 ]; then
    say "P5  multi-device  ($NDEV devices)"
    note "the slicing path has never run with more than one device; the"
    note "checksum must match the single-device value exactly."
    "$BIN" --json --kernel md5/ocl-s1 --device 0 --working-set-kb 65536 \
        --samples "$SAMPLES" --time-ms "$TIME_MS" > "$OUT/p5-one-device.json" 2>>"$LOG"
    "$BIN" --json --kernel md5/ocl-s1 --device all --working-set-kb 65536 \
        --samples "$SAMPLES" --time-ms "$TIME_MS" > "$OUT/p5-all-devices.json" 2>>"$LOG"
    python3 - "$OUT/p5-one-device.json" "$OUT/p5-all-devices.json" <<'PY' | tee -a "$LOG"
import json, sys
try:
    a = json.load(open(sys.argv[1])); b = json.load(open(sys.argv[2]))
except Exception as e:
    print("      could not compare: %s" % e); raise SystemExit
ca, cb = a["verification"]["checksum"], b["verification"]["checksum"]
print("      1 device   %8.2f MH/s  %s" % (a["result"]["median"]/1e6, ca[:16]))
print("      %d devices  %8.2f MH/s  %s" % (b["device"]["device_count"] if "device_count" in b.get("device",{}) else 0,
                                            b["result"]["median"]/1e6, cb[:16]))
print("      checksums %s" % ("MATCH" if ca == cb else "*** DIFFER -- slicing is wrong ***"))
print("      scaling   %.2fx" % (b["result"]["median"]/a["result"]["median"]))
PY
fi

# ---------------------------------------------------------------- provenance

say "packaging"
{
    echo "valubench GPU session"
    echo "commit:  $(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo 'not a git repo')"
    echo "started: $(date -u -d "@$START" 2>/dev/null || date -u)"
    echo "ended:   $(date -u)"
    echo "quick:   $QUICK"
    echo "xfer ws: $XFER_WS KiB"
    echo "devices: $NDEV"
} > "$OUT/session.txt"
ls -la "$OUT" | tee -a "$LOG"
