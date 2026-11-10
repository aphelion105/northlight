# r86: shared fullscreen geometry and a valid water decode boundary

Change base: r85, `227279c8d0c2fe6a4cf406f0911b7e3dfe7f96d8`.

## Architecture

HDR, Device, WorldRenderer, SurfaceRenderer and WaterRenderer now use one
small fullscreen geometry helper. It emits the same four XYZRHW/UV vertices,
96 bytes and two triangle-strip primitives, including the original half-pixel
offset. Callers retain their viewport policies and state setup. Effect
admission and calculations remain independent. No new resource, state cache,
mask or shader variant is introduced.

Water-mask clearing now runs after its SavedState capture, target/sampler
unbinds and full viewport setup, immediately before the existing decode draw.
This removes the separate ColorFill/manual-target-swap clearing path at that
boundary. The raw MRT capture still needs and retains clearTarget(). A mixed
frame with a second-draw fallback retains that captured mask; decode still
discards coverage below one half and overwrites only the covered pixels.

## Correctness fixes discovered during validation

The old runtime ps_2_0 resolve shader uses a nonidentity TEXLD coordinate
swizzle. Native D3D9 rejects CreatePixelShader with 0x8876086c, disabling the
MRT path for the session. Replacing that source token with the identity
swizzle makes native creation succeed. The two-dimensional sample still
uses the same x/y coordinates. The C++ generator and Python oracle change
together. The GPU test probes the old token separately: native rejects it,
while DXVK 2/3 accept it.

With ColorFill deliberately refused and an MSAA auxiliary render target
still attached, the old manual mask-clear fallback reports success on the
tested native device but leaves stale pixels. Clearing after detaching the
auxiliary targets and depth-stencil fixes this. Those known-broken outputs
are not a golden image; normal ColorFill results remain bit-exact between
class snapshots.

## Validation and measurement scope

The GPU oracle uses actual before/after water renderer classes on one
device. Both snapshots deliberately share the current SavedState and
native-compatible resolve shader, isolating the clear-placement change.
Each of native D3D9 and DXVK 2/3 exercises 136 cases: even/odd dimensions,
MSAA 0/4, empty/full/fractional/edge coverage, fallback union, ColorFill
refusal and clear/draw failures. Current output always matches an authored
distance/coverage float reference. Targets, depth, viewport, scissor,
textures and blend state are restored; failed decode never exposes an
incomplete MRT mask. The old refused-clear path has 45,879 incorrect pixels
on native, zero on DXVK 2/3; the new path has zero on all three.

Water state tests pass O2 and ASan/UBSan: 160,000 scripted steps, 79,723
captures, 13,342 refused getter cases and 5,099 injected API failures per
build. All eight shader oracle/interpreter tests pass, including both
4,000-program corpora. MRT wiring checks pass.

Clear/decode timing uses 16 ABBA blocks, four warm-up blocks and 32 frames
per block, with six samples per side at 1080p/4K. There is no useful speed
gain on the normal ColorFill-supported path. The new boundary is retained
for simpler ownership and reproduced correctness fixes. GPU timestamps
can include submission gaps.

A fullscreen-triangle prototype was rejected: AOBlur had no stable speed
gain and some edge pixels changed by up to 0.014648438. Production topology
remains unchanged.

Neither the component fixture nor the captured-input graph establishes the
requested total 2-3 ms in-game budget. No newer local gameplay log than the
previously reviewed November 9 session was present during this pass.

## Common captured-input graph

DXVK 3, 3840×2160, three ABBA rounds per mode, 32 measured frames
after 16 warm-up frames per process. All 48 before/after readbacks are
bit-exact against r85. CPU and GPU remain effectively unchanged by r86.

