/*
 * md5_kernel_impl.h -- the multi-way MD5 kernel, written once.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * This header is included repeatedly, once per (ISA, stream count) variant,
 * with the vector operations supplied as macros by the including translation
 * unit. Algorithm written once, width a compile-time parameter -- the standard
 * structure for a multi-buffer hash implementation. See docs/research.md section 2.3
 * for the reasoning, and 2.9 for the licensing constraint that required writing
 * it from the specification.
 *
 *
 * WHY STREAMS EXIST
 * =================
 *
 * MD5's 64 steps form one serial dependency chain roughly 4-5 cycles deep per
 * step. SIMD width does not break that chain: all lanes of a vector advance in
 * lockstep as a single chain. A one-stream kernel therefore measures dependency
 * latency, not ALU throughput, and leaves most of the machine idle.
 *
 * MD5K_STREAMS independent vector chains are interleaved at step granularity so
 * the out-of-order engine always has ready work. This is the single most
 * important tuning knob in the CPU benchmark (docs/research.md 2.4), which is why
 * every ISA is instantiated at several stream counts and the harness picks the
 * winner by measurement rather than assumption.
 *
 *
 * REQUIRED MACROS
 * ===============
 *   MD5K_NAME     function name to define
 *   MD5K_VEC      vector type
 *   MD5K_LANES    u32 lanes per vector
 *   MD5K_STREAMS  independent chains (1..4)
 *   MD5K_SET1(x)  broadcast a u32
 *   MD5K_ADD(a,b) 32-bit lanewise add
 *   MD5K_XOR(a,b) bitwise xor
 *   MD5K_ROTL(x,n) lanewise rotate left by a literal n
 *   MD5K_STORE(p,v) store vector to a u32 array
 *   MD5K_LOAD(p)  load a vector from a const u32 array (possibly unaligned)
 *   MD5K_F/G/H/I(x,y,z)  round functions
 *
 * All are #undef'd at the end so the header can be included again.
 */

#include "valubench.h"

#include <stddef.h>

/*
 * The template is included once per stream count, so the table needs a guard of
 * its own to be defined once per translation unit. That is the whole job the
 * former include/md5_const.h did for it.
 */
#ifndef VB_MD5_T_DEFINED
#define VB_MD5_T_DEFINED
/*
 * Round constants, RFC 1321 section 3.4: T[i] = floor(2^32 * abs(sin(i))) for
 * i = 1..64 with i in radians. Recorded here as documentation of where the
 * values come from, not as a build step -- they are a frozen standard.
 *
 * Correctness is established by the RFC 1321 known-answer vectors in
 * tests/test_hashes.c rather than by inspection: a single wrong digit fails
 * them.
 */
static const uint32_t MD5_T[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,   /* T[ 0.. 3] */
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,   /* T[ 4.. 7] */
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,   /* T[ 8..11] */
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,   /* T[12..15] */
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,   /* T[16..19] */
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,   /* T[20..23] */
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,   /* T[24..27] */
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,   /* T[28..31] */
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,   /* T[32..35] */
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,   /* T[36..39] */
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,   /* T[40..43] */
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,   /* T[44..47] */
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,   /* T[48..51] */
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,   /* T[52..55] */
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,   /* T[56..59] */
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,   /* T[60..63] */
};
#endif

/*
 * SVE hooks.
 *
 * Every one of these defaults to the code this template already had, so a
 * fixed-width ISA compiles byte-for-byte what it compiled before. They exist
 * because SVE vectors are sizeless: they cannot go in an array, so per-stream
 * state has to be held in separately named variables and every loop over the
 * stream index has to be expanded to reach them with a literal.
 *
 * Defaulting rather than converting is deliberate and was measured. Expanding
 * the stream loops unconditionally costs md5/scalar-s2 3.6% and sha1/scalar-s2
 * 5.5% -- the register allocator simply lands differently on the fully
 * expanded form -- and the scalar kernels are the baseline every vector ratio
 * is divided by, so a few percent there moves every published number.
 *
 *   MD5K_V/MD5K_V2   read one stream's state
 *   MD5K_SDECL       declare it
 *   MD5K_FOREACH     visit every stream
 *   MD5K_W           the message-word accessor, register or memory
 *   MD5K_FOLDN       size of the lane-fold scratch
 */
