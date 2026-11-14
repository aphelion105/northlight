# Common fine world/character cascade experiment

Status: implemented in a **test-only WorldRenderer overlay**, not enabled in the
production DLL. The requested performance improvement is **not established**.
Do not mistake these files for a shipped renderer feature or remove the current
actor map on the strength of the component tests.

## What is implemented

The existing 1024² actor target becomes a common fine depth map, covering 24 world
units. Nearby buildings, terrain and current animated/cutout replays enter it.
This retains the actor texel size (24/1024) and gives nearby world geometry four
times the linear density of the existing 96/1024 near-world map. The existing
near and far world cascades cover the surrounding area.

There is no separate actor-only depth draw in this candidate. A signed R32F
value distinguishes actor and world casters without sacrificing depth mantissa
bits. World statics use a separate exact cache; a full-map seed writes its colour
depth and hardware Z, then animated casters render into the same final target.
The redundant actor clear is omitted because seeding overwrites every texel.

The static cache requires a bit-identical fine projection, mesh generation and
static-content signature. Live terrain prevents reuse. Light motion is not
quantized or frozen to make the benchmark faster. This means continuous light
motion can invalidate the fine cache on almost every frame, even when the coarse
world cache remains reusable.

The filter uses fine visibility inside the common cascade and blends back to
the established coarse world filter at its boundary. It does not take the
minimum with the coarse *static* world shadow throughout the fine area, which
would preserve the coarse edge and obscure the intended detail improvement.
The actor softness cap remains 0.1 world units; world blockers retain the broader
0.3984375 cap. Mixed blockers use a weighted cap and therefore are **not** a
pixel-identical replacement for the old two-domain PCSS algorithm.

Exact block bounds and occupancy moments skip PCF work only when the receiver
plane proves the entire block lit/blocked, or its tent weights can be integrated
analytically. Other blocks retain every fine texel comparison. At the broadest
world penumbra this can still require a 35×35 support instead of the old 9×9.

## Compare the complete costs

The relevant baseline is **current World Shadows + all Detailed Character
Shadow work**, including both geometry paths, static cache work, reductions and
the existing combined visibility resolve. The candidate is the entire world
shadow system with its common near map, fine static cache, seeding, reductions
and resolve. Comparing a disappearing actor timer alone is invalid.

`test_common_shadow_pipeline_gpu.py` compiles the actual current WorldRenderer
twice conceptually in one executable: unchanged behavior with the experiment
disabled, and the injected common route. Both use the same authored scene,
camera, 48 independently animated bone banks, materials and settings. The scene
has 128 static building/cutout quads plus terrain. Both routes retain the current
near/far update intervals. Production files and the installed client are not
modified by this test.

Four scenarios cover a stationary scene, actor motion, camera plus continuous
light motion within a coarse direction-lattice cell, and geometry publication
every eight frames. Each has ABBA blocks, four warm-up blocks and six measured
blocks per mode, with 32 full frames per block. Output records:

- `shadowGpuMs`: map construction/preparation plus all shadow reductions and
  visibility filtering; both old domains are included.
- `shadowCpuMs`: CPU submission/preparation over those same regions. The first
  region includes shared renderer setup and replay preparation before the maps.
- `fullGpuMs` / `fullCpuMs`: the entire fixture WorldRenderer call, including the
  common normals/lighting/composition work outside the shadow regions.
- Draw counts and cache refresh/reuse counts, independent of periodic game-log
  counter resets. CPU and GPU times overlap; never add them together.

The actor test fixture must assign distinct `constantGroup` values to distinct
bone banks. An early development run omitted that capture invariant, so actors
overlapped. **`pipeline-dev2` is invalid as a scene/performance comparison.**
`pipeline-dev3` and subsequent runs fixed it. Older unsigned log-counter deltas
were also replaced by direct device-side draw/clear counting.

The earlier `test_common_shadow_cascade_gpu.py` is only a component experiment.
Its stage 0 measures resolve; stage 1 also charges depth union and summaries;
stage 2 gives the pre-unified depth map for free. None of those stages includes
geometry, so they cannot decide the user's complete-pipeline comparison.

