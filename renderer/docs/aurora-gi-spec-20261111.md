# Northrend aurora illumination and GI response

Status: option B selected by the user on 2026-11-11; visible sources and complete-sky HDR composition implemented; final in-client visual acceptance pending.
The earlier uniform-sky implementation is superseded. Base: `6b87cafa`.
Evidence: `aurora-gi-validation-20261111.json`; integration notes:
`aurora-gi-handoff-20261111.md`.

## Intended result

All selected native Northrend aurora ribbons emit visible light and directional
GI whenever the game displays them. They gain extra green/purple emission
during the evening gap after sunset and before moonrise. The visible additional
emission is the source of the corresponding illumination on geometry. Objects and terrain receive stronger,
geometry-aware GI by default. Players can independently tune GI influence and
make the measured indirect light darker or brighter.

## Controls and compatibility

| Quality key | Range | Default, all presets | Meaning |
| --- | --- | --- | --- |
| `GIStrength` | 0..240 | 120 | Influence of measured GI versus native ambient. 60 retains the old 0.5 blend, 120 gives full replacement, 240 doubles the correction. 0 suspends probe work. |
| `GIBrightness` | -100..200 | 0 | Signed percentage adjustment to measured indirect radiance: -100 = zero, -50 = half, 0 = unchanged, +100 = double, +200 = triple. |
| `AuroraStrength` | 0..200 | 100 | Common scale of all native aurora emission, twilight addition and their GI. 0 disables the aurora, 100 is normal, 200 doubles source radiance. |

All controls are live and support preview, save, reset and INI parsing.
GI controls appear under Global illumination; AuroraStrength appears under
Atmosphere & weather / Weather. Visible emission also works with GI disabled;
the control is independent of enhanced rain/snow and atmospheric fog. Existing explicit `GIStrength`
values keep their numeric response. Only defaults become stronger. Brightness
changes neither rays, bounce count, probe range, capture nor solver work.

Signed integer support belongs in the quality key registry; existing unsigned
keys must continue rejecting negative values, overflow and trailing junk.

## Aurora source Р В Р вЂ Р В РІР‚С™Р Р†Р вЂљРЎСљ selected option B

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
decoded through the ordinary world path. This preserves the authored non-emissive sky hue and brightness. AuroraStrength=0
keeps native aurora HDR emission; the added twilight source is disabled.
The conversion uses the existing FP16 target; no additional full-size image is
allocated. It replaces the earlier initial copy/decode and per-layer transfers.
MSAA resolves into the same frame target; inline/fallback retries never add the
native baseline twice. A new sky phase resets this ownership every frame.

Proven aurora materials use native additive RGB as emitter intensity and retain
authored RGB above 1 using the particle transfer decode. Their native radiance
is `max(rawRGB, decode(rawRGB)) * intensity * exp2(HDRLegacyExpansion)`. This is an explicit
emissive calibration of the authored masks; ordinary diffuse sky layers keep
their existing SDR decode. Only the excess above the scaled SDR source baseline
and the controlled E are added to the sky target. Native HDR emission remains
active in daylight while AuroraStrength is nonzero. The existing legacy highlight
control applies 0..2 EV (1..4x) to this native radiance. It does not multiply E,
which retains its own visible/GI scale. This display expansion does not change
GI energy, consistent with the existing legacy highlight control. Native
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

Native source radiance is `max(max(rawRGB,0), decode(rawRGB)) * intensity`.
GI receives this native RGB source plus E through the same directional transport.
The native field and twilight luminance field are projected together in one
ray traversal (six channels), then combined into the ordinary 32-sector RGB
snapshot. No separate surface tint or assumed subtraction from native ambient
is introduced. The HDR legacy EV control remains a display expansion; the
AuroraStrength multiplier itself scales the visible source and GI together.

The legacy shader encodes decode(N) * intensity + E for the SDR target. During HDR the
native SDR draw applies the common intensity without E, and the dedicated replay writes
`max(max(rawRGB,0), decode(rawRGB)) * intensity * exp2(legacyEV) - min(decode(saturate(rawRGB)) * intensity, 1) + E` into the extra-emission
target. Native alpha/blend applies once. The complete native sky is decoded
once at composition; E is never counted in both the SDR baseline and HDR replay.
The SM2 transfer uses a TEXCOORD linker input so the compiler cannot optimize
away RGB headroom by assuming COLOR is already bounded to [0,1]. There is no
new brightness slider: the existing `HDRLegacyExpansion` controls native emission
independently of the orbital gate; AuroraStrength scales the whole source.

- Exact map `Northrend`, valid celestial context, evening half of the game day,
  both native celestial elevations below the geometric horizon.
- Smoothstep fade from sun elevation 0 to -4 degrees and moon -4 to 0 degrees.
  Camera direction and a moon hidden by scenery never trigger activation.
- Common gain = 4 * AuroraStrength / 100 * orbital weight, bounded by the
  setting's 0..200 range. The 4x gain is an initial calibration, to be reviewed
  with a real native frame; it multiplies native emission rather than ambient.
- Green-to-purple phase follows the game-day interval 0.85..0.925 with
  smoothstep. Native animation clock remains unchanged.
- Daylight, dawn and moonlit night have no extra twilight E; native emission
  and GI remain active while native ribbons exist. At AuroraStrength=0 the
  visible aurora and both GI source fields are off. Another map or invalid
  native context retains the game material without a guessed GI source.

### Directional transport and caches

Project the native and twilight emission into 32 world-oriented, equal-solid-angle sectors:
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
controls all visible source emission and its GI; GI brightness/strength remain artistic
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

## Native HDR follow-up from the third client capture

The two captures at renderer frames 37385/37740 use source `5602770c...`,
Effects enabled, and native HDR sky admission. Native aurora E is zero because
the scene is outside the sunset/moonrise interval. `HDRLegacyExpansion=0.50`
is present in the renderer INI but was never passed to the aurora transfer.
Below-one native pixels produced zero HDR excess in that build. Complete-sky
preservation alone therefore did not satisfy native emitter HDR brightness.

The follow-up adds the native emissive calibration above and passes the live
legacy EV through AuroraEmission.w. The legacy SDR material and controlled E
formula are unchanged. Debug AURORA lines now record `nativeHdr=1 legacyEV=...`.
GPU checks cover 0/0.5/2 EV, zero twilight E, source alpha, colour, authored
overshoot and state restoration; scene replays use curated native triangles
and the captured coarse mip. They verify HDR values and parameter response,
not full-resolution client appearance or the live VB contents.

## Whole-aurora control and Weather grouping

The user explicitly chose whole-source control after reviewing the third
capture: AuroraStrength now scales native emission as well as twilight E.
Native GI no longer depends on the twilight gate. A 0/50/100/200% change
publishes no source / half / normal / double native radiance, and visible
SDR/HDR emission follows the same multiplier. The native SDR baseline is
scaled before complete-sky decode, so setting 0 also removes its old baseline
rather than leaving an untunable ribbon under an empty HDR residual.

The two runtime shader control registers are reserved contiguously and restored
together. Both native source RGB and the twilight basis are integrated in one
worker traversal; GI transport storage remains the same 32-sector RGB sidecar
inside the existing 4 MiB cache budget. Native observations stay bounded to 8 Hz.
The slider moved to Weather without coupling aurora to the enhanced rain/snow
master switch. Resident GI probes are still required to show its surface light.
