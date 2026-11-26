# Architecture and ownership

Northlight runs as a D3D9 proxy in the client process on x86 and x64. This map describes
the implemented boundaries. Historical investigations and release reports remain
in `renderer/docs/`; they do not replace the current contracts or prove current performance.

## Tideglass

The producer captures Tideglass inputs at the World snapshot and shares them
with the matching UI boundary. Direct mode reads those inputs on the device's
calling thread. On the supported AMD64 build, the adapter verifies the layout
before reading the camera's liquid type and depth, the transport list, and
the texture identities of native magma and slime. The device owner uses
the captured positions to update wake trails and supplies the frame data
to Tideglass. Both architectures share the water distance and coverage mask
and lend the resolved depth to Tideglass.
Liquid shading runs on the legacy scene before the effects copy. The core
resolves multisampled colour into its snapshot textures and keeps the game's
multisampled targets for liquid shading. A conversion at the handoff expands
the compact distance mask into the distance and coverage channels the core
expects. The rain mask retains streaks from the native scene at the composite
boundary. The underwater pass runs before HDR decode, or on the
completed world target in SDR, including frames without a visible liquid draw.
Frame completion closes the Tideglass session, Reset releases
default-pool resources, and teardown destroys the context on the device owner.

[Device adapter](../src/proxy/device_tideglass.inl) ·
[Snapshot ownership](../src/stream/stream_device.h) ·
[Frame effects](../src/proxy/device_frame_effects.inl)

## Shader and resource navigation

The pipeline map follows M2 sections and ADT terrain through material capture,
model normals, scene depth, world lighting, temporal history and HDR output.
It also traces M2, WMO and ADT caster admission through static and live
directional depth, actor maps and point shadow cubes. The atmosphere routes
cover fog history and volume integration, water distance, native rain and
menu weather, sky composition, particles and the HDR UI boundary.
[Pipeline registry](../scripts/shader_pipeline_map.json)

```powershell
python scripts/audit_shaders.py
python scripts/audit_shaders.py --query WorldCompositeHdr
python scripts/audit_shaders.py --query 'M2 section'
python scripts/audit_shaders.py --query ADT
python scripts/audit_shaders.py --query WMO
python scripts/audit_shaders.py --query ShadowUnion
python scripts/audit_shaders.py --query LocalLightingPcss --replay <validated-shadow-audit-replay>
python scripts/audit_shaders.py --query FogBlur
python scripts/audit_shaders.py --query Water
python scripts/audit_shaders.py --query Weather
python scripts/audit_shaders.py --query Sky
python scripts/audit_shaders.py --query 'fog-only'
python scripts/audit_shaders.py --query '2x MSAA'
```

The audit writes `pipeline.json`, `pipeline.md` and `pipeline.mmd` under its
output directory. `--query` also writes `query.json`. Entry queries start from
that shader's ASM sampler table.
Object queries follow downstream consumers and include admission and fallback.
Mode queries for `AO-only`, `fog-only`, `Menu` and `2x MSAA` show selected
producer routes and their conditions.
The report links C++ selectors, HLSL definitions, manifests, include hashes,
registers, resource lifetimes and test entry points. Each use of a shared
entry has its own bindings and inputs. For example, `ShadowUnion` can read
directional scratch or point face scratch on s0, depending on the pass.

`--replay` adds exported products for the renderer source and payload
identified by the replay. The audit checks the trace join and hashes the
near/far depth, current local light, committed point cube faces, fog and
receiver distance at the selected Presents. The report records missing
observations for products outside the export. The binding and fallback routes
in the map come from C++ and ASM.

`world_effects.hlsl` includes eleven pass modules in declaration order.
Geometry reconstructs view distance and normals. Shadow sampling, directional
and actor lighting, and local lighting each own their helpers. Temporal
history, fog and atmosphere, fog filtering, composition, shadow-caster entry
points and fog entry points live in separate files. The transition and
volume compilers use the same world sources.

`NorthlightDrawCapture::capture` owns the first handoff. The `Device` adapter
provides the render-state predicate and normal and water calls at the draw
boundary. Active buffered indexed
terrain publishes bounds before material work. The other draws visit material,
model normals and world capture in that order, followed by water. `CaptureSource`
retains its declaration for the duration of the draw. A mesh reuse requires the
current declaration, draw range and source version. Normals accept the material
bank only when the requested registers are covered by a matching device write
generation. The range proof borrows rows directly from a same-draw material
bank under the mirror gate. It requires coverage through c33 for rigid programs
or c255 for one-bone programs, and falls back to stamped mirror reads when
coverage or the generation check fails. Queued packets keep their own storage.
For buffered indexed draws, the normal recorder can recover an existing mesh
from the mirror's current shader, declaration and buffer bindings. Tracked
vertex and index revisions must match the stored draw's range. Unknown bindings
and source misses use the ordinary snapshot reader.
On x86 and x64 DXVK with CommandStream, a completed frame with at least 4,096 normal
packets enables the mirrored lookup and raises the material cache allowance to
64 MiB within the shared 96 MiB scene limit. The renderer returns to the regular
path below 3,072 packets. Memory pressure restores the 32 MiB material limit.
Buffered model capture for shadows also borrows its four projection rows from
the material bank under the held mirror gate. The projection check consumes
the rows immediately and falls back to the device getter when the loan is
absent, incomplete or stale.
On a diagnostic frame the verified 12340 command producer carries the original
M2 section's resolved draw range and a digest from independently checked palette
rows. The model-normal recorder compares the range and opaque raster state with
the active draw, then checks the digest against the constants of an accepted
packet. It counts packets with a retained, versioned geometry source separately.
The section identifiers are diagnostic values. The renderer continues to hold
geometry and palette data through the existing packet owners.
[M2 producer](../src/stream/m2_draw_12340.h) · [Normal consumer](../src/proxy/model_normal_renderer.h)
UP draws retain the material guard and the original caller memory contract.
[Draw handoff](../src/proxy/draw_capture_route.h) · [Geometry alias test](../tests/test_capture_handoff.py) · [Normal consumer test](../tests/test_model_normal_consumers.py)

Native overlay coverage and additive emission write auxiliary targets alongside
the game draw. On x86 and x64 DXVK with CommandStream, the device mirror can
retain a coverage attachment after restoring the logical slot to empty. The
next compatible draw reuses that binding. Other draws, target and depth
changes, clears, state blocks and raw access materialize outstanding detaches.
[Coverage planes](../src/proxy/effect_coverage.h) · [Device state](../src/proxy/device_mirror.h)

AutoBump submits adjacent opaque material packets together when their resident
geometry and material state match. The shared palette shader proof keeps each
instance's captured bones and the original packet order. A failed instancing
bind restores the ordinary vertex state before scalar replay resumes.
[Material replay](../src/water/material_palette_replay.inl)

Replay `--capture-work-frame` audits one certified renderer frame. The
draw log joins material, model-normal and world capture work by device,
Reset epoch, frame and renderer draw ordinal. It records the native
draw range, the published geometry fingerprint and per-consumer capture
counters. The snapshot byte count is budget charged, which can include
cache hits. The normal bank counters record float4 requests and handoffs
separately for the range proof and the queued packet. Capture scopes around
the three consumers record VB/IB reads routed through `ReplayCopies::Reader`.
Each event identifies the buffer within the run, the byte range and the path
used by the reader. Direct locks and whole-buffer fills are D3D9 lock attempts.
A successful lock reports its span. A copy view reports the requested span
served by replay-owned CPU memory, including after a fill.
The final frame row proves both export counts. The replay writes
`capture-work.json` and excludes this diagnostic from timing.
[Audit parser](../scripts/northrend_capture_work.py)

