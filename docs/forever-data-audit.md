# Forever data audit and local research archive

Audited 2026-11-13 against the installed Forever 1.60.1.70938. Start here before
re-extracting data or assuming that a Northlight effect has been lost.

## Durable local evidence

The private snapshot is `.local/research/forever-1.60.1.70938/` at the repository
root (on this workstation: `D:/dev/Projects/Northlight/`). It contains 106 files,
8,331,089 bytes, plus `manifest.json` with each file's SHA-256 and byte size.
It is ignored by Git: preserve it when cleaning build outputs. It is a local
archive, not a cloud backup or release payload. The findings in this document
belong in Git; raw extracted tables and artwork stay local. The converted NFA2
can now be explicitly included in a release with the package builder's
`--forever-atmosphere` option; it has its own manifest hash and rollback entry.

| Directory | Contents |
|---|---|
| `features/` | `audit.json`, raw DB2/BLP/pvdata/M2/BLS files, decoded tables, `particle-settings.json`, `grading-analysis.json`, extraction manifests and helper scripts |
| `atmosphere/` | Fresh 70938 atmosphere DB2 tables and matching DBD definitions, comparison with the prior extraction, NFA2 import and provenance |
| `fogdata-comparison/` | Local VFD1/v4 file, decoded contents and comparison with Northlight |

Original working directories remain under `.local/out/test-output/`:
`forever-feature-audit-20261113`, `fogdata-comparison-20261113`, and
`weather-moon-perf-20261113/forever-70938`.
The local extraction helpers expect the existing CascLib at
`.local/out/forever-atmosphere-70888/CascLib.dll`; their paths are workstation
provenance, not portable installer APIs. Production DB2 decoding/import tools
are `scripts/forever_db2.py` and `scripts/import_forever_atmosphere.py`.
The investigation decoder also handles relation columns for WeatherXParticulate,
LightParamsLightShaft and LightWorldShadow; the original generic helper did not.

## File roles and implemented import

- `northlight-assets.bin` is NLA1: compressed Northlight shader bytecode, weather
  artwork and overlay font. Moving these resources out of the DLL preserved bytes.
- `forever-atmosphere.bin` is Northlight's optional NFA2: 216 placements, 2374
  time keys, 17 outlines and 576 outline points for map 0/1. It includes direct
  and ambient colors, eight sky colors and three volume-fog layers per key.
- `fogdata.bin` is another renderer's VFD1 version 4: 625 placements, 508 params,
  2079 keys, 6222 layers, 18 outlines, 533 points and eight grading curves.
  It is not loaded by Northlight and is not a superset of NFA2. Neither that file
  nor the imported Forever placements provides map 530/571 atmosphere.

The fresh NFA2 SHA-256 is
`803c56797d9dba03d3afb2aa4db146179bad8be1d9340ecffbdee43a5c90e04e`.
It matches the prior November 12 import. LightData, LightParams and Weather
DB2 bytes also match the previous 70888 extraction. The game NFA2 file was
missing before restoration; why or when it disappeared is unknown. Three recent
installers contained no NFA2 because this local game-data import was separate.
Do not infer that an installer deleted it.

## Additional data verified in 70938

| Feature | Evidence / current gap |
|---|---|
| Color grading | 307 of the 2374 imported time keys reference nine LUTs, including identity. IDs: 1140733, 8248426, 8248427, 8286663, 8286665..8286669. All extracted BLP2 strips are 1024x32 (32 cubed RGB inputs). Every one reduces exactly to a shared 32-entry RGB curve. The eight VFD curves match byte-for-byte; 8286663 is absent there. NFA2 imports neither curve nor binding. |
| Darker grading | Present elsewhere in LightData, but zero on all imported map-0/1 keys. Extracted 1308655 is a genuinely cross-channel LUT; never assume every LUT is a 1D curve. |
| Glow | 25 of the 218 map-0/1 referenced LightParams have nonzero Glow, up to 1. Northlight's HDR/bloom exists but NFA2 does not import this value. Matching Glow is not proof of matching HDR exposure/tone mapping. |
| Clouds | CloudDensity, CloudEmissiveColor, CloudLayer1AmbientColor and CloudLayer2AmbientColor contain authored map-0/1 values. Only CloudSunColor is currently among NFA2's eight sky colors. |
| Ambient and water | HorizonAmbientColor/GroundAmbientColor and close/far ocean/river colors are populated. These and LightParams water alpha values are omitted by NFA2. Main-continent water alpha values are constant 0.5/1.0 (river), 0.75/1.0 (ocean). |
| Weather and wind | Weather has 79 records; WindSettings has 24. WeatherXParticulate has 59 bindings to 27 distinct assets. All 27 extracted; 19 JSON descriptions decoded, eight newer binary variants not decoded. The JSON descriptions reference 14 texture IDs whose payloads were not extracted in this audit. |
| Particle behavior | Extracted descriptors specify count, size, animation, velocity, wind, wandering, lighting, occlusion and distance/height fades. Duststorm 1990930, blown sand 1990933 and large dust 2151595 are separate layers. Snow 2147858 includes ground particles. Data counts are not a suitable D3D9 performance budget without measurement. |
| Skyboxes | Main-continent profiles reference DeathClouds (3), c60 Global Storm (712, model 8490742) and Dalaran (715, model 8216399). The latter two models were extracted, but their complete texture/model dependencies were not converted. This is not evidence of Northrend coverage. |
| Shafts/lightning | Eight LightShaft records and eight bindings were read; none binds the imported main-continent profiles/keys. Lightning has three records, but all Weather.LightningID values are zero. Presence is not proof of active use. |

Unknown LightParams/LightData fields must remain unknown until their meaning is
proved. Light and ZoneLight have unreadable encrypted sections (15 and one
records respectively), so the readable export is incomplete. Empty CelestialBody
and MapCelestialBody tables do not establish missing celestial renderer code.

## Renderer evidence and next work

See [the shader investigation](forever-fog-renderer-investigation.md) for static
SM5/SM6 volume injection, temporal reprojection, integration and resolve evidence.
Its historical "current Northlight" raymarch description predates the implemented
atlas path; [fog-volume.md](fog-volume.md) describes today's production renderer.
Original bytecode/disassembly evidence is preserved at
`.local/out/forever-fog-architecture-70888/`. Its executable version is 70888,
not the newer 70938 data audit. Available shaders and quality CVars do not prove
exact live pass order, frequency, exposure or selected variants.

Prioritize grading/bloom integration, then cloud/ambient parameters, then adapted
particle/wind rendering. Preserve linear HDR, authored transitions, geometry
occlusion and existing quality. Outland/Northrend require the existing authored
adaptation, not fabricated Forever table coverage. Compare images and component
timings; only a matched game run proves gameplay FPS.

References: [WoWDBDefs](https://github.com/wowdev/WoWDBDefs),
[VFD converter](https://github.com/ghbset/coa-vfog/blob/main/tools/convert_classic_fog.py),
[VFD loader](https://github.com/ghbset/coa-vfog/blob/main/src/fog_data.cpp).
