# Wintergarde layered-wall regression, 16 November 2026

QA supplied `northlight-diagnostics.INSIDE.INN.zip`, made with .9 source identity
`77ace228c4339028415613c5c90e9a4d9794865db7cb2250834fe68ef9c2b98e`
(07:37:22 UTC). Its SHA-256 is
`2576174bacb3d4ef72f37e664f53a56c034fba1a2e776239791148ea3ff585d3`.
The compact ZIP is 88,273,053 bytes; restoring its references yields 4,516
logical files, 808,444,358 bytes and 686 distinct contents. The lighting/native
journals are complete. The separate weather capture reports no active weather.

## Black walls

The native scene has intact, lit walls. The black patches first appear during
Northlight composition. Native trace draws 147/148 and 149/150 are paired WMO
layers: SRCALPHA/ZERO followed by INVSRCALPHA/ONE, both with depth writes.
Their shared vertex shader has SHA-256 prefix `d42f571cae501714`.

The first layer receives the outdoor bank: c10 RGB
`(0.376471, 0.533333, 0.454902)` and c11 `(0, 0.666667, 0.768627)`.
The second receives the room bank: c10 `(0.439216, 0.419608, 0.474510)`
and zero c11. The .8 room fix admitted a blended receiver only when its
destination factor was ZERO. The second layer therefore lost the room mask;
subtracting outdoor lighting erased its authored contribution.

The room mask now also admits depth-writing WMO layers with ADD and
RGB-independent alpha/ONE/ZERO factors. It retains the live shader-equation and
room-bank proof, RGB writes, world depth and full viewport checks. It writes
the existing stencil bit during the original draw. It adds no geometry replay,
shadow resource or shader variant. The added blended-layer path excludes
outdoor banks, non-depth-writing layers, RGB-dependent factors and subtract
operations.

`test_prelit_receiver_gpu.py` drives the production `prepareDrawImpl` and
`nameMaskDraw`, including the exact captured vertex shader when supplied through
`NORTHLIGHT_CAPTURE_VERTEX`. Before the fix, the second layer yields mask kind 0;
afterward, both layers yield authored mask kind 5. Negative controls cover
subtract, missing depth/RGB writes, source-color blending and the outdoor bank.
The fixture also checks scoped stencil restoration and SDR/HDR composition.

## Local shadows and timing

All six selected cube maps are complete and usable. Their 36 faces before
native scene rendering and after refresh are byte-identical in this frame.
The captured point passes replay on D3D9, DXVK 2 and DXVK 3 with clear and
fully blocked visibility controls. Five sources illuminate visible receivers;
the sixth has no screen contribution. Across the first five, roughly 527,000
half-resolution source/pixel samples are fully occluded. Negative correction
is bounded by the fully blocked control. These checks do not reproduce moving
casters or establish that every lighting issue is resolved in gameplay.

Paired ordinary/authored composition of the supplied 2560x1440 buffers takes:

| Backend | Ordinary GPU ms | Authored GPU ms |
|---|---:|---:|
| D3D9 | 0.12149 | 0.12093 |
| DXVK 2 | 0.14645 | 0.14544 |
| DXVK 3 | 0.13302 | 0.13542 |

The harness uses interleaved ABBA blocks with separate A/A controls. These
measure the existing compositor paths, not draw admission, game frame time or
the QA machine. No game was running during measurement. The ordinary replay
matches the captured SDR intermediate to mean absolute error 0.001602.

Private evidence is under `.local/out/qa-inn-20261116/`: `stages.png`,
`draw-overlap.json`, `layer-before/`, `layer-after/` and `replay/validation.json`.
The export lacks VB/IB geometry and stencil, so the paired buffer replay is not
a reconstructed gameplay screenshot.

## Subsequent Stormwind shop capture

QA then supplied `northlight-diagnostics.STORMWIND.SHOP.zip` while reporting that
disabling original game shadows had no effect. Its SHA-256 is
`5afccf44e4dd1f2699e39e99237d292fae8fb8f8e425978f2185a0cdf6b37ef1`.
The 124,732,211-byte archive restores to 3,679 logical files, 771,217,955 bytes
and 721 distinct contents.

Production shader classification identifies 65 WMO, 250 model, six other and
five fixed-function draws in its 326-draw native trace. There are no terrain
draws. Draws 1/2, for example, use the same `d42f571cae501714` vertex program
and depth-writing layer pair as the inn. The second layer's room bank is
c10 RGB `(0.129412, 0.129412, 0.129412)`, c11 zero; it also misses the old mask
gate. The native scene is lit, while the final image darkens the doorway and
ceiling. These observations support the room-layer diagnosis, but do not prove
the cause of every shadow reported by QA.

The supplied cube maps and constants replay all 23 submitted local-shadow
passes with clear/blocked controls on D3D9, DXVK 2 and DXVK 3. Usable cube slots
are sparse (0, 1, 2, 3, 5, ... 35); the replay tool now uses the journal's actual
slot IDs instead of assuming consecutive slots. The ordinary composite matches
the captured SDR intermediate to mean absolute error 0.001603. This is another
buffer replay, not a geometry replay or frame-time measurement.

The capture has no setting snapshot or toggle comparison. Its main-view trace
cannot establish whether the offscreen native directional producer was disabled.
The original-terrain control affects terrain receivers; native round unit decals
have their own `BlobShadows` control, and WMO authored lighting is separate.
Subsequent QA video screenshots identify `Disable original terrain shadows`
and `Directional shadow renderer = Northlight`. At 0:05, however, QA first
turns `All Northlight effects` off. The terrain toggle is then off at 0:06 and
on at 0:08 while that master override remains off (two pending changes become
one). This explains the unchanged image: `planTerrainShadowSwap` requires
`enabled`, and the control's tooltip explicitly describes that master-off
restores original game shading. The checkbox records a preference, not proof
that the replacement pass ran. This comparison does not establish overlapping
native and Northlight shadows. An isolated comparison needs Effects on and
Northlight selected; World shadows may be off to isolate the native terrain
layer. No broader shadow suppression was added on the basis of these images.

Private evidence is under `.local/out/qa-stormwind-20261116/`: `stages.png`,
`draw-classes.json`, `shader_classes.txt` and `replay-sparse/validation.json`.
