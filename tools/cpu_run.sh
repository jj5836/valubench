#!/usr/bin/env bash
#
# cpu_run.sh -- one-command capture for a time-boxed session on a CPU instance.
#
# This is free and unencumbered software released into the public domain.
# See LICENSE.
#
# The CPU-only sibling of gpu_run.sh, and written for the same situation: the
# clock is running on rented hardware and you want data rather than a shell
# history. Same discipline -- capture the environment, gate on correctness, run
# the matrix in priority order, leave one tarball -- and the same promise, that
# if the session is cut short the phases that finished are the ones that
# mattered most.
#
#   ./tools/cpu_run.sh              full run, roughly 15-25 min
#   ./tools/cpu_run.sh -q           quick pass, roughly 4 min
#   ./tools/cpu_run.sh --skip-check skip 'make check' (the gate -- not advised)
#
# On a fresh Ubuntu instance, disable the background upgrader first:
#
#   sudo systemctl disable --now unattended-upgrades
#
# It can install a kernel and reboot the machine mid-run. See CONTRIBUTING.md.
#
# WHAT THIS TRIP IS FOR, in the order the phases run:
#
#   P1  The AVX-512 ratio on a full-width datapath. docs/research.md 4.1 measured
#       2.20x on Cascade Lake and explained the shortfall against a 2.67x
#       instruction reduction as issue width -- ports 0 and 1 fusing to serve
#       one 512-bit unit. A part with a genuine 512-bit datapath is the test of
#       that explanation, and it has never been run.
#
#   P2  Licence-based downclocking. TODO item 1 wants it and no machine has been
#       able to answer: the only AVX-512 host so far was a VM with no cpufreq
#       interface. This needs bare metal, and it needs the clock sampled *during*
#       the run -- valubench itself reads frequency once at startup, so the
#       sampling lives here rather than in the binary.
#
#   P3  Energy per hash, AVX-512 against AVX2. The other half of TODO item 1.
#       512-bit work draws more power, so the throughput ratio and the energy
#       ratio can disagree, and only one of them is a purchasing argument.
#
#   P4  The SHA-NI ratio on a Zen part. TODO item 5 says the 2.13x measured on
#       Gracemont is flattered by that core's 128-bit vector datapath and must
#       not be generalised until a P-core or a Zen part has run it. AMD has had
#       SHA-NI since Zen 1, so this closes the item.
#
#   P5  AMD validation and the algorithm baseline. No AMD part has ever run this
#       benchmark: every kernel passing its reference check here is the result,
#       and the checksums matching the values from Intel hosts is the stronger
#       one -- the XOR fingerprint is supposed to be machine-independent.
#
#   P6  Stream interleaving, for every algorithm on every ISA the machine has.
#       Worth 3.1x on the N100's scalar path and the single largest effect in
#       the benchmark. It is also the phase that has twice found something
#       unexpected: a scalar-s2 dip that shows up on both Zen 5 and Neoverse V1,
#       and a best stream count on AArch64 that runs s4/s2/s1 across
#       md5/sha1/sha512 where every x86 part wants three or four for all three.
#
#   P7  Message length and working set on the best MD5 kernel. Both should leave
#       compressions/sec flat; a machine where one of them does not is the
#       finding. Cheap enough that there is no reason to leave it out.

set -u

OUT=""
QUICK=0
SKIP_CHECK=0

usage() {
    cat <<EOF
Usage: $0 [options]

  -o DIR          output directory (default: results-<host>-<timestamp>)
  -q, --quick     smaller grids and fewer samples, for a first pass
      --skip-check  skip 'make check' (the correctness gate -- not advised)
  -h, --help      this text

Run it on a quiet machine. All-core numbers are the least trustworthy thing
here; the single-thread ISA ladder in P1 is the headline and is stable.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -o) OUT="${2:-}"; shift 2 ;;
        -q|--quick) QUICK=1; shift ;;
        --skip-check) SKIP_CHECK=1; shift ;;
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

