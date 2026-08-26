/*
 * sve2.c -- vector-length-agnostic SVE2 kernels.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * The same vector-length-agnostic structure as sve.c beside it, and a
 * different instruction set. That is the whole reason both exist.
 *
 * SVE1 has none of the bit-manipulation operations this workload wants. SVE2
 * adds all of them:
 *
 *   BSL   three-input bitwise select, so MD5's F and G and SHA's Ch cost one
 *         instruction instead of three
 *   EOR3  three-way XOR, for MD5's H and SHA-1's Parity
 *   SRI   shift-right-and-insert, so a rotate is two instructions not three
 *   XAR   xor then rotate in one, which SHA-1's schedule expansion ends with
 *
 * So SVE2 is not "SVE, newer": for an add-rotate-xor hash it is a materially
 * better instruction set at the same width. Neoverse V2 offers SVE2 at 128
 * bits where V1 offers SVE at 256, which makes the pair the cleanest test in
 * this project of whether instruction set or vector width matters more --
 * three instruction sets at one width on V2, and width doubling against a
 * worse instruction set on V1.
 *
 * Operand orders below were established by running them, not by reading the
 * specification: svbsl(a, b, sel) selects on its *third* argument, and
 * svxar(a, b, n) is a rotate *right* of a ^ b.
 */

#include <stdint.h>
#include <arm_sve.h>

#define SVE_P32 svptrue_b32()
#define SVE_P64 svptrue_b64()

/* ---- 32-bit operation set (MD5, SHA-1) ---------------------------------- */

#define OPS_VEC         svuint32_t
#define OPS_LANES       svcntw()
#define OPS_SET1(x)     svdup_n_u32((uint32_t) (x))
#define OPS_ADD(a, b)   svadd_u32_x(SVE_P32, (a), (b))
#define OPS_XOR(a, b)   sveor_u32_x(SVE_P32, (a), (b))
#define OPS_STORE(p, v) svst1_u32(SVE_P32, (uint32_t *) (void *) (p), (v))
#define OPS_LOAD(p)     svld1_u32(SVE_P32, (const uint32_t *) (const void *) (p))

/*
 * XAR is xor-then-rotate-right in one instruction. With a zero operand it is a
 * plain rotate, and rotating left by n is rotating right by 32 - n. One
 * instruction where shift-left plus shift-right-and-insert is two.
 */
#define OPS_ROTL(x, n)  svxar_n_u32((x), svdup_n_u32(0), 32 - (n))

/* svbsl(a, b, sel) = (a & sel) | (b & ~sel), selector last. */
#define OPS_CH(x, y, z)  svbsl_u32((y), (z), (x))
#define OPS_MAJ(x, y, z) svbsl_u32((z), (x), sveor_u32_x(SVE_P32, (x), (y)))

#define OPS_F(x, y, z)  OPS_CH(x, y, z)
#define OPS_G(x, y, z)  OPS_CH(z, x, y)
#define OPS_H(x, y, z)  sveor3_u32((x), (y), (z))

/* Capabilities the templates ask for by name. EOR3 collapses a three-way XOR
   into one instruction; XAR fuses the XOR that precedes a rotate into it. Both
   appear in SHA-1's schedule expansion and all four of SHA-512's sigmas. */
#define OPS_XOR3(a, b, c)   sveor3_u32((a), (b), (c))
#define OPS_XORROT(a, b, n) svxar_n_u32((a), (b), 32 - (n))
#define OPS_I(x, y, z)                                                  \
    sveor_u32_x(SVE_P32, (y),                                           \
        svorr_u32_x(SVE_P32, (x), svnot_u32_x(SVE_P32, (z))))

/* ---- 64-bit operation set (SHA-512) ------------------------------------- */

#define OPS64_VEC         svuint64_t
#define OPS64_LANES       svcntd()
#define OPS64_SET1(x)     svdup_n_u64((uint64_t) (x))
#define OPS64_ADD(a, b)   svadd_u64_x(SVE_P64, (a), (b))
#define OPS64_XOR(a, b)   sveor_u64_x(SVE_P64, (a), (b))
#define OPS64_SHR(x, n)   svlsr_n_u64_x(SVE_P64, (x), (n))
#define OPS64_STORE(p, v) svst1_u64(SVE_P64, (uint64_t *) (void *) (p), (v))
#define OPS64_LOAD(p)     svld1_u64(SVE_P64, (const uint64_t *) (const void *) (p))
#define OPS64_ROTR(x, n)  svsri_n_u64(svlsl_n_u64_x(SVE_P64, (x), 64 - (n)), (x), (n))
#define OPS64_CH(x, y, z)  svbsl_u64((y), (z), (x))
#define OPS64_MAJ(x, y, z) svbsl_u64((z), (x), sveor_u64_x(SVE_P64, (x), (y)))
/*
 * No OPS64_XOR3, because defining it changes nothing: both toolchains already
 * contract sveor(sveor(a,b),c) into EOR3 in the sigma expansion. Adding the
 * macro leaves clang's object byte-identical and gcc's mix unchanged -- 288
 * EOR3 either way for sha512/sve2-s1, one extra movprfx and some scheduling
 * churn. An earlier note put the cost at 3% and blamed EOR3's destructive
 * encoding; whatever that measured, it was not this macro.
 *
 * The 32-bit OPS_XOR3 above is not the same story, and not for the reason the
 * old note gave either. It is worth nothing to MD5's H -- identical objects
 * with and without -- but SHA-1's schedule needs it spelled out: removing it
 * costs 4.7% of gcc's instructions and 15.1% of clang's on sha1/sve2-s1. The
 * EOR3 count is 104 either way, so the macro is not adding EOR3s; it is
 * keeping the compiler from materialising the intermediate XOR.
 */

/* Every algorithm at every stream count, from the shared matrix. The hooks are
   the same ones sve.c uses: the sizeless-type problem is identical. */
#define VB_ISA sve2
#define VB_MD5_HOOKS    "sve_md5_hooks.h"
#define VB_SHA1_HOOKS   "sve_sha1_hooks.h"
#define VB_SHA512_HOOKS "sve_sha512_hooks.h"
#include "instantiate_all.h"
