# Renderer throughput r53 — skin bounds and map admission

## Latest run

Analyzed the r52/DXVK 3.1.1 run frozen at `out/perf-20261108-r53/game.log`, SHA-256 `d582d8df72fdb83dc74d14e13f2b840935148946856644bcdb06ca259fe86ba4`.

The foreground windows average 34.790 and 32.617 ms (28.74 and 30.66 FPS). Background/menu windows are not used as performance baselines. There is no controlled disabled-effects A/B in this run.

- The r52 receiver-program sharing is active: two created PS programs and seven shared sources by frame 635.
- Warm geometry is resident, with zero source reads and UP fallback. ModelNormals still submits 869 of 1204 logical draws at frame 635, and 713 of 987 at frame 762. Its GPU spans are 2.345–3.335 ms; GameScene spans are 15.053–18.206 ms. These timestamp intervals include submission gaps and cannot establish GPU utilization.
- Capture CPU is 7.465 ms at frame 635; effects CPU is 5.062 ms. Pose-envelope work costs 0.716–0.861 ms for 1359–1716 calls in warmed samples. Snapshot/setup and native draw wrappers are additional material costs.
- Geometry residency is not the same cache as generated material maps. The latter holds 33 maps / 33,495,692 bytes against its existing 32 MiB allowance. At frame 635, 797 normal-build-stage attempts accompany only 53 deferred material captures. Many map admissions do not produce a capture, but previously still saved replay state before discovering that result.
- Address-space availability reaches 502 MiB, largest free region 357 MiB, with no memory-guard trims. Raising the material-map allowance is not justified without revisiting total process residency; this iteration preserves the existing limits.
- EVENT startup total is 183.2746 ms, including 175.1837 ms before replay. Active frame 584 totals 27.2251 ms, including 21.5924 ms before replay and 5.5761 ms in replay. Those are queue/wait partitions, not exclusive CPU or GPU work. Long waits remain.

## Changes

1. The aligned skin-envelope kernel computes coefficient pairs with SSE2. It retains the scalar J/K accumulation order, literal values, error enclosure, final outward rounding, and exact stop-callback cadence. Eight-byte loads remain inside the palette through bone 74 / c255. The existing scalar fallback remains. No shader, geometry or culling approximation changes.
2. Material capture now resolves map availability after existing geometry, coverage and shader admission, before saving replay state or allocating private targets. The same operations are reordered; no new backend policy, cache layer, admission limit or runtime branch is added. Native displacement preparation and immutable geometry/pose handoff still happen before a map refusal. Deferred leases continue to protect maps across reuse and queued captures.

## Measurements

The mixed 1/4/16/75-bone CPU kernel benchmark, 256 bounds per synthetic frame, has an aligned median of 0.199916 → 0.161119 ms (-19.4%). Nine alternating reference/current rounds are used. The unaligned path is unchanged and measures 0.070911 / 0.070691 ms (noise).

The map-refusal component exercises 1000 valid draw candidates with either an exhausted generation quota or a map budget refusal. Each process reports the median of nine samples after two warmups; variants run separately. Native image, shader identity and constants remain exact.

| Backend | Quota refusal, reference/current | Budget refusal, reference/current |
| --- | ---: | ---: |
| DXVK 1.10.3 | 0.2087 / 0.1369 ms | 0.2405 / 0.1545 ms |
| DXVK 2.7.1 | 0.2141 / 0.1364 ms | 0.2792 / 0.1587 ms |
| DXVK 3.1.1 | 0.3485 / 0.1496 ms | 0.2385 / 0.1538 ms |

For each 1000 refusals, 1000 PS-constant reads and 8000 sampler reads become zero. Absolute component savings are small; these percentages must not be presented as whole-frame FPS gains. Neither benchmark measures GPU busy time or Mac performance.

## Validation

- 28,672 logical skin controls per build, covering both aligned/unaligned modes, random poses, 1–75 bones, invalid inputs and exact stop callbacks, pass against the frozen r52 kernel in O2 and ASan/UBSan.
- Current-palette/definition/source-generation reuse, budget failure and interleaved material-work controls pass.
- Immediate, deferred and 2x MSAA material capture compares against the frozen r52 admission path on all three DXVKs. The skin kernel is current in both material binaries; its equivalence is established independently above.
- Models and terrain retain exact normal/POM/displacement composition. Together with quota/budget miss controls, 159 output image files compare byte-for-byte. Missing-map native shaders/constants are restored, the native image is unchanged and no capture callback executes.
- Raw DXVK 2/3 do not advertise RESZ to this standalone harness. Their MSAA comparisons explicitly use the harness's existing raster-depth oracle. This does not validate production RESZ resolution on those versions. DXVK 1 uses its advertised RESZ path.

The new miss check is runnable through `tests/auto_normal_oblique_gpu.cpp` with `NORTHLIGHT_TEST_MAP_ADMISSION=1`; `NORTHLIGHT_TEST_RASTER_DEPTH=1` selects the explicit MSAA depth oracle where needed. Exact source hashes, commands, component data and logs are retained under `out/perf-20261108-r53/`.

Remaining priorities are the repeated geometry/normal submissions, native scene cost and other capture CPU work. The existing 32 MiB map policy also leaves significant admission churn, but raising it without aggregate memory evidence risks more address-space pressure. This iteration makes refusals cheaper without changing what the cache admits. EVENT and world-entry pauses are not claimed eliminated.

AO, render resolution, material precision, effect strength, deformation rules and lighting/shadow coverage are unchanged. No installer, installed-client changes, game control, subagents or repository publication are part of this iteration.

Production DLL: `out/dll-20261108-r53/d3d9.dll`, 10618880 bytes; SHA-256 `5cd24b09a6458fc08f475a0a9cb3ad4931f515aefbf2a9c6b6ed32aabf17e812`; source identity `66ef28be30dc8d931dc325a102a009760881c038365af8592f65d8823c557432`, built 2026-11-08 20:38:31 UTC. Compiler preflight, shader freshness, PE32/export validation, all 779 final source hashes and 192 real EVENT completions through the production command-stream proxy pass. The installed DLL and latest game log remain unchanged.
