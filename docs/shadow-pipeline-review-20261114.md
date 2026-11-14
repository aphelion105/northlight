# Shadow cost and the copied Forever pipeline

## Which run was measured

The current `northlight-renderer.log` ends at 2026-11-14 17:55:02 local time.
It identifies source `bd8e5d429a2447972b4edb28ace5a1eda6caf807bc598bdefb3d58516df6dc44`,
built at 15:19:06 UTC. It predates the rain-roof DLL installed at 18:05.
The previous log is preserved separately. Neither is a benchmark of this change.
Private inputs, hashes and numeric summaries are in
`.local/out/shadow-pipeline-review-20261114/{renderer.log,renderer.prev.log,log-analysis.json}`.

Most of the long run has `focus=0`. Its 16–19 ms effects totals cannot be mixed
with focused play. The final stable window is renderer frames **41038–41729**,
after ActorShadows returned to 1. Five GPU/CPU samples and six 120-frame intervals
remain; the intervals report focus 1. Medians:

| Measurement | ms |
|---|---:|
| GPU frame | 9.121 |
| Northlight GPU effects | 5.433 |
| Point-shadow cube construction | 0.709 |
| Applying shadowed point lights | 0.867 |
| Lighting removal smoothing | 0.722 |
| World composite | 0.488 |
| World shadow filter | 0.206 |
| Detailed actor map raster | 0.014 |
| Detailed actor filter | 0.051 |
| Actor min-depth tiles | 0.006 |
| Northlight CPU profile total | 2.377 |
| CPU effects | 1.594 |
| CPU point-shadow bucket | 0.801 |

These are separate distributions, not additive CPU/GPU costs. Median interval
FPS is 93.14, not a predicted FPS after optimization. The focused pre-toggle
window 38000–38990 gives similar effects (5.608 ms), point construction
(0.861 ms) and actor filter (0.046 ms). In the previous run, frames 600–2199
give 6.186 ms effects, 0.774 ms point construction, 1.094 ms point lighting and
0.047 ms actor filter; this is a different run, not a paired comparison.

Frame 41656 has 14 selected local sources, 44 terrain draws and 355 replay
draws. Existing coalescing already merged 847 replay packets. Point preparation
takes 0.484 ms, of which replay candidate preparation is 0.036 ms; submission
is 0.250 ms. There are no cold GPU allocations or static-face rebuilds there.

ActorShadows was off only during frames 40868–41007. That toggle also removes
model casters from local point shadows, including eligible rigid models; it is
not an isolated ActorShadowDetail comparison. No stable GPU/CPU sample survives
inside that short window after excluding the transition. WorldShadows also
gates point shadows. Neither toggle's apparent FPS change can be attributed
entirely to the directional actor map.

## Keep the detailed map for now

Northlight has one shared 1024² actor map per active celestial source, covering
24 yards across. The near world map has the same resolution over 96 yards:
4 times larger texels. Putting actors only in that map loses silhouette/contact
detail. Matching the actor density over the whole near map would require 4096²
texels, 16 times its area, before filtering costs. These dimensions are defined
in `actor_shadow_map.h` and `shadow_cascades.h`.

The actor map is not an unconditional duplicate raster. In frame 41656,
`WORLD actor shadow map` reports 44 actors drawn and the same 44 skipped in
the near cascade. Unproven or boundary casters retain the conservative fallback.
Turning ActorShadowDetail off already routes actors into the near cascade.

The detailed raster/filter/tiles total about 0.071 ms in the stable window.
Merging them would not remove the 399 local-shadow draws: each lamp needs
occlusion from its own position, whereas the world cascades represent the
sun/moon direction. A unified resource or shader family does not remove those
different projections. Native dynamic shadow-map suppression is already owned
by Northlight; see [the pipeline audit](pipeline-audit-20261114.md).

## CPU change without changing shadow output

`preparePointTerrain` retains the six face-membership bits for each immutable
committed terrain batch. Previously every refreshed light repeated the sphere
and plane tests even with an unchanged light and mesh. Cache identity is the
committed mesh generation, batch count, exact light position and attenuation
end. Near plane and cube resolution are constants in this path.

Only a full six-face classification can populate the cache. A partial update
intersects those bits with its requested faces; a cold partial update uses the
original calculation. Live terrain and captured model poses still get fresh
bounds/classification each refresh. Static candidates, batch order, gap proofs,
draw calls, maps, filters, resolution, light limits and update cadence are unchanged.

Storage is at most 64 KiB per active slot, 4 MiB for 64 lights. Larger meshes
fall back to the existing calculation. Allocation failure also falls back;
inactive static keeps discard the memo. Reset/map/resource teardown releases it.
`POINT_PHASE` now reports terrain memo hits/misses and list/batch sizes so the
next game log can establish how often this optimization actually applies.

