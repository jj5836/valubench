/*
 * hashes.h -- scalar reference implementations, written from their specs.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * These are the correctness oracles every SIMD and GPU kernel is validated
 * against, so they are written for auditability rather than speed. None is
 * derived from an existing implementation: MD5 from RFC 1321, SHA-1 and SHA-512
 * from FIPS 180-4, with constants transcribed from those documents.
 */

#ifndef VALUBENCH_HASHES_H
#define VALUBENCH_HASHES_H

#include <stddef.h>
#include <stdint.h>

/* Widest of the supported digests and blocks, for fixed-size buffers. */
#define VB_MAX_DIGEST_BYTES 64
#define VB_MAX_BLOCK_BYTES  128

/* ---- MD5: 128-bit digest, 512-bit block, little-endian ------------------ */

#define MD5_DIGEST_LEN 16
#define MD5_BLOCK_LEN  64

typedef struct {
    uint32_t state[4];
    uint64_t total_len;
    uint8_t  buf[MD5_BLOCK_LEN];
    size_t   buf_len;
} md5_ctx;

void md5_init(md5_ctx *ctx);
void md5_update(md5_ctx *ctx, const void *data, size_t len);
void md5_final(md5_ctx *ctx, uint8_t digest[MD5_DIGEST_LEN]);
void md5(const void *data, size_t len, uint8_t digest[MD5_DIGEST_LEN]);
void md5_compress(uint32_t state[4], const uint8_t block[MD5_BLOCK_LEN]);

/* ---- SHA-1: 160-bit digest, 512-bit block, big-endian ------------------- */

#define SHA1_DIGEST_LEN 20
#define SHA1_BLOCK_LEN  64

typedef struct {
    uint32_t state[5];
    uint64_t total_len;
    uint8_t  buf[SHA1_BLOCK_LEN];
    size_t   buf_len;
} sha1_ctx;

void sha1_init(sha1_ctx *ctx);
void sha1_update(sha1_ctx *ctx, const void *data, size_t len);
void sha1_final(sha1_ctx *ctx, uint8_t digest[SHA1_DIGEST_LEN]);
void sha1(const void *data, size_t len, uint8_t digest[SHA1_DIGEST_LEN]);

/* ---- SHA-512: 512-bit digest, 1024-bit block, big-endian ---------------- */

#define SHA512_DIGEST_LEN 64
#define SHA512_BLOCK_LEN  128

typedef struct {
    uint64_t state[8];
    uint64_t total_len;          /* bytes; the 128-bit field never overflows
                                    for any message this benchmark builds */
    uint8_t  buf[SHA512_BLOCK_LEN];
    size_t   buf_len;
} sha512_ctx;

void sha512_init(sha512_ctx *ctx);
void sha512_update(sha512_ctx *ctx, const void *data, size_t len);
void sha512_final(sha512_ctx *ctx, uint8_t digest[SHA512_DIGEST_LEN]);
void sha512(const void *data, size_t len, uint8_t digest[SHA512_DIGEST_LEN]);

#endif /* VALUBENCH_HASHES_H */