#ifndef MD5K_V
#  define MD5K_V(name, k)      name[k]
#  define MD5K_V2(name, k, i)  name[k][i]
#  define MD5K_SDECL_ALL                                   \
        MD5K_VEC wv[MD5K_STREAMS][4];                       \
        MD5K_VEC h0[MD5K_STREAMS], h1[MD5K_STREAMS];        \
        MD5K_VEC h2[MD5K_STREAMS], h3[MD5K_STREAMS];        \
        MD5K_VEC A[MD5K_STREAMS], B[MD5K_STREAMS];          \
        MD5K_VEC C[MD5K_STREAMS], D[MD5K_STREAMS];
#  define MD5K_FOREACH(BODY) \
       for (unsigned k = 0; k < MD5K_STREAMS; k++) { BODY(k) }
#  define MD5K_FOLDN MD5K_LANES
   /*
    * The round constant, and where its address comes from.
    *
    * Defaults to indexing the table directly, which is what every fixed-width
    * ISA has always compiled. An ISA whose broadcast-from-memory takes an
    * immediate offset wants a base register held across the whole function
    * instead, and says so by overriding these two.
    */
#  define MD5K_TDECL
#  define MD5K_TC(TI) MD5K_SET1(MD5_T[TI])
#endif

/*
 * Message word accessor.
 *
 * Words 0..3 live in registers because iterated hashing rewrites them with the
 * previous digest; on blocks after the first, and on the first iteration, they
 * are simply loaded from the corpus into the same registers at block entry.
 * Words 4..15 are loaded straight from the corpus. WI is always a literal, so
 * the branch folds away at compile time.
 */
#ifndef MD5K_W
#  define MD5K_W(k, WI) (((WI) < 4) ? MD5K_V2(wv, k, WI) \
                                    : MD5K_LOAD(wp[k] + (WI) * MD5K_LANES))
#endif

#define MD5K_STEP1(k, F, A, B, C, D, WI, TI, SH)              \
    MD5K_V(A,k) = MD5K_ADD(MD5K_V(A,k),                                      \
                           F(MD5K_V(B,k), MD5K_V(C,k), MD5K_V(D,k)));        \
    MD5K_V(A,k) = MD5K_ADD(MD5K_V(A,k), MD5K_W(k, WI));                      \
    MD5K_V(A,k) = MD5K_ADD(MD5K_V(A,k), MD5K_TC(TI));                        \
    MD5K_V(A,k) = MD5K_ROTL(MD5K_V(A,k), SH);                                \
    MD5K_V(A,k) = MD5K_ADD(MD5K_V(A,k), MD5K_V(B,k));

/*
 * Streams are expanded explicitly rather than with a loop. A loop over a small
 * literal bound usually unrolls, but "usually" is not good enough here: if it
 * failed to unroll the kernel would silently collapse to one chain and the
 * benchmark would under-report the hardware by a large factor.
 */
#if MD5K_STREAMS == 1
#  define MD5K_STEP(F, A, B, C, D, WI, TI, SH) do { \
       MD5K_STEP1(0, F, A, B, C, D, WI, TI, SH)     \
   } while (0)
#elif MD5K_STREAMS == 2
#  define MD5K_STEP(F, A, B, C, D, WI, TI, SH) do { \
       MD5K_STEP1(0, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(1, F, A, B, C, D, WI, TI, SH)     \
   } while (0)
