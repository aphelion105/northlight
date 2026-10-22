# Material performance investigation — 22 October 2026

## New user run: world capture overhead and missing opaque bump — 22 October, 17:33

### What the fresh logs establish

The installed DLL matches the delivered `fb3ed48` build: SHA-256 `84a7caa183aae9e7e2997642f524b628a6f83497dec1da1105393e233de196a4`. The immutable snapshots are in `northlight-diagnostics/material-batch-live-20261022-1733/`; the new main log has SHA-256 `9296e189f90e29a5eee5647c6b242a22f13ba3a1faaea313d3072768cd1cdab4`. The main and previous logs both keep **M2 Off** (`modes=3/3/3/0`, also `AutoBumpModels=0` in the saved INI). Consequently, `MATERIAL_DEFERRED` reports `enabled=1 queued=0 flushed=0`: batching is permitted by the loaded DXVK 3 backend but is never exercised by M2. This run cannot establish its in-game speedup. Comparing it with the earlier M2-enabled run would conflate a code change with a different configuration.

With world materials enabled, warm sampled frame 3175 reports:

| Work | CPU exclusive (ms) | GPU interval (ms) |
| --- | ---: | ---: |
| Material constants, all domains | 0.327 | — |
| Material source snapshots | 0.211 | — |
| Bounds/pose checks | 0.366 | — |
| Capture setup, all domains | 0.419 | 1.088, for only 5 WMO captures |
| WMO capture submission | 0.0044 | 0.0777 |
| Original terrain draws, 811 | 0.598 | 2.727 |
| Original M2 draws, M2 materials Off | 0.312 | 0.224 |
| Final material composite | 0.0021 | 0.1587 |

Total measured material CPU work is **2.510 ms** versus roughly **1.07–1.54 ms** in later material-disabled samples. The shader/map cache is warm; displacement still builds and draws zero geometry. There are **969 rejected captures out of 974 successful range proofs**, with only five WMO replays remaining. The bounds cache has 891 entries and no new evidence of the former 1024-entry churn in this M2-disabled run. Expensive admission and target transitions remain even when practically no material pixels contribute.

The warm material-enabled windows at frames 3121/3241/3361 are **12.654–13.717 ms / 72.90–79.03 FPS**. Nearby disabled windows at frames 2641/2761/2881 are **10.803–11.234 ms / 89.02–92.57 FPS**. Menu, loading, focus-loss and GI warm-up intervals are excluded. This is an observational setting comparison, not a controlled before/after measurement of the new code.

The largest remaining original-scene interval in this sample is terrain rendering. Outside materials, the effect pass totals **3.134 ms**, including **FogMarch 0.847 + FogClouds 0.625 = 1.472 ms**, AO/blur 0.500 ms, and WorldComposite 0.547 ms. These are later optimization candidates; disabling those effects or reducing their quality is not part of this fix. GPU interval attribution can include transitions charged to a subsequent draw; CPU and GPU timings must not be added.

### Why bump can disappear

All six material-enabled 128×72 probes report `rawCaptured=0`, not merely rejected final depth. This is sampled coverage, not proof that every full-resolution pixel is empty; small coverage can miss the grid. It does show that the old normal-strength-only explanation is insufficient.

A definite coverage bug was reproduced in the existing WMO path. With native alpha blending and alpha test both disabled, native WMO RGB is visible even when vertex/diffuse alpha is zero. Capture nevertheless clipped or weighted its normals by that unused alpha. In an authentic WMO shader fixture at 320×200, the previous code captures **0 pixels**; the corrected code captures **37,894 pixels**. Original native color is **bit-exact**. The final normal-lighting difference rises from **5.96e-8 numerical noise** to **0.50935 maximum RGB difference**. This proves the defect and the correction. The fresh user log does not record source alpha, so it cannot prove that this defect is the only cause of its five empty-looking captures.

There is also a distance/configuration constraint: M2 is Off, the camera is about **47.6–48.8 units from the character**, and the normal range is 48. Normal detail has full weight only through 36 and fades to zero over 36–48. Character/material detail at that camera distance is therefore heavily faded or outside range. Cavity and self-shadow are still disabled; when enabled, they retain their common near range (8 in Quality). Distances, strength, POM steps, subdivision and MSAA have not been increased or reduced automatically.

The sampled audit now distinguishes scene coverage inside the normal range (`sceneNormalRange`), inside the full-weight region (`sceneNormalFull`), and inside the common occlusion range (`sceneOcclusionRange`). These complement raw capture and depth-mismatch counts. They will distinguish an empty near region from a capture-coverage problem in the next user run. All nine 128×72 queries remain asynchronous and bounded; production uses no GPU waits or full-target readbacks.

### Changes implemented and measured

1. **Reject distant rigid displacement draws before capture state setup.** The conservative range proof now also applies to rigid mode-3 draws. Buffered mode-3 probes reuse only already resident, write-tracked geometry snapshots from the existing geometry cache: a cold or untracked source follows the original preparation path. They perform no buffer locks, consume no new-source read budget, and create no duplicate source copies in the smaller optional range cache. UP draws retain their bounded position-only probe. Rejection uses the maximum of the effective capture and geometry ranges, preserving a geometry consumer even when normal range is shorter. Unknown, nonfinite and budget-limited inputs retain the original path. Skinned mode-3 draws retain their existing pose path rather than paying for a duplicate pose probe. A proof beyond geometry range also avoids full geometry preparation when only normal capture can still contribute.

   A real-device, admission-only comparison against `fb3ed48` measures **1.028003 → 0.496478 ms CPU per 1000 far draws**, averaged across 32 frames: **51.70% less admission CPU**. Pixel-shader and depth-surface queries fall from **32,000 each to zero**. Both variants perform zero GPU capture work. This is not a whole-frame FPS claim.

