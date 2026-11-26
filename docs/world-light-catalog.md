# World lighting catalog for testing

[Download the catalog of decisions for all 2,555 models](world-light-catalog.tsv). The table is a UTF-8 TSV. Every row is marked **"Not approved"**. The Implementation column identifies the 93 models with changes in this branch. For the remaining models, each row records a decision based on an in-game capture and includes proposed parameters where the evidence supports them.

## In-game checks

The approval marker records review status. The applied profiles and renderer
fixes are active for testing with this build. Rebuild any world-light cache
generated from an earlier PR preview before testing the Drak'Tharon bonfire.

| Effect | Models |
|---|---:|
| Direct contribution of the selected light | 386 |
| Shadows | 233 |
| Surface response to GI | 755 |
| Local light scattering in fog | 7 |

There are 918 direct-light checks and 35 point-shadow checks still open for applicable profiles. The GI count reflects a surface response to toggling GI. Fog scattering was counted only when fog rendered and the selected light was tested independently.

## Where to start testing

- In Utgarde, check small rugs, shields, banners, and crates in dark rooms. The baked tint was reduced on two small-rug placements.
- In Blade's Edge Mountains, check the masonry and ground around the ancient draenei lamppost. Pink light should reach the stone to the side, with a shadow beneath the housing.
- In Hyjal, check the `classicalelfruin10` ruins and the wooded slope below. Two lights positioned more than 560 yards from the ruins were removed from this placement.
- If you encounter `giantlantern01` at a normal world placement, check for blown-out glass or a halo. The halo persisted on the test stand when the point light, GI, and particle emitter were each disabled.

Include the map, a nearby landmark or coordinates, and the time of day in your report. If possible, attach two screenshots from the same camera angle. After teleporting, allow GI and other effects to finish loading before judging the result. Describe what changed in the image, such as the brightness of the object itself, illumination on a nearby surface, the shadow, or the fog.

## Comparison with Lumenveil

Lumenveil generates `lampdata.bin` from the game archives. Comparing its extraction rules surfaced four additional candidates. Their existing lighting settings are retained pending isolated tests. [Lumenveil extractor](https://github.com/lumenveil/Lumenveil/blob/main/engine/tools/lamp_extract/main.cpp) · [M2 rules](https://github.com/lumenveil/Lumenveil/blob/main/engine/tools/lamp_extract/m2_lights.cpp)

| Model | Testing decision |
|---|---|
| `caveminelantern02` | Warm light already reaches the ground and boards in Shadowmoon Valley. Isolate the nearest light's contribution before changing the fixture. |
| `ancient_drainei_lamppost_ruined` | In Netherstorm, test for a short-range, faint pink contribution near the crystal. The base should block light beneath it. |
| `wt_lantern01` | In Borean Tundra, capture a clear view of the lantern and the snow next to it. |
| `wt_lanternhanging01` | In Dragonblight, capture a clear view of the lantern, its bracket, and the adjacent wall. |
