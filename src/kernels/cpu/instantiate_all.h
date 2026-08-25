/*
 * instantiate_all.h -- emit every kernel for the ISA including this file.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * An ISA translation unit defines its OPS_* (32-bit) and OPS64_* (64-bit)
 * operation sets, then includes this once:
 *
 *     #define VB_ISA avx2
 *     #include "instantiate_all.h"
 *
 * and gets all algorithms at all stream counts. Previously each ISA file
 * carried two dozen near-identical stanzas; the only thing that varied between
 * them was the ISA token.
 *
 * The stanzas are written out rather than looped because #include cannot be
 * produced by macro expansion, and each instantiation needs a fresh include of
 * its template (the template #undef's its own macros on the way out, which is
 * what allows repeated inclusion). Adding an algorithm adds a block here and a
 * line in matrix.h -- and touches no ISA file.
 */

#ifndef VB_ISA
#  error "define VB_ISA before including instantiate_all.h"
#endif

#include "matrix.h"

/* ---- MD5 ---------------------------------------------------------------- */

#define MD5K_NAME    VB_KSYM(md5, VB_ISA, 1)
#define MD5K_STREAMS 1
#include "instantiate_md5.h"

#define MD5K_NAME    VB_KSYM(md5, VB_ISA, 2)
#define MD5K_STREAMS 2
#include "instantiate_md5.h"

#define MD5K_NAME    VB_KSYM(md5, VB_ISA, 3)
#define MD5K_STREAMS 3
#include "instantiate_md5.h"

#define MD5K_NAME    VB_KSYM(md5, VB_ISA, 4)
#define MD5K_STREAMS 4
#include "instantiate_md5.h"

#define MD5K_NAME    VB_KSYM(md5, VB_ISA, 6)
#define MD5K_STREAMS 6
#include "instantiate_md5.h"

#define MD5K_NAME    VB_KSYM(md5, VB_ISA, 8)
#define MD5K_STREAMS 8
#include "instantiate_md5.h"

/* ---- SHA-1 -------------------------------------------------------------- */

#define S1K_NAME    VB_KSYM(sha1, VB_ISA, 1)
#define S1K_STREAMS 1
#include "instantiate_sha1.h"

#define S1K_NAME    VB_KSYM(sha1, VB_ISA, 2)
#define S1K_STREAMS 2
#include "instantiate_sha1.h"

#define S1K_NAME    VB_KSYM(sha1, VB_ISA, 3)
#define S1K_STREAMS 3
#include "instantiate_sha1.h"

#define S1K_NAME    VB_KSYM(sha1, VB_ISA, 4)
#define S1K_STREAMS 4
#include "instantiate_sha1.h"

#define S1K_NAME    VB_KSYM(sha1, VB_ISA, 6)
#define S1K_STREAMS 6
#include "instantiate_sha1.h"

#define S1K_NAME    VB_KSYM(sha1, VB_ISA, 8)
#define S1K_STREAMS 8
#include "instantiate_sha1.h"

/* ---- SHA-512 (64-bit operation set) ------------------------------------- */

#define S5K_NAME    VB_KSYM(sha512, VB_ISA, 1)
#define S5K_STREAMS 1
#include "instantiate_sha512.h"

#define S5K_NAME    VB_KSYM(sha512, VB_ISA, 2)
#define S5K_STREAMS 2
#include "instantiate_sha512.h"

#define S5K_NAME    VB_KSYM(sha512, VB_ISA, 3)
#define S5K_STREAMS 3
#include "instantiate_sha512.h"

#define S5K_NAME    VB_KSYM(sha512, VB_ISA, 4)
#define S5K_STREAMS 4
#include "instantiate_sha512.h"

#define S5K_NAME    VB_KSYM(sha512, VB_ISA, 6)
#define S5K_STREAMS 6
#include "instantiate_sha512.h"

#define S5K_NAME    VB_KSYM(sha512, VB_ISA, 8)
#define S5K_STREAMS 8
#include "instantiate_sha512.h"
