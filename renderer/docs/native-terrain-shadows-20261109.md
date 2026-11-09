# Original terrain shadow visibility

QA supplied Kamagua screenshots with Northlight off/on and the game's Shadow
Quality at minimum. The static ground shadows are consistent with the terrain's
separate baked shadow layer. Stock data at the supplied map position (Howling
Fjord 25.3, 57.6) resolves to `Northrend_37_30.adt`, which contains 228 nonempty
MCSH chunks. Exact archive/file identities are recorded in
`out/qa-kamagua-20261109/kamagua-assets.json`. This confirms local stock map data,
not the origin of every dark pixel in the tester's image.

## Control

**Disable original terrain shadows** is a live checkbox in Shadows. Its INI key
is `DisableOriginalTerrainShadows=1`, enabled by default in all three presets.
It suppresses the original terrain shadow factor, independently of World
shadows, map readiness and active sun/moon replacement coverage. It requires
Northlight scene effects to be active; it does not start the shared scene
pipeline by itself. Master effects off/F10 restores the original game image.

The setting is not restart-only: schema, save/reload, live quality application
and the next-frame draw latch use it immediately. A change invalidates temporal
lighting history. Existing explicit file values remain authoritative.

The help text distinguishes map shadows from baked lighting/darkness in texture
RGB or vertex colors. The latter is preserved, along with layer weights, fog,
native lighting and specular terms except their original shadow multiplier.
Northlight's directional, character and local-light shadow controls are separate.
The implementation uses the existing cached terrain PS replacement; no MPQ
modification, white alpha-map substitution, new render pass or image allocation
is introduced. Non-terrain vertex families are rejected before PS replacement.

## Coverage fixes

Exhaustive stock PS 2.0/3.0 enumeration found two missing Terrain0 consumers.
They have no literal one and use the shadow in the second MAD multiplicand.
The identity fold now handles both `S*A+B` and `A*S+B`, producing `A+B` while
preserving bytecode word offsets and destination modifiers.

Real native D3D9 shader creation exposed another failure in the old replacement:
substituting one into the literal diffuse scale could create a rejected
all-constant MAD. The proven `0.3*S+0.7` scale is now folded to ADD plus padding
NOP. This uses the same literal register and keeps the native fog epilogue and
other meanings of reused temporaries intact.

## Validation

- All **392 stock terrain shadow consumers** patch successfully and their
  transformed programs are accepted by native D3D9, DXVK 1.10.3, 2.7.1 and 3.1.1.
- The remaining two stock TerrainSM programs do not sample a terrain shadow
  texture; they generate shadow distance and remain unchanged. All **256 stock
  terrain VS variants** examined are recognized by the existing family gate.
- Existing POM and shared-POM eligibility is retained for every supported stock
  terrain program, in both single/dual-layer modes.
- Real Terrain0 PS2/PS3 GPU draws in SDR and FP16 match the original shader with
  an unshadowed map byte-for-byte, with nontrivial diffuse/specular/fog and packed
  RGB layer data. Alpha is unchanged. The shadowed reference differs, preventing
  an empty/no-op comparison. Sixteen comparisons pass across the four backends.
- Settings tests cover enabled defaults, inverse checkbox semantics, save/reload,
  live apply and availability with World shadows off. Extracted production
  planning covers 8,192 visibility/gate combinations; existing coverage arithmetic
  covers 121,108 cases and draw-state/refcount/fault handling covers 260 cases.

Reproduce the stock/GPU checks with `tests/test_terrain_shadow_disable_gpu.py`
(`--backend` can repeat; `--output` must name a new directory). Results and source
hashes are in `out/qa-kamagua-20261109/gpu-validated/validation.json`.
No in-game Kamagua acceptance run, DLL linking or installation was performed.
