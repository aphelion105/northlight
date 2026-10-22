# Northlight rotation stalls: terrain capture cache and accurate stage profiling

23 October 2026. Complete user run ending at approximately 00:23 Europe/Kaliningrad. Installed baseline: `11f79a8`. The agents did not launch the game.

## Main finding and change

The new command profiler identifies the major remaining foreground cost with materials Off: **repeated terrain snapshot preparation after eviction from the 32 MiB CPU content cache**. At sampled frames 3556/4064/6858, terrain capture takes **11.159/9.947/12.032 ms CPU**, with **513/429/618 misses** and **494/441/619 evictions**. Their native material-reference draw CPU totals are only **0.649/0.909/0.708 ms**. The large DrawIndexedPrimitive interval includes these renderer capture hooks before the actual native draw.

Each cached draw previously retained its own copy of the original index bytes, in addition to the detached snapshot's remapped indices. Neighboring terrain draws can use identical source index topology. The cache now shares **exactly equal immutable source-index byte blocks** through a bounded one-block weak memo. It does not share a mutable game buffer or infer equivalence from pointer identity. Full draw/layout keys, live lifetime/write stamps, fallback byte verification, output geometry, coverage and ordering are preserved.

The **32 MiB budget and 4096-entry limit remain unchanged**. Every source block's vector capacity, object and control-block allowance is charged once while referenced by cache entries. A conservative fixed reserve covers the expired weak memo. When eviction removes the last entry referencing a prospective shared block, admission recomputes its charge before publishing the new entry. Allocation failures keep the computed snapshot usable and leave no leaked cache charge. The optimization retains more exact snapshots within the same limit.

The frozen old/new rotation fixture uses 4000 draws, twelve yaw sectors and 8000 revisits. The old cache misses and evicts on all 8000 revisits and rereads **80,160,000 bytes**. The new cache hits on all 8000, with **zero revisit reads, evictions or replacement vertex preparation**. Geometry fingerprints agree. The prior cache retains 2947 entries; the new cache retains all 4000 under 32 MiB. The fixture includes cold setup and geometry-digest work; its component timing is not an in-game FPS prediction. Real-client source-block sharing must be checked using the new counters in the next user run.

## What the fresh run proves

The installed DLL SHA-256 is `e058285b2a108d349c14266ae8fc4a50f480f7313681e14ee0a8929185275d2e`. The frozen log is 2,872,390 bytes, SHA-256 `c7d91da6b7154546a3f70d6f85ded70733f041ec3f5cd673526c407a14a50689`. Its genuine preceding log has SHA-256 `f535b934ad04a85fea8aa34e64cc1e01953d7564d4abecea952f0e5bf2057993`.

The preceding inactive-shader fix works: **37 originals queued while Off; zero material vertex-compilation attempts and zero cold vertex CPU time while Off**. Enabled first use still compiles synchronously: one first-On event costs 26.366 ms for 21 variants. That cold toggle cost is separate from continuous rotation and preserves the first enabled image.

This session contains switches; it is not entirely material-Off. Sampled endpoint ranges are:

| Sampled frames | Material master | Terrain/walls/grass/M2 modes |
| --- | --- | --- |
| 0–2794 | Off | 3/3/3/0 |
| 2921–3175 | On | 3/3/3/3 |
| 3302–3429 | On | 3/3/3/0 |
| 3556–8509 | Off | 3/3/3/0 |
| 8636–8763 | On | 3/3/3/0 |
| 8890–11430 | On | 3/3/3/3 |

These are sampled ranges, not exact switch timestamps. AO disappears from the stage signature at 3937–7874 and returns at 8001. Cloud passes disappear at 5461–9398; base fog still runs. Overall effects are toggled near the end. Saved final INI settings therefore cannot describe every interval. The quality INI also differs from the preceding session. Cross-session FPS differences are not a controlled DLL comparison.

Within the long Off section, 120-frame windows still fall to **59.03 FPS** at 4080, **63.33** at 5040 and **68.43** at 6960, with settled windows around 118 FPS. At frames 3556/4064/6858, query before-replay spans are 13.672/15.812/14.005 ms, while actual query tasks take 2.111/2.262/2.311 ms. Most of the extra delay is earlier draw/capture work. Query completion, generations, HRESULTs, ordering and pumped waits are unchanged.

GPU GameScene intervals of 14.707/16.642/15.462 ms overlap CPU submission/capture work and may include GPU starvation gaps. They do not prove that shader arithmetic consumes the entire interval. None of these overlapping timings should be added together.

## Fifteen investigations in three separate directions