The command stream owns VB/IB CPU copies at the replay position. A regular
buffer larger than 64 KiB earns a whole-buffer copy for partial reads after
requests in two recent frames and cumulative requested spans of at least
one quarter of its length. A quiet gap or eviction clears that demand.
Small buffers and full regular-buffer requests qualify immediately. The
separate large-buffer allowance keeps its existing backoff and pressure
checks. [Replay copies](../src/proxy/replay_copies.h)

On x64 DXVK, the producer captures the verified client's camera, view stack,
sky lighting and native shadow state at the existing draw triggers. The replay
owner reads those fields from the matching snapshot, which expires at Present
or Reset. Missing reads use the renderer's ordinary unavailable-input paths.
[Snapshot capture](../src/stream/game_snapshot.h)

World shadow replay packets hold a reference to their vertex float bank.
Packets share the bank after a matching epoch or a certified block comparison.
The prepared worker and held replay packets keep the same bank alive until
their last reference drops. A self-check correction makes a private copy before
writing. Boolean and integer rows stay in each packet. The idle packet pool
charges retained float storage against its byte limit and releases it under
pressure. [Constant capture](../src/replay/replay_capture_constants.h) ·
[Replay owner](../src/world/world_renderer.h)

Image parity can use a fixed 60 Hz visual clock tied to the renderer Present
ordinal. The device advances it once per frame. Local light and probe fades,
cloud drift, atmosphere transitions, the directional cascade anchor and
transitions, and HDR adaptation consume it when the parity runner enables it.
Job scheduling, resource
retries and profiling use the process clocks. Parity exports the ordered
local-light inputs and clock evidence for each selected Present.
[Parity clock](../src/core/frame_parity_clock.h) · [Parity runner](northrend-baseline.md#compare-resolved-color-and-depth-at-2-msaa)

`NorthlightContactAO::render` submits contact AO, optional actor preparation
and bilateral blur between scene copy and composition. The Device owns the
saved state, target and shader lifetime, and folded-versus-legacy composition.
Actor preparation borrows the denoised destination until the raw AO draw
unbinds it. Blur reads the raw AO in s0 and s2, then restores scene color in s0.
Folded HDR can use the world's neutral AO texture when contact AO is disabled.
The legacy fallback materializes that neutral clear before sampling the target.
The screen fallback owner supplies the solid AO, white and zero textures, plus
the signed blue-noise tile used by the SDR composite. The shadow reset releases
white with the shadow maps. The screen reset releases the remaining textures.
Missing zero or dither textures use the existing composite fallback.
[Screen fallback resources](../src/world/world_screen_fallbacks.h)
[AO submission](../src/proxy/contact_ao_pass.h) · [AO variants and resources](../scripts/shader_pipeline_map.json)

`NorthlightSceneDepth::copy` owns the RESZ command sequence and its state
restoration for both the scene and rain depth snapshots. The Device unbinds the
rain MRT before the copy and keeps the per-frame `captured` and early-resolve
latches. Scene-copy failures can disable effects. Rain-copy failures discard
only the rain snapshot. Native D3D9 uses a write-disabled point draw before
the trigger. With the x64 native HDR backend, 1× scenes use an INTZ depth
attachment. The same snapshot boundaries sample it into R32F, with the
depth attachment unbound for the copy and restored before the game resumes.
[Depth copy](../src/proxy/scene_depth_resolve.h) · [State and failure checks](../tests/test_saved_state.cpp)

The proxy resolves the private or game root before loading its backend and
external shader bundle. Its logger owns rotation, synchronization, batching
and timing. The Device keeps COM entry points, scene boundaries and Reset.
Draw preparation shares one lazy set of render-state reads across receivers,
early depth resolve and census. Draw submission coordinates capture and native
submission. Frame effects schedule the depth, AO and composition passes and
use the Device's shared effect targets.
[Backend startup](../src/proxy/renderer_backend_windows.h) · [Logger](../src/proxy/renderer_log.h) · [Draw preparation](../src/proxy/device_draw_preparation.inl) · [Draw submission](../src/proxy/device_draw_submission.inl) · [Frame effects](../src/proxy/device_frame_effects.inl)

The device mirror tracks blend states that rule out particle light capture.
For draws that clear this check, the capture router classifies the bound
shaders once per known binding and sends candidates to the draw-local proof.
An invalid mirror, an in-flight state change or an unknown shader binding
allows the original D3D9 state reads. The proof still checks the current draw,
including fixed-function geometry and emissive M2 passes.
When ordinary probes reach the frame budget, a bound texture with a decoded
mip can rule out another probe if none of the sampled textures can contribute
to a world or sky flash. The route falls back to capture for unknown textures
and gives possible flashes their separate budget.
[Mirrored state](../src/proxy/particle_draw_state.h) · [Particle capture](../src/proxy/particle_light_capture.h)

The world publication state owns the worker request IDs, wake-up, cancellation
and published snapshot. The geometry builder prepares immutable BVH and page
data. The GI worker adopts the geometry result, can publish it before solving
probes, and assigns a probe serial to each accepted atlas. The render thread
admits and stages missed GPU pages and material textures under one budget. It
swaps the drawable page table only after staging and commit preparation are
complete. The previous drawable mesh stays available while a replacement is
deferred. The probe atlas has a separate upload and readiness check.
[Publication state](../src/world/world_publication.h) · [Worker pipeline](../src/world/world_build_pipeline.inl) · [Region build](../src/world/world_region_build.inl) · [Mesh staging and commit](../src/world/world_mesh_publication.inl)

`NorthlightWorldActorShadows` owns the per-source depth and tile targets,
shared reduction scratch, visibility target and optional shaders. Geometry
prewarm stages targets through the shared budget. Pressure trim releases the
textures, and a change to actor settings also releases the shaders. The
renderer schedules depth replay with the near cascade, tile generation before
lighting, and the separate actor mask when the shared resolve is unavailable.
The near cascade prepares the projection once and selects each axis's two
extremal corners against the same inset and depth planes. It hands off its
ordered omissions to the actor depth pass. The actor pass publishes the map and
fog coverage after submission succeeds. Use `--legacy-actor-inset` and
`--no-legacy-actor-inset` on the same replay to capture the actor depth map for
both projection paths.
[Actor shadow resources](../src/world/world_actor_shadows.h) · [Actor depth pass](../src/world/world_directional_actor_draw.inl)

The actor map keeps short histories for casters near its inset and for
recently valid bounds. Primitive and snapshot vertex counts determine the
bounded probe windows. Owner identity still controls which caster can reuse
an entry. Replaying the same draw at a different heap address leaves its
probe windows unchanged. [Actor shadow history](../src/shadows/actor_side_hold.h)

`NorthlightWorldDirectionalTargets` owns the four final maps, four static
caches, two depth stencils, shared dynamic scratch and optional near
environment maps. Staged allocation retains completed targets under the
geometry budget. The owner assembles static and dynamic depth and holds the
optional readback used to verify static cache updates. `renderDirectional`
prepares static and live terrain, schedules cascade and actor replays, and
publishes the maps. `NorthlightDirectionalShadow::GpuPass` borrows the depth
buffer, binds and clears the cascade and actor targets, batches terrain draws
and restores the near caster target after assembly of the environment map.
Its cache pass also binds the oversized static target and shared depth, clears
dirty rectangles before enabling scissor, and disables scissor at scope exit.
The coordinator supplies ordered meshes and publishes a cache key after the
draws complete.
`WorldRenderer::render` starts the pass after geometry
upload and atmosphere submission. It consumes the result for point-shadow
budgeting and profiling. The pass joins its terrain, culling and static
workers on early return, and resets unfinished directional transitions.
[Directional resources](../src/world/world_directional_targets.h) · [Directional raster pass](../src/shadows/directional_gpu_pass.h)

`NorthlightWorldShadowPrograms` owns the caster, replay, cache and union
shaders and both world-mesh declarations. Directional, point and flash
passes borrow those programs. Device warmup can create the union shader and
world declaration ahead of screen initialization, which reuses both handles.
The owner selects the cache shader and binds the declaration, vertex stream
and index buffer in that order. Reset releases the programs. A failed
optional cache shader leaves the ordinary depth shader available.
[Shared shadow programs](../src/world/world_shadow_programs.h)

`NorthlightShadowReplay::Pass` tracks the vertex streams, indices, material
and projected constant banks for animated shadow draws. Cascades, actor maps,
flash shadows, point cubes and the replay probe use the same setters and
draw dispatch. The point cube can retain known geometry across faces and
rebinds its material for each face. Palette draws invalidate both banks.
A refused packed index bind retries with the packet's original index buffer.
The callers still choose caster lists, grouping, and publication.
[Replay submission](../src/shadows/shadow_replay_submit.h) · [Recording device test](../tests/test_shadow_replay_submit.py)

The diagnostic replay probe owns its private R32F target, D24S8 depth and
saved device state. The directional pass supplies the recorded packets and
probe mode after sun-near replay and before the shadow union. Failed
allocation or setup ends that probe run. Reset releases its private targets.
[Replay probe pass](../src/world/world_replay_probe_pass.h)

At a recorded World checkpoint with fixed static streaming, the replay stand
can finish pending mesh pages and material textures under one 32 MiB upload
budget. The sealed payload bounds the number of texture creations. A page
upload that cannot fit another complete vertex yields with its cursor intact.
The stand still requires complete publication and resource admission on the
recorded Present. `REPLAY_GEOMETRY_STAGING` records work and remaining cursors.
[Mesh publication](../src/world/world_mesh_publication.inl)

`NorthlightWorldMeshPool` owns the page table and VB/IB resources in the
device scene. It samples current address-space headroom before a driver
allocation, grows only a page claimed by staging, and owns buffer locking,
conversion to the compact caster format, and preload. The coordinator
publishes the complete table after upload. A failed allocation drops partial
COM results and leaves active pages available. On Reset the coordinator
cancels staging, trims idle pages and chooses whether to replay a lost
DEFAULT generation. [Mesh page pool](../src/world/world_mesh_pool.h)

`NorthlightWorldMeshPublication::MaterialUpload` owns an unfinished cutout
texture, its staging reference and the next row. It advances the cursor
after a complete row copy and unlock, releases both references on
cancellation, and transfers the texture after the final upload. The mesh coordinator
accounts for bytes, reuses complete textures and publishes the material
table with the geometry. [Material upload owner](../src/world/world_mesh_material_upload.h)

`NorthlightWorldProbeAtlas` owns the managed GI textures and their upload
mirrors. The geometry coordinator grants an allocation budget and publishes a
serial only after all required uploads succeed. Reset retains compatible
managed textures. The owner validates the dimensions, format and pool before
reusing a retained texture, and uploads again when history is reset. OOM defers
admission. A device that refuses the optional previous coefficients can still
use current GI. [GI atlas owner](../src/world/world_probe_atlas.h)

The local visibility mirror keeps immutable near and coarse probe references
in GPU slot order. Radiance-only updates skip the visibility lock and mixed
updates touch only the affected tiles. [Probe visibility upload](../src/gi/probe_texture_upload.h)

The two coefficient mirrors retain the current RGB and previous blend
coefficients by slot. Publication reuses their GPU textures when the packed
coefficients match. The moment and activation uploads still publish their own
state. A failed texture upload leaves its mirror dirty for retry. A recreated
texture or a mirror reset requires a fresh upload. [Coefficient mirror](../src/gi/probe_blend.h)

The CPU atmosphere pass prepares the selected light list, fog bounds and
spell transmittance from the frame's frozen world inputs. The coordinator joins
the worker before publishing shader constants. SpellFog selects the imported
or regional integration once per ray and computes profiles only when the
current frame has a spell consumer. Lamp extinction and the sky veil use the
same prepared medium.
[CPU atmosphere pass](../src/world/world_atmosphere_frame.inl)

The replay buffer owner holds four vertex rings and one index ring. Their
capacity drives admission and their spans borrow the frame's shared EVENT
fence. After residency admission, a fully resident frame skips bulk layout
and binding. A mixed frame lays out nonresident packets in draw order and
passes that list to the owner for copies and binding publication. Publication
starts after every copy succeeds. Pressure and admission retries rebuild the
list from the packets they will actually draw.
[Replay upload](../src/world/world_replay_upload.inl) · [Buffer owner](../src/world/world_replay_buffers.h)

The static-cache cost estimate and redraw share the ordered batch indices
prepared at mesh publication. The estimate runs on the render thread or its
plan worker while the committed mesh and fixed terrain set are immutable. The
redraw clips those batches against the cascade and dirty rects in draw order.
If the current generation has no index, both paths apply the original filter
to the committed batches.
[Batch selection](../src/shadows/directional_cache_mesh.h) · [Redraw planning](../src/world/world_directional_cache_plan.inl) · [Cache submission](../src/world/world_directional_cache_draw.inl)

After the normals pass, the coordinator builds actor tiles and chooses the
shared resolve or the separate actor mask. `NorthlightWorldDirectionalVisibility`
owns the half-resolution target, paired filter and lighting shaders, allocation
retry, and the visibility draws for each source and shadow epoch. It binds the
shared actor inputs and restores the samplers borrowed by transitions. If the shared
draw fails, the coordinator rebuilds the separate actor mask before lighting.
Lighting selects its shader from the resolve result, and fog can reuse the tiles.
[Visibility owner](../src/world/world_directional_visibility.h) · [Pass order](../src/world/world_soft_shadows.inl)

`NorthlightWorldShadowTransition` owns the previous static crop, current
dynamic targets, and the shader variants for both lighting epochs and fog.
The coordinator chooses when to preserve a cache and supplies its crop
offset, source matrices, and memory admission. The owner records the crop
only after draw and state restoration succeed. Reset and pressure release
the optional targets. Allocation failure cancels active fades and leaves
the ordinary cascade schedule available.
[Transition owner](../src/world/world_shadow_transition.h) · [GPU crop and Reset test](../tests/test_shadow_history_state_gpu.py)

`NorthlightWorldNormalsPass` owns the half-resolution normal target and
shader variants. The coordinator passes the pending distance surface from
lighting history when removal smoothing needs it. After visibility resolve,
`NorthlightWorldScreenLighting` writes raw directional light and the baseline,
adds GI, and fills the optional prelit daylight target. It owns the targets and
the directional and GI shader variants. It binds the shared frame constants,
textures and sampler states before the screen passes. It restores the probe
and fog bindings before temporal resolve and leaves the baseline bound for
subsequent readers.
[Screen-space normals](../src/world/world_normals_pass.h) · [Screen-space lighting](../src/world/world_screen_lighting.h)

`NorthlightWorldLightHistory` allocates and publishes paired half-resolution
lighting and distance images. `WorldNormals` writes distance on RT1 when
directional removal needs it early. Otherwise `TemporalLight` writes it on
RT1 in the lighting path, or `WorldDistance` writes it on RT0 in the fog-only
path. A successful resolve advances the pair together. Reprojection requires a
compatible lighting mode, map and camera. Fog reads the previous distance and
can borrow the old light image after temporal resolve finishes. The raw
lighting target becomes the local-light target.

`NorthlightWorldLocalLighting` owns the lamp shader variants and the
profiling split target. It draws direct lamp batches into the raw light
target after temporal resolve. After atmospheric history and blur, it
borrows the previous light slot as an accumulator and caps the summed
glow once. Both passes receive the frame's light selection and precomputed
screen rectangles. The lamp owner applies the scissor and records sampled
viewport and rectangle state at submission. Its scissor state also accounts
for skipped and clipped point corrections. The coordinator schedules point and flash draws. The local
surface pass binds the recycled target and restores the shared world constants
and texture bindings before fog.
[Lamp pass owner](../src/world/world_local_lighting.h) · [Frame scheduling](../src/world/world_local_lighting.inl)

The lamp, flash, actor visibility, directional visibility and shadow merge
passes submit their screen draws directly. Each pass sets the viewport at
submission using the shared D3D9 screen triangle. The coordinator supplies
the frame's selected resources and retains pass order.
[Screen draw](../src/core/screen_quad.h)

`NorthlightWorldPointLighting` owns the hard and PCSS shaders and the
screen-space correction for each published shadow cube. The frame prepares
the per-light constants and supplies the view and capture observer. The pass
binds the cube and screen inputs, clips the projected light sphere, and disables
scissor after the draw. Successive corrections may share their screen-input
bindings. A failed PCSS allocation is held until Reset and uses hard shadows.
[Point correction](../src/world/world_point_lighting.h)

`NorthlightPointShadow::MixedFaceCache` owns up to 32 R32F images for the
proven rigid opaque contribution of mixed cube faces. Its proof compares
source geometry, readable pose rows, projection and the static base.
`FacePass` copies a hit and blends the remaining draw into the cube face
on the static-base route that already uses MIN blending.
Allocation or bake failure leaves the finished cube face available.
A refused copy refreshes the face with all current casters. Reset and
memory trimming release the cached images.
[Mixed point faces](../src/shadows/point_mixed_face_cache.h) · [GPU checks](../tests/test_point_palette_gpu.py)
[Lighting history owner](../src/world/world_light_history.h) · [GPU temporal oracle](../tests/test_world_temporal_merge_gpu.py)

`NorthlightWorldFlashShadows` owns both cascades for each active flash, the
correction shader, and retries after partial allocation. It borrows the
directional depth surface and caster programs. Its cascade commands bind
world materials and mesh pages, submit live terrain, and bind the pixel
shader for actor replay. `WorldRenderer` selects caster ranges and requests
current-pose bounds after resources are ready.
Local lighting consumes a pair only after both cascades finish. Geometry
trim and Reset release the owned resources.
[Flash pass owner](../src/world/world_flash_shadows.h) · [Flash GPU oracle](../tests/test_flash_shadow_gpu.py)

`NorthlightWorldFogField` uploads the published regional field to a managed
density texture and derives a second texture of height bounds. The atmospheric
pass receives density on s13 and height bounds on s12 in that order. If height
bounds are unavailable, the raymarch uses the density texture alone. Required
texture memory failures release both textures and defer retry for one second.
Reset releases the textures and the uploaded field reference. The coordinator
chooses the publication and invalidates moon-floor inputs when it changes.
[Field owner](../src/world/world_fog_field.h) · [Recording device test](../tests/test_world_fog_field.py)

`NorthlightFogScreen` owns the raw, scratch and history images at half
resolution. Lighting borrows scratch for removal smoothing before fog.
Directional shadows admit that shader and scratch image. Fog can create
the same scratch image independently. Both reuse the allocation until
the screen resources reset. A fresh lighting-only frame uses raw light
for temporal resolve and can run without either smoothing resource.
The screen pass rotates the history texture and its surface together after
a successful resolve, then filters into the current fog image.
`renderAtmosphericFog` admits both sources before writing the volume atlas,
reuses an eligible settled atlas, or raymarches directly when admission fails.
`NorthlightAtmosphereCommands` submits the source bindings and cloud draw and
restores the borrowed lighting state after the base medium and clouds. The
coordinator resolves screen history where needed, adds lamp fog after blur,
and returns the current image and history flags to composition and diagnostics.
[Screen fog owner](../src/world/world_fog_screen.h) · [Atmospheric commands](../src/world/world_atmosphere_commands.h) · [Atmospheric scheduling](../src/world/world_atmospheric_fog.inl)

`NorthlightRegionalFogGpu` owns the base, actor and ambient fog and cloud
shader variants and the managed cloud-noise volume. Optional actor shaders
publish as a pair. Reset releases the shaders and texture and clears optional
shader refusals. The noise backoff and format refusal remain in effect across
screen resource resets. The coordinator still selects the regional field and
checks available address space before admitting the noise upload.
[Regional fog GPU resources](../src/world/world_regional_fog_gpu.h)

The frame shader bank prepares the shared pixel constants, including the
previous view, measured fog hold and horizon haze. The render coordinator
finalizes cloud availability before starting spell-fog work, publishes
c0–c67 once alongside the base texture bindings, and passes the same rows
to the lighting and fog stages for their partial restores.
[Frame shader bank](../src/world/world_frame_constants.inl)

Reset, resize and screen allocation rollback enter through
`WorldRenderer::releaseGPU`. The coordinator drains workers and orders
release. Screen lighting, normals, local lighting, fog and shadow modules
release their own GPU objects. Geometry pressure uses the narrower target
release path and preserves the active screen images. Full device reset
also releases the shadow programs and clears history.
[Reset coordinator](../src/world/world_renderer.h) · [Geometry targets](../src/world/world_geometry_resources.inl)

The scene targets, screen passes and composite scratch allocate render
textures through `NorthlightWorldRenderTarget`. A texture and its level-zero
surface publish together. A failed call releases both temporary references.
Prewarm and memory admission still govern each request before creation.
[Render target allocation](../src/world/world_render_target.h)

The shadow module sets the initial MRT and depth state. The compositor
sets the shared screen quad state. Screen lighting sets additive blending,
admits the optional prelit target and binds the receiver constants.
Lighting history submits its smoothing, temporal and distance quads.
Screen lighting binds the neutral baseline and AO input after distance-only
resolve. It binds the celestial maps and constants used by both lighting
and fog. The atmospheric pass binds the current source's actor maps within
that sequence. The
composite returns its borrowed regional-fog sampler after success or a
draw failure. An active caller snapshot covers game-state restoration
only for its own device. Standalone world and weather calls capture their
own state when the supplied snapshot has already restored.
[Saved state](../src/proxy/saved_state.h) · [Composite bindings](../src/world/world_composite.h)

`NorthlightWorldComposite` owns the composite shaders, the full-size color
scratch and the scene copy when the caller supplies no folded scene. It consumes
the frame stencil masks and restores the temporary sampler bindings used by
the world and material draws. It also clears the diagnostic target when
geometry or the shadow pass is unavailable. `WorldRenderer` supplies the resolved fog
and lighting textures and owns the enclosing device state. Changes to the
scratch allocation invalidate that owner's cached state blocks.
[World composite owner](../src/world/world_composite.h)

For native-versus-Northlight work ownership, current pass dependencies and
measured optimization priorities, see the [2026-11-14 pipeline audit](pipeline-audit-20261114.md).
For the next gameplay log, zero-weight lighting-filter optimization and separate
HDR stage timings, see the [2026-11-15 follow-up](pipeline-20261115.md).
For local-light shadow costs and exact reuse of unchanged cube preparation, see
the [2026-11-17 investigation](local-light-performance-20261117.md).

Directional world and character shadows share a visibility resolve. The fine
character map covers 24 yards at 1024² and the near world cascade covers 96
yards at 1024². Directional maps take priority in the screen's shadow refresh
budget. `ShadowRefreshCpuMs` measures elapsed CPU submission time for cascades
and lamp cubes together. Once spent, a stationary lamp can carry a complete
animated cube across one due refresh if its map, projection, static content,
mesh and live terrain still match. A cube older than its refresh period plus
33 ms must update. Required work may exceed the target. `SHADOW_REFRESH` logs
directional submission time and the number of cubes held on sampled frames.

The static directional cache batches consecutive index ranges within a mesh
page. The pixel shader, alpha coverage and scissor govern when a draw closes.
The dynamic directional terrain pass uses the same range checks for cached
chunks left out of the live terrain draw. It keeps the current material
binding across consecutive ranges with that material and flushes before the
live terrain submission.
Directional and character map replays collect casters after per-map selection.
The submission loop combines adjacent triangle-list ranges with identical
state and shares native alpha targets across compatible draws. Opaque
single-influence casters with the same geometry share the point-shadow palette
submission. A caster can cross an intervening draw only when their padded
raster footprints are disjoint. Alpha casters and unknown bounds keep their
original order. Projection and fog bounds remain per map.

`NorthlightShadowPalettePass` owns the shader, vertex declaration, instance
buffer and allocation retry shared by point, directional and actor shadows.
The map passes select compatible packets and account for their own work.
The palette pass binds the instance data, submits the draws and restores both
stream frequencies. Geometry-only release keeps the shader and declaration
and drops the instance buffer. Full device release drops all three resources.
[Shadow palette pass](../src/shadows/shadow_palette_pass.h)

`ReplayAlpha` owns the native-format scratch surfaces and their allocation
retry. A frame begins a new refusal scope. Reset clears the pool and retry
state, while a memory trim frees the surfaces and retains the current retry.
The frame coordinator withholds incomplete directional maps and invalidates
lighting and fog histories when fallback changes.
[Replay alpha owner](../src/shadows/shadow_alpha_replay.h)

Lamp cube faces use the same page-range and device primitive-limit checks.
`NorthlightWorldPointTargets` owns the active cubes, retained static faces,
shared R32F scratch, D24S8 depth, neutral texture and caster shader pair.
Warmup and frame rendering reuse the shader pair, including across Reset.
Full device release drops both shader handles. The owner reports the
attachments to the GPU memory inventory. `WorldRenderer` schedules updates, submits the selected casters and
publishes a complete cube. `NorthlightPointShadow::FacePass` borrows the targets
for a single submission and owns their binding order, clears, material state,
min blending and explicit depth union. An unfinished paired render target is
detached when the pass exits.
[Point target owner](../src/world/world_point_targets.h) · [Face pass](../src/shadows/point_shadow_face_pass.h)

The face pass submits the ordered static and cached-terrain ranges. It owns
the alpha-mask sampler changes and material bindings for those ranges.
For live terrain it binds the compact geometry, marks the index-ring slice
as used before the first draw, and restores the static declaration after
the final draw. The coordinator supplies the candidate lists and page
bindings, updates the counters and publishes the completed face.

`NorthlightLiveTerrainIndices` owns the dynamic INDEX32 buffer and the current
slice base. It writes the complete and directional index lists under one lock
and publishes the base after Unlock. Directional, flash and point passes use
that slice and touch the shared EVENT before drawing. Reset releases the ring
and buffer. Growth refusal leaves the cached terrain available.
[Terrain index owner](../src/world/live_terrain_indices.h)

The optional device-initialization warmup uses borrowed point targets and
shaders to submit the opaque, cutout and union pipelines. The warmup restores
the original attachments, viewport and all shader constant banks before its
one EVENT submission kick. The frame pass publishes later cube contents.
[Point pipeline warmup](../src/world/point_pipeline_warmup.h)

On a static face rebuild, a second R32F render target can receive the same
depth as the working cube. The cache detaches before dynamic casters draw.
The working face keeps its colour and resets its depth buffer. On devices
without a compatible second target, the existing cache copy and union paths
remain available. The workload record counts successful paired face writes.

For dynamic lamp shadows, `point_shadow_clip.h` classifies world-space caster
bounds against the six cube faces. Sparse planes use precomputed support
corners. Cases close to a clip boundary use the full floating-point margin.
`point_replay_casters.h` prepares the current frame's ordered caster bounds and
optional group bounds after the pose worker joins. Each lamp reuses those boxes
with its own sphere and face matrices. Unknown bounds keep their original
fail-open behaviour and the face lists retain the chosen draw order.
`PointShadowAdaptiveRefresh=1` alternates due updates for lamps whose projected
influence covers at most a quarter of the viewport. It keeps the complete
previous cubemap for one extra update while the map, committed geometry and
live terrain are current. Stationary lamps require a cube under 200 ms old.
For small movements of the same native emitter at a fixed range, the usual
age limit is 66–100 ms, depending on the refresh interval. When at least 256
caster packets are active and the shadow CPU budget is exhausted, a visible
moving emitter can use the alternate slot even if it covers more than a quarter
of the viewport. Its previous complete cube must be younger than 150 ms at the
refresh decision. The hold keeps the previous cube origin for sampling. The
option uses the whole-cube path and is disabled by default.
`world_point_replays.inl` owns point-caster ordering, palette instancing and
indexed draw coalescing. Compatible ranges from different capture groups can
share a draw after comparing the captured pose. Within a run of identical
opaque instanced geometry, equal consecutive bone palettes draw once. Logical
packet counts still drive the partial-face scheduler. `POINT workload` reports
the two reductions in `crossGroupMerged` and `duplicateInstances`.

Adjacent rigid opaque triangle ranges can also share a draw when their captured
banks agree on every float register read by the patched vertex shader. Shader
registration records those reads from the bytecode sent to the device. Cube
projection supplies c2–c5 independently. The draw path still requires a single
geometry and shader binding and preserves the original triangle order. The
stand reports these merges in `POINT_EFFECTIVE_POSE`. Use
`--point-effective-pose` and `--no-point-effective-pose` for the paired replay.
[Point rigid pose rows](../src/shadows/point_rigid_pose_rows.h)

Completed-face reuse is opt-in. Set `NORTHLIGHT_POINT_OPAQUE_REUSE=1` before
launching the game or `NORTHLIGHT_RETRACE_POINT_OPAQUE_REUSE=1` in the replay
environment. The proof checks both projections,
the light and static-face identity, the ordered immutable caster ranges and
every float register read by the patched rigid shader. Direct faces and
faces copied from an unchanged static map with MIN blending can reuse the
proof. Cube replacement, release and a failed draw invalidate its token.
The memory-pressure trim clears the saved proofs.
The bound of 32 saved faces is shared across lights. `POINT_OPAQUE_REUSE`
reports the faces, logical packets, triangles and physical draws it saved.
[Completed-face proof](../src/shadows/point_opaque_face_cache.h)

Adjacent opaque casters with the same captured pose, vertex streams and
coverage can share a draw across disjoint index ranges. Point, directional
and character maps can pack up to four ranges in packet order. A batch must
cross at least one gap and leaves overlaps and any following contiguous run
to the ordinary draw path. All three passes share the index-buffer cache and
its upload budget.
Each submission checks the current vertex page and pose. Entries expire
with their mesh owners and clear under memory pressure or on device release.
`POINT_INDEX_PAIRS` reports pair submissions and combined cache usage for
the point pass. `POINT_INDEX_BATCHES` reports point batches of three or four
ranges. `WORLD directional index pairs` reports pair and batch submissions,
packet counts and uploads for the directional passes.

Point replay submission lives in `world_point_replays.inl`. Within one cube,
successive actor-only faces can reuse vertex bindings. Each face starts with
its own projection, vertex constant bank and material tracking. Static or
terrain binds, palette instancing and the union quad invalidate the retained
bindings.

`world_shadow_bounds.inl` schedules the common current-pose bounds job for
directional, point and flash shadows. The first consumer joins it. The
committed-mesh path in `world_point_mesh.inl` owns per-light terrain masks
and uses the same validated mesh boxes for every cube in a mesh generation.
The optional CPU box cache is capped at 2 MiB. Oversized meshes and failed
allocations use the raw bounds, with the same masks and submission order.

`point_cube_cache.h` owns cube and static-face allocation lifetimes, including
partial creation retries, static-face retention and GPU teardown. The render
pass supplies the current map, selected lights and spare capacity. Its budget
and retry policy remain at the call site.

For geometry capture reuse, pose certificates and range-bank transfer, see
the [capture acceleration investigation](capture-acceleration-20261117.md).
For the current cost of M2 capture and the data the installed game has before
D3D9 submission, see the [native M2 capture investigation](native-m2-capture-20261118.md).

On sampled frames in build 12340, the game's command producer checks WMO
group membership, prepared ranges and group-local bounds, and ADT prepared
ranges and source chunk bounds. The producer reads these records synchronously
from the verified game caller. The shader identity and the caller must agree
on the object class. The audit compares native Gx pool offsets with the
D3D9 arguments before accepting a record. For ADT it recomputes terrain
height extrema from MCVT, then sends an owned box on the same draw command.
Terrain capture checks that box against the exact submitted geometry after
validating the camera basis. The exact D3D9 snapshot remains the geometry
source for the shadow and material passes. [World batch decoder](../src/stream/world_batch_12340.h)

Buffered terrain capture validates its view and projection directly against
the current camera rows when the device mirror knows them under the draw
gate. Missing rows use the original getter. ModelNormals copies known rows
into its existing owned packet bank and retains the independent reread for
its epoch self-check. No borrowed pointer outlives the draw.

For skinned material range checks, `material_skin_bounds.h` owns the envelope and
last world and view bounds of each immutable mesh section. It can reuse both
bounds when the section's active bone rows, camera basis and floating-point mode
match. `material_view_bounds.h` owns camera validation, projection and depth limits.
The capture coordinator shares the resulting bounds with downstream consumers and
reports section-local reuse and evaluations in `MATERIAL_LOCAL_POSE`.

Independent sky/fog controls, the AO-only HDR specialization and current native
precipitation ownership are documented in
[celestial controls and weather ownership](celestial-controls-20261114.md).
The celestial renderer owns the terrain-mask target, shaders and Reset.
The terrain pass culls and submits indexed ranges. World supplies the committed
mesh generation and binds each page through the shared page store. A current
terrain list merges contiguous ranges on the same page. A stale list draws the
accepted batches in their original order.
[Terrain-mask submission](../src/sky/celestial_terrain_pass.h)

The command producer and renderer share `NorthlightWorldDrawDomain::mainWorldDraw`
when admitting a rigid shader as the first world draw. Coordinate proof alone also
accepts client sky and shadow-map programs. The World snapshot therefore requires
known depth-test state and the client main-world viewport band (0..0.94); sky,
horizon, auxiliary and depth-off draws cannot consume its once-per-frame trigger.
Missing cached state after a target/state-block change is recovered through the
ordered device Get path; a failed read cannot grant world admission.
The renderer repeats this admission before reading WMO camera context, alongside
its full-viewport and projection checks. Camera matrix validation remains strict.

Full-screen menu models have a separate scene domain: a proven lit model shader,
perspective projection, depth test, 0..1 viewport and actual backbuffer. The audited
native lighting hook publishes the UI backdrop identity; the producer validates
its loaded model, scene generation and inverse camera pair. The current Gx view
must be identity during M2 drawing: native M2 Update stores the scene camera at
`scene+0x84/+0xc4`, then M2 Render temporarily replaces the device view because
the model vertices/bones already carry the camera transform. The hook verifies
both native code paths; scene and device matrices must not be compared for equality.
The Menu trigger owns camera matrices and local lights in the queued snapshot
(archive version 5), independently of the World trigger and CWorld globals.
Offscreen ModelFFX glow passes and partial UI previews are excluded.

An admitted menu runs the material, GI, directional/local shadow, AO, bloom and HDR
passes before the UI. Current model replays provide GPU casters; bounded posed
geometry supplies the CPU BVH for probes and weather shelter. No terrain or WMO
cache is loaded for a menu identity, and menu shadows do not acquire the native
world shadow provider. Native backdrop ambient/direct light remains the baseline;
its measured key-light direction and energy also drive menu shadows and GI.
The decorative celestial orbit cannot rotate that authored lighting rig.
Backdrop-specific atmosphere profiles supply sky, fog, celestial bodies and weather
without inheriting the last map's clock, underwater state or weather. Menu sky
fill runs before native scene draws, using live per-sample depth. Late backdrop
artwork without depth writes remains intact. HDR menu fill writes absolute linear
colour rather than subtracting a resolved scene at MSAA edges. World precipitation
remains native.
Changing the backdrop or returning to the world clears scene-dependent captures,
probes, lighting and atmosphere history. Main-world admission stays at 0..0.94.

Native point sources retain their original position for draw-slot ownership.
Optional eight-light compatibility retains public shader identity and uses
scoped private variants. Proven no-extra-light draws keep the original shader;
resource caches are bounded independently of source capability metadata.
See [native light performance](native-light-performance.md) for image-checked
A/B measurements, cache ownership and remaining costs.
An independently resolved emitter position feeds direct light, fog and point
shadows. Per-installation NLF1 v4 profiles are computed from actual fixture
geometry and emission evidence without writing MPQs; unsupported profiles retain
the original point. See [point-light-resolution.md](point-light-resolution.md)
for placement bounds, cache compatibility and acceptance requirements.

## Repository map

| Path | Responsibility |
|---|---|
| `src/proxy/` | D3D9 adapters, mirrored state, native draw admission and capture orchestration |
| `src/stream/` | Ordered command transport, replay thread and real completion/query semantics |
| `src/core/`, `src/settings/` | Frame requirements, budgets, profiling and settings policy |
| `src/replay/` | Immutable source snapshots, geometry identity, residency and lifetime |
| `src/world/` | World context, consumer planning, resource ownership and owned pass execution |
| `src/shadows/`, `src/lights/` | Directional/local shadow selection, maps, light identity and scheduling |
| `src/gi/` | CPU acceleration, probe work and immutable publication |
| `src/sky/`, `src/water/` | Sky, weather, fog and water producers/consumers |
| `src/overlay/` | Settings and monitoring UI |
| `src/generated/`, `src/gamedata/` | Generated headers and derived tables; retain their producer/freshness checks |
| `src/imgui/` | Vendored UI dependency |
| `shaders/` | Shader sources/manifests; bytecode and assembly in `compiled/` |
| `scripts/` | Development commands; shader tools in `shaders/`, tuning tools in `dev_tuning/` |
| `renderer/` | Installer, client-data pipeline, package payloads and release packaging |
| `tests/` | Python drivers and native fixtures; shared shims in `support/`, frozen inputs in `fixtures/` |
| `docs/` | Current architecture and development instructions |
| `.github/workflows/` | Automated source-contract checks |
| configured output root | Private build/test artifacts, separated by run; never source inputs |

Root Python modules are existing installer entry points/imports and
`northlight_paths.py`. New development commands belong in `scripts/`.
`check_layout.py` rejects stray root source files, including ignored copies.
Source basenames form the existing include namespace and must stay unique.

## Runtime contracts

The production DLL loads `northlight-assets.bin` once, outside `DllMain`, before
constructing its D3D9 factory. It checks the complete file's SHA-256 against the
identity compiled into the DLL, bounds both file and output sizes, and validates
the zlib stream before publishing immutable, aligned resource storage. Rendering
uses those resident bytes; there is no per-frame file access or decompression.
The decoded bundle occupies about 2.2 MiB of process memory. Smaller installation
files are not a claim of lower rendering memory or improved FPS.

Missing, damaged or mismatched resources leave the backend's original D3D9 factory
unwrapped and log why effects were disabled. Windows/Linux transactions install
the resource file before the DLL; macOS migration includes it in the same rollback
record. Package verification checks the DLL/resource identity as well as manifest
hashes. Canonical generated arrays remain available to independent native/GPU
oracles, including byte equality and malformed-input controls.

1. **One native submit owner.** Four `Draw*` adapters preserve their D3D9 argument
   tuples. `drawHook` owns preparation, fault boundaries and the shared tail.
   `captureGameDraw` preserves terrain-before-material ordering and the current
   material → normal → world → water sequence. Raw and wrapped callbacks have
   different consumers; UP draws retain their separate memory/lifetime rules.
2. **Potential demand and available inputs are separate phases.**
   `NorthlightFrameRequirements::make` is the shared consumer plan. Capture can
   ask for potential materials before composition knows whether they exist.
   AO-only does not acquire model normals or lighting. Fog-only owns its R32F
   distance pair through `WorldDistance`, without model normals or lighting.
   Lighting frames share that pair with WorldNormals/TemporalLight. A producer
   change invalidates history before the pair rotates; fog blur always reads the
   current dry distance and validates water independently.
   The packaged DXVK backends share one R32F water distance texture across
   consumers. Without MSAA the game water draw writes it directly. With MSAA
   the same draw writes distance and binary L8 coverage at the game target's
   sample count and quality. DXVK NONMASKABLE quality zero uses one raster
   sample and writes R32F directly. The edge shader rejects fully wet and dry
   pixels before reconstructing the exact coverage fraction.
   The R32F resolve spans the target to preserve native floating-point rounding.
   The union of the game water draws' scissor rectangles limits correction.
   Bounds covering at most a quarter of the target also limit the L8 resolve.
   Missing or disabled scissor state selects the full target. Sampled WATER
   diagnostics report the bounding area rounded up as `boundsPct` (−1 when
   the frame has no compact capture). A boundary
   pass restores the covered-sample mean at half coverage or above and clears
   smaller fractions. Fully wet and dry pixels already hold their final value.
   Mixed frames merge into the ordinary redraw mask. A busy auxiliary slot
   leaves the game binding in place and uses the existing redraw path.
   Local lamp/spell scattering is added to the current final fog after
   atmospheric history and blur: its volume has no receiver depth for surface
   reprojection. Lamp/veil density uses the shared bilinear medium sampler.
3. **Resources follow consumers.** `world_screen_resources.inl` owns screen
   groups; `world_geometry_resources.inl` owns geometry targets. Groups remain
   warm until reset, resize or pressure. Frame-local/transient sharing requires
   compatible formats and disjoint lifetimes, with one owner and safe fallback.
   Model-normal capture accepts the material pass's raw mesh or looks in the shared
   snapshot pool. On a CPU miss, single-stream draws that need no vertex data for
   culling may lease a resident upload from `scene_native_store.h`. The full tracked
   draw identity includes buffer revisions and layout. Packets hold the buffers
   through replay. A later CPU consumer can acquire its own snapshot.
   `model_normal_programs.h` owns the native shader variants used by replay.
   `model_normal_replay.inl` submits the ordered deferred batch. Consecutive
   packets can share a retained declaration when the current device mirror
   confirms the binding under the draw lock. The owning packet stays in the
   batch through submission or discard.
   `world_normals.hlsl` combines captured model normals with depth-derived normals.
   World passes share raster, depth and camera reconstruction in `world_view.hlsl`.
4. **Worker inputs are immutable.** CPU jobs consume held source generations and
   publish complete results. The D3D owner performs device calls. Reset, pressure,
   resource mutation and pointer reuse cannot retain stale poses or identities.
5. **Completion is real.** EVENT/query availability, source mutation barriers and
   resource leases are correctness contracts. Neither an optimization nor a
   fixture may treat unfinished work as completed.
   The replay thread reads the real D3D9 EVENT after a scheduler wake.
   `CSTREAM QUERYFRAME` splits the request's wall time at replay entry and
   exit. `CSTREAM EVENT_CPU` measures replay-thread cycles only inside the
   pending query loop when command-stream timing is enabled.
   `stream_proxies.h` owns shared proxy metadata, `StreamCore` and dispatch by
   interface type. `stream_buffer_locks.inl` owns VB/IB lock records, shadows
   and slices leased by command sequence. `stream_image_locks.inl` owns
   image subresources, staging and GPU-write invalidation.
   `stream_image_proxies.inl` adapts the image COM interfaces.
   `stream_query_proxy.inl` owns query results by generation and device epoch.
   `stream_proxy_lifetime.inl` owns parent and device pins and records ordered
   destruction. `replay_thread.h` dispatches those commands and retires them
   after the backend call.
6. **Native lamps have one direct-light owner.** A selected lamp can hand
   its native RGB contribution to the renderer when a fresh scene identity or
   an anonymous, proven draw-bank source agrees with its position, hue, falloff
   and selection gain. Draw witnesses expire after 100 ms and grant no shadow
   intent. A valid scene contains positive observations, not a complete inventory:
   a missing ID cannot withdraw a source. A current observation of that same ID
   supersedes its draw witness; unrelated scene entries do not. Scene/draw
   observations share stable identity and the existing 100 ms freshness / 250 ms
   expiry. An observed zero intensity or ownership claim retires the duplicate
   immediately. Required native attachments retain poses and claims for at most
   100 ms across omitted samples, then stop publishing.
   Model/WMO handoff applies to opaque, depth-writing receivers with proven
   vertex outputs and independent pixel coverage. Exact original SM1/2/3 terrain
   shaders use the separate interleaved c28..36 bank, with camera-relative world
   positions. Terrain RGB handoff also covers blended material layers when
   vertex outputs and pixel coverage are independent of the lamp RGB.
   Only the RGB rows c29/c32/c35 change; neighbouring positions, falloff and alpha
   retain their original values. `NorthlightTerrainShadowPrograms` owns the
   neutralized and far-coverage variants, including separate pixel programs
   for one or two shadow sources. These shader objects survive Reset.
   Re-registering a source erases its derived native-light proof before releasing
   the variants. Prepared displacement keeps the source shader's bank through
   its existing provenance.
   Alpha slicing ignores zero-operand NOPs inserted by shadow neutralization.
   `NorthlightNativePointSubmit` owns the temporary bank and extra-light handoff
   during the real submit. It retries restoration once and propagates persistent
   failure to the world renderer. It holds no device resources across draws or
   Reset. Unselected, ambiguous and unsupported sources retain native lighting.
   The original bank and the handoff status remain capture inputs.
   Explicit native diagnostics also record selected sources on receivers where
   handoff is unsafe, and opaque WMO receivers without a proven point bank.
   Receivers without a proven point bank share at most four of the eight image
   pairs per capture. The first four terrain point-bank draws are prioritized
   even when source matching fails. Registration logs their actual shader model.
   Four draw records and four image pairs remain available for terrain point
   banks when other native receivers arrive first.
   MSAA image pairs reuse a GPU resolve capped at 64 MiB;
   only the cropped region (at most 768 by 768) reaches system-memory staging.
   Resolving never changes the game's scene boundaries or render state.
7. **Fixture identity grants shadow intent.** An exact
   GameObjectDisplayInfo owner plus a supported authored M2 point source grants
   root-fixture shadow intent after runtime name, vertex and structure hashes
   match. Constant and animated emitters use the same proof. NLF1 v3 permits
   zero owned-shell triangles and shell sources that are a subset of the proven
   source mask; v2 retains its stricter legacy validation. Lamp geometry casts
   shadows like other geometry; the renderer does not match or exclude a
   light's own triangles. Missing shell metadata cannot disqualify an open brazier. Parent-attached sources and unmatched assets gain
   no fixture classification. See [point-light investigation](point-light-investigation.md)
   for the capture evidence and remaining live acceptance gap.
8. **Track evaluation reads only semantic dependencies.** A single-key M2
   track is constant and does not depend on timestamp memory. The reader still
   validates the descriptor, key count and value. Multi-key curves require
   readable, ordered timestamps. This distinction is necessary for existing
   client assets whose shared constant timestamp tables are relocated repeatedly;
   rejecting an unused pointer must not delete their lights.

9. **Diagnostics do not require receiver or projection admission.** An explicit
   normal-view capture traces full-size, depth-tested native submissions at the
   common Draw adapter, outside shader-family and point-bank gates. It validates
   the bound target, depth and viewport directly: `worldDepth` and
   `projectionValid` are populated too late for an unknown terrain program.
   Each submission exports its
   original programs, constants, render states and before/after thumbnails.
   The 2048-draw cap marks the export incomplete when exceeded. This is causal
   image evidence, not a full geometry replay or a performance measurement.

Directional shadows retain the player-centred near map in flight. Above 128
units of terrain clearance, the far map follows authored ground height and grows
in 96-unit steps, up to radius 768. Its static caster request uses the same
shifted volume; shadow-only terrain loads retain ground below the camera.
The committed mesh carries the ground height and admitted terrain reach, so
memory fallback limits the far scale to supported geometry.

Prelit WMO receivers keep their authored illumination and receive celestial
light as a separate additive term. The directional pass publishes its shadowed
irradiance into a lazy half-resolution FP16 MRT; the prelit compositor uses the
same depth/colour-guided upsample and bounded material estimate as local lamps.
It does not subtract outdoor ambient or apply a solar mask to baked RGB. Roofs
and off-camera casters use the existing camera-independent geometry path; actor
visibility also gates this daylight. Missing coverage/resources retain authored
lighting. GI replaces outdoor ambient on ordinary lit receivers, clamping its
correction before the probe residency fade. Measured probe occlusion can also
attenuate transported baked RGB without subtracting the outdoor light bank.
Posed M2 receivers with a proven room override use the baked receiver path when
the native ambient and directional terms carry the same room tint. This lets
probe occlusion affect both terms, with removal capped at 80% of the native
room light so materials remain legible where probes measure very little bounce.
Different-coloured terms limit probe occlusion to the native ambient share.
The original draw marks that share using the existing stencil bits, rounded
down to one quarter, one third or two thirds across RGB.
The prelit compositor handles only the grades present in the frame. GI brightness controls
added indirect radiance independently of measured occlusion; current local and
directional light remain separate. Missing probes and zero prelit mix preserve
authored illumination.
Receiver registration accepts either a known WMO identity or the same rigid
coordinate proof used by world capture, followed by an independent baked-RGB
dependency proof. A material edit must not remove the prelit mask while its
geometry remains admitted. Normal/global-light dependencies still reject prelit
classification, and known self-lit M2 receivers retain their separate mask.
The sampled `NAMEMASK` record includes prelit and unlit draw counts.

The world environment validates coherent sky samples in their own origin frame.
A source witnessed near the camera may continue during a continuous same-map
walk into an interior whose sky origin stops updating. Colours and celestial
records are reread, not extrapolated. Map changes, camera jumps over 40 units,
invalid cameras and observation gaps over 500 ms revoke that permission. The
strict standalone sky decoders and shorter coherent-light fallback remain.

Sky background composition closes before the first native foreground draw.
That draw may not establish world context: the read-only background camera
path then uses the current validated sky-phase basis and a same-map held
projection/palette. It must not mark world capture valid, depend on celestial
disc visibility, or retry past the foreground/scene-grading boundary.

Expanded-HDR world/material composition may use the existing fused shader only
on a verified loaded DXVK v3.1.1 runtime; DXVK 2 and unknown runtimes retain their
expanded-input guard. Caps, diagnostics and receiver masks retain their existing
fallbacks. See [HDR material fusion](hdr-material-fusion-20261114.md).

## Evidence for a change

World shader admission is shared between command recording and renderer
registration (`shader_tags.h`, `world_shader_contract.h`). An exact known
identity or a complete rigid WMO coordinate proof grants the world camera and
geometry path. The proof follows position input through c31..33 model-view and
c2..5 projection to the final clip output, rejecting later position rewrites,
relative addressing and control flow. Material instruction changes alone must
not remove world admission. Fog, native point-bank layout, RGB-only ownership,
pixel alpha and the current main-target/projection checks remain independent.
Dynamic coordinate admission does not grant c10..12 lighting fallback or a
material replacement. Unknown capabilities retain native rendering.

The producer must run the same proof before the first draw snapshot; accepting
a program only on the replay thread is too late to record its current camera
and native light scene. Admission is computed at shader creation, not per draw.

| Boundary changed | Required checks |
|---|---|
| Frame demand/resource ownership | `architecture` suite; isolated AO/fog/GI/shadow/material combinations |
| Draw/capture orchestration | Four-adapter oracle, draw gates, capture handoff and terrain bounds |
| Shader/pass/state behavior | `gpu` suite plus the affected shader-specific oracle and freshness gate |
| Installer/public Python paths | `packaging` suite and verification of the actual produced archives |
| Harness, layout or selection | `source` suite, including intentional failure controls |

Run the affected tests as well as the named contracts. Named suites are focused
gates, not the full test inventory. Commands and report semantics are in
[development.md](development.md#validation-harness).

A passing report belongs to its recorded source tree. Changed inputs or HEAD
invalidate it. An INCOMPLETE run does not establish missing coverage. Use
`--strict` for acceptance and keep the generated JSON with the reviewed change.

## Remaining architectural work

The imported atmosphere now uses a D3D9 slice atlas for medium/light injection,
3D temporal history, parallel column integration and depth resolve. Its ownership,
cache invalidation and raymarch fallback are documented in [Volumetric fog](fog-volume.md).

The common consumer plan and capture entry are implemented foundations. Complete
full capture-inclusive CPU/GPU budgeting, compact
hot/cold packet storage, further normal replay elimination and measured queue/wait
improvements remain separate work. `WorldRenderer` coordinates pass order,
capture admission and publication across the GPU owners.

The GPU controller includes deferred captures/effects, while in-scene capture
remains outside that interval. The CPU controller does not cover all distributed
draw-hook work. These controllers do not prove a total Northlight budget.
Component tests and worker timings cannot establish game FPS or a 2–3 ms total.
That claim needs matched live routes with identical quality, output and current poses.
