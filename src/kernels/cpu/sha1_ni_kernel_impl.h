/*
 * sha1_ni_kernel_impl.h -- SHA-1 on the x86 SHA extensions (SHA-NI).
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 *
 * WHY THIS KERNEL IS SHAPED DIFFERENTLY FROM EVERY OTHER ONE
 * =========================================================
 *
 * Every other kernel here is multi-buffer: one message per SIMD lane, the same
 * scalar algorithm run L ways at once. This one is not, and cannot be. SHA1RNDS4
 * consumes a *single* message's state -- A,B,C,D packed into one 128-bit
 * register, E in the high dword of another -- and performs four rounds of the
 * real SHA-1 on it. The register width buys no parallelism at all; it holds one
 * message's state, so this kernel is lanes = 1.
 *
 * All the throughput therefore has to come from stream interleaving: several
 * independent messages in flight so the ~6-cycle latency of SHA1RNDS4 is filled
 * by other messages' work rather than by stalls. That makes the stream sweep
 * more load-bearing here than anywhere else in the benchmark.
 *
 * The counterweight is register pressure. Each stream needs four message
 * registers, a working ABCD, two E registers and the two chaining values -- nine
 * xmm registers, against sixteen architectural. Past two streams this spills,
 * and the spills are visible in the measurement. That trade is the result, not a
 * defect: the harness picks the stream count by measuring, so whichever side of
 * the trade wins on a given microarchitecture is what gets reported.
 *
 *
 * WHAT THIS MEASURES, AND WHAT IT DOES NOT
 * ========================================
 *
 * Not integer SIMD. design.md finding 1 chose MD5 precisely because no hardware
 * has MD5 instructions, so MD5 is forced onto the general integer vector ALUs on
 * every architecture. SHA-NI is the counterexample that makes the point
 * concrete: this path measures a fixed-function unit, and the ratio between it
 * and sha1/avx2 is "what the SHA accelerator is worth on this chip". Read the
 * number as that ratio. It is not comparable to an integer-SIMD figure and must
 * never be quoted as one.
 *
 *
 * DERIVATION
 * ==========
 *
 * Written from the instruction semantics in the Intel SDM, not from any existing
 * SHA-NI implementation -- the same constraint as everything else here
 * (docs/research.md 2.9). Three facts follow from those semantics and drive the code:
 *
 *   Dword order is reversed. SHA1RNDS4 takes A in bits [127:96] and D in
 *   [31:0], and the message quad likewise runs W0 in the high dword down to W3
 *   in the low. The corpus stores words ascending, so each quad needs one
 *   PSHUFD (imm 0x1b) on load. Four shuffles per block against twenty round
 *   instructions -- cheap, and it keeps the corpus layout shared with every
 *   other kernel.
 *
 *   SHA1NEXTE rotates by 30, not 5. Tracking the state through four rounds gives
 *   E' = ROTL30(A) where A entered those four rounds, and the instruction
 *   computes exactly that, adding it to the next message quad's high dword to
 *   form the W0+E term SHA1RNDS4 expects. This is why the previous ABCD is
 *   stashed before each SHA1RNDS4.
 *
 *   The schedule is a three-instruction chain, tapering at the end.
 *   SHA1MSG1(x,y) yields {W0^W2, W1^W3, W2^W4, W3^W5}; XOR with the quad two
 *   ahead folds in the W-8 term; SHA1MSG2 with the quad three ahead folds in
 *   W-13..W-16 and applies the ROTL1, including the W16-feeds-W19 dependency
 *   internally. Working backwards from round 79: the last useful SHA1MSG2 is at
 *   group 18, so SHA1MSG1 stops after group 16 and the XOR after group 17.
 *   Running them to the end would be harmless but would cost real instructions.
 *
 * Required macros: S1NI_NAME, S1NI_STREAMS. All are #undef'd on the way out so
 * the header can be included again.
 */

#include "valubench.h"

#include <immintrin.h>
#include <stddef.h>

/*
 * Only the initial state: the round constants live inside SHA1RNDS4 itself,
 * which is the whole point of the instruction. Guarded because this template is
 * included once per stream count, and shares the guard with the SIMD template.
 */
#ifndef VB_SHA1_IV_DEFINED
#define VB_SHA1_IV_DEFINED
/* FIPS 180-4 section 5.3.1. */
static const uint32_t SHA1_IV[5] = {
    0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u,
};
#endif

/* Load one message quad, reversing dword order into SHA-NI's convention. */
#define S1NI_LOADQ(p) \
    _mm_shuffle_epi32(_mm_loadu_si128((const __m128i *) (const void *) (p)), 0x1b)

/*
 * One round group: four rounds. `nx` receives the ABCD that entered them, which
 * is what SHA1NEXTE needs next time round; `cu` already holds the one from last
 * time. The two alternate, so no register copy is needed between groups.
 */
