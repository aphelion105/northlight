# Interactive cage shadow investigation

The tester supplied two short Gjalerbron / Howling Fjord recordings and a ZIP
containing one renderer log, not a recorded D3D draw stream. The log identifies
source `78230609d33855625b2bbac96122660fb6886a37d18b1f18b4958264150de8d8`,
built at 2026-11-09 16:17:05 UTC, DXVK 3.1.1, an RTX 3080, 2560x1440 SDR and
no MSAA. Private inputs and experiment receipts are under
`out/cage-20261109/`; game assets, recordings and raw logs are excluded from Git.

## What the supplied evidence establishes

The recordings show changing shading around the cage group while the camera
moves. They do not identify the exact submitted mesh, shader pair or pose of
each affected draw. `vr_cage_02.m2` is a relevant stock interactive-cage
candidate, rather than a runtime identity proved by the log.

Stock MPQ inspection finds that this candidate has 1,600 vertices, 1,382
triangles, six bones and one two-sided, alpha-tested material. Its vertices
reference four bone indices. The related base has 68 triangles and one bone;
the top has separate 814-triangle opaque and 42-triangle cutout batches.
The candidate's texture-weight track is constant one. That does not prove
that its game-supplied, model-wide diffuse opacity is always one.

No direct cage placement occurs in the nine queried Northrend ADTs around
Gjalerbron (tile X 26..28, Y 37..39). This is not a complete proof about WMO
doodads or all static-cache content. The supplied log reports zero static-M2
dedup skips/verifications and an unlimited actor-shadow selection budget.
Neither static dedup nor quota eviction is established as the cause.

There are 345 actor-map diagnostic samples in the selected Gjalerbron camera
region. They report 647 map-side transitions in total, but lack the affected
caster identities. A transition can also be a legitimate crossing of the
actor-map boundary. The diagnostic data does not associate these transitions
with a particular cage. No shadow-fate capture window was recorded.

The old MRT path commits no sampled frames: 373 samples report
`vertex-provenance`, 41 report `blend-srgb`, and ten report `none`. The separate
draw-prologue correction addresses the stale per-draw provenance. A general
rollback frame still uses the original full normal replay.

## Real-cage controls

The private stand extracts the candidate mesh and `vr_cage01.blp` from stock
archives. It uses explicit synthetic, unequal bone rotations and translations;
these are not the unrecorded gameplay poses.

- `normal-oracle-allowed/validation.json`: 4,698 correctness frames on each of
  native D3D9 and DXVK 3, 9,396 total. Native/MRT colors and model normals,
  whole-frame rollback, occluder ownership and state restoration pass on this
  real mesh with rigid/single/weighted stock vertex programs. The texture and
  neutral material-height controls retain the existing stand's scope.
- `near-oracle/validation.json`: 252 comparisons on each backend, 504 total.
  The production shadow renderer uses the candidate mesh and its decoded
  128x128 cutout texture, different bone transforms, both light sources,
  actor detail on/off, and near intervals 1 versus 2/4/16. Current poses match
  the interval-one control through map changes, environment reuse and fallback.
  This does not compare the actor filter's appearance against a different
  resolution or reproduce the tester's complete scene.

These controls have not reproduced the exact video flicker. They do not prove
that every interactive-object issue is fixed. They are correctness checks;
concurrent game execution was explicitly allowed, and their timings are not
performance measurements.

## Confirmed coverage defect

The shadow-capture path previously treated fixed-function alpha testing as a
test of texture alpha alone. A stock M2 pixel program can instead output
`texture alpha * COLOR0 alpha`. For texture alpha 0.75, diffuse alpha 0.5 and
reference 0.5, the native draw rejects the pixel while a texture-only shadow
replay retains it. Model-wide opacity can expose this defect independently
of the asset's authored texture-weight track.

The bytecode proof now recognizes the final alpha output of supported PS2/PS3
programs: texture alpha, diffuse alpha, or their product. Ambiguous sampling,
output modifiers, flow or additional discard operations are not accepted as
this proof. The descriptor retains the native reference, comparison and source
render-target format through retained rigid packets and merged point runs.

