/*
 * test_config.c -- vb_config_defaults() must leave nothing indeterminate.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * main() puts a vb_config on the stack and calls vb_config_defaults() on it.
 * That function used to assign fifteen fields by name and touch nothing else,
 * so `have_expected` -- added later -- kept whatever the frame happened to
 * hold. Non-zero there makes the run skip the reference computation and
 * compare against `expected`, garbage from the same stack, and report
 * VERIFICATION FAILED for a kernel that computed correctly. Non-deterministic,
 * varying with compiler and optimisation level.
 *
 * Poisoning the struct first is the only way to test this: on a quiet stack
 * the bug is invisible, which is why it survived.
 */

#include "bench.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    vb_config cfg;
    int checks = 0, failures = 0;

    memset(&cfg, 0xAA, sizeof cfg);
    vb_config_defaults(&cfg);

    checks++;
    if (cfg.have_expected != 0) {
        failures++;
        printf("  FAIL  config: have_expected is %d, not 0 -- the run would "
               "compare against stack garbage\n", cfg.have_expected);
    }

    checks++;
    for (unsigned i = 0; i < VB_MAX_DIGEST_WORDS; i++) {
        if (cfg.expected[i] != 0) {
            failures++;
            printf("  FAIL  config: expected[%u] is not zero\n", i);
            break;
        }
    }

    /* The named defaults must survive the zeroing, or this fix broke them. */
    checks++;
    if (cfg.iterations != 1 || cfg.n_samples != 10 || cfg.pin_cpu != 1 ||
        cfg.alg == NULL || cfg.force_kernel != NULL) {
        failures++;
        printf("  FAIL  config: a named default did not survive\n");
    }

    printf("%d config checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