#elif MD5K_STREAMS == 3
#  define MD5K_STEP(F, A, B, C, D, WI, TI, SH) do { \
       MD5K_STEP1(0, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(1, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(2, F, A, B, C, D, WI, TI, SH)     \
   } while (0)
#elif MD5K_STREAMS == 4
#  define MD5K_STEP(F, A, B, C, D, WI, TI, SH) do { \
       MD5K_STEP1(0, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(1, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(2, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(3, F, A, B, C, D, WI, TI, SH)     \
   } while (0)
#elif MD5K_STREAMS == 6
#  define MD5K_STEP(F, A, B, C, D, WI, TI, SH) do { \
       MD5K_STEP1(0, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(1, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(2, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(3, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(4, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(5, F, A, B, C, D, WI, TI, SH)     \
   } while (0)
#elif MD5K_STREAMS == 8
#  define MD5K_STEP(F, A, B, C, D, WI, TI, SH) do { \
       MD5K_STEP1(0, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(1, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(2, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(3, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(4, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(5, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(6, F, A, B, C, D, WI, TI, SH)     \
       MD5K_STEP1(7, F, A, B, C, D, WI, TI, SH)     \
   } while (0)
#else
#  error "MD5K_STREAMS must be 1, 2, 3, 4, 6 or 8"
#endif

void MD5K_NAME(const void *corpus_v, uint64_t n_groups, uint32_t blocks,
               uint32_t iterations, uint64_t checksum[VB_MAX_DIGEST_WORDS]);

void MD5K_NAME(const void *corpus_v, uint64_t n_groups, uint32_t blocks,
               uint32_t iterations, uint64_t checksum[VB_MAX_DIGEST_WORDS])
{
    const uint32_t *corpus = (const uint32_t *) corpus_v;
    /* uint32 stride from one message slot (MD5K_LANES messages) to the next. */
    const size_t slot_words = (size_t) blocks * 16u * MD5K_LANES;
    const size_t block_words = 16u * MD5K_LANES;

    /*
     * Zero iterations or zero blocks means no digests, and the XOR of nothing
     * is zero. The harness never asks for either, but stating it explicitly
     * also tells the compiler the loops below run at least once -- without
     * which it cannot prove the digest registers are initialised on exit.
     */
    if (iterations == 0 || blocks == 0) {
        for (unsigned i = 0; i < VB_MAX_DIGEST_WORDS; i++)
            checksum[i] = 0;
        return;
    }

    MD5K_VEC acc0 = MD5K_SET1(0), acc1 = MD5K_SET1(0);
    MD5K_VEC acc2 = MD5K_SET1(0), acc3 = MD5K_SET1(0);

    for (uint64_t g = 0; g < n_groups; g++) {
        /* A group is MD5K_STREAMS consecutive message slots. */
        const uint32_t *slot[MD5K_STREAMS];
        const uint32_t *wp[MD5K_STREAMS];

        MD5K_SDECL_ALL

        /*
         * Load block 0's words 0..3 once, before any iteration. From here on
         * they are only rewritten by the digest feedback at the end of each
         * iteration, so they are unconditionally live on every path into the
         * step sequence.
         */
#define MD5K_LOAD_SLOT(k)                                           \
        slot[k] = corpus + (g * MD5K_STREAMS + (k)) * slot_words;         \
        MD5K_V2(wv,k,0) = MD5K_LOAD(slot[k] + 0 * MD5K_LANES);            \
        MD5K_V2(wv,k,1) = MD5K_LOAD(slot[k] + 1 * MD5K_LANES);            \
        MD5K_V2(wv,k,2) = MD5K_LOAD(slot[k] + 2 * MD5K_LANES);            \
        MD5K_V2(wv,k,3) = MD5K_LOAD(slot[k] + 3 * MD5K_LANES);
        MD5K_FOREACH(MD5K_LOAD_SLOT)
#undef MD5K_LOAD_SLOT

        for (uint32_t it = 0; it < iterations; it++) {

#define MD5K_INIT_H(k)                                              \
        MD5K_V(h0,k) = MD5K_SET1(0x67452301u);                            \
        MD5K_V(h1,k) = MD5K_SET1(0xefcdab89u);                            \
        MD5K_V(h2,k) = MD5K_SET1(0x98badcfeu);                            \
        MD5K_V(h3,k) = MD5K_SET1(0x10325476u);
        MD5K_FOREACH(MD5K_INIT_H)
#undef MD5K_INIT_H

        for (uint32_t b = 0; b < blocks; b++) {

#define MD5K_START_BLOCK(k)                                         \
        wp[k] = slot[k] + (size_t) b * block_words;                       \
        /* Blocks after the first always take words 0..3 from the corpus.  \
           Block 0 keeps whatever wv already holds: the corpus values on   \
           the first iteration, the previous digest on later ones. */      \
        if (b > 0) {                                                      \
            MD5K_V2(wv,k,0) = MD5K_LOAD(wp[k] + 0 * MD5K_LANES);          \
            MD5K_V2(wv,k,1) = MD5K_LOAD(wp[k] + 1 * MD5K_LANES);          \
            MD5K_V2(wv,k,2) = MD5K_LOAD(wp[k] + 2 * MD5K_LANES);          \
            MD5K_V2(wv,k,3) = MD5K_LOAD(wp[k] + 3 * MD5K_LANES);          \
        }                                                                 \
        MD5K_V(A,k) = MD5K_V(h0,k);                                       \
        MD5K_V(B,k) = MD5K_V(h1,k);                                       \
        MD5K_V(C,k) = MD5K_V(h2,k);                                       \
        MD5K_V(D,k) = MD5K_V(h3,k);
        MD5K_FOREACH(MD5K_START_BLOCK)
        MD5K_TDECL
#undef MD5K_START_BLOCK

    /* ---- round 1 ---- */
    MD5K_STEP(MD5K_F, A, B, C, D,  0,  0,  7);
    MD5K_STEP(MD5K_F, D, A, B, C,  1,  1, 12);
    MD5K_STEP(MD5K_F, C, D, A, B,  2,  2, 17);
    MD5K_STEP(MD5K_F, B, C, D, A,  3,  3, 22);
    MD5K_STEP(MD5K_F, A, B, C, D,  4,  4,  7);
    MD5K_STEP(MD5K_F, D, A, B, C,  5,  5, 12);
    MD5K_STEP(MD5K_F, C, D, A, B,  6,  6, 17);
    MD5K_STEP(MD5K_F, B, C, D, A,  7,  7, 22);
    MD5K_STEP(MD5K_F, A, B, C, D,  8,  8,  7);
    MD5K_STEP(MD5K_F, D, A, B, C,  9,  9, 12);
    MD5K_STEP(MD5K_F, C, D, A, B, 10, 10, 17);
    MD5K_STEP(MD5K_F, B, C, D, A, 11, 11, 22);
    MD5K_STEP(MD5K_F, A, B, C, D, 12, 12,  7);
    MD5K_STEP(MD5K_F, D, A, B, C, 13, 13, 12);
    MD5K_STEP(MD5K_F, C, D, A, B, 14, 14, 17);
    MD5K_STEP(MD5K_F, B, C, D, A, 15, 15, 22);

    /* ---- round 2 ---- */
    MD5K_STEP(MD5K_G, A, B, C, D,  1, 16,  5);
    MD5K_STEP(MD5K_G, D, A, B, C,  6, 17,  9);
    MD5K_STEP(MD5K_G, C, D, A, B, 11, 18, 14);
    MD5K_STEP(MD5K_G, B, C, D, A,  0, 19, 20);
    MD5K_STEP(MD5K_G, A, B, C, D,  5, 20,  5);
    MD5K_STEP(MD5K_G, D, A, B, C, 10, 21,  9);
    MD5K_STEP(MD5K_G, C, D, A, B, 15, 22, 14);
    MD5K_STEP(MD5K_G, B, C, D, A,  4, 23, 20);
    MD5K_STEP(MD5K_G, A, B, C, D,  9, 24,  5);
    MD5K_STEP(MD5K_G, D, A, B, C, 14, 25,  9);
    MD5K_STEP(MD5K_G, C, D, A, B,  3, 26, 14);
    MD5K_STEP(MD5K_G, B, C, D, A,  8, 27, 20);
    MD5K_STEP(MD5K_G, A, B, C, D, 13, 28,  5);
    MD5K_STEP(MD5K_G, D, A, B, C,  2, 29,  9);
    MD5K_STEP(MD5K_G, C, D, A, B,  7, 30, 14);
    MD5K_STEP(MD5K_G, B, C, D, A, 12, 31, 20);

    /* ---- round 3 ---- */
    MD5K_STEP(MD5K_H, A, B, C, D,  5, 32,  4);
    MD5K_STEP(MD5K_H, D, A, B, C,  8, 33, 11);
    MD5K_STEP(MD5K_H, C, D, A, B, 11, 34, 16);
    MD5K_STEP(MD5K_H, B, C, D, A, 14, 35, 23);
    MD5K_STEP(MD5K_H, A, B, C, D,  1, 36,  4);
    MD5K_STEP(MD5K_H, D, A, B, C,  4, 37, 11);
    MD5K_STEP(MD5K_H, C, D, A, B,  7, 38, 16);
    MD5K_STEP(MD5K_H, B, C, D, A, 10, 39, 23);
    MD5K_STEP(MD5K_H, A, B, C, D, 13, 40,  4);
    MD5K_STEP(MD5K_H, D, A, B, C,  0, 41, 11);
    MD5K_STEP(MD5K_H, C, D, A, B,  3, 42, 16);
    MD5K_STEP(MD5K_H, B, C, D, A,  6, 43, 23);
    MD5K_STEP(MD5K_H, A, B, C, D,  9, 44,  4);
    MD5K_STEP(MD5K_H, D, A, B, C, 12, 45, 11);
    MD5K_STEP(MD5K_H, C, D, A, B, 15, 46, 16);
    MD5K_STEP(MD5K_H, B, C, D, A,  2, 47, 23);

    /* ---- round 4 ---- */
    MD5K_STEP(MD5K_I, A, B, C, D,  0, 48,  6);
    MD5K_STEP(MD5K_I, D, A, B, C,  7, 49, 10);
    MD5K_STEP(MD5K_I, C, D, A, B, 14, 50, 15);
    MD5K_STEP(MD5K_I, B, C, D, A,  5, 51, 21);
    MD5K_STEP(MD5K_I, A, B, C, D, 12, 52,  6);
    MD5K_STEP(MD5K_I, D, A, B, C,  3, 53, 10);
    MD5K_STEP(MD5K_I, C, D, A, B, 10, 54, 15);
    MD5K_STEP(MD5K_I, B, C, D, A,  1, 55, 21);
    MD5K_STEP(MD5K_I, A, B, C, D,  8, 56,  6);
    MD5K_STEP(MD5K_I, D, A, B, C, 15, 57, 10);
    MD5K_STEP(MD5K_I, C, D, A, B,  6, 58, 15);
    MD5K_STEP(MD5K_I, B, C, D, A, 13, 59, 21);
    MD5K_STEP(MD5K_I, A, B, C, D,  4, 60,  6);
    MD5K_STEP(MD5K_I, D, A, B, C, 11, 61, 10);
    MD5K_STEP(MD5K_I, C, D, A, B,  2, 62, 15);
    MD5K_STEP(MD5K_I, B, C, D, A,  9, 63, 21);

        /* Chain this block into the running state, per RFC 1321 section 3.4. */
#define MD5K_CHAIN(k)                                               \
        MD5K_V(h0,k) = MD5K_ADD(MD5K_V(h0,k), MD5K_V(A,k));               \
        MD5K_V(h1,k) = MD5K_ADD(MD5K_V(h1,k), MD5K_V(B,k));               \
        MD5K_V(h2,k) = MD5K_ADD(MD5K_V(h2,k), MD5K_V(C,k));               \
        MD5K_V(h3,k) = MD5K_ADD(MD5K_V(h3,k), MD5K_V(D,k));
        MD5K_FOREACH(MD5K_CHAIN)
#undef MD5K_CHAIN

        }   /* blocks */

        /* Feed the finished digest back as the leading 16 message bytes, so
           the next iteration is the same work on different data. */
#define MD5K_FEEDBACK(k)                                            \
        MD5K_V2(wv,k,0) = MD5K_V(h0,k);                                   \
        MD5K_V2(wv,k,1) = MD5K_V(h1,k);                                   \
        MD5K_V2(wv,k,2) = MD5K_V(h2,k);                                   \
        MD5K_V2(wv,k,3) = MD5K_V(h3,k);
        MD5K_FOREACH(MD5K_FEEDBACK)
#undef MD5K_FEEDBACK

        }   /* iterations */

        /* Only the final digest of each message contributes. */
#define MD5K_ACC(k)                                                 \
        acc0 = MD5K_XOR(acc0, MD5K_V(h0,k));                              \
        acc1 = MD5K_XOR(acc1, MD5K_V(h1,k));                              \
        acc2 = MD5K_XOR(acc2, MD5K_V(h2,k));                              \
        acc3 = MD5K_XOR(acc3, MD5K_V(h3,k));
        MD5K_FOREACH(MD5K_ACC)
#undef MD5K_ACC
    }

    /* Fold the lanes. XOR is commutative and associative, so the result does
       not depend on lane or stream assignment -- that is what lets one expected
       value validate every kernel. */
    uint32_t t0[MD5K_FOLDN], t1[MD5K_FOLDN];
    uint32_t t2[MD5K_FOLDN], t3[MD5K_FOLDN];
    MD5K_STORE(t0, acc0);
    MD5K_STORE(t1, acc1);
    MD5K_STORE(t2, acc2);
    MD5K_STORE(t3, acc3);

    uint32_t c0 = 0, c1 = 0, c2 = 0, c3 = 0;
    for (unsigned l = 0; l < (unsigned) (MD5K_LANES); l++) {
        c0 ^= t0[l];
        c1 ^= t1[l];
        c2 ^= t2[l];
        c3 ^= t3[l];
    }

    for (unsigned i = 0; i < VB_MAX_DIGEST_WORDS; i++)
        checksum[i] = 0;
    checksum[0] = c0;
    checksum[1] = c1;
    checksum[2] = c2;
    checksum[3] = c3;
}

#undef MD5K_V
#undef MD5K_V2
#undef MD5K_SDECL_ALL
#undef MD5K_FOREACH
#undef MD5K_FOLDN
#undef MD5K_TDECL
#undef MD5K_TC
#undef MD5K_W
#undef MD5K_STEP1
#undef MD5K_STEP
#undef MD5K_NAME
#undef MD5K_VEC
#undef MD5K_LANES
#undef MD5K_STREAMS
#undef MD5K_SET1
#undef MD5K_ADD
#undef MD5K_XOR
#undef MD5K_ROTL
#undef MD5K_STORE
#undef MD5K_LOAD
#undef MD5K_F
#undef MD5K_G
#undef MD5K_H
#undef MD5K_I
