/*
 * shani.c -- SHA-1 on the x86 SHA extensions.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * The exception to the pattern every other file in this directory follows.
 *
 * The others describe an ISA as a set of OPS_* macros -- add, xor, rotate,
 * three-input boolean -- and hand them to an algorithm template that is written
 * once and knows nothing about the hardware. That works because those ISAs
 * differ only in how wide and how expressive their general vector ALU is; the
 * algorithm is the same either way.
 *
 * SHA-NI is not a wider ALU. It is SHA-1 itself, in silicon: SHA1RNDS4 performs
 * four real rounds, SHA1MSG1/SHA1MSG2 perform the message expansion. There are
 * no primitive operations to hand to a template, because the template's whole
 * job has been absorbed into the instructions. So this ISA brings its own
 * kernel, sha1_ni_kernel_impl.h, and supplies exactly one algorithm -- there is
 * no MD5 or SHA-512 to be had here at any price, which is precisely
 * design.md finding 1 stated as a build constraint.
 *
 * Consequently it also does not go through instantiate_all.h (which emits all
 * three algorithms), and its matrix.h block names one algorithm directly rather
 * than using VB_FOR_ALGS.
 *
 * -msha is the only flag this needs. The kernel deliberately avoids SSE4.1's
 * PBLENDW so nothing beyond the x86-64 SSE2 baseline is assumed alongside it.
 */

#include "matrix.h"

#define S1NI_NAME    VB_KSYM(sha1, shani, 1)
#define S1NI_STREAMS 1
#include "sha1_ni_kernel_impl.h"

#define S1NI_NAME    VB_KSYM(sha1, shani, 2)
#define S1NI_STREAMS 2
#include "sha1_ni_kernel_impl.h"

#define S1NI_NAME    VB_KSYM(sha1, shani, 3)
#define S1NI_STREAMS 3
#include "sha1_ni_kernel_impl.h"

#define S1NI_NAME    VB_KSYM(sha1, shani, 4)
#define S1NI_STREAMS 4
#include "sha1_ni_kernel_impl.h"
