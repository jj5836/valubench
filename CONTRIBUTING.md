# Contributing

## Building and testing

```bash
sudo apt install build-essential     # the whole requirement for the CPU paths
make                                 # or: make CC=clang
make check                           # known-answer vectors + every kernel
make config                          # what this toolchain can build
```

`make check` runs two suites: published test vectors (RFC 1321, FIPS 180-4)
against the scalar references, and every registered kernel against those
references across message sizes, block boundaries, lane counts and iteration
counts. Both must pass with zero failures before a change is worth reviewing.

## Rules that are not negotiable

These exist because breaking one produces a *plausible wrong number* rather than
an error, which is the failure mode this project is built to avoid.

- **No `-march=native`, ever.** It makes results depend on the machine that
  compiled the binary. Each ISA is a separate translation unit with its own `-m`
  flags, selected at run time by CPUID.
- **No `-flto`.** Link-time optimization could prove the message corpus constant
  and fold message words into the round constants — precisely the shortcut this
  workload exists to exclude.
- **Streams are expanded, never looped.** A loop that failed to unroll would
  collapse the interleaved dependency chains into one and under-report the
  hardware several-fold, silently. The same applies to step lists.
- **Round functions are written per ISA, not once generically.** AVX-512
  collapses each to a single `vpternlogd`; a shared "generic" form leaves that
  on the table, and an optimization that helps one ISA can hurt another.
- **Every kernel is validated against the scalar reference**, which is an
  independent implementation written from the specification. Agreement between
  them is evidence only because they share no code.
- **Measured numbers are tracked outside this repository**, and prose should
  characterise results qualitatively — "more than double the best SIMD path"
  survives a re-measurement; "2.13x" does not. A figure that genuinely carries
  an argument must name its machine, compiler and date, because none of those
  are recoverable later.

## Adding a kernel or an algorithm

[src/kernels/cpu/README.md](src/kernels/cpu/README.md) is the reference. Adding
an ISA is three steps; adding an algorithm is about ten and they are enumerated
there. The kernel matrix in `src/kernels/cpu/matrix.h` is the single source of
truth — registry rows and forward declarations are expanded from it, so
`src/registry.c` is never edited by hand.

## Verifying a change did what you think

Two techniques this project leans on, worth knowing:

- **Checksum invariance.** The XOR fingerprint is invariant to lanes, streams,
  threads and devices, so `md5-full-55x1` must produce `955e84cb…` before and
  after any change that is not supposed to alter results.
- **Disassembly comparison.** For refactors that should not change generated
  code — renames, macro restructuring — compare `objdump -d` before and after
  with symbol names normalised. This has caught a silently scalar-only build
  that compiled cleanly and ran.

## Testing a kernel for an architecture you do not have

The ARM kernels were written, compiled and validated on an x86 machine. The
recipe generalises to any target with a cross toolchain and a qemu-user build:

```bash
sudo apt install -y gcc-aarch64-linux-gnu qemu-user-static
export QEMU_LD_PREFIX=/usr/aarch64-linux-gnu     # where the ARM loader lives
make CC=aarch64-linux-gnu-gcc HOSTCC=cc BUILD=build-arm64 check
```

Three details make it work. `HOSTCC` keeps the build tools native, since
`embed_cl` has to run on the machine doing the building. `BUILD` keeps the
cross objects out of the native ones. And the architecture comes from
`$(CC) -dumpmachine` rather than `uname -m`, so the build configures for the
target rather than the host.

Emulated **throughput is meaningless** — do not record it. Emulated
**correctness is not**: the checksum is invariant across lanes, streams,
threads, devices *and architectures*, so a kernel that produces the same value
under qemu as on x86 is computing the right thing. That is the property worth
testing this way, and CI does it on every push.

## Measuring on rented hardware

`tools/run.sh` captures a whole session in one command — CPU phases, then
device phases if the machine has an OpenCL device. Two failures are worth pre-empting before either is worth running,
both learned the expensive way.

**Stop the machine patching itself.**

```bash
sudo systemctl disable --now unattended-upgrades
sudo systemctl disable --now apt-daily.timer apt-daily-upgrade.timer
```

A fresh Ubuntu image runs a background upgrader that can install a kernel and
reboot the instance out from under a run. Two instances did exactly that within
minutes of first login. An instance is rented for hours and then destroyed, so
it gains nothing from unattended patching and can lose a session to it.
`run.sh` records whether the service is live, because a machine that reboots
mid-run looks like a network fault from the other end.

**Do not poll the machine with bare TCP probes, and reuse one SSH connection.**

```
Host <instance>
    ControlMaster   auto
    ControlPath     /tmp/cm-%r@%h:%p     # must stay under 108 bytes
    ControlPersist  30m
```

Recent OpenSSH enables `PerSourcePenalties` by default: an address that
repeatedly opens connections and abandons them without authenticating is dropped
silently for an escalating period, up to about ten minutes. A loop of `nc -z` or
`/dev/tcp` reachability checks is exactly that pattern, and the symptom is
indistinguishable from a broken network — timeouts rather than refusals,
recovering on its own. The `ControlPath` limit is not advisory either: a Unix
socket path of 108 bytes or more fails at connection time, and temp directories
routinely exceed it.

**Energy needs the RAPL driver, and AWS kernels ship without it.** On bare
metal, `/sys/class/powercap` may not exist at all — the failure is silent, and
the energy phase simply reports no counter:

```bash
[ -d /sys/class/powercap ] || sudo apt install -y "linux-modules-extra-$(uname -r)"
sudo modprobe intel_rapl_msr
sudo chmod a+r /sys/class/powercap/*/energy_uj
```

Do it **before** starting a capture: the run decides once, during its
environment phase, whether energy is available, so loading the driver mid-run
does not help.

**Run detached, and copy results off as they land** rather than in one transfer
at the end. The script writes its log to disk before the terminal and tars the
output directory on exit, including on failure, so a session survives losing the
connection — but a capture you cannot retrieve is worth nothing.

## Style

C11, four-space indent, no tabs. `//` for single-line comments, `/* */` for
multi-line blocks. Comments explain *why*; the code already says what.

## AI-assisted contributions

**Allowed and welcome.** Much of this project was written with AI assistance and
the repository does not distinguish between contributions on that basis.

What is required is the same for everyone: the change passes `make check`, it
does not violate the rules above, and you understand it well enough to defend
the design in review. Generated code that nobody can explain is the problem, not
generated code as such.

If you use an assistant, [AGENTS.md](AGENTS.md) carries the project conventions
in a form it can load directly.
