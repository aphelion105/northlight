# Architecture and ownership

The current renderer is an x86 D3D9 proxy in the client process. This map describes
the implemented boundaries. Historical investigations and release reports remain
in `renderer/docs/`; they do not replace the current contracts or prove current performance.

For native-versus-Northlight work ownership, current pass dependencies and
measured optimization priorities, see the [2026-11-14 pipeline audit](pipeline-audit-20261114.md).
For the next gameplay log, zero-weight lighting-filter optimization and separate
HDR stage timings, see the [2026-11-15 follow-up](pipeline-20261115.md).
For local-light shadow costs and exact reuse of unchanged cube preparation, see
the [2026-11-17 investigation](local-light-performance-20261117.md).
For geometry capture reuse, pose certificates and range-bank transfer, see
the [capture acceleration investigation](capture-acceleration-20261117.md).
For the current cost of M2 capture and the data the installed game has before
D3D9 submission, see the [native M2 capture investigation](native-m2-capture-20261118.md).

Independent sky/fog controls, the AO-only HDR specialization and current native
precipitation ownership are documented in
[celestial controls and weather ownership](celestial-controls-20261114.md).

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
   material в†’ normal в†’ world в†’ water sequence. Raw and wrapped callbacks have
   different consumers; UP draws retain their separate memory/lifetime rules.
2. **Potential demand and available inputs are separate phases.**
   `NorthlightFrameRequirements::make` is the shared consumer plan. Capture can
   ask for potential materials before composition knows whether they exist.
   AO-only does not acquire model normals or lighting. Fog-only owns its R32F
   distance pair through `WorldDistance`, without model normals or lighting.
   Lighting frames share that pair with WorldNormals/TemporalLight. A producer
   change invalidates history before the pair rotates; fog blur always reads the
   current dry distance and validates water independently.
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
   `world_normals.hlsl` combines captured model normals with depth-derived normals.
   World passes share raster, depth and camera reconstruction in `world_view.hlsl`.
4. **Worker inputs are immutable.** CPU jobs consume held source generations and
   publish complete results. The D3D owner performs device calls. Reset, pressure,
   resource mutation and pointer reuse cannot retain stale poses or identities.
5. **Completion is real.** EVENT/query availability, source mutation barriers and
   resource leases are correctness contracts. Neither an optimization nor a
   fixture may treat unfinished work as completed.
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
   retain their original values. Terrain shadow-neutralization and coverage
   factories audit their derived shaders too; prepared displacement keeps the
   source shader's bank through its existing provenance.
   Alpha slicing ignores zero-operand NOPs inserted by shadow neutralization.
   The native constant change is scoped to the real submit and restored
   even on failure. Unselected, ambiguous and unsupported sources retain native
   lighting. The original bank and the handoff status remain capture inputs.
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
Explicit room banks keep their authored fill: their baked, ambient and emissive
components cannot be separated by the outdoor GI estimate. In the world scene,
posed M2 receivers with a proven room override use the existing baked-receiver
path so their carried light responds to resident probe occlusion. GI brightness controls
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
improvements remain separate work. `WorldRenderer` still contains much of the
pass executor; moving resources into files did not complete its decomposition.

The GPU controller includes deferred captures/effects, while in-scene capture
remains outside that interval. The CPU controller does not cover all distributed
draw-hook work. These controllers do not prove a total Northlight budget.
Component tests and worker timings cannot establish game FPS or a 2вЂ“3 ms total.
That claim needs matched live routes with identical quality, output and current poses.
