# Local shadows and geometry capture, 17 November 2026

This continues [the effect investigation](effect-speedup-20261117.md). The latest
game log is still the 14:16:54 run; no new gameplay/FPS result is established.
The changes preserve resolution, sample counts, light limits, refresh intervals,
geometry, textures and admission budgets.

## Point-shadow geometry

Changing the global mesh cells from four terrain chunks to one improved static
point shadows but regressed directional shadow submission by 30–40%. That
experiment is not enabled. The production `PlanOrder` remains unchanged.

Instead, worker preparation partitions sufficiently large nonterrain batches
into 16-unit centroid cells **inside each existing material and page**. It only
reorders complete index triples; vertex bytes, triangle corners, material
ownership, coarse bounds, page boundaries and GPU buffer sizes stay unchanged.
Each cell's bounds include all of its triangle vertices, including triangles
crossing cell boundaries. Terrain and proven lamp enclosures stay whole.

Directional passes keep the original coarse batches. Point shadows first reject
their coarse bounds, then classify the fine ranges only for survivors. Existing
alpha/transmission filtering, sphere clipping, page binding and primitive limits
remain in force. Unknown/excluded ranges remain barriers to draw merging.

Fine metadata follows the same page payload, preparation memo, atomic mesh commit,
retirement accounting and file-backed Reset recovery. It is capped at 65,536
subranges and contains no new GPU allocation. Optional partition allocation
refusal retains the complete coarse draw. In the tested Northrend subset, the
metadata adds about 261 KiB.

`test_point_shadow_spatial_gpu.py` compares the unpartitioned and production
paths with the same shaders and production merger on native D3D9 and DXVK. Its
two real lamps come from the frozen log. The loader's 64-unit box contains both
complete light spheres, so the point comparison includes their relevant static
casters. Directional controls use that same subset and are not a full-city
performance measurement. The static six-face pass is approximately twice as
fast on DXVK; all compared cube and directional texels are bit-identical.
Animated replay, cache reuse and game FPS are outside this benchmark's scope.

## Single-bone capture bounds

The FLOAT3/UBYTE4 fallback now transposes each used bone matrix once per draw
and computes X/Y/Z in SIMD lanes. All four separate DP4 multiply/add steps,
their initial zero, the following camera transform and min/max tie rules retain
the original order. Current palette values are used on every call. There is no
cross-frame palette cache or change to deformation, envelope eligibility or the
logical vertex allowance.

Only the audited single-bone kernel with control values 3/1 and no palette DEF
overrides enters this path. Other declarations, controls and overrides retain
the original evaluator. Invalid indices, nonfinite results and partial bounds
on failure preserve the previous behavior.

`test_single_skin_bounds.py --benchmark` compares against `fe00d60a` with changing
palettes, camera bases, separate streams, bone 74, malformed indices and IEEE
special values. O2/O0 comparisons require bit-identical bounds. The measured
71–8,192-vertex components improved approximately 5–9 times. This is the vertex
bounds fallback, not all pose envelopes or the complete capture hook.

## Validation and remaining work

Run source and architecture suites, affected point-shadow/material GPU tests,
the stock actor-kernel corpus, page/preparation lifetime tests and the production
DLL build. Raw measurements and delivery records belong under
`.local/out/remaining-effects-20261117/`.

The whole-renderer temporal comparison exposed one FP16 RGB rounding step in one
channel of one of 198 images. Its zero-weight comparison now permits at most one
FP16 ULP and at most 0.000244 absolute RGB error; alpha and non-FP16 images remain
exact. Negative controls reject two ULPs, changed alpha and nonfinite values.
No production temporal shader or quality setting changed.

The latest log still has substantial animated point-shadow replay and model-normal
replay work. Native normal MRT refuses the MSAA scene (`target-msaa`); enabling it
requires a matching coverage/depth/resolve contract and memory validation. AO,
GI, composite and temporal passes do not yet have a demonstrated multiplicative
gain. Existing fog acceleration is retained. A matched gameplay run must establish
the combined benefit and which remaining stage dominates after these changes.
