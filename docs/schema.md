# Output schemas

valubench emits JSON so that nothing has to scrape human output. Four documents
are defined, each carrying a `schema` field naming itself and its version.

| schema | produced by | contents |
|---|---|---|
| `valubench/result/1` | `--json` | one measurement |
| `valubench/capabilities/1` | `--list --json` | what this binary can do here |
| `valubench/devices/1` | `--list-devices --json` | OpenCL devices, or why there are none |
| `valubench/comparison/1` | `tools/compare.py --json` | a diff of two result sets |

## The compatibility rule

**Adding a field is compatible. Removing or renaming one is not.** A consumer
should therefore match on the prefix and ignore fields it does not recognise:

```python
if not doc["schema"].startswith("valubench/result/"):
    raise ValueError("not a valubench result")
```

Both bundled tools do exactly that — see `Capabilities.query` in
[../tools/sweep.py](../tools/sweep.py) and `load_file` in
[../tools/compare.py](../tools/compare.py).

The version number moves when a field is removed or its meaning changes.

## `valubench/result/1`

| object | fields |
|---|---|
| `benchmark` | `name`, `version`, `algorithm`, `workload`, `workload_description`, `comparability` |
| `parameters` | `message_bytes`, `digest_bytes`, `block_bytes`, `blocks_per_message`, `iterations`, `threads`, `batch_messages`, `working_set_bytes` |
| `result` | `unit`, `direction`, `median`, `min`, `max`, `mean`, `stddev`, `cov_percent`, `stable`, `cov_threshold_percent`, `message_bytes_per_second`, `compressions_per_second`, `samples`, `total_hashes`, `total_seconds` |
| `verification` | `verified`, `checksum`, `method` |
| `kernel` | `name`, `isa`, `lanes`, `streams`, `selected_by`, `runs_on` |
| `device` | present only for device kernels: name, vendor, driver, launch geometry, `kernel_busy_fraction`, `transfer_mode`, and in streaming mode the per-pass transfer figures |
| `environment` | `cpu`, `cpus_online`, `smt_active`, `governor`, frequency fields, `loadavg_1min`, `kernel_version`, `os`, `compiler`, `isa_available`, `threads_used` |
| `energy` | `available`, and either `reason` or the joules/watts/`hashes_per_joule` figures with their `sources` |
| `warnings` | array of strings; conditions that make the number less trustworthy |

Three fields carry more weight than the rest:

**`benchmark.workload`** — e.g. `md5-full-55x1`. Any change to the work per hash
changes this string, so two results are comparable only when it matches. Both
tools refuse to compare across a difference. It contains no product name, so it
survived the rename and older captures still pair correctly.

**`verification.checksum`** — the XOR of every digest computed. Invariant to
lanes, streams, threads and devices, so for a given workload it is the same on
every machine that computes correctly. `compare.py` treats a mismatch as a hard
failure rather than a performance delta: it means one side computed something
else.

**`result.stable`** — false when `cov_percent` exceeds the threshold. The process
also exits 3 in that case. The number is still a real measurement; it just
should not carry an argument on its own.

### Exit codes

Reported in the capabilities document under `exit_codes`, so a driver need not
hard-code them: `0` success, `1` verification failure, `2` usage error, `3`
result too noisy to trust. Exit 3 still produces valid JSON on stdout.

## `valubench/capabilities/1`

`--list --json` describes the binary and the machine: `benchmark`, `limits`,
`defaults`, `exit_codes`, `transfer_modes`, `where_filters`, `algorithms`
(each with digest and block geometry and its minimum iteration message length),
`kernels` (each with isa, lanes, streams, `where`, and `available` on this
machine), and the `opencl` object.

It exists so that tooling asks rather than assumes. Every fact in it was once
transcribed into `sweep.py`, and the transcription drifted — the per-algorithm
minimum message length was carried as a single constant, which silently skipped
every SHA-512 point with more than one iteration.

Unavailable kernels are listed too, with `available: false`. "This build has
AVX-512 kernels but this CPU cannot run them" is a different situation from
"this build has none", and a sweep should be able to tell them apart.

## CSV from `tools/sweep.py`

One row per measured point, in the column order of `CSV_COLUMNS` in
[../tools/sweep.py](../tools/sweep.py). It is not versioned; `--resume` refuses
to append to a file whose header differs rather than mixing two shapes.

Three columns are worth knowing:

**`point_id`** encodes what was *requested* — algorithm, kernel, where,
transfer, message bytes, iterations, working set, threads. `--resume` matches on
it. It exists because requests cannot be recovered from results: `transfer_mode`
comes back empty for CPU kernels whatever was asked for, and `kernel` comes back
as autotune's choice where the request was "pick one".

**`working_set_kb`** is what was actually used, not what was asked for. The
batch has a floor of `VB_BATCH_LCM` messages, so several small requests round
onto the same corpus — which the sweep warns about, and which is why the
requested value lives in `point_id` instead.

**The device columns are empty on CPU rows**, not missing: `device`,
`transfer_mode`, `kernel_busy_pct`, `transfer_busy_pct`,
`transfer_gbytes_per_sec`, `compute_transfer_ratio` and `bound_by` all describe
a host-to-device transfer, and a CPU kernel reads the corpus out of the memory
it is already running in. The CSV always carries the columns so that CPU and
device sweeps concatenate; the human table drops any column that no row filled,
so a CPU-only sweep does not print two permanently blank ones.
