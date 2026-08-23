/*
 * main.c -- command line entry point.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * One command, no configuration, a number. Everything is discovered or
 * autotuned; the flags exist so a result can be reproduced exactly, not so it
 * can be obtained at all (docs/research.md 1.5).
 */

#include "bench.h"
#include "valubench.h"
#include "report.h"
#include "sysinfo.h"
#include "opencl.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f, const char *argv0)
{
    fprintf(f,
"Usage: %s [options]\n"
"\n"
"  Integer SIMD microbenchmark. One hash is N chained full hashes of an\n"
"  L-byte message. Message length and iteration count are the two axes of\n"
"  the throughput surface; working set is the memory axis.\n"
"\n"
"Options:\n"
"  --algorithm NAME     md5 (default), sha1, or sha512\n"
"  --json               emit machine-readable JSON instead of a report.\n"
"                       With --list or --list-devices it describes the\n"
"                       binary rather than a result: algorithms, kernels,\n"
"                       limits, defaults, exit codes and devices.\n"
"  --list               list kernels and whether they run on this machine\n"
"  --list-devices       list OpenCL devices, or say why there are none\n"
"  --device LIST        OpenCL devices to use: an index, a comma-separated\n"
"                       list, or 'all' (the default). Several devices run\n"
"                       concurrently over slices of the same corpus.\n"
"  --kernel NAME        force a kernel (default: autotune)\n"
"  --where WHICH        restrict autotune to 'cpu' or 'device' kernels\n"
"                       ('any' is the default). A CPU baseline on a machine\n"
"                       with a GPU needs this: otherwise the device kernel\n"
"                       wins the probe and the result is not a CPU number.\n"
"  --threads N          worker threads (default: one per online CPU, %u here)\n"
"  --message-bytes L    message length (default %u, range %u..%u). Raises\n"
"                       both compute and bytes read per hash.\n"
"  --iterations N       chained hashes per hash (default 1, max %u). Raises\n"
"                       compute per hash without touching more memory.\n"
"                       Requires --message-bytes >= the digest size.\n"
"  --transfer MODE      how the corpus reaches an OpenCL device:\n"
"                       'resident' (default) uploads it once and launches\n"
"                       against it; 'stream' re-uploads before every launch,\n"
"                       putting the PCIe link inside the timed region. Use\n"
"                       stream with a large --working-set-kb to find where\n"
"                       compute overtakes transfer. No effect on CPU kernels.\n"
"  --working-set-kb K   target corpus size (default 1024). Sets how many\n"
"                       messages are hashed, so sweeping it walks the result\n"
"                       from L1-resident to DRAM-bound.\n"
"  --samples N          timed iterations (default 10, max %d)\n"
"  --time-ms N          wall time per iteration (default 100)\n"
"  --warmup-ms N        warm-up before timing (default 300)\n"
"  --no-pin             do not pin worker threads to cores\n"
"  --verbose            show autotune probes\n"
"  --version            print version\n"
"  -h, --help           this text\n"
"\n"
"Exit status: 0 success, 1 verification failure, 2 usage error,\n"
"             3 result too noisy to trust.\n",
            argv0, vb_online_cpus(),
            VB_DEFAULT_MSG_BYTES, VB_MIN_MSG_BYTES, VB_MAX_MSG_BYTES,
            VB_MAX_ITERS, VB_MAX_SAMPLES);
}

static void list_kernels(void)
{
    size_t count;
    const vb_kernel *ks = vb_kernels(&count);

    printf("%-16s %-8s %6s %8s %7s  %s\n",
           "NAME", "ISA", "LANES", "STREAMS", "WHERE", "AVAILABLE");
    for (size_t i = 0; i < count; i++) {
        printf("%-16s %-8s %6u %8u %7s  %s\n",
               ks[i].name, ks[i].isa, ks[i].lanes, ks[i].streams,
               ks[i].device ? "device" : "cpu",
               ks[i].available() ? "yes" : "no");
    }
}

/* "all", or a comma-separated list of indices. */
static int parse_devices(const char *spec, vb_config *cfg)
{
    cfg->device_count = 0;

    if (!strcmp(spec, "all"))
        return 0;                       /* 0 means every device */

    for (const char *p = spec; *p; ) {
        char *end;
        long v = strtol(p, &end, 10);

        if (end == p) {
            fprintf(stderr, "valubench: --device wants indices or 'all', "
                            "got '%s'\n", spec);
            return -1;
        }
        if (cfg->device_count >= VB_MAX_THREADS)
            break;
        cfg->device_index[cfg->device_count++] = (int) v;

        p = end;
        while (*p == ',' || *p == ' ')
            p++;
    }
    return 0;
}

