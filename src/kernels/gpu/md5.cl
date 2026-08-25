/*
 * md5.cl -- the OpenCL MD5 kernel.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * A real .cl file, not a C string: editable with syntax highlighting and no
 * escaping. tools/embed_cl.c turns it into a byte array the binary carries, so
 * nothing has to be installed or located at run time.
 *
 * Complete as it stands. The round constants and all 64 steps are written out
 * below, because MD5 is a frozen standard -- RFC 1321 is not going to gain a
 * step -- and a tool that regenerated them on every build would be machinery
 * earning nothing. They were derived from the specification's own arithmetic,
 * not copied from an implementation (docs/research.md 2.9), and `make check`
 * validates the result against the scalar reference.
 *
 * LANES and STREAMS arrive as -D at build time, exactly as the CPU kernels get
 * them at compile time, so one source specialises into every variant.
 *
 * The work decomposition mirrors the CPU template so the XOR checksum matches
 * bit for bit: a group is STREAMS consecutive corpus slots, each slot holds
 * LANES messages, and work-item (g, lane) hashes the STREAMS messages at
 * (g*STREAMS + k, lane).
 *
 * LANES is the corpus interleave width, which is what buys coalescing. Neither
 * the work-group size nor the total launch size is tied to it: the host picks
 * both, and each work-item strides over as many groups as it takes to cover the
 * corpus. That decoupling lets the launch be sized to saturate the device while
 * the corpus stays free to serve the memory axis.
 */

#define ROTL(x, n)   rotate((uint)(x), (uint)(n))
/* bitselect(a,b,c) picks b where c has 1 bits, a where 0 -- one instruction on
   AMD (BFI_INT) and NVIDIA (LOP3.LUT), the GPU equivalent of the AVX-512
   vpternlogd win described in docs/research.md 4.1. */
#define MD5F(x, y, z) bitselect((z), (y), (x))
#define MD5G(x, y, z) bitselect((y), (x), (z))
#define MD5H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5I(x, y, z) ((y) ^ ((x) | ~(z)))

#define W(k, j) (((j) < 4) ? wv[k][j] : wp[k][(j) * LANES])

#define STEP(f, a, b, c, d, k, j, t, s)                 \
    a[k] = a[k] + f(b[k], c[k], d[k]) + W(k, j) + T[t]; \
    a[k] = ROTL(a[k], s);                               \
    a[k] = a[k] + b[k];

/* Streams are expanded by macro, never by a loop -- if a loop failed to unroll
   the kernel would collapse to one dependency chain and silently under-report
   the device. Same reasoning as the CPU template. */
#if STREAMS == 1
#define EACH(f, a, b, c, d, j, t, s) STEP(f,a,b,c,d,0,j,t,s)
#elif STREAMS == 2
#define EACH(f, a, b, c, d, j, t, s) STEP(f,a,b,c,d,0,j,t,s) STEP(f,a,b,c,d,1,j,t,s)
#elif STREAMS == 3
#define EACH(f, a, b, c, d, j, t, s) STEP(f,a,b,c,d,0,j,t,s) STEP(f,a,b,c,d,1,j,t,s) STEP(f,a,b,c,d,2,j,t,s)
#elif STREAMS == 4
#define EACH(f, a, b, c, d, j, t, s) STEP(f,a,b,c,d,0,j,t,s) STEP(f,a,b,c,d,1,j,t,s) STEP(f,a,b,c,d,2,j,t,s) STEP(f,a,b,c,d,3,j,t,s)
#else
#error "STREAMS must be 1..4"
#endif

/*
 * Round constants, T[i] = floor(2^32 * abs(sin(i+1))) with i in radians, from
 * RFC 1321 section 3.4. Derived from that formula rather than copied out of any
 * implementation, which is what keeps this file free of anyone else's licence
 * (docs/research.md 2.9).
 *
 * The CPU path holds the same values in src/reference/md5.c and in the CPU kernel
 * template. They cannot drift apart unnoticed: every device kernel is validated
 * against the scalar reference by `make check`, so a single wrong digit fails
 * the build's correctness gate rather than producing a fast wrong answer.
 */
