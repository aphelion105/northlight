# Aurora / GI integration handoff

User request: Northrend green/purple ground illumination between sunset and
moonrise; stronger GI on objects; live intensity adjustment in both directions.

Branch: `codex/aurora-gi-20261111`, based on `6b87cafa`.
Worktree: `D:/dev/Projects/Northlight/.local/aurora-gi-20261111`.
Destination chat: `01a1c14a-bb92-79d3-adfd-adeb63952299` (local).

The specification is implemented. Changes remain isolated from the destination
chat's active stutter optimizations. No game files were installed and nothing
was pushed or published.

## Behaviour

- `GIStrength`: 0..240, default 120 in every preset. 60 preserves the previous
  half-strength response; 120 fully replaces native ambient with probe light.
- `GIBrightness`: -100..200, default 0. Signed adjustment to measured indirect
  radiance, applied in the shader without rescheduling the GI solver. -50 halves
  it, +100 doubles it. Strong removal is bounded before residency fading.
- `AuroraStrength`: 0..200, default 100. A subtle native-clock green-to-purple
  source enters existing sky transport only in Northrend's evening celestial
  gap. Roofs occlude it through real GI paths. No extra GPU pass, target or probe
  texture sample. It follows GI/F8/Effects, requires resident probes, and works
  independently of atmospheric fog. Missing probes preserve native lighting.
- All controls have F11 labels/help, preview/save/reset, dependencies and INI
  documentation. Explicit old `GIStrength` overrides are preserved; users with
  an override must select 120 themselves to get the stronger default.

## Integration points

1. Cherry-pick the handoff commit onto the integration branch. Preserve the
   destination's newer optimization code when resolving overlapping files.
2. `world_renderer.h`: apply the new sky source to `Request.light.skyRadiance`,
   carry `auroraActive` for exact activation/deactivation request boundaries,
   and upload signed brightness to c66.y in the initial 68-register bank.
   c66.y belongs to GI until the later volume passes replace c66 with
   `FogActorBounds` / `LocalLightFog`. Diagnostic captures retain 68 registers.
3. `world_effects.hlsl`: retain the new `worldGIValue` math in both standalone
   and merged paths. Keep the destination's newer lighting/filter/composition
   optimizations. Prelit WMO, sky/water/name exclusions and the relative 45%
   surface-darkening limit remain unchanged.
4. Quality keys now use `Key::get/set` so the shared registry can carry the
   signed brightness field without changing existing unsigned setting types.
   This includes live copies, INI/schema, equality/presets and diagnostics.
   `AuroraStrength` changes invalidate the lighting request; brightness and
   nonzero influence changes do not. Zero-strength transitions still do.
5. Regenerate affected shaders from the **combined** source. Do not replace
   newer destination bytecode or manifests wholesale with this branch's copies.
   Rebuild `WorldGI`, `WorldLightingUnshadowedGI`, any other destination-modified
   entries and transition shaders; retain all normal freshness/SM3 gates.
6. Build with `python scripts/build_renderer.py`, then run the tests below and
   the destination's own performance checks. Version/PR/deployment are owned by
   the destination chat. This package is a local verification artifact.

## Validation

Detailed logs, source fingerprint and artifact SHA-256 are recorded in
`aurora-gi-validation-20261111.json`.

| Check | Result |
| --- | --- |
| Quality parser/presets, O2 and debug | Pass; signed boundaries, malformed/overflow inputs, legacy scale |
| Aurora orbit and production transport | Pass; 86,400 seconds, 6,430 active evening seconds, both palettes, sealed roof, cached/fresh relight equivalence |
| Settings service | Pass; signed persistence/reset, validation and live updates |
| Live settings | Pass; 1,000 cases |
| GI dependencies, O2/debug | Pass; 116 keys, 76 profile cases, 1,000 no-op/HDR cases |
| Settings overlay | Pass; signed draft and GI/strength dependencies |
| Native GPU GI response | Pass; 360 tuning cases, max FP16 error 0.001563; 769 fallback/baseline cases; strong settings leave sky/water unchanged |
| Native GPU SDR/HDR chain | Pass; 2,160 cases, max error 0.000782; black, AO and direct shadow controls |
| Separate/merged GI | Pass; 16 cases each on native D3D9, DXVK 2.7.1 and DXVK 3.1.1; max difference 0.0004883; baseline/alpha exact |
| Actual WorldRenderer GPU pipeline | Pass; MSAA 0/2, HDR/SDR, prelit/unlit/name safeguards and restored D3D state |
| Production DLL | Pass with Zig 0.15.2, normal compiler/source freshness checks |
| Windows installer package | Built and passed `tests/verify_packages.py` |

Shader cost: WorldGI 233 → 238 static slots, 19 → 20 temporaries;
merged unshadowed GI 387 → 393 slots, 23 temporaries unchanged. Both are within
SM3 gates. These figures are static costs, not measured game FPS.

Reproduce with the existing test scripts:
`test_quality_settings.py`, `test_aurora_illumination.py`,
`test_settings_service.py`, `test_live_settings.py`,
`test_gi_settings_dependencies.py`, `test_settings_overlay.py`,
`test_gi_fallback_gpu.py`, `test_world_lighting_gi_gpu.py` and
`test_world_screen_effects_gpu.py`. The merged-pass runner needs a fresh
`--output` directory. Windows builds/native/GPU/package checks must run outside
the restricted execution sandbox as required by AGENTS.md.

Local artifacts: `renderer/frd9.dll` and
`out/packages/Northlight-0.3.217-aurora-gi-20261111-Windows.zip`.

The game was not launched or modified. Final artistic review in an actual
Northrend evening and gameplay performance measurement remain unclaimed.
