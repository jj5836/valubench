/*
 * sha512_kernel_impl.h -- the multi-way SHA-512 kernel, written once.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * Same shape as the MD5 and SHA-1 templates, on 64-bit words. This is the
 * kernel that justified generalising the harness: it is the first with a word
 * size other than 32 bits, a block other than 64 bytes, and eight state words.
 *
 * A rolling sixteen-entry window holds the schedule, as in SHA-1, but the
 * recurrence is arithmetic rather than pure XOR:
 *
 *   W[t] = SSIG1(W[t-2]) + W[t-7] + SSIG0(W[t-15]) + W[t-16]
 *
 * so SHA-512 puts real adds and rotates in the schedule as well as the round
 * function -- a third distinct instruction mix from MD5's and SHA-1's.
 *
 * Note the lane count halves for a given register width: an AVX2 register holds
 * eight 32-bit messages but only four 64-bit ones. The registry records that,
 * and the batch divisibility invariant still holds because every lane count in
 * use divides VB_BATCH_LCM.
 */

#include "valubench.h"
#include "sha512_const.h"

#include <stddef.h>

/* FIPS 180-4 section 4.1.3. */
#define S5K_BSIG0(x) S5K_XOR(S5K_XOR(S5K_ROTR(x, 28), S5K_ROTR(x, 34)), S5K_ROTR(x, 39))
#define S5K_BSIG1(x) S5K_XOR(S5K_XOR(S5K_ROTR(x, 14), S5K_ROTR(x, 18)), S5K_ROTR(x, 41))
#define S5K_SSIG0(x) S5K_XOR(S5K_XOR(S5K_ROTR(x,  1), S5K_ROTR(x,  8)), S5K_SHR(x, 7))
#define S5K_SSIG1(x) S5K_XOR(S5K_XOR(S5K_ROTR(x, 19), S5K_ROTR(x, 61)), S5K_SHR(x, 6))

/*
 * SVE hooks, each defaulting to the code already here, so a fixed-width ISA
 * compiles byte-for-byte what it did before -- verified against the
 * pre-refactor objects. See the MD5 template for why defaulting rather than
 * converting: expanding the stream loops unconditionally measured several
 * percent slower on the scalar kernels, and those are the baseline every
 * vector ratio is divided by.
 */
#ifndef S5K_V
#  define S5K_V(name, k)      name[k]
#  define S5K_V2(name, k, i)  name[k][i]
#  define S5K_SDECL_ALL                                                    \
        S5K_VEC fb[S5K_STREAMS][8];      /* digest fed into block 0 */      \
        S5K_VEC h[S5K_STREAMS][8];                                          \
        S5K_VEC A[S5K_STREAMS], B[S5K_STREAMS], C[S5K_STREAMS],             \
                D[S5K_STREAMS];                                             \
        S5K_VEC E[S5K_STREAMS], F[S5K_STREAMS], G[S5K_STREAMS],             \
                H[S5K_STREAMS];
#  define S5K_ACCDECL         S5K_VEC acc[8];
#  define S5K_ACCV(j)         acc[j]
#  define S5K_FOREACH(BODY) \
       for (unsigned k = 0; k < S5K_STREAMS; k++) { BODY(k) }
#  define S5K_FOR8(BODY)     for (int j = 0; j < 8; j++) { BODY(j) }
#  define S5K_FOR8K(BODY, k) for (int j = 0; j < 8; j++) { BODY(k, j) }
#  define S5K_FOLDN S5K_LANES
#endif

#ifndef S5K_WDECL
#  define S5K_WDECL         S5K_VEC w[S5K_STREAMS][16];
#  define S5K_WGET(k, i)    w[k][(i)]
#  define S5K_WSET(k, i, v) w[k][(i)] = (v)
#endif

#define S5K_STEP1(k, t)                                                   \
    {                                                                     \
        S5K_VEC t1 = S5K_ADD(S5K_V(H,k), S5K_BSIG1(S5K_V(E,k)));                      \
        t1 = S5K_ADD(t1, S5K_F_CH(S5K_V(E,k), S5K_V(F,k), S5K_V(G,k)));                     \
        t1 = S5K_ADD(t1, S5K_SET1(SHA512_K[t]));                          \
        t1 = S5K_ADD(t1, S5K_WGET(k, (t) & 15));                                 \
        S5K_VEC t2 = S5K_ADD(S5K_BSIG0(S5K_V(A,k)),\
                             S5K_F_MAJ(S5K_V(A,k), S5K_V(B,k), S5K_V(C,k))); \
        S5K_V(H,k) = S5K_V(G,k);                                                      \
        S5K_V(G,k) = S5K_V(F,k);                                                      \
        S5K_V(F,k) = S5K_V(E,k);                                                      \
        S5K_V(E,k) = S5K_ADD(S5K_V(D,k), t1);                                         \
        S5K_V(D,k) = S5K_V(C,k);                                                      \
        S5K_V(C,k) = S5K_V(B,k);                                                      \
        S5K_V(B,k) = S5K_V(A,k);                                                      \
        S5K_V(A,k) = S5K_ADD(t1, t2);                                           \
    }

