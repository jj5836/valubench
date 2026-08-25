/*
 * sha512.c -- scalar reference SHA-512, from FIPS 180-4.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * Written from the specification (sections 4.1.3, 5.3.5, 6.4). Constants are
 * transcribed from the standard in include/sha512_const.h.
 *
 * SHA-512 is the first algorithm here that is not 32-bit and not 64-byte
 * blocked: 64-bit words, 1024-bit blocks, an 80-round schedule with real
 * expansion, and a 128-bit length field. It is deliberately the second SHA
 * added, because it forces the width and block-geometry assumptions out of the
 * shared code rather than letting them hide.
 */

#include "hashes.h"
#include "sha512_const.h"

#include <string.h>

static inline uint64_t rotr64(uint64_t v, unsigned s)
{
    return (v >> s) | (v << ((64u - s) & 63u));
}

static inline uint64_t load_be64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | p[i];
    return v;
}

static inline void store_be64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t) (v >> (56 - 8 * i));
}

/* FIPS 180-4 section 4.1.3. */
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x)     (rotr64((x), 28) ^ rotr64((x), 34) ^ rotr64((x), 39))
#define BSIG1(x)     (rotr64((x), 14) ^ rotr64((x), 18) ^ rotr64((x), 41))
#define SSIG0(x)     (rotr64((x),  1) ^ rotr64((x),  8) ^ ((x) >> 7))
#define SSIG1(x)     (rotr64((x), 19) ^ rotr64((x), 61) ^ ((x) >> 6))

static void sha512_compress(uint64_t state[8],
                            const uint8_t block[SHA512_BLOCK_LEN])
{
    uint64_t w[80];

    for (int i = 0; i < 16; i++)
        w[i] = load_be64(block + i * 8);

    for (int i = 16; i < 80; i++)
        w[i] = SSIG1(w[i - 2]) + w[i - 7] + SSIG0(w[i - 15]) + w[i - 16];

    uint64_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint64_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 80; i++) {
        uint64_t t1 = h + BSIG1(e) + CH(e, f, g) + SHA512_K[i] + w[i];
        uint64_t t2 = BSIG0(a) + MAJ(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha512_init(sha512_ctx *ctx)
{
    memcpy(ctx->state, SHA512_IV, sizeof ctx->state);
    ctx->total_len = 0;
    ctx->buf_len = 0;
}

void sha512_update(sha512_ctx *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *) data;

    ctx->total_len += len;

    if (ctx->buf_len > 0) {
        size_t need = SHA512_BLOCK_LEN - ctx->buf_len;
        size_t take = (len < need) ? len : need;

        memcpy(ctx->buf + ctx->buf_len, p, take);
        ctx->buf_len += take;
        p += take;
        len -= take;

        if (ctx->buf_len < SHA512_BLOCK_LEN)
            return;

        sha512_compress(ctx->state, ctx->buf);
        ctx->buf_len = 0;
    }

    while (len >= SHA512_BLOCK_LEN) {
        sha512_compress(ctx->state, p);
        p += SHA512_BLOCK_LEN;
        len -= SHA512_BLOCK_LEN;
    }

    if (len > 0) {
        memcpy(ctx->buf, p, len);
        ctx->buf_len = len;
    }
}

void sha512_final(sha512_ctx *ctx, uint8_t digest[SHA512_DIGEST_LEN])
{
    /*
     * Padding, FIPS 180-4 section 5.1.2: 0x80, zeros to 112 mod 128, then the
     * length in bits as a 128-bit big-endian value. The high 64 bits are always
     * zero here -- reaching them would need a message of 2^61 bytes.
     */
    uint64_t bit_len = ctx->total_len * 8u;

    ctx->buf[ctx->buf_len++] = 0x80;

    if (ctx->buf_len > SHA512_BLOCK_LEN - 16) {
        memset(ctx->buf + ctx->buf_len, 0, SHA512_BLOCK_LEN - ctx->buf_len);
        sha512_compress(ctx->state, ctx->buf);
        ctx->buf_len = 0;
    }

    memset(ctx->buf + ctx->buf_len, 0, (SHA512_BLOCK_LEN - 16) - ctx->buf_len);
    store_be64(ctx->buf + SHA512_BLOCK_LEN - 16, 0);
    store_be64(ctx->buf + SHA512_BLOCK_LEN - 8, bit_len);
    sha512_compress(ctx->state, ctx->buf);

    for (int i = 0; i < 8; i++)
        store_be64(digest + i * 8, ctx->state[i]);
}

void sha512(const void *data, size_t len, uint8_t digest[SHA512_DIGEST_LEN])
{
    sha512_ctx ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, data, len);
    sha512_final(&ctx, digest);
}
