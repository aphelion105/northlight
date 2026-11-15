# Runtime cache inventory and upload reuse, 15 November 2026

Audited after `418c3f34` / release 0.3.218.5. The previous QA report remains the
source for scene timings; no new gameplay log exists for the sparse-range DLL.
The installed client log still predates that release. Do not attribute its FPS
or cache hit counts to the new build.

This inventory covers the main runtime cache families and their ownership.
It is not a hardware L1/L2/LLC cache-miss measurement. Windows GPU component
experiments and reports are under `.local/out/cache-audit-20261115/`.

## What already exists

| Cache / source | Reused work and validity | Assessment |
|---|---|---|
| Command-stream state, buffer and texture shadows (`stream_state.h`, `stream_proxies.h`) | Repeated state writes, exact CPU contents and owned deferred uploads. Write flags, revisions, GPU mutation and replay completion control reuse. | Keep exact ownership. Sparse oversized VB/IB ranges were fixed in 0.3.218.5; no larger full-buffer allowance is added here. |
| Immutable geometry snapshots (`draw_snapshot.h`) | Complete resource/range/layout/tracking keys, shared source pool, decoded indices, source identities, lookup prediction/prefetch and LRU. Unknown writes require revalidation. | Already shared by material/shadow consumers. A second snapshot cache would duplicate geometry and risk lifetime errors. |
| Vertex declarations (`vertex_declaration_cache.h`) | 128 retained immutable COM declarations, decoded elements, validated capture extents and most-recent-entry lookup. | Cache hits avoid driver decoding. Other hits still scan the small table; benchmark actual declaration diversity before replacing its lookup. |
| Rigid position ranges (`position_bounds.h`) | 4,096 bounded entries keyed by resource identity, byte range, stride and count, validated against write revision and size. | Existing early bounds avoid full mesh/index copies. Current transforms remain draw inputs. |
| Prepared pose metadata (`replay_bounds_metadata.h`) | Shader/declaration program/layout preparation, retained immutable objects, 128 entries / 2 MiB and four cold preparations/frame. | QA has 934–981 hits and zero misses in late rotation samples. More entries would not remove the current-pose calculation. |
| Sampled vertex inputs and rigid-bone classification (`sampled_vertex_cache.h`) | Immutable geometry/input layouts with weak-owner validation; current constants/camera evaluated on every draw. 2,048 sampled-input and 8,192 rigid-bone slots. | Existing set associativity addresses pointer-stride collisions. Do not cache animated output using geometry identity alone. |
| Pose envelopes and exact bounds (`replay_bounds.h`, `replay_envelopes.inl`) | Immutable topology/envelope preparation, current pose evaluation, bounded optional result memo. | The earlier pose-result memo was disabled after low-hit evidence. Re-enabling it blindly adds key comparisons and palette copies. |
| Shared native GPU geometry (`scene_native_store.h`, `gpu_scene.h`) | Material/shadow consumers share identity, uploaded geometry and batches; cross-frame probation, LRU and mutation-safe immutable sources. Normal consumer limits are 32/64 MiB. | Production already avoids duplicate uploads. `replay_gpu_cache.h` is the old per-mesh test oracle, not another active production cache. Frame sweeping is skipped when all entries have tracked identity. |
| Generated material maps (`auto_normal_cache.h`) | Source/model recipe and write revision, bounded GPU maps, hashed lookup, recent hit, recycled targets, active leases and frame-local refusal cache. Adaptive allowance is 32/96 MiB. | Maps cannot be evicted/reused while a pending draw borrows them. At 32 MiB the QA runs show churn; increasing this budget competes with the 32-bit client's address space. |
| Persistent generated maps (`material_disk_cache.h`, `material_map_persistence.h`) | Content/recipe/shader/backend identity; asynchronous CPU file jobs and owned bytes. Ready maps survive GPU eviction and process restart. | Disk reuse is effective, but temporary upload-surface reuse was needlessly restricted to exactly equal dimensions. Fixed below. |
| Static world/caster residency and plans (`static_shadow_gpu.h`, `static_cache_slices.h`) | GPU geometry/texture residency, bounded upload work, reused model plans and partial shadow-map updates. | Geometry/lighting changes must invalidate the corresponding work; a stale depth map is not a quality-preserving optimization. |
| GI decoded sources and acceleration (`scene_source_cache.h`, `gpu_scene.h`) | Immutable FGS2 sources shared by workers, file revision tokens and retained prepared geometry/BLAS. Scene source allowance is 8 MiB with leases accounted separately. | File revision checks remain necessary. Optimize lookup only if worker identity/load timings demonstrate a bottleneck. |
| GI probes and relighting (`world_probe_cache.h`) | Spatial probe LRU, recycled nodes, invalid-probe results and a bounded 4 MiB relight-response cache. Geometry/light/solver changes invalidate reuse. | Static response is cacheable; final sun/moon/animated-light output cannot simply be frozen across frames. |
| Local-light assets and attachments (`world_local_lights.h`, `model_light_attachments.h`) | Map-level FGL/FGA data; dynamic observations/attachments have separate freshness and ownership contracts. | Asset decoding is already amortized. Source position, animation and intensity still need current observations. |
| Weather shelter (`weather_shelter.h`) | Small asynchronous camera-lens exposure query, bounded spatial/time reuse. | Native WoW owns scene precipitation/shelter. The old full weather geometry path must not be mistaken for active duplicated rain work. |
| Screen targets, state blocks and deferred packets | Lazy screen resources survive ordinary frames; state blocks and replay bindings reuse uninterrupted valid state, with reset/resize/pressure and mutation barriers. | These are also caches. Reusing state across a barrier without checking ownership would corrupt native rendering. |
| Packaged shaders/art (`renderer_assets.cpp`) | Immutable resource bundle decoded once, validated by SHA-256 and retained in memory. | There is no per-frame DLL/`.bin` decompression to cache again. |

