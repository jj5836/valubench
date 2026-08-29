<!--
SPDX-License-Identifier: BSD-3-Clause
Copyright (c) 2026, The valubench authors. See LICENSE.
-->

# Findings

Claims that span more than one machine. Everything here is reasoning; the data
lives outside the repository, and each claim carries the query that produces it
from `results.db` (see [results.md](results.md)).

That separation is the point. An earlier `RESULTS.md` mixed the two: figures
were hand-transcribed beside the prose that interpreted them, two drifted into
disagreeing copies, and re-measuring meant rewriting essays. A claim here is
false the moment its query stops supporting it, and nothing has to be
transcribed for that to be checkable.

Single-machine results are not here — they belong in that capture's own
README. A claim earns a place in this file by needing more than one machine.

Rebuild the database first:

    tools/ingest.py ~/projects/valubench-results

---

## Interleaving independent streams is the largest single kernel win

MD5's 64 steps are one serial dependency chain, and SIMD does not break it: all
lanes of a vector advance in lockstep as a single chain. A one-stream kernel
measures dependency latency rather than throughput, and interleaving
independent chains is worth more than any instruction-set choice.

**Up to 5.4x**, and the best stream count is a property of the part and the
instruction set rather than a constant — which is why the kernels are generated
at every count instead of a chosen one.

```sql
SELECT part, isa,
       MAX(hashes_per_sec) / MIN(CASE WHEN streams=1 THEN hashes_per_sec END) AS gain
FROM m WHERE algorithm='md5' AND threads=1 AND status='ok' AND runs_on='cpu'
GROUP BY part, isa HAVING gain IS NOT NULL ORDER BY gain DESC;
```

Grace SVE2 5.44, Grace SVE 5.13, Grace NEON 4.60, Graviton4 SVE 4.28.

## The compiler outweighs the instruction set, by about 3x

The largest effect in this project is not architectural. On one part, one
instruction set, one source tree, four compilers span **4.15x**; the best
instruction-set choice anywhere in the same data is worth **1.20x**.

```sql
WITH best AS (SELECT part, isa, compiler, MAX(hashes_per_sec) h FROM m
  WHERE algorithm='md5' AND threads=1 AND status='ok' AND runs_on='cpu'
  GROUP BY part, isa, compiler)
SELECT part, isa, COUNT(*) compilers, MIN(h)/1e6 lo, MAX(h)/1e6 hi, MAX(h)/MIN(h) spread
FROM best GROUP BY part, isa HAVING compilers > 1 ORDER BY spread DESC;
```

Grace SVE2 4.15x, Grace SVE 3.86x, Graviton4 SVE2 3.54x — against NEON at 1.24x
and 1.29x on the same parts. **The spread tracks how recently the instruction
set arrived.** Settled paths agree between compilers; new ones do not, and gcc
13 and 14 are the low end of every SVE row.

A vector figure quoted without naming its toolchain therefore carries an error
bar wider than every architectural difference measured here.

## At equal width, NEON usually wins — Grace is the exception

Graviton4 and Grace are both Neoverse V2, with NEON, SVE and SVE2 all 128 bits,
so width gives SVE2 nothing and any difference is the instruction set alone.
SVE2 exceeds NEON in six rows out of the whole matrix, four of them Grace:

```sql
WITH b AS (SELECT part, compiler, algorithm, isa, MAX(hashes_per_sec) h FROM m
  WHERE threads=1 AND status='ok' AND isa IN ('NEON','SVE2') GROUP BY 1,2,3,4)
SELECT part, compiler, algorithm,
       MAX(CASE WHEN isa='SVE2' THEN h END) / MAX(CASE WHEN isa='NEON' THEN h END) ratio
FROM b GROUP BY part, compiler, algorithm HAVING ratio > 1 ORDER BY ratio DESC;
```

Grace reaches 1.05x on sha512 and 1.03x on md5; Graviton4 manages 1.01x and
1.00x, both on sha512 alone and both within noise of parity. The
same core family, the same compiler and the same source give opposite verdicts
on two implementations, which places the difference below the architecture.

## A fixed-function hash unit is not automatically faster

SHA-NI computes SHA-1 in dedicated hardware and is **slower than the integer
vector path on every machine in the database**, by between 2.2x and 4.8x:

```sql
SELECT part,
       MAX(CASE WHEN isa='SHA-NI' THEN hashes_per_sec END) /
       MAX(CASE WHEN isa IN ('AVX2','AVX512') THEN hashes_per_sec END) AS ratio
FROM m WHERE algorithm='sha1' AND threads=1 AND status='ok'
GROUP BY part HAVING ratio IS NOT NULL;
```

