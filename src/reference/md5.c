/*
 * md5.c -- scalar reference MD5, implemented from the RFC 1321 specification.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * Written from the algorithm description in RFC 1321 sections 3.1-3.4. No code
 * is derived from any existing MD5 implementation -- see docs/research.md section 2.9
 * for why that constraint exists. The round constants are transcribed from the
 * specification.
 *
 * MD5 is little-endian throughout: message words are read little-endian and the
 * digest is emitted little-endian. This file makes every such conversion
 * explicit so it is correct on any host byte order.
 */

#include "hashes.h"

#include <string.h>

/*
 * Round functions, RFC 1321 section 3.4.
 *
 * These are the naive forms. They are deliberately NOT hand-optimized here:
 * this file is the oracle, and the optimizations described in docs/research.md
 * (3-input boolean instructions, H-round XOR sharing) belong in the dispatched
 * kernels where they can be measured against this baseline.
 */
#define MD5_F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | ~(z)))

/* Rotate left. The mask on the shift count keeps this defined when s == 0,
   which never happens in MD5 but costs nothing and avoids a UB footgun. */
static inline uint32_t rotl32(uint32_t v, unsigned s)
{
    return (v << s) | (v >> ((32u - s) & 31u));
}

static inline uint32_t load_le32(const uint8_t *p)
{
    return (uint32_t) p[0]
         | ((uint32_t) p[1] << 8)
         | ((uint32_t) p[2] << 16)
         | ((uint32_t) p[3] << 24);
}

static inline void store_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v);
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static inline void store_le64(uint8_t *p, uint64_t v)
{
    store_le32(p,     (uint32_t) v);
    store_le32(p + 4, (uint32_t) (v >> 32));
}

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

/*
 * Per-round rotation amounts, RFC 1321 section 3.4. Each round cycles through
 * four values across its sixteen steps.
 */
static const unsigned MD5_S[4][4] = {
    { 7, 12, 17, 22 },   /* round 1 */
    { 5,  9, 14, 20 },   /* round 2 */
    { 4, 11, 16, 23 },   /* round 3 */
    { 6, 10, 15, 21 },   /* round 4 */
};

void md5_compress(uint32_t state[4], const uint8_t block[MD5_BLOCK_LEN])
{
    uint32_t m[16];
    for (int i = 0; i < 16; i++)
        m[i] = load_le32(block + i * 4);

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];

    /*
     * The step is  a = b + rotl32(a + f(b,c,d) + m[g] + T[i], s),  with the
     * registers rotated one position each step. The message-word index g and
     * the function f change per round, per RFC 1321 section 3.4.
     */
    for (int i = 0; i < 64; i++) {
        int round = i / 16;
        uint32_t f;
        int g;

        switch (round) {
        case 0: f = MD5_F(b, c, d); g =  i;                break;
        case 1: f = MD5_G(b, c, d); g = (5 * i + 1) % 16;  break;
        case 2: f = MD5_H(b, c, d); g = (3 * i + 5) % 16;  break;
        default: f = MD5_I(b, c, d); g = (7 * i)     % 16; break;
        }

        uint32_t tmp = d;
        d = c;
        c = b;
        b = b + rotl32(a + f + m[g] + MD5_T[i], MD5_S[round][i % 4]);
        a = tmp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void md5_init(md5_ctx *ctx)
{
    /* RFC 1321 section 3.3. */
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
    ctx->total_len = 0;
    ctx->buf_len = 0;
}

void md5_update(md5_ctx *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *) data;

    ctx->total_len += len;

    /* Top up a partial block first. */
    if (ctx->buf_len > 0) {
        size_t need = MD5_BLOCK_LEN - ctx->buf_len;
        size_t take = (len < need) ? len : need;

        memcpy(ctx->buf + ctx->buf_len, p, take);
        ctx->buf_len += take;
        p += take;
        len -= take;

        if (ctx->buf_len < MD5_BLOCK_LEN)
            return;

        md5_compress(ctx->state, ctx->buf);
        ctx->buf_len = 0;
    }

    while (len >= MD5_BLOCK_LEN) {
        md5_compress(ctx->state, p);
        p += MD5_BLOCK_LEN;
        len -= MD5_BLOCK_LEN;
    }

    if (len > 0) {
        memcpy(ctx->buf, p, len);
        ctx->buf_len = len;
    }
}

void md5_final(md5_ctx *ctx, uint8_t digest[MD5_DIGEST_LEN])
{
    /*
     * Padding, RFC 1321 section 3.1: append 0x80, then zeros until the length
     * is 56 mod 64, then the original message length in bits as a little-endian
     * 64-bit value.
     */
    uint64_t bit_len = ctx->total_len * 8u;

    ctx->buf[ctx->buf_len++] = 0x80;

    if (ctx->buf_len > MD5_BLOCK_LEN - 8) {
        memset(ctx->buf + ctx->buf_len, 0, MD5_BLOCK_LEN - ctx->buf_len);
        md5_compress(ctx->state, ctx->buf);
        ctx->buf_len = 0;
    }

    memset(ctx->buf + ctx->buf_len, 0, (MD5_BLOCK_LEN - 8) - ctx->buf_len);
    store_le64(ctx->buf + MD5_BLOCK_LEN - 8, bit_len);
    md5_compress(ctx->state, ctx->buf);

    for (int i = 0; i < 4; i++)
        store_le32(digest + i * 4, ctx->state[i]);
}

void md5(const void *data, size_t len, uint8_t digest[MD5_DIGEST_LEN])
{
    md5_ctx ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(&ctx, digest);
}
