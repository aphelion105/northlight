# r58: common HDR state path and exact occlusion-query caching

The new game run uses the final r57 source identity `0d2bfc79c51604eb7b1bc611fcadedd961b0a80056a5ce5f2e589c56b9be3500`, DXVK 3.1.1 and 3840x2160 scRGB HDR without MSAA. Frozen log: `out/perf-20261109-r58/game.log`, 680,540 bytes, SHA-256 `435e1213014ffdac3b5d7fdcf8a0c1abc5baaf10e867f920c4b7e069cb6a3ab6`. Client modification time: November 9, 01:44:41 local.

## Findings from this run

The world enters after frame 1100. The focused window at 1335 averages 25.689 ms / 38.93 FPS; frame 1215 includes a 786.116 ms peak during loading. At 1524, CPU capture is 6.977 ms and effects 4.623 ms. GPU timestamp intervals are GameScene 16.689 ms, ModelNormals 2.548 ms and Effects 4.069 ms. The GameScene interval reaches 18.377 ms at 1397. Timestamp intervals include submission gaps; they are not exclusive GPU busy time or a utilization percentage.

The r57 direct-sky path did not activate: `HDR_WORK` at 1200 has `fixedAdditiveDirect=0`, 740 compatibility layers and 6,137,856,000 layer pixels. Recorded native sky draws match the XYZ/NORMAL/DIFFUSE/TEX1, COUNT2, texture/diffuse MODULATE, SRCALPHA/ONE pattern. The old diagnostic omits lighting, specular, alpha-test state and texture format, so it does not prove which additional eligibility check rejected them.

The largest non-startup EVENT wait is 61.7435 ms at 1363: 58.6504 ms before replay, 3.0801 ms in replay, 0.0137 ms afterward. The dominant part precedes the backend query call. The startup wait at frame 2 is 169.8748 ms. No timer, queue-fence or fake EVENT-completion workaround is added.

## Common CPU and HDR changes

1. The outer sky replay no longer captures/applies D3DSBT_ALL. `Color::draw` already restores its own shader, constants and raster changes. The outer scope saves/restores its four target bindings, depth surface, viewport, scissor, Z-write and stencil-write masks. It also clears the replay flag on exceptions. No new public helper hierarchy, cache, backend policy or user option was introduced.
2. The shared PS 2.0 additive transfer now accepts original fixed-function lighting and alpha testing. The native vertex processor still computes lighting and UVs, and native alpha comparison remains enabled. The old exclusions were unnecessary for the verified PS 2.0 path. Specular addition, MSAA and unsupported texture/coordinate/operation states retain the compatibility layer. Existing shader bytecode is unchanged.
3. Presence and source-format proof share one retained texture read instead of fetching the same source twice. Limited fallback diagnostics now record the missing lighting/alpha/specular/texture-format fields.

A component CPU microbenchmark compares the old full-state wrapper with the extracted current production method, with geometry submission replaced by S_OK. It uses 8,192 calls per block and eight alternating paired blocks after warmup:

| Backend | Full state, microseconds/call | Target scope, microseconds/call |
|---|---:|---:|
| Native D3D9 | 7.779 | 0.868 |
| DXVK 1.10.3 | 6.024 | 0.424 |
| DXVK 2.7.1 | 8.041 | 0.369 |
| DXVK 3.1.1 | 7.696 | 0.391 |

This measures API CPU work, not a frame or FPS. Evidence: `scope-bench.cpp`, `scope-bench-*.log`, `scope-bench-validation.json` in the frozen evidence folder.

An uncontended two-layer 4K standalone comparison of forced legacy composition versus direct HDR records approximately 0.139 -> 0.075 ms on DXVK 1, 0.128 -> 0.071 ms on DXVK 2, and 0.126 -> 0.072 ms on DXVK 3. Native D3D9 records 0.471 -> 0.280 ms. These are isolated GPU timestamp intervals including submission gaps, not in-game gains. `additive-final/validation.json` records game_observed_open=false and timings_usable_for_comparison=true.

## Occlusion-query correctness and duplicate work

The expanded oracle initially failed through the production command stream although it passed directly on each backend. Reusing a single occlusion query for identical native/direct coverage returned different counts, 0 versus 4,096. The stream's FLUSH shortcut accepted any previously completed generation. Its old comment assumed all occlusion queries were disposable glare history; the shared API has no such guarantee.

