/*
 * sha1_kernel_impl.h -- the multi-way SHA-1 kernel, written once.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * Same shape as md5_kernel_impl.h -- included once per (ISA, stream count) with
 * the vector operations supplied as macros -- but the algorithm differs in
 * three ways that matter to the code:
 *
 *   80 rounds, not 64.
 *   Five state words, not four.
 *   The sixteen message words are *expanded* to eighty rather than permuted, so
 *   there is real work in the schedule. A rolling sixteen-entry window holds it:
 *   W[t & 15] is overwritten in place, which is exactly the recurrence
 *   W[t] = ROTL1(W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16]) with the indices reduced.
 *
 * That expansion is why SHA-1 is a useful second algorithm here: it moves work
 * out of the round function and into the schedule, a materially different
 * instruction mix from MD5's.
 *
 * Required macros are the OPS_* set from the including file, mapped by
 * instantiate_sha1.h. All are #undef'd on the way out so the header can be
 * included again.
 */

#include "valubench.h"

#include <stddef.h>

/*
 * Guarded because the template is included once per stream count, and because
 * the SHA-NI template defines the same initial state: whichever is included
 * first in a translation unit provides it.
 */
#ifndef VB_SHA1_K_DEFINED
#define VB_SHA1_K_DEFINED
/*
 * SHA-1 constants, FIPS 180-4. K is the leading 30 fractional bits of sqrt(2),
 * sqrt(3), sqrt(5) and sqrt(10), one per twenty-round group; the initial state
 * is section 5.3.1. Recorded as documentation of where the values come from,
 * not as a build step -- they are a frozen standard, and the FIPS 180-4
 * known-answer vectors in tests/test_hashes.c fail on a single wrong digit.
 */
/* FIPS 180-4 section 4.2.1. One constant per twenty-round group. */
static const uint32_t SHA1_K[4] = {
    0x5a827999u, 0x6ed9eba1u, 0x8f1bbcdcu, 0xca62c1d6u,
};
#endif

#ifndef VB_SHA1_IV_DEFINED
#define VB_SHA1_IV_DEFINED
/* FIPS 180-4 section 5.3.1. */
static const uint32_t SHA1_IV[5] = {
    0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u,
};
#endif

/*
 * SVE hooks. Each defaults to the code that was already here, so a fixed-width
 * ISA compiles byte-for-byte what it did before -- verified against the
 * pre-refactor objects. See the MD5 template for why defaulting rather than
 * converting: expanding the stream loops unconditionally measured 5.5% slower
 * on sha1/scalar-s2, and the scalar kernels are the ratio baseline.
 *
 * The schedule window is separate (S1K_WDECL/WGET/WSET) because it is indexed
 * by (t & 15), an expression rather than a token, so no naming scheme reaches
 * it. Sixteen vectors per stream fit no register file here anyway.
 */
#ifndef S1K_V
#  define S1K_V(name, k)      name[k]
#  define S1K_V2(name, k, i)  name[k][i]
#  define S1K_SDECL_ALL                                          \
        S1K_VEC fb[S1K_STREAMS][5];  /* digest fed into block 0 */ \
        S1K_VEC h0[S1K_STREAMS], h1[S1K_STREAMS], h2[S1K_STREAMS]; \
        S1K_VEC h3[S1K_STREAMS], h4[S1K_STREAMS];                  \
        S1K_VEC A[S1K_STREAMS], B[S1K_STREAMS], C[S1K_STREAMS];    \
        S1K_VEC D[S1K_STREAMS], E[S1K_STREAMS];
#  define S1K_FOREACH(BODY) \
       for (unsigned k = 0; k < S1K_STREAMS; k++) { BODY(k) }
   /* The five-word digest, likewise: a loop by default, expanded where the
      index has to be a literal. */
#  define S1K_FOR5(BODY, k) for (int j = 0; j < 5; j++) { BODY(k, j) }
#  define S1K_FOLDN S1K_LANES
#endif

#ifndef S1K_WDECL
#  define S1K_WDECL         S1K_VEC w[S1K_STREAMS][16];
#  define S1K_WGET(k, i)    w[k][(i)]
#  define S1K_WSET(k, i, v) w[k][(i)] = (v)
#endif

