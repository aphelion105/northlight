# Northlight rotation stalls with materials disabled

22 October 2026, fresh run ending 23:25 local time. Baseline: `c5ef83c`. No game was launched during this investigation.

## Result

The user is correct: this run reproduces substantial rotation stalls **with the material master switch disabled**. Saved modes remain `3/3/3/0`, but `enabled=0` overrides them. From sampled frame 3302 onward, capture, source reads, pose evaluation, normal generation, deferred replay, material GPU uploads and material application are all zero.

An Off window averages **48.20 FPS / 20.746 ms** at frame 3469, another **59.70 FPS / 16.749 ms** at 3589, and the settled window reaches **102.01 FPS / 9.803 ms** at 3829. These are changing views within one run, not a controlled old/new DLL comparison. The renderer does not timestamp the keyboard/mouse input itself.

The principal retained optimization is outside materials: live terrain's shadow GPU arena now stores the same float positions and UVs in the existing **20-byte caster vertex format**, instead of a 32-byte world vertex with unused normals. This changes neither geometry nor precision. At the same memory limits, a pressured 19-rotation test reduces uploads from **94,841,600 to 23,316,000 bytes**, and arena rollovers from **5 to 0**. Production shadow shaders pass 96 exact image comparisons including MSAA 0/2/4/8.

A separate enabled-material fix rejects already unsupported oversized displacement meshes before height/UV processing. The build also adds missing profiling for query wait attribution, AO setup versus draw, precise GI worker stages and rare material shader creation. No quality, range, sample-count, shader arithmetic or settings reduction was made. The actual game-FPS effect of this DLL still requires the user's next run.

## Input and measured bottlenecks

Frozen evidence directory: `northlight-diagnostics/rotation-disabled-20261022-2325/` under the game root. The fresh complete renderer log is 1,042,816 bytes, SHA-256 `14c972be1289ddf8047a4f18cbde375afbc7504a14584c3375ac20762eeefcda`. Its previous log is the genuine 22:38 run, SHA-256 `1948f37eb25e8c716e91b55c48d04cd39df52588f811a98ba63e6f9f88f588ea`.

Installed DLL SHA-256: `5a8c38d0ba7aa41878b91eca4eeac0652c2445cb524e229d94ce9b93745d18b7`. Quality INI SHA-256: `f37ee1a0729eef29cc8c9404af54a4e195881f62b5d80a77a1fe9d915cc6c78b`. The installed DLL, INIs and backend configuration were preserved.

| Off sample | Frame 3302 | Frame 3556 | Frame 3810 |
| --- | ---: | ---: | ---: |
| CPU world capture | 0.682 ms | 3.791 ms | not sampled by this CPU timer |
| Complete sampled GPU interval | 6.903 ms | 13.391 ms | 7.160 ms |
| GPU GameScene | 3.040 ms | 8.585 ms | 3.375 ms |
| GPU Effects | 3.007 ms | 3.657 ms | 2.898 ms |
| GPU material application | 0 | 0 | 0 |
| Original native draw in material reference profiler | 0.8142 ms | 0.9794 ms | 0.8563 ms |
| Material admission | 0.0561 ms | 0.0615 ms | 0.0606 ms |

The approximately 0.86–1.04 ms material-profile total in Off samples includes the original native draw. It is not an extra material render. The sampled profiler itself also reports about 0.30–0.33 ms of query CPU overhead; that is diagnostic work and must be distinguished from effect cost.

The largest sampled full-run GPU frame is 40.025 ms, with 36.126 ms GameScene and 3.055 ms Effects. That sample is outside the global-Off interval. Inside Off, AO generation is ordinarily about 0.115 ms, AO blur 0.343 ms, fog march 0.817 ms, cloud fog 0.622 ms and final world composition 0.542 ms. These comparatively steady tails do not explain the whole rotation loss. An isolated AOBlur interval rises to 2.048 ms; the new split instrumentation will distinguish setup/dependency gaps from the draw interval.

GPU timestamps measure elapsed command intervals, including queue starvation, resource dependencies and driver scheduling. They are not an isolated measure of shader arithmetic. CPU and GPU timelines overlap and must not be added.

## Fifteen separate investigations

Three agents each owned a different direction and five topics. The root separately investigated command-stream waits and integrated the changes.

### GI, world and streaming

