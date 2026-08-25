/*
 * sve_md5_hooks.h -- SVE overrides for the MD5 template.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * Included once per stream count, immediately before the template, because
 * the template #undef's its hooks on the way out and because these depend on
 * MD5K_STREAMS. See sve_hooks.h for why any of this is necessary.
 */

#include "sve_hooks.h"

/* ---- MD5 ---------------------------------------------------------------- */
#define MD5K_V(name, k)      VB_SVE_V(name, k)
#define MD5K_V2(name, k, i)  VB_SVE_V2(name, k, i)
#define MD5K_FOLDN           VB_MAX_LANES

/*
 * Round constants: the same as the template's default.
 *
 * Pinning the table base in a register with an empty asm was tried and is 30%
 * worse. It does what it promises in isolation -- eight MD5 rounds go from 52
 * instructions to 36, because ld1rw takes an immediate offset and stops
 * needing adrp+add for every constant -- but the kernel has no register to
 * spare, and holding one across the whole function costs more in spills than
 * the addressing saves. Measured, not assumed.
 */
#define MD5K_TDECL                                      \
    const uint32_t *md5k_tbase = MD5_T;                 \
    __asm__ ("" : "+r" (md5k_tbase));
#define MD5K_TC(TI) MD5K_SET1(md5k_tbase[TI])


#define MD5K_SVE_DECL(k)                                                    \
    MD5K_VEC MD5K_V2(wv,k,0), MD5K_V2(wv,k,1),                              \
             MD5K_V2(wv,k,2), MD5K_V2(wv,k,3);                              \
    MD5K_VEC MD5K_V(h0,k), MD5K_V(h1,k), MD5K_V(h2,k), MD5K_V(h3,k);        \
    MD5K_VEC MD5K_V(A,k), MD5K_V(B,k), MD5K_V(C,k), MD5K_V(D,k);

#if MD5K_STREAMS == 1
#  define MD5K_FOREACH(BODY) BODY(0)
#  define MD5K_SDECL_ALL MD5K_SVE_DECL(0)
#elif MD5K_STREAMS == 2
#  define MD5K_FOREACH(BODY) BODY(0) BODY(1)
#  define MD5K_SDECL_ALL MD5K_SVE_DECL(0) \
                        MD5K_SVE_DECL(1)
#elif MD5K_STREAMS == 3
#  define MD5K_FOREACH(BODY) BODY(0) BODY(1) BODY(2)
#  define MD5K_SDECL_ALL MD5K_SVE_DECL(0) \
                        MD5K_SVE_DECL(1) \
                        MD5K_SVE_DECL(2)
#elif MD5K_STREAMS == 4
#  define MD5K_FOREACH(BODY) BODY(0) BODY(1) BODY(2) BODY(3)
#  define MD5K_SDECL_ALL MD5K_SVE_DECL(0) \
                        MD5K_SVE_DECL(1) \
                        MD5K_SVE_DECL(2) \
                        MD5K_SVE_DECL(3)
#elif MD5K_STREAMS == 6
#  define MD5K_FOREACH(BODY) BODY(0) BODY(1) BODY(2) BODY(3) BODY(4) BODY(5)
#  define MD5K_SDECL_ALL MD5K_SVE_DECL(0) \
                        MD5K_SVE_DECL(1) \
                        MD5K_SVE_DECL(2) \
                        MD5K_SVE_DECL(3) \
                        MD5K_SVE_DECL(4) \
                        MD5K_SVE_DECL(5)
#elif MD5K_STREAMS == 8
#  define MD5K_FOREACH(BODY) BODY(0) BODY(1) BODY(2) BODY(3) BODY(4) BODY(5) BODY(6) BODY(7)
#  define MD5K_SDECL_ALL MD5K_SVE_DECL(0) \
                        MD5K_SVE_DECL(1) \
                        MD5K_SVE_DECL(2) \
                        MD5K_SVE_DECL(3) \
                        MD5K_SVE_DECL(4) \
                        MD5K_SVE_DECL(5) \
                        MD5K_SVE_DECL(6) \
                        MD5K_SVE_DECL(7)
#endif

/* Word 0..3 live in named registers; 4..15 come from the corpus. A table,
   because "register or memory" must be decided before the name exists. */
#define MD5K_W_0(k)  MD5K_V2(wv, k, 0)
#define MD5K_W_1(k)  MD5K_V2(wv, k, 1)
#define MD5K_W_2(k)  MD5K_V2(wv, k, 2)
#define MD5K_W_3(k)  MD5K_V2(wv, k, 3)
#define MD5K_W_N(k, WI) MD5K_LOAD(wp[k] + (WI) * MD5K_LANES)
#define MD5K_W_4(k)  MD5K_W_N(k,  4)
#define MD5K_W_5(k)  MD5K_W_N(k,  5)
#define MD5K_W_6(k)  MD5K_W_N(k,  6)
#define MD5K_W_7(k)  MD5K_W_N(k,  7)
#define MD5K_W_8(k)  MD5K_W_N(k,  8)
#define MD5K_W_9(k)  MD5K_W_N(k,  9)
#define MD5K_W_10(k) MD5K_W_N(k, 10)
#define MD5K_W_11(k) MD5K_W_N(k, 11)
#define MD5K_W_12(k) MD5K_W_N(k, 12)
#define MD5K_W_13(k) MD5K_W_N(k, 13)
#define MD5K_W_14(k) MD5K_W_N(k, 14)
#define MD5K_W_15(k) MD5K_W_N(k, 15)
#define MD5K_WSEL_(WI)  MD5K_W_##WI
#define MD5K_WSEL(WI)   MD5K_WSEL_(WI)
#define MD5K_W(k, WI)   MD5K_WSEL(WI)(k)

