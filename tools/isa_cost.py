#!/usr/bin/env python3
"""Instructions per message, from the generated code.

Static and deterministic, so it costs nothing and cannot be perturbed by load.

DO NOT READ THIS AS A THROUGHPUT PREDICTION. It was used as one, and it was
wrong on both ARM parts:

  Graviton3, SVE-256 vs NEON-128   predicted 1.33x-1.68x   measured 0.52x-1.17x
  Graviton4, SVE2-128 vs NEON-128  predicted 0.91x-1.03x   measured 0.32x-0.91x

Two reasons, both measured on Graviton4 with perf. First, SVE code retires
about half the instructions per cycle that NEON does -- 1.56 against 3.18 --
at one micro-op per instruction either way, so it is an issue-rate limit that
no instruction count can see. Second, comparing each ISA at *its own* best
stream count compares the wrong pairs: the stream count that minimises
instructions is not the one that maximises throughput.

Throughput ratio decomposes as (instruction ratio) x (IPC ratio), and this tool
supplies only the first. On Graviton4 that product came within 2% of measured.
So: use this to understand where instructions go, and to compare an ISA against
itself across a change. To predict throughput, measure IPC too.

A kernel body processes one group of (lanes x streams) messages, so
instructions per message is body / (lanes x streams). For a vector-length
agnostic ISA the body does not change with the vector length -- the same
instructions simply cover more lanes -- so the lane count is supplied rather
than read from the object.

Usage: isa_cost.py <objdir> [objdump]
"""
import re, subprocess, sys, collections

ARGS = [a for a in sys.argv[1:] if not a.startswith("-")]
OBJDIR = ARGS[0] if ARGS else "build-arm64"
OBJDUMP = ARGS[1] if len(ARGS) > 1 else "aarch64-linux-gnu-objdump"

# lanes per vector for 32-bit and 64-bit words, at the width being modelled
WIDTH = {
    "neon": (4, 2, "128-bit, fixed"),
    "sve":  (8, 4, "256-bit, Neoverse V1"),
    "sve2": (4, 2, "128-bit, Neoverse V2"),
}

# movprfx exists because most SVE data instructions are destructive: it copies
# a register so the following instruction can overwrite it. Neoverse is
# documented to fuse the pair at rename, so counting it inflates SVE's apparent
# cost against NEON's three-operand encoding. Both figures are reported: the
# truth is somewhere between, and which end depends on the core.
def body_counts(obj, drop_movprfx=False):
    out = subprocess.run([OBJDUMP, "-d", "--no-show-raw-insn", obj],
                         capture_output=True, text=True).stdout
    res, cur = {}, None
    for ln in out.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", ln)
        if m:
            # GCC emits internal labels inside SVE functions (.SVLPSPL0 and
            # friends, for the lazy-save prologue). They are not function
            # boundaries, and treating them as such truncated every SVE body
            # to its first fourteen instructions.
            if m.group(1).startswith("."):
                continue
            cur = m.group(1); res[cur] = 0; continue
        if cur and re.match(r"^\s+[0-9a-f]+:", ln):
            if drop_movprfx and "movprfx" in ln:
                continue
            res[cur] += 1
    return res

DROP = "--no-movprfx" in sys.argv
counts = {}
for isa in WIDTH:
    try:
        counts[isa] = body_counts("%s/kernel_%s.o" % (OBJDIR, isa), DROP)
    except Exception:
        pass
print("counting movprfx: %s\n" % ("no (assumed fused at rename)" if DROP else "yes"))

print("Instructions per message, from the generated code\n")
for alg, w in (("md5", 32), ("sha1", 32), ("sha512", 64)):
    print("  %s" % alg)
    print("    %-10s %-22s %9s %9s %9s %9s" %
          ("isa", "width", "s1", "s2", "s3", "s4"))
    per = {}
    for isa in ("neon", "sve", "sve2"):
        if isa not in counts:
            continue
        l32, l64, label = WIDTH[isa]
        lanes = l32 if w == 32 else l64
        row = []
        for st in (1, 2, 3, 4):
            sym = "vb_%s_%s_s%d" % (alg, isa, st)
            n = counts[isa].get(sym)
            row.append(n / (lanes * st) if n else None)
        per[isa] = row
        print("    %-10s %-22s %s" % (isa, label,
              " ".join("%9.1f" % v if v else "        -" for v in row)))
    # Matched stream counts only. Comparing each ISA at its own cheapest
    # stream count pairs configurations that no run would ever choose.
    for other, label in (("sve", "SVE  vs NEON"), ("sve2", "SVE2 vs NEON")):
        if "neon" not in per or other not in per:
            continue
        cells = []
        for st in range(4):
            a, b = per["neon"][st], per[other][st]
            cells.append("s%d %.2fx" % (st + 1, a / b) if a and b else "s%d   -" % (st + 1))
        print("    %s, instructions only: %s" % (label, "  ".join(cells)))
    print("    (instructions only -- multiply by the IPC ratio for throughput)")
    print()
