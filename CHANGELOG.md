# Changelog

Notable changes. Measured figures are not recorded here; they are tracked
outside this repository while a durable format for them is decided.

## Unreleased

### Fixed

- **The scalar kernel was not scalar.** At `-O2`, GCC's SLP vectoriser fused the
  independent streams and emitted SSE2 on x86-64 and NEON on AArch64 — 88% and
  79% of the two-stream kernel's instructions — while clang did not, so the
  baseline every ISA ratio divides by depended on the compiler. That translation
  unit is now built with `-fno-tree-vectorize -fno-tree-slp-vectorize`, and
  `make check` disassembles the result and fails if more than 5% of its
  instructions touch a vector register. Any scalar figure measured before this
  is not a scalar baseline and must not be used as a denominator.

## 0.5.0 — 2026-08-23

First public release. What it contains:

### The benchmark

- **MD5, SHA-1 and SHA-512**, each written from its specification rather than
  adapted from an existing implementation, which is what lets the whole tree be
  public domain.
- **A compile-time kernel matrix selected at runtime**: scalar, SSE2, AVX2,
  AVX-512 and SHA-NI on x86, NEON on AArch64, OpenCL on a device, each at one to
  four interleaved streams. One binary carries every path, `CPUID` and
  `getauxval(AT_HWCAP)` choose between them, and the choice is reported in the
  output. No `-march=native`, so a binary's behaviour never depends on the
  machine that built it.
- **Autotune**, which measures the available kernels rather than assuming. It
  has to: the best stream count varies by algorithm and by core, and a
  fixed-function SHA unit is 2.13x faster than the vector path on one
  microarchitecture and 0.46x as fast on another.
- **`--where cpu|device|any`** to restrict autotune to one side, without which a
  CPU baseline on a machine with a GPU is quietly a GPU number.

### Correctness

- Every kernel is checked against a scalar reference, and each run reduces its
  digests to an XOR fingerprint **invariant across lanes, streams, threads,
  devices and instruction set architectures**. The same three checksums come
  back from Gracemont, Cascade Lake, Zen 5 and Neoverse V1.
- `make check` runs 1468 kernel and known-answer checks. CI builds with gcc and
  clang and cross-compiles for aarch64 to run the NEON path under emulation, so
  the ARM kernels cannot rot while nobody has the hardware.
- A run that fails verification produces no number.

### Measurement

- **JSON and human-readable output rendered from one data model**, so the two
  cannot disagree. `--list --json` dumps what the binary can do on this machine,
  which is how the tooling avoids carrying a transcribed copy that drifts; all
  four schemas are documented in [docs/schema.md](docs/schema.md).
- **Dispersion is reported, not hidden** — min, median, mean, stddev, CoV and
  the raw samples, with a run flagged when its own noise exceeds the
  significance bar.
- **The environment is captured automatically**: CPU model and flags, the ISA
  path actually taken, governor and frequency, thread count, compiler, device
  and driver. A number without its machine is not comparable to anything.
- **Energy per hash** where powercap exposes it, from `intel-rapl` or
  `amd-rapl`.
- **The PCIe balance point and CPU break-even are solved rather than
  bracketed.** Transfer is constant in the iteration count and compute is linear
  in it, so both come from a linear fit, with the fit quality reported alongside.

### Tools

- `tools/sweep.py` — parameter sweeps to CSV, with resume.
- `tools/compare.py` — diffs two result sets, pairing by workload id, refusing
  to compare across a checksum mismatch, and judging a delta against both the
  significance bar and the noise the runs themselves reported.
- `tools/gpu_run.sh` and `tools/cpu_run.sh` — one-command captures for a
  time-boxed session on rented hardware, phased in priority order so an
  interrupted session still yields what mattered most.

### Known limits

- **No SVE or SVE2 kernel.** Vector-length-agnostic code does not fit the
  fixed-width kernel template and needs its own.
- **OpenCL is validated on an Intel iGPU only**, which has no PCIe link, so
  every device figure comes from a part where "transfer" is a copy within system
  RAM.
- **No bare-metal run**, leaving AVX-512 licence downclocking and energy
  unmeasured.
- **Device uploads come from pageable memory**, roughly half what pinned staging
  achieves, which moves the balance point by about that factor.