#define S1NI_RND1(k, g, imm)                                              \
    {                                                                     \
        e[k][(g) & 1] = _mm_sha1nexte_epu32(e[k][(g) & 1], m[k][(g) & 3]); \
        e[k][((g) + 1) & 1] = abcd[k];                                    \
        abcd[k] = _mm_sha1rnds4_epu32(abcd[k], e[k][(g) & 1], (imm));     \
    }

#define S1NI_MSG1_1(k, g) \
    m[k][((g) + 3) & 3] = _mm_sha1msg1_epu32(m[k][((g) + 3) & 3], m[k][(g) & 3]);
#define S1NI_XOR1(k, g) \
    m[k][((g) + 2) & 3] = _mm_xor_si128(m[k][((g) + 2) & 3], m[k][(g) & 3]);
#define S1NI_MSG2_1(k, g) \
    m[k][((g) + 1) & 3] = _mm_sha1msg2_epu32(m[k][((g) + 1) & 3], m[k][(g) & 3]);

/*
 * Streams expanded by macro, never a loop -- the same rule as every other
 * template. A loop that failed to unroll would serialise the dependency chains
 * and under-report the hardware without failing anything.
 */
#if S1NI_STREAMS == 1
#  define S1NI_EACH(M, ...) M(0, __VA_ARGS__)
#elif S1NI_STREAMS == 2
#  define S1NI_EACH(M, ...) M(0, __VA_ARGS__) M(1, __VA_ARGS__)
#elif S1NI_STREAMS == 3
#  define S1NI_EACH(M, ...) M(0, __VA_ARGS__) M(1, __VA_ARGS__) M(2, __VA_ARGS__)
#elif S1NI_STREAMS == 4
#  define S1NI_EACH(M, ...) M(0, __VA_ARGS__) M(1, __VA_ARGS__) M(2, __VA_ARGS__) M(3, __VA_ARGS__)
#else
#  error "S1NI_STREAMS must be 1..4"
#endif

/* The four group shapes, by which schedule steps are still needed. */
#define S1NI_G_RND(g, imm)  S1NI_EACH(S1NI_RND1, g, imm)
#define S1NI_G_M1(g, imm)   S1NI_G_RND(g, imm) S1NI_EACH(S1NI_MSG1_1, g)
#define S1NI_G_M1X(g, imm)  S1NI_G_M1(g, imm)  S1NI_EACH(S1NI_XOR1, g)
#define S1NI_G_ALL(g, imm)  S1NI_G_M1X(g, imm) S1NI_EACH(S1NI_MSG2_1, g)
#define S1NI_G_XM2(g, imm)  S1NI_G_RND(g, imm) S1NI_EACH(S1NI_XOR1, g) \
                                               S1NI_EACH(S1NI_MSG2_1, g)
#define S1NI_G_M2(g, imm)   S1NI_G_RND(g, imm) S1NI_EACH(S1NI_MSG2_1, g)

void S1NI_NAME(const void *corpus_v, uint64_t n_groups, uint32_t blocks,
               uint32_t iterations, uint64_t checksum[VB_MAX_DIGEST_WORDS]);

