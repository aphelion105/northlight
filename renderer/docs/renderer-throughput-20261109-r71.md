# r71: requested-face culling and direct point faces

Baseline: `24ecb9c44e5a78fda26adf850be28bb613158e73`.
Production source identity:
`9b6f1d0e9b062d1bc645a7b5bdc82cabf96948dc93588a2ffd0d61f5f6a79676`.
Build: Zig 0.15.2, `scripts/build_renderer.py`, x86 Windows,
2026-11-09 20:13:21 UTC. Receipts are in
[renderer-throughput-20261109-r71-validation.json](renderer-throughput-20261109-r71-validation.json).

## Changes

Point-shadow candidate clipping now tests only faces requested by the current
refresh. Static candidates additionally intersect the static-rebuild mask.
The complete face mask used for content signatures is unchanged. Invalid
bounds and transforms still retain every requested face.

A face without cached static content now receives current casters directly
into its final R32 target. The existing shadow shader saturates depth, so
the former union with white, `min(dynamic, 1)`, produces the same result.
An empty face receives a white clear. Faces with static content retain their
cache, scratch target and union. Partial-cycle weights, alpha coverage,
current-pose replay and failure/publication rules remain in place.

Ordinary point-resource creation and staged prewarming no longer create a
white texture for this union. The two-light prewarm fixture requires seven
creates instead of eight. The white texture helper remains necessary for
device-initialization pipeline warmup; the shared scratch allocation remains
necessary for faces with static content.

Production residency now depends directly on bindings, lock accounting and
one small shared statistics/budget header. The preprocessor dependency graph
contains neither legacy per-mesh nor legacy batched-cache implementation.
Those implementations remain available to comparison tests. Cache budgets,
eviction algorithms and runtime storage behavior are unchanged.

## Evidence

The scalar/reference clip oracle passes 174,335 boxes/transforms plus subset
mask checks, including invalid inputs, overflow-adjacent values and boundary
neighbors, in optimized and ASan/UBSan builds. Requested masks match the old
full mask intersected with the request.

An optional ABBA CPU component run uses 163,840 candidate checks per sample,
four warmups and six retained samples per variant. The game remains open.
These are component observations, not game-frame or isolated system results:

| Requested faces | Full-mask median, ms | Requested-mask median, ms |
|---|---:|---:|
| 1 | 12.622122 | 3.321276 |
| 2 | 11.629593 | 5.024809 |
| 3 | 11.597969 | 6.945305 |

The real point renderer is also compiled with the baseline world/point
headers as a control. On native D3D9 and DXVK 1, 2 and 3, five alpha recipes
(opaque, legacy cutout and native fixed-alpha ARGB8/FP16/FP32) each exercise
merged and unmerged geometry. All 40 nonempty R32 cubes, covering 240 faces,
have matching complete-cube 64-bit hashes between versions. Actual fullscreen
draw interception agrees with the work counter: 240 union draws before,
zero after in this fixture. This is conditional work removal; cached-static
faces still need their union.

The full point oracle also passes its prewarm, static retention, cycling,
white-cube, current-pose, bounds and source-lifetime cases on all four
backends. Native canonical-residency GPU comparisons pass at MSAA 0/2/4/8,
including pressure/fallback cases. They use the runner's explicit compatible
Microsoft shader-compiler option; the default compiler pin is unchanged.

Focused CPU/contracts cover legacy-cache comparisons, shared residency,
upload locking, maintenance, diagnostics, point selection/scheduling,
replay upload and hotpath equivalence. The schedule source check was updated
to the shader's existing structured return type, then rerun successfully.
Windows-only checks skipped by Linux ran on Windows; C++ checks skipped by
Windows ran under WSL. This is not a full repository-suite run.

The user permits minor visual differences, including one R16 step. This
iteration requires no such tradeoff and changes no HLSL or compiled shaders.
GPU timings are invalid with the concurrent WoW and are not used. No game
FPS claim is made. The exact castle-video problem remains deferred by the
user. The local game log has not changed since the preceding iteration.

## Delivery

`0.3.217-test.20261109.5`: production DLL and verified Windows installer.
The package contains the identical DLL, with all 17 expected exports and
ordinals. Artifact sizes and SHA-256 values are recorded in the receipt.
No client DLL was installed and no game was started by this iteration.
