/*
 * kernel_scalar.c -- portable single-lane kernels.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * No intrinsics, no ISA assumptions. This is the fallback that lets valubench
 * produce a number on any C11 target, and the baseline the SIMD paths are
 * measured against.
 *
 * Stream count still matters here. MD5's serial dependency chain leaves a
 * scalar core just as idle as a vector one, so the scalar path is instantiated
 * at 1..4 streams like every other (docs/research.md 2.4).
 */

#include <stdint.h>

static inline uint32_t sc_rotl(uint32_t v, unsigned s)
{
    return (v << s) | (v >> ((32u - s) & 31u));
}

#define OPS_VEC        uint32_t
#define OPS_LANES      1
#define OPS_SET1(x)    ((uint32_t) (x))
#define OPS_ADD(a, b)  ((uint32_t) ((a) + (b)))
#define OPS_XOR(a, b)  ((a) ^ (b))
#define OPS_ROTL(x, n) sc_rotl((x), (n))
#define OPS_STORE(p, v) ((p)[0] = (v))
#define OPS_LOAD(p)    ((p)[0])

#define OPS_CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define OPS_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

#define OPS_F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define OPS_G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define OPS_H(x, y, z) ((x) ^ (y) ^ (z))
#define OPS_I(x, y, z) ((y) ^ ((x) | ~(z)))

static inline uint64_t sc_rotr64(uint64_t v, unsigned s)
{
    return (v >> s) | (v << ((64u - s) & 63u));
}

#define OPS64_VEC        uint64_t
#define OPS64_LANES      1
#define OPS64_SET1(x)    ((uint64_t) (x))
#define OPS64_ADD(a, b)  ((uint64_t) ((a) + (b)))
#define OPS64_XOR(a, b)  ((a) ^ (b))
#define OPS64_ROTR(x, n) sc_rotr64((x), (n))
#define OPS64_SHR(x, n)  ((x) >> (n))
#define OPS64_STORE(p, v) ((p)[0] = (v))
#define OPS64_LOAD(p)    ((p)[0])
#define OPS64_CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define OPS64_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

/* Every algorithm at every stream count, from the shared matrix. */
#define VB_ISA scalar
#include "instantiate_all.h"