# The log is written first and the terminal second, deliberately. Piping this
# script into anything that exits early -- `| head`, or an ssh session that
# drops -- closes stdout, and a `tee` would then take SIGPIPE and stop writing
# the file too. On a metered box the transcript is the thing you paid for, so it
# must not depend on anyone still watching. Run under tmux or nohup regardless.
# Everything is written to the log first and shown second, and the showing is
# done in a subshell so it can die without taking the script with it.
#
# The reason is SIGPIPE. Pipe this script into anything that exits early -- a
# `| head`, or an ssh session that drops -- and the next write to stdout kills
# the shell outright; redirecting stderr does not help, because it is a signal
# rather than an error. On a metered box the transcript is the thing you paid
# for, so it must not depend on anyone still watching. Run under tmux or nohup
# anyway.
emit() { printf '%s\n' "$1" >> "$LOG"; ( printf '%s\n' "$1" ) 2>/dev/null || true; }
say()  { emit "$(printf '\n=== [%s] %s' "$(elapsed)" "$*")"; }
note() { emit "$(printf '    %s' "$*")"; }
# For command output: same guarantee, for anything that used to go through tee.
show() { local t; t=$(cat); emit "$t"; }

package() {
    tar czf "$OUT.tar.gz" -C "$(dirname "$OUT")" "$(basename "$OUT")" 2>/dev/null
    printf '\n=== [%s] done\n' "$(elapsed)"
    printf '    %s\n' "$OUT.tar.gz"
    printf '    copy it off with:  scp %s:%s .\n' \
           "$(hostname -s 2>/dev/null || echo HOST)" "$OUT.tar.gz"
}
trap package EXIT INT TERM

if [ "$QUICK" = 1 ]; then
    SAMPLES=3; TIME_MS=100; WARMUP=150; FREQ_SECS=6
else
    SAMPLES=9; TIME_MS=200; WARMUP=400; FREQ_SECS=20
fi

SW="python3 $SWEEP --samples $SAMPLES --time-ms $TIME_MS --warmup-ms $WARMUP
    --keep-going -q"

NPROC=$(nproc 2>/dev/null || echo 1)

# ---------------------------------------------------------------- environment

say "environment"
{
    echo "# date";  date -u
    echo; echo "# uname"; uname -a
    echo; echo "# instance"
    TOK=$(curl -sX PUT "http://169.254.169.254/latest/api/token" \
          -H "X-aws-ec2-metadata-token-ttl-seconds: 60" 2>/dev/null)
    curl -s -H "X-aws-ec2-metadata-token: $TOK" \
         http://169.254.169.254/latest/meta-data/instance-type 2>/dev/null || echo "(not EC2)"
    echo; echo; echo "# cpu"; lscpu 2>/dev/null
    echo; echo "# flags of interest"
    # The two families spell their capability lists differently and share no
    # names, so asking for the x86 set on an AArch64 part prints six MISSINGs
    # and says nothing about the part you are actually on.
    case "$(uname -m)" in
        aarch64|arm64) FLAGS="asimd sve sve2 sha1 sha2 sha512" ;;
        *)             FLAGS="sse2 avx2 avx512f avx512dq avx512bw sha_ni" ;;
    esac
    for f in $FLAGS; do
        grep -qm1 " $f" /proc/cpuinfo && echo "  $f" || echo "  $f MISSING"
    done
    echo; echo "# cpufreq"
    cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver 2>/dev/null || echo "  (no cpufreq)"
    cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort | uniq -c
    echo; echo "# powercap (energy)"
    ls /sys/class/powercap/ 2>/dev/null || echo "  (none)"
    for e in /sys/class/powercap/*/; do
        [ -r "$e/name" ] && printf '  %s = %s readable=%s\n' \
            "$(basename "$e")" "$(cat "$e/name" 2>/dev/null)" \
            "$( [ -r "$e/energy_uj" ] && echo yes || echo NO)"
    done
    echo; echo "# background upgrader"
    printf '  unattended-upgrades: %s\n' \
        "$(systemctl is-active unattended-upgrades 2>/dev/null || echo unknown)"
    echo; echo "# memory"; free -m 2>/dev/null | head -2
    echo; echo "# load"; uptime
    echo; echo "# compiler"; ${CC:-cc} --version 2>/dev/null | head -1
} > "$OUT/environment.txt" 2>&1

note "$(grep -m1 'Model name' "$OUT/environment.txt" | cut -c1-72)"
note "$NPROC logical CPUs"
grep -q '^  avx512f$' "$OUT/environment.txt" && HAVE512=1 || HAVE512=0
grep -q '^  sha_ni$'  "$OUT/environment.txt" && HAVESHA=1 || HAVESHA=0
note "avx512f: $([ $HAVE512 = 1 ] && echo yes || echo NO -- P1..P3 will be thin)"
note "sha_ni:  $([ $HAVESHA = 1 ] && echo yes || echo no -- P4 skipped)"

# A rented instance that reboots mid-run looks like a network fault from the
# other end, so record the one thing that reboots rented instances.
if systemctl is-active --quiet unattended-upgrades 2>/dev/null; then
    note "unattended-upgrades is ACTIVE and can reboot this machine mid-run:"
    note "  sudo systemctl disable --now unattended-upgrades"
fi

# Energy needs the counters readable; harmless if they are not there.
sudo chmod a+r /sys/class/powercap/*/energy_uj 2>/dev/null || true
HAVE_RAPL=0
for e in /sys/class/powercap/intel-rapl:*/energy_uj \
         /sys/class/powercap/amd-rapl:*/energy_uj; do
    [ -r "$e" ] && HAVE_RAPL=1