#define S5K_EXPAND1(k, t)                                                 \
    {                                                                     \
        S5K_VEC x = S5K_ADD(S5K_SSIG1(S5K_WGET(k, ((t) - 2) & 15)),              \
                            S5K_WGET(k, ((t) - 7) & 15));                        \
        x = S5K_ADD(x, S5K_SSIG0(S5K_WGET(k, ((t) - 15) & 15)));                 \
        x = S5K_ADD(x, S5K_WGET(k, ((t) - 16) & 15));                            \
        S5K_WSET(k, (t) & 15, x);                                               \
    }

#if S5K_STREAMS == 1
#  define S5K_EACH(M, ...) M(0, __VA_ARGS__)
#elif S5K_STREAMS == 2
#  define S5K_EACH(M, ...) M(0, __VA_ARGS__) M(1, __VA_ARGS__)
#elif S5K_STREAMS == 3
#  define S5K_EACH(M, ...) M(0, __VA_ARGS__) M(1, __VA_ARGS__) M(2, __VA_ARGS__)
#elif S5K_STREAMS == 4
#  define S5K_EACH(M, ...) M(0, __VA_ARGS__) M(1, __VA_ARGS__) M(2, __VA_ARGS__) M(3, __VA_ARGS__)
#else
#  error "S5K_STREAMS must be 1..4"
#endif

#define S5K_STEP(t)   S5K_EACH(S5K_STEP1, t)
#define S5K_EXPAND(t) S5K_EACH(S5K_EXPAND1, t)

/*
 * The eighty steps, sixteen at a time.
 *
 * Still expansion, not iteration: S5K_RUN16 emits sixteen copies and the
 * preprocessor resolves every index, so the compiler sees the same straight
 * line of steps it saw when all eighty were written out. That property is the
 * whole reason this file avoids loops (see S5K_EACH above) and it survives
 * here -- what is gone is 144 lines of transcription, not the expansion.
 *
 * Two step forms, because the schedule differs: words 0..15 arrive from the
 * corpus, and every word after that is expanded in place immediately before
 * the step that first reads it.
 */
#define S5K_PLAIN(t)    S5K_STEP(t)
#define S5K_EXPANDED(t) S5K_EXPAND(t) S5K_STEP(t)

#define S5K_RUN16(DO, base)                                         \
    DO((base) +  0) DO((base) +  1) DO((base) +  2) DO((base) +  3) \
    DO((base) +  4) DO((base) +  5) DO((base) +  6) DO((base) +  7) \
    DO((base) +  8) DO((base) +  9) DO((base) + 10) DO((base) + 11) \
    DO((base) + 12) DO((base) + 13) DO((base) + 14) DO((base) + 15)

void S5K_NAME(const void *corpus_v, uint64_t n_groups, uint32_t blocks,
              uint32_t iterations, uint64_t checksum[VB_MAX_DIGEST_WORDS]);

