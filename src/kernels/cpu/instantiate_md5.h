/*
 * kernel_instantiate.h -- map a translation unit's OPS_* vector macros onto the
 * kernel template and emit one kernel.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * Usage, after defining the OPS_* set once per ISA:
 *
 *     #define MD5K_NAME    vb_md5_avx2_s3
 *     #define MD5K_STREAMS 3
 *     #include "kernel_instantiate.h"
 *
 * md5_kernel_impl.h undefines every MD5K_* macro on the way out, which is what
 * lets this header be included repeatedly. The OPS_* macros survive, so an ISA
 * defines its vector operations once and instantiates as many stream counts as
 * it wants.
 */

#ifndef MD5K_NAME
#  error "define MD5K_NAME before including kernel_instantiate.h"
#endif
#ifndef MD5K_STREAMS
#  error "define MD5K_STREAMS before including kernel_instantiate.h"
#endif

#define MD5K_VEC    OPS_VEC
#define MD5K_LANES  OPS_LANES
#define MD5K_SET1   OPS_SET1
#define MD5K_ADD    OPS_ADD
#define MD5K_XOR    OPS_XOR
#define MD5K_ROTL   OPS_ROTL
#define MD5K_STORE  OPS_STORE
#define MD5K_LOAD   OPS_LOAD

/* Optional ISA capabilities. An ISA that has a three-way XOR, or an
   xor-fused-with-rotate, says so; the template falls back to composing them
   from two-input operations when it does not. */
#ifdef OPS_XOR3
#  define MD5K_XOR3 OPS_XOR3
#endif
#ifdef OPS_XORROT
#  define MD5K_XORROT OPS_XORROT
#endif
#define MD5K_F      OPS_F
#define MD5K_G      OPS_G
#define MD5K_H      OPS_H
#define MD5K_I      OPS_I

/* An ISA whose vector type cannot be arrayed supplies hooks here; see
   sve.c. Nothing else defines this. */
#ifdef VB_MD5_HOOKS
#  include VB_MD5_HOOKS
#endif

#include "md5_kernel_impl.h"