#define S1K_STEP1(k, F, KC, a, b, c, d, e, t)                    \
    {                                                            \
        S1K_VEC tmp = S1K_ADD(S1K_ROTL(S1K_V(a,k), 5),           \
                              F(S1K_V(b,k), S1K_V(c,k), S1K_V(d,k))); \
        tmp = S1K_ADD(tmp, S1K_V(e,k));                          \
        tmp = S1K_ADD(tmp, S1K_SET1(KC));                        \
        tmp = S1K_ADD(tmp, S1K_WGET(k, (t) & 15));               \
        S1K_V(e,k) = S1K_V(d,k);                                 \
        S1K_V(d,k) = S1K_V(c,k);                                 \
        S1K_V(c,k) = S1K_ROTL(S1K_V(b,k), 30);                   \
        S1K_V(b,k) = S1K_V(a,k);                                 \
        S1K_V(a,k) = tmp;                                        \
    }

/* Expand one schedule word in place, for t >= 16. */
#define S1K_EXPAND1(k, t)                                        \
    {                                                            \
        S1K_VEC x = S1K_XOR(S1K_WGET(k, ((t) - 3) & 15),         \
                            S1K_WGET(k, ((t) - 8) & 15));        \
        x = S1K_XOR(x, S1K_WGET(k, ((t) - 14) & 15));            \
        x = S1K_XOR(x, S1K_WGET(k, ((t) - 16) & 15));            \
        S1K_WSET(k, (t) & 15, S1K_ROTL(x, 1));                   \
    }

/*
 * Streams expanded by macro, never a loop: if a loop failed to unroll the
 * kernel would collapse to one dependency chain and silently under-report the
 * hardware. Same reasoning as the MD5 template.
 */
#if S1K_STREAMS == 1
#  define S1K_EACH(M, ...) M(0, __VA_ARGS__)
#elif S1K_STREAMS == 2
#  define S1K_EACH(M, ...) M(0, __VA_ARGS__) M(1, __VA_ARGS__)
#elif S1K_STREAMS == 3
#  define S1K_EACH(M, ...) M(0, __VA_ARGS__) M(1, __VA_ARGS__) M(2, __VA_ARGS__)
#elif S1K_STREAMS == 4
#  define S1K_EACH(M, ...) M(0, __VA_ARGS__) M(1, __VA_ARGS__) M(2, __VA_ARGS__) M(3, __VA_ARGS__)
#else
#  error "S1K_STREAMS must be 1..4"
#endif

#define S1K_STEP(F, KC, a, b, c, d, e, t) S1K_EACH(S1K_STEP1, F, KC, a, b, c, d, e, t)
#define S1K_EXPAND(t)                     S1K_EACH(S1K_EXPAND1, t)

/*
 * The eighty steps, in the runs the specification groups them into.
 *
 * Still expansion, not iteration: these emit one copy per step and the
 * preprocessor resolves every index, so the compiler sees the same straight
 * line it saw when all eighty were written out. That is the property S1K_EACH
 * exists to protect, and it survives here -- what is gone is 144 lines of
 * transcription.
 *
 * Two step forms, because the schedule differs: words 0..15 arrive from the
 * corpus, and every word after that is expanded in place immediately before
 * the step that first reads it. The state rotation is inside S1K_STEP1, which
 * is why every step names the registers in the same order.
 */
#define S1K_PLAIN(F, KC, t)    S1K_STEP(F, KC, A, B, C, D, E, t)
#define S1K_EXPANDED(F, KC, t) S1K_EXPAND(t) S1K_STEP(F, KC, A, B, C, D, E, t)

#define S1K_RUN4(DO, F, KC, base)                   \
    DO(F, KC, (base) + 0) DO(F, KC, (base) + 1)     \
    DO(F, KC, (base) + 2) DO(F, KC, (base) + 3)

#define S1K_RUN16(DO, F, KC, base)                                    \
    S1K_RUN4(DO, F, KC, (base) +  0) S1K_RUN4(DO, F, KC, (base) +  4) \
    S1K_RUN4(DO, F, KC, (base) +  8) S1K_RUN4(DO, F, KC, (base) + 12)

#define S1K_RUN20(DO, F, KC, base)                                    \
    S1K_RUN16(DO, F, KC, base) S1K_RUN4(DO, F, KC, (base) + 16)

