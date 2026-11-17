# Effect toggles, 2026-11-17

The client log ending at 17:14:54 identifies production source
`045663ed987363f91a0efc6ed7bfece2696669d5b0d887e1c37c1298015be2b2`.
Its frozen copy and experiments are under
`.local/out/toggle-effects-20261117/`. The game was not launched for the
component comparisons below.

## Gameplay evidence

3840x2160, MSAA 2, RTX 4090, DXVK 3.1.1. Setting changes are applied at frame
boundaries. Samples below are useful for attribution, not controlled FPS A/B:
the camera, crowd, streaming and memory pressure also change.

| State / renderer frame | Effects GPU | Fog march | Point shadows | Capture CPU |
|---|---:|---:|---:|---:|
| All on, 1397 | 9.222 ms | 3.253 ms | 2.322 ms | 7.729 ms |
| Godrays off, 1524 | 5.783 ms | 0.547 ms | 1.769 ms | — |
| Actor shadows off, 1651 | 4.439 ms | 0.555 ms | 0.509 ms | — |
| AutoBump on, other principal effects off, 2540 | 1.066 ms | — | 0 | 6.027 ms |
| AutoBump also off, 2667 | 0.541 ms | — | 0 | 0.202 ms |
| Local lights + AO + AutoBump, 3556 | 2.184 ms | — | — | 6.335 ms |
| AutoBump off, GI on, 3683 | 1.919 ms | — | — | 0.466 ms |
| Crowded/moving scene, all on, 4318 | 19.395 ms | — | 9.231 ms | 18.147 ms |

At 3556, two cold subdivision builds cost 1.934 ms and produce 23,904
triangles. At 4318, the point pass submits 8,884 replay draws and 2,854
static draws across 22 lights, processing 7.55 million triangles. Every replay
has valid bounds: unknown bounds do not explain this fan-out. Late virtual
address availability falls to about 258 MiB; streaming deferrals and snapshot
misses also affect those frames.

Capture `session-5776-594333390-0`, world frame 3174 / renderer frame 3823,
has an R32 actor map with 4.206% occupied texels. Its NLCREF1 files resolve
through `scripts/capture_files.py`. The lighting export contains only c0..115;
imported fog uses c116..153. The separate constants file has 79 float4 rows.
The native trace is capped and incomplete. These files do not establish a
complete, authentic whole-scene replay or an exact imported-fog replay.

## Accepted changes

`world_point_rendering.inl` groups compatible, bounded opaque packets once per
frame, then reuses that ordering for every light and face. Cutouts, alpha modes
and unknown bounds remain barriers. Existing exact palette admission, pose
checks, triangle ranges, face weights, refresh schedules and failure handling
remain authoritative. Palette spans are classified once before sorting.
`POINT_ORDER` reports ordering time and eligible packets in the next game log.

The ready source scan now also retains its first used bone. Instancing uploads
only the inclusive first/exclusive-end palette range and subtracts the exact
integer base in the vertex shader. The original byte indices, matrix values,
dot products and vertex buffers are unchanged. Absolute indices up to 74 no
longer force a one-instance fallback when their actual span fits. Bounds must
be complete; capture ranges and palette DEFs are still validated. The shader
uses 23 VS3 slots and two temporaries. `rebasedPackets` and `savedRegisters`
report actual use in gameplay.

For 100 actors whose source uses bone 74, the previous algorithm cannot batch.
Ten complete production point cubes fall from 1,304 draws to 40. Paired medians
(after two warmup blocks):

| Backend | CPU submission before / after | CPU gain | GPU timestamps before / after |
|---|---:|---:|---:|
| DXVK | 0.410 / 0.094 ms | 4.35x | 0.290 / 0.181 ms |
| Native D3D9 | 1.086 / 0.115 ms | 9.45x | 1.079 / 0.084 ms |

These are authored repeated-mesh populations. Real gains depend on compatible
geometry, alpha policy and used bone ranges. GPU timestamps can include CPU
submission gaps; they are not pure compute timings. New image cases cover
nonzero starts 1, 33, 50 and 74, both ends of the register bank, partial cold
construction and failure before/after applying c252, alongside previous faults.

The production point-cube oracle compares native-order **enabled** instancing
against grouped **enabled** instancing on an interleaved three-mesh population.
One six-face update falls from 130 to 48 draws, with identical triangle work.
GPU depth tolerance remains the existing 2e-6. Paired CPU submission of ten
lights changes from 0.393 to 0.188 ms on DXVK (2.09x),
and from 1.112 to 0.399 ms on native D3D9 (2.79x). This is a
component CPU gain, not a measured 2x reduction in the game's 9.231 ms point
bucket or in frame time. The ordinary instancing tests also cover unique
meshes, alpha barriers, sparse palettes, resource faults, recovery and reset.

`displacement_geometry.h` removes work whose output was overwritten by the
original pinned M2 corner bytes, normalizes proven uniform one-bone byte inputs
without repeating the generic merge, computes each barycentric weight once,
and avoids hashing interior samples of unique source faces. Shared edges and
duplicate faces retain their original deduplication. Original geometry,
subdivision factors, height samples, relief, limits and output ordering remain.

The paired oracle compares **every output byte and index** against the frozen
entry header, in O2 and O0. It covers native byte layouts, 1–4 influences,
centroids, duplicate/reversed faces, seams, signed zero, nonfinite inputs,
invalid weights and eight actual stock-client subsets. Real-subset cold builds
improve about 1.3–1.8x; synthetic M2 seam cases reach about 2.5x. WMO and generic
paths are measured too. The dense scratch fixture drops from 2,015,960 to
1,032,680 bytes. These cold-build gains do not represent total capture CPU.

## Rejected fog experiments and remaining target

Private prototypes tested fine/coarse traversal with jumps, nested traversal,
whole-ray rejection, a minimum pyramid, precomputed frame uniforms, density
culling, and a carried whole-ray traversal. The independent sorted-grid CPU
oracle caught near-parallel boundary stalls in the first jump prototype.
Corrected variants passed that oracle, but complete 48-interval noisy passes
showed no consistent multiplicative win; several regressed dense cases or
exceeded 32 SM3 temporaries. No such prototype or relaxed image tolerance is
installed. Existing production fog shaders remain the entry versions.

The requested multiplicative improvement of **all** effects is not established.
In particular the approximately 3.25 ms actor/Godrays fog pass still needs an
algorithm with a demonstrated full-pass gain. Point replay fan-out, general
material admission and late address-space pressure also remain targets.

## Validation and delivery

Current reports are saved beneath `.local/out/toggle-effects-20261117/` and
`.local/out/test-output/`. Run the strict source and architecture suites, the
point palette/terrain/state GPU contracts, and affected displacement/height
contracts. The explicit native-subset paired run is recorded in
`geometry/paired-final/validation.json`; final point measurements are in
`palette-range/first/validation.json`. Input hashes must match the final tree.

Build with `python scripts/build_renderer.py`, retaining shader freshness and
asset-package verification. `installed.json` records the delivered DLL/assets,
source identity, backups and acceptance reports. Confirm gameplay improvement
with the same route and staged settings; component checks cannot establish FPS.
