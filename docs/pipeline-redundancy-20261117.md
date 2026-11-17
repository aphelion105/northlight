# Repeated point-shadow preparation — new run, 2026-11-17

The input is the client run ending at 19:17:11 local time, installed renderer
source `e39f2968815d10ce1875667142d6c6571c4e764d6f803972e007d83a17b9ddd0`.
Its frozen log and parsed settings are in
`.local/out/pipeline-redundancy-20261117/gameplay-before.log` and `analysis.json`.
No newer capture accompanies this run. These are sampled observations, not a
controlled FPS benchmark: loading, shader warmup and lamp animation vary.

## What the staged disabling established

| Renderer frame / active changes | Effects GPU | PointShadow | FogMarch | TemporalLight | Capture CPU |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1397: actors, clouds, Godrays, celestial, weather, materials disabled | 4.357 ms | 0.854 ms | 0.552 ms | 0.643 ms | 2.550 ms |
| 1524: additionally AO disabled | 4.578 ms | 1.477 ms | 0.590 ms | 0.601 ms | 1.922 ms |
| 1651: additionally local lights disabled | 2.682 ms | 0 | 0.614 ms | 0.622 ms | 1.907 ms |
| 1905: additionally world fog disabled | 1.638 ms | 0 | 0 | 0.570 ms | 2.034 ms |
| 2032: additionally world shadows disabled | 0.933 ms | 0 | 0 | 0.049 ms | 0.515 ms |
| 2794: all settings restored | 11.672 ms | 3.402 ms | 3.847 ms | 0.643 ms | 8.475 ms |

With actors disabled, 14 selected lamps still rebuilt 54 static faces and
submitted about 930 static draws in a sampled frame. After restoration there
were 16 lamps, 60 rebuilt static faces and 3671 actor replay draws. The bounds
were valid for all 1155 packets. Previously added palette instancing was only
eligible for 95 packets; its synthetic speedups do not describe this workload.

The actor-free frames are not idle shadows: animated equipment lamps can move
even while the camera is stationary. Reusing their previous cube unchanged
would move the shadows incorrectly. The fixes below remove preparation work
while preserving current-position rendering.

## Changes

**Spatial broad phase.** Each prepared upload owns an optional immutable bounds
tree over the coarse batches and their point-only subranges. The worker builds
it once; commit transfers its shared ownership with the exact pages. A moving
lamp rejects whole distant nodes using the original double-precision sphere
predicate. Survivors retain the previous face tests, order, masks and mergers.
Terrain and fine ranges both participate. Unknown bounds stay eligible; stale
sizes, refused allocation and small/oversized plans use the original scan.
The tree is included in page/memo memory accounting and is released with the
committed mesh. This does not change geometry, resolution or refresh policy.

**Demand-driven content signatures.** A moving lamp already redraws every static
face. Previously, its submission also walked every committed content record to
hash that temporary light position. Those hashes could not save that redraw.
Moving-light faces now record their exact mesh generation without this walk.
An unchanged light can reuse them within that generation. The first subsequent
mesh publication conservatively refreshes faces whose content was not hashed;
stationary faces then resume the existing content-based retention across later
publications. This saves repeated hashing without retaining old scene snapshots
or introducing another memory cache. `POINT_PHASE` now reports `contentPasses`,
`contentRecords` and `spatialIndex` for gameplay confirmation.

## Evidence and limits

`test_point_batch_tree.py` compares 4,732,026 face masks, including partial-face
requests, sphere boundaries, invalid bounds, extreme coordinates, unknown
leaves and failed admission. A moving-lamp synthetic case reduces classification
calls from 4,732,026 to 47,433; its isolated speedup is about 6.8×.

On real Northrend geometry around the logged camera, 5050 coarse batches plus
3124 point subranges, the two logged 12-unit lamps reduced CPU preparation from
about 0.045 ms to 0.023 ms each: **about 1.9×**. This includes ordered scans,
terrain classification, face masks, candidate lists and static draw coalescing;
it excludes actor admission and GPU submission. The tree occupies 326,988 bytes
for this fixture. Linear and indexed paths produce identical masks and draw
commands. Native D3D9 and DXVK cube pixels also match exactly.

The GPU fixture's separate old/new partition timings include the spatial
triangle partition delivered earlier. They are not a new GPU speedup from this
tree: this change preserves its submitted commands. Final measurements with
input hashes belong in `spatial-final/point-shadow-spatial/validation.json`.

The production WorldRenderer GPU contract covers the indexed and stale-index
fallback paths, six-face image equality, copy failures and terrain ownership.
Its content-demand check exercises 10,000 moving positions with zero content
walks, then unchanged, changed and unknown mesh publications.

This patch does **not** establish a multiplicative whole-game or every-effect
gain. The expensive animated fog integral, actor point replay fan-out and
material/normal admission remain measurable costs. The log's 43,114 timer reads
also contribute an estimated 0.915 ms of CPU profiling overhead in frame 2794.
Keep the same scene and staged toggles for the next gameplay comparison; do not
add CPU and GPU timings or extrapolate the component ratio to FPS.

## Acceptance and delivery

Run strict source and architecture suites, affected page/store tests and the
point-shadow coverage/stutter/pipeline GPU contracts. Build with
`python scripts/build_renderer.py`; keep compiler, shader freshness and asset
verification gates. Current report paths, source identity, hashes and the
installed DLL/assets backup are recorded in `installed.json` under the artifact
directory above. Reports must verify against the final tree.
