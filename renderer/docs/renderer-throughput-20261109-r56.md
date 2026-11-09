# Renderer throughput r56 — shared input layouts and paired bounds

## Latest game run

Analyzed the r55 / DXVK 3.1.1 log frozen under `out/perf-20261109-r56/game.log`, SHA-256 `cdbd1f11a2d6501cbd3ac77bd65ec92d37974f5a3868ad0b0945320325a70662`. Installed source: `b7aff6a8cd23e19bf9b3a9c9936ac24275a94e26d69ff2f1a403601e360740ee`.

The final foreground windows average 30.963 / 27.222 ms (32.30 / 36.74 FPS). World entry includes a 2513.199 ms maximum. The scene/camera is not controlled against the earlier run, so these differences do not establish r55's FPS gain.

- Capture CPU is 6.436 / 5.458 ms at frames 508 / 635. Effects CPU is 2.869 / 4.085 ms.
- Frame 762 has 1854 pose-envelope calls / 1.0087 ms, 164 fallback calls / 0.1214 ms, snapshot 0.4389 ms, capture setup 0.6904 ms, native draw wrappers 1.0613 ms and deferred flush 0.6516 ms.
- r55 handoff is active: 18 / 41 / 53 current-pose range culls at frames 508 / 635 / 762. Receiver submissions are 627 / 633 / 611. All geometry is resident, with zero source reads, UP or streaming fallback.
- GPU timestamp intervals at frame 762: GameScene 15.852 ms, ModelNormals 2.279 ms, effects 4.361 ms. At frame 635 MoonFar and ShadowFilter each exceed 1.39 ms; at frame 762 ShadowFilter is 0.248 ms and RemovalSmooth 0.635 ms. These intervals include command-submission gaps and do not measure exclusive GPU busy time or utilization. CPU/GPU intervals overlap.
- Cold material VS preparation still reaches 0.676 ms, and displacement variants 1.1148 ms. Both allocation churn and repeated program preparation matter during source introduction.

## Cache findings

Native geometry charges 68,327,468 bytes against 96 MiB, without pressure trims in this run. The material source cache also reports zero read misses in the warm samples. Raising these geometry limits is not indicated.

Generated normal maps occupy 32,860,820 bytes / 29 entries against 32 MiB, with 123 cumulative evictions, 120 allocations and 32 reuses. Disk persistence reports 143 hits, 132 uploads and 20 generated-map copies. The 847 normal-build-stage attempts at frame 762 are not 847 GPU builds: the generator/mip stages do no work in that sample. Map admission, pending disk requests and capacity refusals still enter that stage.

The pose cache has 1100 source entries but only 17 prepared shader/declaration metadata records. Before this change, every source separately rebuilt and allocated the same vertex-input layout, even when it already shared an immutable program key. Sharing that existing metadata is preferable to another cache or a higher memory limit.

## Changes

Prepared envelope metadata now owns one immutable vertex-input layout. Source entries retain it after metadata eviction instead of reparsing declarations and allocating a vector for each mesh. Every source still validates topology, index count, vertex count, strides, offsets and available bytes before admission. Independently prepared metadata still requires the complete program/declaration key comparison. Generic callers retain the same validation path. Both metadata and source storage remain charged to their existing bounded budgets; shared storage is conservatively charged per source too.

The aligned skin kernel evaluates interval low/high endpoints together using the existing SSE2 path. Multiplication and K accumulation order, scalar first-operand min/max ties, error allowances, outward rounding and stop callbacks are preserved. The scalar fallback remains. There is no pose-result cache, animation throttling or geometry approximation.

The material producer also supplies its already compiled native-position program to the existing receiver-range classifier. This removes a second deformation-program compilation. Native bytecode analysis and projection/DEF restrictions remain the same. Other callers keep the original classifier entry point.

All changes use the common CPU path for DXVK 1/2/3. No backend policy, render pass, shader precision, AO setting, scene coverage or cache limit changes.

## Measurements and validation

For 512 immutable sources sharing a prepared layout, measured cold allocations fall from 2569 to 1033; allocated bytes from 1,269,920 to 1,226,912. Conservative source accounting increases from 1,796,096 to 1,812,480 bytes, still below the existing 8 MiB cap. Warm cache timing varies between trials; no reliable whole-cache warm speedup is claimed from that single-pair benchmark.

The direct 256-bound mixed 1/4/16/75-bone workload uses nine alternating reference/current pairs. Final O2 medians are 0.164109 / 0.143561 ms (reference / current, 12.5% lower); receipts are in `out/perf-20261109-r56/final-source-kernel/`. Sanitizer timings are excluded from performance claims. These are CPU components, not game FPS.

- O2 and ASan/UBSan compare 28,672 numeric/invalid-input/stop-callback cases per build against frozen r55, with bit-identical bounds and callback counts.
- Shared-layout tests cover independent keys, changed programs, metadata destruction, shortened/empty buffers, changed strides, duplicate declaration semantics and complete release.
- Displacement admission/state, both prepared/unprepared receiver-range classifiers, exact current material poses at O0/O2, and typed/multistream skinning checks pass.
- 24 SM2/SM3 program/backend range cases and six full weighted GPU cases pass on DXVK 1.10.3 / 2.7.1 / 3.1.1: exact capture pixels, MSAA, cutouts, source mutation, native state restoration, fallback and shader sharing. Six sampled lighting CSVs also match the retained r54 reference byte-for-byte.

The allocation fixture's lifetime audit now runs after its timing: it deliberately replaces the shared prepared key with an independent key. Timing after that replacement compared different lookup conditions, so those earlier warm timing comparisons are excluded.

## EVENT waits and limits

Foreground frame 566 accumulates 42.0337 ms waiting: 24.3802 before replay, 17.6348 in replay. Observed pause overshoot sums to 15.7611 ms, with a 0.5466 ms maximum pause. The overshoot sum is not removable frame time: GPU/backend work can progress during the pauses, and the log does not identify the instant completion became available. Startup frame 2 reaches 170.7087 ms. No completion is synthesized, and this iteration does not claim to eliminate EVENT waits.

No game control, installer, installed-client modification, subagent, publication or sleep operation is included. Production compiler preflight, shader freshness, PE32/export validation, all 779 final source hashes and 192 real EVENT completions through the final proxy DLL pass. The installed r55 DLL and latest game log remain byte-identical.

Production DLL: `out/dll-20261109-r56/d3d9.dll`, 10630656 bytes; SHA-256 `6cafc4738b1b025265d10aa3dcddf48de2b34245e2056abde4be5c99da837cd4`; source identity `a38d722dd348d3137fc5b5fb36ed8491774cdaca7eb8557766fda9d3ddb41776`, built 2026-11-08 22:30:15 UTC. Gameplay FPS and remaining EVENT pauses require a new actual game run.