2. **Batch safe world captures as well as M2.** Terrain opaque and supported WMO opaque captures now use the same bounded immutable replay queue. A native opaque diffuse WMO draw uses the existing map-only capture because its unused diffuse alpha creates no live-texture dependency. Normal-map binding is stored per packet (world sampler 2, M2 sampler 14); counters retain the actual material domain. Native scene order, private capture order, native color shaders and MSAA are preserved. Prepared displacement, layered/alpha-dependent captures and unsupported state keep immediate replay. The previous 512-packet/8 MiB packet budget, 2 MiB new-source read budget and 4 MiB retained source cache remain unchanged.

   In the final isolated 4K/MSAA 2× WMO test, three alternating complete-pass rounds reduce median GPU time **17.583232 → 9.038208 ms (48.60%)**. CPU submission changes **0.093912 → 0.133500 ms (+0.039588 ms)**. Original color and normal/depth targets are bit-exact. The tested opaque-terrain pass similarly changes **17.809664 → 9.299584 ms (47.78%)**. These synthetic scenes use 32 native draws plus captures and color resolve, with occlusion disabled as in the user log. A concurrent validation/benchmark run timed out its diagnostic query and was excluded; the isolated WMO repeat is stable at 17.14–17.79 ms versus 8.97–9.15 ms. A user game run is still needed to establish actual FPS changes and queue utilization.

3. **Honor native coverage rather than unused WMO alpha.** Opaque WMO capture writes full coverage; alpha-test and blending continue to use their native alpha. Unneeded blending of the fully covered normal target is disabled. The coverage offset folds into the shared composite's existing multiply-add, keeping its strict **512-slot SM3 limit** without reducing quality. Alpha-test and alpha-blending regressions compare the old/new outputs bit-for-bit (13 and 8 outputs respectively). The intentional change is restored bump on opaque surfaces whose native alpha is unused.

### Validation and reproduction

- The CPU range/state suite passes, including a normal range shorter than geometry range, conservative boundaries, source replacement, pose budgets, nonfinite values, constant epochs and reset/lifetime handling.
- All **40 M2 shader/skin/combiner fixtures** pass; the normal/capture/native POM bytecode for M2 is unchanged. New world queue tests cover MSAA 0/2/4/8, immutable DISCARD snapshots, current matrices, changing scissor/fill state, immediate barriers, the 520-draw packet limit, unused queues, injected failure, memory pressure and complete asynchronous profiling. Native D3D9 world tests also pass with batching forced for compatibility; production still leaves batching disabled there.
- All **56 terrain/WMO/composite/grass MSAA/pressure cases** pass, plus **256 shared-POM comparisons**. Shared POM remains disabled by default. Only the WMO coverage capture programs and the diagnostic audit change shader bytecode. CPU/GPU benchmark timings are preserved separately from correctness comparisons.
- Prepare world fixtures with `python tests/validate_parallax_msaa.py --backend <dxvk-d3d9.dll> --output-subdir material-msaa-release`. Run `tests/validate_material_deferred.py --backend <dxvk-d3d9.dll> --family terrain --quick --baseline-ref fb3ed48 --benchmark --occlusion-off` for admission/terrain proof; use `--family wall --wall-diffuse --vertex-alpha-zero --quick --baseline-ref fb3ed48` for the missing-bump and native-alpha regressions. Use `--family wall --quick --benchmark --occlusion-off` for WMO timing, running GPU benchmarks without concurrent GPU workloads.

The final Windows x86 DLL builds successfully with the same 17 proxy exports and the two existing compiler warnings. SHA-256: `753b77e19fb7176e15892f900f2353cb974dbd5113b4d75c71d87f1895b8c760`. Windows CPU geometry/state/snapshot-policy tests and final terrain, M2, WMO alpha/MSAA/queue regressions pass. Additional native clang++/sanitizer snapshot scripts could not start because clang++ is unavailable on this host; they are not counted as passes. No new compiler warning was introduced.

No game process was launched and no game-root DLL or settings were replaced. The next user run should explicitly enable M2 Normals for an M2 batching comparison, retain the same camera for on/off timing, and include a close view of a WMO surface for the bump check. The new domain counters identify world batching separately from M2 batching.

## M2 capture bottleneck — 22 October, 15:18 user run (previous build)

The new log confirms that the shared material capture path is expensive even in normal-only mode. The installed DLL is `7c5c699`, SHA-256 `57a923a347c118496572fc06e40ed69c20b19a1f6753feb4d2342eb3939e232b`. The immutable input is `northlight-diagnostics/material-live-stages-20261022/northlight-renderer.log`, SHA-256 `79495013dbaa887454d768137ed37c99472cb0656b5f622d46ca2a83c488fa60`. This is a 3840×2160, MSAA 2×, DXVK 3.1.1 run. `parsed-stages.json` and `warm-stage-summary.json` preserve the per-frame evidence. No game process was started during this work.

### Where the frame time goes

These are warmed samples with **zero newly created profiling queries**, not cold shader/map creation:

