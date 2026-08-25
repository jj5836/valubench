/*
 * cpu_features.h -- runtime ISA detection.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * valubench ships every SIMD path in one binary and picks at runtime, rather
 * than building with -march=native. A -march=native binary's behaviour depends
 * on the machine that compiled it, which is exactly the kind of hidden variable
 * that makes benchmark results incomparable (docs/research.md 4.3).
 */

#ifndef VALUBENCH_CPU_FEATURES_H
#define VALUBENCH_CPU_FEATURES_H

int vb_cpu_has_sse2(void);
int vb_cpu_has_avx2(void);
int vb_cpu_has_avx512f(void);

/*
 * AArch64. NEON (Advanced SIMD) is architecturally mandatory on AArch64, so the
 * predicate is a formality there and exists for symmetry -- the registry calls
 * one of these per kernel and should not special-case an ISA. It is a real
 * check on 32-bit ARM, which valubench does not currently build for.
 */
int vb_cpu_has_neon(void);

/*
 * SVE and SVE2. Both are AArch64-only and report 0 everywhere else.
 *
 * These are separate ISAs here, not one with a feature flag, because they are
 * separate instruction sets for this workload: SVE has no three-input bitwise
 * select, no three-way XOR, no fused xor-rotate and no shift-right-insert,
 * and SVE2 has all four. Comparing them is the point.
 */
int vb_cpu_has_sve(void);
int vb_cpu_has_sve2(void);

/*
 * Lanes per vector at the run-time vector length: svcntw() for 32-bit words,
 * svcntd() for 64-bit. The registry calls these once. They live here rather
 * than in the kernel translation units because registry.c is built without
 * -march=armv8-a+sve and must not see arm_sve.h.
 */
unsigned vb_sve_lanes32(void);
unsigned vb_sve_lanes64(void);

/*
 * The x86 SHA extensions. Not a wider vector ALU but a fixed-function SHA-1 and
 * SHA-256 unit, so it is available to the SHA-1 kernels only -- see
 * src/kernels/cpu/shani.c.
 */
int vb_cpu_has_sha_ni(void);

/* Best-effort CPU model string, "unknown" if it cannot be determined. */
const char *vb_cpu_brand(void);

#endif /* VALUBENCH_CPU_FEATURES_H */
