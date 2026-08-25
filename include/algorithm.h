/*
 * algorithm.h -- what the harness needs to know about a hash function.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * Everything the corpus builder, the reference oracle and the reporting need in
 * order to be algorithm-agnostic. Kernels remain algorithm-specific -- MD5's 64
 * fixed-schedule steps and SHA-512's 80 expanded ones share no code worth
 * sharing -- but nothing *around* them does.
 *
 * The three supported algorithms deliberately span the axes that break naive
 * generalisation:
 *
 *   md5      4 x 32-bit digest, 512-bit block,  little-endian
 *   sha1     5 x 32-bit digest, 512-bit block,  big-endian
 *   sha512   8 x 64-bit digest, 1024-bit block, big-endian, 128-bit length
 *
 * SHA-512 is the one that forces the issue: adding only SHA-1 would have let a
 * 32-bit word and a 64-byte block stay hardcoded.
 *
 * One invariant holds across all three and the corpus layout depends on it:
 * every block is exactly sixteen words. 16 x 32 bits is MD5's and SHA-1's
 * 512-bit block; 16 x 64 bits is SHA-512's 1024-bit block.
 */

#ifndef VALUBENCH_ALGORITHM_H
#define VALUBENCH_ALGORITHM_H

#include <stddef.h>
#include <stdint.h>

/* Widest digest in words: SHA-512's eight. */
#define VB_MAX_DIGEST_WORDS 8

/* Every supported block is sixteen words wide, whatever the word size. */
#define VB_WORDS_PER_BLOCK 16

typedef enum {
    VB_ALG_MD5 = 0,
    VB_ALG_SHA1,
    VB_ALG_SHA512,
    VB_ALG_COUNT
} vb_alg_id;

typedef struct {
    vb_alg_id   id;
    const char *name;           /* "md5", "sha1", "sha512" */

    unsigned digest_words;      /* 4, 5, 8 */
    unsigned digest_bytes;      /* 16, 20, 64 */
    unsigned word_bytes;        /* 4, 4, 8 -- corpus element width */
    unsigned block_bytes;       /* 64, 64, 128 */
    unsigned length_bytes;      /* 8, 8, 16 -- padded length field */
    unsigned rounds;            /* 64, 80, 80 -- steps per compression */
    int      big_endian;        /* word packing and length field */

    /* Reference implementation: the oracle every kernel is checked against. */
    void (*hash)(const void *msg, size_t len, uint8_t *digest);
} vb_algorithm;

/* Descriptor for an id, or NULL. */
const vb_algorithm *vb_algorithm_by_id(vb_alg_id id);

/* Descriptor by name ("md5"), or NULL. */
const vb_algorithm *vb_algorithm_by_name(const char *name);

/* Blocks a message of `bytes` occupies once padded. */
static inline uint32_t vb_alg_blocks_for(const vb_algorithm *a, uint32_t bytes)
{
    uint32_t per = a->block_bytes;
    uint32_t overhead = 1u + a->length_bytes;   /* 0x80 terminator + length */
    return (bytes + overhead + per - 1u) / per;
}

/*
 * Iterated hashing feeds the digest back over the head of the message, so the
 * message has to be at least a digest long.
 */
static inline uint32_t vb_alg_min_iter_bytes(const vb_algorithm *a)
{
    return a->digest_bytes;
}

#endif /* VALUBENCH_ALGORITHM_H */
