# Local Forever atmosphere import

The renderer can load `forever-atmosphere.bin` next to its DLL. This is an optional,
local data import; the repository and release packages do not contain Blizzard's
DB2 data. Removing the file restores the regional Northlight atmosphere after restart.

The source installation is Forever 1.60.1.70888. Its reused table schema identifies
itself as `WOWSTATIC_1_60_1_70399`. CascLib 3.0 extracted the local CASC files without
changing the installation. Table layouts were checked against
[WoWDBDefs](https://github.com/wowdev/WoWDBDefs); the WDC5 metadata format is documented
by [DBCD's reader](https://github.com/wowdev/DBCD/blob/master/DBCD.IO/Readers/WDC5Reader.cs).
The volume field interpretations are inferred from the public
[CoA fog converter and model](https://github.com/ghbset/coa-vfog), not confirmed by
executing or disassembling Forever's renderer. No implementation code was copied.
The subsequent [local shader investigation](../../docs/forever-fog-renderer-investigation.md)
examines the volume-grid renderer separately; it does not establish live update
frequency or turn the original import into an implementation port.

Observed client settings: `volumeFogLevel=4`, `sunShafts=2`, `weatherDensity=3`,
`graphicsLightMode=3`, `graphicsShadowQuality=5`, `shadowSoft=1`,
`shadowBlendCascades=1`, `shadowNumCascades=4`, `shadowTextureSize=2048`,
`graphicsComputeEffects=4`, `graphicsBloomUserMult=1`. These are quality selectors;
they are not physical density multipliers. Northlight retains its Quality preset,
weather detection, shadow renderer and HDR output configuration. Its existing
`weatherDensity=3` already matches. The imported fog uses 48 integration intervals.

The local NFA1 contains 216 map-0/map-1 light placements, 2374 time keys and 17 zone
outlines. Full `LightData`, `LightDataGlobalVolumeFog`, `Weather`, and
`ZoneLightPoint` tables were readable. The final encrypted sections of `Light`
(15 records) and `ZoneLight` (one record) were unavailable; only complete readable
sections were used. Their coverage is unknown. Source and output SHA256 values
are recorded in the adjacent import JSON.

Runtime behavior:

- Original light spheres and polygon outlines select profiles in Azeroth and
  Kalimdor. Profiles interpolate over the original 2880 half-minute daily cycle,
  including midnight. Map changes and teleports reset transition history.
- Native rain/snow intensity and the existing weather transition select between
  clear slot 0 and storm slot 2. Server weather timing is not synthesized.
- Three layers retain diffuse/emissive/shadow colours, density, start distance,
  upper/lower height decay, relative-height flags, anisotropy, intensity,
  distance strength/exponent and two noise octaves. The imported medium replaces
  Northlight's additional regional fog/cloud banks/horizon haze; these are not
  accumulated on top of it. Surface lighting and native sky assets remain owned
  by their existing paths.
- Existing static and actor shadow maps cut the shafts. Shadow transition variants
  use the same imported layers. Lamp and spell extinction use the imported density;
  their CPU estimate uses the mean medium, without sampling the noise texture.
- Missing/invalid imports and unsupported maps retain the existing atmosphere.

Rendering adaptations, not a claim of pixel identity: Northlight uses its own
noise volume, geometry, sun trajectory, HDR/tone mapping, terrain coverage and
precipitation rendering. Fog distance is bounded by the current client far clip
and 4000 yards; Forever's 10000-yard scene distance is not forced into 3.3.5.
Unknown volume fields are preserved in the data but not assigned invented meanings.
Weather.db2 particle/sound/texture IDs and LightParams sky models are recorded in
the extraction, not installed into the older client's incompatible asset systems.
This change imports atmosphere parameters; it does not port the complete Forever
weather, sky, cloud, particle, or surface-light renderer.

Rebuild the import from the local extraction directory (it contains `tables/` and
the matching `.dbd` definitions):

```text
python scripts/import_forever_atmosphere.py --source <extraction> --output <client>/forever-atmosphere.bin
python tests/test_forever_atmosphere.py <client>/forever-atmosphere.bin
```

Validation: all imported time-key density endpoints and the 18:04 Tirisfal /
18:12 Hillsbrad interpolants; weather blends, cyclic time, height units and
invalid/missing inputs; compiled production shaders and shadow-transition shaders;
1632 hidden D3D9/DXVK oracle cases (including three-layer transport, static shadow
occlusion, height, day/night and transitions), maximum absolute error below 0.00002.
The GPU stand ran only after its process guard confirmed WoW was absent.
In-game screenshot parity and frame-time cost have not been measured.
