# Volumetric fog

The imported Forever and expansion atmosphere uses a camera-frustum volume on
D3D9. The implementation is independent of the shipped Forever shader code;
[the client investigation](forever-fog-renderer-investigation.md) describes the
reference and the limits of the static evidence.

For Azeroth/Kalimdor the local `forever-atmosphere.bin` must be beside the DLL.
`CelestialStyle=Forever` selects orbital placement only; it does not supply
atmosphere data. `FOREVER atmosphere loaded=0` means those continents use the
regional fallback even when all volume shaders are in the resource bundle.
An active volume reports `FOG_VOLUME` on sampled frames. Outland/Northrend
instead use the built-in biome profiles and their FRF parent-zone coverage;
they do not require the external Forever table.

Expansion profiles author scattering/extinction only. They inherit the native
direct/ambient palette before interpolation, preserving skybox and material
emission. Publishing their default zero palette would incorrectly remove
environment lighting when the shared atmosphere consumers become active.

`fog_volume.hlsl` evaluates the existing three-layer medium, noise and celestial
shadow functions per cell instead of per screen-ray sample. The grid has one cell per 16 screen pixels in
XY and 64 radial slices with cubic distance spacing. An 8×8 slice atlas allows
SM3 raster passes to implement the volume without compute shaders or 3D render
targets. Sun and moon add radiance into the same coefficients; density is written
only by the first source.

The imported medium uses the authored absolute/relative height profiles. The
regional field supplies coverage and a reference elevation for relative layers;
its topmost WMO/terrain height is not an occupancy test. Treating everything
below that surface as solid erases fog under city overhangs. Both the atlas and
fallback raymarch preserve air below roofs; scene depth supplies the receiver
endpoint. Shadows affect scattering, never extinction. See the
[Orgrimmar capture diagnosis](fog-overhang-20261114.md).

Noisy cells use two-point Gauss quadrature within their radial interval. This
reduces aliasing of density noise in wider distant slices without adding atlas
memory or prefix passes. Quiet cells retain a single density evaluation.

The three RGBA16F atlases rotate between current coefficients, coefficient
history and prefix-scan scratch. A temporal pass reconstructs each cell in the
previous view, samples its previous XYZ position and clamps history to six
current neighbours. History is rejected outside the previous frustum and after
a camera cut, map/field replacement, disabled fog or resource reset. It does not
use the depth of the visible wall or ground.

One conversion pass and six parallel prefix passes integrate each column.
The accumulated alpha stores opacity to avoid FP16 rounding near unit
transmittance. Resolve samples the prefix before and after the receiver and
integrates the fractional segment with Beer–Lambert attenuation. Water retains
the existing validated receiver endpoint. The established half-resolution
bilateral blur and final upsample remain in use; this path skips the old 2D
atmospheric history pass.

## Update policy

Regional terrain/medium data and the noise texture retain their existing caches.
The integrated volume is reused when the camera, projection, medium, celestial
inputs and relevant shadow content are unchanged. Receiver depth and local
lamp movement do not invalidate it. Shadow transitions, moving actor shadows
and active noise wind require updates. Static noise without wind can be reused.
After camera motion stops, one frame computes fresh coefficients without
temporal blending before caching, so a partially converged history cannot freeze.

Per-frame work on a reusable volume is the depth resolve and existing screen
filtering. Motion still requires volume updates because the grid follows the
camera and the imported medium contains view-distance terms; it cannot safely
be cached solely on the sun's direction.

Local torch/spell glow remains an analytic current-frame pass after atmospheric
history and both blur passes. It never enters either atmospheric history.

## Ownership and fallback

`WorldRenderer` owns `NorthlightFogVolume`. It creates the atlases lazily for an
active imported atmosphere, counts them as post targets and releases them on
reset, resize and memory pressure. The extra allocation is
`24 × 64 × ceil(width/16) × ceil(height/16)` bytes: approximately 12 MiB at
1920×1080 and 47.5 MiB at 3840×2160. These values exclude driver overhead.

Unsupported shader/texture caps, shader creation failure, partial allocation
failure and memory pressure retain the original raymarch path. The separate
regional artistic atmosphere also retains its existing path. No new resource
is required by fog-off or other effect-only frames.

## Verification

`test_fog_volume_build.py` checks dependency freshness, all eleven generated
SM3 kernels and embedded bytecode. `test_fog_volume_gpu.py` checks independent
Beer–Lambert integration, atlas boundaries, odd sizes, 3D reprojection and all
eight injection variants against the fallback raymarch on native
D3D9 and DXVK. This comparison measures numerical differences; it does not
establish identical output for every scene.

The same test checks roofs above the camera, a split roof field, masked regions,
outside-field fallback, absolute and relative profiles, and lit/shadowed source
directions. Roof changes preserve absolute-profile transmittance bit for bit;
light changes preserve transmittance while changing scattering. `--backend`
can be repeated to test an explicit backend scope; the report records that scope
and DXVK environment instead of implying native D3D9 coverage.

`test_world_screen_effects_gpu.py` exercises the production executor, cache
settling, on/off, partial allocation failure, shader/capability fallback,
state restoration and local-light retirement with MSAA off/on.

Optional component GPU timings are enabled with
`NORTHLIGHT_FOG_VOLUME_BENCHMARK=1` for `test_fog_volume_gpu.py`. The process guard
requires the game to be closed for timings. Timestamp results cover the fog
component, not whole-game FPS or representative live-route acceptance.
