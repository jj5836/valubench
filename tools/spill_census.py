#!/usr/bin/env python3
"""Count stack traffic per stream in the scalar kernels.

The question: what sets the best stream count on a given core?
Live state per stream orders the algorithms identically on x86-64 and AArch64 --
MD5 four, SHA-1 one to two, SHA-512 one -- but does not predict the peak across
machines. AArch64 has 31 general-purpose registers to x86-64's 16, yet
sha1/scalar peaks at s1 on Neoverse V1 and s2 on Zen 5, the opposite of what
register count implies.

If register pressure is the mechanism, spill traffic should rise where
throughput falls off. This counts it: every load and store whose address is
formed from the stack pointer or frame pointer inside a kernel body is a spill
or a reload, because the message words come from a pointer argument and the
digest goes out through another one -- nothing else legitimately lives on the
stack in these functions.
"""
import re, subprocess, sys, json

# x86-64 AT&T: an operand like 0x38(%rsp) or -0x20(%rbp).
# Instructions that can fold a memory operand into the arithmetic. Not
# exhaustive by design: these are the forms that appear in the round bodies.
X86_RMW = ('add', 'sub', 'and', 'or', 'xor', 'cmp', 'test',
           'adc', 'sbb', 'inc', 'dec', 'imul', 'lea', 'rol', 'ror')

X86_MEM = re.compile(r'-?0x[0-9a-f]+\((%rsp|%rbp)\)|\((%rsp|%rbp)\)')
# AArch64: [sp, #96] / [x29, #-16] / [sp]
ARM_MEM = re.compile(r'\[(sp|x29)(,|\])')

def disasm(objdump, obj, sym, size):
    out = subprocess.run([objdump, '-d', '--no-show-raw-insn', obj],
                         capture_output=True, text=True).stdout
    lines, on = [], False
    for ln in out.splitlines():
        m = re.match(r'^[0-9a-f]+ <(.+)>:$', ln)
        if m:
            on = (m.group(1) == sym)
            continue
        if on and re.match(r'^\s+[0-9a-f]+:', ln):
            lines.append(ln.split('\t', 1)[-1].strip() if '\t' in ln else ln.strip())
    return lines

def analyse(arch, lines):
    total = len(lines)
    ld = st = 0
    frame = 0
    for ln in lines:
        parts = ln.split(None, 1)
        if not parts:
            continue
        op, rest = parts[0], (parts[1] if len(parts) > 1 else '')
        rest_full = ln
        if arch == 'x86':
            # Frame size from the prologue's sub $N,%rsp.
            m = re.match(r'sub\s+\$(0x[0-9a-f]+),%rsp', ln)
            if m and not frame:
                frame = int(m.group(1), 16)
            if not X86_MEM.search(rest):
                continue
            # AT&T: destination last. Memory on the right is a store.
            src, _, dst = rest.rpartition(',')
            if op.startswith(('mov', 'movq', 'movl')):
                if X86_MEM.search(dst):
                    st += 1
                elif X86_MEM.search(src):
                    ld += 1
            elif op.startswith(X86_RMW):
                # A folded memory operand: `addl 0x18(%rsp),%eax` reads the
                # stack slot as part of the arithmetic, no separate mov. This
                # is the form x86 reaches for under register pressure, which
                # is exactly the case MD5 creates -- and counting only mov*
                # made those reloads invisible while AArch64, which has no
                # folded form and must emit an ldr, counted every one. The
                # comparison this tool exists to make was biased against
                # AArch64 by however much the folding saved.
                if X86_MEM.search(dst):
                    st += 1          # read-modify-write touches the slot twice
                    ld += 1
                elif X86_MEM.search(src):
                    ld += 1
        else:
            m = re.match(r'sub\s+sp, sp, #(0x[0-9a-f]+|\d+)', ln)
            if m and not frame:
                frame = int(m.group(1), 16) if m.group(1).startswith('0x') else int(m.group(1))
            if not ARM_MEM.search(rest):
                continue
            # Skip the ABI prologue and epilogue. AArch64 saves callee-saved
            # registers with stp/ldp through memory, where x86-64 uses
            # push/pop -- which carry no memory operand and so were never
            # counted on that side. Counting them here would have made every
            # ARM figure look worse by a fixed amount that has nothing to do
            # with register pressure in the round body.
            if re.match(r'(ld|st)p\s+(x(19|2[0-8])|x29), (x(19|2[0-8])|x30),', rest_full):
                continue
            if op.startswith(('ldr', 'ldp', 'ldur')):
                ld += 2 if op.startswith('ldp') else 1
            elif op.startswith(('str', 'stp', 'stur')):
                st += 2 if op.startswith('stp') else 1
    return dict(insns=total, loads=ld, stores=st, stack=ld + st, frame=frame)

def main():
    objs = [('x86', 'objdump', 'build/kernel_scalar.o'),
            ('arm', 'aarch64-linux-gnu-objdump', 'build-arm64/kernel_scalar.o')]
    res = {}
    for arch, od, obj in objs:
        for alg in ('md5', 'sha1', 'sha512'):
            for s in (1, 2, 3, 4):
                sym = 'vb_%s_scalar_s%d' % (alg, s)
                lines = disasm(od, obj, sym, 0)
                if not lines:
                    continue
                res[(arch, alg, s)] = analyse(arch, lines)
    print(json.dumps({'%s/%s/s%d' % k: v for k, v in res.items()}, indent=1))

main()
