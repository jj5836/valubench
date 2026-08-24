/*
 * sve_hooks.h -- hold per-stream state in named variables, not arrays.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * The three kernel templates keep every piece of per-stream state in arrays
 * indexed by stream. SVE types are sizeless, so those arrays do not compile:
 *
 *     error: array elements cannot have SVE type 'svuint32_t'
 *
 * Each template therefore routes its state through hooks that default to the
 * array form -- exactly the code the fixed-width ISAs have always compiled,
 * byte for byte -- and this file overrides them for SVE.
 *
 * The substitution is name pasting: A[k] becomes A0, wv[k][2] becomes wv0_2.
 * It works because the stream index is a literal at every use, since the
 * templates expand the streams rather than looping over them. Where the
 * template *did* loop -- over streams, or over the five or eight words of a
 * digest -- the loop is expanded here for the same reason.
 *
 * Two things deliberately keep their array form even here:
 *
 *   the schedule window, indexed by (t & 15), because t reaches the step as an
 *   arithmetic expression rather than a token and nothing can paste it. It is
 *   memory-backed on every ISA anyway -- sixteen vectors per stream fit no
 *   register file -- so SVE holds it as a plain scalar buffer, sized by the
 *   run-time lane count, and loads and stores through it;
 *
 *   the lane-fold scratch, which is sized by VB_MAX_LANES because the lane
 *   count is not a constant.
 */

#ifndef VALUBENCH_SVE_HOOKS_H
#define VALUBENCH_SVE_HOOKS_H

#include "valubench.h"

/* Two-level, so the argument is expanded before ## sees it. */
#define VB_SVE_V_(name, k)      name##k
#define VB_SVE_V(name, k)       VB_SVE_V_(name, k)
#define VB_SVE_V2_(name, k, i)  name##k##_##i
#define VB_SVE_V2(name, k, i)   VB_SVE_V2_(name, k, i)

#endif /* VALUBENCH_SVE_HOOKS_H */

/*
 * Below is re-included per instantiation rather than guarded, because each
 * template #undef's its own hooks on the way out. instantiate_all.h includes
 * this file once per algorithm, before the template.
 */

