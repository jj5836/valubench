/*
 * sve_sha1_hooks.h -- SVE overrides for the SHA-1 template.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * Included once per stream count, immediately before the template, because
 * the template #undef's its hooks on the way out and because these depend on
 * S1K_STREAMS. See sve_hooks.h for why any of this is necessary.
 */

#include "sve_hooks.h"

/* ---- SHA-1 -------------------------------------------------------------- */
#define S1K_V(name, k)      VB_SVE_V(name, k)
#define S1K_V2(name, k, i)  VB_SVE_V2(name, k, i)
#define S1K_FOLDN           VB_MAX_LANES
#define S1K_FOR5(BODY, k)   BODY(k,0) BODY(k,1) BODY(k,2) BODY(k,3) BODY(k,4)

#define S1K_SVE_DECL(k)                                                     \
    S1K_VEC S1K_V2(fb,k,0), S1K_V2(fb,k,1), S1K_V2(fb,k,2),                 \
            S1K_V2(fb,k,3), S1K_V2(fb,k,4);                                 \
    S1K_VEC S1K_V(h0,k), S1K_V(h1,k), S1K_V(h2,k),                          \
            S1K_V(h3,k), S1K_V(h4,k);                                       \
    S1K_VEC S1K_V(A,k), S1K_V(B,k), S1K_V(C,k), S1K_V(D,k), S1K_V(E,k);

#if S1K_STREAMS == 1
#  define S1K_FOREACH(BODY) BODY(0)
#  define S1K_SDECL_ALL         S1K_SVE_DECL(0)
#elif S1K_STREAMS == 2
#  define S1K_FOREACH(BODY) BODY(0) BODY(1)
#  define S1K_SDECL_ALL         S1K_SVE_DECL(0) S1K_SVE_DECL(1)
#elif S1K_STREAMS == 3
#  define S1K_FOREACH(BODY) BODY(0) BODY(1) BODY(2)
#  define S1K_SDECL_ALL         S1K_SVE_DECL(0) S1K_SVE_DECL(1) S1K_SVE_DECL(2)
#elif S1K_STREAMS == 4
#  define S1K_FOREACH(BODY) BODY(0) BODY(1) BODY(2) BODY(3)
#  define S1K_SDECL_ALL         S1K_SVE_DECL(0) S1K_SVE_DECL(1)                  \
                            S1K_SVE_DECL(2) S1K_SVE_DECL(3)
#endif

/* The schedule window: a scalar buffer, strided by the run-time lane count. */
#define S1K_WDECL           uint32_t wbuf[S1K_STREAMS * 16 * VB_MAX_LANES];
#define S1K_WAT(k, i)       (wbuf + (((k) * 16 + (i)) * (size_t) S1K_LANES))
#define S1K_WGET(k, i)      S1K_LOAD(S1K_WAT(k, i))
#define S1K_WSET(k, i, v)   S1K_STORE(S1K_WAT(k, i), (v))