| M2 setting / sampled frame | Material CPU, all domains (ms) | M2 capture draw GPU (ms) | Original M2 draw GPU interval (ms) | Final material composite GPU (ms) |
| --- | ---: | ---: | ---: | ---: |
| Displacement + normals / 2286 | 5.773 | 1.786 (332 draws) | 2.066 (510 draws) | 0.158 |
| POM / 2921 | 4.185 | 1.890 (348 draws) | 2.231 (510 draws) | 0.159 |
| Normals / 3175 | 3.662 | 2.144 (326 draws) | 2.039 (510 draws) | 0.159 |
| Off / 3302 | 2.366 | 0 | 0.190 (510 draws) | 0.160 |

The normal-only frame excludes POM and displacement math as the primary explanation. Its native pixel shader is unchanged, yet its original-draw interval is about 1.85 ms longer than with models off. Repeated transitions between the MSAA scene and the single-sample material targets interrupt the native pass; a backend can charge transition work to the following draw. Individual rows do not identify a shader's isolated arithmetic cost. The complete-pass experiment below tests the transition hypothesis directly.

CPU work is also measurable. At frame 2286, bounds/pose evaluation costs **2.028 ms**, source snapshots **0.804 ms**, capture setup **0.893 ms**, capture submission **0.260 ms**, and restoration **0.253 ms** across all domains. At frame 3175 those stages cost **0.695 / 0.300 / 0.758 / 0.212 / 0.209 ms**. Normal-map generation, mip generation, height extraction, subdivision and geometry uploads execute **zero work** in these samples. Sampled query submission itself costs about 0.52–0.57 ms in the active-model samples; profiling is sampled, not performed every frame. CPU and GPU intervals overlap and must not be added.

The observed normal-only frame window is **15.491 ms / 64.55 FPS** at frame 3240. Later model-off windows are **10.733–11.282 ms / 88.64–93.17 FPS** at frames 3480/3600/3720. Displacement-mode windows are roughly 16–17 ms; POM windows roughly 15–16 ms. Focus-loss and large-stutter windows are excluded from this comparison. These setting toggles corroborate the slowdown, but are not a before/after test of the new DLL.

### Are the effects working?

- **Cavity and POM self-shadow are disabled in this user run.** Every configuration reports `cavity=0.000 selfShadow=0.000`; no occlusion target is published. Their zero visible-pixel counts are expected and cannot establish a broken shader.
- **Bump is executing but has almost no confirmed visible contribution in this scene.** Of 17 regular-grid probes, 15 report zero valid samples; the other two report only 3–4 valid samples and 3 changed normals. This 128×72 grid is not a full-resolution visibility measurement. Range clipping or occlusion may explain the result; the old probe cannot distinguish them from a depth mismatch. The camera is roughly 49 units from the character and the configured normal range is 48, which is relevant context, not proof of the rejection cause.
- **GPU displacement is supported but executes zero height-fetch draws and builds zero geometry in this run.** The 8-yard model geometry setting cannot be blamed for tessellation that did not occur.
- Independent production-shader fixtures confirm that all three effects can change the image near the camera: maximum RGB differences are **0.2668 normal / 0.1327 self-shadow / 0.01060 cavity**. At a tested distance of 12 yards with an 8-yard common occlusion range, cavity and self-shadow differences are exactly zero while normal detail remains visible. These are test-scene results, not evidence that the effects were enabled in the user's session.

### Implemented changes and the evidence for them

**1. Group eligible M2 captures after native scene rendering on the actual DXVK 3 runtime.** The existing capture shaders and single-sample material targets remain in use. Original game draws keep their order and MSAA settings. Eligible captures retain their original order within the private pass, instead of switching render targets for every model. The queue is drained before an intervening immediate capture and before final material consumption. Unsupported or prepared-displacement draws retain immediate capture; native D3D9, DXVK2 and non-MSAA paths retain it by default.

The final alternating, three-round, complete-pass benchmark includes all 32 native draws, all captures, state snapshots/restoration, lazy clears, queue recording, and MSAA color resolve at 4K/MSAA 2×. Median GPU time changes from **7.702400 to 3.997312 ms (48.10% reduction)**. Median CPU submission changes from **0.063875 to 0.102362 ms (+0.038487 ms)**. Original color, captured normal/depth, and captured cavity/occlusion targets are **bit-exact**. `deferred-release-dxvk.json` preserves every measurement. This establishes a useful synthetic pass improvement; it does not establish the game's new FPS.

A separate run matching the user's **normal-only, cavity/self-shadow disabled** configuration also passes. Its three alternating 4K/MSAA 2× rounds measure median **17.683584 → 9.405696 ms GPU (46.81% reduction)** and **0.065388 → 0.121262 ms CPU (+0.055874 ms)**. Both original color and captured normal/depth are bit-exact; no occlusion target is allocated. These absolute values belong to a separate benchmark configuration and should not be compared directly with the auxiliary-target benchmark above. Raw records are in `deferred-no-occlusion.json`. Add `--occlusion-off` to the benchmark command to reproduce it.

Each queued draw owns an immutable mesh snapshot, current vertex/bone constants, required pixel constants, integer/boolean constants, raster/scissor/viewport state, shader/declaration references and a generated-map lease. Captures that read other live native textures, vertex textures, multiple streams, user clip planes, N-patches, or unsupported topology use the original path. Limits are **512 packets / 8 MiB charged packet-plus-source payload per batch, 2 MiB new source reads per frame, and 4 MiB retained source snapshot cache**; the existing index cache also remains bounded. Pressure disables queueing and releases pending work, its source/index caches and cached declarations. A failed batch hides incomplete material outputs and retains native rendering. Unconsumed queues are discarded at frame end without capture draws or target clears.

