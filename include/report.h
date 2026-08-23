/*
 * report.h -- result rendering.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 */

#ifndef VALUBENCH_REPORT_H
#define VALUBENCH_REPORT_H

#include "bench.h"
#include "sysinfo.h"

#include <stdio.h>

void vb_report_json(FILE *f, const vb_result *r, const vb_sysinfo *si,
                    const vb_config *cfg);

void vb_report_human(FILE *f, const vb_result *r, const vb_sysinfo *si,
                     const vb_config *cfg);

/*
 * What this binary can do, on this machine: algorithms and their geometry,
 * every registered kernel and whether it runs here, the parameter limits, the
 * defaults, the exit codes, and the OpenCL devices. Emitted by `--list --json`.
 *
 * This is the machine-readable half of `--list`, and it exists so that tooling
 * asks the binary instead of carrying a transcribed copy of these facts that
 * drifts (see the header comment on the implementation).
 */
void vb_report_capabilities_json(FILE *f);

/* The OpenCL device list alone, for `--list-devices --json`. */
void vb_report_devices_json(FILE *f);

#endif /* VALUBENCH_REPORT_H */
