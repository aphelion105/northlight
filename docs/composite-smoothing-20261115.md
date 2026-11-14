# WorldComposite and lighting smoothing, 2026-11-15

Baseline: `6e981fe2362a4584a64e93d5b2040e00b6849e24`. The latest gameplay log
still ends at 00:03:31 and predates that CPU-only build. Its roughly 0.599 ms
WorldComposite and 0.408 ms RemovalSmooth identify targets, but cannot measure
this change. Tests below use private D3D9 devices on the RTX 4090. Forever stayed
open, as requested; background contention changes absolute times substantially.

## Changes

The existing HDR world/material compositor now shares its scene color, depth,
water test, view distance and native-fog transmission with material relighting.
Its SM3 instruction count falls from 755 to 699, with 22 temporary registers.
It no longer fetches the same native scene through both s0 and s5. The existing
backend policy for HDR material fusion remains in force. Separate, unlit,
prelit and non-HDR entries compile to the same bytecode as the baseline.

`TemporalLightMerged` runs the unchanged 24-tap removal filter and temporal
accumulation in one half-resolution draw. It replaces the intermediate FP16
write/read, retaining current normals/distance in s14/s15 and reading previous
light/depth through s9/s13. It compiles to 402 slots and 21 temporary registers,
inside the ordinary 512-slot SM3 gate. The separate shaders use 227 and 210
slots. This supersedes the old comment that fusion cannot fit that budget.

The raw lighting buffer remains available to the original neighbourhood clamp.
History reprojection, rejection, visibility margin, filter radius, weights,
normal confidence and sample count are unchanged. Eliminating the intermediate
FP16 rounding can change RGB slightly; it does not change shadow visibility.
No additional textures or per-frame allocations are introduced. Existing fog
scratch remains allocated because other consumers still use it.

Normal frames fuse only when WorldNormals produced current distance. No-source
frames retain the raw-light path. Diagnostic captures retain separate smoothing
to export its intermediate image. Optional shader creation failure retains the
old graph without disabling the renderer or repeatedly trying creation; reset
allows a retry. Regional fog regains s13 before its consumers. The ordinary
renderer state guard handles failed draws and restores native bindings.

The sampled `WORLD temporal` line records `mergedRemoval`, `removalRequired`
and `diagnostic`. Compare **old RemovalSmooth + old TemporalLight** with **new
RemovalSmooth + new TemporalLight**. The merged filter's cost is charged to
TemporalLight; a near-zero RemovalSmooth label alone does not establish a gain.

## Image and ownership evidence

Evidence root: `.local/out/composite-smoothing-20261115/`.

* `composite-bulk/`: five authored HDR graph cases and one case using saved
  material/scene/depth inputs. All pixels are byte-identical, including alpha.
  Cases cover 1080p/4K, MSAA 0/2/4, expanded HDR, odd sizes, sky and names.
  A material-disabled negative control verifies that material detail contributes.
* `smoothing-final-dxvk3-ab/` and `smoothing-final-dxvk2-ab/`: 48-frame authored
  sequences with moving reprojection, visibility changes, history reset, depth
  edges, normal/confidence edges, sky, no source, near/far and dense native fog.
  Alpha is exact; no nonfinite or out-of-tolerance output. Largest absolute RGB
  difference is 0.00390625 in the high-value fog fixture; ordinary/captured
  inputs differ by at most 0.000244140625. The explicit acceptance bound is
  `0.002 + 0.001 * abs(reference)` per RGB component.
* `runtime-trial2/world-temporal-merge/`: actual WorldRenderer on DXVK 3, DXVK 2
  and system D3D9, 72 frames per variant/backend. Each backend compares 249
  exported images byte-for-byte, including final color, temporal light, fog and
  diagnostics. Tests exercise moving silhouettes, odd resize, three partial
  allocation failures, resets, optional shader refusal and failed-draw state
  restoration. The authored fixture validates ownership; the separate shader
  fixture exercises nontrivial removal filtering. Sixty eligible ordinary
  frames use one draw, three diagnostic frames retain separate smoothing.

