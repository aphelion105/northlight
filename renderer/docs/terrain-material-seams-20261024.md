# Terrain material seam investigation

Scope: source inspection only at c568dea. No game, build, GPU test, or installed file was touched. The two supplied Crystalsong screenshots show broad facet boundaries as well as texture detail; screenshots alone cannot establish that every boundary is an ADT chunk boundary.

## Confirmed normal-frame inconsistency

`shaders/auto_parallax.hlsl:materialFrame` derives its normal from `cross(ddx(p),ddy(p))`. This is a triangle-flat geometric normal. `shaders/auto_bump_capture.hlsl:materialValue` produces that normal even when the generated normal's XY is exactly zero. `shaders/auto_bump_normal.hlsl:autoBumpNormal` then replaces the smoothed scene normal with the captured absolute normal. `shaders/world_effects.hlsl:MaterialComposite` adds lighting for that captured normal and subtracts lighting for the smoothed normal. Consequently a neutral height map changes broad surface lighting at triangle boundaries. This violates the expected neutral-detail invariant and affects bump-only as well as POM and displacement capture.

The existing `worldNormalValue` pass deliberately reconstructs a smoothed surface normal to avoid facet boundaries. Applying absolute triangle normals defeats that behavior; increasing texture blur or lowering strength/range would conceal rather than fix it.

Minimal proposed correction: reorient the captured normal from its geometric reference frame onto the existing smoothed normal before applying strength/fade. A shortest-arc rotation preserves the captured detail angle and maps a neutral generated normal exactly onto the scene normal. Reconstruct the geometric reference from camera-relative position to avoid large-world-coordinate precision loss. Derivatives must be evaluated before divergent branches; caller early returns and silhouettes need explicit GPU coverage checks. Do not ship an unverified derivative implementation solely on the screenshot diagnosis.

## Five directions checked

1. **UV/layers/tangents:** UV derivatives cancel integer chunk UV offsets. Independent tangent and bitangent normalization is not an orthogonalization for skew UVs, but the confirmed flat reference mismatch already explains neutral-texture facet changes. Native UV transforms and shared edge UV continuity still require an adjacent ADT oracle.
2. **Height/normal/mip/wrap:** `auto_normal_generate.hlsl` uses central differences; `auto_normal_cache.h` wraps terrain source sampling and generates linear-filtered mips. Replay copies native sampler U/V addressing. There is no inspected per-chunk normal generation reset. Verify all mip levels and integer UV offsets with the same actual MPQ texture.
3. **Alpha/layer semantics:** `BumpLayer` consumes `dot(tex2D(LayerMask,maskUV),NativeLayerMask)` and replay uses native-style SRCALPHA/INVSRCALPHA composition. Octahedrally encoded normals are blended before decoding, which is nonlinear and potentially sensitive to layer order; this is a separate candidate, not proven to cause the pictured seams. Compare native terrain0 mask behavior and adjacent chunks with reordered equivalent layers.
4. **Capture/depth/MSAA/derivatives:** opaque deferred captures flush before immediate layer captures; viewport and raster state are retained. MSAA uses a private single-sample capture depth. That can affect coverage edges and must be tested both ways, but does not explain large uniform triangle interiors by itself. Existing clips/early returns also make derivative placement relevant.
5. **Displacement edges/LOD:** `displacement_geometry.h:build` pins draw-boundary vertices to the authored surface and chooses one subdivision factor per draw. This prevents open cracks but changes relief slope near boundaries and does not guarantee identical adjacent draw tessellation. This is distinct from the bump-only normal problem; removing the pin without shared-edge ownership would introduce cracks.

## Targeted regression

Extend the existing actual-MPQ offscreen oracle (`validate_auto_bump.py`, `auto_normal_oblique_gpu.cpp`) rather than launching the game. Its current plane is coplanar and its normal buffer is constant, so it cannot catch this defect.

