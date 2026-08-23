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

#define S5K_STEP1(k, t)                                                   \
    {                                                                     \
        S5K_VEC t1 = S5K_ADD(H[k], S5K_BSIG1(E[k]));                      \
        t1 = S5K_ADD(t1, S5K_F_CH(E[k], F[k], G[k]));                     \
        t1 = S5K_ADD(t1, S5K_SET1(SHA512_K[t]));                          \
        t1 = S5K_ADD(t1, w[k][(t) & 15]);                                 \
        S5K_VEC t2 = S5K_ADD(S5K_BSIG0(A[k]), S5K_F_MAJ(A[k], B[k], C[k])); \
        H[k] = G[k];                                                      \
        G[k] = F[k];                                                      \
        F[k] = E[k];                                                      \
        E[k] = S5K_ADD(D[k], t1);                                         \
        D[k] = C[k];                                                      \
        C[k] = B[k];                                                      \
        B[k] = A[k];                                                      \
        A[k] = S5K_ADD(t1, t2);                                           \
    }

#define S5K_EXPAND1(k, t)                                                 \
    {                                                                     \
        S5K_VEC x = S5K_ADD(S5K_SSIG1(w[k][((t) - 2) & 15]),              \
                            w[k][((t) - 7) & 15]);                        \
        x = S5K_ADD(x, S5K_SSIG0(w[k][((t) - 15) & 15]));                 \
        x = S5K_ADD(x, w[k][((t) - 16) & 15]);                            \
        w[k][(t) & 15] = x;                                               \
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

    S5K_VEC acc[8];
    for (int i = 0; i < 8; i++)
        acc[i] = S5K_SET1(0);

    for (uint64_t g = 0; g < n_groups; g++) {
        const uint64_t *slot[S5K_STREAMS];
        S5K_VEC w[S5K_STREAMS][16];
        S5K_VEC fb[S5K_STREAMS][8];        /* digest fed back into block 0 */
        S5K_VEC h[S5K_STREAMS][8];
        S5K_VEC A[S5K_STREAMS], B[S5K_STREAMS], C[S5K_STREAMS], D[S5K_STREAMS];
        S5K_VEC E[S5K_STREAMS], F[S5K_STREAMS], G[S5K_STREAMS], H[S5K_STREAMS];

        for (unsigned k = 0; k < S5K_STREAMS; k++) {
            slot[k] = corpus + (g * S5K_STREAMS + k) * slot_words;
            for (int j = 0; j < 8; j++)
                fb[k][j] = S5K_LOAD(slot[k] + (size_t) j * S5K_LANES);
        }

        for (uint32_t it = 0; it < iterations; it++) {

        for (unsigned k = 0; k < S5K_STREAMS; k++)
            for (int j = 0; j < 8; j++)
                h[k][j] = S5K_SET1(SHA512_IV[j]);

        for (uint32_t b = 0; b < blocks; b++) {

        for (unsigned k = 0; k < S5K_STREAMS; k++) {
            const uint64_t *wp = slot[k] + (size_t) b * block_words;

            for (int j = 0; j < 16; j++)
                w[k][j] = S5K_LOAD(wp + (size_t) j * S5K_LANES);

            if (b == 0)
                for (int j = 0; j < 8; j++)
                    w[k][j] = fb[k][j];

            A[k] = h[k][0]; B[k] = h[k][1]; C[k] = h[k][2]; D[k] = h[k][3];
            E[k] = h[k][4]; F[k] = h[k][5]; G[k] = h[k][6]; H[k] = h[k][7];
        }


    S5K_RUN16(S5K_PLAIN,     0);
    S5K_RUN16(S5K_EXPANDED, 16);
    S5K_RUN16(S5K_EXPANDED, 32);
    S5K_RUN16(S5K_EXPANDED, 48);
    S5K_RUN16(S5K_EXPANDED, 64);

        for (unsigned k = 0; k < S5K_STREAMS; k++) {
            h[k][0] = S5K_ADD(h[k][0], A[k]);
            h[k][1] = S5K_ADD(h[k][1], B[k]);
            h[k][2] = S5K_ADD(h[k][2], C[k]);
            h[k][3] = S5K_ADD(h[k][3], D[k]);
            h[k][4] = S5K_ADD(h[k][4], E[k]);
            h[k][5] = S5K_ADD(h[k][5], F[k]);
            h[k][6] = S5K_ADD(h[k][6], G[k]);
            h[k][7] = S5K_ADD(h[k][7], H[k]);
        }

        }   /* blocks */

        for (unsigned k = 0; k < S5K_STREAMS; k++)
            for (int j = 0; j < 8; j++)
                fb[k][j] = h[k][j];

        }   /* iterations */

        for (unsigned k = 0; k < S5K_STREAMS; k++)
            for (int j = 0; j < 8; j++)
                acc[j] = S5K_XOR(acc[j], h[k][j]);
    }

    uint64_t tmp[S5K_LANES];
    for (int j = 0; j < 8; j++) {
        uint64_t c = 0;
        S5K_STORE(tmp, acc[j]);
        for (unsigned l = 0; l < S5K_LANES; l++)
            c ^= tmp[l];
        checksum[j] = c;
    }
}

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
