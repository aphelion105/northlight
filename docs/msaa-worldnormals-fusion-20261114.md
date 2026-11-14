# MSAA normals fused into WorldNormals: rejected experiment

Status: **not accepted for production**. Removing a pass did not produce a reliable
whole-path improvement, and the new WorldNormals image is not yet equivalent at
4K. The installed DLL and main runtime remain unchanged. Preserve this work on
`experiment/msaa-worldnormals-fusion-20261114` (commit `b292b52f`); do not enable the old standalone
MSAA resolve in production by merging this experiment indiscriminately.

## Why a model replay exists

WoW's audited forward shaders consume mesh normals and pose constants to produce
lit color. They do not leave an accessible per-screen-pixel normal texture. The
normal replay creates that texture using the captured geometry, pose, depth and
alpha coverage. It does not repeat every material/lighting effect. WorldNormals
combines those authored model normals with depth-derived terrain/background
normals, rotates to world space, and produces half-resolution normals/confidence
and optional distance for Northlight lighting/shadow/GI consumers.

Writing an additional normal attachment during the native draw can eliminate
geometry replay. Under MSAA, its samples must match native depth. An ordinary
color resolve averages unrelated surfaces and is unsuitable. The first
[MSAA experiment](msaa-normal-experiment-20261114.md) extracted sample zero in a
separate full-resolution compute dispatch and was slower than replay.

## This experiment

`scripts/shaders/compile_world_normals_msaa.py` extracts the existing WorldNormals
functions from `shaders/world_effects.hlsl` and compiles a Vulkan compute consumer.
It reads native MSAA sample zero directly and writes half-resolution FP16 world
normals/confidence plus R32F distance. It removes the full-resolution normal
extraction dispatch and its output read/write. The existing compiled SM3 shader
remains the independent image oracle. No averaging, normal compression, lowered
resolution, disabled MSAA or reduced effect quality is counted as an optimization.

The test macro `NORTHLIGHT_WORLD_NORMAL_MSAA_TEST` deliberately gates consumer
fusion. `model_normal_world_msaa.inl` is an exploratory interop implementation,
not a production integration. In particular the native benchmark bypasses resolve
at commit and asserts consumer success; it is not a safe runtime fallback
transaction. Production would need to retain the immutable replay queue until
consumer success, share the WorldRenderer output ownership correctly, preserve
two distinct temporal distance histories, and cover reset/device loss/capability
and shader freshness contracts. No DLL claiming these properties was installed.

DXVK 3 still needs a copy into a sampled MSAA image. DXVK 2 can sample directly.
Both require a D3D command flush before external Vulkan submission. Fusion removes
one pass, but not attachment bandwidth, the DXVK 3 copy, per-draw MRT admission and
shader binding work, or that synchronization. The test helper still allocates the
unused legacy resolve target for the independent oracle; native fusion does not
read or write it. This is a memory limitation, not hidden resolve GPU work.

## Measurements and image gate

Evidence: `.local/out/test-output/msaa-fusion-20261114/`. RTX 4090, RDP, packaged
DXVK 3.1.1 and 2.7.1. Process-local overrides allow the hidden device without
changing the game configuration or closing Forever. This is a component/path
benchmark with background workloads, **not a measured game FPS change**.

The isolated resolve-plus-WorldNormals comparison on DXVK 3 at 3840x2160/MSAA2
improved from 0.486 to 0.311 ms with depth normals, and 0.463 to 0.349 ms with
model normals. Those baselines include the previous rejected resolve; they are
not the installed production path.

The complete affected interval includes capture queue, native drawing, replay or
MRT, common native scene/depth resolves and WorldNormals. Alternating ABBA blocks
warm up each variant; 32 samples/variant report medians. 192 near submissions of
the stock 6,305-vertex human mesh, with resident geometry:

| Workload/backend | Native color | Existing GPU ms | Fused GPU ms | Existing CPU ms | Fused CPU ms |
|---|---|---:|---:|---:|---:|
| Three local lights / DXVK 3 | SDR | 0.400 | 0.561 | 0.346 | 0.460 |
| Three local lights / DXVK 3 | HDR | 0.456 | 0.585 | 0.342 | 0.448 |
| Three local lights / DXVK 2 | SDR | 0.353 | 0.413 | 0.379 | 0.452 |
| Three local lights / DXVK 2 | HDR | 0.376 | 0.435 | 0.376 | 0.443 |
| Four-weight skinning + alpha / DXVK 3 | SDR | 0.222 | 0.449 | 0.257 | 0.353 |
| Four-weight skinning + alpha / DXVK 3 | HDR | 0.269 | 0.470 | 0.256 | 0.350 |
| Four-weight skinning + alpha / DXVK 2 | SDR | 0.225 | 0.219 | 0.260 | 0.350 |
| Four-weight skinning + alpha / DXVK 2 | HDR | 0.263 | 0.244 | 0.257 | 0.347 |

Final source-bound reports: `final-native-local/validation.json`,
`final-native-weighted/validation.json` and `final-images/validation.json`.
All identify `b292b52f` and report no source changes during execution. The image
report deliberately has `image_acceptance=false`; no performance acceptance is
claimed. Earlier exploratory logs remain in `whole-graph/`,
`whole-weighted4-alpha/`, `consumer-timing/`.
The small DXVK 2 weighted GPU reduction comes with higher CPU cost and does not
establish a game FPS benefit. GPU timestamp intervals can include command
starvation. Do not add CPU and GPU times as if they were sequential frame costs.

The initial 108 image cases (2/4/8 samples; odd/even sizes; six depth patterns;
model coverage on/off) passed. The 4K depth-normal case failed: 1,592 half-float
components exceeded 0.001 error, with differences up to 1 at image edges. Confidence
was unchanged in that case. Raster UV interpolation/texel selection and compiler
arithmetic need further investigation; the exact cause has not been established.
This must not be described as lossless or as complete game-image validation.
Failed runs are retained and return nonzero even when timings are emitted.

## Reproduce

DXC is an offline development dependency only. The used official release is
[v1.9.2609](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609),
asset `dxc_2026_09_29.zip`, SHA256
`ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1`.
The shader manifest records source, compiler, library and SPIR-V hashes. Strict
floating point compilation targets Vulkan 1.2; a production gate would have to
verify the required device capabilities. Current checks target the installed GPU.

```powershell
python scripts/shaders/compile_world_normals_msaa.py --dxc .local/tools/dxc-1.9.2609/bin/x64/dxc.exe --output .local/out/test-output/msaa-fusion-20261114/shader
$env:NORTHLIGHT_TEST_ALLOW_RUNNING_GAME='1'
$env:DXVK_CONFIG='d3d9.enumerateByDisplays = False; d3d9.customVendorId = 1002; dxvk.enableDescriptorHeap = False'
$env:DXVK_FILTER_DEVICE_NAME='NVIDIA GeForce RTX 4090'
python tests/test_world_normals_msaa_gpu.py --shader <shader-directory> --backend <dxvk.dll> --output <new-directory> --timing
python tests/test_world_normals_msaa_gpu.py --shader <shader-directory> --backend <dxvk.dll> --output <new-directory> --native-fixture <native-normal-fixture> --native-mode plain-local-msaa-near
python tests/test_world_normals_msaa_gpu.py --shader <shader-directory> --backend <dxvk.dll> --output <new-directory> --native-fixture <native-normal-fixture> --native-mode plain-weighted4-alpha-msaa-near
```

Run Windows GPU/compiler tools outside the restricted sandbox as documented in
AGENTS.md. System D3D9 remains unavailable in this RDP session and is not covered.
The native fixture is produced by `test_model_normal_native_gpu.py` from installed
stock game data; no game files are uploaded. Backend, fixture, process census,
source identity and command options are recorded in each validation report.

Further work must first make native MSAA attachment access cheap and demonstrate
an end-to-end win. Fewer draws/passes alone is not an acceptance criterion. Do not
ship this path or spend more production integration work on it merely because
its isolated resolve-plus-WorldNormals comparison is faster.

## Final repository state

The experiment passed source 15/15 and architecture 27/27 on `b292b52f`:
`.local/out/test-output/20261114-132014-8b34fed4/results.json` and
`.local/out/test-output/20261114-132014-c24e2023/results.json`. These checks do not
overrule the failed GPU image/performance gates. Main contains this report only,
with the previous runtime preserved. The installed DLL remains SHA256
`7c4109684b592b154e64c2baa212218756d0cdab8826be90efb3646f03a1bdff`.
No game was closed. The final process census contained no Wow.exe; background
processes were recorded, not controlled.
