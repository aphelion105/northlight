# Independent sky and fog lighting

`CelestialEffects` owns Northlight's sun, moon, glare, sky composition and HDR
sky residual capture. It appears in the overview and **Sun & moon** page.
Turning AO, shadows, GI or weather on no longer implicitly enables that owner.
With CelestialEffects off the native sky draws remain; HDR presentation is a
separate setting. The master Effects/F10 switch still disables all scene effects.
Both new controls default to 1 to preserve existing installations.

`Godrays`, in the overview and **Atmosphere**, controls sun/moon scattering in
WorldFog. It needs a fog medium, so it does not activate the pipeline by itself.
Off retains extinction, ambient scattering, cloud density and local lamp fog.
It skips the second celestial source, wide actor tiles needed only by fog, and
directional shadow sampling. Regional clouds and volume-atlas injection have
ambient-only shaders; the legacy imported-fog fallback takes its existing uniform
no-shadow branch. Changing the control invalidates fog history and the atlas.
Surface shadows still run when their own consumers need them.

## Base composition and measurement

The final AO-only interval in the copied November 14 log used 3840 x 2160, MSAA 2:
AO plus blur approximately 0.57 ms, WorldSetup 0.18 ms, WorldComposite 0.30 ms.
The old WorldSetup interval includes HDR base preparation and late HDR celestial
work. A near-zero CelestialDiscs interval alone does not establish free celestial
rendering. New HDRBase and HDRCelestial markers separate the late stages;
native-sky hooks are still inside GameScene.

The `ao-hdr` compositor specializes the existing verified HDR base path when
frame requirements prove there is no lighting producer. It removes lighting and
baseline texture reads (samplers 8 and 12), retaining AO, depth-edge fallback,
night brightness, native fog and coverage. Failed shader creation retains the
ordinary compositor. This is fewer operations, not a measured game FPS claim.

`test_celestial_controls_gpu.py` runs the real WorldRenderer on private targets
with MSAA 0 and 2. It compares old/new base composition over varied depth, alpha,
AO, day/night and native-fog inputs; checks fog extinction with rays on/off in
regional, imported fallback and noisy/quiet atlas paths; checks actual shader
dispatch and live history reset. These are component image/ownership contracts.
The optional 4K component timings were noisy under concurrent GPU load, including
one reversed pair; they do not establish a reliable speedup or gameplay FPS.

Local evidence lives in `.local/out/celestial-controls-20261114/`; only reports
that verify against the current source tree establish current acceptance.

## Native precipitation ownership

The live weather tail now leaves scene rain/snow particles and their shelter
decisions to WoW. It no longer runs Northlight's additional procedural rain/snow
pass or builds its world-space roof grid. Camera rain, snow/frost and sand remain;
their exposure worker retains five cached upward rays and is requested only when
camera optics need it. With no camera effect, the tail avoids its saved-state scope.

This removes an added particle layer, so total precipitation is intentionally not
pixel-identical to the former native-plus-supplemental composition. Native rain
material/coverage adaptations and installed weather artwork remain. Legacy
RainDropSize/SnowFlakeSize settings are retained for old lab captures but hidden
from the live menu. New captures declare `scene_precipitation: native`; the lab
does not add particles again. The old roof implementation exists only as a frozen
test fixture for historical captures. Earlier roof reports describe the former
runtime path.
