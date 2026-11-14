# Crypt lighting, lamp fog and character-shadow capture

This change adds `GIPrelitMix` (0–100, default 100) to **Lighting & AO →
Global illumination → Baked lighting blend (%)**. It applies live and is saved
by the normal settings service. Zero retains authored building illumination;
100 applies resident-probe occlusion and positive bounce. `GIStrength` and
`GIBrightness` remain independent controls. Moving this slider does not restart
the probe solver.

## Evidence and capture ownership

The local investigation is preserved in
`.local/out/lighting-shadow-review-20261114/`. The original outdoor log was
copied before the next game session rotated it. `crypt-northlight-renderer.log`
and `crypt-northlight-renderer.prev.log` are separate copies of the later logs.
Private captures and previews are not release inputs.

* Outdoor session `session-32056-331176218-0`: captures at frames 57183
  (world/actor shadows on), 57321 (world shadows off), 58131 (actor shadows off).
  Native scene images are nearly unchanged. The final atmospheric aureole changes
  drastically with World Shadows.
* Crypt session `session-23072-331879875-0`: complete captures at frames 1040
  (stairs) and 1737 (room); frame 1930 is incomplete. Probe residency is present
  (`gi=1 probes=1`). Authored blue fill remains on walls/floor in the final image.
* HDR previews decode the captured linear buffer to sRGB and clip at one. They
  do not reproduce the client's HDR exposure, display or tone mapping.

The crypt run, excluding startup below frame 254 and ±140 frames around captures,
has 10 CPU samples: median Northlight CPU total 3.488 ms, capture 1.021 ms,
effects 1.808 ms, cleanup 0.271 ms. Nine effect GPU samples have a median
5.406 ms total; largest median buckets are PointLight 0.843 ms, PointShadow
0.723 ms, RemovalSmooth 0.710 ms and WorldComposite 0.569 ms. This short,
moving/streaming session is not a matched performance comparison.

## Why detailed actor maps stay separate

There is one shared 1024² near actor map per celestial source, not one map per
character. Its roughly 24-unit extent gives much finer texels than the world
near cascade. The near-cascade replay already skips actors fully covered by
that map; partially covered or unproven actors remain in the near cascade.
Merging at the existing world-map resolution loses detail. Matching the actor
texel density by enlarging the world map increases area and cost.

The outdoor log is more informative than the setting's label. With World Shadows
on and quality GI off, excluding settings transitions and captures, actor-on has
26 CPU samples (median total 4.854 ms, capture 1.862 ms); actor-off has only two
(3.218 ms, capture 1.193 ms). The detailed filter itself is approximately
0.039 ms GPU and the actor-map raster approximately 0.010 ms. Disabling
Actor Shadows also stops model capture and point-shadow casters, including
rigid props/foliage. It is not a measurement of the detailed filter alone.
There is no clean actor-off 120-frame FPS window after exclusions.

The implemented CPU change hands the normals producer's immutable snapshot to
the shadow consumer in the same serialized draw, including when AutoBump is off.
It publishes before receiver range culling or substitution of a GPU-resident
lease. UP draws retain their original ownership. The shadow consumer still
checks the source generation, declaration, draw arguments and its own budget;
untracked or stale snapshots fall back to the ordinary capture route.

Five alternating pairs of a 128-draw, 4K, stock-model GPU stand produced these
median CPU times (milliseconds, repeated lookup → handoff):

| Path | SDR | HDR |
|---|---:|---:|
| Deferred normal replay | 0.2602 → 0.2063 | 0.2260 → 0.2015 |
| Native normal MRT | 0.2398 → 0.2105 | 0.2375 → 0.2484 |

The last result is noisy and slightly worse; do not claim a universal speedup
or translate this into game FPS. The stand includes native submission, normal
capture and one shadow-source admission, not the remaining renderer. Detailed
shadow resolution, geometry, animation, alpha tests and filter quality are unchanged.

## Lighting changes

**GI on authored surfaces.** The prelit WMO composite previously selected
`PrelitDaylight.rgb` instead of `LightingBuffer.rgb`, discarding GI even when
probes were solved. The existing prelit target now carries positive GI bounce
in RGB and resident-probe occlusion in its unused alpha. The separate GI draw
writes it as MRT1; merged lighting/GI writes it as MRT2. The existing final
composite consumes it. There is no additional draw or probe solve. GI-only
frames with prelit receivers can now allocate this existing optional target.

Occlusion attenuates the transported authored surface term, preserving native
fog and authored hue. Missing probes retain authored lighting. Fresh local
radiance is added afterward. The authored layer may itself contain baked lamp
light; this is not an exact decomposition of its original material/illumination.
Direct sun/moon on prelit surfaces still requires actual directional-map
coverage (`DirectLight.w`), so enabling GI with World Shadows off cannot leak
unoccluded daylight through a crypt roof. The slider does not alter that proof.

**Sky and World Shadows.** The old volume path zeroed source RGB and imported
Forever source weight when directional shadow maps were unavailable. This
disabled atmospheric light rather than just its shadow visibility. Neutral
maps now mean unoccluded light; atmospheric direction follows the current
celestial source, independent of committed shadow-map history. Surface shadow
directions retain their existing stabilization. Existing celestial light/glow
controls remain the controls for these quantities; no duplicate master toggle
was introduced.

**Weak lamps.** Final composition previously capped new lamp radiance by the
already-dark native surface. HDR now retains incoming radiance for tone mapping
and bloom. SDR compresses the sum once into remaining display headroom, keeping
its hue. Source positions, attenuation and shadow-map depth/filtering are unchanged.

**Excessive lamp fog.** LocalFogCombine no longer boosts overlapping glows by an
effective light-count exponent. It applies one hue-preserving bound to the summed
radiance, independent of batching. The empirical scattering gain changes from
10 to 0.25; extinction, spatial support and the near fade are unchanged. This is
an artistic calibration, not a claim of calibrated physical units. Appearance
in the user's crypt/outdoor scene still requires checking in the new game build.

Shader slots change from 415 to 382 for LocalFog, 22 to 10 for its combine,
510 to 500 for the prelit HDR composite, and 506 to 495 for the ordinary HDR
composite. GI grows from 377 to 387; merged unshadowed lighting/GI from 501 to
510, within its 512-slot gate. All dependent shadow-transition and volume
manifests must also be regenerated when `world_effects.hlsl` changes.

## Validation and delivery

Use source and architecture suites plus affected settings/GPU tests. The GI GPU
oracle checks 0/50/100%, separate versus merged output, absent probes/map proof,
sky/unlit/ordinary preservation and independent HDR lamp addition on native D3D9,
DXVK 2 and DXVK 3. The local-fog oracle checks analytical radiance, exact zero,
bounded energy and batching on the same backends. The production WorldRenderer
fixture checks state restoration, MSAA and extinction independent of source light.
The normal-source GPU fixture covers 12,120 image/state/failure scenarios.

Final reports belong under this investigation's `accepted-*` directories;
`acceptance.json` records the exact accepted source identity and installation.
Trial, failed or STALE reports are not acceptance. Install the matching
`d3d9.dll` **and** `northlight-assets.bin`, with the previous pair backed up.
No new matched live-game FPS or full crypt-scene replay is claimed.