done
if [ "$HAVE_RAPL" = 0 ] && ls /sys/class/powercap/ >/dev/null 2>&1; then
    note "no readable {intel,amd}-rapl:* energy counter."
    note "  src/power.c accepts those two prefixes. If environment.txt shows"
    note "  powercap entries under some other name, P3 will report nothing and"
    note "  the fix is one more prefix in that filter."
fi

# ------------------------------------------------------------------- build

say "build"
make -C "$REPO" -s clean >/dev/null 2>&1
if ! make -C "$REPO" -j"$NPROC" > "$OUT/build.log" 2>&1; then
    note "BUILD FAILED -- see build.log"; tail -20 "$OUT/build.log" | show; exit 1
fi
make -C "$REPO" config > "$OUT/make-config.txt" 2>&1
"$BIN" --list       > "$OUT/kernels.txt" 2>&1
"$BIN" --list --json > "$OUT/capabilities.json" 2>&1
note "kernels registered: $(grep -c 'yes$' "$OUT/kernels.txt") available"

if [ "$SKIP_CHECK" = 0 ]; then
    VENDOR=$(grep -m1 vendor_id /proc/cpuinfo 2>/dev/null | awk '{print $3}')
    if [ "${VENDOR:-}" = "AuthenticAMD" ]; then
        say "correctness gate  (no AMD part has ever run this -- the gate is a result)"
    else
        say "correctness gate"
    fi
    if make -C "$REPO" check > "$OUT/check.log" 2>&1; then
        note "$(grep -E '^[0-9]+ checks' "$OUT/check.log" | tail -1)"
    else
        note "CHECK FAILED -- every number below is suspect. Continuing anyway;"
        note "the failure is the result in that case. See check.log"
        grep -E "FAIL|failures" "$OUT/check.log" | head -20 | show
    fi
else
    say "correctness gate SKIPPED by request"
fi

# ------------------------------------------------- P1: the ISA ladder (headline)

say "P1  ISA ladder, single thread  (the reason for this session)"
note "md5 across every ISA this machine has, at every stream count."

# The rungs come from the binary rather than from a list written here. A
# hardcoded x86 ladder degrades to a single scalar row on an AArch64 part --
# silently, because --keep-going treats the absent kernels as skips -- and the
# ladder is the one phase that most wants to run on an unfamiliar machine.
ISAS=$(python3 - "$OUT/capabilities.json" <<'PY'
import json, sys
ORDER = ["scalar", "sse2", "avx2", "avx512", "neon", "sve", "sve2"]
seen = []
for k in json.load(open(sys.argv[1]))["kernels"]:
    alg, _, rest = k["name"].partition("/")
    # where == "device" is the OpenCL path. It belongs in the GPU phases of
    # gpu_run.sh, not in a single-thread ISA ladder: it would sit at the top of
    # the table comparing a 24-EU iGPU against a scalar loop, and P7 would then
    # pick it as "the best MD5 kernel" and measure the device's axes instead of
    # the core's.
    if alg != "md5" or not k.get("available") or k.get("where") != "cpu":
        continue
    isa = rest.split("-")[0]
    if isa not in seen:
        seen.append(isa)
seen.sort(key=lambda i: (ORDER.index(i) if i in ORDER else len(ORDER), i))
print(" ".join(seen))
PY
)
note "rungs on this machine: ${ISAS:-none}"

KS=""
for isa in $ISAS; do
    for s in 1 2 3 4; do KS="$KS${KS:+,}md5/$isa-s$s"; done
done
$SW --algorithm md5 --kernel "$KS" --threads 1 --message-bytes 55 \
    --csv "$OUT/p1-isa-ladder.csv" >> "$LOG" 2>&1

