# valubench

[![license: Unlicense](https://img.shields.io/badge/license-Unlicense-blue.svg)](LICENSE)

An integer SIMD microbenchmark. It measures how fast hardware executes the
general-purpose integer vector path, using MD5 as the vehicle, and verifies that
the hardware computed the right answer while doing it.

Public domain (Unlicense). Builds with a C11 compiler and make. No configure
step, no network access at build or run time, and the binary links only libc —
OpenCL and NVML are `dlopen`'d, so the same build runs with or without a GPU.

```
sudo apt install build-essential      # that is the whole requirement for CPU
make                                  # or: make CC=clang
./build/valubench
```

GPU and energy support need a few more packages — see
[docs/dependencies.md](docs/dependencies.md), which has copy-paste blocks for fresh
Ubuntu/Debian and RHEL/Fedora/Amazon Linux cloud instances.

## Algorithms

Three, selected with `--algorithm`:

| | digest | block | word | endian | why it is here |
|---|---|---|---|---|---|
| `md5` *(default)* | 4 x 32 | 512 bit | 32 bit | little | no hardware accelerator anywhere |
| `sha1` | 5 x 32 | 512 bit | 32 bit | big | expands its schedule; a different instruction mix |
| `sha512` | 8 x 64 | 1024 bit | 64 bit | big | 64-bit words and a 128-bit length field |

They span the axes that break naive generalisation on purpose. SHA-512 is the
one that forced the harness to be genuinely algorithm-agnostic: adding only
SHA-1 would have left a 32-bit word and a 64-byte block hardcoded, because
SHA-1 shares MD5's geometry.

One invariant survived and the corpus layout rests on it: **every block is
sixteen words**, whatever the word size.

SHA-1 costs more than MD5 because it runs 80 rounds and *expands* its sixteen
message words to eighty rather than permuting them. SHA-512 costs more again:
80 rounds of 64-bit work with four sigma functions, at half the lanes per
register. Throughput for each, and which kernel wins:
[RESULTS.md](RESULTS.md#algorithms).

All three have OpenCL kernels, which makes one comparison possible that a
single-algorithm benchmark would hide — **the GPU's advantage is not uniform**.
Both sides slow down on SHA-512 — AVX2's lanes halve at 64 bits too — but the
GPU gives up substantially more of its relative footing, because consumer GPU
ALUs are 32-bit and every 64-bit add and rotate is emulated. "Is the accelerator
worth it" has no single answer even on fixed hardware:
[the measured ratios](RESULTS.md#gpu-advantage).

## Why MD5 is the default

Not for its cryptographic properties — it has none left. MD5 is used because
**no hardware has MD5 instructions.**

Benchmarking SHA-1 or SHA-256 on a modern CPU measures the SHA-NI fixed-function
unit, not the integer SIMD ALUs — on this machine that unit is worth more than
double the best AVX2 path ([measured](RESULTS.md#shani)), and it is the reason
`--algorithm sha1` ships both kernels side by side rather than one. MD5 has no such accelerator on any architecture,
so it is forced onto the general integer vector path everywhere, which is exactly
what this benchmark is trying to measure. It also happens to lean on the specific
integer capabilities that separate ISA generations — 32-bit rotate and 3-input
boolean logic — which makes it unusually good at exposing them.

> Sample output blocks in this file show the *shape* of what the tool prints.
> Their numbers are illustrative and not maintained; every measured result lives
> in [RESULTS.md](RESULTS.md).

## What the number means

Workload id `md5-full-LxN`: messages of L bytes (`--message-bytes`), N chained
MD5s each (`--iterations`). A message of L bytes occupies `ceil((L+9)/64)` blocks
once padded, and each block is one compression, so

```
compressions/sec = hashes/sec x iterations x blocks_per_message
```

The default is `md5-full-55x1`: 55 bytes is the largest message fitting in a
single block once padded, so one hash is exactly one compression.

All 64 steps run with real message words. There is deliberately **no** constant
folding of `K + w[i]`, no step reversal against a target digest, and no early
exit.

> **Not comparable to figures that take those shortcuts.** An implementation
> using any of the three performs strictly less work per hash it reports, so its
> number is larger and answers a different question. Both are valid measurements;
> they are not the same measurement. See [docs/research.md](docs/research.md) §2.2.
## Verifying the answer, not just timing it

Every kernel XORs together each digest it computes. XOR is commutative and
associative, so that checksum is **invariant to how the work was distributed** —
across lanes, streams, threads, or devices. One expected value therefore
validates every implementation, and the same value appears at any thread count
on any machine.

That gives three things for the price of one:

- **A correctness gate.** Each kernel is checked against an independently
  written scalar reference before it is timed, and re-checked on every timed
  iteration. A mismatch suppresses the result rather than reporting a fast wrong
  answer.
- **A cross-machine fingerprint.** `md5-full-55x1` produces `955e84cb…` on
  Gracemont, Cascade Lake and Zen 5 alike. If two machines disagree, one of them
  is broken.
- **Honest error bars.** Results are medians with a coefficient of variation,
  and the tool exits non-zero when a run is too noisy to trust.

The scalar references are deliberately separate implementations, written from
RFC 1321 and FIPS 180-4 rather than shared with the kernels, so that agreement
between them is evidence rather than a tautology.

## Results

Measured throughput lives in one place, [RESULTS.md](RESULTS.md), and the other
documents link to it rather than restating figures. A sample — MD5, one thread,
on three machines:

| | AMD EPYC 9R45 (Zen 5) | Intel Xeon 4210 | Intel N100 |
|---|---:|---:|---:|
| best kernel | `avx512-s4` | `avx512-s2` | `avx2-s2` |
| MH/s | **344** | 178 | 43 |

Two findings from those runs give the flavour of what the tool is for: AVX-512
is worth [2.72x over AVX2](RESULTS.md#avx512) on a full-width datapath but only
2.20x where the issue ports are shared, and the dedicated SHA-NI unit is
[2.13x faster than the vector path on one core and 0.46x on another](RESULTS.md#shani-zen5)
— the same instructions, opposite conclusions.

## Documentation

| | |
|---|---|
| [docs/guide.md](docs/guide.md) | Full usage: the three axes, sweeping, comparing runs, GPUs, energy, the PCIe crossover |
| [RESULTS.md](RESULTS.md) | Every measured number, with the machine it came from |
| [docs/design.md](docs/design.md) | What this measures, why MD5, and what measurement changed about the plan |
| [docs/research.md](docs/research.md) | The decision log — every design choice with its reasoning, including the ones that turned out wrong |
| [docs/schema.md](docs/schema.md) | The JSON and CSV output contracts, and the compatibility rule |
| [docs/dependencies.md](docs/dependencies.md) | Per-distribution packages, and what each is for |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Building, testing, and adding a kernel or an algorithm |

## Status

Working and verified on x86-64: scalar, SSE2, AVX2, AVX-512 and SHA-NI CPU
kernels, OpenCL device kernels for all three algorithms, resident and streaming
transfer, autotune, statistics, energy where counters allow, JSON and human
output. GCC 13.3 and Clang 18.1.3 both build clean under the full warning set,
pass the suite, and produce identical checksums.

Known gaps, in the order they matter:

- **Device validation is Intel-only.** The OpenCL path has run on an Intel iGPU
  and nowhere else. An integrated part has no PCIe link, so the crossover
  machinery works but has never produced a number that carries a purchasing
  decision. AMD and NVIDIA GPUs are untested.
- **No bare-metal run yet.** Both AVX-512 hosts so far were virtual machines
  with no `cpufreq` and no RAPL, so licence downclocking and energy per hash are
  unmeasured.
- **Overlapped transfer and compute.** Streaming uploads then launches, in
  order. The reported ratio already answers the pipelined question, so this
  concerns achieved throughput rather than correctness of the ratio.
- **SVE and SVE2 have no kernel.** NEON is written, validated and
  [measured on Graviton3](RESULTS.md#neon); the 256-bit SVE the same part offers
  is unused, and the NEON figures suggest there is headroom above them.

## Layout

| Path | What |
|---|---|
| [src/reference/](src/reference/) | Scalar references from RFC 1321 and FIPS 180-4; the correctness oracles every kernel is checked against |
| [src/kernels/cpu/](src/kernels/cpu/) | CPU kernels, one translation unit per ISA — see its [README](src/kernels/cpu/README.md) for how to add one |
| [src/kernels/cpu/md5_kernel_impl.h](src/kernels/cpu/md5_kernel_impl.h) | The multi-way kernel, written once |
| [src/kernels/gpu/](src/kernels/gpu/) | Device kernels: complete, self-contained OpenCL |
| [src/opencl/](src/opencl/) | The host-side OpenCL driver — loader, context, upload, launch. No hash functions |
| [src/bench.c](src/bench.c) | Validation, autotune, timing, statistics |
| [tools/sweep.py](tools/sweep.py) | Walks a parameter grid, writes CSV, solves for the PCIe balance point |
| [tools/compare.py](tools/compare.py) | Diffs two result sets, gated on the verification checksum |
| [tools/gpu_run.sh](tools/gpu_run.sh), [tools/cpu_run.sh](tools/cpu_run.sh) | One-command capture for a time-boxed session on rented hardware |
| [RESULTS.md](RESULTS.md) | Every measured number; the other documents link here |
| [docs/research.md](docs/research.md) | Background research and every design decision, with rationale |
| [docs/design.md](docs/design.md) | What this measures and why, and what measurement changed about the plan |
| [docs/dependencies.md](docs/dependencies.md) | Packages per distro, and the files they must provide |

```
make check           # known-answer vectors + every kernel against its reference
make config          # show what this toolchain can build
make CC=clang        # build with Clang/LLVM instead of GCC
```

`-flto` is deliberately never enabled: it could let the compiler prove
the message corpus is constant and fold message words into the round constants,
the shortcut this workload excludes.

## Licensing note

The MD5 core is written from the RFC 1321 specification, and the round constants
are generated from `T[i] = floor(2^32 * abs(sin(i)))` rather than transcribed. No
code is copied from any existing MD5 implementation. That includes the RFC 1321
reference implementation, which carries an RSA notice, and permissive-licensed
implementations, which require their notice be retained in derivatives —
obligations incompatible with a public domain dedication. See
[docs/research.md](docs/research.md) §2.9.