## Measured remaining churn

The supplied logs contain these cumulative disk-map counters:

| QA run | Ready-map uploads | Upload surface creates | Upload surface reuses | GPU map evictions | Map allowance |
|---|---:|---:|---:|---:|---:|
| Town / ziggurat, log (5) | 1,134 | 500 | 634 | 1,537 | 32 MiB |
| Zangarmarsh rotation, log (6) | 689 | 326 | 363 | 938 | 32 MiB |

In the rotation log only 11 new maps were generated while the normal-map cache
served over 1.7 million hits. The persistent cache restored maps after eviction;
the renderer was not regenerating every map from scratch. Roughly half of the
disk uploads nevertheless allocated a new system-memory staging surface.
The logs do not identify every allocation's dimensions, so they cannot prove
that every one of these creates came from size alternation.

The current implementation destroyed its upload surface whenever either
requested dimension differed. A 64×64 map following a 512×512 map therefore
discarded perfectly usable storage. This policy remained present in 0.3.218.5.

## Implemented change

Keep the high-water width/height of the single staging surface, growing only
when the new map cannot fit. `UpdateSurface` receives the exact written source
rectangle, so unused rows/columns and bytes from a previous map never reach the
target. A tall-to-wide transition grows each dimension as needed.

The existing maximum remains 512×512 BGRA8, or 1 MiB; it is one surface, not a
pool of full-size staging images. Pressure, explicit trim and reset still free
it. Busy resources still receive only the existing nonblocking fresh attempt,
and a failed copy drops the surface for a later retry. No map precision, mip
chain, shader, target dimensions, GPU-map budget or quality setting changes.

## Validation

`test_material_staging_sizes_gpu.py` compiles the frozen 0.3.218.5 uploader and
current uploader into the same real-device test. It alternates 512×512, 64×64,
512×64, 64×512, 31×17, 128×256, 511×127 and 1×1 maps with changing nonuniform
pixels. All restored pixels must equal the requested bytes, including odd
sizes and different row pitches. Cross-aspect growth, explicit trim/reset,
busy/nonblocking retry and failed-copy recovery are covered too.

Each 96-map mixed-size sequence drops from **96 allocations to 1**. Starting
with a narrow/tall surface and growing later requires two allocations. Initial
ABBA measurements, milliseconds spent submitting those 96 uploads on the CPU:

| Windows backend | Frozen uploader | New uploader |
|---|---:|---:|
| System D3D9 | 2.842 | 0.625 |
| DXVK 3.1.1 | 14.879 | 2.770 |
| DXVK 2.7.1 | 15.238 | 2.745 |

These timings exclude target creation and image readback. They are component
measurements, not a 5× frame-rate improvement. A frame may perform no disk-map
upload at all. Actual FPS/stutter improvement still needs a new gameplay run.
All 960 compared uploads per backend matched their requested pixels exactly.

The named `material-cache` suite covers memory limits, identity/ownership,
file persistence, map reuse and the new mixed-size GPU contract. The separate
existing disk GPU fixture also checks restart/corruption/settings compatibility.

## Further work: prioritize evidence over additional caches

1. Recheck cache pressure and reuse-distance in a current rotation run. If maps
   churn despite a small visible set, profile admission/eviction policy before
   increasing the 32 MiB pressure allowance or retaining another CPU pixel pool.
2. Measure declaration lookup diversity and source-alias key construction inside
   warm snapshot time. A compact lookup could help varied declarations, but a
   scene with one repeated declaration already uses the existing direct hit.
3. Measure FGS2 stamp/identity time separately from decoding and BLAS building
   during travel. Avoid caching away revision checks without a new invalidation
   contract; GPU scene identity and file identity are different domains.
4. Keep current-pose evaluation distinct from immutable envelope preparation.
   A useful pose-result key must include the exact relevant palette/constants,
   program, layout and source generation. A geometry-only pose cache would
   produce stale animation and shadows.

Existing diagnostic counters are sufficient to confirm upload reuse in the
next run: compare `MATERIAL_DISK uploads/uploadCreates/uploadReuses/busy`, map
evictions and CPU material capture with the same scene/settings. No diagnostics
were disabled to obtain the component result.
