/*
 * sysinfo.h -- environment capture.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * A number without a machine description is not comparable to anything, which
 * is the main reason results databases like OpenBenchmarking work at all
 * (docs/research.md 1.3). Everything here is read from /proc and /sys -- no
 * dependencies, no network.
 */

#ifndef VALUBENCH_SYSINFO_H
#define VALUBENCH_SYSINFO_H

typedef struct {
    char cpu_brand[64];
    int  cpus_online;
    int  smt_active;            /* -1 unknown */
    char governor[32];          /* "mixed" if cores disagree */
    long freq_khz_min;
    long freq_khz_max;
    long freq_khz_now;
    /* Sized for the worst case the kernel and /etc/os-release can hand back:
       utsname fields are 65 bytes each, PRETTY_NAME can run to 243. */
    char kernel[160];
    char os[256];
    char compiler[64];

    /* 1-minute load average, -1 if unavailable. On a busy machine this is
       usually the dominant source of run-to-run variance -- larger than
       governor or SMT effects -- so it is captured and warned about. */
    double loadavg1;

    int  has_sse2, has_avx2, has_avx512f;
} vb_sysinfo;

void vb_sysinfo_collect(vb_sysinfo *si);

/*
 * Environment conditions that make results less trustworthy, as a
 * newline-free human string, or NULL if nothing is worth warning about.
 * Returns a pointer to a static buffer.
 */
const char *vb_sysinfo_warnings(const vb_sysinfo *si);

#endif /* VALUBENCH_SYSINFO_H */
