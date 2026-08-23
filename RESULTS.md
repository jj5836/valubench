# Measured results

**Every measured number in this project lives in this file.** The other
documents — [README.md](README.md), [docs/design.md](docs/design.md),
[docs/research.md](docs/research.md), [docs/guide.md](docs/guide.md) — link
to the tables here rather than restating them. The rule exists because they did
restate them, three and four times over, and the copies drifted: the same
"best kernel per algorithm" table appeared with two different sets of numbers in
two files at once. A re-measurement should change one file.

One exception, stated so it does not become a loophole: **sample output blocks**
in the other documents show the shape of what the tool prints, and the numbers
inside them are illustrative rather than maintained. Anything presented as a
result belongs here.

What belongs elsewhere: the *reasoning* about a number. docs/research.md explains why
AVX-512 lands at 2.20x rather than the 3-4x it first guessed; this file records
that it is 2.20x. If you find yourself copying a figure out of here into prose,
link to the anchor instead.

---

## Machines

Results are only comparable within a machine, so every table below names one.

| id | what | relevant limits |
|---|---|---|
| **N100** | Intel N100 (Gracemont, 4 E-cores, 2.9 GHz), Intel UHD iGPU (24 EU), 16 GB | no AVX-512; fanless and thermally limited; iGPU has no PCIe, so device "transfer" is a copy within system RAM |
| **Xeon-4210** | Intel Xeon Silver 4210 (Cascade Lake), in a VM | AVX-512 validation only. No `cpufreq` and a nominal-TSC `/proc/cpuinfo`, so core clock and licence downclocking could not be observed; no RAPL, so no energy |
| **EPYC-9R45** | AMD EPYC 9R45 (Zen 5), EC2 `c8a.4xlarge`, 16 cores, **no SMT**, 30 GiB, Ubuntu 24.04, gcc 13.3 | AVX-512, AVX-512DQ/BW/VL and SHA-NI all present. A VM, so again no `cpufreq` and no `powercap`: downclocking and energy remain unmeasured. Dedicated cores and no hyperthread contention, which is why its CoV is an order of magnitude better than anything else here |
| **Graviton3** | AWS Graviton3 (Neoverse V1, 2.6 GHz), EC2 `c7g.large`, 2 cores, **no SMT**, 4 GiB, Ubuntu 24.04, gcc 13.3 | The only AArch64 part. NEON is the architectural baseline, so the ISA ladder has two rungs rather than four. SVE is present at 256 bits (`sve_default_vector_length` reports 32) and **unused — there is no SVE kernel**; SVE2 absent. Crypto extensions `sha1`/`sha2`/`sha512` present and deliberately unused, see [what a fixed-function hash unit is worth](#shani). No `cpufreq` and no `powercap` |

| **Graviton3-16** | The same Neoverse V1 part, EC2 `c7g.4xlarge`, 16 cores, no SMT, 32 GiB — but **Ubuntu 26.04, kernel 7.0, gcc 15.2**. Everything above applies. The compiler is two generations ahead of the `c7g.large` row and [that turns out to matter](#compiler), so the two ARM captures are reported separately rather than merged |

None of the five is a discrete GPU on a real PCIe link, which is the machine
the goal-2 numbers actually want, and none is bare metal, which is what the
downclocking and energy questions need. CONTRIBUTING.md covers what to set up
on a rented machine before measuring on one.

## How to read these

- **MH/s** is millions of hashes per second; **MC/s** is millions of
  compressions per second, which is the invariant that survives a change of
  message length. **kH/J** is thousands of hashes per joule.
- Every figure is the median of the sample set, from a run that verified its
  checksum against the scalar reference. An unverified run has no number.
- **All-core figures on the N100 carry 20-30% run-to-run variance** and are
  marked where it matters. Single-thread and device figures on that machine run
  under 2% CoV and are the trustworthy ones. See
  [environment and noise](#environment-and-noise).
- Compare two result sets with `tools/compare.py`, which pairs points by
  workload and refuses to compare across a checksum mismatch.
- **Scalar figures dated before 2026-08-23 are not a scalar baseline.** GCC was
  auto-vectorising that kernel, so multi-stream "scalar" numbers are partly SSE2
  or NEON. Every ratio that divides by one is wrong by the amount described in
  [the scalar baseline](#scalar-baseline). Rows measured with the corrected
  kernel are marked.

---

## Algorithms at a glance

<a id="algorithms"></a>
**N100, 4 threads, 55-byte messages, kernel chosen by autotune**, 2026-08-17.

| algorithm | best kernel | MH/s |
|---|---|---:|
| md5 | `md5/ocl-s1` | 233.00 |
| sha1 | `sha1/ocl-s1` | 122.57 |
| sha512 | `sha512/avx2-s3` | 9.54 |

SHA-1 costs more than MD5 because it runs 80 rounds and *expands* its sixteen
message words to eighty rather than permuting them. SHA-512 costs more again:
80 rounds of 64-bit work with four sigma functions, at half the lanes per
register.

The SHA-512 row is the least stable number in this file. Autotune does not
always converge on the same kernel at full thread count on this part —
successive invocations have picked `scalar-s1`, `avx2-s3` and `avx2-s2` at 6.65,
9.54 and 4.19 MH/s. That is the environment, not the measurement; see
[environment and noise](#environment-and-noise).

<a id="algorithms-zen5"></a>
**EPYC-9R45, 55-byte messages, CPU kernels only**, 2026-08-22. AVX-512 wins
every algorithm here, so autotune converges without drama:

| algorithm | best kernel | 1 thread MH/s | 16 threads MH/s |
|---|---|---:|---:|
| md5 | `md5/avx512-s4` | 345.87 | **5404.20** |
| sha1 | `sha1/avx512-s2` (1t), `-s3` (16t) | 178.26 | 3334.61 |
| sha512 | `sha512/avx512-s2` | 31.74 | 501.24 |

CoV 0.02–0.26% throughout, all-core included. The relative costs hold: SHA-1
runs 80 expanded rounds, SHA-512 does 64-bit work at half the lanes.

**The verification checksum matched the value from Intel hosts** —
`md5-full-55x1` gives `955e84cb…` on Gracemont, on Cascade Lake and on Zen 5.
That is the first cross-vendor confirmation that the XOR fingerprint is a
property of the workload rather than of the machine, which is the assumption
every kernel comparison in this file rests on.

<a id="algorithms-graviton3"></a>
**Graviton3, 55-byte messages, CPU kernels only**, 2026-08-22. The first
measurement on AArch64; before this the NEON kernels were validated under
emulation and had no throughput figure at all.

| algorithm | best kernel | 1 thread MH/s | 2 threads MH/s |
|---|---|---:|---:|
| md5 | `md5/neon-s4` | 45.97 | **93.42** |
| sha1 | `sha1/neon-s2` | 21.47 | 42.36 |
| sha512 | `sha512/neon-s1` | 3.65 | 7.29 |

CoV 0.02–0.29%. All-core scaling is 2.03x, 1.97x and 2.00x on two cores, which
is what a part with no SMT and no shared execution resources should give.

**Note the stream counts: s4, s2, s1.** Every other machine in this file wants
more streams for every algorithm; this one wants fewer as the algorithm gets
wider. That is the AArch64 finding and it is [below](#streams-graviton3).

**The checksum matched again** — `955e84cb…` on Neoverse V1, the same value
Gracemont, Cascade Lake and Zen 5 produce. The fingerprint now holds across two
instruction set architectures, not just two x86 vendors, which is what makes
[cross-compiled validation](CONTRIBUTING.md) worth anything: a kernel written
for a machine nobody has can still be proven right.

<a id="algorithms-graviton3-16"></a>
**Graviton3-16, 55-byte messages**, 2026-08-22. Sixteen of the same cores, and
the first ARM all-core figure worth quoting:

| algorithm | best kernel | 1 thread MH/s | 16 threads MH/s | scaling |
|---|---|---:|---:|---:|
| md5 | `md5/neon-s4` | 43.64 | **705.17** | 16.2x |
| sha1 | `sha1/neon-s2` | 23.05 | 369.76 | 16.0x |
| sha512 | `sha512/neon-s3` | 3.95 | 63.28 | 16.0x |

CoV 0.008–0.14%. **Scaling is linear to within measurement error** — no SMT, no
shared vector unit, no shared L2. The 16.2x on MD5 is above 16 by less than the
difference between the two single-thread samples, so read it as "linear", not as
superlinear.

Against [Zen 5 at the same core count](#algorithms-zen5), 705 MH/s to 5404: a
7.7x gap on MD5 from four times the vector width, a higher clock, and a
substantially wider core. Note the checksums are identical across both.

---

## CPU

### Stream interleaving is the largest single win

<a id="streams"></a>
**N100, single thread, 55-byte messages, MD5**, re-measured 2026-08-23 with a
[genuinely scalar kernel](#scalar-baseline), 15 samples:

| kernel | MH/s | |
|---|---:|---|
| `scalar-s1` | 9.36 | one chain — this measures latency, not throughput |
| `scalar-s2` | 14.79 | |
| `scalar-s3` | **15.92** | **1.70x, from stream interleaving alone** |
| `scalar-s4` | 14.71 | four streams no longer fit in 16 registers |
| `sse2-s3` | 41.28 | |
| `avx2-s4` | 42.26 | |

MD5's 64 steps are one serial dependency chain and SIMD does not break it — all
lanes of a vector advance in lockstep as a single chain. A one-stream kernel
measures dependency latency, not throughput.

The scalar path peaks at **three** streams and falls back at four: x86-64 has
sixteen general-purpose registers, and four streams of four state words plus
message words and temporaries no longer fit. The vector paths do not have that
problem until much later, which is a register-file story rather than an
execution-resource one.

*The earlier figures here — scalar-s1 9.41, scalar-s4 29.49, "3.1x from
interleaving" — were measured before the scalar kernel was scalar. The 29.49 was
an auto-vectorised SSE2 kernel, which is why it looked so strong.*

<a id="streams-zen5"></a>
**On a wide out-of-order core the scalar path no longer needs the help.**
EPYC-9R45, single thread, MD5:

| | s1 | s2 | s3 | s4 | best / s1 |
|---|---:|---:|---:|---:|---:|
| `scalar` | 15.41 | **10.59** | 17.05 | 18.07 | **1.17x** |
| `avx512` | 134.87 | 221.53 | 281.91 | 344.06 | **2.55x** |

**The scalar row is not a scalar measurement** — s2 through s4 were
auto-vectorised into SSE2 by the compiler, and this machine was gone before that
was found, so it cannot be re-measured. See [the scalar
baseline](#scalar-baseline). The `avx512` row is unaffected: that kernel is
intrinsics, and what it does is what was written.

Read the scalar row as "what GCC produced from scalar C on a strong
out-of-order core", and read the 0.69x at two streams as the cost of packing two
streams into four lanes on a core whose scalar path was already fast. It is the
deepest such loss measured, and Zen 5 is exactly where the theory predicts the
deepest loss.

<a id="streams-graviton3"></a>
**On AArch64 the best stream count depends on the algorithm, and for two of
three it is not four.** Graviton3, single thread, 55-byte messages:

| kernel | s1 | s2 | s3 | s4 | best |
|---|---:|---:|---:|---:|---|
| `md5/neon` | 14.30 | 25.86 | 36.73 | **45.94** | s4, 3.21x over s1 |
| `sha1/neon` | 15.52 | **21.42** | 18.54 | 17.60 | s2, and s4 is *below* s2 |
| `sha512/neon` | **3.65** | 3.64 | 3.43 | 2.90 | s1 — interleaving only costs |
| `sha512/scalar` | **3.55** | 3.33 | 2.86 | 2.49 | s1, falling monotonically |

CoV 0.01–0.20%. Every x86 part in this file wants three or four streams for
every algorithm, so a kernel that peaks at one stream is new.

**The likely cause is register pressure, and the ordering fits it.** AArch64 has
32 vector registers. A stream must keep its state words and its rolling
sixteen-word message schedule live at once: SHA-512 at two 64-bit lanes per
register needs roughly 24 vectors for a *single* stream, so a second one cannot
fit; SHA-1 needs about 21, so two streams already spill but the latency hidden
still pays for it, and three do not; MD5's four state words leave room to reach
four streams. Read that as a hypothesis consistent with the measurement rather
than as a verified account — the spills have not been counted in the generated
code.

**What follows is a warning about autotuning, not about ARM.** A harness that
had assumed "more streams is better" — which every earlier measurement in this
file supports — would have run SHA-512 at s4 and lost 21%, and SHA-1 at s4 and
lost 18%. Autotune measures all four and picked s1 and s2 correctly.

<a id="streams-graviton3-16"></a>
**The same ladder on Graviton3-16 under gcc 15.2**, which is the same core with
[a different compiler](#compiler):

| kernel | s1 | s2 | s3 | s4 | best |
|---|---:|---:|---:|---:|---|
| `md5/neon` | 13.20 | 23.96 | 34.52 | **43.95** | s4 |
| `md5/scalar` | 7.24 | **5.58** | 7.00 | 11.15 | s4, via a 23% dip at s2 |
| `sha1/neon` | 16.00 | **23.00** | 20.11 | 19.93 | s2 |
| `sha1/scalar` | 8.77 | **9.02** | 6.89 | 5.09 | s2 |
| `sha512/neon` | 3.81 | 3.95 | **3.96** | 3.92 | s3 |
| `sha512/scalar` | 3.63 | **3.64** | 3.33 | 3.24 | s2 |

**The ordering survives the compiler change for MD5 and SHA-1** — climb to s4,
peak at s2 — so those are properties of the core. SHA-512's peak moved from s1
to s3 and its curve flattened to a 4% spread, which is what a different register
allocator looks like on the kernel with the least room.

<a id="scalar-baseline"></a>
### The scalar baseline was not scalar

**Found 2026-08-23, and it invalidates every scalar figure above dated earlier.**
The `scalar` kernels are plain C with no intrinsics, compiled with no `-m`
flags. At `-O2`, GCC's SLP vectoriser fuses the independent streams and emits
vector code anyway:

| kernel | instructions touching a vector register |
|---|---:|
| `scalar-s1` | 1.2% — prologue only, genuinely scalar |
| `scalar-s2` | **88.0%** (SSE2 on x86-64), **78.9%** (NEON on AArch64) |
| `scalar-s3` | 55.8% |
| `scalar-s4` | 84.2% |

SHA-1 and SHA-512 are affected the same way. Clang does not do this at all, so
the same source produced a *different baseline depending on the compiler* — a
worse property than either behaviour alone.

**This explains the two-stream dip completely.** At two streams the compiler
packs two independent hashes into four-lane registers, wasting half the width
and paying pack and unpack costs on every step. Whether that beats honest scalar
code depends entirely on how strong the core's scalar path is:

| core | s2 as measured (auto-vectorised) | vs its own s1 |
|---|---:|---:|
| Gracemont, N100 | 12.31 | 1.31x — weak scalar path, so half-empty vectors still win |
| Zen 5, EPYC-9R45 | 10.59 | **0.69x** — strong scalar path, so the trade is a loss |
| Neoverse V1, gcc 13.3 | 6.71 | 0.93x |
| Neoverse V1, gcc 15.2 | 5.58 | 0.77x — newer vectoriser, worse trade |

The N100 never dipped below 1.0x, which is why the effect hid there for six
days. It was still losing: **14.79 MH/s honest scalar against 12.31
auto-vectorised, a 17% cost** the ladder never revealed because it stayed above
s1.

**The fix** is `-fno-tree-vectorize -fno-tree-slp-vectorize` on that translation
unit alone. `make check` now disassembles the built object and fails if more
than 5% of its instructions touch a vector register, because a flag that stops
being honoured would restore the old behaviour with no symptom except numbers
that quietly improve.

**What is still wrong in this file.** The corrected N100 figures are
[above](#streams). EPYC-9R45 and both Graviton3 instances were terminated before
this was found, so their scalar rows cannot be re-measured and are left in place,
marked, as a record of what the vectorised kernel did. **Do not quote a ratio
that divides by one of them** — including the 3.48x NEON-over-scalar figure,
whose denominator was partly NEON.

### ISA generation beats vector width, and the datapath decides by how much

<a id="avx512"></a>
**EPYC-9R45, single thread, MD5, 55-byte messages**, 2026-08-22. Every rung, so
the width and the instruction-set effects can be read separately.

| | s1 | s2 | s3 | s4 |
|---|---:|---:|---:|---:|
| `scalar` | 15.39 | 10.59 | 17.12 | 18.09 |
| `sse2` | 22.89 | 39.93 | 55.61 | 65.48 |
| `avx2` | 44.73 | 78.30 | 109.19 | 126.67 |
| `avx512` | 134.87 | 221.53 | 281.91 | **344.06** |

CoV 0.03–0.09% on every point.

| ratio, best against best | EPYC-9R45 (Zen 5) | Xeon-4210 (Cascade Lake) | N100 (Gracemont) |
|---|---:|---:|---:|
| AVX-512 / AVX2 | **2.72x** | 2.20x | — (no AVX-512) |
| AVX2 / SSE2 | **1.93x** | — | ~1.06x |

**The AVX2/SSE2 column is the datapath, and it is the cleanest such measurement
in this file.** Doubling the register width doubles throughput on Zen 5 (1.93x)
and does essentially nothing on Gracemont (1.06x), which cracks each 256-bit
operation into two 128-bit halves. Same source, same instructions, opposite
answers — which is what "wider is faster cannot be assumed" looks like measured.

**The AVX-512 column confirms an explanation rather than just a number.**
docs/research.md §4.1 attributed Cascade Lake's shortfall — 2.20x delivered from a
2.67x instruction reduction — to issue width, ports 0 and 1 fusing to serve one
512-bit unit. On a part without that constraint the ratio goes to 2.72x, which
is *above* the instruction-count reduction rather than below it.

**Xeon-4210, gcc 15.2, MD5**, 2026-08-17, for the comparison above: AVX2
`avx2-s3` 80.83 MH/s, AVX-512 `avx512-s2` 177.79 MH/s, consistent at 2.20x /
2.34x / 2.28x for one, two and four threads.

<a id="avx512-threads"></a>
**The ratio does not decay under all-core load.** Measured on EPYC-9R45 with
`avx2-s4` against `avx512-s4`:

| threads | AVX2 MH/s | AVX-512 MH/s | ratio |
|---:|---:|---:|---:|
| 1 | 128.2 | 347.1 | **2.71x** |
| 16 | 1995.5 | 5406.1 | **2.71x** |

Identical to three significant figures. This is not a frequency measurement —
the VM exposes no `cpufreq` — but it is the consequence that matters: whatever
this part does to its clock under 512-bit load, it does not cost AVX-512 its
advantage. All-core scaling is 97% of linear across 16 cores.

### NEON against the scalar path, and the headroom above it

<a id="neon"></a>
**Graviton3, single thread, MD5, 55-byte messages**, 2026-08-22. Two rungs,
because Advanced SIMD is the AArch64 baseline rather than an extension:

| | s1 | s2 | s3 | s4 |
|---|---:|---:|---:|---:|
| `scalar` | 7.24 | 6.71 | 9.99 | 13.19 |
| `neon` | 14.30 | 25.86 | 36.73 | **45.94** |

CoV 0.02–0.07%. **NEON / scalar = 3.48x**, best against best.

**That is a larger vector win than any 128-bit x86 rung shows**, and the reason
is the denominator rather than the numerator. NEON's 45.94 MH/s sits just under
the N100's 128-bit SSE2 at 51.52 — two 4-lane datapaths landing in the same
place. The scalar row is where the parts diverge: 13.19 here against 29.49 on
Gracemont and 18.09 on Zen 5. A weaker scalar path makes the same vector unit
look better, which is worth remembering whenever a vector speedup is quoted
without the baseline beside it.

**The machine is roughly half idle at the top of the ladder.** At 2.6 GHz,
45.94 MH/s is 56.6 cycles per hash; with four lanes and four streams that is
3.54 cycles per four-lane MD5 step, and a step is six or seven NEON operations
— call it two per cycle against the four 128-bit pipes Neoverse V1 provides.
Stream scaling agrees: MD5 is still climbing at s4 (3.21x over s1, and the
curve has not flattened), so the kernel is latency-bound rather than
issue-bound and the part has more to give.

**Which is the argument for an SVE kernel, from data rather than from
enthusiasm.** V1 implements SVE at 256 bits as two pipes rather than four, so
the same operation count moves twice the lanes. `sve_default_vector_length`
reports 32 bytes on this instance. Nothing here proves SVE would deliver 2x —
[AVX2/SSE2 on Gracemont](#avx512) is the cautionary case, 1.06x from a doubling
that the datapath declined to honour — but on V1 the width is real rather than
cracked, and the measured headroom is where it would have to come from.

**The `scalar-s2` dip appeared here too** — 7.24 → 6.71 at two streams,
recovering to 9.99 at three — and chasing it across this machine and Zen 5 is
what eventually found the cause: the compiler was vectorising the scalar kernel,
and NEON on AArch64 for exactly the same reason it emitted SSE2 on x86. See [the
scalar baseline](#scalar-baseline). **The scalar rows in the tables above are
therefore not a scalar baseline**, and the 3.48x NEON-over-scalar ratio divides
by a denominator that was itself partly NEON.

### The compiler is a variable, and a large one

<a id="compiler"></a>
**The same Neoverse V1 silicon, two compiler generations apart**, 2026-08-22.
This comparison exists by accident — the second instance came up on Ubuntu 26.04
rather than the 24.04 that was asked for — and it is the most uncomfortable
table in this file:

| kernel | gcc 13.3 | gcc 15.2 | change |
|---|---:|---:|---:|
| `md5/scalar-s4` | 13.19 | 11.16 | **−15.4%** |
| `md5/neon-s4` | 45.94 | 44.00 | −4.2% |
| `sha1/neon-s2` | 21.47 | 23.05 | **+7.4%** |
| `sha512/neon`, best | 3.65 (at s1) | 3.96 (at s3) | **+8.5%, and the peak moved** |

Same source, same instruction set, same core, same clock, CoV under 0.08%
everywhere. **Two compiler releases are worth between −15% and +8% depending on
the kernel**, and for SHA-512 they move the *optimal stream count* from one to
three.

Three things follow, and none of them are about ARM.

**Every "best stream count" in this file is a property of the generated code as
much as of the core.** The [AArch64 stream ordering](#streams-graviton3) still
holds under both compilers for MD5 and SHA-1, so it is not an artifact — but
SHA-512's peak moved, which is exactly the case where the register-pressure
account predicts a compiler would matter most.

**A benchmark comparing two machines is also comparing two toolchains** unless
it pins one, and this project does not. The environment block of every result
records the compiler for this reason; the `c7g.large` and `c7g.4xlarge` captures
are kept as separate machines above rather than merged into one ARM story.

**It puts a floor on how much any single-digit ratio here should be trusted.**
A 4% difference between two kernels is within what a toolchain change can
produce on its own.

### What a fixed-function hash unit is worth

<a id="shani"></a>
**N100, single thread at 2.9 GHz, SHA-1**, 2026-08-17.

| kernel | organisation | MH/s | vs best SIMD |
|---|---|---:|---:|
| `sha1/scalar-s4` | 1 lane x 4 streams | 5.88 | 0.27x |
| `sha1/sse2-s4` | 4 lanes x 4 streams | 15.53 | 0.71x |
| `sha1/avx2-s3` | 8 lanes x 3 streams | 21.73 | — |
| `sha1/shani-s1` | **1 message at a time** | **46.31** | **2.13x** |

Stream count on SHA-NI, same conditions — the ordering inverts, uniquely:

| streams | 1 | 2 | 3 | 4 |
|---|---:|---:|---:|---:|
| MH/s | **46.31** | 43.59 | 42.39 | 41.20 |

2.9 GHz / 46.31 MH/s is ~63 cycles per 64-byte block over 20 `SHA1RNDS4`
instructions, so ~3.1 cycles per four-round group: reciprocal throughput
essentially equal to latency.

**The 2.13x does not generalise, and on Zen 5 it inverts.**

<a id="shani-zen5"></a>
**EPYC-9R45, single thread, SHA-1, 55-byte messages**, 2026-08-22:

| kernel | MH/s |
|---|---:|
| `sha1/avx512-s2` | **181.65** |
| `sha1/avx512-s3` | 181.13 |
| `sha1/shani-s3` | 83.01 |
| `sha1/shani-s1` | 80.02 |
| `sha1/avx2-s3` | 53.16 |

**SHA-NI / best SIMD = 0.46x.** The fixed-function unit is less than half the
speed of the integer vector path on the same core — where on Gracemont it was
more than twice as fast. Nothing about the SHA unit changed; the vector path
either side of it did, from a 128-bit datapath with no AVX-512 to a full-width
one with it.

Two things follow. **"What is the SHA unit worth" has no answer independent of
the core it sits in** — it is 2.13x on one part and 0.46x on another, a spread
of 4.6x in the same benchmark. And an autotuner that assumed the accelerator
wins would pick a kernel less than half the available speed here, which is why
the harness measures instead of assuming.

SHA-NI's stream ordering stays flat on Zen 5 (80.0 / 78.9 / 83.0 / 70.3 for
s1..s4) rather than declining monotonically as it does on Gracemont, so the
unit is not latency-bound in the same way, but no stream count rescues it.

---

## GPU

### The accelerator's advantage is not uniform

<a id="gpu-advantage"></a>
**N100 iGPU against one N100 core, same corpus, each side at its own best
kernel**, 2026-08-17.

| | CPU (AVX2), MH/s | iGPU (OpenCL), MH/s | GPU advantage | GPU rate relative to its own MD5 |
|---|---:|---:|---:|---:|
| MD5 | 42.85 | 220.45 | **5.15x** | 1.000 |
| SHA-1 | 21.70 | 122.34 | **5.64x** | 0.555 |
| SHA-512 | 3.83 | 12.75 | **3.33x** | **0.058** |

Relative to its own MD5 throughput the CPU delivers 0.089 on SHA-512 and the
iGPU 0.058. Both sides lose at 64 bits — AVX2's lanes halve too — but the GPU
gives up about 1.5x more, because consumer GPU ALUs are 32-bit and every 64-bit
add, rotate and shift is emulated.

### Streams on a GPU are not streams on a CPU

<a id="gpu-streams"></a>
**N100 iGPU**, 2026-08-17.

| streams | 1 | 2 | 3 | 4 |
|---|---:|---:|---:|---:|
| `sha1/ocl`, MH/s | 122.3 | 122.1 | 118.1 | **11.6** |
| `sha512/ocl`, MH/s | 12.76 | **5.98** | 5.80 | **1.40** |

A 10.5x collapse for SHA-1 between three streams and four, and SHA-512 hits the
same wall two streams earlier — 2.1x lost going from one stream to two. Both
*expand* their message schedule, so the rolling window must stay resident in
private memory; past the register budget it spills. MD5 permutes sixteen words
instead and varies smoothly.

### Saturating the device

<a id="saturation"></a>
**N100 iGPU, 55-byte messages, MD5**, before and after decoupling launch
geometry from corpus size.

| working set | before, MH/s | after, MH/s |
|---:|---:|---:|
| 64 KiB | 8.5 | 108.8 |
| 1 MiB *(default)* | 90.3 | 242.2 |
| 4 MiB | 134.8 | 252.5 |
| 64 MiB | 167.6 | 254.9 |

Throughput used to track `--working-set-kb`, which made the memory axis and the
occupancy axis the same knob and under-reported the device by 2.7x at the
default. It is now flat from about 4 MiB up, at 98.9% kernel-busy.

**Below roughly 4 MiB the device cannot be saturated at all**: 768 messages is
12 groups, and no launch geometry turns that into GPU-scale parallelism.

For orientation, the same chip's best CPU kernel against its iGPU: `sse2-s4` at
68 MH/s, `ocl-s1` at 242 MH/s.

---

## Transfer and the PCIe crossover

<a id="crossover"></a>
The goal-2 measurement: at what iteration count does compute overtake the link?

**N100 iGPU, `md5/ocl-s1`, 64-byte messages, 256 MiB working set,
`--transfer stream`.**

| iterations | 1 | 2 | 4 | 8 | 16 | 32 |
|---|---:|---:|---:|---:|---:|---:|
| compute/transfer | 0.55 | 0.93 | **1.95** | 3.50 | 6.97 | 16.89 |
| bound by | transfer | transfer | compute | compute | compute | compute |

The measured link rate stays flat across the sweep, which is the check that the
link is being measured consistently rather than varying with the workload.

### N* is solved, not bracketed

<a id="nstar"></a>
Transfer is constant in the iteration count and kernel time is linear in it, so
`sweep.py` fits both and reports the intercept rather than the bracket:

```
PCIe balance point  (compute == transfer)
  md5/ocl-s1       N* = 1.54 iterations
      compute-bound from 2 iterations up
      1.951 ms per iteration, 0.190 ms fixed launch cost, 3.190 ms transfer
      fit r2 = 1.0000 over 8 points, transfer flat to 10.8%
```

`r2` has been 1.0000 on every sweep so far. The cruder `N / ratio` estimate
charges the fixed launch cost to compute and drifts 1.8-2.3 where the fit gives
exactly 2.00.

### N* is a property of the (kernel, working set) pair

<a id="nstar-ws"></a>
**N100 iGPU, `md5/ocl-s1`, 64-byte messages.**

| working set | ms/iter | launch | transfer | N* | GB/s |
|---:|---:|---:|---:|---:|---:|
| 32 MiB | 1.951 | 0.190 | 3.190 | **1.54** | 10.5 |
| 64 MiB | 3.832 | 0.637 | 8.307 | **2.00** | 7.9 |

Doubling the corpus scaled compute by exactly 1.96x, as designed, but transfer
by 2.60x — the achieved link rate itself fell from 10.5 to 7.9 GB/s. **A bare
"N* = 2" is not a portable claim**; quote the working set with it.

A single streaming run reports the same thing directly:

```
  kernel busy 20.7% of wall time
  transfer    39.3% of wall, 7.62 GB/s host->device (64.0 MiB per pass)
  bound by    TRANSFER  (compute/transfer = 0.53)
```

**These figures demonstrate the mechanism, not the answer.** An integrated GPU
has no PCIe; the ~8 GB/s above is a copy within system RAM. The number that
carries a purchasing decision needs a discrete card, where the device is also
10-50x faster and N* correspondingly higher.

### Break-even: when does offload beat the whole CPU?

<a id="break-even"></a>
A different question from N\* above, and it can disagree with it. N\* asks
whether the bus is in the way, which decides whether a *faster* accelerator
would help. Break-even asks whether this accelerator beats the machine you
already own, which decides whether to buy one. The device side pays for its
transfers; the CPU side reads the memory it already has.

**N100 iGPU (`md5/ocl-s1`, streaming) against all four N100 cores
(`md5/avx2-s2`), 64-byte messages, 32 MiB working set**, solved from a
nine-point iteration ladder.

| | value |
|---|---:|
| break-even | **3.51 iterations** — offload pays from 4 up |
| device per iteration | 0.007 us |
| CPU per iteration (4 threads) | 0.012 us |
| compute advantage once transfers amortise | **1.59x** |
| device fixed cost per hash (upload + launch) | 0.025 us |
| fit | r² 0.9992 device, 0.9968 CPU, 9 points |

Read the compute-advantage row first. Against **one** core this iGPU is worth
5.15x ([above](#gpu-advantage)); against **four** it is worth 1.59x, and that is
before the corpus has to cross anything. Offload only comes out ahead at all
once about four iterations have amortised the upload. On this pair the
accelerator barely earns its place — which is exactly the judgement goal 2
exists to support, and it is invisible in any single-threaded comparison.

Solved the same way as N\*, since time per hash is linear in the iteration
count on both sides:

```
    cpu:  p + q·N        device:  c + d·N        N_be = (c − p) / (q − d)
```

`q > d` is the precondition — the device must compute an iteration faster than
the CPU, or the curves never cross and no iteration count makes offload pay.
The solver reports that case as "never" rather than extrapolating a crossing
that does not exist.

**The working-set dependence is unmeasured here.** Repeating at 64 MiB gave
break-even 2.82 and a 1.82x advantage, but its CPU fit failed the linearity
check (r² 0.9521) and is therefore not quoted. That is this machine, not the
method: all-core figures on this part carry 20-30% run-to-run variance
([above](#environment-and-noise)), which is wide enough to bend a nine-point
line. N\* moves with the working set and break-even should move with it for the
same reason — more corpus is more upload to earn back — but that needs a quieter
machine to demonstrate.

**Two checks the ladder has to pass**, both learned from getting them wrong:

- **r² on each side.** A ladder that is not linear is not describing the model.
- **A non-negative fixed cost.** Time per hash at N=0 is what is paid before any
  iteration runs, so a fit that extrapolates below zero has been dragged by a
  point off the line. One degraded 64-iteration CPU point (CoV 16.7%) did
  exactly that in an early session capture and turned a ~1.2x compute advantage
  into 4.7x while leaving r² at 0.98 — high enough to pass the first check
  alone.

---

## The three axes

### Message length

<a id="axis-message"></a>
**N100, single core, `avx2-s4`, MD5.** Block-count boundaries dominate:
crossing 55->56 bytes doubles the blocks and roughly halves hashes/sec, while
compressions/sec stays in a band.

| bytes | blocks | MH/s | MC/s | MB/s |
|---:|---:|---:|---:|---:|
| 55 | 1 | 43.20 | 43.20 | 2376 |
| 56 | 2 | 16.66 | 33.33 | 933 |
| 120 | 3 | 13.30 | 39.91 | 1596 |
| 503 | 8 | 5.62 | 44.95 | 2826 |
| 4087 | 64 | 0.70 | 44.83 | 2863 |

Two-block messages are the worst case: the second block is nearly all padding,
so half the compressions do almost no useful byte-work.

**Graviton3-16, single core, `md5/neon-s4`**, 2026-08-22, for contrast — the
same axis on a different architecture:

| bytes | blocks | MH/s | MC/s |
|---:|---:|---:|---:|
| 8 | 1 | 43.96 | 43.96 |
| 55 | 1 | 43.72 | 43.72 |
| 56 | 2 | 22.04 | 44.08 |
| 247 | 4 | 11.18 | 44.72 |
| 1015 | 16 | 2.83 | 45.34 |
| 4087 | 64 | 0.71 | 45.23 |

**Compressions per second is flat within 3.8% across a 500x range of message
length here**, including the 55→56 boundary, where the N100 above loses 23% of
its compression rate. Hashes/sec halves at the boundary on both machines — that
is arithmetic, two blocks instead of one. The N100's *additional* loss of
compression throughput is therefore a property of that machine or that kernel
and not of two-block messages as such, which the single-machine table could not
have told you. Treat MC/s as the length-invariant figure it was meant to be, and
the N100 row as the exception needing explanation.

### Working set

<a id="axis-working-set"></a>
**N100 (L1d 32 KiB/core, L2 2 MiB, L3 6 MiB), single core, 1015-byte messages,
MD5.**

| working set | MB/s | MC/s |
|---:|---:|---:|
| 192 KiB | 2799 | 44.11 |
| 384 KiB | 2826 | 44.55 |
| 1.9 MiB | 2515 | 39.64 |
| 7.9 MiB | 2427 | 38.26 |
| 128 MiB | 2493 | 39.30 |

**MD5 barely becomes memory-bound.** Falling out of L2 costs ~13% and going to
DRAM costs nothing further — one compression is several hundred integer ops per
64 bytes, which puts the workload far to the right of the roofline ridge point
on any machine we are likely to meet. There is no CPU-side memory crossover to
find with this workload.

**Graviton3-16 (L1d 64 KiB/core, L2 1 MiB/core, L3 32 MiB), single core,
55-byte messages, `md5/neon-s4`** — the same conclusion, harder:

| working set | MH/s |
|---:|---:|
| 48 KiB | 44.31 |
| 1008 KiB | 43.61 |
| 4080 KiB | 43.86 |
| 16368 KiB | 43.88 |
| 64 MiB | 43.96 |

**1.6% total, from fitting in L1 to twice the size of L3**, and not even
monotonic. At 2.4 GB/s of message traffic against a 64-byte compression, the
corpus never becomes the constraint on either architecture; a second part is
enough to stop treating this as a property of the N100.

### Iterations

<a id="axis-iterations"></a>
**N100, single core, 55-byte messages, MD5.** Compressions/sec stays flat while
hashes/sec falls proportionally, which is what a linear compute knob should do.

| `--iterations` | MH/s | MC/s |
|---:|---:|---:|
| 1 | 41.55 | 41.55 |
| 4 | 10.64 | 42.57 |
| 64 | 0.70 | 44.95 |
| 256 | 0.17 | 43.40 |

---

## Energy

<a id="energy"></a>
**N100, 55-byte messages, MD5**, via powercap RAPL.

| kernel | threads | MH/s | kH/J | package W |
|---|---:|---:|---:|---:|
| `scalar-s4` | 4 | 40.7 | 2288 | 17.8 |
| `sse2-s4` | 4 | 78.6 | 3801 | 20.8 |
| `avx2-s4` | 4 | 89.9 | 4038 | 19.3 |
| **`ocl-s1`** | 1 | **241.9** | **9635** | 25.1 |

The integrated GPU is 2.7x the throughput of the best CPU kernel and 2.4x its
efficiency. RAPL domains cross-check as they should: package (10.63 J) bounds
core (8.51 J) plus uncore (1.73 J), and on client Intel parts the `uncore`
domain **is** the integrated GPU.

Energy for the AVX-512 path is **unknown** — Xeon-4210 is a VM with no RAPL.
Given 512-bit work draws more power, it may tell a different story than the
2.20x throughput ratio.

---

## Environment and noise

<a id="environment-and-noise"></a>
Reproducibility is mostly outside the benchmark's control, and these are the
numbers that say so. All on the N100.

| machine | condition | run-to-run variation |
|---|---|---|
| N100 | single thread, quiet machine | under 2% CoV |
| N100 | device (iGPU) runs | under 2% CoV |
| N100 | all four cores | 20-30% CoV |
| N100 | all four cores under background load (load average ~2.4) | over 25% |
| EPYC-9R45 | single thread | **0.03–0.09%** |
| EPYC-9R45 | all sixteen cores | **0.02–0.26%** |

**The EPYC rows are what this benchmark looks like on hardware that is not
fighting itself.** Three hundred times tighter than the N100 all-core figure,
from a dedicated-core cloud instance with no hyperthreading, no thermal ceiling
worth the name and nothing else running. It is worth knowing that the wide
variance recorded above is a property of a fanless four-core desktop part and
not of the measurement method: on a server part the same harness resolves
differences of a fraction of a percent.

The all-core figure is wider than the gaps between kernels, which is why
autotune does not always converge on the same SHA-512 kernel on this part. Runs
intended for comparison want a quiet machine with the performance governor set;
the tool reports the environment with the result and exits non-zero when a
result is too noisy to trust, but it cannot make a busy machine quiet.

**Toolchains.** GCC 13.3 and Clang 18.1.3 both build clean under the full
warning set, pass the test suite, and produce identical checksums. GCC is around
5% faster on the AVX2 kernel; both emit the same AVX-512 instruction selection.

---

## What has never been measured

Listed because an absent number is easy to mistake for a bad one.

- **SVE and SVE2.** No kernel exists. Graviton3 offers SVE at 256 bits and the
  NEON measurement leaves [visible headroom](#neon), so this is the largest
  unclaimed number in the file.
- **A pinned toolchain.** Two gcc releases move single-thread throughput by
  −15% to +8% on identical silicon ([above](#compiler)), and nothing in the
  harness holds the compiler constant across machines.
- **NVIDIA and AMD GPUs.** Only the Intel iGPU has run. Every device figure
  above is from a part with no PCIe link.
- **AVX-512 on bare metal.** Only in a VM, so no core clock, no licence
  downclocking, no energy.
- **SHA-NI on an Intel P-core.** Measured on Gracemont and on
  [Zen 5](#shani-zen5), which disagree by 4.6x. No Intel performance core has
  run it, and the ARM crypto extensions are unused by design.
- **NVML and DRM hwmon energy paths.** Written, never exercised — the
  development box has only RAPL, and it is root-only there.
- **Pinned-host-memory streaming.** Uploads are from pageable memory, roughly
  half the rate tuned staging would achieve, which moves N* by about that
  factor.