void S1NI_NAME(const void *corpus_v, uint64_t n_groups, uint32_t blocks,
               uint32_t iterations, uint64_t checksum[VB_MAX_DIGEST_WORDS])
{
    const uint32_t *corpus = (const uint32_t *) corpus_v;

    /* lanes == 1, so a slot is just the message's words end to end. */
    const size_t slot_words  = (size_t) blocks * VB_WORDS_PER_BLOCK;
    const size_t block_words = (size_t) VB_WORDS_PER_BLOCK;

    if (iterations == 0 || blocks == 0) {
        for (unsigned i = 0; i < VB_MAX_DIGEST_WORDS; i++)
            checksum[i] = 0;
        return;
    }

    /* Keeps the low three dwords of a loaded quad, clearing the W4 slot so the
       fed-back E can be OR'd in. Avoids needing SSE4.1's PBLENDW. */
    const __m128i lo3 = _mm_set_epi32(0, -1, -1, -1);

    __m128i acc_abcd = _mm_setzero_si128();
    __m128i acc_e    = _mm_setzero_si128();

    for (uint64_t gr = 0; gr < n_groups; gr++) {
        const uint32_t *slot[S1NI_STREAMS];
        __m128i m[S1NI_STREAMS][4];
        __m128i abcd[S1NI_STREAMS], e[S1NI_STREAMS][2];
        __m128i h[S1NI_STREAMS], he[S1NI_STREAMS];
        __m128i fb[S1NI_STREAMS], fbe[S1NI_STREAMS];

        /*
         * Digest feedback, in registers. The fed-back digest occupies message
         * words 0..4, and ABCD already holds h0..h3 in exactly the dword order
         * a message quad wants -- so quad 0 *is* the previous ABCD, and only
         * quad 1's high dword (W4 = h4) has to be spliced in.
         */
        for (unsigned k = 0; k < S1NI_STREAMS; k++) {
            slot[k] = corpus + (gr * S1NI_STREAMS + k) * slot_words;
            fb[k]  = S1NI_LOADQ(slot[k]);
            fbe[k] = _mm_set_epi32((int) slot[k][4], 0, 0, 0);
        }

        for (uint32_t it = 0; it < iterations; it++) {

        for (unsigned k = 0; k < S1NI_STREAMS; k++) {
            h[k]  = _mm_set_epi32((int) SHA1_IV[0], (int) SHA1_IV[1],
                                  (int) SHA1_IV[2], (int) SHA1_IV[3]);
            he[k] = _mm_set_epi32((int) SHA1_IV[4], 0, 0, 0);
        }

        for (uint32_t b = 0; b < blocks; b++) {

        for (unsigned k = 0; k < S1NI_STREAMS; k++) {
            const uint32_t *wp = slot[k] + (size_t) b * block_words;

            m[k][0] = S1NI_LOADQ(wp);
            m[k][1] = S1NI_LOADQ(wp + 4);
            m[k][2] = S1NI_LOADQ(wp + 8);
            m[k][3] = S1NI_LOADQ(wp + 12);

            if (b == 0) {
                m[k][0] = fb[k];
                m[k][1] = _mm_or_si128(_mm_and_si128(m[k][1], lo3), fbe[k]);
            }

            abcd[k] = h[k];
            /* Group 0 has no previous ABCD to rotate, so the W0+E term is
               formed by a plain add instead of by SHA1NEXTE. */
            e[k][0] = _mm_add_epi32(he[k], m[k][0]);
            e[k][1] = abcd[k];
            abcd[k] = _mm_sha1rnds4_epu32(abcd[k], e[k][0], 0);
        }

    /* ---- rounds 4..79, twenty groups of four ---- */
    S1NI_G_M1 ( 1, 0);
    S1NI_G_M1X( 2, 0);
    S1NI_G_ALL( 3, 0);
    S1NI_G_ALL( 4, 0);
    S1NI_G_ALL( 5, 1);
    S1NI_G_ALL( 6, 1);
    S1NI_G_ALL( 7, 1);
    S1NI_G_ALL( 8, 1);
    S1NI_G_ALL( 9, 1);
    S1NI_G_ALL(10, 2);
    S1NI_G_ALL(11, 2);
    S1NI_G_ALL(12, 2);
    S1NI_G_ALL(13, 2);
    S1NI_G_ALL(14, 2);
    S1NI_G_ALL(15, 3);
    S1NI_G_ALL(16, 3);
    S1NI_G_XM2(17, 3);
    S1NI_G_M2 (18, 3);
    S1NI_G_RND(19, 3);

        for (unsigned k = 0; k < S1NI_STREAMS; k++) {
            /* Group 19 stashed the incoming ABCD in e[k][0]; feeding that to
               SHA1NEXTE against the chaining E adds ROTL30(A) to h4, which is
               the same "add the incoming state" step ABCD gets from PADDD. */
            he[k] = _mm_sha1nexte_epu32(e[k][0], he[k]);
            h[k]  = _mm_add_epi32(abcd[k], h[k]);
        }

        }   /* blocks */

        for (unsigned k = 0; k < S1NI_STREAMS; k++) {
            fb[k]  = h[k];
            fbe[k] = he[k];
        }

        }   /* iterations */

        for (unsigned k = 0; k < S1NI_STREAMS; k++) {
            acc_abcd = _mm_xor_si128(acc_abcd, h[k]);
            acc_e    = _mm_xor_si128(acc_e, he[k]);
        }
    }

    uint32_t ta[4], te[4];
    _mm_storeu_si128((__m128i *) (void *) ta, acc_abcd);
    _mm_storeu_si128((__m128i *) (void *) te, acc_e);

    for (unsigned i = 0; i < VB_MAX_DIGEST_WORDS; i++)
        checksum[i] = 0;

    /* Dword order is reversed on the way back out: ta[3] is h0. */
    checksum[0] = ta[3];
    checksum[1] = ta[2];
    checksum[2] = ta[1];
    checksum[3] = ta[0];
    checksum[4] = te[3];
}

#undef S1NI_LOADQ
#undef S1NI_RND1
#undef S1NI_MSG1_1
#undef S1NI_XOR1
#undef S1NI_MSG2_1
#undef S1NI_EACH
#undef S1NI_G_RND
#undef S1NI_G_M1
#undef S1NI_G_M1X
#undef S1NI_G_ALL
#undef S1NI_G_XM2
#undef S1NI_G_M2
#undef S1NI_NAME
#undef S1NI_STREAMS
