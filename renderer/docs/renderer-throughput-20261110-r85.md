# r85: one state boundary for the weather batch

Change base: r84, `5b7cd29ce7bff8e5c6ffb415feec097f207418ba`.
Measured and production source identity:
`c848841a60ce0a969a05815aaa001a6f2728c2b855d6be7a522dc73265198103`.

## Pipeline change

`Device::surfaceWeather` already saves and restores the game's D3D9 state.
Precipitation and camera optics formerly captured/restored that state again
inside each pass. They now accept the existing enclosing snapshot, using the
same valid-snapshot / identical-device rule as WorldRenderer.

One small surface helper owns this decision for precipitation, lens/frost and
sand lens. Standalone calls, invalid snapshots and mismatched device wrappers
retain their own pooled SavedState. The enclosing caller owns the snapshot's
lifetime and restores game state on every return, including draw/copy/shader
failures. Each pass still initializes its own shader/sampler/render state.

Rain/snow plus lens: three state captures/applies become one. Sand lens: two
become one. Effect calculations, scene copies, draw counts, simulation,
name-stencil protection and alpha remain independent and unchanged. No new
state cache, resource, shader, mask or GPU pass is introduced.

## Validation

One executable compares the existing standalone path against the shared path.
Each backend passes 768 cases: rain, snow/frost, sand and no work; UNORM8,
FP16 and FP32; odd 129×97 dimensions; MSAA 0/4; protected names; draw, scene-copy
and shader-creation failures; null, valid, invalid and mismatched snapshots.
Images and alpha are bit-exact on native D3D9 and DXVK 2/3. The outer scope
restores viewport, texture, auxiliary MRT, depth-stencil and blend state.

The native MSAA fixture initially cleared with an auxiliary MRT attached and
produced an invalid background. It now initializes the full viewport/colour
before attaching that MRT, and asserts the expected background. No production
copy-order or MSAA workaround is shipped.

The actual weather-dispatch harness verifies that all three calls receive the
valid enclosing snapshot. Shelter/exterior, pre-UI ordering, no-work and
memory-admission regressions pass. All 191 shader binaries and world-renderer
source remain identical to r84. Client files and game processes are untouched.

## CPU / GPU

DXVK 3, one executable, FP16, MSAA 0, 1080p/4K. Six samples per side from 16
ABBA blocks with the first four discarded; each block averages 32 frames.
Simulation state is reset identically before each block. Timing includes the
outer snapshot, actual passes, lens scene copy and restoration; queries,
background preparation and readback sit outside the CPU interval.

| Weather | CPU standalone → shared at 1080p, ms | CPU at 4K, ms |
| --- | --- | --- |
| Rain + lens | 0.06909 → 0.05662 | 0.06816 → 0.05247 |
| Snow + lens/frost | 0.06649 → 0.05254 | 0.06264 → 0.05066 |
| Sand lens | 0.01896 → 0.01102 | 0.01735 → 0.01034 |
| Empty protected batch (control) | 0.00733 → 0.00732 | 0.00708 → 0.00713 |

CPU decreases about 18–23% for rain/snow and 40–42% for sand. The empty-batch
control is effectively unchanged; it still constructs the outer guard.
Actual no-weather admission is outside this fixture. These are
submission-batch timings with a counting wrapper, not full game frames.

| Weather | GPU standalone → shared at 1080p, ms | GPU at 4K, ms |
| --- | --- | --- |
| Rain + lens | 0.03842 → 0.03845 | 0.16754 → 0.16752 |
| Snow + lens/frost | 0.06024 → 0.06024 | 0.24442 → 0.24445 |
| Sand lens | 0.00939 → 0.00936 | 0.02099 → 0.02091 |

GPU work stays practically unchanged. Timestamps span completed batches and
can include submission gaps; this change does not accelerate shader math.
The test excludes world passes, native weather, HDR output mapping, game draw
hooks and presentation. It does not establish the overall 2–3 ms budget.
The common world graph's r84 timings are not relabelled as new measurements.

```powershell
python tests/benchmark_surface_batch.py --backend $native --backend $dxvk2 --backend $dxvk3 --output $newOutput --benchmark --allow-game-process
python tests/test_lens_wiring.py
python tests/test_weather_exterior.py
python tests/test_surface_memory_admission.py
```

Receipts: ignored `out/optimization-20261110-r85/surface-final/validation.json`,
`test_*.py.log`, `shader-retention.json`, `artifacts.json` and
`package-verification/verification.json`.

Production DLL/package `0.3.217-test.20261110.10` pass Zig 0.15.2 compiler
preflight, shader freshness, x86 PE/17 exports, exact installer payload and
release-package verification. DLL source identity matches the measured source.
Build time: `2026-11-10 05:58:51 UTC`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `Northlight-0.3.217-test.20261110.10/d3d9.dll` | 10,853,376 | `6a801ffd40ed1ac2f30ea1b315db1f0cc326bb3be5d6ca8fdc7462e1d9eb23f1` |
| `Northlight-0.3.217-test.20261110.10-Windows.zip` | 20,820,985 | `0b81a9a1ed935fcbcad4eedeef17f51a5f686fc71d7d39439845431efc4630a6` |