/* FIPS 180-4 section 4.1.1. Ch and Maj get a 3-input boolean where the ISA has
   one; Parity is a plain double XOR. */
#define S1K_CH(x, y, z)  S1K_F_CH(x, y, z)
#define S1K_PAR(x, y, z) S1K_XOR(S1K_XOR(x, y), z)
#define S1K_MAJ(x, y, z) S1K_F_MAJ(x, y, z)

void S1K_NAME(const void *corpus_v, uint64_t n_groups, uint32_t blocks,
              uint32_t iterations, uint64_t checksum[VB_MAX_DIGEST_WORDS]);

void S1K_NAME(const void *corpus_v, uint64_t n_groups, uint32_t blocks,
              uint32_t iterations, uint64_t checksum[VB_MAX_DIGEST_WORDS])
{
    const uint32_t *corpus = (const uint32_t *) corpus_v;

    const size_t slot_words  = (size_t) blocks * VB_WORDS_PER_BLOCK * S1K_LANES;
    const size_t block_words = (size_t) VB_WORDS_PER_BLOCK * S1K_LANES;

    if (iterations == 0 || blocks == 0) {
        for (unsigned i = 0; i < VB_MAX_DIGEST_WORDS; i++)
            checksum[i] = 0;
        return;
    }

    S1K_VEC acc0 = S1K_SET1(0), acc1 = S1K_SET1(0), acc2 = S1K_SET1(0);
    S1K_VEC acc3 = S1K_SET1(0), acc4 = S1K_SET1(0);

    for (uint64_t g = 0; g < n_groups; g++) {
        const uint32_t *slot[S1K_STREAMS];
        S1K_WDECL
        S1K_SDECL_ALL

#define S1K_LOAD_FB(k, j) \
        S1K_V2(fb,k,j) = S1K_LOAD(slot[k] + (size_t) (j) * S1K_LANES);
#define S1K_LOAD_SLOT(k)                                          \
        slot[k] = corpus + (g * S1K_STREAMS + (k)) * slot_words;          \
        S1K_FOR5(S1K_LOAD_FB, k)
        S1K_FOREACH(S1K_LOAD_SLOT)
#undef S1K_LOAD_SLOT
#undef S1K_LOAD_FB

        for (uint32_t it = 0; it < iterations; it++) {

#define S1K_INIT_H(k)                                             \
        S1K_V(h0,k) = S1K_SET1(SHA1_IV[0]);                               \
        S1K_V(h1,k) = S1K_SET1(SHA1_IV[1]);                               \
        S1K_V(h2,k) = S1K_SET1(SHA1_IV[2]);                               \
        S1K_V(h3,k) = S1K_SET1(SHA1_IV[3]);                               \
        S1K_V(h4,k) = S1K_SET1(SHA1_IV[4]);
        S1K_FOREACH(S1K_INIT_H)
#undef S1K_INIT_H

        for (uint32_t b = 0; b < blocks; b++) {

#define S1K_FEED_FB(k, j) S1K_WSET(k, j, S1K_V2(fb,k,j));
#define S1K_START_BLOCK(k)                                        \
        {                                                                 \
            const uint32_t *wp = slot[k] + (size_t) b * block_words;      \
            for (int j = 0; j < 16; j++)                                  \
                S1K_WSET(k, j, S1K_LOAD(wp + (size_t) j * S1K_LANES));    \
            /* Block 0 carries the previous digest over the head of the   \
               message; every other block is corpus data unchanged. */    \
            if (b == 0)                                                   \
                S1K_FOR5(S1K_FEED_FB, k)                                     \
        }                                                                 \
        S1K_V(A,k) = S1K_V(h0,k); S1K_V(B,k) = S1K_V(h1,k);               \
        S1K_V(C,k) = S1K_V(h2,k); S1K_V(D,k) = S1K_V(h3,k);               \
        S1K_V(E,k) = S1K_V(h4,k);
        S1K_FOREACH(S1K_START_BLOCK)
#undef S1K_START_BLOCK
#undef S1K_FEED_FB


    /*
     * The round function and constant change every twenty steps, FIPS 180-4
     * section 6.1.2. Expansion starts at 16, which is why the first twenty
     * split into a plain run and an expanded one.
     */
    S1K_RUN16(S1K_PLAIN,    S1K_CH,  SHA1_K[0],  0);
    S1K_RUN4 (S1K_EXPANDED, S1K_CH,  SHA1_K[0], 16);
    S1K_RUN20(S1K_EXPANDED, S1K_PAR, SHA1_K[1], 20);
    S1K_RUN20(S1K_EXPANDED, S1K_MAJ, SHA1_K[2], 40);
    S1K_RUN20(S1K_EXPANDED, S1K_PAR, SHA1_K[3], 60);

#define S1K_CHAIN(k)                                              \
        S1K_V(h0,k) = S1K_ADD(S1K_V(h0,k), S1K_V(A,k));                   \
        S1K_V(h1,k) = S1K_ADD(S1K_V(h1,k), S1K_V(B,k));                   \
        S1K_V(h2,k) = S1K_ADD(S1K_V(h2,k), S1K_V(C,k));                   \
        S1K_V(h3,k) = S1K_ADD(S1K_V(h3,k), S1K_V(D,k));                   \
        S1K_V(h4,k) = S1K_ADD(S1K_V(h4,k), S1K_V(E,k));
        S1K_FOREACH(S1K_CHAIN)
#undef S1K_CHAIN

        }   /* blocks */

#define S1K_FEEDBACK(k)                                           \
        S1K_V2(fb,k,0) = S1K_V(h0,k); S1K_V2(fb,k,1) = S1K_V(h1,k);       \
        S1K_V2(fb,k,2) = S1K_V(h2,k); S1K_V2(fb,k,3) = S1K_V(h3,k);       \
        S1K_V2(fb,k,4) = S1K_V(h4,k);   /* five literals: no loop existed */
        S1K_FOREACH(S1K_FEEDBACK)
#undef S1K_FEEDBACK

        }   /* iterations */

#define S1K_ACC(k)                                                        \
        acc0 = S1K_XOR(acc0, S1K_V(h0,k));                                \
        acc1 = S1K_XOR(acc1, S1K_V(h1,k));                                \
        acc2 = S1K_XOR(acc2, S1K_V(h2,k));                                \
        acc3 = S1K_XOR(acc3, S1K_V(h3,k));                                \
        acc4 = S1K_XOR(acc4, S1K_V(h4,k));
        S1K_FOREACH(S1K_ACC)
#undef S1K_ACC
    }

    uint32_t t0[S1K_FOLDN], t1[S1K_FOLDN], t2[S1K_FOLDN];
    uint32_t t3[S1K_FOLDN], t4[S1K_FOLDN];
    S1K_STORE(t0, acc0); S1K_STORE(t1, acc1); S1K_STORE(t2, acc2);
    S1K_STORE(t3, acc3); S1K_STORE(t4, acc4);

    uint32_t c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0;
    for (unsigned l = 0; l < (unsigned) (S1K_LANES); l++) {
        c0 ^= t0[l]; c1 ^= t1[l]; c2 ^= t2[l]; c3 ^= t3[l]; c4 ^= t4[l];
    }

    for (unsigned i = 0; i < VB_MAX_DIGEST_WORDS; i++)
        checksum[i] = 0;
    checksum[0] = c0; checksum[1] = c1; checksum[2] = c2;
    checksum[3] = c3; checksum[4] = c4;
}

#undef S1K_V
#undef S1K_V2
#undef S1K_SDECL_ALL
#undef S1K_FOREACH
#undef S1K_FOR5
#undef S1K_FOLDN
#undef S1K_WDECL
#undef S1K_WGET
#undef S1K_WSET
#undef S1K_STEP1
#undef S1K_EXPAND1
#undef S1K_EACH
#undef S1K_STEP
#undef S1K_EXPAND
#undef S1K_PLAIN
#undef S1K_EXPANDED
#undef S1K_RUN4
#undef S1K_RUN16
#undef S1K_RUN20
#undef S1K_CH
#undef S1K_PAR
#undef S1K_MAJ
#undef S1K_NAME
#undef S1K_VEC
#undef S1K_LANES
#undef S1K_STREAMS
#undef S1K_SET1
#undef S1K_ADD
#undef S1K_XOR
#undef S1K_ROTL
#undef S1K_STORE
#undef S1K_LOAD
#undef S1K_F_CH
#undef S1K_F_MAJ
