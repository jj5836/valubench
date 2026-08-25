/*
 * fail_pthread_create.c -- make one pthread_create() fail, on purpose.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * LD_PRELOAD this and set VB_FAIL_CREATE=N to make the Nth call fail with
 * EAGAIN, one-based. Every other call is forwarded.
 *
 * The point is the *non-contiguous* failure. A test that exhausts the thread
 * limit gets a contiguous prefix of successes, which the old code happened to
 * handle; the defect only appears when thread 1 fails and thread 2 starts, and
 * nothing but an interposer produces that.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static unsigned calls;

int pthread_create(pthread_t *t, const pthread_attr_t *a,
                   void *(*fn)(void *), void *arg)
{
    typedef int (*create_fn)(pthread_t *, const pthread_attr_t *,
                             void *(*)(void *), void *);
    static create_fn real;
    if (!real) {
        /* ISO C forbids assigning void* to a function pointer directly; the
           copy is the portable spelling POSIX itself recommends. */
        void *sym = dlsym(RTLD_NEXT, "pthread_create");
        memcpy(&real, &sym, sizeof real);
    }

    const char *want = getenv("VB_FAIL_CREATE");
    unsigned n = ++calls;

    if (want && n == (unsigned) atoi(want))
        return EAGAIN;

    return real(t, a, fn, arg);
}
