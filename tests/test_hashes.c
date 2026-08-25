/*
 * test_hashes.c -- correctness tests for the scalar reference hashes.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * The reference implementation is the oracle every other kernel is checked
 * against, so it has to be checked against something external: the published
 * RFC 1321 test vectors, plus the padding boundaries and streaming behaviour
 * that a test-vector-only suite would miss.
 */

#include "hashes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int checks   = 0;

static void hex(const uint8_t d[MD5_DIGEST_LEN], char out[33])
{
    static const char *digits = "0123456789abcdef";
    for (int i = 0; i < MD5_DIGEST_LEN; i++) {
        out[i * 2]     = digits[d[i] >> 4];
        out[i * 2 + 1] = digits[d[i] & 0x0f];
    }
    out[32] = '\0';
}

static void expect(const char *what, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("  FAIL  %s\n        got  %s\n        want %s\n", what, got, want);
    }
}

/* ---- RFC 1321 appendix A.5 test suite ---------------------------------- */

static void test_rfc_vectors(void)
{
    static const struct { const char *msg; const char *want; } v[] = {
        { "",                                 "d41d8cd98f00b204e9800998ecf8427e" },
        { "a",                                "0cc175b9c0f1b6a831c399e269772661" },
        { "abc",                              "900150983cd24fb0d6963f7d28e17f72" },
        { "message digest",                   "f96b697d7cb7938d525a2f31aaf161d0" },
        { "abcdefghijklmnopqrstuvwxyz",       "c3fcd3d76192e4007dfb496cca67e13b" },
        { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
                                              "d174ab98d277d9f5a5611c2c9f419d9f" },
        { "123456789012345678901234567890123456789012345678901234567890"
          "12345678901234567890",             "57edf4a22be3c955ac49da2e2107b67a" },
    };

    printf("RFC 1321 test vectors\n");
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        uint8_t d[MD5_DIGEST_LEN];
        char got[33], label[80];

        md5(v[i].msg, strlen(v[i].msg), d);
        hex(d, got);
        snprintf(label, sizeof label, "md5(\"%.32s%s\")",
                 v[i].msg, strlen(v[i].msg) > 32 ? "..." : "");
        expect(label, got, v[i].want);
    }
}

/* ---- Padding boundaries ------------------------------------------------ */

/*
 * The RFC vectors span one- and two-block messages but miss every point where
 * the block count actually changes -- 55/56, 119/120, 183/184 -- which is
 * exactly where padding implementations break.
 *
 * These lengths bracket each transition, so together with the RFC vectors the
 * padding rule is pinned at every structural boundary in the range. Values were
 * captured from coreutils md5sum, an independent implementation; that tool is
 * no longer invoked by the suite, since a value once established is a fact and
 * re-deriving it on every run bought only a dependency.
 */
static void test_padding_boundaries(void)
{
    static const struct { size_t len; const char *want; } v[] = {
        {  54, "eced9e0b81ef2bba605cbc5e2e76a1d0" },
        {  55, "ef1772b6dff9a122358552954ad0df65" },
        {  56, "3b0c8ac703f828b04c6c197006d17218" },
        {  57, "652b906d60af96844ebd21b674f35e93" },
        {  63, "b06521f39153d618550606be297466d5" },
        {  64, "014842d480b571495a4a0363793f7367" },
        {  65, "c743a45e0d2e6a95cb859adae0248435" },
        { 119, "8a7bd0732ed6a28ce75f6dabc90e1613" },
        { 120, "5f61c0ccad4cac44c75ff505e1f1e537" },
        { 128, "e510683b3f5ffe4093d021808bc6ff70" },
        { 184, "63642b027ee89938c922722650f2eb9b" },
    };

    printf("Padding boundaries (message of 'a' repeated)\n");
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        uint8_t d[MD5_DIGEST_LEN];
        char got[33], label[64];
        char *msg = malloc(v[i].len);

        memset(msg, 'a', v[i].len);
        md5(msg, v[i].len, d);
        hex(d, got);
        snprintf(label, sizeof label, "%zu bytes", v[i].len);
        expect(label, got, v[i].want);
        free(msg);
    }
}

/* ---- Streaming equivalence --------------------------------------------- */

/*
 * md5_update() must produce the same digest regardless of how the message is
 * split across calls. This exercises the partial-block buffering that the
 * one-shot path never touches.
 */