| Topic | Evidence, action and remaining limit |
| --- | --- |
| 1. GI grid motion, retargeting and cancellation | The Off interval contains 179 logged GI requests, all reason 2 (grid motion). Third-person orbit really moves the eye; it is not a spurious yaw invalidation. Compatible retargets reach 23 versus six incompatible cancellations. Final static activations reuse all 5,600 probes. Probe placement, coverage and cadence remain unchanged. |
| 2. Static region/page preparation and publication | Even with materials Off, ten region builds and nine mesh commits occur. Warm commits reuse 46–57 pages but upload 50–60 pages, about 20.21–23.15 MiB. Existing page reuse works; changed eye-centered coverage needs additional pages. Background phase durations are recorded separately and are not added to foreground frame time. No unproved page omission or delayed-lighting shortcut was adopted. |
| 3. Terrain arena/index churn after CPU compaction | The preceding CPU compaction is working: the settled cache retains 3,197 compact snapshots, avoids 8.597 MiB of index storage and reads zero source bytes. Yet live GPU arena rollovers rise from 36 to 68 during Off. **Implemented the lossless 32 -> 20-byte GPU vertex layout**, using the existing caster type and a dedicated declaration in directional and point passes. It removes unused data and increases retained vertices in the same byte cap. |
| 4. Dynamic actor tracing and path reuse | Actor correction remains on its existing roughly 250 ms cadence and replays approximately 8,154–8,169 of 8,192 rays. Coarse `actorMs=0` does not mean no work. Added precise `actorWorkMs` and `solveWorkMs` elapsed worker timings at stage/publication boundaries, without per-ray clocks or scheduling changes. |
| 5. Worker scheduling, generation handoff and locks | The observed retirement-wait peak of 6.154 ms belongs to the background builder. It is not a directly measured 6.154 ms render-thread stall. Existing generation limits and retirement fences are retained. Precise worker timing plus root query attribution will separate contention from foreground waiting in the next run. |

### Materials and capture

| Topic | Evidence, action and remaining limit |
| --- | --- |
| 6. Hidden rendering while globally Off | All expensive material stages and cache bytes are zero in Off samples. Added a regression proving no capture/state/texture/snapshot/barrier work while preserving exactly one native draw. Recognized shader registration is a separate cold path, covered below. |
| 7. Normal/height cache and rejected geometry work | Warm samples have zero normal/mip generation. A WMO UV stage repeatedly costs about 0.37–0.38 ms despite no subdivision/upload. Source inspection finds the existing 8,192-primitive cold-build rejection after UV/height work. **Moved that unchanged rejection before height and UV**, after the current range proof and exact cache/overlay hits. A new cumulative `primitiveRejected` counter can identify its actual live contribution. The old log alone does not prove every UV refusal is oversized. |
| 8. Current animation pose and bounds | Warm enabled samples spend about 0.295 ms in envelope service and 0.576–0.586 ms in fallback, with about 8,182 current-pose vertices. Both vanish with M2 Off. The preceding O(1) rigid-bounds LRU remains in use. No stale-pose visibility proof, larger budget or approximate cull was introduced. |
| 9. Capture packet/state/source promotion | A changing view can still cause source churn and UP fallback, but a warm sample uses 271 resident draws, zero reads/uploads/UP/expiry and skips 17,897 identical setters while issuing 1,444. Existing source sharing, weak ownership, state reuse and mutation barriers are working. Caps and lifetime validation are retained. |
| 10. First-visible shader variants and cold dependencies | Recognized vertex registration can create patched material/displacement variants even while Off; model capture variants are lazy. The old log lacks a separate timer. Added **`MATERIAL_COLD`** counts and CPU durations for recognized registration, model variant misses/builds and static capture PS creation. Warm success/failure cache hits add no cold clock reads. This measures CPU creation/patching, not later backend pipeline compilation. |

### AO and postprocessing

| Topic | Evidence, action and remaining limit |
| --- | --- |
| 11. AO generation and reconstruction | Ordinary Off AO generation is about 0.115 ms; all four existing AO methods retain their original shaders and samples. Ninety-six GPU method cases pass. No justification was found for lowering quality to treat the dominant scene stall. |
| 12. AO denoise/upsample bandwidth | An isolated 2.048 ms AOBlur interval previously included render-target/state preparation plus the draw. Added **AOBlurSetup / AOBlur GPU intervals** and paired **AO_SUBMIT CPU setup/submission timings**, only on recorded samples. Full filter/output is unchanged; 144 GPU comparisons remain exact. |
| 13. Full-resolution composition, bloom, water and HDR | WorldComposite remains approximately 0.542 ms and MaterialDetail is zero when globally Off. Shader arithmetic, bloom RGB, water rejection, formats and HDR handling are unchanged. No extra full-frame copy or lower-resolution substitute was added. |
| 14. Temporal reprojection, disocclusion and history | Ordinary yaw does not invalidate the lighting/fog histories. Existing reprojection and resource swaps remain intact. No unproved history reset or rotation-dependent shortcut was made. Existing AO rotation/stripe/texel and fog temporal wiring regressions pass. |
| 15. Fog/cloud/source-shadow costs and cold pipelines | A real moon remains active; its fog/cloud contribution is required. Fog/cloud costs are relatively stable. DXVK loads 1,660 cached shaders (about 18 MiB) and has eight compiler threads, but current logs do not identify per-frame pipeline creation latency. No backend setting or async draw policy was changed based on that hypothesis. |

## Exact implementation and evidence

Live terrain uses `StaticShadow::Vertex`, containing the original float32 position and original zero UV. The shadow shaders do not consume the old normal. A dedicated POSITION+UV declaration is created/released with the existing world resources and selected for both actual consumers. The previous world declaration is restored afterwards. GPU indices remain uint32, and the active capture cap remains 262,144 vertices. Arena limits remain 16 MiB normally / 8 MiB for the existing admission fallback; event-query retirement, unsupported-query fallback, index ordering, admission and failure behavior remain unchanged.

