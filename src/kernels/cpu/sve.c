/*
 * sve.c -- vector-length-agnostic SVE kernels.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * The one ISA here whose vector width is not known when the code is compiled.
 * SVE vectors are 128 to 2048 bits, chosen by the hardware, and a single
 * binary has to run well at any of them -- which is not hypothetical: Neoverse
 * V1 offers 256 bits and its successor V2 offers 128, so the newer and faster
 * part has the narrower vector.
 *
 * Two consequences shape this file.
 *
 * SVE types are *sizeless*: svuint32_t cannot go in an array, a struct or a
 * union, and sizeof does not apply. The shared templates hold per-stream state
 * in arrays indexed by stream, so this file overrides the hooks they provide
 * and holds that state in separately named variables instead, reaching them by
 * pasting the stream index -- which is always a literal, because the templates
 * expand the streams rather than looping over them.
 *
 * The lane count is a run-time value. It is svcntw() for 32-bit words and
 * svcntd() for 64-bit, it multiplies every corpus offset, and the registry
 * learns it through vb_sve_lanes32/64 rather than from a constant column.
 *
 * What SVE does *not* have is as interesting as what it does. There is no
 * three-input bitwise select, so MD5's F and G and SHA's Ch cost three
 * instructions where NEON spends one; no shift-right-insert, so a rotate is
 * three instructions rather than two; no three-way XOR and no fused
 * xor-rotate. All four arrived with SVE2, which is why sve2.c exists beside
 * this file rather than sharing it. On Neoverse V1 that means twice NEON's
 * width with roughly SSE2's operation set, and whether the width wins is
 * exactly the measurement.
 */

#include <stdint.h>
#include <arm_sve.h>

/*
 * Every lane active, always. The corpus is built as a whole number of vectors
 * -- vb_batch_divides() refuses a kernel whose lanes x streams does not divide
 * the batch -- so there is no tail to predicate off, and a governing predicate
 * of all-true is the honest description rather than a shortcut.
 */
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

/* No rotate and no shift-right-insert: shift both ways and or the halves. */
#define OPS_ROTL(x, n)                                                  \
    svorr_u32_x(SVE_P32, svlsl_n_u32_x(SVE_P32, (x), (n)),              \
                         svlsr_n_u32_x(SVE_P32, (x), 32 - (n)))

/* No BSL either, so select is and/bic/or -- three instructions for what NEON
   and SVE2 do in one. This is the single biggest reason to expect SVE1 to
   fall short of its width advantage. */
#define OPS_CH(x, y, z)                                                 \
    svorr_u32_x(SVE_P32, svand_u32_x(SVE_P32, (x), (y)),                \
                         svbic_u32_x(SVE_P32, (z), (x)))
#define OPS_MAJ(x, y, z)                                                \
    svorr_u32_x(SVE_P32,                                                \
        svand_u32_x(SVE_P32, (x), (y)),                                 \
        svorr_u32_x(SVE_P32, svand_u32_x(SVE_P32, (x), (z)),            \
                             svand_u32_x(SVE_P32, (y), (z))))

#define OPS_F(x, y, z)  OPS_CH(x, y, z)
#define OPS_G(x, y, z)  OPS_CH(z, x, y)
#define OPS_H(x, y, z)  sveor_u32_x(SVE_P32, sveor_u32_x(SVE_P32, (x), (y)), (z))
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
#define OPS64_ROTR(x, n)                                                \
    svorr_u64_x(SVE_P64, svlsr_n_u64_x(SVE_P64, (x), (n)),              \
                         svlsl_n_u64_x(SVE_P64, (x), 64 - (n)))
#define OPS64_CH(x, y, z)                                               \
    svorr_u64_x(SVE_P64, svand_u64_x(SVE_P64, (x), (y)),                \
                         svbic_u64_x(SVE_P64, (z), (x)))
#define OPS64_MAJ(x, y, z)                                              \
    svorr_u64_x(SVE_P64,                                                \
        svand_u64_x(SVE_P64, (x), (y)),                                 \
        svorr_u64_x(SVE_P64, svand_u64_x(SVE_P64, (x), (z)),            \
                             svand_u64_x(SVE_P64, (y), (z))))

/* Every algorithm at every stream count, from the shared matrix. */
#define VB_ISA sve
#define VB_MD5_HOOKS    "sve_md5_hooks.h"
#define VB_SHA1_HOOKS   "sve_sha1_hooks.h"
#define VB_SHA512_HOOKS "sve_sha512_hooks.h"
#include "instantiate_all.h"
