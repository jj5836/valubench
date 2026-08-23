/*
 * test_checkpoints.c -- the checkpointed reference must equal the serial one.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * vb_reference_checksums() walks each message once to the largest requested
 * iteration count and snapshots the accumulator at each rung, because the
 * digest after k iterations is a prefix of the chain for any larger k. That is
 * only a saving if it is exact: the expected checksums are the correctness gate
 * for every kernel, so a checkpoint that drifted from the serial value would
 * not make results slower, it would make them wrong.
 *
 * So: compute a ladder both ways and require equality, for every algorithm, at
 * message lengths either side of a padding boundary.
 */

#include "valubench.h"
#include "algorithm.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    static const uint32_t ladder[] = { 1, 2, 3, 4, 5, 8, 16, 17, 32, 64 };
    static const unsigned n_ladder = sizeof ladder / sizeof ladder[0];
    /* 55/56 straddles the 64-byte block boundary, 111/112 the 128-byte one. */
    static const uint32_t lengths[] = { 55, 56, 111, 112, 255 };
    static const uint64_t count = 64;

    int checks = 0, failures = 0;

    for (int a = 0; a < VB_ALG_COUNT; a++) {
        const vb_algorithm *alg = vb_algorithm_by_id((vb_alg_id) a);

        for (unsigned li = 0; li < sizeof lengths / sizeof lengths[0]; li++) {
            uint32_t mb = lengths[li];

            /* Iterating feeds the digest back over the message head, so the
               message must be at least a digest wide. */
            if (mb < alg->digest_bytes)
                continue;

            uint64_t batched[sizeof ladder / sizeof ladder[0]][VB_MAX_DIGEST_WORDS];
            vb_reference_checksums(alg, 0, count, mb, ladder, n_ladder, batched);

            for (unsigned k = 0; k < n_ladder; k++) {
                uint64_t serial[VB_MAX_DIGEST_WORDS];
                vb_reference_checksum(alg, 0, count, mb, ladder[k], serial);
                checks++;

                if (memcmp(serial, batched[k],
                           VB_MAX_DIGEST_WORDS * sizeof(uint64_t)) != 0) {
                    printf("  FAIL  %s %u bytes, %u iterations: "
                           "checkpoint %016llx serial %016llx\n",
                           alg->name, mb, ladder[k],
                           (unsigned long long) batched[k][0],
                           (unsigned long long) serial[0]);
                    failures++;
                }
            }
        }
    }

    printf("%d checkpoint checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
