# Settings overlap audit and cleanup

Status: implemented after user approval. Performance contains monitoring;
rendering parameters live in the corresponding effect's Fine tuning controls.

## Duplicate rows removed

The 114 Quality keys have distinct storage fields. `ActorShadowCpuMs` and
`ActorShadowBudgetMiB` previously appeared in both Shadows and Performance.
They now appear once, in Shadows / Character shadows / Fine tuning, alongside
capture-memory and minimum-character-triangle controls. Existing keys and
saved values are retained. Overview's master toggles remain intentional shortcuts.

Command-stream, replay and GPU-budget controls moved from Performance into
Renderer & HDR / Rendering / Fine tuning. Diagnostics keeps capture actions
and logging. Performance contains live metrics and memory snapshots, plus
session-only HUD visibility and a statistics reset button.

## Two explicit local-light budgets

Lighting & AO / Local lights contains both controls:

- **Total local lights (0 = off)**: the source selection target, also used by fog.
- **Of these, shadowed lights**: the eligible shadowed subset. The effective
  map budget is `min(LocalLightLimit, PointShadowLimit)` everywhere, including
  allocation, warm-up and spare retention. It never increases the total.

Outgoing lights can briefly remain while the existing fade completes; this
preserves smooth transitions. Shadow maps still respect the effective subset.

Previously, `NorthlightGpuBudget::lightLimit` used the larger of the two
limits. The old preset pairs 48/64, 32/64 and 24/64 all selected up to 64 when
local shadows were active. The helper is removed. New preset totals are 64,
matching that previous effective selection rather than reducing the picture.

`LocalLightLimitsVersion=2` is migration metadata, not another exposed control.
On the first load of an old file, active-shadow totals are raised once to their
previous effective value: 48/64 becomes 64/64, 128/16 stays 128/16, and a zero
total stays zero. With world/local shadows disabled, the old total is retained.
The transaction preserves comments, unknown settings and document encoding.
After migration, lowering the total remains effective on subsequent loads.
If the write fails, the runtime loader still preserves the old effective value.

The shadow toggle and distance remain independent. Update controls are now
called **Shadow update interval (ms)** and **Faces updated per light**.

Validation: settings-service migration/edit/idempotence cases, quality parser,
source/shadow selection bounds, source wiring and native UI rendering.

## Similar names with different purposes

| Controls | Difference | Recommendation |
| --- | --- | --- |
| PointShadowRefreshMs / PointShadowFacesPerFrame | Minimum update interval versus cube directions updated per eligible light | Keep separate; say "faces per light", not a global frame budget |
| CaptureBudgetMiB / ActorShadowBudgetMiB | Overall captured animated geometry versus the character-shadow selection subset | Keep separate, clarify scope |
| GpuBudgetMs / ActorShadowCpuMs | GPU quality controller versus CPU-driven character-shadow radius controller | Keep separate, do not call both a general frame-time target |
| ActorShadowRadius / PointShadowDistance / terrain_radius | Camera-to-character reach, camera-to-light influence sphere, regional terrain caster reach | Keep separate, name the object being limited |
| AutoDisplacement* / AutoModelDisplacement* | Terrain/world objects versus model geometry, with intentionally different relief and subdivision | Keep separate |
| AutoBumpDistance / AutoParallaxDistance / AutoMaterialOcclusionDistance | Normal detail, POM relief and near cavity/self-shadow shading reach | Keep separate |
| WorldFog / FogClouds / RainFog / HorizonHaze | Different atmospheric layers and weather response | Keep separate |
| HDRBloomStrength / celestial glow_strength | HDR image bloom versus an individual celestial source's atmospheric glow | Keep separate |
| GI / ContactAO / AutoCavity | Probe-based indirect light, scene-depth occlusion and texture-derived cavities | Keep separate |
| BlobShadows / DisableOriginalTerrainShadows / WorldShadows | Original character decals, original terrain shadow term and Northlight shadows | Keep separate |

Master toggles and intensity controls are not duplicate settings: turning an
effect off can skip its processing while preserving the user's chosen strength.
Quick toggles on Overview intentionally point to the same values as their
detailed pages; they are shortcuts rather than independent controls.
