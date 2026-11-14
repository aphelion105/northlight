# MSAA model normals: rejected full-screen resolve experiment

This branch is research, not an accepted performance change. Do not merge its
runtime into a release or replace the installed client DLL on this evidence.
It removes normal geometry replay on supported complete frames, but the measured
replacement is slower on the ordinary fixtures below. No game FPS gain is claimed.

## What is duplicated

Northlight already obtains the engine's geometry, vertex normals, deformation
constants and source shader provenance. A vertex normal is not a screen-space
normal texture: rasterization, interpolation, alpha coverage and visibility
must still be evaluated. The inspected WoW forward path produces scene color
and depth, without a reusable full-frame normal attachment.

The existing normal capture replays eligible geometry into an FP16 normal/depth
texture. `worldNormalValue` in `shaders/world_effects.hlsl` samples it for the
half-resolution WorldNormals pass, which produces world-space surface normals
and confidence for lighting. Where the captured depth disagrees with current
scene depth, it reconstructs a fallback normal from depth. AO-only and fog-only
frames already do not request this model-normal producer.

## Prototype

The existing transactional native MRT shader patch writes the normal and view
distance during the original WoW draw into a matching MSAA attachment. A Vulkan
compute dispatch copies sample zero into the existing consumer's single-sample
format. No averaging, reduced precision or native RGB change is introduced.
The immutable capture queue remains available until the entire frame commits;
unsupported shaders, native attachment hazards and resolve failures replay it.

The sample-zero choice follows DXVK's RESZ depth resolve. MSAA silhouette
coverage consequently follows sample zero, not the old single-sample replay's
pixel-center coverage. The MSAA oracle runs the independent replay shaders at
the same MSAA sample count, then compares resolved normal/depth pixels. This is
not a claim of bit-identical legacy silhouettes or complete game-image acceptance.

DXVK 2.7.1 accepts an interop-created sampled MSAA attachment directly. Installed
DXVK 3 rejects that creation: its texture normalization rejects multisampling on
the texture resource type passed by the interop creation method. Its ordinary
render target lacks sampled-image usage. The prototype therefore copies it into
a sampled Vulkan image before extraction. This extra copy is expensive.

Four pre-recorded command buffers/fences avoid waiting for a fence every frame.
DXVK's required rendering-command flush still synchronizes its command thread.
Cold reset/resize waits for pending external submissions before destroying them.
This is prototype lifetime handling, not full device-loss acceptance.

Primary API references:

- [DXVK interop contract](https://github.com/doitsujin/dxvk/blob/v2.7.1/src/d3d9/d3d9_interfaces.h)
- [DXVK depth resolve](https://github.com/doitsujin/dxvk/blob/v2.7.1/src/d3d9/d3d9_device.cpp)
- [D3D9 render-target compatibility](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setrendertarget)

## Evidence and limits

Local evidence root: `.local/out/test-output/msaa-normals-20261114/`.
`native-first/` passed 12,048 native/normal/state cases on the installed DXVK 3
backend; 3,316 frames committed. This includes the existing non-MSAA rollback,
shader factory, HDR, alpha-test, local-light and material provenance checks, plus
MSAA2 comparisons with the independent normal replay shaders.
`resolve-copy/` passed 48 isolated cases on each installed DXVK 2/3 backend:
2/4/8 samples, repeated frames, odd dimensions, resets, native state retention,
and distinct per-sample values. An ordinary averaging resolve is a negative
control and must differ. Earlier failed attempts remain in sibling directories.

Exploratory complete capture/native-draw intervals on RTX 4090, 3840x2160,
MSAA2, 192 near receiver submissions with the three-light shader:

| Backend | Native color | Replay GPU ms | MRT + resolve GPU ms | Replay CPU ms | MRT CPU ms |
|---|---|---:|---:|---:|---:|
| DXVK 3, copy needed | SDR | 0.267 | 0.608 | 0.348 | 0.446 |
| DXVK 3, copy needed | HDR | 0.278 | 0.609 | 0.349 | 0.429 |
| DXVK 2, direct sample | SDR | 0.263 | 0.412 | 0.403 | 0.463 |
| DXVK 2, direct sample | HDR | 0.262 | 0.351 | 0.393 | 0.429 |

Raw exploratory logs are under `.local/out/test-output/adhoc/`, named
`msaa-timing-near.log` and `msaa-timing-dxvk2.log`. These alternate replay/MRT
blocks, discard warmup, and include queue capture, original drawing, replay or
commit, and resolve. They exclude other game/renderer work. Large repeated-mesh
stress cases also exceeded normal cache budgets and are not representative FPS
evidence. RDP and background workloads are not controlled for a clean game test.

RDP can run these hidden DXVK tests without changing Windows or closing Forever:

```powershell
$env:DXVK_CONFIG='d3d9.enumerateByDisplays = False'
$env:DXVK_FILTER_DEVICE_NAME='NVIDIA GeForce RTX 4090'
$env:NORTHLIGHT_TEST_ALLOW_RUNNING_GAME='1'
python tests/test_model_normal_msaa_gpu.py --backend <dxvk.dll> --output <new-output-directory>
python tests/test_model_normal_native_gpu.py --fixture <human-fixture> --backend <dxvk.dll> --output <new-output-directory> --timing-draws 192 --timing-local --timing-msaa --timing-near
```

Use process-local overrides only. System D3D9 CreateDevice remains unavailable
in the tested RDP session; do not count that backend as covered.

## Next investigation

Do not enable this implementation merely because it has fewer geometry draws.
WorldNormals currently samples model normals only on its canonical half grid;
an exact consumer-grid extraction could reduce unused work, but requires an
odd-dimension and texel-selection oracle. Eliminating the standalone extraction
would require a consumer capable of reading the MSAA attachment, or folding
normal capture into another compatible geometry pass. D3D9/SM3 cannot directly
express the Vulkan multisample texture read used here. Do not drop MSAA,
replace authored normals with depth normals, or average surface/depth samples
and describe that as quality-preserving optimization.
