# Northrend aurora illumination and GI response

Status: option B selected by the user on 2026-11-11; visible sources and complete-sky HDR composition implemented; final in-client visual acceptance pending.
The earlier uniform-sky implementation is superseded. Base: `6b87cafa`.
Evidence: `aurora-gi-validation-20261111.json`; integration notes:
`aurora-gi-handoff-20261111.md`.

## Intended result

Existing Northrend aurora ribbons gain controlled green/purple emission during
the evening gap after sunset and before moonrise. The visible additional
emission is the source of the corresponding illumination on geometry. Objects and terrain receive stronger,
geometry-aware GI by default. Players can independently tune GI influence and
make the measured indirect light darker or brighter.

## Controls and compatibility

| Quality key | Range | Default, all presets | Meaning |
| --- | --- | --- | --- |
| `GIStrength` | 0..240 | 120 | Influence of measured GI versus native ambient. 60 retains the old 0.5 blend, 120 gives full replacement, 240 doubles the correction. 0 suspends probe work. |
| `GIBrightness` | -100..200 | 0 | Signed percentage adjustment to measured indirect radiance: -100 = zero, -50 = half, 0 = unchanged, +100 = double, +200 = triple. |
| `AuroraStrength` | 0..200 | 100 | Common scale of added visible aurora emission and its transport. 0 preserves native ribbons and disables the addition. |

All controls are live and support preview, save, reset and INI parsing.
GI controls appear under Global illumination; AuroraStrength appears under
Environment because visible emission also works with GI disabled. Existing explicit `GIStrength`
values keep their numeric response. Only defaults become stronger. Brightness
changes neither rays, bounce count, probe range, capture nor solver work.

Signed integer support belongs in the quality key registry; existing unsigned
keys must continue rejecting negative values, overflow and trailing junk.

## Aurora source вЂ” selected option B

### Ownership and source identity

Preserve the game's selected mesh, native vertex/UV animation, textures, alpha,
and skybox blend. The supplied capture contains seven emitter draws inside
`fiord` and `fr_single_moon_105_529/530/531` skyboxes, not standalone Aurora.m2.
The curated LightSkybox catalog has 264 unique batch signatures in 15 emitter
families; two ambiguous signatures are rejected. Assets are bound using both
texture upload hashes, full indexed draw tuple, declaration and the exact
captured VS/PS signatures. This is an audited asset-to-draw binding, not a
cryptographic proof of the live VB bytes (those were unavailable in the capture).
Other shader programs retain native shading until independently supported.

The source worker evaluates the captured native VS/constants against the
curated position/UV inputs. Native color, animated UVs, source-side culling,
alpha test, sampler addressing and additive alpha are retained. Only the
source worker expands geometry and projects its field; no GPU buffer lock or
readback occurs. Complete emitter subsets are required per native VB group;
unknown, duplicate or incomplete subsets never create a guessed source.

Source observations are bounded to eight groups / 16,384 triangles per frame,
at most every 125 ms. A single worker consumes copied CPU state and returns
immutable fields. Selection/map/reset changes reject in-flight old results.
Camera-independent world directions come from the validated current sky camera
basis, not the previous world frame. A complete source remains when its native
draw is culled offscreen. Its last observed native UV pattern/opacity is held
until another complete observation; no unverified animation extrapolation is
claimed. Native motion remains full rate in the visible material.

The shared existing CPU mip cache remains bounded to 8 MiB and is retained
independently of EquipmentLights, which may be disabled while aurora is active.
The diffuse source uses its 32-texel mip with bilinear sampling (an approximation
to the native anisotropic filter), not pixel-exact texture integration.

### Complete sky HDR composition

Both captures retain saturated green ribbons in native-scene but lose colour
and horizon brightness in hdr-linear-scene. The first per-material intensity
calibration restored visibility but did not preserve the native additive colour;
the second capture exposed that limitation. It is superseded by complete-sky
composition, not a saturation or brightness floor.

The native framebuffer retains all authored sky layers in their original order.
The existing FP16 sky target starts at zero extra emission. Ordinary opaque and
translucent foreground draws only attenuate that extra emission with their native
coverage and destination blend factor. Additive native layers need no replay;
screen and multiplicative layers retain their transmission factors. MRT writes
native COLOR0 alongside zero-RGB/native-alpha on its HDR companion. Unsupported
blend operations reject the extra-sky path for the frame and retain native sky.

At the effects boundary, decode the complete native sky once and add it to the
FP16 extra emission, only at untouched clear depth. Native horizon geometry is
decoded through the ordinary world path. This preserves authored sky hue and
brightness at zero extra emission, including daylight and AuroraStrength=0.
The conversion uses the existing FP16 target; no additional full-size image is
allocated. It replaces the earlier initial copy/decode and per-layer transfers.
MSAA resolves into the same frame target; inline/fallback retries never add the
native baseline twice. A new sky phase resets this ownership every frame.

Proven aurora materials retain authored RGB above 1 using the same linear
transfer principle as proven additive particles. Only that excess above the
native UNORM output and the controlled E are added to the sky target. Native
alpha, UV animation, alpha test and texture sampling are retained.
Early owned sun/moon discs similarly write their linear excess above the native
SDR surface. Their early screen glow is already in the native sky; it adds no
separate headroom and does not attenuate its own HDR disc. Late celestial passes keep their
normal HDR output. Clouds continue to cover early discs in native order.

