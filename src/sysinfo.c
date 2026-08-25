/*
 * sysinfo.c -- environment capture from /proc and /sys.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 */

#define _GNU_SOURCE

#include "sysinfo.h"
#include "cpu_features.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>

static int read_line_file(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    if (!fgets(out, (int) n, f)) {
        fclose(f);
        return 0;
    }
    fclose(f);

    out[strcspn(out, "\r\n")] = '\0';
    return 1;
}

static long read_long_file(const char *path)
{
    char buf[64];
    if (!read_line_file(path, buf, sizeof buf))
        return -1;
    return strtol(buf, NULL, 10);
}

static void collect_governor(vb_sysinfo *si)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    char first[32] = "";
    int mixed = 0;

    snprintf(si->governor, sizeof si->governor, "unknown");

    for (long i = 0; i < n; i++) {
        char path[128], gov[32];
        snprintf(path, sizeof path,
                 "/sys/devices/system/cpu/cpu%ld/cpufreq/scaling_governor", i);
        if (!read_line_file(path, gov, sizeof gov))
            continue;

        if (first[0] == '\0')
            snprintf(first, sizeof first, "%s", gov);
        else if (strcmp(first, gov) != 0)
            mixed = 1;
    }

    if (first[0] != '\0')
        snprintf(si->governor, sizeof si->governor, "%s",
                 mixed ? "mixed" : first);
}

static void collect_freq(vb_sysinfo *si)
{
    si->freq_khz_min = read_long_file(
        "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq");
    si->freq_khz_max = read_long_file(
        "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    si->freq_khz_now = read_long_file(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
}

static void collect_os(vb_sysinfo *si)
{
    struct utsname u;

    snprintf(si->kernel, sizeof si->kernel, "unknown");
    snprintf(si->os, sizeof si->os, "unknown");

    if (uname(&u) == 0)
        snprintf(si->kernel, sizeof si->kernel, "%s %s", u.sysname, u.release);

    FILE *f = fopen("/etc/os-release", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                char *p = line + 12;
                if (*p == '"')
                    p++;
                p[strcspn(p, "\"\r\n")] = '\0';
                snprintf(si->os, sizeof si->os, "%s", p);
                break;
            }
        }
        fclose(f);
    }
}

static void collect_compiler(vb_sysinfo *si)
{
#if defined(__clang__)
    snprintf(si->compiler, sizeof si->compiler, "clang %d.%d.%d",
             __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    snprintf(si->compiler, sizeof si->compiler, "gcc %d.%d.%d",
             __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    snprintf(si->compiler, sizeof si->compiler, "unknown");
#endif
}

void vb_sysinfo_collect(vb_sysinfo *si)
{
    memset(si, 0, sizeof *si);

    snprintf(si->cpu_brand, sizeof si->cpu_brand, "%s", vb_cpu_brand());
    si->cpus_online = (int) sysconf(_SC_NPROCESSORS_ONLN);

    char smt[8];
    si->smt_active = read_line_file("/sys/devices/system/cpu/smt/active",
                                    smt, sizeof smt)
                   ? atoi(smt) : -1;

    si->loadavg1 = -1.0;
    {
        double la[3];
        if (getloadavg(la, 3) >= 1)
            si->loadavg1 = la[0];
    }

    collect_governor(si);
    collect_freq(si);
    collect_os(si);
    collect_compiler(si);

    si->has_sse2     = vb_cpu_has_sse2();
    si->has_avx2     = vb_cpu_has_avx2();
    si->has_avx512f  = vb_cpu_has_avx512f();
}

const char *vb_sysinfo_warnings(const vb_sysinfo *si)
{
    static char buf[512];
    buf[0] = '\0';

    /*
     * These do not invalidate a result, but they widen its error bars, and a
     * 10%-significance bar is easily swamped by a powersave governor.
     * Report rather than silently tolerate (docs/research.md part 5).
     */
    if (strcmp(si->governor, "performance") != 0 &&
        strcmp(si->governor, "unknown") != 0) {
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf),
                 "CPU governor is '%s', not 'performance'; ", si->governor);
    }

    /*
     * Competing load is measured against core count because that is what
     * actually matters: 2.0 on a 4-core box means half the machine is already
     * busy and any multi-threaded result will scatter.
     */
    if (si->loadavg1 >= 0.0 && si->cpus_online > 0 &&
        si->loadavg1 > 0.25 * si->cpus_online) {
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf),
                 "system load average is %.2f on %d cores, so other work is "
                 "competing for the CPU; ", si->loadavg1, si->cpus_online);
    }

    if (si->smt_active == 1) {
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf),
                 "SMT is enabled, which increases run-to-run variance; ");
    }

    if (si->freq_khz_now > 0 && si->freq_khz_max > 0 &&
        si->freq_khz_now < si->freq_khz_max * 9 / 10) {
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf),
                 "CPU is running below 90%% of max frequency; ");
    }

    return buf[0] ? buf : NULL;
}