if [ -s "$OUT/p1-isa-ladder.csv" ]; then
    python3 - "$OUT/p1-isa-ladder.csv" <<'PY' | show
import csv, sys
rows = [r for r in csv.DictReader(open(sys.argv[1])) if r["hashes_per_sec"]]
best, order = {}, []
for r in rows:
    isa = r["kernel"].split("/")[1].split("-")[0]
    v = float(r["hashes_per_sec"]) / 1e6
    if isa not in order:
        order.append(isa)
    if v > best.get(isa, (0, ""))[0]:
        best[isa] = (v, r["kernel"], float(r["cov_percent"]))
print("      %-10s %10s  %-16s %8s" % ("isa", "MH/s", "best kernel", "CoV%"))
for isa in order:
    v, k, c = best[isa]
    print("      %-10s %10.2f  %-16s %7.2f%%" % (isa, v, k, c))

# Prior measurements, so a number that disagrees with the record is visible
# here rather than three days later in a spreadsheet.
PRIOR = [("avx512", "avx2",   "Cascade Lake VM measured 2.20x"),
         ("avx2",   "sse2",   "N100 measured ~1.06x, 128-bit datapath"),
         ("neon",   "scalar", "no prior figure -- this is the first"),
         ("sve",    "neon",   "no prior figure -- this is the first")]
lines = ["%s / %s = %.2fx" % (hi.upper(), lo.upper(), best[hi][0] / best[lo][0])
         + "  (%s)" % why
         for hi, lo, why in PRIOR if hi in best and lo in best]
if not lines and len(order) > 1:
    hi, lo = order[-1], order[0]
    lines = ["%s / %s = %.2fx" % (hi.upper(), lo.upper(),
                                  best[hi][0] / best[lo][0])]
if lines:
    print("      %s" % ("-" * 52))
    for line in lines:
        print("      %s" % line)
PY
fi

# --------------------------------------------- P2: frequency under 512-bit load

say "P2  licence downclocking  (clock sampled during the run, not before it)"
if [ ! -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq ]; then
    note "no scaling_cur_freq -- cannot observe the clock. This is what a VM"
    note "looked like, and the reason this phase wants bare metal."
else
    sample_freq() {                      # $1 = label, $2 = kernel
        local out="$OUT/p2-freq-$1.txt"
        ( while :; do
              cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
              sleep 0.25
          done ) > "$out" 2>/dev/null &
        local sampler=$!
        "$BIN" --kernel "$2" --threads 1 --samples "$SAMPLES" \
               --time-ms $(( FREQ_SECS * 1000 / SAMPLES )) --warmup-ms 200 \
               > "$OUT/p2-run-$1.txt" 2>&1
        kill $sampler 2>/dev/null; wait $sampler 2>/dev/null
        awk '{ s+=$1; n++; if (min==""||$1<min) min=$1; if ($1>max) max=$1 }
             END { if (n) printf "      %-10s mean %.0f MHz   min %.0f   max %.0f   (%d samples)\n",
                          "'"$1"'", s/n/1000, min/1000, max/1000, n }' "$out" | show
    }
    note "one core loaded, sampling cpu0 every 250 ms for ~${FREQ_SECS}s per kernel."
    note "scalar is the non-vector baseline; the question is whether 512-bit"
    note "work sits below it and below avx2."
    sample_freq scalar md5/scalar-s4
    sample_freq avx2   md5/avx2-s3
    [ "$HAVE512" = 1 ] && sample_freq avx512 md5/avx512-s2
    note "a lower mean under avx512 than avx2 is the downclock; equal means none"
fi

# ------------------------------------------------------------- P3: energy

say "P3  energy per hash, AVX-512 against AVX2"
if [ "$HAVE_RAPL" = 0 ]; then
    note "no readable energy counter -- skipped. See the note in the environment"
    note "section above; on AMD this may be a naming mismatch rather than absence."
else
    for k in md5/avx2-s3 md5/avx512-s2; do
        [ "$HAVE512" = 0 ] && [ "${k#*avx512}" != "$k" ] && continue
        tag=$(echo "$k" | tr '/' '-')
        "$BIN" --kernel "$k" --threads 1 --json --samples "$SAMPLES" \
               --time-ms "$TIME_MS" --warmup-ms "$WARMUP" \
               > "$OUT/p3-energy-$tag.json" 2>&1
    done
    python3 - "$OUT"/p3-energy-*.json <<'PY' | show