**2. Preserve scissor state across immediate capture too.** Render-target changes reset the D3D9 scissor rectangle. The former immediate replay could widen the game's subsequent native draw and the capture region. Both paths now preserve the authored rectangle; private target clears cover the full target before per-draw scissor is restored. A regression alternates solid/wireframe rendering, scissor rectangles, buffer DISCARD writes and current bone palettes, then compares all three targets and complete VS/PS float constant banks.

**3. Stop tiny bounds-cache records from thrashing at 1024 entries.** The log reaches 1024 entries / **65,536 bytes**, despite its 8 MiB byte budget. Repeated misses accompany millions of rescanned vertices: 8.28 million cumulative object scans by frame 2400 and 22.29 million by frame 3000. The count cap increases to **4096**, with the byte cap unchanged and weak source ownership preserved. A 1536-source regression now sustains **6144 warm hits with zero new misses**, uses less than 256 KiB, and still enforces entry/byte limits and address-reuse safety. A matching game run is needed to quantify its CPU benefit.

**4. Expose actual visibility loss.** The sampled audit adds `rawCaptured` and `depthMismatch`. `valid`, `normalChanged`, `cavity` and `selfShadow` retain their meaning. Raw coverage with rejected depth can indicate ordinary occlusion or a mismatch; it does not prove a depth bug by itself. The six 128×72 queries use a three-slot asynchronous ring and `GetData(..., 0)`, with no production GPU wait or full-target readback. Only the diagnostic `MaterialAudit` shader bytecode changes; rendering/capture shader bytecode remains unchanged.

**5. Attribute queue costs explicitly.** `deferredRecord` measures CPU snapshot/state recording; `deferredFlush` measures the complete batch GPU interval, including transitions/restoration. Its child `captureDraw` rows provide CPU submissions without overlapping GPU timestamps. `MATERIAL_DEFERRED` reports queued/flushed draws, source/state fallback, prepared geometry and unused discards on sampled frames. The next user log can therefore show whether batching was actually used and whether source budgets or live-state dependencies force frequent barriers.

### Validation and remaining check

DXVK: all **40 M2 shader/skin/combiner fixtures** compare 14 outputs each bit-for-bit, plus MSAA 0/4/8 comparisons, DISCARD-and-palette mutation, changing scissor/fill mode, immediate/deferred barriers, a 520-draw packet-limit case, unused queues and injected replay failure. Native D3D9 passes the weighted four-bone fixture and the same MSAA/safety cases with queueing forced for compatibility tests; queueing remains disabled there in production. The **56-case terrain/wall/composite/grass MSAA/pressure matrix** passes. CPU regressions cover bounds ownership/accounting, geometry/snapshot/upload correctness, state/constant epochs and nonblocking profiling. The shader gate recompiles all world entries; only `MaterialAudit` bytecode differs.

Reproduce with `python tests/validate_material_deferred.py --backend <dxvk-d3d9.dll> --benchmark`, `python tests/validate_material_stages.py --backend <dxvk-d3d9.dll>`, `python tests/validate_parallax_msaa.py --backend <dxvk-d3d9.dll>` and the material CPU tests. Fixture prerequisites are documented by those runners. Build with `python scripts/build_renderer.py`.

The Windows x86 DLL builds successfully, retains all **17 proxy exports**, and is **5,481,984 bytes** with SHA-256 `84a7caa183aae9e7e2997642f524b628a6f83497dec1da1105393e233de196a4`. The build emits the same two existing shared-POM/height-texture warnings. Injected memory pressure also passes with a populated source cache, and the live GPU profiling fixture reports complete coverage including `deferredFlush` and all six visibility counters. The installed game DLL remains unchanged; a separate test package is produced.

A new user game run must confirm actual queue utilization, the complete native/capture GPU delta, bounds misses and visible coverage. Shared POM remains disabled by default. No distance, strength, subdivision quality or MSAA setting is reduced by this change.

## Material stage diagnostics and near-only occlusion — 22 October, 13:48 user run

The installed DLL is the delivered `1f98a0b` build: SHA-256 `c82ea36ef2aff004e28510ffdfabdb8fcd0e42158a2baea787983139e77ed479`. The new immutable log is `northlight-diagnostics/material-stages-20261022/` (input SHA-256 `8e7d1a79ac1afe48be6afaa833d2f2ae6621ec0ec20e034b8e726e5673ab6cf6`). `inputs.json`, `log-index.json` and `frame-windows.json` preserve source identity, setting changes and matching camera windows. The game was not launched during this investigation.

### What the latest game run establishes

