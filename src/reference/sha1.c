/*
 * sha1.c -- scalar reference SHA-1, from FIPS 180-4.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * Written from the specification (sections 4.1.1, 5.3.1, 6.1). No code derives
 * from an existing implementation; the constants are transcribed from the
 * standard.
 *
 * SHA-1 is big-endian throughout, where MD5 is little-endian, and it *expands*
 * its sixteen message words to eighty rather than permuting them. Those two
 * differences are most of what separates the two implementations.
 *
 * Like md5.c beside it this is the correctness oracle, written for clarity: no
 * unrolling, no tricks. Speed is the job of the dispatched kernels.
 */

#include "hashes.h"

#include <string.h>

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

/* FIPS 180-4 section 5.3.1. */
static const uint32_t SHA1_IV[5] = {
    0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u,
};

static inline uint32_t rotl32(uint32_t v, unsigned s)
{
    return (v << s) | (v >> ((32u - s) & 31u));
}

static inline uint32_t load_be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24)
         | ((uint32_t) p[1] << 16)
         | ((uint32_t) p[2] << 8)
         |  (uint32_t) p[3];
}

static inline void store_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) (v);
}

static inline void store_be64(uint8_t *p, uint64_t v)
{
    store_be32(p,     (uint32_t) (v >> 32));
    store_be32(p + 4, (uint32_t) v);
}

static void sha1_compress(uint32_t state[5], const uint8_t block[SHA1_BLOCK_LEN])
{
    uint32_t w[80];

    for (int i = 0; i < 16; i++)
        w[i] = load_be32(block + i * 4);

    /* Message schedule, FIPS 180-4 section 6.1.2 step 1. Unlike MD5, which
       reuses its sixteen words in a fixed permutation, SHA-1 expands them. */
    for (int i = 16; i < 80; i++)
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = state[0], b = state[1], c = state[2];
    uint32_t d = state[3], e = state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f;
        int g = i / 20;

        switch (g) {
        case 0:  f = (b & c) | (~b & d);            break;  /* Ch  */
        case 2:  f = (b & c) | (b & d) | (c & d);   break;  /* Maj */
        default: f = b ^ c ^ d;                     break;  /* Parity */
        }

        uint32_t t = rotl32(a, 5) + f + e + SHA1_K[g] + w[i];
        e = d;
        d = c;
        c = rotl32(b, 30);
        b = a;
        a = t;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1_init(sha1_ctx *ctx)
{
    memcpy(ctx->state, SHA1_IV, sizeof ctx->state);
    ctx->total_len = 0;
    ctx->buf_len = 0;
}

void sha1_update(sha1_ctx *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *) data;

    ctx->total_len += len;

    if (ctx->buf_len > 0) {
        size_t need = SHA1_BLOCK_LEN - ctx->buf_len;
        size_t take = (len < need) ? len : need;

        memcpy(ctx->buf + ctx->buf_len, p, take);
        ctx->buf_len += take;
        p += take;
        len -= take;

        if (ctx->buf_len < SHA1_BLOCK_LEN)
            return;

        sha1_compress(ctx->state, ctx->buf);
        ctx->buf_len = 0;
    }

    while (len >= SHA1_BLOCK_LEN) {
        sha1_compress(ctx->state, p);
        p += SHA1_BLOCK_LEN;
        len -= SHA1_BLOCK_LEN;
    }

    if (len > 0) {
        memcpy(ctx->buf, p, len);
        ctx->buf_len = len;
    }
}

void sha1_final(sha1_ctx *ctx, uint8_t digest[SHA1_DIGEST_LEN])
{
    /* Padding, FIPS 180-4 section 5.1.1: 0x80, zeros to 56 mod 64, then the
       length in bits as a 64-bit big-endian value. */
    uint64_t bit_len = ctx->total_len * 8u;

    ctx->buf[ctx->buf_len++] = 0x80;

    if (ctx->buf_len > SHA1_BLOCK_LEN - 8) {
        memset(ctx->buf + ctx->buf_len, 0, SHA1_BLOCK_LEN - ctx->buf_len);
        sha1_compress(ctx->state, ctx->buf);
        ctx->buf_len = 0;
    }

    memset(ctx->buf + ctx->buf_len, 0, (SHA1_BLOCK_LEN - 8) - ctx->buf_len);
    store_be64(ctx->buf + SHA1_BLOCK_LEN - 8, bit_len);
    sha1_compress(ctx->state, ctx->buf);

    for (int i = 0; i < 5; i++)
        store_be32(digest + i * 4, ctx->state[i]);
}

void sha1(const void *data, size_t len, uint8_t digest[SHA1_DIGEST_LEN])
{
    sha1_ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, digest);
}
