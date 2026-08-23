/*
 * cpu_features.h -- runtime ISA detection.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
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
 * The x86 SHA extensions. Not a wider vector ALU but a fixed-function SHA-1 and
 * SHA-256 unit, so it is available to the SHA-1 kernels only -- see
 * src/kernels/cpu/shani.c.
 */
int vb_cpu_has_sha_ni(void);

/* Best-effort CPU model string, "unknown" if it cannot be determined. */
const char *vb_cpu_brand(void);

#endif /* VALUBENCH_CPU_FEATURES_H */
