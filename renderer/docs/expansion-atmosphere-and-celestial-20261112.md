# Expansion atmosphere and selectable celestial placement

Outland and Northrend use 20 authored biome profiles in
`src/sky/expansion_atmosphere.h`. These are **approximations**, not Forever
assets: that client has neither continent. They use the same three-layer volume
integrator as the local Forever import, with terrain-relative heights, shadows,
two-scale noise, cyclic day/night colour and density, and detected weather.

| Region | Treatment |
|---|---|
| Hellfire Peninsula | Warm, dusty air; moderate low dust |
| Nagrand | Clear green air; thin ground mist |
| Terokkar Forest | Cool woodland haze and low mist |
| Shadowmoon Valley | Green ash and fel haze |
| Zangarmarsh | Dense cyan marsh fog and broad low banks |
| Blade's Edge Mountains | Dry, relatively clear highland air |
| Netherstorm | Violet haze with moving fine structure |
| Shattrath | Restrained grey-green city haze |
| Dragonblight | Cold blue-white snow haze |
| Zul'Drak | Dark, cool forest/ruin mist |
| Storm Peaks | Clear mountain air; strong storm response |
| Icecrown | Dense steel-blue haze and snow response |
| Grizzly Hills | Warm forest haze; stronger nocturnal ground mist |
| Howling Fjord | Cool coastal mist |
| Crystalsong Forest | Restrained violet mist |
| Borean Tundra | Pale coastal/tundra haze |
| Sholazar Basin | Humid green air and dense low mist |
| Wintergrasp | Cold ground mist and storm response |
| Dalaran | Thin air, small ground contribution |
| Hrothgar's Landing | Cold sea mist |

The profile rows expose density per 100 yards, ground-layer density, upper
height in yards, falloff per 100 yards, scattering anisotropy, night and storm
multipliers, and noise mixture. Colours are display RGB, converted to linear by
the common integrator. Precipitation still comes from the game; these profiles
do not invent a server weather schedule. They also do not replace sky textures.

FRF parent-zone IDs select profiles. Four adjacent cells blend bilinearly; the
existing atmosphere transition smooths motion. Missing/unknown zones retain
the old atmosphere, including blood elf and draenei areas on map 530.
`regional_fog_builder.py --maps Expansion01 Northrend` now includes indoor WMO
bounds for every profiled zone. Existing cache geometry still controls shelter
and underground exclusion. The built-in profiles work without the optional
`forever-atmosphere.bin` import.

### Restoration audit, 13 November

The 20 profiles, Outland planet/ring geometry and native emission handling are
retained from `0634e962`; they were not removed by resource externalization.
The installed FRF cache contains 800 Expansion01 and 1,131 Northrend tiles,
including all eight authored Outland land-zone profiles and eleven Northrend
terrain profiles. Dalaran's floating city is not a terrain parent in these tiles.
Unknown/ocean regions and the blood elf/draenei starting regions keep their
native fallback instead of inheriting an unrelated biome.

