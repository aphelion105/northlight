# Inn props darker than the walls, 27 November 2026

The supplied Northrend inn capture (renderer frame 2949, 2560×1440) showed
bright walls and very dark props. Compared with the native scene image, the
WMO walls, floor, ceiling and counter kept their brightness, apart from contact
AO. The painting, shelf, chandelier, trophy heads and barrels fell to about a
quarter of native linear luminance.

| Receivers | Native linear luma (median) | Final | Ratio |
| --- | ---: | ---: | ---: |
| WMO room surfaces | 0.0515 | 0.0504 | 1.00 |
| M2 props | 0.0255 | 0.0070 | 0.26 |

## Cause

Solving `1 - final / (native × AO)` per pixel gives three groups. WMO surfaces
have no removal. The M2 props group near 0.8 (`RoomSharedTint`) and 0.6 (the
two-thirds ambient level). Those M2 room levels let measured probe occlusion
attenuate the native room bank. The captured `prelit-daylight` alpha was 1.0
across the frame and the lighting buffer equalled `-baseline`, so every level
reached its full cap.

The probes measure darkness because the solve only receives sky, sun and fitted
lamp positions. Valid interior probes near the camera had SH DC values of 0 to
0.44, against an outdoor median of about 1.1. The floor-level probe layer near
the camera was flagged inside geometry. The authored room ambient,
(0.957, 0.871, 0.816), lights the room in the native frame, but no probe sees it.
GI strength, brightness and prelit mix were at their defaults.

Authored WMO room banks were already exempt for this reason. See
[effect continuity and room fill](effect-continuity-and-room-fill-20261116.md).

## Change

Only skinned M2s request a probe-attenuated room level. This keeps the
[Wintergarde player](wintergarde-player-shadow-20261118.md) behaviour for posed
characters. Rigid doodads now mark the plain authored room stencil, as the WMO
surfaces around them do, and keep their native room fill. Rigid doodad programs
also pass the WMO coordinate proof, so they keep the WMO `INVSRCALPHA/ONE` layer
admission.

`tests/test_unlit_receiver.py` checks the gate.
`tests/test_prelit_receiver_gpu.py` runs the production draw admission on D3D9,
DXVK 2 and DXVK with a lit rigid client program (vs_3_0 `Diffuse_T1` #1,
`0x9a566cccdd77ebf7`) and the tower player program. Without the change, the
rigid program received `RoomSharedTint`.

No in-game frame with the revised DLL has been captured yet.

## Lanterns lit from under the floor

The same capture explains why the interior probes read black. Depth gives the
floor at z 18.68 and the lower ceiling at z 27.46. Two visible hanging lanterns
have glass at z 22.7 and 22.8. Each has a cached point source with the same x/y
at z 15.71, under the floor. A third source shares that height. In this room
only the z=24 probe layer of the 8-unit grid lies between floor and ceiling;
most of its probes measured zero.

The [tavern fix](northrend-and-inn-20261119.md) pins a reviewed flame origin to
the exact `generalhanginglantern01.m2` model and skin hashes. The tested
installation loads that path from `Data/patch-D.mpq` with model sha256
`858fc61d…` and the pinned skin. Its additive glow batch is centered at
(0.011, 0, 0.386), the reviewed position. Its native light is at local z -5.93.
The model hash did not match, so the builder kept the native origin.

A `native_anchors` entry can now list `variants` keyed by model sha256.
Omitted fields reuse the pin, including the skin hash, so the skin is still
checked. This installed model is listed as a variant.
`tests/test_world_fixture_emission.py` covers variant reuse, overrides and
rejection of unlisted models or changed skins. Both the scene and light
builders resolve this anchor, so the scene cache revision is now 3 and the next
`install_world_cache.py` run rebuilds both.

In a Northrend light build against that client, the variant moves 98 point
sources and nothing else. Each one rises 6.89 local units, times the doodad
scale (0.68 to 1.70).
