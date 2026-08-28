# Changelog

Notable changes. Measured figures are not recorded here; they are tracked
outside this repository while a durable format for them is decided.

## Unreleased

_Nothing yet._

## 0.6.0 — 2026-08-28

### Fixed

- **Autotune pinned every worker to one core, under-reporting by up to 3.4x.**
  `pool_create()` pins worker 0 by calling `pthread_setaffinity_np` on the
  calling thread, and never restores it. `vb_allowed_cpus()` then read the mask
  back with `sched_getaffinity(0, ...)`, which reports the *calling thread's*
  mask rather than the process's. Autotune builds one pool per candidate kernel
  on that same thread, so the first probe narrowed the mask to a single CPU and
  every pool after it — including the one behind the reported result — put all
  of its workers there.

  Nothing gave it away. `threads_used` still reported the count that was asked
  for, and no pin failure was raised, because pinning to a CPU you already
  occupy always succeeds. Measured at 2.9x slow on four cores and 3.4x on
  eight, and confirmed with `ps -L`: every thread on PSR 0 before the fix,
  spread across all CPUs after.

  It also chose the wrong kernel. Every candidate was ranked on one core, so
  autotune selected the kernel that wins under contention rather than the one
  that wins on the machine.

  **No released version is affected.** The defect was introduced after 0.5.0
  was tagged, by the VB-007 pinning fix on 2026-08-25, and never appeared in a
  release — 0.5.0 derives worker CPUs from the online count and contains no
  `sched_getaffinity` call at all. It affects builds taken from `main` between
  2026-08-25 and this release.

  For such a build, a figure is suspect only if all three hold: it ran on the
  CPU, with more than one thread, and let autotune pick the kernel. Naming
  `--kernel` means one pool per process and the mask is read before the first
  pin can narrow it.

  Results now carry `pinned_cpus` beside `threads_used`. The broken build
  reports `threads_used=8, pinned_cpus=1`; nothing in the old output could
  express that, which is how this passed review, CI, and five hardware
  sessions.

- **The binary died at startup on every AArch64 CPU without SVE.** The registry
  resolved lane counts by calling each kernel's `lanes_fn` with no
  `available()` check, and for SVE rows that function is a bare `CNTW` — an SVE
  instruction, UNDEF without the feature. SIGILL on Graviton2, Ampere Altra,
  every Raspberry Pi and every Cortex-A5x/A7x, before computing anything.

  Four parts, because guarding the call alone trades a crash for a lie:
  `resolve_lanes()` consults `available()`; `vb_batch_divides()` returns 0 for a
  zero group size, since AArch64 `UDIV` by zero yields 0 rather than trapping
  and `768 % 0` evaluated to 768; the structural test skips rows whose width
  needs an absent ISA; and `vb_cpu_has_sve2()` now requires SVE, because qemu's
  `-cpu max,sve=off` clears `HWCAP_SVE` while leaving `HWCAP2_SVE2` set and
  that reached `CNTW` by a separate path.

  CI now runs the binary on `neoverse-n1`, `cortex-a72`, `cortex-a53` and
  `max,sve=off`, and asserts the SVE rows stay registered-but-unavailable so a
  build that dropped them cannot pass by testing nothing.

- **Multi-block messages were corrupted on SVE.** An unbraced `if (b == 0)`
  guarded one of five macro-expanded statements. Invisible at the default
  55-byte message, which is a single block; found on real hardware at 112 of
  1032 checks.

- **Sixteen findings from an independent review**, four of which were verified
  against the code before being accepted and two of which were materially worse
  than filed. A degraded thread pool reported **176 MH/s against a true 43** and
  marked it verified, because slices were sized before any thread started while
  `hashes_per_iter` counted the whole corpus; pools are now all-or-nothing
  behind a start gate. An energy total counted Intel's `uncore` domain twice,
  understating hashes/joule by 15% on client parts.

- **`make check` failed on any SVE machine whose vector length is not a power
  of two** — 12 failures at 384 bits, blaming kernels that were correct. A
  run-time lane count that does not tile the batch is a property of the machine;
  a fixed-width one that does not is a defect, and only the second now fails.

- **A hang counted as a passing test.** `check-threadfail` asserted that a
  degraded pool never reports a result, with no time bound — and removing the
  start gate to verify that assertion deadlocks rather than returning a wrong
  number. Two such waits sat on a development machine for 9 and 23 hours. Each
  fault injection is now bounded.

