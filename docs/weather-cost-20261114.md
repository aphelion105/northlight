# Enhanced Weather cost and shelter reuse

The run ending at 2026-11-14 20:58:03 uses the installed shared-shadow DLL
(`04711041315a856f6bb30430dc58312fb878207800b9d117abb182d1ee5f7fd8`).
The private log is preserved at `.local/out/weather-sparse-20261114/renderer.log`.
It has 78 GPU profiles at 3840x2160. Forever remained open; this run also
overlapped independent GPU experiments. These timings cannot isolate game FPS.

| Observed GPU stage | Samples | Median | P90 | Maximum |
|---|---:|---:|---:|---:|
| Precipitation | 54 | 0.1755 ms | 0.178 ms | 1.749 ms |
| WeatherLens | 29 | 0.074 ms | 0.085 ms | 0.097 ms |

Enhanced Weather also changes native rain blending/coverage, mist suppression,
atmosphere and camera optics. Its toggle therefore does not isolate
`Precipitation`. Between frames 5400 and 7800, recorded frame-interval means have
medians 12.298 ms with Weather on and 11.558 ms with it off, but these are
unpaired intervals with changing scene, ContactAO and concurrent GPU load.
They are not an attributable 0.740 ms weather cost. At the end of the run,
Weather is off; PointShadow still takes about 1.0 ms and WorldComposite about
0.81 ms. Frame 10015 is a diagnostic capture stall, not steady weather cost.

## Why native shelter is insufficient

Native WoW clips its own precipitation. Northlight also adds procedural rain
and snow in `surface_effects.hlsl`; those samples are not native particles and
do not inherit their shelter visibility. The rain mask captures native streak
coverage for composition, not an outdoor/roof map. Native draw counts are not
a per-position visibility oracle and also depend on texture-detector state.

The [crypt regression](weather-roof-20261114.md) already demonstrates rain
between the camera and a distant wall under an off-screen ceiling. Camera
exposure alone cannot fix this without hiding exterior rain through a doorway.
Keep per-sample shelter. Its field uses the existing CPU GI BVH, without a new
geometry render or full-screen pass. It rebuilds on movement/publication, not
every frame. Lens exposure has a different five-ray, 80-yard upward query;
replacing it with the top-down roof value would change its semantics.

## Shipping CPU change

When the 128x128 world-aligned roof grid scrolls, retain cells whose actual
world-space ray origins are exactly unchanged. Reuse requires the same BVH
object, map and Z interval. A non-overlapping move, changed geometry or height
falls back to fresh rays. The worker still publishes an immutable grid, and
the render thread retains the existing coverage/generation checks.

The native test compares every height against a separate worker doing a full
rebuild, including reversed and diagonal movement, changed height and teleport:

| Movement | New BVH rays | Full rebuild |
|---|---:|---:|
| 8 yards horizontally, unchanged height | 1,280 | 16,384 |
| 8 yards on the other axis | 1,280 | 16,384 |
| Reverse diagonal | 2,460 | 16,384 |
| Changed height or non-overlapping teleport | 16,384 | 16,384 |

That is 92.2% fewer ray queries for the first two updates, not a 92.2% CPU
frame-time or FPS improvement. Hills and frequent BVH publications reduce the
opportunity. Diagnostic roof metadata now includes `traced_cells` per
publication. The field spacing, refresh distance, native precipitation,
particle appearance and shipping shader bytecode are unchanged.

## GPU experiment retained separately

`tests/fixtures/precipitation_roof_baseline.hlsl` freezes the production shader
at `64684a78`. `precipitation_sparse_roof_experiment.hlsl` postpones roof fetches
until a particle can contribute. `test_weather_sparse_roof_gpu.py` compiles
both and checks 720 exact image comparisons per backend: rain/snow, both
presets, size/intensity/time variation, covered/open/mixed fields, foreground
depth, coverage edges, UNORM/FP16 and 1x/4x MSAA. Timings use alternating order,
warmup and 4K FP16 targets. They measure shader work only.

The experiment passed 2,160 exact comparisons across system D3D9, DXVK 2 and
DXVK 3. Some DXVK 3 open-rain samples improved from roughly 0.102 to 0.095 ms,
but other cases and repeated runs regressed. Concurrent-game timing is marked
invalid for uncontended acceptance. This shader is **not enabled in the DLL**.
A further split-hash experiment changed pixels and was discarded. An early
roof/depth branch preserved pixels but did not provide a reliable gain either.
Private trial outputs remain under `.local/out/weather-sparse-20261114/`;
the first sparse trial is under
`.local/out/test-output/test_weather_sparse_roof_gpu/sparse-roof/`.

Run `python scripts/run_tests.py --suite weather-experiments --strict` for the
experimental shader and `python scripts/run_tests.py 'test_weather*.py'
validate_weather_occlusion_gpu.py --strict` for weather regressions.
With Forever open, set `NORTHLIGHT_TEST_ALLOW_RUNNING_GAME=1`. Acceptance
reports must be regenerated for the current tree. No live-game FPS gain is
claimed by these component checks.
