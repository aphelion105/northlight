# Northrend aurora illumination and GI response

Status: implemented and validated on Windows. Base: `6b87cafa`.
Evidence: `aurora-gi-validation-20261111.json`; integration notes:
`aurora-gi-handoff-20261111.md`.

## Intended result

Northrend receives a subtle green/purple indirect fill during the evening gap
after sunset and before moonrise. Objects and terrain receive stronger,
geometry-aware GI by default. Players can independently tune GI influence and
make the measured indirect light darker or brighter.

## Controls and compatibility

| Quality key | Range | Default, all presets | Meaning |
| --- | --- | --- | --- |
| `GIStrength` | 0..240 | 120 | Influence of measured GI versus native ambient. 60 retains the old 0.5 blend, 120 gives full replacement, 240 doubles the correction. 0 suspends probe work. |
| `GIBrightness` | -100..200 | 0 | Signed percentage adjustment to measured indirect radiance: -100 = zero, -50 = half, 0 = unchanged, +100 = double, +200 = triple. |
| `AuroraStrength` | 0..200 | 100 | Scale of the tuned Northrend aurora sky illumination. 0 disables it. |

All three controls are live, appear under Lighting & AO / Global illumination,
and support preview, save, reset and INI parsing. Existing explicit `GIStrength`
values keep their numeric response. Only defaults become stronger. Brightness
changes neither rays, bounce count, probe range, capture nor solver work.

Signed integer support belongs in the quality key registry; existing unsigned
keys must continue rejecting negative values, overflow and trailing junk.

## Aurora source

- Exact map `Northrend`, valid renderer celestial context, evening half of the
  game day, both native celestial elevations below the geometric horizon.
  Camera direction and a moon hidden behind scenery do not trigger the effect.
- Smoothstep ramps from zero at sunset to full strength at sun elevation -4°,
  and back to zero as the moon climbs from -4° to the horizon. Dawn, daytime,
  moonlit night, other maps and invalid context contribute exactly zero.
- A slow green-to-purple palette follows game time through the evening gap.
  No wall-clock animation, camera-space pattern or extra sky artwork.
- Add at most 20% of native ambient luminance per RGB channel at strength 100
  to the GI sky source. Preserve black ambient. The existing upper-hemisphere
  transport gives upward ground the strongest response, weaker side response,
  real roof occlusion and reflected colour. Prelit WMO surfaces retain the
  existing native-light exclusion; sky, water, UI and direct lamps are untouched.
- Reuse the existing colour-response probe cache and publication fade. Switching
  aurora on/off forces a lighting request so a sub-threshold residual cannot
  remain. The effect follows GI availability and GI/F8/Effects switches; it
  requires resident probes and does not fabricate lighting outside their area.
  Fog can be disabled independently.

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

No additional captures, ray counts, render targets, fullscreen passes or probe
texture reads. Aurora recolours the existing sky transport. Brightness and
strength are shader arithmetic and do not invalidate the solver.

Required checks: full-day orbital gates/continuity and invalid inputs; open sky
versus sealed roof transport and cache relight; signed parsing/range/presets;
live settings and solver invalidation; GPU GI equations including missing and
partial probes, strong positive/negative adjustments, normals and black input;
SDR/HDR composition safeguards; merged/separate GI equivalence; shader SM3 and
freshness gates; production DLL build and existing package verification.

Record measured validation and limitations in the accompanying handoff. Real
in-game artistic approval is separate from synthetic/native GPU correctness.