`test_point_shadow_stutter_gpu.py` compares the candidate lists, masks and cull
counts against the uncached algorithm from adaeaa4a. It checks full/partial
faces, moved/resized lights, mesh mutation, list fallback, the size cap and
resource retention/reset. GPU checks compare all six faces exactly, including
warm/cold refresh, moving actors, alpha cutouts, live terrain and partial cycles.
The development run passed on system D3D9, DXVK 2.7.1 and DXVK 3.1.1.

An ABBA CPU-only section (14 lights, 2048 authored batches, 32 iterations per
block, four warm-up blocks and six measured blocks per mode) observed
0.466–0.476 ms uncached versus 0.098–0.104 ms cached across those runs. A game
process remained open: the harness correctly marks uncontended timing invalid.
These raw CPU observations support removing repeated work, not a clean GPU
speedup or a gameplay FPS claim. Development evidence is in `gpu-candidates-dev/`;
final acceptance reports must match the final source tree.

The next GPU targets are point-light evaluation and removal smoothing. The
latter is a neighborhood operation, not another shadow-map raster. Blindly
folding it into lighting repeats neighbor work and risks the SM3 instruction
budget; previous rejected fusion experiments are documented in the pipeline
audit. This change does not silently lower quality to remove either pass.

## Forever: copied-file evidence, not a live capture

All examination used `.local/research/forever-shadow-20261114/`. The original
`D:\Battle.net\Games` was used **only as a copy source**. No client launch,
attachment, edits, source hashing or separate source-file inspection occurred.
Copied files are `Wow.exe`, `Config.wtf`, `gx.log`, `.build.info`, CASC metadata
and the five required archives `data.011`, `.012`, `.013`, `.061`, `.062`.
Account/config and proprietary shader files remain private and untracked.

The copy identifies **1.60.1.70938**; copied Wow.exe SHA-256 is
`7d6d40d3b7dc326ff936ece92e52f86742d7895d8e4701545e4412bf2a653b0f`.
`evidence-manifest.json` records examined-file hashes. The copied config requests
D3D12, four 2048 shadow cascades, soft shadows, cascade blending and an entity
shadow fade scale of 40. Copied gx.log says `Using shader family SM6`.
Those facts do not identify every active runtime shader branch.

The extractor recovered 28 of 60 selected named BLS packages. Other entries
had open/size errors in the partial local storage; **unavailable is not absent**.
Listfile names are discovery aids; FileDataID, bytes and hashes identify evidence.
The versioned extractor is `scripts/inspect_forever_shadows.py`. Its optional
decoder recovers DXBC containers from HSXG zlib blocks, not the full permutation
table. Recovered-program counts must not be called runtime variant counts.

| Copied evidence | What it establishes |
|---|---|
| FileDataID 2991129, `vertex/dx_6_0/shadowmap.bls` | 33 recovered programs; both rigid and BLENDWEIGHT/BLENDINDICES input variants; shared ShadowMap constant layout includes 256 3×4 bone transforms. A common shadow-map shader family supports animated geometry. |
| 2990918, `pixel/dx_6_0/shadowmask.bls` | A separate mask/filter program with `PSShadowData`, `CSMInfo`, `VSMInfo` and `PSWorldShadowData`; comparison samples from a 2D texture array. CSM layout has five cascade records, VSM has twelve clipmap records. |
| Same mask, LLVM lines around 528–565 and 1355–1370 | The ordinary cascade selector excludes an optional last cascade; another branch selects count−1 when the flag is set. This is evidence against assuming all shadow processing is simply one undifferentiated map. The semantic identity of that last cascade is not proved. |
| 3670277, `compute/dx_6_0/lightbufferomnishadow.bls` | Recovered programs include RaytracingAccelerationStructure/ShadowRT contracts. Their presence does not prove ray tracing is enabled, nor justify treating the name as a raster cubemap implementation. |
| Copied executable strings at file offsets 0x41a9d98, 0x41ad108, 0x41ad138 | `ShadowCache`, `VirtualShadowMap`, `CascadedShadowMap` exist. Strings alone do not prove which path runs. |

Reproducible extraction/disassembly (all input paths below are local copies or
previously saved tools):

```powershell
python scripts/inspect_forever_shadows.py --storage .local/research/forever-shadow-20261114/client --casclib .local/out/forever-atmosphere-70888/CascLib.dll --listfile .local/out/forever-fog-architecture-70888/community-listfile.csv --output .local/research/forever-shadow-20261114/reproduced --disassemble shaders/vertex/dx_6_0/shadowmap.bls --disassemble shaders/pixel/dx_6_0/shadowmask.bls --disassemble shaders/compute/dx_6_0/lightbufferomnishadow.bls --dxc .local/tools/dxc-1.9.2609/bin/x64/dxc.exe
```

The manifests and `.ll` disassemblies are under `reproduced/`. Older ad-hoc
decoding is retained under `dxbc/`; use the reproduced results for line references.
What remains unknown: live caster-to-map bindings, per-frame pass order/counts,
cache update policy and whether the optional CSM/VSM/RT branches are active.
The copied binaries do not establish that Forever has no separate actor work.
There is no evidence here supporting a quality-preserving FPS gain from simply
deleting Northlight's detailed actor map.