import json, sys
print("      %-16s %10s %12s %10s" % ("kernel", "MH/s", "kH/J", "watts"))
for p in sys.argv[1:]:
    try:
        d = json.load(open(p))
    except Exception:
        continue
    e = d.get("energy", {})
    if not e.get("available"):
        print("      %-16s %10.2f   (no energy: %.60s)"
              % (d["kernel"]["name"], d["result"]["median"]/1e6,
                 e.get("reason", "unknown")))
        continue
    print("      %-16s %10.2f %12.0f %10.1f"
          % (d["kernel"]["name"], d["result"]["median"]/1e6,
             e.get("hashes_per_joule", 0)/1e3, e.get("cpu_package_watts", 0)))
PY
fi

# ------------------------------------------------------- P4: SHA-NI on this core

if [ "$HAVESHA" = 1 ]; then
    say "P4  SHA-NI against integer SIMD  (closes a generalisation TODO item)"
    note "N100 measured 2.13x, flattered by a 128-bit vector datapath."
    KS="sha1/shani-s1,sha1/shani-s2,sha1/shani-s3,sha1/shani-s4,sha1/avx2-s3,sha1/avx2-s4"
    [ "$HAVE512" = 1 ] && KS="$KS,sha1/avx512-s2,sha1/avx512-s3"
    $SW --algorithm sha1 --kernel "$KS" --threads 1 \
        --csv "$OUT/p4-shani.csv" >> "$LOG" 2>&1
    [ -s "$OUT/p4-shani.csv" ] && python3 - "$OUT/p4-shani.csv" <<'PY' | show
import csv, sys
rows = [r for r in csv.DictReader(open(sys.argv[1])) if r["hashes_per_sec"]]
ni   = max((float(r["hashes_per_sec"]) for r in rows if "shani" in r["kernel"]), default=0)
simd = max((float(r["hashes_per_sec"]) for r in rows if "shani" not in r["kernel"]), default=0)
for r in sorted(rows, key=lambda r: -float(r["hashes_per_sec"])):
    print("      %-16s %10.2f MH/s" % (r["kernel"], float(r["hashes_per_sec"])/1e6))
if ni and simd:
    print("      %s" % ("-" * 40))
    print("      SHA-NI / best SIMD = %.2fx      (N100 measured 2.13x)" % (ni/simd))
PY
fi

# ------------------------------------- P5: algorithm baseline and the fingerprint

say "P5  all three algorithms: single thread, then all cores"
$SW --algorithm md5,sha1,sha512 --where cpu --threads 1 \
    --csv "$OUT/p5-1thread.csv" >> "$LOG" 2>&1
$SW --algorithm md5,sha1,sha512 --where cpu --threads "$NPROC" \
    --csv "$OUT/p5-allcores.csv" >> "$LOG" 2>&1
for f in p5-1thread p5-allcores; do
    [ -s "$OUT/$f.csv" ] && python3 -c "
import csv,sys
print('      $f')
for r in csv.DictReader(open('$OUT/$f.csv')):
    if r['hashes_per_sec']:
        print('        %-8s %-16s %10.2f MH/s  CoV %5s%%' % (
            r['algorithm'], r['kernel'], float(r['hashes_per_sec'])/1e6, r['cov_percent']))
" | show
done

note "checksum cross-check against known values from Intel hosts:"
python3 - "$OUT/p5-1thread.csv" <<'PY' | show
import csv, sys
# The XOR fingerprint is invariant across machines, lanes, streams and threads.
# A mismatch here is a correctness event, not a performance one.
KNOWN = {"md5-full-55x1": "955e84cbbc05470019604a2bd9ff2821"}
for r in csv.DictReader(open(sys.argv[1])):
    w, c = r.get("workload"), r.get("checksum")
    if w in KNOWN:
        ok = "MATCH" if c == KNOWN[w] else "*** MISMATCH ***"
        print("        %-18s %s  %s" % (w, ok, c[:32]))
PY

# ----------------------------------------------- P6: does interleaving still pay

say "P6  stream interleaving, every algorithm on every ISA"
note "worth 3.1x on the N100 scalar path and the largest single effect measured"
note "there -- but Graviton3 wants s4/s2/s1 for md5/sha1/sha512, so the best"
note "stream count is a property of the (algorithm, core) pair. Sweep all of it."

