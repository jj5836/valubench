#!/bin/sh
#
# check_output_contract.sh -- the machine-readable output must be machine-readable.
#
# This is free and unencumbered software released into the public domain.
# See LICENSE.
#
# Every other test here checks that the numbers are right. This one checks that
# they can be read at all, which nothing did: CI built the binary, ran the
# kernel checks and never once parsed the JSON the tool exists to emit, nor
# exercised a single documented exit code.
#
# Two defects lived in that gap for months and were found by review rather than
# by testing:
#
#   - the energy `sources` array separated elements on the loop index rather
#     than on what had been emitted, so a machine whose first power source was
#     invalid produced "sources": [, {...}] -- unparseable;
#   - seven options were parsed with atoi(), so `--threads abc` ran on one
#     thread and reported it as though it had been requested.
#
# So: parse every document, check every exit code, and confirm the tools that
# consume the contract can still drive the binary.
#
# Usage: check_output_contract.sh <binary> [srcdir]

set -eu

BIN=${1:?usage: $0 <binary> [srcdir]}
SRC=${2:-.}
[ -x "$BIN" ] || { echo "  skip  output-contract  ($BIN not built)"; exit 0; }

fail=0
pass=0

# ---- every JSON document the tool can emit must parse -----------------------
#
# One run per document shape, plus a couple of parameter combinations, because
# the shapes differ: a device run carries a "device" object a CPU run does not,
# and an iterated run reaches padding paths a single-iteration one does not.
check_json() {
    desc=$1; shift
    out=$("$@" 2>/dev/null || true)
    if printf '%s' "$out" | python3 -c 'import json,sys; json.load(sys.stdin)' 2>/dev/null; then
        pass=$((pass + 1))
    else
        printf '  FAIL  output-contract  %s: not valid JSON\n' "$desc"
        printf '%s' "$out" | head -3 | sed 's/^/          /'
        fail=1
    fi
}

check_json "result, defaults"      "$BIN" --json --samples 2 --time-ms 30 --warmup-ms 30
check_json "result, sha512"        "$BIN" --json --algorithm sha512 --samples 2 --time-ms 30 --warmup-ms 30
check_json "result, iterated"      "$BIN" --json --algorithm sha1 --message-bytes 64 --iterations 4 \
                                        --samples 2 --time-ms 30 --warmup-ms 30
check_json "result, all threads"   "$BIN" --json --threads 2 --samples 2 --time-ms 30 --warmup-ms 30
check_json "capabilities"          "$BIN" --list --json
check_json "devices"               "$BIN" --list-devices --json

# ---- the capability dump is a contract, so check its shape ------------------
if "$BIN" --list --json 2>/dev/null | python3 -c '
import json, sys
d = json.load(sys.stdin)
need = ("schema", "benchmark", "algorithms", "kernels", "limits",
        "exit_codes", "transfer_modes")
missing = [k for k in need if k not in d]
if missing:
    print("  FAIL  output-contract  capabilities missing: %s" % ", ".join(missing))
    raise SystemExit(1)
if not str(d["schema"]).startswith("valubench/capabilities/"):
    print("  FAIL  output-contract  capabilities schema is %r" % d["schema"])
    raise SystemExit(1)
for k in d["kernels"]:
    for f in ("name", "isa", "algorithm", "lanes", "streams", "where", "available"):
        if f not in k:
            print("  FAIL  output-contract  kernel row missing %s: %r" % (f, k))
            raise SystemExit(1)
' ; then pass=$((pass + 1)); else fail=1; fi

# ---- the documented exit codes must be the ones actually used ---------------
#
# usage=2 and noisy=3 are reachable from the command line. verify_failed=1 is
# not: producing it means breaking a kernel, which the kernel tests cover.
expect_exit() {
    want=$1; desc=$2; shift 2
    "$@" >/dev/null 2>&1 && got=0 || got=$?
    if [ "$got" = "$want" ]; then
        pass=$((pass + 1))
    else
        printf '  FAIL  output-contract  %s: expected exit %s, got %s\n' "$desc" "$want" "$got"
        fail=1
    fi
}

expect_exit 2 "unknown option"        "$BIN" --no-such-option
expect_exit 2 "missing argument"      "$BIN" --threads
expect_exit 2 "non-numeric argument"  "$BIN" --threads abc
expect_exit 2 "trailing garbage"      "$BIN" --message-bytes 12x
expect_exit 2 "negative"              "$BIN" --threads -4
expect_exit 2 "out of range"          "$BIN" --samples 0
expect_exit 2 "unknown algorithm"     "$BIN" --algorithm nosuchalg
# A valid run exits 0, or 3 if the machine was too noisy to trust the number.
# Both mean it ran; only 1 and 2 mean it did not. Asserting 0 here would make
# this test flaky on precisely the shared, contended runners CI uses -- which is
# the same reason the tool reports noise instead of hiding it.
"$BIN" --samples 5 --time-ms 120 --warmup-ms 120 >/dev/null 2>&1 && rc=0 || rc=$?
if [ "$rc" = 0 ] || [ "$rc" = 3 ]; then
    pass=$((pass + 1))
else
    printf '  FAIL  output-contract  a valid run exited %s (want 0 or 3)\n' "$rc"
    fail=1
fi

# ---- a rejected argument must not be silently substituted -------------------
#
# The atoi defect: the run proceeded and the JSON recorded the substituted
# value, so the failure was invisible in the artifact it produced.
if "$BIN" --threads abc --json >/dev/null 2>&1; then
    echo "  FAIL  output-contract  --threads abc was accepted"
    fail=1
else
    pass=$((pass + 1))
fi

# ---- the tools that consume the contract must still drive the binary --------
if [ -f "$SRC/tools/sweep.py" ]; then
    tmp=$(mktemp -d)
    if python3 "$SRC/tools/sweep.py" --bin "$BIN" --algorithm md5 \
            --kernel md5/scalar-s1,md5/scalar-s2 --threads 1 \
            --samples 2 --time-ms 30 --warmup-ms 30 -q --csv "$tmp/a.csv" >/dev/null 2>&1 \
       && [ -s "$tmp/a.csv" ] \
       && python3 -c '
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
assert len(rows) == 2, "expected 2 rows, got %d" % len(rows)
for r in rows:
    assert r["checksum"], "no checksum recorded"
    assert r["verified"] == "true", "row not verified"
' "$tmp/a.csv" 2>/dev/null; then
        pass=$((pass + 1))
    else
        echo "  FAIL  output-contract  sweep.py could not drive the binary"
        fail=1
    fi
    rm -rf "$tmp"
fi

[ "$fail" = 0 ] || exit 1
printf '  ok    output-contract  (%d checks: JSON parses, exit codes, tooling)\n' "$pass"