__constant uint T[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
};

__kernel
void vb_md5(__global const uint *corpus,
            const uint blocks,
            const uint iterations,
            const ulong n_groups,
            const uint repeats,
            __global uint4 *partials,
            __local uint4 *scratch)
{
    const size_t gid  = get_global_id(0);
    const size_t lid  = get_local_id(0);
    const size_t lane = gid % LANES;

    const size_t slot_words  = (size_t)blocks * 16 * LANES;
    const size_t block_words = 16 * LANES;

    const size_t g_stride = get_global_size(0) / LANES;

    uint4 acc = (uint4)(0, 0, 0, 0);

    /* Sweep the corpus `repeats` times. This is how the device gets saturated
       when the working set is deliberately small: work is amplified without
       growing the footprint. `repeats` is always odd, so XORing every pass
       leaves the single-pass checksum intact -- an even count would cancel to
       zero and silently weaken verification. */
    for (uint rep = 0; rep < repeats; rep++)
    for (size_t g = gid / LANES; g < n_groups; g += g_stride) {
        __global const uint *slot[STREAMS];
        __global const uint *wp[STREAMS];
        uint wv[STREAMS][4];
        uint h0[STREAMS], h1[STREAMS], h2[STREAMS], h3[STREAMS];
        uint A[STREAMS], B[STREAMS], C[STREAMS], D[STREAMS];

        for (int k = 0; k < STREAMS; k++) {
            slot[k] = corpus + (g * STREAMS + k) * slot_words + lane;
            wv[k][0] = slot[k][0 * LANES];
            wv[k][1] = slot[k][1 * LANES];
            wv[k][2] = slot[k][2 * LANES];
            wv[k][3] = slot[k][3 * LANES];
        }

        for (uint it = 0; it < iterations; it++) {
            for (int k = 0; k < STREAMS; k++) {
                h0[k] = 0x67452301u; h1[k] = 0xefcdab89u;
                h2[k] = 0x98badcfeu; h3[k] = 0x10325476u;
            }

            for (uint b = 0; b < blocks; b++) {
                for (int k = 0; k < STREAMS; k++) {
                    wp[k] = slot[k] + (size_t)b * block_words;
                    if (b > 0) {
                        wv[k][0] = wp[k][0 * LANES];
                        wv[k][1] = wp[k][1 * LANES];
                        wv[k][2] = wp[k][2 * LANES];
                        wv[k][3] = wp[k][3 * LANES];
                    }
                    A[k] = h0[k]; B[k] = h1[k];
                    C[k] = h2[k]; D[k] = h3[k];
                }

                EACH(MD5F, A, B, C, D, 0, 0, 7)
                EACH(MD5F, D, A, B, C, 1, 1, 12)
                EACH(MD5F, C, D, A, B, 2, 2, 17)
                EACH(MD5F, B, C, D, A, 3, 3, 22)
                EACH(MD5F, A, B, C, D, 4, 4, 7)
                EACH(MD5F, D, A, B, C, 5, 5, 12)
                EACH(MD5F, C, D, A, B, 6, 6, 17)
                EACH(MD5F, B, C, D, A, 7, 7, 22)
                EACH(MD5F, A, B, C, D, 8, 8, 7)
                EACH(MD5F, D, A, B, C, 9, 9, 12)
                EACH(MD5F, C, D, A, B, 10, 10, 17)
                EACH(MD5F, B, C, D, A, 11, 11, 22)
                EACH(MD5F, A, B, C, D, 12, 12, 7)
                EACH(MD5F, D, A, B, C, 13, 13, 12)
                EACH(MD5F, C, D, A, B, 14, 14, 17)
                EACH(MD5F, B, C, D, A, 15, 15, 22)
                EACH(MD5G, A, B, C, D, 1, 16, 5)
                EACH(MD5G, D, A, B, C, 6, 17, 9)
                EACH(MD5G, C, D, A, B, 11, 18, 14)
                EACH(MD5G, B, C, D, A, 0, 19, 20)
                EACH(MD5G, A, B, C, D, 5, 20, 5)
                EACH(MD5G, D, A, B, C, 10, 21, 9)
                EACH(MD5G, C, D, A, B, 15, 22, 14)
                EACH(MD5G, B, C, D, A, 4, 23, 20)
                EACH(MD5G, A, B, C, D, 9, 24, 5)
                EACH(MD5G, D, A, B, C, 14, 25, 9)
                EACH(MD5G, C, D, A, B, 3, 26, 14)
                EACH(MD5G, B, C, D, A, 8, 27, 20)
                EACH(MD5G, A, B, C, D, 13, 28, 5)
                EACH(MD5G, D, A, B, C, 2, 29, 9)
                EACH(MD5G, C, D, A, B, 7, 30, 14)
                EACH(MD5G, B, C, D, A, 12, 31, 20)
                EACH(MD5H, A, B, C, D, 5, 32, 4)
                EACH(MD5H, D, A, B, C, 8, 33, 11)
                EACH(MD5H, C, D, A, B, 11, 34, 16)
                EACH(MD5H, B, C, D, A, 14, 35, 23)
                EACH(MD5H, A, B, C, D, 1, 36, 4)
                EACH(MD5H, D, A, B, C, 4, 37, 11)
                EACH(MD5H, C, D, A, B, 7, 38, 16)
                EACH(MD5H, B, C, D, A, 10, 39, 23)
                EACH(MD5H, A, B, C, D, 13, 40, 4)
                EACH(MD5H, D, A, B, C, 0, 41, 11)
                EACH(MD5H, C, D, A, B, 3, 42, 16)
                EACH(MD5H, B, C, D, A, 6, 43, 23)
                EACH(MD5H, A, B, C, D, 9, 44, 4)
                EACH(MD5H, D, A, B, C, 12, 45, 11)
                EACH(MD5H, C, D, A, B, 15, 46, 16)
                EACH(MD5H, B, C, D, A, 2, 47, 23)
                EACH(MD5I, A, B, C, D, 0, 48, 6)
                EACH(MD5I, D, A, B, C, 7, 49, 10)
                EACH(MD5I, C, D, A, B, 14, 50, 15)
                EACH(MD5I, B, C, D, A, 5, 51, 21)
                EACH(MD5I, A, B, C, D, 12, 52, 6)
                EACH(MD5I, D, A, B, C, 3, 53, 10)
                EACH(MD5I, C, D, A, B, 10, 54, 15)
                EACH(MD5I, B, C, D, A, 1, 55, 21)
                EACH(MD5I, A, B, C, D, 8, 56, 6)
                EACH(MD5I, D, A, B, C, 15, 57, 10)
                EACH(MD5I, C, D, A, B, 6, 58, 15)
                EACH(MD5I, B, C, D, A, 13, 59, 21)
                EACH(MD5I, A, B, C, D, 4, 60, 6)
                EACH(MD5I, D, A, B, C, 11, 61, 10)
                EACH(MD5I, C, D, A, B, 2, 62, 15)
                EACH(MD5I, B, C, D, A, 9, 63, 21)


                for (int k = 0; k < STREAMS; k++) {
                    h0[k] += A[k]; h1[k] += B[k];
                    h2[k] += C[k]; h3[k] += D[k];
                }
            }

            for (int k = 0; k < STREAMS; k++) {
                wv[k][0] = h0[k]; wv[k][1] = h1[k];
                wv[k][2] = h2[k]; wv[k][3] = h3[k];
            }
        }

        for (int k = 0; k < STREAMS; k++)
            acc ^= (uint4)(h0[k], h1[k], h2[k], h3[k]);
    }

    /* Reduce within the work-group so only one uint4 per group crosses the bus.
       A partial per work-item would put megabytes of readback inside the timed
       region and corrupt the very PCIe measurement this exists to make. */
    scratch[lid] = acc;
    barrier(CLK_LOCAL_MEM_FENCE);

    for (size_t s = get_local_size(0) / 2; s > 0; s >>= 1) {
        if (lid < s)
            scratch[lid] ^= scratch[lid + s];
        barrier(CLK_LOCAL_MEM_FENCE);
    }

    if (lid == 0)
        partials[get_group_id(0)] = scratch[0];
}
