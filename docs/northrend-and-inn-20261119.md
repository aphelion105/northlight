# Northrend frame cost and indoor lamp shadows

The Northrend log and the inn QA capture both use 0.3.218.14. The frame-time
comparison uses focused, in-world intervals within the Northrend run. Each
average weights an interval by the number of frames it contains.

| Effects before toggle | Effects off | Effects re-enabled |
| --- | --- | --- |
| 30.32 ms (98 frames) | 14.86 ms (364) | 28.26 ms (380) |
| 27.54 ms (399 frames) | 13.89 ms (531) | 27.85 ms (122) |

Across ten profiled frames with the normal effect set, the median GPU
Northlight time was 6.46 ms, including 2.57 ms for point shadows. CPU capture
was 6.38 ms. Point lights rebuilt a median of 39 static faces, submitted
3,649 replay draws and processed 5.29 million caster triangles. Disabling
actor shadows alone gave 23.57 ms per frame. A later AutoBump toggle with
several other effects already disabled changed 23.62 ms to 16.17 ms.
These toggles measure different configurations.

## Point-face update

The static shadow shader already emits identical depth to two colour outputs.
When a working cube face and its static cache can be bound together, both now
receive the same rasterization. Dynamic geometry writes only to the working
face. Cache reuse, single-target hardware and failed binds retain their
previous paths. A mixed terrain/static fixture compared all six output faces
bit for bit and exercised failures binding the second target and copying the
cache. The rebuilt occupied face went from one copy to zero on D3D9 and DXVK.
No game frame-time measurement for the revised DLL is available yet.

## Tavern capture

The submitted frame had five selected point lights with six faces each.
Replaying the point-light passes with the captured cubes and with fully lit
and fully blocked controls showed that nearly all affected samples were
receiving the blocked result. The three lower hanging lanterns used a native
M2 source at world height 212.06, below the floor. The replacement model puts
its visible flame near 218.96. The two upstairs lamps had the same displacement.

The cache builder now uses a reviewed origin for the exact model and skin
pair. Shared scene geometry, placed-light records and native attachment
proofs use the same origin. The original native anchor still proves ownership.
The source keeps its ID, RGB, range and shadow intent. Rebuild the scene and
light cache when deploying this change to another installation of this pack.

A geometric check traced the three lower lamps to 2,160 floor samples within
their range, using the tavern's placed opaque geometry and each lamp's own
source-shell exclusion. The original lamps reached zero samples. The corrected
lamps reached 109 of 603, 205 of 858 and 188 of 699 respectively. This is a
static-geometry check. Actors and the final PCSS filter require an in-game frame.

The same capture showed a second defect. Probe occlusion could erase both
ambient and direct room lighting from a posed model. The compositor now
limits removal to the ambient fraction registered during the native draw.
A replay using the captured inputs and the model's room grade restored two
cloak samples from RGB (0, 0, 0) to (23, 20, 16) and (47, 36, 3) on both
backends. The original native pixels were (36, 30, 24) and (73, 56, 5).
The archive did not retain the final stencil surface, so the replay applied
the recorded room bank's grade explicitly.