An R32F shadow target cannot itself preserve every native alpha-test boundary:
ARGB8, FP16 and FP32 targets have different alpha-test conversion precision.
The exact path binds an owned scratch RT0 with the source format, disables
its color writes, and writes the existing R32F shadow depth through RT1. Native
alpha testing consequently uses the source format's conversion. Both shadow
shaders preserve their previous depth values. Directional, actor, point,
flash and diagnostic shadow replays use this same wrapper. Model-normal replay
is outside this change.

The wrapper adds no raster draw or fullscreen pass. A fixed-alpha packet has
nine device getters, ten setup setters, ten restoration setters and one
surface-description query around its existing draw. Proved native-alpha
capture also reads RT0 and its description. These deterministic call costs are
a correctness tradeoff; this investigation makes no FPS or timing claim.

Scratch storage has eight direct slots: two actual dynamic shadow sizes
(256 and 1024) times four source formats (ARGB8, XRGB8, FP16 and FP32). Creation
is lazy, and alternating formats reuse their own slots. One SDR format at both
sizes uses 4.25 MiB of logical DEFAULT-pool storage; the full eight-slot bound
is 34 MiB. These are resource sizes, not measured resident VRAM. Every slot
passes growth admission, appears in the shadow-map memory inventory, and is
released on GPU reset/destruction or memory-pressure trimming.

Unsupported MRT capabilities or source formats retain the proved legacy mask
recipe at capture. This improves product-alpha coverage but does not promise
native precision at every threshold on unsupported hardware. It does not
allocate or retry a permanently unsupported target. A temporary allocation or
growth-admission refusal cancels the map job, invalidates directional/actor/
point/flash validity and temporal history, and pauses attempts for one second.
It neither publishes a partial map nor permanently disables WorldRenderer.
The next attempt consumes the current frame's captured pose.

The confirmed coverage defect is relevant to interactive cutout coverage. Its
exact causal connection to the two recordings is not established by the log.

## Acceptance receipts

`out/cage-20261109/alpha-exact-scissor/validation.json` records 32 combinations:
native D3D9 and DXVK 1/2/3, stock PS2/PS3 `Combiners_Mod`, and source ARGB8,
XRGB8, FP16 and FP32. All 11,520 independent native-vs-replay cases match binary
coverage and exact retained R32 depth. The frozen old shaders differ by
1,678,232 pixels across the same comparisons. References include 0, 1, 63,
64, 127, 128, 191, 254 and 255, both `>` and `>=`, and five diffuse opacities.
The stand checks the actual in-draw RT0 format, RT1 destination and zero scratch
write mask, full and partial scissor rectangles, and restored scissor state.
It also passes 672 setup/draw failure-restoration cases, warm
interleaving of all eight resource slots, exact 34 MiB accounting, and release.
The receipt hashes the tested sources and rejects changes during the run.

The post-fix real-mesh controls are `normal-oracle-final/validation.json`
(4,698 frames on each of native D3D9 and DXVK 3) and
`near-oracle-scissor-final/validation.json` (252 comparisons on each). The latter uses
the fixed native XRGB8 descriptor and synthetic pose-dependent diffuse opacity,
so it exercises the exact shadow MRT path on the candidate cage and its cutout
texture, alongside actor-map and retained-near controls.

Related CPU binding, near-reserve and render-thread probe checks pass with
O2 and ASan/UBSan in
`out/cage-20261109/linux-resource-final/test-output/20261109-203723/`.

`near-resource-scissor-final/validation.json` passes on native D3D9 and DXVK 3:
252 near/actor comparisons and four actor-settings cases per backend, plus
the new map-level scratch-refusal regression. Growth denial cancels the job
without a composite or valid partial maps, the immediate repeat remains
paused, and recovery matches a fresh control at a newly captured pose.
The independent alpha stand separately injects a null scratch acquisition
(the allocation-failure result) and verifies unchanged target/state bindings.
