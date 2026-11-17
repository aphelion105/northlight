# Geometry capture acceleration, 17 November 2026

This implements the three candidates investigated at `e2bd3760`: subdivision
reuse, current-pose certificates and same-draw capture handoff. Shaders, range,
resolution, sample counts and cache budgets are unchanged. No gameplay FPS
improvement is established by the component measurements below.

## Reuse immutable GPU-height geometry

A new material/texture owner can reuse an existing displacement VB/IB and output
declaration. The complete key retains immutable source ownership/content,
source declaration, program, domain, detail, address mode, exact UV constants
and requested subdivision factor. Skinned relief also requires a bit-identical
height peak. CPU-height and layered paths retain their ordinary construction.
Each material still binds its own current height texture and current pose.

The source/UV/peak proof matters: equal topology alone cannot authorize sharing
vertex attributes, seam pinning, barycentrics or anatomical relief. No CPU mesh
payload or additional GPU buffer is cached. Existing COM references keep shared
buffers alive, and the existing resource ledger charges their bytes once until
the last entry leaves. Height and source charges retain their previous rules.
Eviction, pressure and Reset follow the same release path. The three new entry
fields cost 12 bytes on x86, at most 3 KiB for the 256-entry table.

`DISPLACEMENT heightGPU` reports cumulative `subdivisionReuses`. This counts
avoided successful build/upload operations, not avoided draws or GPU time.

## Two current-pose bases

Each immutable envelope can reference two records in the existing fixed table
of sixteen pose certificates. View and world basis requests no longer overwrite
each other immediately. Serial, held typed bank, exact basis, reset, BOOL/INT,
source reconstruction and admission/operation/time budgets remain checked.

Changed constant epochs still run the original evaluator. No interval
arithmetic or rounding changes. Fixed certificate storage and the number of
records remain unchanged; each envelope entry gains one four-byte slot index.
Logs add `poseBasisMisses` and `poseStampMisses`.

A narrow identical-write experiment held at most 96 floats per certificate. It
improved repeated eight-bone palette writes by about 15%, but made continuously
moving eight-bone poses about 44% slower (0.54235 to 0.78035 ms per 1,024 calls).
It was removed. No pose-value copy/memo exists in the production change.

## Transfer the range constant bank

The existing per-draw `CaptureSource` now carries the range bank identity and
its exact epoch. Material preparation can swap the two already allocated banks
instead of copying the same palette again. Transfer requires this object's
range buffer, matching captured span/epoch and the owning typed device's held
draw gate. A changed pose, independent source, reset, unsupported span or unheld
reader keeps ordinary constant acquisition. No borrowed bank survives the draw
or becomes packet storage; replay consumers retain their existing immutable
copies and leases. The two original 4 KiB arrays remain the only bank payload.

`MATERIAL_ADMISSION_SOURCE constantBankTransfers` counts these transfers for the
sampled frame. Model overlay capture explicitly copies the full bank after the
array-to-pointer change.

An initial generic handoff from materials into normals/world was rejected:
checking a small borrowed span was slower than the existing D3D mirror getter
(0.0648 versus 0.0489 ms for 8,192 three-register reads). That implementation is
absent from production. Ownership transfer removes a copy instead of adding a
second general validity layer.

## Controlled comparisons

Baseline headers are frozen from `e2bd3760`, compiled with the same Zig 0.15.2,
x86/O2 options. Seven alternating measured pairs follow a warmup pair. Existing
certificates are enabled in the baseline when present; comparison does not
artificially disable its existing fast path. Output digests and exact bounds
must match. O0 runs validate correctness, not performance.

| CPU component | Before ms | Current ms | Factor |
|---|---:|---:|---:|
| 1,024 requests alternating two bases, 75-bone source | 2.8036 | 0.9831 | 2.85x |
| 1,024 identical palette rewrites, eight bones (control) | 0.5134 | 0.5401 | 0.95x |
| 2,048 material bank acquisitions after range capture | 0.2662 | 0.1220 | 2.18x |
| 64 repeated build/copy operations versus existing GPU geometry leases | 52.9452 | 0.0431 | 1,228x |

The last case deliberately represents reuse across new texture owners. Its
baseline includes CPU subdivision and a fake upload byte copy, not real driver
or GPU execution. It is not a multiplier for complete capture. Existing full
material-cache hits already avoid construction; ordinary cold/changed geometry
still builds. The actual two-texture GPU fixture warms both material owners,
so its warm whole-path times remain approximately unchanged.

The one-basis 75-bone control is 0.6655 versus 0.6537 ms; alias and maintenance
controls remain approximately unchanged. Continuously moving, single-consumer
poses take 0.548325 versus 0.579450 ms for eight bones (+0.031125 ms per 1,024
calls), and 2.70155 versus 2.726125 ms for 75 bones (+0.9%). The two-slot lookup
therefore has a small miss-path cost; it does not accelerate every workload.
The separate identical-write control has about 5.2% overhead. These controls
are retained in the results rather than being counted as improvements. Saved raw samples and
source hashes are under `.local/out/capture-acceleration-20261117/`.

## Validation and live check

`test_capture_acceleration.py` checks current O2/O0 geometry keys, independent
byte equality, COM lifetimes, shared-byte eviction, range-bank transfer, changed
palettes, unheld fallback and Reset. It also executes the existing constant
capture oracle using Zig on Windows. `test_cpu_capture_stages.py` adds paired
basis and identical-write cases, plus used/unused-row negative controls. The new
test belongs to `architecture`, with discovery updated.

`test_material_equal_depth_gpu.py --m2-preflight --preflight-source
--subdivision-sharing --benchmark` compares real current capture/native/flush
images with sharing disabled/enabled, independently alternating two texture
owners. It checks that the reference has no shares and the displacement case
actually reuses GPU geometry. Normal, POM and displacement, near/mixed/far
patterns, normal/color/composited pixels, state/constants and pressure/recovery
remain compared. Fixture compiler compatibility is recorded separately from
the unchanged production shader freshness gate.

Final acceptance uses the source and architecture suites, affected material,
pose, subdivision, normal-capture and GPU contracts, followed by
`python scripts/build_renderer.py`. Reports must match the final source tree.
The production DLL must be delivered together with its matching
`northlight-assets.bin`. No release archive or full gameplay acceptance is
implied by the DLL build.

For the game test, use the same route/settings and inspect `CPU capture`,
`constantBankTransfers`, `poseCertifiedHits`, `poseBasisMisses` and
`subdivisionReuses`. Include cold entry/rotation, crowds, animation and pressure.
The earlier gameplay logs predate this change and cannot measure its benefit.