EPYC 9R45 0.46, Xeon 8358 0.22, Xeon 8480+ 0.21, Xeon 8488C 0.21. The unit
processes one message at a time while the vector path processes several, so
what the unit is worth depends entirely on the vector path beside it — and the
spread between 0.21 and 0.46 is that comparison changing, not the unit
changing.

## Compare per-core rates at full load, not scaling ratios

A scaling ratio divides an all-core figure by a single-thread baseline taken on
an idle machine, so it measures boost headroom as much as scaling. Two
compilers on Graviton4 reach the same per-core rate at sixteen threads while
their single-thread figures differ by 10%:

```sql
SELECT part, compiler,
       MAX(CASE WHEN threads=1  THEN hashes_per_sec END)/1e6    AS one_thread,
       MAX(CASE WHEN threads=16 THEN hashes_per_sec END)/16e6   AS per_core_at_16
FROM m WHERE kernel='sha1/neon-s2' AND status='ok'
GROUP BY part, compiler HAVING one_thread IS NOT NULL AND per_core_at_16 IS NOT NULL;
```

Graviton4 gives 27.27 → 24.68 under gcc 13 and 24.96 → 24.57 under gcc 15. The
per-core rates agree to 0.4%; the apparent scaling differs by 1.5x. **The
sustainable per-core rate is the comparable number**, and a "scaling deficit"
computed the other way is an artifact of the denominator.

## One device stream is enough; four is a mistake

On a single A100, at the compute plateau, one to three streams are
indistinguishable and four is clearly worse:

```sql
SELECT algorithm, streams, MAX(compressions_per_sec)/1e9 AS gcompress_s
FROM m WHERE capture='a100-sxm40-20260828' AND runs_on='device'
  AND transfer_mode='resident' AND iterations=16 AND status='ok'
GROUP BY algorithm, streams ORDER BY algorithm, streams;
```

md5 35.34 / 35.42 / 35.32 / 23.84 and sha1 15.66 / 15.95 / 15.68 / 14.11 across
s1–s4. SHA-512 is the exception and degrades from the first step, halving by
s4 — reproducing the A10 and both H100s. Note the 0.2% between md5's s1 and s2
is well inside the noise; this setup resolves about 3% and up.

## Relative algorithm cost is a property of the algorithms, not the hardware

Across seven parts, three vendors and two architectures, the ratios barely
move: MD5 is **1.7-2.3x** SHA-1 and **10-14x** SHA-512.

```sql
WITH b AS (SELECT part, algorithm, MAX(hashes_per_sec) h FROM m
  WHERE threads=1 AND status='ok' AND runs_on='cpu' GROUP BY part, algorithm)
SELECT part,
  MAX(CASE WHEN algorithm='md5'    THEN h END) / MAX(CASE WHEN algorithm='sha1'   THEN h END) AS md5_over_sha1,
  MAX(CASE WHEN algorithm='md5'    THEN h END) / MAX(CASE WHEN algorithm='sha512' THEN h END) AS md5_over_sha512
FROM b GROUP BY part;
```

SHA-1 costs more than MD5 because it runs 80 rounds and *expands* sixteen
message words to eighty rather than permuting them. SHA-512 costs more again:
80 rounds of 64-bit work with four sigma functions, at half the lanes per
vector.

**The ARM parts are consistently worse at SHA-512** — 11.95 to 13.84x against
9.95 to 11.0x on x86 — which is the 64-bit lane count showing through, since
AVX-512 gives SHA-512 eight lanes where NEON and 128-bit SVE2 give two.

## The checksum is invariant across every machine and instruction set

The property the whole verification story rests on, and it is queryable rather
than asserted:

```sql
SELECT COUNT(DISTINCT checksum) checksums, COUNT(DISTINCT part) parts,
       COUNT(DISTINCT isa) instruction_sets, COUNT(*) rows
FROM m WHERE workload='md5-full-55x1' AND working_set_kb=1008
  AND status='ok' AND verified=1;
```

**One** checksum, across 7 parts, 8 instruction sets and 584 measurements. Any
other answer means something computed a different thing.

---

## What is deliberately not here

- **Single-machine results.** They belong to their capture's README, where the
  conditions that produced them are recorded beside them.
- **Claims the database cannot support.** The strongest SHA-NI figure the
  project ever measured, 2.13x on a Gracemont N100, is absent because that
  machine has no capture — it is the development box. A number that cannot be
  re-derived is not a finding.
- **Claims that were true and are not.** The scalar baseline being accidentally
  vectorised, and the GPU stream conclusions drawn before a single-device A100
  removed the two-device confound, were both superseded. History is in git.
