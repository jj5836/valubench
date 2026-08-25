/*
 * kernel_avx512.c -- 16-lane AVX-512 kernels.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * This is the path docs/research.md 4.1 predicts will beat AVX2 by more than the 2x
 * its width implies, because two single-instruction wins compound:
 *
 *   vpternlogd  computes any 3-input boolean function in one instruction, so
 *               each MD5 round function costs 1 op instead of ~3. The immediate
 *               is the truth table of the function over (a,b,c); the values
 *               below were derived from the definitions and cross-checked
 *               against the known 0x96 (three-way XOR) and 0xCA/0xE4 (mux)
 *               encodings.
 *
 *   vprold      rotates in one instruction instead of shift+shift+or.
 *
 * Per step that is ~6 ops down to ~2, on top of twice the lanes.
 *
 * Note what is deliberately absent: the H-round XOR-sharing trick that helps
 * AVX2 (docs/research.md 2.2b) is pointless here, because vpternlogd already does
 * x^y^z in a single instruction. Applying it would add work. This is why the
 * round functions are per-ISA macros rather than one shared definition.
 *
 * UNVERIFIED ON HARDWARE: the development machine is an Intel N100, which has
 * no AVX-512. This path compiles and is disassembled but has not been executed.
 * See docs/research.md 4.3.
 */

#include <stdint.h>
#include <immintrin.h>

#define OPS_VEC         __m512i
#define OPS_LANES       16
#define OPS_SET1(x)     _mm512_set1_epi32((int) (x))
#define OPS_ADD(a, b)   _mm512_add_epi32((a), (b))
#define OPS_XOR(a, b)   _mm512_xor_si512((a), (b))
#define OPS_STORE(p, v) _mm512_storeu_si512((void *) (p), (v))
#define OPS_LOAD(p)     _mm512_loadu_si512((const void *) (p))

/* One instruction, unlike the shift/shift/or every earlier x86 ISA needs. */
#define OPS_ROTL(x, n)  _mm512_rol_epi32((x), (n))

/*
 * Round functions as vpternlogd truth tables over (x, y, z):
 *
 *   F(x,y,z) = (x&y) | (~x&z)   = x ? y : z   -> 0xCA
 *   G(x,y,z) = (x&z) | (y&~z)   = z ? x : y   -> 0xE4
 *   H(x,y,z) = x^y^z                          -> 0x96
 *   I(x,y,z) = y ^ (x | ~z)                   -> 0x39
 */
/* Ch is the same mux as MD5's F (0xCA); Maj is the majority table (0xE8). */
#define OPS_CH(x, y, z)  _mm512_ternarylogic_epi32((x), (y), (z), 0xCA)
#define OPS_MAJ(x, y, z) _mm512_ternarylogic_epi32((x), (y), (z), 0xE8)

#define OPS_F(x, y, z)  _mm512_ternarylogic_epi32((x), (y), (z), 0xCA)
#define OPS_G(x, y, z)  _mm512_ternarylogic_epi32((x), (y), (z), 0xE4)
#define OPS_H(x, y, z)  _mm512_ternarylogic_epi32((x), (y), (z), 0x96)
#define OPS_I(x, y, z)  _mm512_ternarylogic_epi32((x), (y), (z), 0x39)

#define OPS64_VEC        __m512i
#define OPS64_LANES      8
#define OPS64_SET1(x)    _mm512_set1_epi64((long long) (x))
#define OPS64_ADD(a, b)  _mm512_add_epi64((a), (b))
#define OPS64_XOR(a, b)  _mm512_xor_si512((a), (b))
#define OPS64_ROTR(x, n) _mm512_ror_epi64((x), (n))
#define OPS64_SHR(x, n)  _mm512_srli_epi64((x), (n))
#define OPS64_STORE(p, v) _mm512_storeu_si512((void *) (p), (v))
#define OPS64_LOAD(p)     _mm512_loadu_si512((const void *) (p))
#define OPS64_CH(x, y, z)  _mm512_ternarylogic_epi64((x), (y), (z), 0xCA)
#define OPS64_MAJ(x, y, z) _mm512_ternarylogic_epi64((x), (y), (z), 0xE8)

/* Every algorithm at every stream count, from the shared matrix. */
#define VB_ISA avx512
#include "instantiate_all.h"