static void list_devices(void)
{
    vb_ocl_device d[VB_OCL_MAX_DEVICES];
    int n = vb_ocl_devices(d, VB_OCL_MAX_DEVICES);

    if (n <= 0) {
        const char *why = vb_ocl_error();
        printf("No OpenCL devices.\n");
        if (why)
            printf("  %s\n", why);
        printf("  This is not an error: valubench runs CPU-only without them.\n");
        return;
    }

    for (int i = 0; i < n; i++) {
        printf("[%d] %s\n", i, d[i].name);
        printf("     vendor    %s\n", d[i].vendor);
        printf("     type      %s, %u compute units @ %u MHz\n",
               vb_ocl_type_name(d[i].type), d[i].compute_units, d[i].clock_mhz);
        printf("     memory    %llu MiB global, %llu MiB max allocation\n",
               (unsigned long long) (d[i].global_mem >> 20),
               (unsigned long long) (d[i].max_alloc >> 20));
        printf("     max wg    %zu\n", d[i].max_work_group);
        printf("     platform  %s (%s)\n", d[i].platform_name,
               d[i].platform_version);
        printf("     driver    %s, device %s\n", d[i].driver_version,
               d[i].device_version);
    }
}

static int need_arg(int i, int argc, const char *flag)
{
    if (i + 1 >= argc) {
        fprintf(stderr, "valubench: %s requires an argument\n", flag);
        return 0;
    }
    return 1;
}

/*
 * Parse an unsigned option argument, or fail.
 *
 * atoi() was used here and has no error return: a non-numeric argument becomes
 * 0 and a partly-numeric one is truncated, so `--threads abc` ran on one thread
 * and `--message-bytes 12x` measured twelve bytes. Neither was rejected, and the
 * JSON then recorded the substituted value as though it had been asked for --
 * which turns a typo in a sweep script into a result that looks deliberate.
 *
 * Rejects: empty strings, anything with trailing characters, negatives, and
 * values outside [lo, hi]. Leading whitespace is allowed because strtoul allows
 * it and a shell can introduce it.
 */
static int parse_uint(const char *flag, const char *arg,
                      unsigned long lo, unsigned long hi, unsigned *out)
{
    char *end = NULL;

    if (!arg || !*arg) {
        fprintf(stderr, "valubench: %s needs a number\n", flag);
        return 0;
    }
    /* strtoul happily wraps a negative into a huge unsigned; catch the sign
       before it can. */
    for (const char *p = arg; *p; p++) {
        if (*p == '-') {
            fprintf(stderr, "valubench: %s must not be negative (got '%s')\n",
                    flag, arg);
            return 0;
        }
        if (!isspace((unsigned char) *p))
            break;
    }

    errno = 0;
    unsigned long v = strtoul(arg, &end, 10);

    if (end == arg || (end && *end)) {
        fprintf(stderr, "valubench: %s wants a number, got '%s'\n", flag, arg);
        return 0;
    }
    if (errno == ERANGE || v < lo || v > hi) {
        fprintf(stderr, "valubench: %s must be between %lu and %lu (got '%s')\n",
                flag, lo, hi, arg);
        return 0;
    }
    *out = (unsigned) v;
    return 1;
}

/*
 * Listing is an action rather than a run, but it cannot happen while the
 * command line is still being read: `--list --json` and `--json --list` have to
 * mean the same thing, and they did not when --list returned from inside the
 * parse loop. So the loop records what was asked for and the dispatch happens
 * once everything has been seen.
 */
typedef enum {
    ACT_RUN = 0,
    ACT_LIST_KERNELS,
    ACT_LIST_DEVICES
} vb_action;

