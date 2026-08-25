/*
 * sve_sha512_hooks.h -- SVE overrides for the SHA-512 template.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * Included once per stream count, immediately before the template, because
 * the template #undef's its hooks on the way out and because these depend on
 * S5K_STREAMS. See sve_hooks.h for why any of this is necessary.
 */

#include "sve_hooks.h"

/* ---- SHA-512 ------------------------------------------------------------ */
#define S5K_V(name, k)      VB_SVE_V(name, k)
#define S5K_V2(name, k, i)  VB_SVE_V2(name, k, i)
#define S5K_FOLDN           VB_MAX_LANES

/* As the template default; see the MD5 hooks for the base-register experiment
   and why it lost. */
#define S5K_TDECL                                       \
    const uint64_t *s5k_tbase = SHA512_K;               \
    __asm__ ("" : "+r" (s5k_tbase));
#define S5K_TC(t) S5K_SET1(s5k_tbase[t])

#define S5K_FOR8(BODY)      BODY(0) BODY(1) BODY(2) BODY(3)                  \
                            BODY(4) BODY(5) BODY(6) BODY(7)
#define S5K_FOR8K(BODY, k)  BODY(k,0) BODY(k,1) BODY(k,2) BODY(k,3)          \
                            BODY(k,4) BODY(k,5) BODY(k,6) BODY(k,7)
#define S5K_ACCDECL         S5K_VEC acc_0, acc_1, acc_2, acc_3,              \
                                    acc_4, acc_5, acc_6, acc_7;
#define S5K_ACCV(j)         VB_SVE_V(acc_, j)

#define S5K_SVE_DECL(k)                                                     \
    S5K_VEC S5K_V2(fb,k,0), S5K_V2(fb,k,1), S5K_V2(fb,k,2), S5K_V2(fb,k,3), \
            S5K_V2(fb,k,4), S5K_V2(fb,k,5), S5K_V2(fb,k,6), S5K_V2(fb,k,7); \
    S5K_VEC S5K_V2(h,k,0), S5K_V2(h,k,1), S5K_V2(h,k,2), S5K_V2(h,k,3),     \
            S5K_V2(h,k,4), S5K_V2(h,k,5), S5K_V2(h,k,6), S5K_V2(h,k,7);     \
    S5K_VEC S5K_V(A,k), S5K_V(B,k), S5K_V(C,k), S5K_V(D,k),                 \
            S5K_V(E,k), S5K_V(F,k), S5K_V(G,k), S5K_V(H,k);

#if S5K_STREAMS == 1
#  define S5K_FOREACH(BODY) BODY(0)
#  define S5K_SDECL_ALL S5K_SVE_DECL(0)
#elif S5K_STREAMS == 2
#  define S5K_FOREACH(BODY) BODY(0) BODY(1)
#  define S5K_SDECL_ALL S5K_SVE_DECL(0) \
                        S5K_SVE_DECL(1)
#elif S5K_STREAMS == 3
#  define S5K_FOREACH(BODY) BODY(0) BODY(1) BODY(2)
#  define S5K_SDECL_ALL S5K_SVE_DECL(0) \
                        S5K_SVE_DECL(1) \
                        S5K_SVE_DECL(2)
#elif S5K_STREAMS == 4
#  define S5K_FOREACH(BODY) BODY(0) BODY(1) BODY(2) BODY(3)
#  define S5K_SDECL_ALL S5K_SVE_DECL(0) \
                        S5K_SVE_DECL(1) \
                        S5K_SVE_DECL(2) \
                        S5K_SVE_DECL(3)
#elif S5K_STREAMS == 6
#  define S5K_FOREACH(BODY) BODY(0) BODY(1) BODY(2) BODY(3) BODY(4) BODY(5)
#  define S5K_SDECL_ALL S5K_SVE_DECL(0) \
                        S5K_SVE_DECL(1) \
                        S5K_SVE_DECL(2) \
                        S5K_SVE_DECL(3) \
                        S5K_SVE_DECL(4) \
                        S5K_SVE_DECL(5)
#elif S5K_STREAMS == 8
#  define S5K_FOREACH(BODY) BODY(0) BODY(1) BODY(2) BODY(3) BODY(4) BODY(5) BODY(6) BODY(7)
#  define S5K_SDECL_ALL S5K_SVE_DECL(0) \
                        S5K_SVE_DECL(1) \
                        S5K_SVE_DECL(2) \
                        S5K_SVE_DECL(3) \
                        S5K_SVE_DECL(4) \
                        S5K_SVE_DECL(5) \
                        S5K_SVE_DECL(6) \
                        S5K_SVE_DECL(7)
#endif

#define S5K_WDECL           uint64_t wbuf[S5K_STREAMS * 16 * VB_MAX_LANES];
#define S5K_WAT(k, i)       (wbuf + ((size_t) ((k) * 16 + (i)) * (size_t) S5K_LANES))
#define S5K_WGET(k, i)      S5K_LOAD(S5K_WAT(k, i))
#define S5K_WSET(k, i, v)   S5K_STORE(S5K_WAT(k, i), (v))
