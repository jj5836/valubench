/*
 * sysinfo.h -- environment capture.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
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

    /*
     * Whether this is a virtual machine. Three states rather than a flag,
     * because on AArch64 there is frequently no way to tell: the x86
     * hypervisor CPUID bit has no equivalent, so its absence there is not
     * evidence of bare metal. Reporting "no" on that evidence would be a
     * confident wrong answer of exactly the kind this benchmark exists to
     * avoid, and every ARM figure in this project came from a machine whose
     * status it would have got wrong.
     *
     * The evidence is carried alongside the verdict so a reader can disagree
     * with it.
     */
    int  virtualized;               /* vb_virt */
    char sys_vendor[64];            /* DMI, "" if unreadable */
    char product_name[64];          /* DMI, "" if unreadable */
} vb_sysinfo;

typedef enum {
    VB_VIRT_UNKNOWN = 0,
    VB_VIRT_NO,
    VB_VIRT_YES
} vb_virt;

void vb_sysinfo_collect(vb_sysinfo *si);

/* Exposed for testing: the shapes worth checking are ones no single machine
   has. `hv_flag` is 1, 0, or -1 where the x86 hypervisor bit does not apply. */
int vb_classify_virt(const char *sys_vendor, const char *product_name,
                     int hv_flag);

/*
 * Environment conditions that make results less trustworthy, as a
 * newline-free human string, or NULL if nothing is worth warning about.
 * Returns a pointer to a static buffer.
 */
const char *vb_sysinfo_warnings(const vb_sysinfo *si);

#endif /* VALUBENCH_SYSINFO_H */
