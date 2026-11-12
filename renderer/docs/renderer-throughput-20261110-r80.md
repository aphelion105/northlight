# r80: shared unshadowed lighting/GI and cheaper profiling

Baseline: `3e1ad198f7b7790a02bc26f0bf46e37fe9adc72a` (r79).
Upstream main, fetched again for this run:
`e19ffee601acbad2b991917109bcb6d8b2f7d5ca`.
Build source identity:
`cda0dcaa974f78b9026879e15fc506e67c0450f6417ef03ff58cd1648a27a13c`.

## Changes

When GI is active and directional shadows are disabled, the last celestial
lighting draw also computes GI. Both effects share depth, position and normal
reads; their formulas remain independent. A shared HLSL helper serves the
merged and separate routes. GI runs once, including frames with two sources
or a mandatory zero-strength source. Baseline lighting and visibility alpha
retain their original outputs. Failed optional shader creation keeps the
separate passes; GI off/on and resource reset retain their existing behavior.

The merged variant uses 387 SM3 slots and 23 temporaries, versus 230 + 233
slots in the separate shaders. No texture, mask or quality setting is added.
All existing world and transition shader binaries remain byte-identical.
The GPU profile labels the combined interval `WorldLightingGI`, without
pretending that GI's work disappeared.

Two soft-shadow/GI prototypes were rejected: their larger register footprint
made them slower despite fewer passes. The ordinary PCF combination exceeded
the 512-slot baseline budget. Neither is included in production.

Inactive material profiling scopes now initialize only their disabled state
and skip unused work counters. A sampled stage transition uses one clock
read for its shared end/start timestamp. Nested attribution, reset, GPU
queries and disabled zero-clock behavior remain checked.

## Whole common graph comparison

The benchmark now fills a resident probe window around the captured camera
and advances animation deterministically. A fourth mode exercises GI without
shadows. A separate unchanged r79 checkout supplies the full before control;
do not compare these timings directly with r79's earlier single-probe fixture.

RTX 4090 / DXVK 3.1.1, 3840x2160, actual renderer/shaders, fixed scene/depth,
authored casters, fog and probes; SDR, eight lamps and one PCF point shadow
where enabled. Three ABBA rounds per mode/comparison, six runs per side,
32 measured frames after 16 warmup frames. Medians of run medians, ms:

| Common workload | CPU upstream → PR | GPU upstream → PR |
| --- | --- | --- |
| AO/blur/composite | 0.02113 → 0.01335 | 0.54170 → 0.54288 |
| Regional fog | 0.02950 → 0.02140 | 0.84922 → 0.73500 |
| GI, fog, directional/point shadows, lamps | 0.03470 → 0.03570 | 1.04699 → 1.01402 |
| GI, fog, lamps, shadows off | 0.03295 → 0.02475 | 0.99490 → 0.89493 |

Base GPU is equivalent within 0.3%. The shadowed case is about 3% faster on
GPU but about 0.001 ms / 3% slower on CPU. This does not prove unconditional
CPU parity with upstream. Existing PR fog/lighting behavior differs from
upstream, so these are equal-input/settings timings, not image equivalence.

Isolating r80 against r79 in the fourth mode: GPU 0.91382 → 0.89671 ms
(1.9% faster), CPU 0.02723 → 0.02688 ms (small difference), 16 → 15 draws.
Other GPU modes stay within 0.1% of r79. Every final SDR image across the
four before/after modes is byte-exact and deterministic across repeated runs.

The shader-only oracle covers one/two sources, missing probes, SH history,
GI/debug/ambient gains, silhouettes, sky and water. Native D3D9 and DXVK 2/3
each pass 16 cases. The maximum lighting-buffer difference is 0.000488281;
baseline and alpha are exact. Isolated GPU gains vary by backend and source
count; the full-graph result above is the relevant measured improvement.

The profiling microbenchmark uses 500,000 iterations with nested scopes and
stage changes. Disabled: 2.69445 → 1.10230 ms; CPU recording: 154.63405 →
117.74005 ms. These measure bookkeeping only, not capture or game FPS.
The disabled saving amounts to roughly 0.01 ms for 3,000 such iterations.

This graph excludes native draw hooks, model/material capture, real caster
density, HDR/weather and presentation. GPU timestamps may include submission
gaps. It does **not** prove a total 2–3 ms Northlight budget. Available client
and QA logs still identify r73; no post-r79 gameplay measurement is available.

## Validation and reproduction

Actual WorldRenderer screen tests pass on native D3D9 and DXVK 2/3, with
MSAA 0/2, HDR, optional-shader creation failure, blocked merge, GI off/on,
names/unlit/prelit receivers and exact state restoration. Material profiling
passes its CPU/GPU fault matrix and ASan/UBSan. Related actor, weather, rain,
lamp, shader-math and manifest checks pass. Production Zig 0.15.2 build and
Windows package `0.3.217-test.20261110.5` pass freshness, 17-export, payload
equality and package verification. No client files were changed.

```powershell
python tests/benchmark_upstream_pipeline.py --upstream $upstream --baseline $r79Checkout --capture $capture --backend $backend --output $newOutput --rounds 3
python tests/test_world_lighting_gi_gpu.py --output $newOracleOutput
```

Receipts: ignored `out/optimization-20261110-r80/`, particularly
`comparison/validation.json`, `comparison/pixel-validation.json`, `gi-oracle/`,
`world-screen/world-screen-gpu/validation.json`, `profile-measurements.json`
and `artifacts.json`. The measured identity was
`bf015fcd65b9ec317263096790db69a0b54338cf6c33719931d52776cfc2e3c5`;
`transition-equivalence.json` verifies that only the dependent manifest's
freshness metadata changed afterwards, with every runtime source/header and
shader byte unchanged.
