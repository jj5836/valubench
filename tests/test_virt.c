/*
 * test_virt.c -- virtual or bare metal, and knowing when you cannot tell.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, The valubench authors. See LICENSE.
 *
 * The strings here are the ones this project's own captures recorded, plus
 * the two AArch64 shapes that motivated a three-state answer. No single
 * machine has all of them, which is why the classification is a pure function
 * and this test exists at all.
 *
 * The case that matters most is the last group. On AArch64 the x86 hypervisor
 * CPUID bit has no equivalent, so `lscpu` reports no hypervisor on Graviton
 * and Grace alike -- machines that are certainly virtual. A boolean built on
 * that evidence would confidently say "bare metal" for every ARM figure in
 * this project. Unknown is the true answer and has to be sayable.
 */

#include "sysinfo.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;

static const char *name(int v)
{
    return v == VB_VIRT_YES ? "yes" : v == VB_VIRT_NO ? "no" : "unknown";
}

static void check(const char *what, const char *vendor, const char *product,
                  int flag, int want)
{
    int got = vb_classify_virt(vendor, product, flag);

    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL  %s: got %s, want %s\n", what, name(got), name(want));
    }
}

int main(void)
{
    /* Recorded by captures in this project. */
    check("lambda A10 host (QEMU)",   "QEMU",  "Standard PC (Q35 + ICH9, 2009)", 1, VB_VIRT_YES);
    check("lambda 2xH100 host",       "Bochs", "Bochs",                          1, VB_VIRT_YES);
    check("A100 host (KVM)",          "KVM",   "KVM Virtual Machine",            1, VB_VIRT_YES);
    check("development box",          "AZW",   "MINI S",                         0, VB_VIRT_NO);

    /* EC2 names itself identically either way; the instance type decides. */
    check("EC2 bare metal",     "Amazon EC2", "c7i.metal-24xl", 0, VB_VIRT_NO);
    check("EC2 virtual (x86)",  "Amazon EC2", "c8a.4xlarge",    1, VB_VIRT_YES);
    check("EC2 virtual (arm)",  "Amazon EC2", "c7g.4xlarge",   -1, VB_VIRT_YES);
    check("EC2, type unknown",  "Amazon EC2", "",              -1, VB_VIRT_UNKNOWN);

    /* Other hypervisors that name themselves. */
    check("VMware",     "VMware, Inc.",          "VMware7,1",  1, VB_VIRT_YES);
    check("Hyper-V",    "Microsoft Corporation", "Virtual Machine", 1, VB_VIRT_YES);
    check("VirtualBox", "innotek GmbH",          "VirtualBox", 1, VB_VIRT_YES);
    check("Xen",        "Xen",                   "HVM domU",   1, VB_VIRT_YES);

    /*
     * AArch64. The flag does not exist, so its absence proves nothing -- and
     * saying "no" here is the specific wrong answer this design avoids.
     */
    check("arm, no DMI at all",   "",       "",        -1, VB_VIRT_UNKNOWN);
    check("arm, unhelpful DMI",   "Default string", "Default string", -1, VB_VIRT_UNKNOWN);
    check("arm, DMI names a hv",  "QEMU",   "KVM Virtual Machine", -1, VB_VIRT_YES);

    /* Degenerate input must not crash or assert bare metal. */
    check("null vendor and product", NULL, NULL, -1, VB_VIRT_UNKNOWN);

    printf("%d virtualization checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
