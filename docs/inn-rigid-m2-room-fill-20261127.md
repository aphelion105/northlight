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
