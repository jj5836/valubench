/*
 * kernel_sse2.c -- 4-lane SSE2 kernels.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * SSE2 is guaranteed by the x86-64 baseline, so this path needs no runtime
 * check on that target. It has neither a rotate instruction nor 3-input boolean
 * logic, so the round functions and the rotate both cost several ops -- see the
 * ISA table in docs/research.md 4.1. That is not a deficiency in this code; it is
 * the hardware fact the benchmark exists to expose.
 */

#include <stdint.h>
#include <emmintrin.h>

#define OPS_VEC         __m128i
#define OPS_LANES       4
#define OPS_SET1(x)     _mm_set1_epi32((int) (x))
#define OPS_ADD(a, b)   _mm_add_epi32((a), (b))
#define OPS_XOR(a, b)   _mm_xor_si128((a), (b))
#define OPS_STORE(p, v) _mm_storeu_si128((__m128i *) (void *) (p), (v))
#define OPS_LOAD(p)     _mm_loadu_si128((const __m128i *) (const void *) (p))

/* No vector rotate before AVX-512: shift, shift, or. */
#define OPS_ROTL(x, n)  _mm_or_si128(_mm_slli_epi32((x), (n)), \
                                     _mm_srli_epi32((x), 32 - (n)))

/* No 3-input boolean logic either. _mm_andnot_si128(a,b) is (~a) & b. */
#define OPS_CH(x, y, z)  _mm_xor_si128(_mm_and_si128((x), (y)), \
                                     _mm_andnot_si128((x), (z)))
#define OPS_MAJ(x, y, z) _mm_xor_si128(_mm_xor_si128(_mm_and_si128((x), (y)), \
                                                     _mm_and_si128((x), (z))), \
                                       _mm_and_si128((y), (z)))

#define OPS_F(x, y, z)  _mm_or_si128(_mm_and_si128((x), (y)), \
                                     _mm_andnot_si128((x), (z)))
#define OPS_G(x, y, z)  _mm_or_si128(_mm_and_si128((x), (z)), \
                                     _mm_andnot_si128((z), (y)))
#define OPS_H(x, y, z)  _mm_xor_si128(_mm_xor_si128((x), (y)), (z))
#define OPS_I(x, y, z)  _mm_xor_si128((y), \
                            _mm_or_si128((x), \
                                _mm_xor_si128((z), _mm_set1_epi32(-1))))

/* No 64-bit rotate before AVX-512: shift, shift, or. */
#define OPS64_VEC        __m128i
#define OPS64_LANES      2
#define OPS64_SET1(x)    _mm_set1_epi64x((long long) (x))
#define OPS64_ADD(a, b)  _mm_add_epi64((a), (b))
#define OPS64_XOR(a, b)  _mm_xor_si128((a), (b))
#define OPS64_ROTR(x, n) _mm_or_si128(_mm_srli_epi64((x), (n)), \
                                      _mm_slli_epi64((x), 64 - (n)))
#define OPS64_SHR(x, n)  _mm_srli_epi64((x), (n))
#define OPS64_STORE(p, v) _mm_storeu_si128((__m128i *) (void *) (p), (v))
#define OPS64_LOAD(p)     _mm_loadu_si128((const __m128i *) (const void *) (p))
#define OPS64_CH(x, y, z)  _mm_xor_si128(_mm_and_si128((x), (y)), \
                                         _mm_andnot_si128((x), (z)))
#define OPS64_MAJ(x, y, z) _mm_xor_si128(_mm_xor_si128(_mm_and_si128((x), (y)), \
                                                       _mm_and_si128((x), (z))), \
                                         _mm_and_si128((y), (z)))

/* Every algorithm at every stream count, from the shared matrix. */
#define VB_ISA sse2
#include "instantiate_all.h"
