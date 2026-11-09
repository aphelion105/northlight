# r57: common direct HDR for fixed-function additive sky

The latest client log is the r56 build (`a38d722dd348d3137fc5b5fb36ed8491774cdaca7eb8557766fda9d3ddb41776`), DXVK 3.1.1, scRGB HDR at 3840x2160 without MSAA. Its frozen copy is `out/perf-20261109-r57/game.log`, SHA-256 `26957d037ade39315d5b537fe09e8d414afd766a4877a0a73b6332975166a405` (1,794,396 bytes). The source log was last modified at 00:35:57 local time on November 9.

## What the log establishes

- Foreground frame windows 2447РІР‚вЂњ2807 average 23.175РІР‚вЂњ26.997 ms (37РІР‚вЂњ43 FPS). The scene remains expensive after geometry warms up.
- At frame 2794 the GPU timestamp intervals are GameScene 11.352 ms, ModelNormals 1.555 ms and Effects 4.710 ms. At frame 2540, the CPU profile records capture 4.043 ms and effects 2.869 ms. These overlapping CPU/GPU intervals cannot be added into a utilization percentage or interpreted as exclusive GPU execution.
- The largest sampled EFFECT component outside AO is RemovalSmooth: 0.617 ms at 2667 and 1.400 ms at 2794. WorldComposite is about 0.46РІР‚вЂњ0.50 ms. AO is unchanged.
- The largest post-startup EVENT wait, 154.4993 ms at frame 784, is in the pre-world/unfocused section. The world section still includes a 44.6915 ms wait at 2423 and approximately 10РІР‚вЂњ12 ms waits in warmed samples. This iteration does not change synchronization or claim these waits are eliminated. Accumulated scheduler pause time is not independently removable GPU wait time.
- By frame 2400, `HDR_WORK` reports 1,659 compatibility layers and 13,760,409,600 layer pixels: each layer is charged a complete 4K surface. The log includes fixed-function sky draws with FVF `0x152`, stage-0 `COUNT2`, texture/diffuse MODULATE for RGB and alpha, and SRCALPHA/ONE blending. These were rejected by the old direct-UV guard solely because their vertices were not pretransformed.

## Change

`Color::fixedDraw` now sends compatible single-texture additive draws with COUNT2 or disabled UV transforms directly to the HDR target. Native fixed-function vertex processing still performs the original world/view/projection and texture-coordinate transformation. There is no CPU UV transform, geometry copy, approximate vertex shader, or backend-specific policy.

The common shader is PS 2.0, 25 instruction slots, four temporary registers, one texture sample, 652 bytecode bytes. The old PS 3.0 fixed-vertex route also produced incorrect native-D3D9 UVs with texture transforms disabled; the PS 2.0 interface passed native D3D9 and DXVK 1/2/3. Sampling retains the original sampler's filtering and mip state. The shader object is created once per Color instance and released on reset.

Each accepted draw removes the UNORM intermediate clear, render-target switches, saved full-device state, and full-target HDR composition. It uses one geometry draw and preserves only the one pixel constant register it changes. No additional geometry/material cache or backend fork was introduced.

Eligibility remains conservative: fixed-function XYZ with diffuse and one 2D UV set; stage 1 disabled; disabled or affine COUNT2 at stage 0; exact texture/diffuse modulation; ordinary UNORM/DXT 2D texture; additive SRCALPHA/ONE; fog, lighting, specular and alpha testing disabled; no MSAA. Existing multisample, multi-texture, projected, programmable-vertex and unsupported-state paths retain the original compatibility layer. MSAA was excluded after a native-D3D9 oracle exposed a visible mismatch in a private prototype.

The compatibility layer's separate additive destination-alpha policy is preserved and restored. UV transforms, vertex data, native logical scene rendering and native draw order are unchanged. `HDR_WORK.fixedAdditiveDirect` counts accepted draws in the next game log; `compatibilityLayers` and `layerPixels` remain available for comparison. The existing log does not record every new eligibility condition, so actual accepted draw counts require the next run.