- Use the existing Crystalsong grassa/grassb/grassc/rocka MPQ textures and the extracted native terrain VS/terrain0/terrain1 PS; retain asset hashes in the report.
- Render two noncoplanar adjacent terrain pieces with one continuous smooth reference normal and matching shared-edge UVs. First use a neutral generated normal: enabling bump must not change directional lighting apart from FP16/8-bit tolerance. The current code must fail this case.
- Repeat with real generated detail and integer UV offsets, both orientations, reversed submission order, each alpha-mask endpoint plus fractional blend, and reordered equivalent layer sets. Compare the joined draws against an equivalent continuous-draw oracle; retain nonzero detail away from the join so a suppressed-detail implementation cannot pass.
- Repeat bump-only, POM, and displacement, MSAA off/on, immediate/deferred capture, grazing views, large camera-relative world locations, and silhouettes. Record separate interior and edge errors rather than averaging away a thin seam.
- Keep displacement shared-edge position and slope checks separate. Require watertight shared vertices across differing subdivision/LOD choices; a bump normal correction does not establish displacement continuity.

## Candidate and proof status

Root ran the production `MaterialComposite` neutral-frame counterexample against pinned DXVK 3.1.1 in `terrain-neutral-counterexample-1`. It reported maximum RGB error 0.0505633354 in both the interior and aligned join. This proves the neutral-detail invariant failure for that synthetic input; it does not uniquely identify every mechanism in the user's screenshots.

The candidate helper now accepts an explicit geometric reference. The private context patch `tests/terrain-normal-callers.patch` hoists its camera-relative reference calculation before divergent branches in all six callers; root owns application of those caller edits. No generated shaders were changed by this worker.

The oracle supports odd crease and silhouette placement (`--split 127 --silhouette 193`). A final-pass derivative can span multiple surfaces in such a quad, unlike the original per-triangle capture derivative. Passing only the aligned case is insufficient: these cases gate the candidate and may require depth-aware one-sided reconstruction or retaining the exact geometric capture reference.

Root rejected the initial derivative-reference candidate: aligned neutral passed (interior 0.0000652, join 0.0000367), but odd split 127 failed at 0.0504828 and the odd silhouette failed at 0.18. These tolerances were not relaxed.

The replacement candidate uses eight explicit native-depth taps (offsets -2,-1,+1,+2 on each axis). It chooses a one-sided tangent from the lower second difference of inverse view depth, which is affine on a projected plane. Out-of-image, sky and nonfinite neighbors are unavailable. It does not sample the material map for neighbor admission: the geometric plane can continue outside the material's coverage. The existing final center material-depth/coverage check remains authoritative. No arbitrary blur or angle cutoff was added.

Both world and local shader contracts explicitly map viewport MinZ and inverse depth range from their existing helpers (`Projection.w`/`DepthParams.x` and `LocalProjection.w`/`LocalDepthInfo.x`); the rays retain the existing half-texel correction. Missing second neighbors fall back to a valid one-step tangent; no valid one-step tangent produces a zero reference and the finite smoothed-normal fallback. Thin or multiply intersecting geometry can remain ambiguous when neither side contains enough same-plane samples. This is an unresolved geometric limitation, not proof of seam removal.

The private caller delta is `tests/terrain-reference-neighbors.patch`. Incremental GPU cost of the eight depth taps must be measured against the frozen baseline before adoption; no performance result is claimed here. The synthetic/partial-source oracle also supports `--distance 16` (range >0 through 24) to expose native-depth precision differences away from the camera without changing the full-strength detail expectation.

`--source <source.fgr>` uses an existing actual-MPQ fixture from `validate_auto_bump.py` and production `NorthlightAutoNormalCache` generation, asserts nonzero detail, and compares the final composite against analytic smooth-frame detail. Its adjacent-facet capture encoding is synthetic and explicitly logged as such. It is partial actual-source coverage, not a substitute for native adjacent terrain draw replay. This worker executed no tests. No visual fix is claimed until the root agent has built and validated production shaders and the adjacency/coverage cases.

The existing native oracle now also accepts `NORTHLIGHT_TEST_MESH_GRID=2` (or a denser grid) and `NORTHLIGHT_TEST_TERRAIN_FOLD=0.2` to fold the real native terrain mesh with continuous UVs and a shared smooth reference. `NORTHLIGHT_TEST_TERRAIN_NEUTRAL=1` sets all mip RGB to a constant while retaining alpha. Compare its existing `native-normal-off.fgr` and `normal-on.fgr` with the unchanged 0.002 limit, including creases and outer coverage edges. Omitting the neutral flag retains the actual MPQ source detail and exercises native capture rather than analytic encoding. This test hook alone does not establish independent adjacent-draw equivalence, layer reordering, or LOD continuity.