The pressure fixture compares 19 view rotations with about 203,000 active vertices, new immutable owners and map reset. Both busy-query and unsupported-query variants preserve exact point/directional geometry. Uploaded bytes fall 75.4%, reused vertices rise from about 0.893 million to 2.691 million, and rollovers fall from five to zero. This exceeds the per-vertex 37.5% reduction because more old owners remain resident. It is a controlled retention/upload result, not a predicted FPS gain.

The real D3D9 fixture compares old/new layouts through production directional, cached and point shadow programs. All **96** comparisons are bit exact, spanning MSAA 0/2/4/8, 2,823,807 covered pixels and output digest `915430334a5a6b5c`. Seven existing terrain/live-arena suites pass in O2/O0, including allocation/update failures, reset, fence-safe reuse, randomized index preparation and exact uint32 rebasing. Additional geometry-handoff, immutable actor, static page, dynamic path and stale-publication regressions pass. Unavailable sanitizers are not described as passed.

The oversized material regression proves zero height/UV work for 8,193 primitives, unchanged preparation at the 8,192 boundary and preservation of an already cached oversized entry. The actual UV evaluator costs 0.906594 ms for the synthetic 24,579-vertex source; the new rejected path makes zero UV calls. This is not the measured cost of the live WMO shader. The complete **100-check** GPU capture suite against `c5ef83c` passes, including animation, alpha, mutation, state restoration and MSAA.

AO profiling adds one timestamp mark per recorded sample and six possible lazy query objects across the six-slot ring (capacity 40 -> 41 marks). It adds no draw or flush/wait. **144** AO blur image/profile cases and **96** AO generation cases pass, covering production FP16 inputs, FP16/FP32 source cases, HDR RGB, odd/even grids and MSAA. The real query ring remains bounded/nonblocking; profiling Off adds zero AO timing clocks and recorded CPU timing uses four clock reads. All HLSL, compiled shader binaries and generated shader bytecode headers remain baseline-identical.

## Reading the new profiling

- `WORLD terrain ... vertexStride=20 vertexCapacity=...` exposes the new layout alongside uploads, reuse and rollovers. Compare these with active owners and visible coverage rather than FPS alone.
- `GI activated ... actorWorkMs=... solveWorkMs=...` adds sub-millisecond worker elapsed time. These durations can include worker preemption; they are not foreground frame additions or pure consumed CPU time.
- `MATERIAL_COLD ...` identifies creation counts/time, including registration while Off. It prints sampled frames and frames containing cold work; unsampled frame numbers follow the existing local material convention. Timings can overlap an enclosing capture stage and must not be added again.
- `GPU profile ... AOBlurSetup=... AOBlur=...` partitions the former AOBlur interval. `AO_SUBMIT frame=... blurSetupCpuMs=... blurDrawSubmitCpuMs=...` records paired CPU spans and is emitted after rendering so logging does not create a gap inside the GPU pass. The frame tag identifies the recorded GPU sample even if the line is emitted later.
- `CSTREAM QUERYFRAME gameFrame=... replayed=...` reports sampled query trips by type. `beforeReplayMs` includes enqueue/scheduling and prior queued work, `replayMs` includes the real backend poll/wait, and `afterReplayMs` includes completion/wakeup overhead. They partition `tripMs`; `syncWaitMs` overlaps this trip and must not be added. Game/replay frame counters are explicitly separate from renderer sample labels. The 600-frame aggregate carries the same breakdown.

Query diagnostics add four clock reads only to already synchronous query trips when diagnostics is enabled. Fast local replies add none of these new clocks. Actual HRESULTs, current-generation completion, the existing two-millisecond wait budget, polling gaps, message pumping and resource retirement are unchanged. O2/O0 tests check exact partition sums, pending/error/refused tasks, invalid clock order, reset and the 1,700-case polling sweep; real x86 SDK dispatch compilation passes.

## Build, reproduction and next measurement

The x86 release builds with the same 17 exports and the two existing warnings. The separate DLL package contains the English reports and input/build/test evidence. Installed DLL and settings are not replaced by the agent.

```powershell
python tests/validate_live_terrain_layout_gpu.py --backend D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll
python tests/test_displacement_state_reuse.py
python tests/test_query_poll_latency.py
python tests/test_ao_submit_profile.py
python tests/validate_ao_blur_equivalence_gpu.py --baseline-ref c5ef83c --profile-split --backend D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll
python tests/validate_material_deferred.py --baseline-ref c5ef83c --alpha-test --texture-guard --backend D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll
python scripts/build_renderer.py
```

For a comparable next run, keep the scene, camera radius, settings and rotation path the same, first with global materials Off. Correlate frame-time windows with terrain uploads/rollovers, precise GI stages, query-trip breakdown and cold shader events. A reduction in one component does not prove the complete rotation stall is gone; native scene rendering, legitimate world-region changes and cold backend dependencies remain distinct possible contributors. The new instrumentation is intended to resolve those remaining ambiguities without reducing the rendered result.
