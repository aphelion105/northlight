# r84: frame-local scratch ownership

Baseline: r83, `b261aaf556efd3cd7dfcfc3267f9698ce3da3359`.
Upstream comparison: `e19ffee601acbad2b991917109bcb6d8b2f7d5ca`.
Measured and production source identity:
`e52bcd0646ded65db489558a350cdb32e5ae5575d67e88a63679097340f9db16`.

## Resource lifetimes

The world pipeline resolves directional/GI lighting before current local light,
fog and final composition. TemporalLight reads the previous light texture,
writes the next light texture and rotates `temporalIndex`. The previous light
texture is then dead until the next frame's complete temporal write.

LocalFog now borrows that dead FP16 texture instead of owning a separate
accumulator. LocalFogCombine still caps the complete sum once into fog; source
selection, batches, scissors and all effect calculations remain independent.

| Stage | Retired light pair | Current light pair | Previous depth |
| --- | --- | --- | --- |
| TemporalLight | Read | Write | Read |
| LocalFog / combine | Accumulate, then read | Retain | Retain |
| FogTemporal | Dead | Retain | Read |
| Final composition | Dead | Read | Retain |
| Next frame | Full temporal overwrite | Read as light history | Reuse for new distance |

The light and depth histories have different lifetimes: FogTemporal still
needs the previous depth, so only the retired **light** texture is borrowed.
The accumulator is cleared only inside the same scissor union as before;
unconsumed texels outside that union remain unspecified.

Removal smoothing's borrowed fog scratch pointers are also local to `render`,
rather than retained members that need refresh/reset handling after fog swaps.
Four stored pointers and one owned texture/surface pair are removed. The
existing owners handle creation, resize, reset and release. No resource pool,
allocation on first fog use, per-frame AddRef/Release or new pass is added.

Half-resolution FP16 screen targets decrease from nine to eight. Nominal
texture payload saved: 4,147,200 bytes at 1920×1080 (3.96 MiB), or 16,588,800
bytes at 3840×2160 (15.82 MiB). Driver allocation overhead is excluded.

## Correctness

The actual renderer is compiled against frozen r83 and current headers.
Each variant renders 105 frames per backend, across three create/release
cycles, partial allocation failures, an odd 193×129 resize, alternating SDR/HDR,
camera jumps, invalid history, a skipped render, local lights without fog,
fog without local lights, clipped batches and missing FogTemporal shader.

The meter checks that accumulation never overwrites current light history or
samples its own render target, and that combine reads the intended texture.
Both variants produce 174 local-fog batch draws and 60 combines per backend;
all 174 batches have partial scissors. Allocation counts are nine versus eight.

All 411 exported images match bit-for-bit on native D3D9 and DXVK 2. On DXVK 3,
409 are exact; two final images differ in one RGB component each by one FP16
ULP (0.000244140625). Alpha and every intermediate/history image remain exact.
Repeating the same frozen executable reproduces those two changed components;
that repeated reference matches the candidate in all 411 images. The oracle
allows at most one ULP / 0.000244140625 only in final RGB; intermediates and
alpha retain exact comparison. No production shader binary changes.

Removal-smoothing wiring/emulation and grouping-independent LocalFog cap
checks pass. Installed client files and game processes remain untouched.

## CPU / GPU comparison

| Workload | CPU r83 → r84, ms | GPU r83 → r84, ms |
| --- | --- | --- |
| AO/blur/composite | 0.01410 → 0.01405 | 0.54394 → 0.54368 |
| Regional fog | 0.02243 → 0.02210 | 0.73645 → 0.73611 |
| GI, fog, directional/point shadows, lamps | 0.03768 → 0.03650 | 1.01427 → 1.01427 |
| GI, fog, lamps, shadows off | 0.02680 → 0.02558 | 0.89395 → 0.89465 |

All final SDR images are bit-exact against r83, and draw/copy/target-bind
counts stay the same. GPU changes stay within 0.08%; CPU medians improve
0.00005–0.001225 ms. These small steady-state differences do not establish a
general frame-speedup. Initialization has one fewer texture/surface creation.

| Workload | CPU upstream → r84, ms | GPU upstream → r84, ms |
| --- | --- | --- |
| AO/blur/composite | 0.02090 → 0.01328 | 0.54170 → 0.54327 |
| Regional fog | 0.02940 → 0.02165 | 0.84896 → 0.73533 |
| GI, fog, directional/point shadows, lamps | 0.04390 → 0.03375 | 1.09965 → 1.01606 |
| GI, fog, lamps, shadows off | 0.03350 → 0.02555 | 0.99645 → 0.89569 |

The upstream AO-only GPU result is slightly faster (about 0.29%); the more
expensive authored workloads retain the PR's prior improvements.

The fixed 4K SDR graph uses captured scene/depth and authored casters, lamps,
fog and probes. Three ABBA rounds, six runs per side, 32 measured frames after
16 warmup frames. It excludes game draw hooks, model/material capture,
HDR/weather and presentation; GPU timestamps can include submission gaps.
Upstream retains different existing lighting/fog semantics.
This lifetime change saves memory and ownership work; a total-frame speedup
or the overall 2–3 ms Northlight budget is not established by this fixture.

```powershell
python tests/validate_world_removal_alias_gpu.py --fog-scratch --reference $r83Checkout/src/world/world_renderer.h --output $newOracleOutput --allow-game-process
python tests/test_shadow_removal_smoothing.py
python tests/test_local_fog_batching.py
python tests/benchmark_upstream_pipeline.py --upstream $upstream --baseline $r83Checkout --capture $capture --backend $backend --output $newGraphOutput --rounds 3
```

Receipts are under ignored `out/optimization-20261110-r84/`: `fog-alias/`,
`fog-alias-comparison.log`, `fog-alias/repeat-verification.json`,
`removal-wiring.log`, `local-fog-math.log`, `pipeline-comparison/`,
`shader-retention.json`, `artifacts.json` and
`package-verification/verification.json`.

Production DLL/package `0.3.217-test.20261110.9` pass Zig 0.15.2 compiler
preflight, shader freshness, x86 PE/17 exports, exact installer payload and
release-package verification. Source identity matches the measured graph and
current production source. Build time: `2026-11-10 05:12:53 UTC`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `Northlight-0.3.217-test.20261110.9/d3d9.dll` | 10,853,376 | `768994986c60b243038eab3b20d0109702f9d2fe2af7525b23bb5984fe9a9631` |
| `Northlight-0.3.217-test.20261110.9-Windows.zip` | 20,820,619 | `54a047a62e8689f0550325240b3aa1a80e01b14d68a567640425ed8439f70b49` |