Cached FLUSH/non-FLUSH results now require the exact issued END generation, current reset epoch, cooperative device state and valid payload shape. In-flight or invalidated requests use the actual backend. The completed-current-generation fast path is retained. A successful synchronous read publishes its real payload immediately, and the background poller retires that generation without another backend read. Result publication/copy uses the actual payload length rather than copying all 64 reserved bytes for a four-byte count.

Both cache-hit paths share the same eligibility proof. The change is backend-independent. It restores query semantics; no stale count is substituted to avoid a wait. This could affect visibility or glare in the game, but a connection to a particular reported shadow artifact has not been established.

The pre-fix DLL is preserved privately as `out/perf-20261109-r58/before-query-fix.dll`, SHA-256 `21816449f8c0f612783871507b00bf645e9887d4b1721ffd6f1db57a741cc386`. `proxy-probe-correct-root.log` records the mismatch. This intermediate DLL is not the delivered build.

## Cache findings

Geometry residency and material maps behave differently. At 1524, all 1,138 ModelNormals logical draws use resident sources, with zero source-read bytes and zero streamed/UP bytes. Shared native geometry is about 55.9 MB against 96 MiB; capture-source storage is about 92.3 MB against 112 MiB.

Material maps have 90 hits / 857 misses at 1524, and 78 / 1,144 at 1397. These are request counts, not unique missing textures or actual GPU builds. The earlier snapshot at 1200 has 80 cached maps / 95,098,112 bytes, one generated map and zero evictions. Disk persistence reports 80 hits, two misses and one pending read. This snapshot cannot establish cache behavior at the later spike. The map budget can drop from 96 to 32 MiB below the existing address-space threshold; the log does not prove whether that happened later.

Cache policy, capacity, map quality and scheduling are unchanged. New cumulative counters distinguish generation quota, map/source budget rejection, protected-visible-set deferral, disk pending and invalid descriptions. `AUTONORMAL_BUDGET` reports every budget transition, available virtual memory and pressure state. The next run can distinguish those causes without inferring them from the misleading normalBuild request count.

## Verification

Evidence remains under `out/perf-20261109-r58/`:

- 768 colour/UV/lighting/alpha-test combinations on each of four backends (3,072 total), with bit-exact RGBA against baked native UVs. The old UNORM8 layer has different quantization; its observed maximum RGBA difference is 0.01171875 scRGB. Floating-point transfer precision is retained, not bit-exact legacy-layer output.
- 336 native-versus-direct depth/stencil/occlusion controls per backend (1,344 total), covering all eight alpha comparison functions, seven thresholds, three vertex opacities and lighting on/off.
- 432 three-way sky cases per backend (1,728 total): old full-state replay, extracted current production method, existing MRT path. Native bytes/HDR half-floats, viewport/scissor, depth/stencil and query samples match. Skipped calls, target-bind failure, draw failure, exception cleanup and prebound MRTs are covered. Direct replay creates no outer state block.
- Existing 576 multi-stage COUNT2 and 1,728 MRT sky regression cases pass. HDR shader ownership and bytecode-patch regressions pass.
- The material-cache index oracle passes optimized/debug builds and forced hash collisions, preserving cache state and COM ownership. Its persistent-adapter stub was updated for the existing trimUploads method.
- The full command-stream suite passes O2 and ASan/UBSan under Linux clang, including 80,000 equivalence steps per run across frame limits 1/2/3. The query regression now requires a new END to return its new payload rather than the old cached count. The completed-current-generation local path remains covered.

The final production DLL is checked separately through real DXVK 1/2/3 command streams with effects off in isolated hosts, including 192 hardware EVENT generations and both expanded HDR oracles. This does not exercise OS HDR presentation or replace an in-game run. No gameplay FPS, complete elimination of EVENT delays, or macOS performance equivalence is claimed. AO, installed client files, installer packages and repository publication are unchanged.

Final DLL: `out/dll-20261109-r58/d3d9.dll`, 10660864 bytes. SHA-256 `2e903406bf1066798eadff771d51729b27f792d654c1bc08f3549225407c8e05`. Source identity `c44622adf8d76aba29c785d0ba1c9325a5db2dac94be5535db18096e6a9262c0`, built 2026-11-09 00:29:13 UTC.

`final-validation.json` confirms the production DLL passed 2,304 HDR combinations, 1,008 native depth/stencil/occlusion controls, 1,296 sky-scope cases, and 192 real EVENT generations across DXVK 1/2/3. The previously failing reused-query case now passes on each backend. Installed client SHA remains the r57 DLL; no client file was replaced.