- **A guard that found nothing passed.** The scalar-purity check reported
  success while inspecting zero kernels, and immediately caught a real
  `OBJDUMP` derivation bug once it was made to fail on an empty set.

- **SHA-512 reported half its working set.** The result path computed corpus
  size with a hardcoded 64-byte block, which is right for MD5 and SHA-1 and half
  the truth for SHA-512's 128-byte blocks. The corpus was always *built* at the
  correct size, so no throughput figure was wrong — but working set is one of
  the three axes this benchmark sweeps, and the reported number decides which
  side of a cache a measurement is read as sitting on. It now comes from the
  algorithm, and `make check` verifies the reported figure against
  `batch_messages x blocks x block_bytes` for every algorithm at each padding
  boundary.
  A second, correct implementation of the same arithmetic existed in
  `vb_working_set_bytes()` and had no callers, which is how the two could
  disagree unnoticed; it has been removed rather than wired up, because the
  reported figure should describe the corpus that was built rather than one
  recomputed from the request.
  **Comparing SHA-512 results across this fix**: `tools/compare.py` treats
  working set as part of a measurement's identity, so a pre-fix SHA-512 row will
  not pair with a post-fix one even when the runs were otherwise identical. That
  is the tool being right — the two labels genuinely differ — but it means
  archived SHA-512 comparisons need the older side's figure doubled first.
- **The scalar kernel was not scalar.** At `-O2`, GCC's SLP vectoriser fused the
  independent streams and emitted SSE2 on x86-64 and NEON on AArch64 — 88% and
  79% of the two-stream kernel's instructions — while clang did not, so the
  baseline every ISA ratio divides by depended on the compiler. That translation
  unit is now built with `-fno-tree-vectorize -fno-tree-slp-vectorize`, and
  `make check` disassembles the result and fails if more than 5% of its
  instructions touch a vector register. Any scalar figure measured before this
  is not a scalar baseline and must not be used as a denominator.

### Added

- **SVE and SVE2 kernels, vector-length agnostic.** One binary correct at 128,
  256, 384, 512, 1024 and 2048-bit vector lengths, verified at every one. SVE
  types are sizeless and cannot go in the arrays the shared templates use, so
  the templates gained hooks whose defaults are the original code — the x86
  objects are byte-identical with and without them, checked per symbol.

  SVE-256 beats NEON-128 on Neoverse V1 by 1.12-1.20x *with a compiler that can
  generate it*; gcc 13 and 14 emit as many instructions for eight lanes as NEON
  needs for four, which is a code-generation defect fixed in gcc 15. The
  toolchain is worth up to 4.15x on the same silicon — more than any
  architectural difference this project has measured.

- **GPU clock, temperature and throttle-reason telemetry**, sampled across the
  timed region and reported as first/last/min/max. NVML only; parts without it
  report nothing rather than zero. This is what the word *sustained* rests on:
  every device figure before it was potentially a boost-clock number, and no
  sustain mode was needed because `--warmup-ms` already accepts up to an hour.

- **Stream counts beyond four** — 6 and 8 — which MD5 wanted, and which are
  worth 26-36% on Neoverse V2.

- **`pinned_cpus` in every result**, so a pool confined to fewer cores than it
  claims says so on the face of the output.

- **`check-pinning`**, which asserts a multi-threaded pool spans more than one
  CPU. This is the check that was missing when autotune collapsed every worker
  onto one core: no baseline is needed, because it is an invariant rather than
  a comparison against a recorded figure — and a recorded figure was never
  available, since results are deliberately kept out of the repository. It
  fails on a build with the bug, and fails again if `pinned_cpus` ever
  disappears from the output rather than passing quietly. A single-CPU machine
  says it cannot run the check; CI asserts the runner has more than one core,
  so a change there cannot make it vacuous.

- **Virtual or bare metal**, recorded as  with the
  DMI evidence behind it. Three states rather than a flag because on AArch64
  the x86 hypervisor CPUID bit has no equivalent: Architecture:                            x86_64
