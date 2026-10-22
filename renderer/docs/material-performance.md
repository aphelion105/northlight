# Material performance investigation — 22 October 2026

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