These are artistic projections onto the shared Forever-compatible volume
integrator, not recovered expansion data. The existing choices are consistent
with Blizzard's descriptions of [Dragonblight's arctic landscape and Grizzly
Hills' forest](https://worldofwarcraft.blizzard.com/en-gb/news/23840668),
[Sholazar's tropical anomaly](https://worldofwarcraft.blizzard.com/en-us/news/23846691),
and [Netherstorm's chaotic skies and fel-touched Shadowmoon](https://news.blizzard.com/en-us/article/23625673/world-of-warcraft-burning-crusade-classic-deep-dive).
Those descriptions motivate the palettes; numeric densities and noise are
Northlight tuning, not lore facts or Blizzard renderer parameters.

Expansion profiles previously left direct/ambient colours at zero. After the
shared atmosphere consumers began using those fields, that removed native
environment lighting. Sampling now inherits the client's direct/ambient colours
before temporal blending. The existing skybox textures, planet/ring emission,
aurora emission and material lights keep their respective owners.

Forever 1.60.1.70938 has byte-identical readable atmosphere inputs and orbital
tables to 70888. Rebuilding the current NFA2 import yields SHA-256
`803c56797d9dba03d3afb2aa4db146179bad8be1d9340ecffbdee43a5c90e04e`,
matching the 12 November sky-audit installation receipt. The game directory
had lost this optional file; all three checked 0.3.217/0.3.218 installers omit
it because local extracted DB2 assets are not public package payloads.
The unavailable encrypted table sections remain unavailable. Current CASC
moon texture reads also fail, so no new texture parity is claimed.

The current lunar size/HDR correction is documented in
[celestial placement](../../docs/celestial-placement.md). Azeroth, Kalimdor and
Northrend share identical resolved solar/lunar radii at each time key; Outland
retains the earlier planet scale and emission.

## Celestial source and limits

Source: local Forever **1.60.1.70888**, `Wow.exe` SHA256
`054a166a86c2ab79f49366d319458a77c0653f0410c6add534385796f1dc32a9`.
Both `CelestialBody.db2` (260 bytes) and `MapCelestialBody.db2` (212 bytes) have
zero rows in this installation. The executable's initialized data contains a
contiguous block of day/value float tables. `scripts/import_forever_celestial.py`
checks the whole executable hash before reading them and generates
`src/generated/forever_celestial_tables.h` plus its JSON provenance.

| Table | RVA | Keys |
|---|---|---:|
| Solar polar angle | `0x49067a8` | 7 |
| Primary lunar polar angle | `0x49067e0` | 6 |
| Solar azimuth | `0x4906850` | 3 |
| Primary lunar azimuth | `0x4906868` | 3 |
| Secondary lunar polar angle | `0x4906880` | 5 |
| Secondary lunar azimuth | `0x49068a8` | 3 |
| Solar size multiplier | `0x49068c0` | 4 |
| Lunar size multiplier | `0x49068e0` | 4 |

The bytes and values are observed in the local executable. **Table roles and
linear cyclic evaluation are inferred from the corresponding audited 3.3.5a
DayNight tables**, not proved by executing Forever's protected code. The solar
table reaches a 65-degree elevation at noon, with a 05:30/20:30 low-angle pair;
the moon reaches 55 degrees at midnight and has 05:00/21:00 low-angle keys.
Both primary azimuth tables are 45 degrees. Solar size varies from 1 to 2;
lunar size from 1 to 1.5. Every source float is preserved in the generated file.

The initial installation multiplied the supported client's base billboard
size. A subsequent correction reuses Northlight's earlier tuned base sizes
(`0.506736` for the sun, `0.46176318 × 1.75` for the moon), which remained in
the orbit code after commit `0f55ea9f` switched rendering to native sizes.
See [the current size policy](../../docs/celestial-placement.md).
Forever's absolute base-size constants and texture footprint have not been
independently established: these are imported size curves over Northlight's
existing calibration, **not proven pixel-identical absolute diameters**.
The extracted secondary-moon tables are retained for provenance; Northlight's
existing policy still displays only the primary moon.

## Player control

`Sun & moon` now has **Celestial placement & size: Default / Forever**.
`[Quality] CelestialStyle=Default` (or `0`) preserves native positions and
animated sizes. `Forever` (or `1`) selects the recovered tables. Factory
presets remain Default. The setting previews and saves live, and page Defaults
restores it along with the selected region's palette.

Early discs, late halos, directional lighting, shadows, fog rays, night fill,
shadow prewarming and aurora illumination use the same selected schedule.
Switching invalidates lighting/fog history and the directional cache. Colour
profiles remain independent of the placement selector.

## Validation

- `test_expansion_atmosphere.py`: 17,280 zone/time/weather combinations;
  finite constants, terrain-relative altitude, weather response, biome ordering,
  spatial seams, unit orbit directions, midnight continuity, size calibration,
  preserved native mode, schema/parse/live/save/default paths.
- Existing Forever import, celestial profiles, quality settings (optimized and
  debug), settings service and overlay tests pass.
- The local FRF rebuild reads 800 Outland and 1,131 Northrend tiles and 877 WMO
  roots, producing 4,784 placed indoor groups.
- The production build passes the shader freshness and DLL package gates
  (seven existing compiler warnings). Native celestial context, orbit, skybox,
  early planet-phase recovery, aurora and static-shadow prewarm tests pass.
- Hidden D3D9/DXVK atmospheric oracle: 1,632 cases pass; maximum absolute
  transport error `1.98585932e-05`. This tests the common integrator, not artistic
  matching of the authored biomes.

Installed in `D:/Games/335_engb_mod` with `CelestialStyle=Forever`: production
DLL and 246 changed FRF tiles. Other INIs and the existing imported atmosphere
binary were preserved. All replaced files were backed up under
`.local/out/forever-celestial-70888/backup-20261112-084350`; the adjacent
`installation.json` records before/after hashes. DLL SHA256:
`0ed7409f4d27a8a92cf9ec25ea59d45817bdc7ba769e5bc9e87d6fcb0f009dbb`.
Source identity:
`970aabffda70c98c50fa614e551fe0bc277f8d09851df8d05967d3dce2a2a17a`.

Visual matching inside either game has not been performed. Static parameter
recovery and analytical tests do not establish visual parity with Forever.
