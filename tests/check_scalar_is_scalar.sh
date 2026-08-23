#!/bin/sh
#
# check_scalar_is_scalar.sh -- the scalar kernel must contain no vector code.
#
# This is free and unencumbered software released into the public domain.
# See LICENSE.
#
# The scalar rung is the denominator of every ISA ratio this project reports,
# so it has to be scalar. It silently was not: at -O2 gcc fuses the independent
# streams with SLP vectorisation and emits SSE2 on x86-64 and NEON on AArch64 --
# 88% and 79% of instructions respectively in the two-stream kernel, using two
# of four lanes. Clang does not do it at all, so the same source produced a
# different baseline depending on the compiler, which is worse than either
# behaviour on its own.
#
# The Makefile builds this translation unit with -fno-tree-vectorize and
# -fno-tree-slp-vectorize. This checks the result rather than trusting the flag,
# because a compiler that stops honouring the spelling would put the old
# behaviour back with no other symptom than numbers that quietly improve.
#
# Usage: check_scalar_is_scalar.sh <object-file> [objdump]

set -eu

OBJ=${1:?usage: $0 <object-file> [objdump]}
OBJDUMP=${2:-objdump}

if ! command -v "$OBJDUMP" >/dev/null 2>&1; then
    echo "  skip  scalar-purity  ($OBJDUMP not found)"
    exit 0
fi
[ -f "$OBJ" ] || { echo "  skip  scalar-purity  ($OBJ not built)"; exit 0; }

# Vector register spellings: x86 %xmm/%ymm/%zmm, AArch64 v0.4s / q0 / d0.
# A handful of hits is normal -- the prologue zeroes a register and the ABI
# moves things around -- so the test is a proportion, not a count.
"$OBJDUMP" -d --no-show-raw-insn "$OBJ" | awk '
    /^[0-9a-f]+ </ { fn = $2; gsub(/[<>:]/, "", fn); next }
    /^[ \t]+[0-9a-f]+:/ {
        if (fn !~ /scalar/) next
        total[fn]++
        if ($0 ~ /%[xyz]mm[0-9]/ || $0 ~ /[ \t,]v[0-9]+\.[0-9]*[bhsd]/ ||
            $0 ~ /[ \t,]q[0-9]+/) vec[fn]++
    }
    END {
        bad = 0
        for (f in total) {
            pct = total[f] ? 100.0 * vec[f] / total[f] : 0
            if (pct > 5.0) {
                printf "  FAIL  %-24s %5.1f%% vector instructions (%d of %d)\n",
                       f, pct, vec[f], total[f]
                bad = 1
            }
        }
        if (bad) {
            print ""
            print "  The scalar kernel was vectorised by the compiler. Every ISA"
            print "  ratio divides by this kernel, so a vector scalar rung makes"
            print "  those ratios meaningless. Check KFLAGS_scalar in the Makefile."
            exit 1
        }
        n = 0; for (f in total) n++
        printf "  ok    scalar-purity  (%d kernels, no vector instructions)\n", n
    }'
