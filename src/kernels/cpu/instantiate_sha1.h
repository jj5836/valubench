/*
 * instantiate_sha1.h -- map a translation unit's OPS_* macros onto the SHA-1
 * kernel template and emit one kernel.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * Usage, after defining the 32-bit OPS_* set once per ISA:
 *
 *     #define S1K_NAME    vb_sha1_avx2_s3
 *     #define S1K_STREAMS 3
 *     #include "instantiate_sha1.h"
 */

#ifndef S1K_NAME
#  error "define S1K_NAME before including instantiate_sha1.h"
#endif
#ifndef S1K_STREAMS
#  error "define S1K_STREAMS before including instantiate_sha1.h"
#endif

#define S1K_VEC    OPS_VEC
#define S1K_LANES  OPS_LANES
#define S1K_SET1   OPS_SET1
#define S1K_ADD    OPS_ADD
#define S1K_XOR    OPS_XOR
#define S1K_ROTL   OPS_ROTL
#define S1K_STORE  OPS_STORE
#define S1K_LOAD   OPS_LOAD

/* Optional ISA capabilities. An ISA that has a three-way XOR, or an
   xor-fused-with-rotate, says so; the template falls back to composing them
   from two-input operations when it does not. */
#ifdef OPS_XOR3
#  define S1K_XOR3 OPS_XOR3
#endif
#ifdef OPS_XORROT
#  define S1K_XORROT OPS_XORROT
#endif
#define S1K_F_CH   OPS_CH
#define S1K_F_MAJ  OPS_MAJ

/* An ISA whose vector type cannot be arrayed supplies hooks here; see
   sve.c. Nothing else defines this. */
#ifdef VB_SHA1_HOOKS
#  include VB_SHA1_HOOKS
#endif

#include "sha1_kernel_impl.h"
