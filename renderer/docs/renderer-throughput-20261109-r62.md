# r62: exact shadow-volume coverage and combined culling DLL

The client log is still the r57 run, 680,540 bytes, SHA-256 `435e1213014ffdac3b5d7fdcf8a0c1abc5baaf10e867f920c4b7e069cb6a3ab6`. No new gameplay FPS measurement is available. This build includes the source-only r60 duplicate-work changes and r61 receiver-frustum culling, plus the changes below.

## Shadow coverage work

`StaticShadow::containsBounds` previously projected all eight corners to prove that a live replay was entirely covered by the cached shadow volume. Its first corner still runs first, preserving the short rejection path for obviously uncovered boxes. For a finite affine light matrix, interval extrema and a conservative float-error allowance can then prove containment or non-containment without seven more projections. Borderline values and projective matrices retain the original corner checks. The selected replay set matches the old float implementation exactly, including clip-plane boundaries.

The existing per-slot cull worker now also computes this cached-volume proof for static M2 candidates. Their render-thread branch consumes the same matrix-validated result as WMO candidates. A missing job or mismatched matrix still uses the inline proof. Cache-current checks, complete model/revision coverage, alpha exclusions, actor-map decisions and ownership rules remain unchanged.

No camera-frustum rejection is added to a shadow-caster path. Offscreen casters remain eligible to affect visible receivers. No new cache, worker, shader, backend branch or resource limit is introduced.

## Component measurements

Native x86 release benchmark, 4,096 boxes repeated 128 times per sample, nine alternating paired blocks:

| Input distribution | Original ns/box | Current ns/box |
|---|---:|---:|
| Inside cached volume | 24.6454 | 15.7406 |
| Mixed containment | 20.9232 | 14.6881 |
| Mostly far outside | 7.0101 | 8.5129 |

This targets already-proved static-cache candidates, where contained boxes require the most original work. The mostly-outside component is slightly slower; the implementation does not claim a universal win or a measured game FPS increase. The existing r57 log also does not establish how often this new path will activate in a later run.

## Validation

Evidence: `out/perf-20261109-r62`.

- 201,000 containment cases at O2/O0 and ASan/UBSan match an independent frozen eight-corner implementation. Cases include grazing lights, translated depth slabs, invalid/reversed bounds, huge values, perspective transforms and clip-plane boundaries.
- The extracted current production cull worker is checked on 4,096 packets in all four shadow slots. WMO decisions, M2 decisions and light-volume cull flags match direct predicates.
- Static M2 decision/coverage/stability tests and production static-caster transform/dirty-region tests pass with sanitizers, including their negative control for the old shrinking-bounds bug.
- Job-system O0/O2 and source-order/lifetime checks pass.
- The r61 receiver suite is repeated on native D3D9 and DXVK 1.10.3, 2.7.1 and 3.1.1: 160,019 CPU projection cases, actual producer/range checks, bit-exact viewport culling, stale-pose/disabled-clipping/wireframe retention, all-offscreen frames and full ModelNormals GPU regressions.

Production source and shader hashes are frozen in `final-source-hashes.json`. The build uses `python scripts/build_renderer.py`, pinned Zig 0.15.2, and existing shader-freshness, export and package checks. Final build and real-EVENT production-proxy smoke receipts are recorded with the delivered DLL.

The installed client DLL, configuration and game log are not changed. This iteration builds a DLL only.

## Delivered artifact

- DLL: `out/dll-20261109-r62/d3d9.dll`, 10,661,888 bytes.
- SHA-256: `4344befd0492db234eafdbe783fddc604b0dec6285daa0989e4d9aa30f5e523a`.
- Source identity: `7e95cd6b7796a55815c2185066468529e0bb381396d2e551a693c84f91d75cbe`.
- Build time: November 9, 2026, 08:58:43 UTC.
- Final production proxy passes 64 real-EVENT generations on each of DXVK 1.10.3, 2.7.1 and 3.1.1.
- Build, export/source-marker and final validation receipts are in `out/dll-20261109-r62`.

The smoke test verifies real query completion and the built artifact, not game FPS or elimination of the remaining EVENT tail.