# md5 alone used to stand for the whole benchmark here. It does not: on AArch64
# md5 climbs to s4 while sha512 is fastest at one stream and loses 21% by four.
# That was found by hand after the capture had finished, which is the argument
# for it being in the capture.
KS=""
for a in md5 sha1 sha512; do
    for isa in $ISAS; do
        for s in 1 2 3 4; do KS="$KS${KS:+,}$a/$isa-s$s"; done
    done
done
$SW --algorithm md5,sha1,sha512 --kernel "$KS" --threads 1 \
    --csv "$OUT/p6-streams.csv" >> "$LOG" 2>&1
[ -s "$OUT/p6-streams.csv" ] && python3 - "$OUT/p6-streams.csv" <<'PY' | show
import csv, sys
rows = [r for r in csv.DictReader(open(sys.argv[1])) if r["hashes_per_sec"]]
by = {}
for r in rows:
    alg, rest = r["kernel"].split("/")
    isa, _, sn = rest.rpartition("-s")
    by.setdefault((alg, isa), {})[int(sn)] = float(r["hashes_per_sec"]) / 1e6
print("      %-8s %-8s %8s %8s %8s %8s   %s"
      % ("alg", "isa", "s1", "s2", "s3", "s4", "best"))
for (alg, isa), v in by.items():
    cells = "".join("%8.2f " % v[s] if s in v else "%8s " % "-"
                    for s in (1, 2, 3, 4))
    bs = max(v, key=lambda s: v[s])
    gain = v[bs] / v[1] if 1 in v and v[1] else 0
    flag = "" if bs == 4 else "   <-- not s4"
    print("      %-8s %-8s %s  s%d, %.2fx over s1%s"
          % (alg, isa, cells, bs, gain, flag))
PY

# ------------------------------------------------ P7: the two cheap axes

say "P7  message length and working set, on the best MD5 kernel"
note "MC/s should be flat in both. Where it is not, the machine is the reason:"
note "the N100 loses 23% of its compression rate at the 55->56 byte boundary"
note "and Graviton3 loses nothing, so that dip is not a property of two-block"
note "messages. Two machines were needed to know that."

BESTK=$(python3 - "$OUT/p1-isa-ladder.csv" 2>/dev/null <<'PY'
import csv, sys
try:
    rows = [r for r in csv.DictReader(open(sys.argv[1])) if r["hashes_per_sec"]]
except OSError:
    rows = []
if rows:
    print(max(rows, key=lambda r: float(r["hashes_per_sec"]))["kernel"])
PY
)
if [ -n "$BESTK" ]; then
    note "using $BESTK"
    $SW --algorithm md5 --kernel "$BESTK" --threads 1 \
        --message-bytes 8,24,55,56,119,247,503,1015,4087 \
        --csv "$OUT/p7-msgsize.csv" >> "$LOG" 2>&1
    $SW --algorithm md5 --kernel "$BESTK" --threads 1 \
        --working-set-kb 64,256,1024,4096,16384,65536 \
        --csv "$OUT/p7-workingset.csv" >> "$LOG" 2>&1
    python3 - "$OUT/p7-msgsize.csv" "$OUT/p7-workingset.csv" <<'PY' | show
import csv, sys
def load(p):
    try:
        return [r for r in csv.DictReader(open(p)) if r["hashes_per_sec"]]
    except OSError:
        return []
rows = load(sys.argv[1])
if rows:
    print("      %8s %6s %10s %10s" % ("bytes", "blocks", "MH/s", "MC/s"))
    mc = []
    for r in rows:
        c = float(r["compressions_per_sec"]) / 1e6
        mc.append(c)
        print("      %8s %6s %10.2f %10.2f"
              % (r["message_bytes"], r["blocks_per_message"],
                 float(r["hashes_per_sec"]) / 1e6, c))
    if mc:
        print("      MC/s spread across the range: %.1f%%"
              % ((max(mc) / min(mc) - 1) * 100))
rows = load(sys.argv[2])
if rows:
    print("      %10s %10s" % ("WS KiB", "MH/s"))
    v = []
    for r in rows:
        h = float(r["hashes_per_sec"]) / 1e6
        v.append(h)
        print("      %10s %10.2f" % (r["working_set_kb"], h))
    if v:
        print("      cache-resident to DRAM costs: %.1f%%"
              % ((1 - min(v) / max(v)) * 100))
PY
else
    note "no P1 CSV to pick a kernel from -- skipped."
fi

say "complete"
