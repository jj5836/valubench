/*
 * test_thread_failure.c -- the reference must be right when a thread does not start.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * vb_reference_checksum_mt() splits the corpus across threads and XORs the
 * parts. It tracked successful creations with a high-water mark, which assumed
 * the successes formed a contiguous prefix. They need not: with thread 1 failed
 * and thread 2 started, the join loop joined a pthread_t that was never created
 * and folded slice 1's untouched zeros into the answer.
 *
 * That is a defect in the oracle. The benchmark decides whether a kernel is
 * correct by comparing against this value, so a wrong reference fails correct
 * kernels -- the one outcome the whole design exists to prevent.
 *
 * Run under tests/fail_pthread_create.so with VB_FAIL_CREATE set to each index
 * in turn. The multithreaded answer must equal the serial one every time.
 *
 * The pool half of this check (in the Makefile) asserts an invariant rather
 * than an index, because the index is not portable: on a machine with an
 * NVIDIA driver, enumerating devices spawns a thread before the pool exists,
 * so fault 1 lands on the driver and the pool starts intact. What must hold
 * everywhere is that a run either fails or uses every thread it asked for --
 * never a degraded pool with a number attached.
 */

#include "valubench.h"
#include "algorithm.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    const vb_algorithm *alg = vb_algorithm_by_id(VB_ALG_MD5);
    const uint64_t count = 512;
    const uint32_t msg = 55, iters = 2;
    int checks = 0, failures = 0;

    uint64_t want[VB_MAX_DIGEST_WORDS];
    vb_reference_checksum(alg, 0, count, msg, iters, want);

    for (unsigned threads = 2; threads <= 8; threads++) {
        uint64_t got[VB_MAX_DIGEST_WORDS];
        vb_reference_checksum_mt(alg, 0, count, msg, iters, threads, got);
        checks++;
        if (memcmp(got, want, sizeof want) != 0) {
            printf("  FAIL  threads=%u disagrees with the serial reference\n",
                   threads);
            failures++;
        }
    }

    printf("%d thread-failure checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
