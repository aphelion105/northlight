# Material CPU capture, 14 November 2026

The run ending at 23:31 uses the empty-HDR-sky build. Its copied log and
analysis are in `.local/out/material-cpu-20261114/`. The HDR foreground counter
stays zero while empty-sky skips grow: the preceding optimization is active.

## Where capture time goes

There are 36 sampled frames with active material work. Median CPU capture is
2.141 ms; material scopes account for 1.366 ms. The outer capture also includes
world/model-normal work, so the entire 1.6–2 ms previously discussed was not
exclusively the material subsystem. Settings and scene/population vary; these
medians are a prioritization aid, not a controlled optimization comparison.

Selected exclusive material costs, milliseconds:

| Work | Grass | Models |
|---|---:|---:|
| Geometry snapshots | 0.188 | 0.083 |
| Bounds preparation, excluding nested pose evaluation | 0.085 | 0.082 |
| Pose envelopes | — | 0.175 |
| Capture setup | 0.072 | 0.069 |
| Capture restore | 0.038 | 0.003 |
| Deferred packet recording | — | 0.089 |
| Deferred flush | — | 0.163 |

`MATERIAL_STAGE cpuMs` and `MATERIAL_CPU exclusiveMs` already subtract children.
Their separate stages can be added; `inclusiveMs` must not be added to its
children. Median values from different samples need not sum to the median total.
Normal-map construction is warm. Existing pose metadata, admission-source
handoff, packet pools and replay-state reuse remain enabled.

Sampled cleanup includes synchronous diagnostic formatting/writes. The logger
reports several milliseconds per interval, including frames with effects off;
do not treat the roughly 1 ms cleanup samples as ordinary per-frame render work.
No logger setting was disabled to manufacture a performance result.

## Changes

Rigid range admission now uses the existing tracked position-bound cache in
Normal/POM modes too. Previously this fast path was restricted to displacement.
It needs neither index decoding nor a full immutable mesh snapshot. The declared
vertex interval contains every legal indexed vertex, so its bounds can only be
more conservative. Current view constants still transform the box on each draw.
Unknown revisions, unsupported layouts, UP draws, nonfinite positions and
budget failures retain the original path. Skinned pose evaluation is unchanged.
Actual replay consumers still obtain the complete immutable geometry they need.

Grass material captures now join the existing deferred packet path when MSAA,
shader independence and the texture-write guard permit it. The packet holds the
diffuse texture and sampler used for alpha coverage. Texture writes flush before
mutation. Vertex constants, geometry, alpha blending, cutouts, raster state and
draw ordering remain packet inputs. Shared POM, prepared displacement and
unsupported cases retain immediate rendering. Native world depth is unchanged.
No shader, texture resolution, material distance or quality setting changed.

## Verification

`test_material_rigid_range_cpu.py` compiles the frozen `af4a7c1c` path and current
path. It checks 35 exact transformed bounds across writes, reset, unknown
revisions and pressure, then alternates CPU measurements for 136 distinct
48-vertex draws. The first updated probe measured about 0.035–0.042 ms before
versus 0.026–0.029 ms after for resident sources, and 0.276–0.279 ms versus
0.038–0.057 ms for rewritten sources. This is a CPU component workload, not game
FPS. `test_position_bounds.py` separately covers conservative indexed subsets,
resource identity reuse, revision changes, failed reads and bounded storage.

`test_material_grass_batch_gpu.py` compares the frozen old implementation with
the current implementation on real DXVK 3 GPU targets, using stock SM2/SM3
DetailDoodad shaders and a grass texture copied from the configured own client.
It requires exact color/normal/occlusion bytes at MSAA 0/2/4, nonempty normals,
and fewer target/depth changes for eligible warm and pressure batches. Mutation,
texture-write barriers and mixed immediate/deferred order are included. Material
and model-normal handoff/state tests cover the surrounding consumers.

Forever remains open. GPU timings in this contention are not a clean FPS or
absolute GPU-saving claim. Final acceptance reports must match the final commit
and tree. The earlier trial reports are retained, including two fixture setup
corrections and a stale model-normal `record` call updated for its existing API.