## Observed development results

All three backends ran: system D3D9, DXVK 2 and DXVK 3. Forever remained open as
requested. Timings are contended, include scheduling stalls, and are not game
FPS. There was substantial run-to-run variation. The following DXVK 3 medians
are from `pipeline-three-backends`, after adding the exact static cache:

| Scenario | Old total GPU ms | Common total GPU ms | Old total CPU ms | Common total CPU ms |
| --- | ---: | ---: | ---: | ---: |
| Stationary | 0.093883 | 0.207220 | 0.058189 | 0.056611 |
| Actor motion | 0.074743 | 0.112256 | 0.052933 | 0.053689 |
| Camera/light motion | 0.100104 | 0.187360 | 0.063637 | 0.063655 |
| Geometry publications | 0.123494 | 0.111814 | 0.062611 | 0.067572 |

This is not a consistent speedup. The deterministic work counters explain why:
the stationary old route averages 62.5 map draws/frame, versus 75.5 for the
cached common route. Both have three reduction/resolve draws. The candidate
adds twelve boundary-caster draws and one cache seed. Without the fine static
cache it averaged 102.5 map draws/frame. With camera/light motion it averages
102.625 because the exact cache rebuilt on 31 of 32 measured frames.

The current production renderer already avoids drawing the fully contained
actors into both near maps, and already combines their visibility filters
([previous implementation](shared-directional-shadows-20261114.md)). There is
no remaining full actor geometry/resolve pass to simply delete a second time.
Adding fine world coverage introduces work that a common-target name alone
does not remove. A future design must reduce this actual geometry/filter work,
not just move it between timer labels or narrow the existing penumbra silently.

## Validation and limits

The component test checks depth union and GPU summaries byte-for-byte against
CPU-generated values, then compares the optimized filter to a full-tap GPU
reference. An independent double-precision CPU PCF oracle also covers slopes,
clamped map borders and support radii up to 17 fine texels. For depth comparisons
within the explicitly bounded FP32 rounding interval, the CPU oracle produces
a visibility interval rather than pretending the comparison is unambiguous.
Reports include the interval width and number of ambiguous queries. NaN and
out-of-range results fail.

The full renderer test verifies retained actor coverage against the baseline
depth map, unchanged coarse-map pixels in the stationary comparison, exact
cached-versus-redrawn common depth, and moving-camera/actor coverage. It saves
baseline/common visibility images. These are synthetic device contracts.

**Production acceptance remains incomplete:** direction-history transitions,
volume-fog consumers of the former actor-only map, full material/native-alpha
coverage, failure/reset/memory-admission routes, streamed instanced world
geometry, and visual continuity at the fine boundary still need complete
integration and validation. The wider mixed penumbra also needs a new admission
proof: the old actor `Stay=.86` proof was sized for the 0.1 actor penumbra.

In particular, do not reuse a coarse static draw plan under a slightly different
fine light direction without a caster-coverage proof. The static directional
shader clamps upstream depth: relevant casters can lie before z=0. Containing
only the finite fine receiver frustum does not establish caster containment.

## Reproduce and preserve

The candidate is in `tests/fixtures/common_shadow_cascade.hlsl` and
`tests/fixtures/common_shadow_renderer.inl`; the full-scene fixture is
`tests/fixtures/common_shadow_benchmark.inl`. The runner creates a guarded,
hashed overlay of the production header under the output directory. Hook drift
fails instead of silently testing another code path. No runtime AB switch is
added to the game.

On Windows, run elevated through the normal tool approval mechanism:

```powershell
$env:NORTHLIGHT_TEST_ALLOW_RUNNING_GAME='1'
python scripts/run_tests.py --suite shadow-experiments --strict
```

Reports and development evidence are under
`.local/out/common-cascade-20261114/`. Source checks use the `source` suite.
These test artifacts intentionally do not enter the DLL, asset pack or installer.
The installed production DLL remains the `6eae4dad` shared-resolve build.