| Direction | Topic | Finding and disposition |
| --- | --- | --- |
| GI/world | 1. Terrain index generation | The preceding directional alias is active. Keep its exact GPU indices and conservative membership proof; no quality reduction. |
| GI/world | 2. Terrain snapshot/arena churn | **Implemented exact source-index sharing** to reduce cache eviction and the resulting re-preparation/upload pressure. Existing GPU format, arena, fences, read limits and cache limits remain. |
| GI/world | 3. Actor decode/pose/hash/BVH | New timers show texture decode around 0.36–0.42 ms, pose 1.6–1.8 ms, hash 0.97–1.02 ms and BVH/copy 1.8–2.1 ms in settled 12-texture worker jobs. A new texture cache would not remove the dominant actor work. These are background elapsed spans, not frame-time additions. |
| GI/world | 4. Static regions/pages/publication | Camera orbit moves the eye and can change legitimate world coverage. Preserve current pages, generation admission and publication semantics; no stale-world shortcut or anchored camera substitution. |
| GI/world | 5. Worker/foreground contention | Separate long foreground terrain captures from background jobs and producer-idle stalls. The measured terrain cache churn is actionable without changing scheduling or fences. |
| Materials | 6. Inactive shader preparation | Prior lazy registration is confirmed: Off compilation is zero. Keep synchronous first-enabled availability and bounded identities. |
| Materials | 7. Inactive profiling overhead | **Remove redundant per-domain native GPU timestamp pairs while inactive.** Native CPU/admission and global GameScene measurements remain. Old Off query CPU has median 0.2163 ms per sampled frame; this is diagnostic overhead, not a new material render. |
| Materials | 8. Warm capture/source/residency | At frame 11430, 151 deferred capture draws use resident geometry, zero UP fallback/new uploads/source reads; repeated setter reuse skips 9767 setters. Captures are real work, but normals are not continuously rebuilt. Preserve writer barriers and exact current state. |
| Materials | 9. Pose/envelope/range fallback | Late enabled median fallback/envelope/remaining boundsPose is 0.5785/0.2868/0.2876 ms. A register-initialization candidate passed 804 exact cases but regressed a longer-program repeat by 4.75%; **excluded from production**. No stale-pose or enlarged-budget workaround. |
| Materials | 10. Normal/height/POM/cavity work | Late normal-build/mip medians are zero; all 151 normal lookups at 11430 hit. Modes are 0 or 3, with no live POM interval. Cavity/self-shadow are disabled. Existing normal range 48 and shared occlusion range 8 remain. Do not infer unmeasured enabled POM/cavity cost. |
| AO/post | 11. AO setup/generation/blur | AO setup GPU rounds to zero and CPU is about 0.0005 ms. Large scene/capture peaks also occur with AO disabled. Preserve sample count, method, strengths and resolution. |
| AO/post | 12. FogTemporal history | Revalidated the preceding shader against genuine pre-fastpath bc382d9 with the actual production LINEAR s9 sampler: **594 exact images**. Earlier POINT-sampler fixture coverage alone was incomplete. No shader change this iteration. Timing remains a targeted microbenchmark, not a universal active-HDR/FPS claim. |
| AO/post | 13. Fog/cloud/light passes | Base fog runs when clouds are Off. **Move FogMarch profiling outside the cloud-only guard** to expose that work correctly. Nonzero source contribution and existing scattering remain. |
| AO/post | 14. Blur/composition/HDR/water | The old WorldComposite interval includes both FogBlur passes. **Add a separate FogBlur mark after both draws.** A shader candidate passed 648 image comparisons but repeatedly regressed water cases; **excluded from production**. HDR and water output remain unchanged. |
| AO/post | 15. Resource/history invalidation | Existing reprojection, rejection, history weighting and ping-pong targets are preserved. No unnecessary full copy or safe changing-input reuse was proven. Independent review found no correctness issue in the terrain sharing change. |

## Profiling behavior and correctness

`TERRAIN index storage` now reports `sourceIndexBlocks`, `sourceIndexReferences`, `sourceIndexMiB` and `sourceIndexAvoidedMiB`. Read these with existing lookup misses, evictions, cache bytes, actual read bytes and terrain CPU time. The memo is intentionally bounded to one preceding block; unique or interleaved nonmatching patterns can still miss. No claim that every client working set now fits is made. The 4096-entry cap can still be a limiting factor.

`MATERIAL_CONFIG nativeGpuEnabled=0` explicitly means inactive native-reference GPU scopes are suppressed, not that native GPU rendering is free. CPU scopes remain. Tests prove Off→On→Off timestamp endpoint counts **0→64→0**, positive CPU scope activity, identical 96 native draw calls/results, unchanged render state and released query references. Enabled per-domain material GPU profiling remains available.

FogMarch now ends after the base source march regardless of cloud activity. FogBlur ends after both guarded blur draws and before composite setup. Clouds-active sampled frames add one timestamp mark; clouds-inactive fog samples add two relative to their former ambiguous layout. Unsampled frames add no timestamp work. The existing six-slot nonblocking profiler has room for 42 marks, one more than before; it neither flushes nor waits for queries. Saturation, failures, reset and release are tested.

Terrain tests cover last-reference eviction/recharge, lowered caps, reset, optional insertion/allocation failures and immutable held snapshots, plus existing source mutation, unknown revisions, lifetime/address reuse, lock/unlock failures, 16/32-bit boundaries and arena behavior. Geometry fingerprints and index/output oracles remain exact. No unavailable sanitizer result is claimed on this Windows host.

All production HLSL and shader binaries remain unchanged from 11f79a8. Accepted changes affect immutable CPU cache storage and sampled diagnostics; render distances, samples, precision, active coverage and MSAA are preserved. The parent integration record contains the final x86 build, original 17-export comparison, 140 material capture checks, all source hashes and package hash. The rejected shader/fallback experiments remain diagnostic evidence outside the production tree.

## Next comparable run

Use the separately packaged DLL with the same scene, camera radius and orbit path. Keep materials, AO and cloud settings constant for each run. Check whether terrain source blocks are actually shared, whether miss/eviction/read peaks fall, and whether capture CPU falls with them. If entry-count pressure or unique topology remains dominant, the new evidence will distinguish it. Actual in-game FPS of this new DLL remains unmeasured until the user runs it. Installed DLL, personal settings and game assets were not changed by the agents.

Final integration: release x86 PE32 builds with all 17 original exports. The 140 material capture checks and 594 production-sampler FogTemporal comparisons pass (734 retained-production GPU validation cases). Another 648 comparisons belong to the rejected FogBlur experiment and are not a shipped shader change. CPU regressions and the 42-mark profiler ring pass. DLL SHA-256: `6fc8d53526aad3cbde780c65ec1d3521aaeadd22ca5808992d1cf0ebca3621ab`.