- At the same logged camera near the character, model **normal-only** mode reports 15.116 ms / 66.15 FPS at frame 1800, versus 10.386 and 10.944 ms / 96.28 and 91.38 FPS with models off at frames 1440 and 1560. This is an observational toggle, not a controlled benchmark of two DLL versions.
- At the later stable camera, roughly 49 units from the character, model normals report 14.433–14.787 ms, whereas model-off windows are roughly 10–11 ms. Other material domains remain active. Turning AutoBump off later reduces the interval to roughly 8.7–9.1 ms.
- GPU displacement is supported but still executes **zero height-fetch draws**, builds zero meshes and retains zero geometry entries. About 301–353 model captures per frame remain. Mode 1 does not run POM. Optimizing POM or subdivision alone cannot explain or remove this normal-only cost.
- GPU GameScene is roughly 7.5–8 ms with materials versus roughly 2.5–3 ms globally off; final MaterialDetail is only about 0.177 ms. CPU capture samples are roughly 3.6–3.7 ms versus 1.6–1.8 ms. These overlapping CPU/GPU intervals must not be added.
- Bounds counters stop rescanning stable vertices once warm, but approximately 970 far draw admissions per frame still occur in the later windows. The previous cache fix addresses repeated source scans, not the cost of hundreds of native/capture transitions.

The old log has no reliable per-stage normal/POM/cavity times. Those values cannot be reconstructed from the aggregate timings. The new instrumentation supplies that missing evidence in the next user-run log.

### Changes implemented

`AutoMaterialOcclusionDistance` gives cavity and POM self-shadow one near range: Quality **8**, Balanced **6**, Performance **4**, editable from **2 to 16**. Both contributions fade over the final max(range / 4, 1 yard). Self-shadow also retains POM's own range and requires POM mode and an active directional light; displacement mode does not implicitly enable it. Normal mapping retains its independent range (48 by default).

The capture shader branches around cavity/self-shadow outside the common near range. The final compositor also avoids their sampling outside that range. Proven far draws do not bind/write the occlusion MRT. Its target is allocated and cleared only on actual near demand, and no stale occlusion texture is exposed on a frame with no successful occlusion draw. If normal gain is zero, capture follows only the effective near occlusion consumers rather than the normal range.

Buffered skinned normal/POM admission now reuses the existing conservative skin-envelope evaluator with the **current** palette. It can reject a whole far capture and its native POM replacement before normal-map generation or render-target changes. It does not skin every vertex on every draw. Rigid admission retains position-only snapshots. Unknown programs, UP skin draws, non-finite data and exhausted budgets fail open, preserving the existing shader distance checks. Consequently, unsupported far draws can still incur admission/normal replay and neutral MRT writes; there is no claim that all distant draw overhead has vanished.

Optional rigid and skin admission have independent 512/256 KiB frame-read budgets; they cannot spend displacement's 2 MiB allowance. Retained payload caps remain **32 MiB combined** (24 displacement + 4 rigid range + 4 skin range), or **8 MiB** under pressure (6 + 1 + 1). Skin envelope work retains its existing bounded cache/time policy. Separate certified constant banks preserve displacement pose reuse.

### New CPU/GPU evidence in the log

Enable the existing `Diagnostics=1` and `RenderProfile=1`. Every 127th frame records the actual material work, grouped by domain (1 terrain, 2 walls, 3 grass, 4 models) and mode (0 off, 1 normal, 2 POM, 3 displacement).

| Stage | What is measured / why it matters |
| --- | --- |
| admission | Recognition, coverage and contribution decisions; candidate/range rejection counters |
| constants | Reading current pose/material constants; detects driver-call overhead |
| snapshot | Immutable geometry reads, vertex/item counts and bytes |
| boundsPose | Conservative bounds, cached envelopes and pose fallback; scan/pose/budget counters |
| heightRead | CPU height acquisition and inference inputs |
| uv | Original UV evaluation for displacement |
| subdivide | CPU topology and mesh construction |
| geometryUpload | Private vertex/index/height upload submission on CPU |
| normalCache | Lookup, hit/miss and map acquisition |
| normalBuild | CPU setup plus a GPU interval around actual normal/height/cavity map generation |
| mips | GPU interval around actual mip generation |
| captureSetup | CPU setup and GPU command interval around capture state/target changes |
| captureDraw | Actual GPU material replay: normal/POM/cavity/self-shadow are fused inside its shader |
| captureRestore | CPU restoration and GPU command interval around restoring the main targets/state |
| pomSetup | Native POM shader preparation and binding |
| nativeDraw | Actual original/POM/displaced scene draw, also classified on sampled material-off frames |
| finalApply | Full-resolution material relighting |
| sharedPOM | Experimental UV-sharing preparation, zero when disabled |
| visibilityAudit | Explicit diagnostic cost of the small visibility probe described below |

`MATERIAL_STAGE` includes all stages, even zero-work ones. `MATERIAL_CPU` provides domain/mode rows with exclusive and inclusive times; nested CPU time is not double-counted. `MATERIAL_WORK` supplies work counters. `MATERIAL_CONFIG` and `MATERIAL_APPLY` record ranges, gains, mode and lighting context.

`MATERIAL_GPU` uses asynchronous timestamp pairs surrounding **actual work**, never diagnostic replay of the material draws. Three bounded slots and at most 4096 spans per sampled frame prevent unbounded query growth. GetData uses flags zero: no flush, spin, sleep or GPU wait. Disjoint/error/reset handling retains CPU profiling. `MATERIAL_GPU_COVERAGE` distinguishes complete and partial samples. Timings include GPU stalls/command starvation, not just shader ALU. Query creation/issue CPU overhead and cold query counts are logged separately; cold samples should not be used as steady-state frame-rate comparisons. Profiling disabled performs no clock reads or extra diagnostic draws.

The fused pixel shader cannot truthfully yield independent normal/POM/cavity/self-shadow timestamps. Compare equal-camera, warmed A/B settings using the logged configuration and visible coverage. RT transition costs can also be charged by the backend to a following draw, so inspect complete intervals as well as individual rows.

