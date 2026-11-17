# Local-light preparation, 17 November 2026

The last completed gameplay log used renderer source
`66115e9463a848097a6ca52973b15b6939c4ba3dcf416deed36649595aa6a148`
with DXVK 3.1.1 at 3840 x 2160. Its preserved copy and numeric extraction are in
`.local/out/local-lights-20261117/last-session.log` and
`last-session-summary.json`. The log SHA-256 is
`03a0d1dcc485c86d49d6b9b95419b86e79a8a93b7f806ff6769962ef70fcdfd1`.

The expensive local-light interval is `PointShadow`: among 36 samples containing
that interval, its median is 2.155 ms, p95 7.144 ms and maximum 11.198 ms.
`LocalLights` itself has median 0.231 ms and maximum 0.627 ms. Restricting to
the 25 samples with positive point-shadow GPU work gives a 3.152 ms median.
These sampled intervals are not interchangeable with whole-frame CPU time.

Frame 2286 submits 1,972 static and 4,095 replay draws for 21 selected sources,
11,107,396 triangles and 84 rebuilt static faces. Even after actor shadows are
disabled, frame 5969 submits 1,490 static draws, 4,135,200 triangles and 54 rebuilt
faces for 21 sources. The log's LocalLights toggles also change the observed
frame rate substantially, but camera/crowd/settings changes prevent treating
those windows as a matched benchmark of this patch.

## Removed work

`renderPointShadow` previously reconstructed terrain candidates on every due
refresh, even when all six existing faces were subsequently reused. Each light
could walk 9,833 terrain batches from a 12,854-batch committed mesh merely to
rediscover empty candidate lists.

An unchanged complete geometry-only cube now returns before candidate gathering
and replay-bound preparation. Reuse requires the same light, valid static
content, matching mesh and live-terrain revisions on every face, and no current
actor replays. Reused static-only faces also renew that geometry proof. Moving
lights, entering/leaving actors, changed geometry, incomplete face cycles and
failed writes retain the existing refresh/invalidation paths.

The existing bounded terrain memo additionally records whether any face contains
terrain. A warm empty memo copies its masks and accounts for the same rejected
batches without expanding six lists again. Counts are collected during the cold
classification, without another scan. Storage remains bounded by the existing
65,536-batch limit; no caster, light or quality setting is removed.

## Measurement and validation

The same-executable ABBA comparison expands the former warm terrain memo versus
the optimized path for 14 lights and 12,854 batches. The isolated DXVK 3 run,
with no game or parallel builds, measures median CPU time **0.396536 ms before,
0.0042455 ms after**, a 93.4x reduction for this preparation step. It checks
identical masks, candidate lists and rejection counts, including partial-face
requests. This is approximately 0.392 ms saved in that synthetic workload,
not a 93x improvement to gameplay or the complete shadow pipeline.

The existing production-renderer GPU oracle passes on native D3D9, DXVK 2 and
DXVK 3. It checks all six cube images, opaque/cutout casters, actor entry/removal,
light/mesh mutation, source replacement, partial cycles, failed writes,
offscreen retention, reset and pressure. The warm complete cube additionally
asserts that no terrain preparation was performed. Artifacts:

- `.local/out/local-lights-20261117/verified/validation.json`: three backends.
- `.local/out/local-lights-20261117/clean-timing/validation.json`: isolated ABBA,
  source identity and executable/backend/input hashes.
- `.local/out/local-lights-20261117/source-final/results.json`: source contracts.
- `.local/out/local-lights-20261117/architecture-final/results.json`: ownership
  and capture contracts.

Production source identity:
`6c13f63a480dbb82605039e5dc48cfcbf223faf62cc6a5aac611458bb38f8fa2`.
The standard production builder retains Zig 0.15.2, shader freshness checks and
the checked DLL/resource pairing. No release archive is produced by this change.

## Remaining GPU cost

This patch removes repeated CPU preparation; it does not reduce the triangle
count for genuinely moved lights. Several native sources in the log animate
their positions, and a changed cube origin legitimately invalidates six static
faces. Ignoring those movements or lowering light/shadow limits would change
the image. Finer conservative caster ranges for large static batches are the
remaining geometry optimization target; they need their own matched GPU
measurement. A new gameplay run is still required to establish the FPS change.
