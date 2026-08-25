/*
 * kernel_avx2.c -- 8-lane AVX2 kernels.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * Twice SSE2's width, but architecturally the same shape: AVX2 still has no
 * 32-bit vector rotate and no 3-input boolean instruction, so a round function
 * costs ~3 ops and a rotate ~3 more. Compare kernel_avx512.c, where both
 * collapse to one instruction each -- that gap is the super-linear AVX-512
 * result predicted in docs/research.md 4.1.
 *
 * Compiled with -mavx2 into its own object. Nothing here may execute before
 * vb_cpu_has_avx2() has returned true.
 */

#include <stdint.h>
#include <immintrin.h>

#define OPS_VEC         __m256i
#define OPS_LANES       8
#define OPS_SET1(x)     _mm256_set1_epi32((int) (x))
#define OPS_ADD(a, b)   _mm256_add_epi32((a), (b))
#define OPS_XOR(a, b)   _mm256_xor_si256((a), (b))
#define OPS_STORE(p, v) _mm256_storeu_si256((__m256i *) (void *) (p), (v))
#define OPS_LOAD(p)     _mm256_loadu_si256((const __m256i *) (const void *) (p))

#define OPS_ROTL(x, n)  _mm256_or_si256(_mm256_slli_epi32((x), (n)), \
                                        _mm256_srli_epi32((x), 32 - (n)))

#define OPS_CH(x, y, z)  _mm256_xor_si256(_mm256_and_si256((x), (y)), \
                                        _mm256_andnot_si256((x), (z)))
#define OPS_MAJ(x, y, z) _mm256_xor_si256( \
                             _mm256_xor_si256(_mm256_and_si256((x), (y)), \
                                              _mm256_and_si256((x), (z))), \
                             _mm256_and_si256((y), (z)))

#define OPS_F(x, y, z)  _mm256_or_si256(_mm256_and_si256((x), (y)), \
                                        _mm256_andnot_si256((x), (z)))
#define OPS_G(x, y, z)  _mm256_or_si256(_mm256_and_si256((x), (z)), \
                                        _mm256_andnot_si256((z), (y)))
#define OPS_H(x, y, z)  _mm256_xor_si256(_mm256_xor_si256((x), (y)), (z))
#define OPS_I(x, y, z)  _mm256_xor_si256((y), \
                            _mm256_or_si256((x), \
                                _mm256_xor_si256((z), _mm256_set1_epi32(-1))))

#define OPS64_VEC        __m256i
#define OPS64_LANES      4
#define OPS64_SET1(x)    _mm256_set1_epi64x((long long) (x))
#define OPS64_ADD(a, b)  _mm256_add_epi64((a), (b))
#define OPS64_XOR(a, b)  _mm256_xor_si256((a), (b))
#define OPS64_ROTR(x, n) _mm256_or_si256(_mm256_srli_epi64((x), (n)), \
                                         _mm256_slli_epi64((x), 64 - (n)))
#define OPS64_SHR(x, n)  _mm256_srli_epi64((x), (n))
#define OPS64_STORE(p, v) _mm256_storeu_si256((__m256i *) (void *) (p), (v))
#define OPS64_LOAD(p)     _mm256_loadu_si256((const __m256i *) (const void *) (p))
#define OPS64_CH(x, y, z)  _mm256_xor_si256(_mm256_and_si256((x), (y)), \
                                            _mm256_andnot_si256((x), (z)))
#define OPS64_MAJ(x, y, z) _mm256_xor_si256( \
                               _mm256_xor_si256(_mm256_and_si256((x), (y)), \
                                                _mm256_and_si256((x), (z))), \
                               _mm256_and_si256((y), (z)))

/* Every algorithm at every stream count, from the shared matrix. */
#define VB_ISA avx2
#include "instantiate_all.h"