### Does the effect reach visible pixels?

`MATERIAL_PIXELS` uses four asynchronous occlusion queries on a **128 x 72 regular grid**, only on sampled profiling frames. The production depth-validity test distinguishes a valid captured material from a changed normal and non-neutral cavity/self-shadow factors. This is sampled coverage, not an exact full-resolution pixel count or proof of a large final brightness difference. The probe uses a private tiny target, restores state and is timed as `visibilityAudit`; it does not read back a 4K image or stall the render thread. Unsupported queries disable this diagnostic alone.

On the actual DXVK MSAA 2x fixture, 5173 grid points have valid capture, roughly 5165 have changed normals, 1944 have cavity attenuation and 44 have self-shadow attenuation. Off modes report zero corresponding coverage. Both effects can work while being visually subtle: cavity modifies ambient contribution, self-shadow modifies the selected directional source. A small source contribution, weak curvature, unsuitable light angle or a small near footprint can hide the change. These fixture results do **not** prove that the user's particular game scene is receiving the effects; the next game's coverage/configuration rows resolve that.

Initial comparison testing exposed a fixture error: raw DXVK had not received the renderer's AMD identity override, which its RESZ depth resolve requires. Matching the production configuration restored visible material effects. The assertions were not weakened. Native D3D9 independently confirms nonzero near effects. At a translated far view, both cavity and self-shadow produce **exactly zero RGB difference**, while normal mapping remains visible.

Demonstration images live in the same diagnostic directory. They show equal-exposure OFF/ON pairs and explicitly labelled amplified difference/attenuation masks. A synthetic coarse masonry texture with 100% strengths still produces a modest final image change. Its stored self-shadow attenuation peaks near 25%, cavity near 11%; the final image changes much less because these attenuate selected lighting terms. No artistic mockup is presented as a rendered result.

### Validation of the delivered change

The x86 DLL builds successfully. Both backends pass 56 terrain/wall/composite/grass cases covering MSAA 0/2/4/8 and allocation-pressure fallback, plus 40 M2 shader/skin/combiner cases each. The dedicated near/far and coverage test passes on both backends, including a current-palette far/near/far sequence and an actual far capture rejected without publishing material targets. CPU tests cover query failures, saturation, disjoint/reset, disabled clock cost, state restoration, constant epochs, independent budgets, settings transport and menu ABI. Two existing compiler warnings remain in shared POM initialization and height-texture indentation; no new compile errors or warnings were introduced.

### Major refactor evidence and boundary

A paired **3840 x 2160, MSAA 2x, 32-draw** fixture compares current native/capture interleaving with grouping the identical native draws before identical captures. Color, captured normal/depth and occlusion buffers match **bit-for-bit**. Three alternating rounds give median GPU intervals **7.673728 ms interleaved vs 3.712000 ms grouped**, a **51.63%** reduction for that workload (`batch-timing.json`, `batch-equivalence.json`). This supports target/pass transitions as an optimization candidate; it is not a measured game FPS improvement.

The grouping experiment does not yet include the cost of retaining arbitrary live game draw state and immutable vertex/index data. Retaining COM buffers alone is unsafe because the game can rewrite them before replay. A production deferred-capture refactor therefore remains gated on a complete packet-retention benchmark and the new live stage/coverage evidence. The experiment is retained as a reproducible test, not silently enabled in the DLL. The immediate change removes proven unused far occlusion work and makes the remaining cost attributable.

## New DLL user run at 12:52 and M2 follow-up

The latest run loads the delivered f8a9a65 DLL, SHA-256 `de66ac3dc4a586d1d2dde953f04c5fe3540b4d388b4e656dc09896b1b02de9d5`. Evidence is preserved outside Git in `northlight-diagnostics/m2-performance-20261022/user-run-new-dll-1252.log` and `new-dll-log-index.json`. Resolution is 4K, MSAA 2x, HDR enabled, DXVK 3.1.1 / RTX 4090. The same logged camera gives:

| State | Selected window end frames | Mean frame time | FPS from mean |
| --- | --- | --- | --- |
| M2 Displacement + normals only (`0/0/0/3`) | 1320–1920, six windows | 14.182 ms | 70.51 |
| All material domains disabled | 2160–2640, five windows | 9.078 ms | 110.16 |

These are observational toggles, not a controlled comparison with the previous build's different camera. CPU capture is approximately 2.1–2.3 versus 0.6–0.7 ms; GPU GameScene approximately 6.2–7.0 versus 2.1–2.9 ms. Final GPU MaterialDetail is only 0.176 ms at frame 1524. CPU/GPU timings overlap and cannot be summed. No separate normal/cavity/POM GPU timings are available in this log.

GPU displacement is supported but reports zero draws/builds/cached meshes. Around 306 model material captures still run per frame. The camera is approximately 19–20 units from the character, outside model geometry's 8-unit radius. Mode 3 does not run POM or its self-shadow; normal/cavity remains active to 48. During M2-only operation, the earlier unused object-bound scans remain zero. Enabling world displacement later reaches 108,037,640 scanned object vertices by frame 4200, exposing a separate bounds-cache churn problem.

This follow-up changes:

- Bounds retain weak source ownership, validate control-block identity against address reuse, and charge metadata rather than complete source meshes. Limits remain 8 MiB / 1024 bounds. A 32 MiB source set retains 64 bounds in less than 64 KiB and passes 640 warm lookups; an independent old/new control reproduces the old unwanted source retention.
- Generic posed-vertex fallback reuses the existing validated declaration bindings once per draw. Pose evaluation, geometry, fade and subdivision remain unchanged.
- Rigid M2 normal/POM captures use the existing conservative early distance admission. Capture follows its active consumers: normal/cavity uses normal range; self-shadow-only capture uses the smaller of normal and POM range. Native POM and displaced native geometry remain independent.
- Logs add bounds hit/miss/entry/byte counters.

A separate no-POM capture specialization was evaluated and **not retained**. Although its prefix falls from 271 to 93 static slots, five alternating complete-pass 4K/MSAA 2x rounds give medians of 0.374432 ms for the original and 0.373632 ms for the specialization (0.21%, within the run-to-run spread). This does not establish a useful speedup. An unrestricted variant also changed one native D3D9 fixture by one FP16 depth step and 0.000073 in color; retaining the original shader on displaced geometry restores exact comparisons. That guarded experiment passes 3840 paired outputs across both backends and all four MSAA settings, plus 3584 alpha/texkill cases, but adds no demonstrated performance benefit. Its source, patch and results remain outside Git in `northlight-diagnostics/m2-performance-20261022/normal-capture-experiment/`. The shipping change keeps all existing shader bytecode and interfaces unchanged; tolerances are not relaxed.

### Which distance should capture use?

Capture replays geometry into normal/depth/coverage and occlusion targets before the native color draw; final lighting consumes them later. It is not a free copy. Normal mapping tilts lighting normals; POM shifts UVs through a height-field raymarch; displacement moves real rasterized vertices; cavity modifies ambient lighting using cached inferred detail.

Capture must end when none of these captured effects contributes. The configured model geometry radius is 8 units, fading over 6–8. Normal/cavity is still visible to depth 48, fading over 36–48; POM is visible to depth 16, fading over 9.6–16 plus minification fade. Geometry uses spherical camera distance; normal/POM use view-space depth. Setting all limits to 8 would remove currently visible normal/cavity detail. No such quality setting is changed here.

GPU distance/depth rejection can occur after CPU setup and vertex work. Animated models require conservative current-pose bounds; unknown or budget-limited proofs retain rendering. Objects crossing the boundary retain their near portion. General occluded-object elimination is not guaranteed. Cached data may be shared by visible instances and is retained within budgets to avoid camera-turn rebuilds; retention is not per-frame shading. Bounds no longer retain full source payloads solely to remember six numbers.

### Reproduce this follow-up

Run `tests/test_displacement_state_reuse.py`, `tests/test_material_geometry.py` and `tests/validate_model_materials.py --backend <d3d9.dll>` on DXVK and system D3D9. Build with `python scripts/build_renderer.py`. The state regression covers effective ranges, conservative boundary crossings, source lifetime/address reuse and cache accounting. Model fixtures cover 40 shader-model/skin/combiner combinations with actual geometry, animation and binding/constant restoration. The separate experimental comparison runner and its shader sources are preserved in the diagnostic snapshot above; its results are not attributed to the shipping shader path.

The final shipping follow-up passes the CPU geometry/snapshot/upload and range/state/cache regressions, and all 40 model cases on each backend (80 total). Windows x86 builds with the same two existing warnings. All existing shader bytecode is unchanged. Tests operate offscreen; the game is not launched and the installed DLL is not replaced. A matching user game run is still required to establish its FPS effect. Shared POM remains disabled by default.

## Previous investigation at 11:02

The latest user-run log still shows a large material cost without displacement output. This code-only continuation removes unnecessary CPU work and capture setup. A shared native/capture POM implementation passes image comparisons, but complete-pass timing exposed a net regression, so it remains an explicit profiling opt-in. The game was not launched and no DLL was installed. **No new in-game FPS or 120 FPS result is claimed.**

## Fresh log evidence

The user run ends at frame 5519, with the log last written at approximately 11:02. It is preserved as `northlight-diagnostics/goal-120fps-20261022/latest-user-run-1102.log`. Selected focused windows from this run have a stable logged camera:

| State | Window end frames | Mean interval | FPS from mean interval |
| --- | --- | --- | --- |
| Materials on | 3000–3720 | 14.422 ms | 69.34 |
| Materials off | 3960, 4080 | 9.231 ms | 108.34 |
| Materials restored | 4320–4680 | 14.552 ms | 68.72 |
| Shadows/cavity off | 4920, 5040 | 14.372 ms | 69.58 |
| Shadows/cavity restored | 5280, 5400 | 14.650 ms | 68.26 |

These observational toggles implicate material capture/preparation beyond shadow/cavity taps. They do not isolate one algorithm's cost. The older DLL does not log every mode-only live edit, and CPU/GPU/query timings overlap. Exact windows and source lines are in `latest-user-material-analysis.json`.

At frame 5400, GPU height reports `supported=1 draws=0`; displacement has no builds or cached geometry. Pose-budget refusals reach 659,651, envelope hits 184,317 and fallback vertices 28,238,142. Snapshot budget refusals stopped at 11,608 after warm-up. The normal cache is stable at 85 entries / 41,899,268 bytes with 549 lifetime generations. Capture still processes 104 terrain, 47 wall and 274 model draws into 3840×2160 targets. Nearby timed CPU capture samples are 3.8–4.6 ms; GPU Scene is approximately 7 ms and Effects approximately 3 ms. Do not sum these overlapping samples.

