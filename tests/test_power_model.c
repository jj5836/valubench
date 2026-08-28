/*
 * test_power_model.c -- energy domains must be counted once each.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * The header always promised sources were used "in the order they are
 * preferred per scope". The implementation summed every valid source instead,
 * which double-counts in two different ways:
 *
 *   an NVIDIA card is visible through both DRM hwmon and NVML, so the GPU
 *   scope added the same device twice;
 *
 *   Intel's RAPL `uncore` domain is the integrated GPU and lives inside the
 *   package, so the machine total added it to a package figure that already
 *   contained it -- 15% on the development machine, in the direction that
 *   makes hardware look less efficient than it is.
 *
 * These shapes are constructed rather than measured because no single machine
 * has all of them, and the ones that do are the ones nobody has to hand.
 */

#include "power.h"

#include <stdio.h>
#include <string.h>

static vb_power_src *add(vb_power *p, const char *name, vb_power_scope sc,
                         vb_power_provider prov, int contained, double j)
{
    vb_power_src *s = &p->src[p->n++];
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", name);
    s->scope = sc;
    s->provider = prov;
    s->contained = contained;
    s->joules = j;
    s->valid = 1;
    return s;
}

static int check(const char *what, double got, double want)
{
    if (got > want - 1e-9 && got < want + 1e-9)
        return 0;
    printf("  FAIL  %s: got %.4f, want %.4f\n", what, got, want);
    return 1;
}

int main(void)
{
    int checks = 0, failures = 0;
    vb_power p;

    /* Intel client: uncore is the iGPU and sits inside the package. The GPU
       figure is still the useful iGPU reading; the machine total is not the
       sum of the two. */
    memset(&p, 0, sizeof p);
    add(&p, "RAPL package-0", VB_PWR_CPU_PACKAGE, VB_PWR_PROV_RAPL, 0, 10.0);
    add(&p, "RAPL core",      VB_PWR_CPU_CORES,   VB_PWR_PROV_RAPL, 0,  7.0);
    add(&p, "RAPL uncore",    VB_PWR_GPU,         VB_PWR_PROV_RAPL, 1,  2.0);
    failures += check("client: gpu scope reports the iGPU",
                      vb_power_scope_joules(&p, VB_PWR_GPU), 2.0); checks++;
    failures += check("client: package reported as itself",
                      vb_power_scope_joules(&p, VB_PWR_CPU_PACKAGE), 10.0); checks++;
    failures += check("client: total counts the package once",
                      vb_power_total_joules(&p), 10.0); checks++;

    /* Two sockets: two packages are two domains and do sum. */
    memset(&p, 0, sizeof p);
    add(&p, "RAPL package-0", VB_PWR_CPU_PACKAGE, VB_PWR_PROV_RAPL, 0, 10.0);
    add(&p, "RAPL package-1", VB_PWR_CPU_PACKAGE, VB_PWR_PROV_RAPL, 0, 11.0);
    failures += check("two sockets sum",
                      vb_power_scope_joules(&p, VB_PWR_CPU_PACKAGE), 21.0); checks++;
    failures += check("two sockets total",
                      vb_power_total_joules(&p), 21.0); checks++;

    /* One card, two providers. DRM is preferred over NVML and the card is
       counted once, not twice. */
    memset(&p, 0, sizeof p);
    add(&p, "card0 hwmon", VB_PWR_GPU, VB_PWR_PROV_DRM,  0, 30.0);
    add(&p, "NVML gpu0",   VB_PWR_GPU, VB_PWR_PROV_NVML, 0, 31.0);
    failures += check("one card seen twice is counted once",
                      vb_power_scope_joules(&p, VB_PWR_GPU), 30.0); checks++;
    failures += check("total counts that card once",
                      vb_power_total_joules(&p), 30.0); checks++;

    /* Two cards through one provider do sum. */
    memset(&p, 0, sizeof p);
    add(&p, "NVML gpu0", VB_PWR_GPU, VB_PWR_PROV_NVML, 0, 30.0);
    add(&p, "NVML gpu1", VB_PWR_GPU, VB_PWR_PROV_NVML, 0, 31.0);
    failures += check("two cards sum",
                      vb_power_scope_joules(&p, VB_PWR_GPU), 61.0); checks++;

    /* A discrete card beside a CPU package: genuinely disjoint, so they add. */
    memset(&p, 0, sizeof p);
    add(&p, "RAPL package-0", VB_PWR_CPU_PACKAGE, VB_PWR_PROV_RAPL, 0, 10.0);
    add(&p, "NVML gpu0",      VB_PWR_GPU,         VB_PWR_PROV_NVML, 0, 30.0);
    failures += check("cpu and a discrete gpu add",
                      vb_power_total_joules(&p), 40.0); checks++;

    /* No sources at all is not zero joules, it is no answer. */
    memset(&p, 0, sizeof p);
    failures += check("nothing reports -1",
                      vb_power_total_joules(&p), -1.0); checks++;

    /*
     * GPU clock telemetry. The aggregation itself needs an NVIDIA card, but
     * the two things that decide whether a report is readable do not: an
     * unsampled aggregate must not look like a measurement of zero, and the
     * throttle mask must name every reason it saw.
     */
    vb_gpu_clocks g;
    vb_gpu_clocks_reset(&g);
    failures += check("unsampled clocks are invalid, not zero",
                      (double) g.valid, 0.0); checks++;
    failures += check("unsampled temperature is -1, not 0",
                      (double) g.temp_c_max, -1.0); checks++;
    vb_gpu_clocks_sample(&g);       /* no NVML here: must stay silent */
    failures += check("sampling without NVML records nothing",
                      (double) g.n_samples, 0.0); checks++;

    char why[128];
    failures += check("no throttle reasons reads as none",
                      (double) !strcmp(vb_gpu_throttle_str(0, why, sizeof why),
                                       "none"), 1.0); checks++;
    failures += check("a power cap is named",
                      (double) !strcmp(vb_gpu_throttle_str(
                          VB_GPU_THROTTLE_POWER, why, sizeof why),
                          "power-cap"), 1.0); checks++;
    failures += check("two reasons are both named",
                      (double) !strcmp(vb_gpu_throttle_str(
                          VB_GPU_THROTTLE_POWER | VB_GPU_THROTTLE_HW_THERMAL,
                          why, sizeof why), "power-cap,hw-thermal"), 1.0);
    checks++;
    /* An unknown bit must not silently read as "none" alongside a known one,
       and must not run off the end of a short buffer. */
    char tiny[6];
    vb_gpu_throttle_str(VB_GPU_THROTTLE_POWER | VB_GPU_THROTTLE_HW_BRAKE,
                        tiny, sizeof tiny);
    failures += check("a short buffer stays terminated",
                      (double) (strlen(tiny) < sizeof tiny), 1.0); checks++;

    printf("%d power-model checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
