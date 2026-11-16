# Effects, capture cost and memory ownership, 17 November 2026

The investigation froze the client `northlight-renderer.log` at task entry as
`.local/out/effects-audit-20261117/gameplay-before.log`. It identifies runtime
source `ff9ad181f82b`, DXVK 3.1.1, 3840x2160 and MSAA 2. Later client sessions
may rotate the live filename; they are not the baseline for this comparison.
The input hash, exploratory summaries and component comparisons are in the
same private output folder. The production source under review started at
`b86cca40`; the log alone cannot establish its complete build equivalence.

## What is expensive

Late-session renderer frames 30000–37186 contain 46 CPU/GPU frame samples with
effects enabled and capture not skipped. The median CPU capture is 1.822 ms,
effect submission 1.921 ms and sampled cleanup 0.704 ms. Capture p95 is 4.182 ms.
The median GPU effect interval is 3.257 ms; model-normal replay is another
0.818 ms outside that interval, with p95 1.861 ms. These are command-stream
intervals, not isolated shader execution times. CPU and GPU overlap.

Joining effect profiles to the same enabled/capture-active CPU frame identities
gives these approximate late-session medians:

| GPU effect | Median ms |
|---|---:|
| Full-resolution world composition | 0.605 |
| AO | 0.354 |
| AO blur | 0.233 |
| Temporal lighting, including merged smoothing | 0.308 |
| GI shading | 0.255 |
| Fog march | 0.178 |
| Directional shadow filtering | 0.151 |

These are independent stage medians and must not be summed. Movement, settings,
work admission and native draw counts vary across the log. The 41 complete,
focused late frame-interval windows have median mean frame time 11.432 ms;
this is an observational baseline, not an effects-off/on experiment.

The last expensive sample, frame 37084, reports capture 4.587 ms, effects
submission 2.411 ms and GPU effects 3.593 ms. It also has 35 count-pressure
events in the material native-cache lane. Other frames have no such pressure;
the cache change below does not claim to fix every capture cost.

## RAM and GPU resources

The latest resource inventory in the frozen log describes logical D3D9 sizes:

| Owner | Observed size | Placement and requirement |
|---|---:|---|
| World-owned geometry | 149.687 MiB | Mostly DEFAULT GPU buffers; CPU upload payload is already discarded after commit. |
| World shadow maps | 129.012 MiB | GPU render targets. |
| World post targets | 268.945 MiB | GPU images, including simultaneous current/history/scratch lifetimes. |
| Probe textures | 17.397 MiB | MANAGED shader inputs; compatible textures survive Reset. |
| Static caster / native replay / live terrain caches | 24.851 / 90.081 / 16.000 MiB | Separate DEFAULT ledgers, excluded from the world-owned resource inventory. |
| GI worker reachable CPU allocations | 109.817 MiB | Includes 56.253 MiB texels, geometry, acceleration and probes. CPU ray tracing reads these. |
| Capture source CPU allocations | 121.445 MiB | Shared immutable vertex/index snapshots used for bounds, poses, workers and replay fallback. |
| Capture source identities | 6.768 MiB | Keys and validity metadata, reported separately from source payload. |

World-owned resources total 553.644 MiB DEFAULT and 17.585 MiB MANAGED. These
are not resident-VRAM measurements. Driver mappings, allocation padding,
renaming, HDR/AO/material owners outside this inventory and the client itself
are not fully represented. CPU scopes share generations/owners and must not
be added indiscriminately. The final process sample has about 994 MiB of free
x86 virtual address space, with a largest free region of 832 MiB.

Moving CPU GI texels/BVH or capture sources into DEFAULT resources would remove
inputs still read by CPU consumers. It requires changing those consumers,
not changing a pool flag. Moving the small MANAGED probe atlas also needs a
replacement upload/Reset contract: the recent capture recovery depends on
retaining that allocation under x86 address-space pressure. Do not undo that
recovery in pursuit of an unmeasured RAM saving.

## Implemented: avoid resident scans under count pressure

`NativeStore::room` previously searched the full LRU for every metadata-only
victim. A prefix of thousands of resident GPU meshes was traversed repeatedly
even when thousands of probation entries were available.

