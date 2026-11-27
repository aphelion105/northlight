# Inn lanterns lit from under the floor, 27 November 2026

A Northrend inn capture (renderer frame 2949, 2560×1440) showed interior probes
reading black. Depth gives the floor at z 18.68 and the lower ceiling at
z 27.46. Two visible hanging lanterns have glass at z 22.7 and 22.8. Each has a
cached point source with the same x/y at z 15.71, under the floor. A third
source shares that height. In this room only the z=24 probe layer of the 8-unit
grid lies between floor and ceiling; most of its probes measured zero.

## Cause

The [tavern fix](northrend-and-inn-20261119.md) pins a reviewed flame origin to
the exact `generalhanginglantern01.m2` model and skin hashes. The tested
installation loads that path from `Data/patch-D.mpq` with model sha256
`858fc61d…` and the pinned skin. Its additive glow batch is centered at
(0.011, 0, 0.386), the reviewed position. Its native light is at local z -5.93.
The model hash did not match, so the builder kept the native origin.

## Change

A `native_anchors` entry can now list `variants` keyed by model sha256.
Omitted fields reuse the pin, including the skin hash, so the skin is still
checked. This installed model is listed as a variant.
`tests/test_world_fixture_emission.py` covers variant reuse, overrides and
rejection of unlisted models or changed skins. Both the scene and light
builders resolve this anchor, so the scene cache revision is now 4 and the next
`install_world_cache.py` run rebuilds both.

In a Northrend light build against that client, the variant moves 98 point
sources and nothing else. Each one rises 6.89 local units, times the doodad
scale (0.68 to 1.70).
