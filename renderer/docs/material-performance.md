# Material performance investigation — 22 October 2026

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
