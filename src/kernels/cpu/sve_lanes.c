/*
 * sve_lanes.c -- the run-time vector length, in the units the registry wants.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * One tiny translation unit of its own because it is the only code outside the
 * SVE kernels that needs arm_sve.h, and registry.c must not be compiled with
 * -march=armv8-a+sve: it is the dispatcher, and an ISA that leaks into the
 * dispatcher faults before the runtime check that was supposed to prevent it.
 *
 * svcntw and svcntb are not instructions that require SVE data registers, but
 * they still need the feature enabled at compile time, hence the separate file
 * and its own KFLAGS entry.
 */

#include "cpu_features.h"

#if VB_HAVE_SVE

#include <arm_sve.h>

unsigned vb_sve_lanes32(void) { return (unsigned) svcntw(); }
unsigned vb_sve_lanes64(void) { return (unsigned) svcntd(); }

#else

unsigned vb_sve_lanes32(void) { return 0; }
unsigned vb_sve_lanes64(void) { return 0; }

#endif
