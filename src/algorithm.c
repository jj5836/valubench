/*
 * algorithm.c -- the algorithm descriptor table.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 */

#include "algorithm.h"
#include "hashes.h"

#include <string.h>

static void md5_thunk(const void *m, size_t n, uint8_t *d)    { md5(m, n, d); }
static void sha1_thunk(const void *m, size_t n, uint8_t *d)   { sha1(m, n, d); }
static void sha512_thunk(const void *m, size_t n, uint8_t *d) { sha512(m, n, d); }

static const vb_algorithm ALGS[VB_ALG_COUNT] = {
    [VB_ALG_MD5] = {
        .id = VB_ALG_MD5, .name = "md5",
        .digest_words = 4, .digest_bytes = 16,
        .word_bytes = 4, .block_bytes = 64, .length_bytes = 8,
        .rounds = 64, .big_endian = 0, .hash = md5_thunk,
    },
    [VB_ALG_SHA1] = {
        .id = VB_ALG_SHA1, .name = "sha1",
        .digest_words = 5, .digest_bytes = 20,
        .word_bytes = 4, .block_bytes = 64, .length_bytes = 8,
        .rounds = 80, .big_endian = 1, .hash = sha1_thunk,
    },
    [VB_ALG_SHA512] = {
        .id = VB_ALG_SHA512, .name = "sha512",
        .digest_words = 8, .digest_bytes = 64,
        .word_bytes = 8, .block_bytes = 128, .length_bytes = 16,
        .rounds = 80, .big_endian = 1, .hash = sha512_thunk,
    },
};

const vb_algorithm *vb_algorithm_by_id(vb_alg_id id)
{
    if (id < 0 || id >= VB_ALG_COUNT)
        return NULL;
    return &ALGS[id];
}

const vb_algorithm *vb_algorithm_by_name(const char *name)
{
    for (int i = 0; i < VB_ALG_COUNT; i++)
        if (strcmp(ALGS[i].name, name) == 0)
            return &ALGS[i];
    return NULL;
}