Saved inputs originate from own-client session `60800-280247484-0/capture-1`.
The smoothing replay uses a **neutral authored AO texture** because that export
was absent, and authored history. The composite graph also contains authored
lighting/fog/HDR-tail inputs. Neither replay is a complete captured game frame.

## GPU component timings

ABBA blocks alternate the old and new graph on the same device; A/A repeats the
old graph under both labels. Compilation does not overlap timing. Smoothing has
16 ABBA blocks (64 measurements, 32 draws each). The following medians describe
the component fixture, not gameplay FPS:

| Backend / input | Old two passes, ms | Fused, ms | Median paired saving | Paired A/A shift |
|---|---:|---:|---:|---:|
| DXVK 3, authored 4K | 2.864 | 2.088 | 27.1% | 0.9% |
| DXVK 3, saved inputs | 0.807 | 0.749 | 6.6% | 0.2% |
| DXVK 2, authored 4K | 2.968 | 2.358 | 19.6% | 1.6% |
| DXVK 2, saved inputs | 4.236 | 3.715 | 12.3% | 0.4% |

All 16 saved-input DXVK 3 blocks favour fusion; 15/16 blocks do so in the other
three comparisons. Absolute A/A costs differ markedly from A/B because Forever
continues running; do not compare measurements across those runs as a frame-time
estimate. The light targets in these 4K cases are 1920×1080.

WorldComposite repeat (`composite-final-ab/`, `composite-final-aa/`):

| Input | Old draw, ms | New draw, ms | Median paired saving | Paired A/A shift |
|---|---:|---:|---:|---:|
| Authored 1080p | 0.277 | 0.257 | 4.9% | 1.9% |
| Authored 4K | 1.369 | 1.263 | 8.5% | -0.6% |
| Authored 4K/MSAA2 | 1.224 | 1.130 | 1.0% | 2.2% |
| Saved inputs 4K/MSAA2 | 0.950 | 0.895 | 6.6% | 4.1% |
| Expanded HDR 1080p/MSAA4 | 0.169 | 0.159 | 8.3% | -1.7% |

Each case has ten ABBA blocks, 256 draws per measurement. Initial bulk runs
showed 6–11% paired savings in the authored 4K cases, but ordinary 1080p was
inconclusive. The repeat's 4K/MSAA2 saving is below its A/A variation; saved-input
timings are also noisy. The supported conclusion is a smaller shader with
exact images and improvements in several workloads, not a uniform percentage
on every scene. Full HDR graph timings contain unchanged passes and submission
cost. Do not equate shader slots or isolated draw percentages with overall FPS.

## Reproduction and release checks

`python scripts/run_tests.py --suite lighting-composite --strict` runs the new
shader and actual-renderer image contracts plus existing screen/capture/assets
GPU checks. The source and architecture suites are also required for this change.
New tests are in the `gpu` suite and discovery inventory.

For component timings use `tests/test_world_composite_inputs_gpu.py --benchmark`
and `tests/test_removal_temporal_merge_gpu.py --benchmark`, then repeat each with
`--control`. Optional `--capture-prefix` requires the specified saved inputs;
the smoothing runner accepts `--backend`. Run Windows compilation/GPU tools
outside the restricted sandbox and explicitly allow the background game with
`NORTHLIGHT_TEST_ALLOW_RUNNING_GAME=1`. Artifacts default to `fp.output_dir()`.

Build with `python scripts/build_renderer.py`. The new shader is externalized
by the existing asset builder, so install **both** the new DLL and its matching
`northlight-assets.bin`. Retain the previous pair for rollback. Production
build, package verification and current-tree suite reports are recorded in the
local acceptance report; standalone component checks do not prove game FPS.
