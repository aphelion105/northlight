# Materials, ground normals and tavern shadows

The investigation uses the player's diagnostic sessions
`session-60224-503032000-0` (captures 1–4) and
`session-51920-503196812-0` (captures 1–2). These are native Northlight
captures, not a complete scene replay. Reference files marked `NLCREF1`
must be resolved with `scripts.capture_files.read_bytes`.

## Evidence and changes

### Missing local and indoor shadows

The tavern log repeatedly rejected the complete local world geometry at
the 256 MiB material-texture limit. Repeated placements charged identical
texture pixels repeatedly. At the captured position in Northrend
`5884.11, 514.88, 651.92`, the previous build stopped with 1,078 materials,
267,884,544 charged texel bytes and another 1 MiB material pending.
No usable world geometry or ready local shadow cubes were published.

The local scene now shares byte-identical immutable texels while retaining
separate material metadata and lamp ownership. Hash candidates are checked
by byte equality; each source material is looked up once per build group,
not once per placement. The limit applies to unique retained pixel blocks.
Failures also reach the renderer log instead of silently leaving geometry
unavailable.

`scripts/probe_world_geometry.py` runs the actual local-cache and shadow-plan
builders against installed game data without starting the game. For the
captured tavern it loads 1,476,418 local triangles, finds the roof along all
five tested upward rays, and produces a shadow upload plan with 2,258,186
triangles in 197 pages. This establishes geometry availability, not the
appearance of sunlight through every doorway in a running client.

The subsequent game run with build `4be59c54d779` exposed a second refusal:
geometry was prepared, but GPU admission required 1,076 MiB of free process
address space while only 387–484 MiB remained. This is x86 virtual address
space, not a measurement of available VRAM. Upload preparation still copied
the same images per material and created separate managed textures.

Upload plans now share exact BGRA blocks and canonical GPU texture owners.
Cutoffs, terrain flags and lamp ownership remain independent. The same rule
applies to resident, prepared and file-backed plans, with validated backward
references and one unique byte charge. In the captured tavern plan, 1,315
material slots include 156 aliases: 267.668 MiB of logical cutout images
occupy 129.802 MiB of unique upload pixels.

The production world geometry path uses `D3DPOOL_DEFAULT` buffers, including
the existing compact position/UV vertex format. Upload staging normally lives
in RAM and is released after commit. It does not require a temporary file or
a permanent managed system copy of the GPU geometry. Cutout textures remain
managed and retain their system copy; CPU BVH data remains for the current GI
solver. This change does not move CPU ray tracing onto the GPU.

Before paging, the worker checks the remaining managed-texture upload budget.
If it cannot fit, the existing file-backed recovery path remains available.
Its files are preparation/recovery storage; shadow draws use GPU buffers.
Published CPU BVHs do not use temporary geometry arrays. Normal DEFAULT
buffers are recreated by the worker after device Reset; the file-backed
recovery path can replay its retained payload. The process reserve is unchanged.

The actual tavern GPU fixture compares both pools with resident staging and
checks every page against independent CPU raster input, then repeats after
Reset. Geometry capacities are 74.406 MiB for the managed layout and
50.191 MiB for the compact DEFAULT layout. A DXVK3 run showed roughly 16 MiB
less net process-address use; this is smaller than the resource capacity
because driver allocations also participate. Upload duration from that single
run is not a performance claim. `validate_world_cold_upload_gpu.py` supports
`--storage actual-device-local --center 5884.11 514.88 651.92` to reproduce it.

Identified moving native emitters now retain `CastShadow`, like placed
fixtures. Anonymous draw-bank observations remain unclassified. A nearby
shadow source is no longer excluded simply because its radius is at most
five yards. The existing bounded selection and cube scheduler still apply;
candles and native-dependent sources retain their explicit exclusions.
An overlapping particle glow cannot replace an identified shadow source
or alter its calibrated energy.

### Triangular ground lighting

The outdoor capture's light discontinuities line up with the triangular
normals reconstructed from depth, while the original scene is smooth and
the ground material-height buffer is largely empty. This is evidence of
incorrect shading normals, not evidence that displacement moved the snow
into those triangles.

Proven terrain shaders now supply the authored, interpolated `NORMAL0` in
view space. The terrain range is independent of the actor's 48-yard cutoff.
On compatible targets, the normal is written alongside the ordinary native
draw through MRT. Incompatible targets, including MSAA, retain the existing
private replay and coverage checks. Terrain and actor shader variants keep
separate distance limits. No terrain subdivision is added by this fix.

### Missing tavern relief

Three vertex programs present in the enabled client archives and tavern
captures were accepted as WMO world geometry but absent from the material
catalog:

| Hash | Client program |
|---|---|
| `b0f4dcddf18e636c` | `mapobjudiffuse_t1`, variant 39 |
| `29c8bb3a05451123` | `mapobjudiffuse_t1`, variant 69 |
| `0a3a712c94bac330` | `mapobjuspecular_t1`, variant 69 |

The material registry now accepts the existing WMO provenance proof and
still requires its bytecode patch to validate. In wall displacement mode,
a draw with no prepared displaced mesh can use POM. A successfully displaced
mesh is never shifted a second time by this fallback. POM retains its own
distance, depth, coverage and material-pair checks.

This repairs admission and the missing fallback; it does not claim a new
displacement algorithm or stronger silhouettes for every game material.
The user's GI strength/brightness settings are unchanged.

## Verification and limits

The material GPU check extracts the three actual enabled-client programs.
It exercises their wall POM fallback and smooth terrain normals at 1 and
80 yards through immediate replay, deferred replay and native MRT. Both
Windows D3D9 and DXVK2 pass. The native normal regression also checks actors,
material/light composition, target changes and rollback on both backends.

The texture-sharing regression retains 900 distinct lamp/material owners
with 900 MiB of logical images in two 1 MiB immutable blocks. It checks
different pixels, metadata, tracing and lifetime with both accelerated and
flat scene builds. Existing shared-scene comparisons compare texel contents
instead of a particular vector representation.

Game acceptance still requires the original views: the neighboring ground
chunk, snow triangles, tavern walls, a moving light and indoor sunlight.
Component tests do not establish whole-game performance or prove that every
reported visual symptom is gone. No release is created as part of this work.