## Five priorities and changes

1. **Bound rejected model work.** Generic skinned displacement built, scanned and retained object-space bounds before checking the pose budget, although its exact posed-vertex path never consumed those bounds. The unused work is removed. A 9000-vertex rejected draw now performs zero object-bound scans, retains no bounds and evaluates no poses. Running the new regression against the previous bounds logic fails. Exact skin envelopes, current-pose evaluation, near fade and subdivision retain their previous behavior.
2. **Reject capture with no contribution.** Move existing RGB-write/depth-write eligibility ahead of shader lookup, geometry reads and capture setup. Skip capture when normal strength, cavity and applicable parallax shadows contribute nothing, retaining native POM and actual geometry displacement. Release unused targets. All modes off makes the material pipeline inactive. This does not infer visibility for arbitrary occluded geometry.
3. **Avoid unnecessary opaque blending.** Terrain base capture and opaque model capture produce alpha exactly one. Their capture targets overwrite without blending. Fractional layers and cutouts retain their previous blend/alpha behavior. Native scene blending and MSAA stay unchanged.
4. **Share POM only when it pays.** A triangle-stamped, full-float UV cache lets the native pass reuse capture's raymarch. Image/state tests pass, but initial 4K MSAA 2x complete-pass medians worsen by approximately 32% for terrain, 30% for walls and 7.5% for composites; grass is approximately flat. After removing opaque blending, terrain remains about 15.7% slower (0.499 to 0.577 ms). Reducing the shared prefix from 185 to 172 static slots still gives 0.501 to 0.596 ms for terrain and 0.822 to 0.860 ms for composites in a subsequent three-run median. A 720p check also does not establish a win. Reuse is therefore **disabled by default**; `NORTHLIGHT_EXPERIMENTAL_SHARED_POM=1` enables profiling. These are warm standalone fixture intervals, not game frame times. Further work should reduce intermediate bandwidth or merge shading while preserving coverage, instead of enabling a measured regression.
5. **Make attribution observable.** Logs now include actual material modes, POM depth/range/steps, object-bound scan counts, early capture rejection counts and shared-POM production/consumption/cache bytes. Retain the separately validated adaptive EVENT-query waits. Wait duration is a dependency, not a removable CPU kernel.

## Shared POM contract

The existing single-sample material capture remains separate from the MSAA scene. Capture stores UV0 and a draw/triangle stamp in an RGBA32F target, plus RG32F UV1 for supported layered materials. Native point lookups reuse full-float UVs only for the matching triangle. Overlap, uncovered MSAA centers, alpha holes and erased/stale stamps run the unchanged raymarch. Original diffuse gradients and native alpha/texkill remain intact. Both passes use the same tagged vertex program and privately expanded triangle attributes; no subdivision or quality setting is reduced.

Unsupported/centroid/partial-precision UV declarations, occupied TEXCOORD6 or sampler slots, SM2 vertex programs, insufficient MRT/format/blending support, pressure, shader failure or budget exhaustion retain the original path. Generated-map identity and sampler addressing must match. Reads are limited to 512 KiB/frame, retained snapshot payload to 8 MiB, expanded/source entries to 24 MiB / 256 entries, and uploads to two builds / 2 MiB per frame. Entries used this or the previous frame are protected against churn. At 4K extra UV targets cost about 126.6 MiB, or 189.8 MiB for dual UVs; allocation is bounded to 192 MiB and requires 256 MiB virtual-address headroom. Default operation allocates none of these targets.

## Reproduction and validation

- Build Windows x86 with `python scripts/build_renderer.py`; output stays in the checkout.
- Run `python tests/test_displacement_state_reuse.py` for rejected-pose work, early capture gates, POM-only operation, exact triangle expansion and distance/epoch/source/budget/resize/lifetime contracts.
- Run `python tests/test_material_geometry.py` for subdivision, snapshots and uploads.
- Prepare client fixtures with `python tests/validate_parallax_msaa.py --backend <plain-d3d9.dll> --output-subdir shared-pom-msaa`.
- Run `python tests/validate_shared_pom.py --backend <dxvk-d3d9.dll>` for actual hit/miss masks, default-disabled behavior, shader-failure fallback, partial holes, stamp wrap, UP/indexed draws, sampler mismatch and POM-only operation. Terrain/wall/composite/grass images are compared at 1x/2x/4x/8x. The runner explicitly enables reuse.
- Use `--color-only --output-subdir shared-pom-native` on system D3D9. Production images/state are checked there; its driver rejects the separate diagnostic hit-mask shader, so direct hit/miss pixel counts are DXVK evidence only.
- Run `python tests/benchmark_shared_pom.py --backend <plain-d3d9.dll>` for capture plus native shading, including extra MRT writes, clears and state changes. The runner alternates paths and reports raw intervals and medians. Timestamp intervals can include submission starvation; neither these results nor earlier kernel-only timing establish game FPS.

Preserved evidence includes paired backend validation JSON/logs, 4K timing runs, CPU regressions, the rejected-pose negative control and build output. The earlier 56-case MSAA/pressure matrix is retained separately. Final shared-POM comparisons cover 256 color outputs per backend, plus default/failure/addressing/POM-only checks. The final model matrix covers all 40 shader/skin/combiner variants, including animation and complete vertex-constant/binding restoration. The integration PR remains draft pending a matching full-quality game run and representative visual checks, deferred under the code-only instruction.
