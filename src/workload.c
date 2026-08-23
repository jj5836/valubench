/*
 * workload.c -- the workload definition, corpus, and reference oracle.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * Algorithm-agnostic: everything specific comes from the vb_algorithm
 * descriptor. This is the single place that decides what bytes get hashed, so
 * the reference path and every kernel are guaranteed to agree on the input.
 */

#include "valubench.h"
#include "hashes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Message content: the index in the first four bytes, then a fixed
 * deterministic pattern. Non-uniform so that a kernel which mixed up word
 * indices could not accidentally produce the right digest.
 */
static uint8_t pattern_byte(uint32_t offset)
{
    return (uint8_t) (offset * 0x9du + 0x3bu);
}

void vb_build_message(uint32_t index, uint32_t bytes, uint8_t *out)
{
    uint32_t n = bytes < 4u ? bytes : 4u;

    for (uint32_t i = 0; i < n; i++)
        out[i] = (uint8_t) (index >> (8 * i));

    for (uint32_t i = n; i < bytes; i++)
        out[i] = pattern_byte(i);
}

const char *vb_workload_id(char *buf, size_t n, const vb_algorithm *alg,
                           uint32_t message_bytes, uint32_t iterations)
{
    snprintf(buf, n, "%s-full-%ux%u", alg->name, message_bytes, iterations);
    return buf;
}

/*
 * Pad a message into whole blocks exactly as the algorithm's own final() would:
 * a 0x80 terminator, zeros, then the bit length in the trailing length field,
 * in the algorithm's byte order. Doing this at corpus build time keeps padding
 * out of the kernels entirely -- they just hash `blocks` blocks.
 */
static void pad_message(const vb_algorithm *a, const uint8_t *msg,
                        uint32_t bytes, uint8_t *out, uint32_t blocks)
{
    size_t total = (size_t) blocks * a->block_bytes;
    uint64_t bit_len = (uint64_t) bytes * 8u;

    memcpy(out, msg, bytes);
    out[bytes] = 0x80;
    memset(out + bytes + 1, 0, total - bytes - 1);

    /* The length field is the last `length_bytes`; only its low 64 bits can
       ever be non-zero for messages this benchmark builds. */
    uint8_t *lf = out + total - 8;
    for (int i = 0; i < 8; i++) {
        if (a->big_endian)
            lf[i] = (uint8_t) (bit_len >> (56 - 8 * i));
        else
            lf[i] = (uint8_t) (bit_len >> (8 * i));
    }

    /* Little-endian algorithms put the length at the *start* of the field. */
    if (!a->big_endian && a->length_bytes > 8)
        memmove(out + total - a->length_bytes, lf, 8);
}

static uint64_t load_word(const vb_algorithm *a, const uint8_t *p)
{
    uint64_t v = 0;

    if (a->big_endian) {
        for (unsigned i = 0; i < a->word_bytes; i++)
            v = (v << 8) | p[i];
    } else {
        for (unsigned i = 0; i < a->word_bytes; i++)
            v |= (uint64_t) p[i] << (8 * i);
    }
    return v;
}

int vb_corpus_build(vb_corpus *c, const vb_algorithm *alg, uint32_t lanes,
                    uint32_t start_index, uint64_t n_messages,
                    uint32_t message_bytes)
{
    memset(c, 0, sizeof *c);

    if (!alg || lanes == 0 || n_messages == 0 || (n_messages % lanes) != 0)
        return -1;

    uint32_t blocks = vb_alg_blocks_for(alg, message_bytes);
    size_t n_words = (size_t) n_messages * blocks * VB_WORDS_PER_BLOCK;
    size_t bytes = n_words * alg->word_bytes;

    /* 64-byte aligned: one cache line, and a natural multiple of any block. */
    size_t alloc = (bytes + 63u) & ~(size_t) 63u;
    void *words = aligned_alloc(64, alloc);
    if (!words)
        return -1;

    uint8_t *msg = malloc(message_bytes);
    uint8_t *padded = malloc((size_t) blocks * alg->block_bytes);
    if (!msg || !padded) {
        free(msg);
        free(padded);
        free(words);
        return -1;
    }

    /*
     * Scatter each message into the lane-interleaved layout:
     *   word[((slot * blocks + b) * 16 + j) * lanes + lane]
     * so the `lanes` copies of word j sit contiguously and load as one vector.
     */
    for (uint64_t m = 0; m < n_messages; m++) {
        uint64_t slot = m / lanes;
        uint32_t lane = (uint32_t) (m % lanes);

        vb_build_message(start_index + (uint32_t) m, message_bytes, msg);
        pad_message(alg, msg, message_bytes, padded, blocks);

        for (uint32_t b = 0; b < blocks; b++) {
            for (uint32_t j = 0; j < VB_WORDS_PER_BLOCK; j++) {
                size_t at = (((slot * blocks) + b) * VB_WORDS_PER_BLOCK + j)
                          * lanes + lane;
                const uint8_t *src = padded + b * alg->block_bytes
                                   + (size_t) j * alg->word_bytes;
                uint64_t v = load_word(alg, src);

                if (alg->word_bytes == 8)
                    ((uint64_t *) words)[at] = v;
                else
                    ((uint32_t *) words)[at] = (uint32_t) v;
            }
        }
    }

    free(msg);
    free(padded);

    c->words = words;
    c->n_words = n_words;
    c->alg = alg;
    c->lanes = lanes;
    c->blocks = blocks;
    c->message_bytes = message_bytes;
    c->n_messages = n_messages;
    c->start_index = start_index;
    return 0;
}

void vb_corpus_free(vb_corpus *c)
{
    free(c->words);
    c->words = NULL;
    c->n_words = 0;
}

void vb_reference_checksum(const vb_algorithm *alg, uint32_t start,
                           uint64_t count, uint32_t message_bytes,
                           uint32_t iterations,
                           uint64_t checksum[VB_MAX_DIGEST_WORDS])
{
    uint64_t acc[VB_MAX_DIGEST_WORDS] = { 0 };
    uint8_t *msg = malloc(message_bytes);

    if (!msg) {
        memset(checksum, 0, VB_MAX_DIGEST_WORDS * sizeof(uint64_t));
        return;
    }

    for (uint64_t n = 0; n < count; n++) {
        uint8_t digest[VB_MAX_DIGEST_BYTES];

        vb_build_message(start + (uint32_t) n, message_bytes, msg);

        for (uint32_t it = 0; it < iterations; it++) {
            alg->hash(msg, message_bytes, digest);
            /* Feed the digest back over the head of the message, leaving the
               rest intact, so every iteration is identical work. Callers
               guarantee message_bytes >= digest_bytes when iterations > 1. */
            if (it + 1 < iterations)
                memcpy(msg, digest, alg->digest_bytes);
        }

        /* Accumulate the digest as the algorithm's own words, so the value
           matches what a kernel carrying that state XORs together. */
        for (unsigned j = 0; j < alg->digest_words; j++)
            acc[j] ^= load_word(alg, digest + (size_t) j * alg->word_bytes);
    }

    free(msg);
    memcpy(checksum, acc, sizeof acc);
}