A second intrusive list now contains only nonresident entries, in exactly the
same LRU order. Pending uploads remain in this subset until successful commit;
failure leaves their original position intact. Promotion removes an entry,
touch moves it, and eviction/reset unlinks it. If no eligible probation entry
exists, the original first eligible full-LRU victim remains the fallback.
Byte pressure, frame protection, frame rewind, upload admission, resource
identity, lane budgets and native draw contents keep their existing contracts.

The additional two pointers cost at most 64 KiB on the production x86 build
at 8192 entries and are charged to the existing metadata budget. No cache
budget, shader, resolution or visual setting is increased or reduced.

The saturated CPU fixture keeps 4096 resident meshes and admits 4096 new
probation identities over 64 frames. Eight alternating old/new runs, excluding
the first warmup pair, measured these O2 medians:

| Consumer | Previous ms | Current ms |
|---|---:|---:|
| Materials | 84.616 | 38.917 |
| Shadows | 87.164 | 37.857 |

The timed interval includes small source construction and admission, not just
list traversal. Both versions produce the same membership digest and resource
creation count, retain all resident meshes during the timed phase, and evict
the same probation prefix. This is a CPU component result, not a 2x improvement
of the entire geometry capture or game FPS.

## How to pursue a multiple-fold capture improvement

1. **Track writes by source range.** Snapshot keys currently use one revision
   per entire VB/IB. Position bounds at frame 37084 show 112 revision scans,
   zero revision hits and 1,183,032 position bytes read; snapshot replay-content
   comparison also finds 26 of 26 changed-generation captures byte-identical.
   This establishes avoidable revalidation work, but not how many writes are
   outside each requested range. Measure overlap first. A bounded write journal
   can preserve a draw-range token across disjoint writes. Overlap, DISCARD,
   ProcessVertices, unknown writes, failed Unlock, outstanding locks and history
   overflow must invalidate conservatively. Use the same proof for initial
   snapshot lookup and the final stability check. A geometry-only key is unsafe.
2. **Remove model-normal replay under MSAA.** Native normal MRT already exists,
   but the frozen log reports `reason=target-msaa` and falls back to replay.
   A matching-sample auxiliary target plus a validated resolve could remove
   that second vertex/skin/raster submission for supported draws. It needs
   per-sample coverage/depth, transparent occluder, unsupported-shader rollback,
   reset and resolved-normal image checks. Its extra 4K/MSAA target has a real
   memory cost. Do not simply enable the non-MSAA path or turn off MSAA.
3. **Reuse immutable GPU geometry while updating only draw inputs.** This is
   already implemented across material/shadow consumers. Extend it only where
   source-range evidence proves new hits. Animation still requires current
   palettes/constants and valid pose bounds. CPU GI actors need a bounded CPU
   geometry path even if render-only consumers stop copying vertices.

The first two changes can remove entire repeated operations, unlike speeding
up a loop. Their whole-capture multiplier is not yet measured. A matching live
route with current source, unchanged quality, camera/animation and complete
capture-inclusive CPU/GPU timings remains the acceptance criterion.

## Validation

`test_canonical_gpu_scene.py` exercises O2 and ASan/UBSan, exact stock mesh bytes,
identity collisions, rehash, both consumers, ownership transfer, frame rewind,
partial creation/lock/unlock failure, retries, current-frame protection and
the saturated membership check. It is now part of the architecture suite.
The pressure-only fixture can be rerun as `<test-output>/O2.exe pressure`.

`test_canonical_gpu_scene_gpu.py` passed native DXVK image comparisons without
MSAA and at MSAA 4/8, including held views, shared-owner transfers and aggregate
pressure. This run used the script's explicit `--compatible-compiler` option
for its two small authored fixture shaders because the locally installed
compiler binary does not match the pinned SHA-256. No production shader was
recompiled or freshness check relaxed. Background clients were allowed for
image correctness only; this run makes no GPU timing claim.

Production compilation uses `python scripts/build_renderer.py`, Zig 0.15.2,
the normal shader-freshness checks and the matching resource bundle. Acceptance
reports must be checked against the final tree; earlier exploratory reports
are not a substitute for that check.