## Cache findings

The warmed ModelNormals samples at 2667 and 2794 report zero source-read bytes, zero UP bytes and zero streamed draws; all 595/734 logical draws use resident sources. At 2794, 273 draws are merged, leaving 461 submitted draws. Raising geometry capacity does not address those warmed samples.

The material-map snapshot holds 87 entries / 99,357,948 bytes against a 96 MiB budget, with 22,943 hits, seven generated maps and nine evictions. Disk persistence reports 90 hits, seven misses/writes, 89 uploads and 48 staging reuses. The snapshot is near capacity, but it does not establish sustained cache thrashing. Available virtual memory at 2452 is 834 MiB, largest free region 730 MiB, pressure=0 and trims=0. Cache limits and visual quality are unchanged.

## Validation and limits

Evidence is under `out/perf-20261109-r57/`:

- `additive-final-v3/validation.json`: 192 colour/UV combinations per backend, across D3D9 and DXVK 1/2/3 (768 total). Perspective/orthographic transforms, COUNT2/disabled transformation modes, identity/nonidentity UV matrices (including nonidentity matrices while transforms are disabled), point/linear filtering, mip-filter states, two white levels and three opacities. Direct RGBA is bit-exact against independently baked UVs under an identity native texture transform.
- The legacy UNORM8 premultiplied intermediate has different quantization. Maximum observed RGBA difference from that reference is 0.01171875 scRGB at the tested white levels; this is not a bit-exact legacy-layer claim. The direct path retains floating-point transfer precision instead of quantizing the intermediate to eight bits.
- Eight fallback controls per backend retain the old path. A native-versus-direct oracle verifies exact depth/stencil through a subsequent equality-tested colour probe, 4,096 identical occlusion samples, and zero compatibility-layer allocation.
- `old-transform-*.log`: the existing 144-case multi-stage COUNT2 oracle passes on all four backends (576 cases). Its native XYZ fallback remains intact.
- `fixed-sky-*.log`: the existing 432-case MRT sky oracle passes on all four backends (1,728 cases), with native bytes/HDR half-floats and alpha/depth/stencil/query side effects preserved. Only the unrelated 4K benchmark was omitted in the private harness.
- HDR shader COM ownership and bytecode patch regression checks pass, including their expected-failure controls. Existing shader bytecode is unchanged; the HDR source/manifest/header and new shader were regenerated with the pinned compiler.

WoW was open during standalone GPU checks on Windows. The initial benchmark runner explicitly records `game_observed_open=true` and `timings_usable_for_comparison=false`; its recorded 4K timings are not performance evidence. The final expanded oracle omits timing runs. macOS performance has not been measured. No in-game FPS, EVENT-tail reduction, or completion of all remaining bottlenecks is claimed. Validate the next game run using `fixedAdditiveDirect`, layer counts, foreground frame times and EVENT distributions.

The production build and final-DLL EVENT smoke receipts (`build-validation.json`, `smoke-backends-final-v2/validation.json`) are delivered beside `out/dll-20261109-r57/d3d9.dll`. The final additive oracle is also executed through the production DLL command stream on DXVK 1/2/3 (`proxy-additive-validation.json`), with effects off in a separate host. It covers the HDR transfer and device-state behavior, not OS HDR presentation. The client DLL and settings are not replaced by this iteration. No installer is produced.

Final DLL: D:\dev\Projects\Northlight\out\dll-20261109-r57\d3d9.dll

SHA-256: `f1b40f920654387a3100ed79b11d30a36f7c93fa2dc9accfae7bc0ca44382e70`. Source identity: `0d2bfc79c51604eb7b1bc611fcadedd961b0a80056a5ce5f2e589c56b9be3500`. Built 2026-11-08 23:20:28 UTC. Final verification: `out/dll-20261109-r57/final-validation.json`, including 576 HDR cases through the final command-stream proxy and 192 real EVENT generations.
