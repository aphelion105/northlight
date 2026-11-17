# CPU/GPU ownership, 17 November 2026

This audit starts at `8db308a6`, after the
[CPU capture changes](cpu-capture-stages-20261117.md). The gameplay logs in
`.local/out/gpu-ownership-20261117/` are frozen with hashes in `baseline.json`.
They predate these builds and are evidence of workload/ownership, not a live
before/after performance comparison.

## What currently computes and stores the data

| Data/work | Current consumer | Decision |
|---|---|---|
| Native vertex skinning and rendered displacement | Vertex shaders | Already GPU work. Keep native current palettes, vertices and coverage. |
| Material normals, POM, lighting, AO, fog and history | Pixel shaders and DEFAULT targets | Already GPU work. No CPU pixel mirror is introduced here. |
| Immutable world/static/native geometry | DEFAULT vertex/index buffers in the main path | Already device resources; world upload payload is discarded after commit. Existing MANAGED recovery paths remain. |
| Height-field pixels used by GPU displacement | GPU texture; previously also a CPU float vector and MANAGED backing | Move the texture to DEFAULT and release the unused CPU float vector after successful upload. |
| Skinned bounds and material distance admission | CPU draw admission | GPU bounds would need a result back before the current draw decision. Retain conservative CPU envelopes/certificates rather than introduce a per-draw GPU wait. |
| Subdivision and immutable vertex interpolation | CPU cold geometry build | Still CPU work. This patch removes its repeated full height-field scan on GPU-owned maps; it does not move the tessellator. |
| GI geometry, BVH, material texels and probes | CPU ray tracer, relighting/cache work and weather queries | Still required CPU input. Moving only the pool flag would not supply a GPU solver. |
| Shared capture snapshots | CPU bounds, animation/worker preparation and replay fallback | Keep immutable CPU ownership while those consumers can read it. GPU-native replay is already shared where source identity proves reuse. |
| MANAGED probe atlases | GPU lighting with retained Reset/recovery state | Keep the existing recovery contract. Changing it requires a replacement upload and low-memory Reset path. |

The last complete frozen worker inventory is about 109.817 MiB reachable CPU
capacity, including 56.253 MiB texels, 16.622 MiB vertices, 8.822 MiB triangles
and 9.650 MiB acceleration. These inputs are actually read by the CPU solver.
The independent source snapshot ledger was about 121.445 MiB. Ownership scopes
share generations and must not be added. Neither ledger is resident-VRAM data.
See the earlier [memory inventory](effects-and-capture-cost-20261117.md).

## Implemented GPU ownership of displacement heights

A height map now uses a one-level R32F DEFAULT render-target texture. A temporary
SYSTEMMEM texture supplies its exact float bytes through `UpdateTexture`; both
temporary upload and readback surfaces are released in the calling scope.
The render-target usage allows exact recovery through `GetRenderTargetData`
if a CPU-only consumer later appears. The field is otherwise immutable and is
sampled by the same vertex shader, with the same values and filtering.

This follows the D3D9 contracts for
[resource placement and Reset](https://learn.microsoft.com/en-us/windows/win32/direct3d9/managing-resources),
[SYSTEMMEM-to-DEFAULT upload](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-updatetexture)
and [render-target readback](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-getrendertargetdata).
DEFAULT is a device-memory allocation policy, not a measurement of physical
local VRAM residency. The MANAGED system-memory backup is no longer requested.

Only successful creation, complete row copy, Unlock and UpdateTexture authorize
CPU-vector release. The exact maximum absolute height deviation stays in a
scalar. New skinned meshes use that scalar for their existing anatomical relief
limit; no repeated scan of all height pixels is needed. Geometry, normals,
weights, topology and the outward pose bounds are unchanged.

A CPU-first texture keeps its pixels. The first CPU request for a GPU-only map
reads its own R32F field back, validates the rows/peak and publishes the CPU
vector only after success. It then retains that vector to avoid repeated
readbacks for mixed consumers. Regenerating from albedo would be unsafe after
source-cache eviction, different mip selection or an untracked source rewrite.
Failed readback leaves the valid GPU map intact. The existing two height reads
per frame remain the admission cap; a refused CPU path retains native rendering.
The first CPU fallback can wait for the GPU, so it is counted and remains inside
the existing HeightRead CPU profile. It is not a recurring GPU-only draw cost.

Reset drops the new DEFAULT resources through the existing displacement reset
owner, before backend Reset. Existing geometry byte charging still includes
width*height*4 for GPU height maps, even after their CPU vector becomes empty.
No cache or quality budget is increased.

## Measured scope and limits

The production height decoder/cache caps fields at 128 by 128, and displacement
retains 32 cached fields. The controlled full-cache case releases **2 MiB of
CPU float capacity**, from 2,097,152 to zero bytes. It retains the same 2 MiB
logical GPU payload and zero upload staging bytes. Geometry entries can also
hold older height texture references; those resources no longer request a
MANAGED backup either. Driver padding/backing, heap reservation and actual
game-process RSS/VRAM reduction are not measured by this capacity result.

The initial seven alternating CPU pairs measured 128 cold skinned mesh builds
sharing an already uploaded 128x128 height field: 4.886 ms with retained pixels
versus 1.335 ms with the GPU-owned summary, about 3.66x for that component.
One-time height generation/upload and rare CPU fallback are excluded. Height
generation itself is still on the CPU when a source/version first appears.
This is not a full capture or game-FPS multiplier.

`test_height_gpu_ownership.py` checks O2/O0, fifteen upload/readback failure
points, exact recovery after source bytes change, read-budget refusal, CPU-first
and mixed consumers, byte-identical skinned geometry and Reset cleanup.
The real-device oracle compares 65,536 R32F samples per backend on DXVK and native
D3D9, with two actual device Resets and exact CPU fallback. Its tiny copy shader
uses the explicit compatible fixture compiler; production shaders are unchanged.

`DISPLACEMENT_HEIGHT_MEMORY` now reports retained CPU capacity, cached GPU logical
bytes, GPU-only/CPU-required fields and successful/failed readbacks. The next game
log must establish the actual field population, mixed-consumer frequency and
capture-inclusive timings. Other CPU/GPU moves need their own consumer/Reset
replacement and measured benefit; the ownership table is not a claim that they
have already been implemented.
