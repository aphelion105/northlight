# Stormwind shop shadow review, 2026-11-16

## Evidence and limits

The supplied `qa_report/2026-11-15_20-13-43.mp4` is 9.292 seconds at
1920x1080, 24 fps. It shows the Trade District shop, multiple lamps, moving
camera and changing shadow boundaries on the floor. No render capture,
source/cube identities, configuration or build ID accompanies this recording.
Its filename is not proof of the binary that produced it. The exact game's
failure cannot be assigned to a particular light or pass from these pixels.

The examined renderer is `82feb10e`. The supplied video is not a replayable
GPU workload. Local analysis images and focused checks live under
`.local/out/shop-shadow/`; they are not replacement captures of this shop.

## Current implementation and relevant fixes

Each selected point source owns its own 256x256, six-face R32F cube and static
face cache. `renderPointShadows` updates the sources independently;
`renderPointLighting` applies every ready source additively, removing only that
source's occluded radiance. There is no one-light rendering limit. The factory
`PointShadowLimit` and `LocalLightLimit` are both 64; the maximum is 128.
Selection uses stable source IDs, incumbent preference and fades under pressure.

There is a separate limit in the native shader endpoint fallback: at most three
sources, sometimes fewer for complex materials, fit its SM3 variant. Source
order follows the game's draw bank. A source can therefore stop being corrected
when the game rearranges that bank. Raising `PointShadowLimit` cannot fix this.

The WMO room-layer fix in `5d58d89a`/`535193c7` avoids that endpoint limit on
proven, depth-writing layers with RGB-independent blending: it hands all matched
native lamp RGB to the deferred local-light pass. The existing vertex and pixel
dependency proofs, alpha, other lighting, and scoped constant restoration remain
mandatory. The fix is already in the installed menu test build `1d5eb8031618`.
Unknown or unsupported materials still retain the conservative fallback.
See [the interior lighting review](interior-lighting-20261116.md).

Another relevant fix, `ad06f6aa`, interpolates cube visibility comparisons rather
than equally weighting four nearest samples. Its previous-filter control
reproduces quarter-visibility jumps during subtexel receiver motion. This fix
is also already in the current test build. Neither defect requires alternating
which lamp is visible each frame.

## Regression coverage

- `test_local_source_shadows.py`: actual rasterized cubes and production shader
  bytecode for 4/8/16/32/64/128 simultaneous sources, independent occluder
  contributions and different source/shadow strengths.
- `test_native_local_light_gpu.py`, layered WMO cases: all 24 permutations of
  four physical lamps for each of three blend modes. Full RGB ownership matches
  the independently zeroed native bank exactly. A two-slot negative control
  changes residual lighting when the bank order changes. Alpha and constants
  must remain intact on every draw. This isolates the failure mechanism; it
  does not establish the shader used by the supplied shop video.
- `test_local_shadow_continuity_gpu.py`: moving receivers, cube seams and corners,
  hard/soft shadows, with the previous filter as a negative control.
- `test_local_overlap_gpu.py`: current local colors survive directional shadows,
  ordering, source motion and retirement without retained local-light trails.
  Its fixture now sets the production directional-map admission flag; the
  shadow-floor assertion is unchanged.
- `test_point_shadow_selection.py`: stable source identity under input reordering,
  competing sources, budget shrink, missing sources and deferred capture.

These are image/contract checks, not a confirmation of the exact shop scene.
No new runtime change is justified solely by this recording; the relevant fixes
are already present. A shop check on the current DLL remains the live acceptance
step. If it still fails, source ownership, missing off-camera casters and
directional/native overlap remain distinct possibilities.

## Options for distributing work

| Option | Behaviour | Cost and limits |
|---|---|---|
| Keep the current complete-cube path | All ready lamps cast every frame; unchanged static faces are reused. | Recommended for this report: no evidence of exhausting the 64-source budget. A cube costs 1.5 MiB, plus up to 1.5 MiB of static faces. |
| Existing face cycles | `PointShadowFacesPerFrame=1..5` refreshes each unchanged lamp over several frames; all six stored faces remain usable. | Already implemented; default is 6. Different faces can represent different moments and show seams for moving casters. This is a per-lamp limit, not a global update budget. New/moved/resized lamps still require a complete cube. |
| Global scheduling of complete cubes | Retain every resident cube, update a bounded subset of lamps each frame, and still apply every valid cube every frame. | Preferred next optimization if measured update cost warrants it. Prioritize invalid maps, moving sources and nearby dynamic casters; use age to prevent starvation. With N due lamps and budget B, a simple round robin revisits them in ceil(N/B) frames. Memory and per-frame lighting cost remain. Requires changing replay demand prediction as well as the update loop. Not implemented by this review. |
| Atomic staged face updates | Build an unchanged lamp's replacement in a spare cube, then publish all six faces together. | Avoids mixing face generations, at least 1.5 MiB per staged cube plus dynamic-source latency. Invalid source identity/position must never use the old projection. A bounded staging pool is preferable to doubling every source's storage. Not implemented by this review. |

The reusable object is the light-space depth map, not accumulated screen darkness.
Summing past darkening would double-count shadows and leave trails after camera,
caster or light movement. Any update scheduler must preserve source identity,
resource failure invalidation, map/device resets and a maximum dynamic-map age.
Updating fewer cubes also cannot overcome the resident-source cap or an
unsupported native receiver's shader limits.

No release, tag, package or client configuration change is part of this review.