int main(int argc, char **argv)
{
    vb_config cfg;
    int as_json = 0, verbose = 0;
    vb_action action = ACT_RUN;

    vb_config_defaults(&cfg);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (!strcmp(a, "--json")) {
            as_json = 1;
        } else if (!strcmp(a, "--verbose")) {
            verbose = 1;
        } else if (!strcmp(a, "--no-pin")) {
            cfg.pin_cpu = 0;
        } else if (!strcmp(a, "--list")) {
            action = ACT_LIST_KERNELS;
        } else if (!strcmp(a, "--list-devices")) {
            action = ACT_LIST_DEVICES;
        } else if (!strcmp(a, "--device")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            if (parse_devices(argv[++i], &cfg) != 0) return VB_EXIT_USAGE;
        } else if (!strcmp(a, "--version")) {
            char wid[64];
            printf("valubench %s (workload %s)\n", VB_VERSION,
                   vb_workload_id(wid, sizeof wid, cfg.alg,
                                  cfg.message_bytes, cfg.iterations));
            return VB_EXIT_OK;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout, argv[0]);
            return VB_EXIT_OK;
        } else if (!strcmp(a, "--algorithm") || !strcmp(a, "--alg")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            cfg.alg = vb_algorithm_by_name(argv[++i]);
            if (!cfg.alg) {
                fprintf(stderr, "valubench: unknown algorithm '%s' "
                                "(md5, sha1, sha512)\n", argv[i]);
                return VB_EXIT_USAGE;
            }
        } else if (!strcmp(a, "--transfer")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            const char *m = argv[++i];
            if (!strcmp(m, "resident")) {
                cfg.transfer = VB_TRANSFER_RESIDENT;
            } else if (!strcmp(m, "stream")) {
                cfg.transfer = VB_TRANSFER_STREAM;
            } else {
                fprintf(stderr, "valubench: unknown transfer mode '%s' "
                                "(resident, stream)\n", m);
                return VB_EXIT_USAGE;
            }
        } else if (!strcmp(a, "--where")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            const char *w = argv[++i];
            if (!strcmp(w, "any")) {
                cfg.where = VB_WHERE_ANY;
            } else if (!strcmp(w, "cpu")) {
                cfg.where = VB_WHERE_CPU;
            } else if (!strcmp(w, "device")) {
                cfg.where = VB_WHERE_DEVICE;
            } else {
                fprintf(stderr, "valubench: unknown --where '%s' "
                                "(any, cpu, device)\n", w);
                return VB_EXIT_USAGE;
            }
        } else if (!strcmp(a, "--kernel")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            cfg.force_kernel = argv[++i];
        } else if (!strcmp(a, "--threads")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            if (!parse_uint(a, argv[++i], 1, VB_MAX_THREADS, &cfg.threads))
                return VB_EXIT_USAGE;
        } else if (!strcmp(a, "--iterations")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            if (!parse_uint(a, argv[++i], 1, 1u << 24, &cfg.iterations))
                return VB_EXIT_USAGE;
        } else if (!strcmp(a, "--message-bytes")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            if (!parse_uint(a, argv[++i], 1, 1u << 20, &cfg.message_bytes))
                return VB_EXIT_USAGE;
        } else if (!strcmp(a, "--working-set-kb")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            if (!parse_uint(a, argv[++i], 1, 1u << 24, &cfg.working_set_kb))
                return VB_EXIT_USAGE;
        } else if (!strcmp(a, "--samples")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            if (!parse_uint(a, argv[++i], 1, VB_MAX_SAMPLES, &cfg.n_samples))
                return VB_EXIT_USAGE;
        } else if (!strcmp(a, "--time-ms")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            if (!parse_uint(a, argv[++i], 1, 3600000, &cfg.target_ms))
                return VB_EXIT_USAGE;
        } else if (!strcmp(a, "--warmup-ms")) {
            if (!need_arg(i, argc, a)) return VB_EXIT_USAGE;
            if (!parse_uint(a, argv[++i], 0, 3600000, &cfg.warmup_ms))
                return VB_EXIT_USAGE;
        } else {
            fprintf(stderr, "valubench: unknown option '%s'\n", a);
            usage(stderr, argv[0]);
            return VB_EXIT_USAGE;
        }
    }

    /* Listing describes the binary, so it answers before the run parameters
       are validated -- asking what exists must work even with a nonsensical
       --iterations on the same line. */
    switch (action) {
    case ACT_LIST_KERNELS:
        if (as_json)
            vb_report_capabilities_json(stdout);
        else
            list_kernels();
        return VB_EXIT_OK;
    case ACT_LIST_DEVICES:
        if (as_json)
            vb_report_devices_json(stdout);
        else
            list_devices();
        return VB_EXIT_OK;
    case ACT_RUN:
        break;
    }

    if (cfg.n_samples < 1 || cfg.n_samples > VB_MAX_SAMPLES) {
        fprintf(stderr, "valubench: --samples must be 1..%d\n", VB_MAX_SAMPLES);
        return VB_EXIT_USAGE;
    }

    if (cfg.iterations < 1 || cfg.iterations > VB_MAX_ITERS) {
        fprintf(stderr, "valubench: --iterations must be 1..%u\n",
                VB_MAX_ITERS);
        return VB_EXIT_USAGE;
    }

    if (cfg.message_bytes < VB_MIN_MSG_BYTES ||
        cfg.message_bytes > VB_MAX_MSG_BYTES) {
        fprintf(stderr, "valubench: --message-bytes must be %u..%u\n",
                VB_MIN_MSG_BYTES, VB_MAX_MSG_BYTES);
        return VB_EXIT_USAGE;
    }

    /*
     * Iterated hashing overwrites the leading 16 bytes with the digest, which
     * needs a message at least that long. Refuse rather than silently changing
     * what is being measured.
     */
    if (cfg.iterations > 1 &&
        cfg.message_bytes < vb_alg_min_iter_bytes(cfg.alg)) {
        fprintf(stderr,
"valubench: --iterations > 1 with %s needs --message-bytes >= %u.\n"
"  Each iteration feeds the %u-byte digest back over the head of the\n"
"  message, so a shorter message has nowhere to put it.\n",
                cfg.alg->name, vb_alg_min_iter_bytes(cfg.alg),
                cfg.alg->digest_bytes);
        return VB_EXIT_USAGE;
    }

    if (cfg.working_set_kb < 1) {
        fprintf(stderr, "valubench: --working-set-kb must be >= 1\n");
        return VB_EXIT_USAGE;
    }

    if (cfg.threads > VB_MAX_THREADS) {
        fprintf(stderr, "valubench: --threads must be 0..%d\n", VB_MAX_THREADS);
        return VB_EXIT_USAGE;
    }

    vb_sysinfo si;
    vb_sysinfo_collect(&si);

    /* Autotune output is progress, not result: keep it off stdout in JSON mode
       so the JSON stays parseable without filtering. */
    const vb_kernel *k = NULL;

    if (cfg.force_kernel) {
        size_t count;
        const vb_kernel *ks = vb_kernels(&count);
        for (size_t i = 0; i < count; i++) {
            if (!strcmp(ks[i].name, cfg.force_kernel)) {
                k = &ks[i];
                break;
            }
        }
        if (!k) {
            fprintf(stderr, "valubench: no kernel named '%s' (try --list)\n",
                    cfg.force_kernel);
            return VB_EXIT_USAGE;
        }
        if (!k->available()) {
            fprintf(stderr,
                    "valubench: kernel '%s' needs %s, which this CPU lacks\n",
                    k->name, k->isa);
            return VB_EXIT_USAGE;
        }
        /* Naming a kernel and then excluding where it runs is a contradiction,
           and silently honouring one over the other would mislabel the result.
        */
        if ((cfg.where == VB_WHERE_CPU && k->device) ||
            (cfg.where == VB_WHERE_DEVICE && !k->device)) {
            fprintf(stderr, "valubench: --kernel '%s' runs on the %s, which "
                            "--where %s excludes\n", k->name,
                    k->device ? "device" : "cpu",
                    cfg.where == VB_WHERE_CPU ? "cpu" : "device");
            return VB_EXIT_USAGE;
        }
        /* Forcing a kernel also fixes the algorithm: they are not separable. */
        cfg.alg = vb_algorithm_by_id(k->alg);
    } else {
        k = vb_autotune(&cfg, verbose && !as_json);
        if (!k) {
            if (cfg.where != VB_WHERE_ANY) {
                fprintf(stderr,
                        "valubench: no %s kernel is available for %s on this "
                        "machine\n",
                        cfg.where == VB_WHERE_CPU ? "cpu" : "device",
                        cfg.alg->name);
                return VB_EXIT_USAGE;
            }
            fprintf(stderr,
                    "valubench: no kernel passed verification on this machine\n");
            return VB_EXIT_VERIFY_FAILED;
        }
    }

    vb_result r;
    if (vb_measure(k, &cfg, &r) != 0) {
        /* A device that could not be set up at all is a configuration problem,
           not the hardware computing wrong answers. Say which. */
        if (r.device_error[0] && !r.n_samples) {
            fprintf(stderr, "valubench: device kernel '%s' could not run:\n"
                            "  %s\n", k->name, r.device_error);
            return VB_EXIT_USAGE;
        }
        fprintf(stderr,
"valubench: VERIFICATION FAILED for kernel '%s'.\n"
"  The hardware did not compute correct MD5 digests. No performance number\n"
"  is reported, because a fast wrong answer is not a result. Causes worth\n"
"  checking: overclocking, marginal cooling, unstable memory, or a compiler\n"
"  bug. Run '%s --kernel scalar-s1 --threads 1' to test the portable path.\n",
                k->name, argv[0]);
        if (r.device_error[0])
            fprintf(stderr, "  device reported: %s\n", r.device_error);
        return VB_EXIT_VERIFY_FAILED;
    }

    if (as_json)
        vb_report_json(stdout, &r, &si, &cfg);
    else
        vb_report_human(stdout, &r, &si, &cfg);

    if (r.cov > cfg.cov_threshold)
        return VB_EXIT_NOISY;

    return VB_EXIT_OK;
}