CPU op-mode(s):                          32-bit, 64-bit
Address sizes:                           39 bits physical, 48 bits virtual
Byte Order:                              Little Endian
CPU(s):                                  4
On-line CPU(s) list:                     0-3
Vendor ID:                               GenuineIntel
Model name:                              Intel(R) N100
CPU family:                              6
Model:                                   190
Thread(s) per core:                      1
Core(s) per socket:                      4
Socket(s):                               1
Stepping:                                0
CPU(s) scaling MHz:                      85%
CPU max MHz:                             3400.0000
CPU min MHz:                             700.0000
BogoMIPS:                                1612.80
Flags:                                   fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx pdpe1gb rdtscp lm constant_tsc art arch_perfmon pebs bts rep_good nopl xtopology nonstop_tsc cpuid aperfmperf tsc_known_freq pni pclmulqdq dtes64 monitor ds_cpl vmx est tm2 ssse3 sdbg fma cx16 xtpr pdcm pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand lahf_lm abm 3dnowprefetch cpuid_fault epb cat_l2 cdp_l2 ssbd ibrs ibpb stibp ibrs_enhanced tpr_shadow flexpriority ept vpid ept_ad fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid rdt_a rdseed adx smap clflushopt clwb intel_pt sha_ni xsaveopt xsavec xgetbv1 xsaves split_lock_detect user_shstk avx_vnni dtherm ida arat pln pts hwp hwp_notify hwp_act_window hwp_epp hwp_pkg_req vnmi umip pku ospke waitpkg gfni vaes vpclmulqdq rdpid movdiri movdir64b fsrm md_clear serialize arch_lbr ibt flush_l1d arch_capabilities
Virtualization:                          VT-x
L1d cache:                               128 KiB (4 instances)
L1i cache:                               256 KiB (4 instances)
L2 cache:                                2 MiB (1 instance)
L3 cache:                                6 MiB (1 instance)
NUMA node(s):                            1
NUMA node0 CPU(s):                       0-3
Vulnerability Gather data sampling:      Not affected
Vulnerability Ghostwrite:                Not affected
Vulnerability Indirect target selection: Not affected
Vulnerability Itlb multihit:             Not affected
Vulnerability L1tf:                      Not affected
Vulnerability Mds:                       Not affected
Vulnerability Meltdown:                  Not affected
Vulnerability Mmio stale data:           Not affected
Vulnerability Old microcode:             Not affected
Vulnerability Reg file data sampling:    Mitigation; Clear Register File
Vulnerability Retbleed:                  Not affected
Vulnerability Spec rstack overflow:      Not affected
Vulnerability Spec store bypass:         Mitigation; Speculative Store Bypass disabled via prctl
Vulnerability Spectre v1:                Mitigation; usercopy/swapgs barriers and __user pointer sanitization
Vulnerability Spectre v2:                Mitigation; Enhanced / Automatic IBRS; IBPB conditional; PBRSB-eIBRS Not affected; BHI BHI_DIS_S
Vulnerability Srbds:                     Not affected
Vulnerability Tsa:                       Not affected
Vulnerability Tsx async abort:           Not affected
Vulnerability Vmscape:                   Mitigation; IBPB before exit to userspace reports no hypervisor
  on Graviton and Grace alike, machines that are certainly virtual, so a
  boolean built on that evidence would have labelled every ARM figure in this
  project bare metal. Sixteen constructed cases cover the shapes no one
  machine has, including the EC2 pair that names itself identically whether
  virtual or not and is told apart only by its instance type.

- **CI gained teeth**: an ASan/UBSan job, a software-OpenCL job, SVE at four
  vector lengths under emulation with a deliberate non-power-of-two width, a
  multi-block sweep against the scalar reference, an objdump assertion that the
  SVE units contain SVE, and the non-SVE AArch64 startup check above.

- **`run.sh` gained D5**, the resident iteration ladder: where compute overtakes
  *memory*, as distinct from D1's where it overtakes the link. Two message sizes,
  because the knee is compressions per byte read and `blocks_per_message` is the
  other half of that ratio.

### Changed

- **Relicensed to BSD 3-Clause**, with SPDX identifiers on every source file.
- **Measured results moved out of the repository.** `RESULTS.md` worked for one
  machine and stopped working at five: numbers outlived their provenance, and
  nothing was machine-checkable.
- **The iteration ladder is solved in one pass** rather than once per rung,
  which took reference computation from dominating a crossover session to
  disappearing into it.
- **`tools/isa_cost.py` no longer claims to predict throughput.** It supplies
  instructions per hash; throughput is that times IPC, and its predictions were
  wrong in both directions.

## 0.5.0 — 2026-08-23

First public release. What it contains:

### The benchmark

- **MD5, SHA-1 and SHA-512**, each written from its specification rather than
  adapted from an existing implementation, which is what leaves the whole tree
  free of any third-party notice.
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
- `tools/run.sh` — a one-command capture for a time-boxed session on rented
  hardware: CPU phases, then device phases if there is a device, ordered so an
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