| Mode | CPU r85 → r86, ms | GPU r85 → r86, ms | CPU upstream → PR, ms | GPU upstream → PR, ms |
| --- | --- | --- | --- | --- |
| 0 | 0.013800 → 0.013700 | 0.543696 → 0.543720 | 0.021075 → 0.013650 | 0.540672 → 0.542816 |
| 1 | 0.023300 → 0.023175 | 0.736256 → 0.735672 | 0.031975 → 0.023700 | 0.849168 → 0.735472 |
| 2 | 0.037700 → 0.035950 | 1.018368 → 1.018368 | 0.047700 → 0.037800 | 1.098936 → 1.018368 |
| 3 | 0.026375 → 0.026500 | 0.893952 → 0.895216 | 0.034750 → 0.026025 | 0.999000 → 0.895488 |

Upstream `e19ffee601acbad2b991917109bcb6d8b2f7d5ca` was verified against
origin/main during this run. CPU is lower in all four modes; GPU is within
0.4% in mode 0 and 7.3–13.4% lower in modes 1–3. These differences belong
to the accumulated PR, not to this geometry refactor. The graph uses frozen
scene/depth and authored casters/fog; it excludes model/material capture,
HDR, weather, presentation and game FPS. Upstream images are not asserted
identical to the PR; r85/r86 images are.

Dedicated water decode at 4K, no captured fallback:

| Coverage | CPU before → after, ms | GPU before → after, ms |
| --- | --- | --- |
| Empty | 0.007781 → 0.007748 | 0.013264 → 0.014100 |
| Fractional checker | 0.007820 → 0.007795 | 0.106576 → 0.106190 |
| Full | 0.007864 → 0.007838 | 0.039840 → 0.040578 |

The empty/full cases cost about 0.0008 ms more on this path; this is
retained as a small measured tradeoff for the native correctness fixes.
The fallback-union controls do not clear. CPU differences at this scale
are not evidence of a useful speedup.

Weather passes also pass 768 bit-exact cases per backend (native D3D9,
DXVK 2/3) after moving their geometry to the shared helper. HDR frame
acceptance passes 2,893 cases on each of DXVK 2/3, including exposure,
output, UI and alpha. All precompiled shader binaries are unchanged; only
the runtime water resolve source token changes.

## Reproduction

```powershell
python tests/benchmark_water_resolve.py --baseline $r85Checkout --backend $native --backend $dxvk2 --backend $dxvk3 --output $newWaterOutput --benchmark --allow-game-process
python tests/benchmark_upstream_pipeline.py --upstream $upstreamCheckout --baseline $r85Checkout --capture $privateCapture --backend $dxvk3 --output $newGraphOutput --rounds 3
python tests/benchmark_surface_batch.py --backend $native --backend $dxvk2 --backend $dxvk3 --output $newSurfaceOutput --allow-game-process
python tests/verify_hdr_frame_gpu.py --dxvk2 $dxvk2 --dxvk3 $dxvk3 --output $newHdrOutput
# Under WSL/clang++:
python3 tests/test_water_mask_state.py
python3 tests/test_water_shader_patch.py
```

GPU runs were sequential, with no concurrent renderer build/test workload.
The previously authorized running-game allowance was retained; these are
controlled harness intervals, not isolated gameplay measurements.
Receipts are ignored under `out/optimization-20261110-r86/`: `water-final/`,
`pipeline-comparison/`, `pipeline-verification.json`, `surface/`, `hdr/`,
water state/shader/wiring logs, `artifacts.json`, package verification and
PR verification. No client installation or game launch/stop is performed.

## Production artifacts

Source identity `33adfc7f6dad8f2cfe51142d85a17d5b6a693b7153b8def942e9722aa255024d` matches measured inputs and the
production DLL. Build time: `2026-11-10 06:43:29 UTC`. Zig 0.15.2 compiler preflight,
shader freshness, x86 PE/17 exports, exact installer DLL payload and release
package verification all pass. Six existing compiler warnings remain.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `releases/Northlight-0.3.217-test.20261110.11/d3d9.dll` | 10,853,376 | `2783e364c2d6c3c45cded0423073f9a3fb0dd9202b98c8d97e60025ce681a0d9` |
| `releases/Northlight-0.3.217-test.20261110.11-Windows.zip` | 20,820,339 | `cf1213077c49bab92c47bcf9cd9265dab4ec8c7b5cbbe7a1dbaa2f8309e8982a` |
