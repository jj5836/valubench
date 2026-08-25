/*
 * neon.c -- 4-lane ARM Advanced SIMD (NEON) kernels.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * The same 128-bit width as SSE2, and the comparison between the two is part of
 * the point: the operation sets differ in ways that show up in the round
 * functions rather than in the lane count.
 *
 * Where NEON is better than SSE2:
 *
 *   vbslq_u32  is a true 3-input bitwise select, so MD5's F and G and SHA's
 *              Ch each cost one instruction where SSE2 needs three. This is
 *              the same win vpternlogd gives AVX-512, arriving by a different
 *              route and at a quarter of the width.
 *   vsriq_n_u32 shift-right-and-insert composes a rotate in two instructions
 *              rather than three, since the insert folds in the or.
 *
 * Where it is not:
 *
 *   There is still no single rotate instruction, and no 4-input operation, so
 *   Maj and the round-3 XOR chain cost what they cost on SSE2.
 *
 * NEON is architecturally mandatory on AArch64, so no -m flag is needed and the
 * runtime predicate is a formality -- see vb_cpu_has_neon().
 */

#include <stdint.h>
#include <arm_neon.h>

#define OPS_VEC         uint32x4_t
#define OPS_LANES       4
#define OPS_SET1(x)     vdupq_n_u32((uint32_t) (x))
#define OPS_ADD(a, b)   vaddq_u32((a), (b))
#define OPS_XOR(a, b)   veorq_u32((a), (b))
#define OPS_STORE(p, v) vst1q_u32((uint32_t *) (void *) (p), (v))
#define OPS_LOAD(p)     vld1q_u32((const uint32_t *) (const void *) (p))

/*
 * Rotate in two instructions: shift left, then shift-right-and-insert, which
 * ors the displaced bits back in without a separate vorrq.
 */
#define OPS_ROTL(x, n)  vsriq_n_u32(vshlq_n_u32((x), (n)), (x), 32 - (n))

/*
 * vbslq_u32(mask, a, b) selects a where mask has 1 bits and b where it has 0,
 * which is exactly MD5's F and SHA's Ch in one instruction.
 */
#define OPS_CH(x, y, z)  vbslq_u32((x), (y), (z))
#define OPS_MAJ(x, y, z) vbslq_u32(veorq_u32((x), (y)), (z), (x))

#define OPS_F(x, y, z)  vbslq_u32((x), (y), (z))
#define OPS_G(x, y, z)  vbslq_u32((z), (x), (y))
#define OPS_H(x, y, z)  veorq_u32(veorq_u32((x), (y)), (z))
#define OPS_I(x, y, z)  veorq_u32((y), vorrq_u32((x), vmvnq_u32((z))))

/* SHA-512: two 64-bit lanes, and the same select and insert tricks apply. */
#define OPS64_VEC        uint64x2_t
#define OPS64_LANES      2
#define OPS64_SET1(x)    vdupq_n_u64((uint64_t) (x))
#define OPS64_ADD(a, b)  vaddq_u64((a), (b))
#define OPS64_XOR(a, b)  veorq_u64((a), (b))
#define OPS64_ROTR(x, n) vsriq_n_u64(vshlq_n_u64((x), 64 - (n)), (x), (n))
#define OPS64_SHR(x, n)  vshrq_n_u64((x), (n))
#define OPS64_STORE(p, v) vst1q_u64((uint64_t *) (void *) (p), (v))
#define OPS64_LOAD(p)     vld1q_u64((const uint64_t *) (const void *) (p))
#define OPS64_CH(x, y, z)  vbslq_u64((x), (y), (z))
#define OPS64_MAJ(x, y, z) vbslq_u64(veorq_u64((x), (y)), (z), (x))

/* Every algorithm at every stream count, from the shared matrix. */
#define VB_ISA neon
#include "instantiate_all.h"
