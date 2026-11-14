# Shared directional shadow resolve

The first implemented step toward common world/character shadows combines their
screen-space visibility filters. It does **not** merge their depth maps or raise
world-map resolution. See [the earlier profile and copied Forever evidence](shadow-pipeline-review-20261114.md)
for why the current actor map cannot simply be removed.

## Runtime change

With soft shadows, a valid actor map and no direction-history transition,
`UnifiedDirectionalShadow` evaluates the existing actor and world filters and
writes their minimum directly to the existing half-resolution FP16 visibility
target. A completely actor-occluded receiver skips the world filter.
`WorldLightingShared` consumes that result without fetching the separate actor
mask. Both sun and moon retain their own channels.

This removes one half-resolution filter draw per active source with an actor
map, the actor-mask clear/write and its lighting read. It retains both original
filter algorithms, contact support, caster admission, animation, cutouts, map
dimensions, update cadence and static caching. The actor min-depth reductions
remain useful to the filter and fog. No new texture is allocated, although the
old actor-mask allocation is retained for fallback.

The shader has 831 static SM3 instruction slots and 21 temporary registers.
Only this optional entry has a 1024-slot build allowance. Runtime checks the
device's actual instruction capacity and shader-creation success. A failed
attempt is not retried every frame; resource reset permits a retry. The shared
lighting shader uses 252 slots versus 256 in the separate-mask variant.

Hard shadows, insufficient device capability, resource failure and direction
transitions use the previous path. A failed shared draw explicitly reconstructs
the actor mask before ordinary lighting: those casters were already excluded
from the near world map and must not disappear on fallback. Direction-history
blending and its fog consumers retain their established behavior.

`WORLD_SHADOW_RESOLVE` records whether the shared path was requested and used.
On shared frames the actor filter cost moves from `ActorShadow` into
`ShadowFilter`; compare their **sum**, not the disappearing individual timer.
The map-raster and tile timers are separate and remain present.

## Image precision and validation

The former actor mask quantizes visibility to 8-bit UNORM. The combined result
stays FP16. This improves visibility precision, not spatial shadow resolution.
Fallback frames retain the old quantization. Both visibility consumers use the
same half-resolution grid and POINT sampling, so moving the minimum into the
filter does not reorder bilinear interpolation.

`test_shared_directional_gpu.py` checks 48 authored cases on system D3D9,
DXVK 2 and DXVK 3. The oracle runs the **unchanged** actor/world filters into
separate FP16 targets, then combines them. Cases include thin blockers, motion,
normal confidence, slopes, water/sky exclusion, disabled actors, and near/far
handoff. The development runs matched exactly in all 144 comparisons. A second
comparison bounds differences from the former 8-bit output; the largest observed
difference was 0.002441406 in visibility on system D3D9.

`test_world_screen_effects_gpu.py` exercises actual WorldRenderer routing with
MSAA off and 2x: one source, both sources, hard shadows, a 512-slot device,
shader-creation failure and shared-draw failure. It checks fallback actor draws,
resource release/retry and restoration of caller GPU state. Separate transition,
history, near-caster and lamp-constant tests cover the neighboring paths.

These are authored device/image contracts, not a captured gameplay benchmark.
The latest available game log still predates this implementation. Forever stays
open by user request. The ABBA timing section uses 256 repetitions per block,
four warm-up blocks and six measured blocks per mode; reports mark uncontended
timing invalid while a game is running. Shorter development blocks showed large
scheduling variance and cannot establish an FPS gain. Saved development and
acceptance evidence is under `.local/out/shared-shadows-20261114/`.

With the longer blocks, the observed DXVK 3 medians for separate/shared filters
were 0.191/0.159 ms (lit), 0.623/0.486 ms (blocked), 0.292/0.247 ms (world
blockers), 0.443/0.379 ms (mixed edges), and 0.225/0.162 ms (sloped receivers).
All five observed medians also decreased on system D3D9 and DXVK 2. These are
contended stand measurements; neither the percentages nor these authored scene
costs can be projected onto game FPS. Baseline timing excludes the oracle's
comparison-only combine draw, so it does not charge the old renderer extra work.

## More detailed world shadows: next structural change

The actor map has 1024 texels across 24 yards; the near world map has 1024 across
96 yards. A uniform 4096 near map would match actor density but increase its
texel area sixteenfold. Merely removing the actor map at the current world
resolution loses detail; increasing the whole map is not a cost reduction.

A more promising common map is a high-density near cascade covering actors
**and nearby world geometry**, backed by a retained static-depth cache. That
requires a complete caster-coverage contract before it can replace the coarse
near result. Taking the minimum with the old coarse world shadow would retain
its coarse edge and defeat part of the quality improvement.

The present actor projection follows the continuous light direction and a
leashed, unsnapped player center. The world cache uses its own stabilized
projection, update policy and direction history. Drawing world geometry into
the actor map unchanged would repeat static draws on every refresh; reusing the
coarse cache would retain its coarse detail. A genuine fine cascade must handle
static-cache margins, animated-caster overlap, cutouts, coverage at its border,
and continuous transitions together. Those changes are not hidden inside this
resolve optimization and are not claimed as implemented or benchmarked here.

Lamp cubemaps remain a separate cost: merging directional cascades cannot remove
the different projections required for light arriving from individual lamps.