void S5K_NAME(const void *corpus_v, uint64_t n_groups, uint32_t blocks,
              uint32_t iterations, uint64_t checksum[VB_MAX_DIGEST_WORDS])
{
    const uint64_t *corpus = (const uint64_t *) corpus_v;

    const size_t slot_words  = (size_t) blocks * VB_WORDS_PER_BLOCK * S5K_LANES;
    const size_t block_words = (size_t) VB_WORDS_PER_BLOCK * S5K_LANES;

    if (iterations == 0 || blocks == 0) {
        for (unsigned i = 0; i < VB_MAX_DIGEST_WORDS; i++)
            checksum[i] = 0;
        return;
    }

    S5K_ACCDECL
#define S5K_ACC_ZERO(j) S5K_ACCV(j) = S5K_SET1(0);
    S5K_FOR8(S5K_ACC_ZERO)
#undef S5K_ACC_ZERO

    for (uint64_t g = 0; g < n_groups; g++) {
        const uint64_t *slot[S5K_STREAMS];
        S5K_WDECL
        S5K_SDECL_ALL

#define S5K_LOAD_FB(k, j) \
        S5K_V2(fb,k,j) = S5K_LOAD(slot[k] + (size_t) (j) * S5K_LANES);
#define S5K_LOAD_SLOT(k)                                         \
        slot[k] = corpus + (g * S5K_STREAMS + (k)) * slot_words;          \
        S5K_FOR8K(S5K_LOAD_FB, k)
        S5K_FOREACH(S5K_LOAD_SLOT)
#undef S5K_LOAD_SLOT

        for (uint32_t it = 0; it < iterations; it++) {

#define S5K_INIT_HJ(k, j) S5K_V2(h,k,j) = S5K_SET1(SHA512_IV[j]);
#define S5K_INIT_H(k)  S5K_FOR8K(S5K_INIT_HJ, k)
        S5K_FOREACH(S5K_INIT_H)
#undef S5K_INIT_H

        for (uint32_t b = 0; b < blocks; b++) {

#define S5K_FEED_FB(k, j) S5K_WSET(k, j, S5K_V2(fb,k,j));
#define S5K_START_BLOCK(k)                                       \
        {                                                                 \
            const uint64_t *wp = slot[k] + (size_t) b * block_words;      \
            for (int j = 0; j < 16; j++)                                  \
                S5K_WSET(k, j, S5K_LOAD(wp + (size_t) j * S5K_LANES));    \
            if (b == 0)                                                   \
                S5K_FOR8K(S5K_FEED_FB, k)                                     \
        }                                                                 \
        S5K_V(A,k) = S5K_V2(h,k,0); S5K_V(B,k) = S5K_V2(h,k,1);           \
        S5K_V(C,k) = S5K_V2(h,k,2); S5K_V(D,k) = S5K_V2(h,k,3);           \
        S5K_V(E,k) = S5K_V2(h,k,4); S5K_V(F,k) = S5K_V2(h,k,5);           \
        S5K_V(G,k) = S5K_V2(h,k,6); S5K_V(H,k) = S5K_V2(h,k,7);
        S5K_FOREACH(S5K_START_BLOCK)
#undef S5K_START_BLOCK


    S5K_RUN16(S5K_PLAIN,     0);
    S5K_RUN16(S5K_EXPANDED, 16);
    S5K_RUN16(S5K_EXPANDED, 32);
    S5K_RUN16(S5K_EXPANDED, 48);
    S5K_RUN16(S5K_EXPANDED, 64);

#define S5K_CHAIN(k)                                             \
        S5K_V2(h,k,0) = S5K_ADD(S5K_V2(h,k,0), S5K_V(A,k));               \
        S5K_V2(h,k,1) = S5K_ADD(S5K_V2(h,k,1), S5K_V(B,k));               \
        S5K_V2(h,k,2) = S5K_ADD(S5K_V2(h,k,2), S5K_V(C,k));               \
        S5K_V2(h,k,3) = S5K_ADD(S5K_V2(h,k,3), S5K_V(D,k));               \
        S5K_V2(h,k,4) = S5K_ADD(S5K_V2(h,k,4), S5K_V(E,k));               \
        S5K_V2(h,k,5) = S5K_ADD(S5K_V2(h,k,5), S5K_V(F,k));               \
        S5K_V2(h,k,6) = S5K_ADD(S5K_V2(h,k,6), S5K_V(G,k));               \
        S5K_V2(h,k,7) = S5K_ADD(S5K_V2(h,k,7), S5K_V(H,k));
        S5K_FOREACH(S5K_CHAIN)
#undef S5K_CHAIN

        }   /* blocks */

#define S5K_FB_J(k, j) S5K_V2(fb,k,j) = S5K_V2(h,k,j);
#define S5K_FEEDBACK(k)  S5K_FOR8K(S5K_FB_J, k)
        S5K_FOREACH(S5K_FEEDBACK)
#undef S5K_FEEDBACK

        }   /* iterations */

#define S5K_ACC_J(k, j) S5K_ACCV(j) = S5K_XOR(S5K_ACCV(j), S5K_V2(h,k,j));
#define S5K_ACCUM(k)  S5K_FOR8K(S5K_ACC_J, k)
        S5K_FOREACH(S5K_ACCUM)
#undef S5K_ACCUM
    }

    uint64_t tmp[S5K_FOLDN];
#define S5K_FOLD(j)                                                       \
    {                                                                     \
        uint64_t c = 0;                                                   \
        S5K_STORE(tmp, S5K_ACCV(j));                                      \
        for (unsigned l = 0; l < (unsigned) (S5K_LANES); l++)             \
            c ^= tmp[l];                                                  \
        checksum[j] = c;                                                  \
    }
    S5K_FOR8(S5K_FOLD)
#undef S5K_FOLD
}

#undef S5K_V
#undef S5K_V2
#undef S5K_SDECL_ALL
#undef S5K_ACCDECL
#undef S5K_ACCV
#undef S5K_FOREACH
#undef S5K_FOR8
#undef S5K_FOR8K
#undef S5K_FOLDN
#undef S5K_WDECL
#undef S5K_WGET
#undef S5K_WSET
#undef S5K_BSIG0
#undef S5K_BSIG1
#undef S5K_SSIG0
#undef S5K_SSIG1
#undef S5K_STEP1
#undef S5K_EXPAND1
#undef S5K_EACH
#undef S5K_STEP
#undef S5K_EXPAND
#undef S5K_PLAIN
#undef S5K_EXPANDED
#undef S5K_RUN16
#undef S5K_NAME
#undef S5K_VEC
#undef S5K_LANES
#undef S5K_STREAMS
#undef S5K_SET1
#undef S5K_ADD
#undef S5K_XOR
#undef S5K_ROTR
#undef S5K_SHR
#undef S5K_STORE
#undef S5K_LOAD
#undef S5K_F_CH
#undef S5K_F_MAJ
