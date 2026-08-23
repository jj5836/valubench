/*
 * registry.c -- the table of available kernels.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * Every (algorithm, ISA, stream count) triple is an entry, and all of them are
 * expanded from the table in src/kernels/cpu/matrix.h rather than written out
 * here.
 * There is nothing to keep in sync: a kernel that exists in the matrix is
 * declared and registered automatically, and one that does not cannot be
 * half-added.
 *
 * The harness measures the available entries for the selected algorithm and
 * picks a winner by measurement rather than assumption -- the optimum moves
 * with microarchitecture, and guessing wrong costs more than the margin
 * the project calls significant.
 */

#include "valubench.h"
#include "cpu_features.h"
#include "opencl.h"
#include "kernels/cpu/matrix.h"

#include <stddef.h>

static int always(void) { return 1; }

/*
 * A device kernel is available if OpenCL loads and reports at least one device.
 * Both are normal to fail -- a machine with no GPU simply has no such kernels.
 */
static int have_opencl(void)
{
    static int cached = -1;
    if (cached < 0) {
        vb_ocl_device d[VB_OCL_MAX_DEVICES];
        cached = vb_ocl_devices(d, VB_OCL_MAX_DEVICES) > 0;
    }
    return cached;
}

/* ---- forward declarations, one per matrix cell -------------------------- */

#define VB_DECL_KERNEL(alg, isa, st, isaname, algid, avail, lanes)          \
    void VB_KSYM(alg, isa, st)(const void *corpus, uint64_t n_groups,       \
                               uint32_t blocks, uint32_t iterations,        \
                               uint64_t checksum[VB_MAX_DIGEST_WORDS]);

VB_FOR_EACH_KERNEL(VB_DECL_KERNEL)

/* ---- the table ---------------------------------------------------------- */

#define VB_ROW_KERNEL(alg, isa, st, isaname, algid, avail, lanes)           \
    { VB_KNAME(alg, isa, st), isaname, algid, lanes, st,                    \
      VB_KSYM(alg, isa, st), avail, 0 },

static const vb_kernel kernels[] = {
    VB_FOR_EACH_KERNEL(VB_ROW_KERNEL)

    /*
     * Device rows, expanded from the same list src/opencl/backend.c builds its
     * program table from, so the two cannot disagree.
     */
#define VB_DEV_ROW(alg, ALG, algid, entry, st)                              \
    { VB_KNAME(alg, ocl, st), "OpenCL", algid, VB_OCL_LANES, st,            \
      NULL, have_opencl, 1 },
#define VB_DEV_ALG(alg, ALG, algid, entry)      \
    VB_DEV_ROW(alg, ALG, algid, entry, 1)       \
    VB_DEV_ROW(alg, ALG, algid, entry, 2)       \
    VB_DEV_ROW(alg, ALG, algid, entry, 3)       \
    VB_DEV_ROW(alg, ALG, algid, entry, 4)

    VB_FOR_EACH_DEVICE_ALG(VB_DEV_ALG)

#undef VB_DEV_ALG
#undef VB_DEV_ROW
};

const vb_kernel *vb_kernels(size_t *count)
{
    *count = sizeof kernels / sizeof kernels[0];
    return kernels;
}
