# AGENTS.md

Conventions for AI assistants working in this repository. Human contributors
want [CONTRIBUTING.md](CONTRIBUTING.md), which covers the same ground plus
build and review expectations.

Read this first, then `src/kernels/cpu/matrix.h` — the kernel matrix is the
source of truth for what exists, and 48 of the 64 registered kernels are macro
expansions with no source-level definition to grep for.

## The rules most likely to be broken by accident

Each of these produces a plausible wrong number rather than an error, which is
why they are worth stating before any change:

- **No `-march=native`, no `-flto`.** The first makes results depend on the
  build host; the second could let the compiler fold message words into round
  constants, the exact shortcut this workload excludes.
- **Streams and step lists are expanded, never looped.** A loop that failed to
  unroll would collapse four dependency chains into one and under-report the
  hardware several-fold, silently.
- **Round functions are per ISA.** An optimization that helps AVX2 can be
  pointless or harmful on AVX-512.
- **`src/registry.c` is generated from the matrix** and never hand-edited.

## Measured numbers are tracked outside this repository

There is deliberately no results file in the tree. One existed, accumulated
figures from five machines and three compilers, and became unmaintainable once
a kernel bug invalidated a whole column of it — a durable format for tracking
measurements is still being decided.

Until then: **characterise results qualitatively in prose.** "More than double
the best SIMD path" survives a re-measurement; "2.13x" does not. Where a figure
genuinely carries an argument, state the machine, the compiler and the date
alongside it, and expect to delete it when it goes stale.
Sample output blocks are the one exception: they show the shape of what the tool
prints and their numbers are illustrative rather than maintained.

`tools/compare.py` decides whether a number actually moved. It pairs points by
workload, refuses to compare across a checksum mismatch, and judges a delta
against both the 10% significance bar and the noise the two runs reported.

## The roadmap is updated in the same change as the work

`docs/roadmap.md` is local to the working tree and not published, but it is the
plan of record. When something lands, update it before considering the work
done — it drifted badly once, describing shipped mechanisms as unbuilt.

Strike items through rather than deleting them, with a sentence on what actually
landed. Say what remains, specifically: "done except for X" is only useful if X
is named. New work discovered while implementing goes in as a new item with the
reason it was not obvious in advance.

**Verify before writing "remaining".** Claims about what does not exist age
worst. One item said a flag "pins exactly one" kernel, which had been false
since the commit that added list support and did not update the roadmap.

## Two verification techniques this project relies on

- **Checksum invariance.** The XOR fingerprint is invariant to lanes, streams,
  threads and devices, so `md5-full-55x1` must produce `955e84cb…` before and
  after any change not intended to alter results.
- **Disassembly comparison.** For refactors that should not change generated
  code, compare `objdump -d` before and after with symbol names normalised. This
  caught a rename that silently produced a scalar-only build: it compiled
  cleanly, ran, and reported numbers several-fold low.

## Style

C11, four-space indent, no tabs. `//` for single-line comments, `/* */` for
multi-line. Comments explain why. Do not bulk-convert existing comment style;
change it in code you are already editing.