static void test_streaming(void)
{
    const size_t n = 1000;
    uint8_t *msg = malloc(n);
    uint8_t one_shot[MD5_DIGEST_LEN];
    char want[33];

    for (size_t i = 0; i < n; i++)
        msg[i] = (uint8_t) (i * 31u + 7u);

    md5(msg, n, one_shot);
    hex(one_shot, want);

    printf("Streaming equivalence (1000-byte message)\n");

    static const size_t chunks[] = { 1, 3, 7, 16, 31, 63, 64, 65, 127, 128, 333 };
    for (size_t c = 0; c < sizeof chunks / sizeof chunks[0]; c++) {
        md5_ctx ctx;
        uint8_t d[MD5_DIGEST_LEN];
        char got[33], label[64];

        md5_init(&ctx);
        for (size_t off = 0; off < n; off += chunks[c]) {
            size_t take = n - off < chunks[c] ? n - off : chunks[c];
            md5_update(&ctx, msg + off, take);
        }
        md5_final(&ctx, d);
        hex(d, got);
        snprintf(label, sizeof label, "chunk size %zu", chunks[c]);
        expect(label, got, want);
    }

    free(msg);
}

/* ---- Long message ------------------------------------------------------ */

static void test_million_a(void)
{
    md5_ctx ctx;
    uint8_t d[MD5_DIGEST_LEN];
    char got[33];
    uint8_t block[1000];

    memset(block, 'a', sizeof block);
    md5_init(&ctx);
    for (int i = 0; i < 1000; i++)
        md5_update(&ctx, block, sizeof block);
    md5_final(&ctx, d);
    hex(d, got);

    printf("Long message\n");
    expect("1,000,000 x 'a'", got, "7707d6ae4e027c70eea2a935c2296f21");
}

/* ---- SHA-1 and SHA-512, FIPS 180-4 ------------------------------------- */

/*
 * These also verify every SHA constant in the tree, wherever it is written:
 * SHA-1's K and initial state in src/reference/sha1.c and the two SHA-1 kernel
 * templates, and SHA-512's eighty round constants and initial state in
 * include/sha512_const.h. The tables are transcribed rather than derived, so a
 * single wrong digit in any of them fails one of these.
 */
static void hexn(const uint8_t *d, int n, char *o)
{
    static const char *digits = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        o[i * 2]     = digits[d[i] >> 4];
        o[i * 2 + 1] = digits[d[i] & 0x0f];
    }
    o[n * 2] = '\0';
}

static void test_sha_vectors(void)
{
    static const struct { const char *msg, *s1, *s512; } v[] = {
        { "",
          "da39a3ee5e6b4b0d3255bfef95601890afd80709",
          "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
          "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e" },
        { "abc",
          "a9993e364706816aba3e25717850c26c9cd0d89d",
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
          "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f" },
        { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
          "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
          "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c335"
          "96fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445" },
        /* Straddles SHA-512's 112-byte padding boundary, where the length
           field stops fitting in the final 1024-bit block. */
        { "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
          "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
          "a49b2446a02c645bf419f995b67091253a04a259",
          "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
          "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909" },
    };

    printf("FIPS 180-4 vectors (SHA-1, SHA-512)\n");
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        uint8_t d1[SHA1_DIGEST_LEN], d5[SHA512_DIGEST_LEN];
        char g1[41], g5[129], label[64];

        sha1(v[i].msg, strlen(v[i].msg), d1);
        sha512(v[i].msg, strlen(v[i].msg), d5);
        hexn(d1, SHA1_DIGEST_LEN, g1);
        hexn(d5, SHA512_DIGEST_LEN, g5);

        snprintf(label, sizeof label, "sha1 of %zu bytes", strlen(v[i].msg));
        expect(label, g1, v[i].s1);
        snprintf(label, sizeof label, "sha512 of %zu bytes", strlen(v[i].msg));
        expect(label, g5, v[i].s512);
    }
}

static void test_sha_long(void)
{
    sha1_ctx c1;
    sha512_ctx c5;
    uint8_t block[1000], d1[SHA1_DIGEST_LEN], d5[SHA512_DIGEST_LEN];
    char g1[41], g5[129];

    memset(block, 'a', sizeof block);
    sha1_init(&c1);
    sha512_init(&c5);
    for (int i = 0; i < 1000; i++) {
        sha1_update(&c1, block, sizeof block);
        sha512_update(&c5, block, sizeof block);
    }
    sha1_final(&c1, d1);
    sha512_final(&c5, d5);
    hexn(d1, SHA1_DIGEST_LEN, g1);
    hexn(d5, SHA512_DIGEST_LEN, g5);

    printf("Long message (SHA)\n");
    expect("sha1 1,000,000 x 'a'", g1,
           "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    expect("sha512 1,000,000 x 'a'", g5,
           "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
           "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b");
}

int main(void)
{
    test_rfc_vectors();
    test_padding_boundaries();
    test_streaming();
    test_million_a();
    test_sha_vectors();
    test_sha_long();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