Final paper white, exposure, contrast, bloom and peak mapping still operate on
the resulting scene. Arbitrary non-neutral grading is not promised to match an
Effects-off screenshot pixel for pixel. The fix removes the unintended change
caused by decoding and blending each native sky layer separately.

### Shared emission

Let N be the clamped native material RGB, interpreted as emitter intensity.
The controlled addition is E = gain * luminance(N) * palette(phase), with the material's actual alpha
and blend factors applied consistently to visible output and captured source.
Original material shading stays authoritative for filament detail and motion.

Visible emission adds E to the native appearance. GI receives E only, alongside the
existing authored ambient; it must not add the whole native appearance again
or subtract an assumed native aurora light. No separate surface tint exists.

The legacy shader encodes decode(N) + E for the SDR target. During HDR the
native SDR draw stays unchanged, and the dedicated replay writes
`decode(max(rawRGB,0)) - decode(saturate(rawRGB)) + E` into the extra-emission
target. Native alpha/blend applies once. The complete native sky is decoded
once at composition; E is never counted in both the SDR baseline and HDR replay.
The SM2 transfer uses a TEXCOORD linker input so the compiler cannot optimize
away RGB headroom by assuming COLOR is already bounded to [0,1]. There is no
new global particle-brightness slider: this reuses its HDR emission principle.

- Exact map `Northrend`, valid celestial context, evening half of the game day,
  both native celestial elevations below the geometric horizon.
- Smoothstep fade from sun elevation 0 to -4 degrees and moon -4 to 0 degrees.
  Camera direction and a moon hidden by scenery never trigger activation.
- Common gain = 4 * AuroraStrength / 100 * orbital weight, bounded by the
  setting's 0..200 range. The 4x gain is an initial calibration, to be reviewed
  with a real native frame; it multiplies native emission rather than ambient.
- Green-to-purple phase follows the game-day interval 0.85..0.925 with
  smoothstep. Native animation clock remains unchanged.
- At zero gain, daylight, dawn, moonlit night, another map or invalid context,
  preserve the native SDR material and contribute no additional source. The
  independently validated HDR baseline correction remains active.

### Directional transport and caches

Project additional emission into 32 world-oriented, equal-solid-angle sectors:
8 azimuths using atan2(worldX, worldY), 4 heights uniform in normalized world Z.
This is a diffuse approximation of the detailed visible field. Surface
normals, geometry visibility and material bounces act through the ordinary GI
tracer. The source continues illuminating when the camera turns away.

At each escaped path evaluate its world sector. Cache separate RGB transport
SH coefficients for these sectors; changed colors relight the same paths.
First-bounce sky and subsequent bounces remain one diffuse solve, not an extra
term over an already-counted source. Dynamic-path generations and static
fallback checks include angular-source changes and removal.

The existing relight-cache budget remains 4 MiB total. Sector responses occupy
an optional sidecar only with an active source; ordinary environments retain
their original entries and slot count. Active angular storage reduces slot
count instead of raising the x86 memory budget. No change to rays, bounces,
probe texture layout, shader sample count or the final GI composition.

GI/F8/Effects controls still determine surface transport. AuroraStrength
controls visible source emission; GI brightness/strength remain artistic
controls for the response on geometry. Missing probes retain native ambient
rather than inventing a substitute screen-space light. Fog is independent.

## GI composition

Let A be nonnegative native ambient, P measured diffuse probe radiance, C probe
coverage, S = GIStrength / 120, and B = 1 + GIBrightness / 100.

`correction = max((P * B - A) * S, -A) * C`

Then apply the existing normal-confidence suppression to positive correction,
and existing twilight/weather fill to the resulting ambient. The clamp precedes
coverage so sparse/new probes fade continuously and cannot subtract more than
native ambient. Missing probes retain native ambient at every slider setting.
Colour remains RGB rather than being reduced to a brightness-only adjustment.

Both standalone WorldGI and merged WorldLightingUnshadowedGI use this equation.
The final relative 45% surface-darkening safety limit, fog transport, local-light
composition and prelit exclusions remain in effect. A negative brightness value
therefore dims GI without promising a black final framebuffer.

Use c66.y for the signed brightness delta in GI shaders. This component is free
during GI; subsequent volume passes own c66 as FogActorBounds/LocalLightFog.
Keep the existing 68-register diagnostic capture format and explicit ownership.

## Performance and validation

Required checks:

- Full-day orbital gates, continuity, invalid inputs and 0/100/200 strength.
- Fresh versus cached directional solves; different normals; sealed room;
  material bounces; source change/removal; dynamic path replay; cache <=4 MiB.
- Native model/batch identities, duplicate/incomplete observations, unsupported
  shader/geometry fallback, offscreen coverage and camera-independent emission.
- Shared CPU/GPU emission equation, native alpha and UV animation, shader
  register/slot admission, no extra texture samples in the emission tail.
- SDR/HDR correspondence and state restoration; exactly one source draw;
  reset/lost-device, sky blends and partial/failed capture.
- Existing signed GI settings, fallback and fused/separate GPU regressions;
  shader freshness, production DLL and release package verification.
- Real captured native aurora frame, camera rotation, source-only versus
  transport contribution, overhang and indoor scenes. Measure CPU/GPU cost and
  cache hit behavior on the target renderer before claiming a frame-time gain.

The earlier preview images are offline architecture illustrations. They do
not validate native animation, material fidelity or runtime performance.
Measured results and outstanding limitations belong in the handoff report.
