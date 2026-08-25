/*
 * power.h -- energy measurement.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * For a buyer comparing hardware, energy per unit of work usually matters more
 * than throughput: it drives both the electricity bill and the rack density a
 * part can be deployed at. A benchmark that reports hashes/second and not
 * hashes/joule leaves out the number the decision often turns on.
 *
 * Sources, in the order they are preferred per scope. That preference is
 * enforced rather than merely documented: a scope picks one provider and sums
 * that provider's devices. It used to sum everything it found, which counted
 * an NVIDIA card twice where DRM hwmon and NVML both saw it.
 *
 *   powercap RAPL   /sys/class/powercap/{intel,amd}-rapl:*  -- CPU package,
 *                   cores, and on client Intel parts the `uncore` domain is
 *                   the integrated GPU, so it doubles as a device reading.
 *   DRM hwmon       the hwmon node under a DRM card device -- discrete AMD
 *                   and Intel parts expose energy1_input or power1_average.
 *   NVML            libnvidia-ml.so.1, dlopen'd like OpenCL, for NVIDIA.
 *
 * Every source is optional. Energy counters are preferred over instantaneous
 * power because integrating a sampled wattage over a short benchmark is
 * inaccurate; where only power1_average exists we say so in the source name.
 *
 * Nothing here requires root to *build* or to *run* -- but RAPL files are
 * root-readable on most modern distributions (hardening against the PLATYPUS
 * side channel), so CPU energy is frequently unavailable to an unprivileged
 * run. That is reported, not worked around.
 */

#ifndef VALUBENCH_POWER_H
#define VALUBENCH_POWER_H

#include <stdint.h>

#define VB_POWER_MAX_SRC 16

typedef enum {
    VB_PWR_CPU_PACKAGE,
    VB_PWR_CPU_CORES,
    VB_PWR_GPU,
    VB_PWR_OTHER
} vb_power_scope;

/*
 * Which subsystem produced a reading. Two providers can see the same physical
 * device -- an NVIDIA card appears through both DRM hwmon and NVML -- so a
 * scope picks one provider and sums that provider's devices, rather than
 * summing everything it finds.
 */
typedef enum {
    VB_PWR_PROV_RAPL,       /* preferred where it exists */
    VB_PWR_PROV_DRM,
    VB_PWR_PROV_NVML
} vb_power_provider;

typedef struct {
    char           name[80];
    vb_power_scope scope;
    vb_power_provider provider;

    /*
     * This source's energy is already inside another source's. Intel's RAPL
     * `uncore` domain is the integrated GPU and sits within the package, so
     * reporting it as the GPU figure is right while adding it to the package
     * counts it twice. Contained sources are reported and never totalled.
     */
    int            contained;

    /* Implementation detail; see power.c. */
    int      kind;
    int      fd;
    unsigned nvml_index;
    uint64_t wrap_uj;       /* counter range, for wraparound */

    uint64_t start_uj;
    double   joules;        /* filled by vb_power_end */
    int      valid;
} vb_power_src;

typedef struct {
    vb_power_src src[VB_POWER_MAX_SRC];
    int          n;
    char         unavailable[512];   /* why there is nothing, if n == 0 */
} vb_power;

/*
 * Discover readable energy sources. Always succeeds; n == 0 simply means none
 * were readable, with `unavailable` explaining why.
 */
void vb_power_open(vb_power *p);

/* Releases handles but preserves the measurements, so a result can still be
   reported after the sources are closed. */
void vb_power_close(vb_power *p);

/* Snapshot counters at the start of a timed region. */
void vb_power_begin(vb_power *p);

/*
 * Close the region and compute joules per source. `seconds` is used only where
 * a source reports average power rather than an energy counter.
 */
void vb_power_end(vb_power *p, double seconds);

/* Total joules for a scope across all sources, or -1 if none measured it. */
double vb_power_scope_joules(const vb_power *p, vb_power_scope scope);

/*
 * Energy over the whole machine for the run: every non-contained domain, one
 * provider per scope. This is the denominator for hashes/joule, and it is not
 * the sum of the reported scopes -- an integrated GPU appears in the GPU scope
 * and inside the CPU package, and must be counted once.
 */
double vb_power_total_joules(const vb_power *p);

const char *vb_power_scope_name(vb_power_scope s);

#endif /* VALUBENCH_POWER_H */
