/*
 * instantiate_sha512.h -- map a translation unit's OPS64_* macros onto the
 * SHA-512 kernel template and emit one kernel.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * Distinct from instantiate_sha1.h because SHA-512 needs the ISA's 64-bit
 * operation set, not its 32-bit one -- different lane count, different add,
 * different rotate.
 */

#ifndef S5K_NAME
#  error "define S5K_NAME before including instantiate_sha512.h"
#endif
#ifndef S5K_STREAMS
#  error "define S5K_STREAMS before including instantiate_sha512.h"
#endif

#define S5K_VEC    OPS64_VEC
#define S5K_LANES  OPS64_LANES
#define S5K_SET1   OPS64_SET1
#define S5K_ADD    OPS64_ADD
#define S5K_XOR    OPS64_XOR
#define S5K_ROTR   OPS64_ROTR
#define S5K_SHR    OPS64_SHR
#define S5K_STORE  OPS64_STORE
#define S5K_LOAD   OPS64_LOAD
#define S5K_F_CH   OPS64_CH
#define S5K_F_MAJ  OPS64_MAJ

#include "sha512_kernel_impl.h"
