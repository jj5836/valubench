/*
 * power.h -- energy measurement.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * For a buyer comparing hardware, energy per unit of work usually matters more
 * than throughput: it drives both the electricity bill and the rack density a
 * part can be deployed at. A benchmark that reports hashes/second and not
 * hashes/joule leaves out the number the decision often turns on.
 *
 * Sources, in the order they are preferred per scope:
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

typedef struct {
    char           name[80];
    vb_power_scope scope;

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

const char *vb_power_scope_name(vb_power_scope s);

#endif /* VALUBENCH_POWER_H */
