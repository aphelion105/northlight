# r74: explicit terrain-shadow suppression after the composite

Baseline: `79c88ca5448658aa73154fa94486d429f09a2562` (completed r73 optimizations).
Production source:
`2638d9ce29655ec008628c5843e8887af99662033c108cde9462c09ea22c5ba2`.
Zig 0.15.2 production build: 2026-11-09 21:32:45 UTC.
See the [validation receipt](native-terrain-shadow-pop-20261109-r74-validation.json).

The supplied `2026-11-09_21-08-46.mp4` is 7.5 seconds at 1920×1080/24 fps.
Tree-shadow patterns briefly appear on the snowy terrain as the camera turns.
Its overlay identifies an RTX 3080/7800X3D. The video provides no matching
renderer log, DLL source identity or complete settings, so it cannot establish
the exact draw phase responsible for each affected frame.

The code has a concrete admission defect relevant to this symptom. With
`DisableOriginalTerrainShadows=1`, known terrain shadow consumers are meant
to run with the neutralized native shadow term independently of replacement
map readiness. However, `prepareDrawImpl` returned after the composite without
planning suppression, and the planner independently refused `applied=true`.
Any terrain batch submitted there could therefore render its original shadow.

The late-draw branch now plans this explicit suppression for recognized terrain
programs. Automatic coverage-aware replacement remains pre-composite only.
Disabled/failed/debug paths and UI/unknown programs retain their guards. The
existing native draw wrapper still binds/restores the neutral pixel shader;
no geometry capture, second effect pass or shader-token change is introduced.
Late diagnostic observation shares the same vertex lookup. A certified mirror
binding is borrowed; an uncached lookup is bounded to one getter and released.

Verification executes the actual extracted production late branch, planner
and draw wrapper. Optimized and ASan/UBSan runs pass 12,288 late admission,
UI/unknown, cached/getter, failure, diagnostics and reference-lifetime cases;
8,192 existing visibility/gate cases and 260 draw-wrapper controls also pass.
The native x86 Zig run passes the same cases. The previous source fails the
new late-draw regression, confirming the counterfactual.

The independent driver oracle validates creation of all 392 stock terrain
shadow-consumer patches on native D3D9 and DXVK 2/3. Two non-sampling generators
remain untouched. Four rendered cases/backend compare SDR/HDR neutralized
output against the no-shadow reference and preserve color, fog/specular layers
and alpha exactly. The generated-bytecode/camera suite passes 121,108 cases.

This closes the proven late-terrain path. It does not constitute a replay of
the tester's entire recording; that exact episode still needs a client check.
No claim is made about an unrecognized receiver permutation or other shadow
sources from the video. The separate castle-video task remains deferred.

Final package `0.3.217-test.20261109.8` includes r73 one-pass point PCSS and this
fix. All 17 exports/ordinals and the installer are verified, with byte-identical
DLL payload. No client installation or game launch was performed. Tests beside
the other WoW are correctness checks; no game-FPS or isolated GPU-time claim.
