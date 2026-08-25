/*
 * test_report_json.c -- the JSON must parse for result shapes hardware is rare.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * check_output_contract.sh parses the documents a real run produces, which is
 * most of what matters and misses exactly the defect that motivated it: the
 * energy `sources` array separated elements on the loop index rather than on
 * what had been emitted, so it emitted a leading comma only when the *first*
 * source was invalid and a later one was valid. No machine here has that
 * combination, so no run reproduces it, and the array is unparseable on the
 * machine that does.
 *
 * This drives the renderer directly with a constructed result, so the shape can
 * be arbitrary rather than whatever the hardware happens to offer. It checks
 * balance and structure rather than parsing JSON in C: a leading comma inside
 * an array is what the bug produces, and a stray comma before ] or } is the
 * general form of it.
 */

/* open_memstream is POSIX 2008; -std=c11 hides it without this. src/power.c,
   sysinfo.c and bench.c use the same spelling. */
#define _GNU_SOURCE

#include "bench.h"
#include "report.h"
#include "sysinfo.h"
#include "algorithm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_empty_element(const char *s)
{
    /* "[," or "{," -- an element separator with nothing before it -- and the
       mirrored ",]" / ",}". Whitespace between is allowed and skipped. */
    for (const char *p = s; *p; p++) {
        if (*p != '[' && *p != '{' && *p != ',')
            continue;
        const char *q = p + 1;
        while (*q == ' ' || *q == '\n' || *q == '\t')
            q++;
        if ((*p == '[' || *p == '{') && *q == ',')
            return 1;
        if (*p == ',' && (*q == ']' || *q == '}'))
            return 1;
    }
    return 0;
}

static int balanced(const char *s)
{
    int braces = 0, brackets = 0, in_str = 0;
    for (const char *p = s; *p; p++) {
        if (in_str) {
            if (*p == '\\' && p[1]) p++;
            else if (*p == '"') in_str = 0;
            continue;
        }
        switch (*p) {
        case '"': in_str = 1; break;
        case '{': braces++;   break;
        case '}': braces--;   break;
        case '[': brackets++; break;
        case ']': brackets--; break;
        default: break;
        }
    }
    return braces == 0 && brackets == 0 && !in_str;
}

/* A minimally plausible result: enough fields set that the renderer has
   something to say, and the power sources arranged by the caller. */
static void base_result(vb_result *r)
{
    size_t n_kernels = 0;
    const vb_kernel *ks = vb_kernels(&n_kernels);

    memset(r, 0, sizeof *r);
    /* The renderer dereferences the kernel, so it has to be a real one. */
    r->kernel = &ks[0];
    r->alg = vb_algorithm_by_id(VB_ALG_MD5);
    r->message_bytes = 55;
    r->blocks = 1;
    r->iterations = 1;
    r->threads = 1;
    r->batch_messages = 1024;
    r->working_set_bytes = 1024 * 64;
    r->n_samples = 3;
    r->sample_hps[0] = 1.0e6; r->sample_hps[1] = 1.1e6; r->sample_hps[2] = 1.05e6;
    r->median = 1.05e6; r->min = 1.0e6; r->max = 1.1e6;
    r->mean = 1.05e6; r->stddev = 1.0e4; r->cov = 0.0095;
    r->verified = 1;
    r->total_hashes = 3000000; r->total_seconds = 3.0;
}

static int render_and_check(const char *desc, vb_result *r)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    if (!f) { printf("  FAIL  %s: open_memstream\n", desc); return 1; }

    vb_sysinfo si;
    memset(&si, 0, sizeof si);
    vb_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.alg = r->alg;

    vb_report_json(f, r, &si, &cfg);
    fclose(f);

    int bad = 0;
    if (!buf || !len) { printf("  FAIL  %s: no output\n", desc); return 1; }
    if (has_empty_element(buf)) {
        printf("  FAIL  %s: an array or object element is empty "
               "(a separator with nothing beside it)\n", desc);
        bad = 1;
    }
    if (!balanced(buf)) {
        printf("  FAIL  %s: braces or brackets unbalanced\n", desc);
        bad = 1;
    }
    free(buf);
    return bad;
}

int main(void)
{
    int checks = 0, failures = 0;
    vb_result r;

    /* No energy at all: the common case, and the "available": false branch. */
    base_result(&r);
    r.power.n = 0;
    snprintf(r.power.unavailable, sizeof r.power.unavailable, "no counters");
    failures += render_and_check("no energy sources", &r); checks++;

    /* One valid source: what every machine with RAPL produces. */
    base_result(&r);
    r.power.n = 1;
    snprintf(r.power.src[0].name, sizeof r.power.src[0].name, "package-0");
    r.power.src[0].valid = 1; r.power.src[0].joules = 12.5;
    failures += render_and_check("one valid source", &r); checks++;

    /* The defect: first source invalid, second valid. The separator must key
       on what has been emitted, not on the loop index. */
    base_result(&r);
    r.power.n = 2;
    snprintf(r.power.src[0].name, sizeof r.power.src[0].name, "broken-0");
    r.power.src[0].valid = 0;
    snprintf(r.power.src[1].name, sizeof r.power.src[1].name, "package-0");
    r.power.src[1].valid = 1; r.power.src[1].joules = 12.5;
    failures += render_and_check("first source invalid, second valid", &r); checks++;

    /* And the reverse, plus a hole in the middle. */
    base_result(&r);
    r.power.n = 3;
    snprintf(r.power.src[0].name, sizeof r.power.src[0].name, "package-0");
    r.power.src[0].valid = 1; r.power.src[0].joules = 9.0;
    snprintf(r.power.src[1].name, sizeof r.power.src[1].name, "broken-1");
    r.power.src[1].valid = 0;
    snprintf(r.power.src[2].name, sizeof r.power.src[2].name, "dram-0");
    r.power.src[2].valid = 1; r.power.src[2].joules = 3.0;
    failures += render_and_check("a hole in the middle", &r); checks++;

    /* Every source invalid: the array must be empty, not a lone comma. */
    base_result(&r);
    r.power.n = 2;
    r.power.src[0].valid = 0;
    r.power.src[1].valid = 0;
    failures += render_and_check("every source invalid", &r); checks++;

    printf("%d report-shape checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
