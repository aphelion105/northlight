# Pipeline copies and declaration lookup, 17 November 2026

This follows [GPU height ownership](cpu-gpu-ownership-20261117.md) at `d66c6dc3`.
The latest available gameplay logs still predate that build. They establish
workload and earlier costs, not current FPS or this change's live contribution.
Artifacts and frozen-header comparisons are in the private output directory
`.local/out/pipeline-buffers-20261117/`.

| Pipeline boundary | Review and resulting action |
|---|---|
| Game writes and command-stream buffers | Existing write-range generations, sparse shadows and completion-owned slices remain necessary. No second shadow or larger allowance added. |
| Snapshot readback and CPU capture | New noncompacted vertex arrays were zero-filled, then overwritten. Reserve capacity first, assign source bytes directly and zero only the trailing stride padding. Apply the same rule to UP captures and raw IB scratch. |
| Declaration decoding and capture-layout lookup | Immutable decoded layouts already have a bounded cache. Its hit path also searched for an eviction victim and stepped through large records. Split compact keys from layout storage; use the existing intrusive LRU for constant-time victim selection. |
| Source/geometry identity | Existing source pool, range tokens and proven alias reuse already share immutable payload. Tracked snapshots already omit raw-index/copy-run revalidation copies. Keep that behavior. |
| Deferred material and normal packets | Packets retain constants, resource leases and geometry until their actual consumers finish. Existing pools reuse allocations; resident normal packets already drop CPU payload. No additional payload cache added. |
| Native GPU replay | Existing buffers and cross-consumer upload ownership remain shared. No new VB/IB, ring, target or shader introduced. |
| GI sources, BVH and probes | CPU readers still exist. Preserve their inputs and revision checks; moving storage alone would remove data from live consumers. |

Allocation stays outside backend locks. For buffered capture, `reserve` happens
before Lock, then trivial byte assignment and tail resize stay within that
capacity. This matters because the existing Reader destructor unpins CPU copies
but does not implicitly Unlock a real backend buffer on allocation failure.
Sparse remapping, index decoding, per-frame admission, byte charges, resource
references, revision checks and failed-Lock/Unlock behavior remain unchanged.
For reused storage that outgrows its capacity, keep the original `resize` growth
policy. Exact `reserve` on every increase caused 256 allocations instead of 16
in the two-stream growing-mesh fixture; that experiment was rejected. Cold
storage skips initialization, while reused storage keeps amortized growth.

The declaration cache retains exactly 128 COM owners with the same LRU order,
recent hit, decoded layouts and capture-layout validation. Failed decoding does
not evict an existing owner. The previous x86 object occupied 71,704 bytes;
the final intrusive-LRU variant uses 71,184 bytes. Links replace the
old timestamps in the fixed key array; no heap nodes or second key cache exist.

`test_pipeline_capture_buffers.py` runs the existing complete snapshot oracle,
64 allocation-failure checkpoints and a separate 50,000-operation declaration
LRU oracle at O2/O0. The latter uses distinct layouts, cached extents, failures,
reset and layout replacement after eviction. Raw output bytes, trailing padding,
native lock/reference balance and recovery are checked independently.
Growing-mesh checks enforce bounded allocations across 128 increasing captures;
the optional baseline comparison also covers increasing raw-index scratch.

With `--baseline-ref d66c6dc3 --benchmark`, complete frozen production headers
are built with identical Zig/O2 flags. Seven alternating median pairs follow a
warmup pair. Early uniform-layout measurements overstated warm-lookup gains.
The final distinct-layout fixture found about 1.6x for warm lookup over 32–128
declarations and 2.6x under 129-object churn with intrusive LRU. A `find_if`
experiment improved larger hits but worsened churn; it was rejected. Cold-copy
gains varied between about 4–19%; retained-buffer copies have little work to
remove. Allocation counts, decode counts and output digests match the baseline.
These are synthetic CPU components, not multipliers for whole capture or FPS.
Fresh final numbers and input hashes are recorded in the acceptance artifacts.

The next gameplay profile must determine declaration diversity, cold-copy
frequency and whether remaining admission/pose, deferred flush, effect submission
or GPU passes dominate. This patch does not reduce samples, resolution, draw
coverage, cache limits or profiling coverage.
