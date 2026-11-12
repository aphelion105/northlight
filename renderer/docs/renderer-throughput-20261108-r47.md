# r47: remove the cold GPU admission bottleneck

The latest game input is frozen in `out/perf-20261108-r47/game.log`: r46 (`9a1b36b430835151ab07eb5e186a06d93f5d687f49efe9760dffa3a7f1022d70`), DXVK 3.1.1, RTX 4090, 3840×2160 HDR and MSAA 0. The world interval ending at frame 601 averages 27.074 ms (36.94 FPS). Login/loading intervals are not comparable gameplay samples.

## Measured bottleneck

Frame 635 has 1,376 normal packets, three flushes, 835 resident replays and 10,320,180 bytes of UP replay. Material/native-scene telemetry reports 16 promoted meshes, 541 attempt deferrals, only 248,784 uploaded bytes, no byte/time deferrals and 0.0459 ms creation time. The configured limits are 4 MiB of data per consumer/frame and a 1 ms creation budget.

The admission limit still counted individual meshes even though the scene store now packs them into shared VB/IB groups. Sixteen small meshes therefore exhausted the quota after creating approximately one pair of buffers. Hundreds of eligible meshes kept using UP replay despite available upload bytes, creation time and residency space.

The sampled command stream spends 20.5362 ms in 3,445 indexed-draw calls; renderer CPU capture is 9.205 ms. The 27.269 ms GameScene GPU interval includes normal flushes that occur inside scene draws, while the final ModelNormals interval is only 0.017 ms. That final interval does not measure the entire normal workload. Timestamp intervals also include submission gaps and must not be interpreted as isolated shader arithmetic costs.

The memory-guard boundary remains significant: frame 703 costs 22.765 ms, including a 17.918 ms trim and 12.182 ms snapshot clear. The nearby EVENT totals 48.5659 ms, with 45.3589 ms before replay and 3.1909 ms during replay. This change addresses excessive cold draw/upload work, not the synchronous memory-clear algorithm.

## Change

The common native scene store now admits up to 16 allocation groups per consumer/frame, with at most 16 members per shared group. Separate/multistream layouts still consume one group per mesh. Logical admissions are independently capped at 256, including failed attempts. The existing 4 MiB data budget, 1 ms creation budget, warm-up rule, 32/64 MiB consumer residency limits and address-space admission callbacks remain active.

A full group commits before accepting the next group. All quotas are rechecked after a reentrant admission callback; the creation-time limit is rechecked too. Automatic commits retain a completion notification until `commitPending` drains it. This is necessary for world bulk consumers to bind meshes uploaded automatically even when the final pending list is empty. Clear/reset removes stale notifications.

No shaders, AO, target resolution, geometry coverage, poses, culling or shadow distances change. Material capture, ModelNormals and actor shadow replay use the same policy on DXVK 1/2/3.

## Counterfactual validation

An offline fixture compiles both the frozen r46 store/normal renderer and current implementation. It partitions the stock M2 into 128 draws and compares captured normal pixels for all eight VS2/VS3 rigid/skinned/weighted programs on all three DXVK backends.

| Second-frame workload | r46 | r47 |
| --- | ---: | ---: |
| Resident packets | 16 | 128 |
| UP draws | 112 | 0 |
| Total physical draws | 113 | 8 |
| UP geometry input bytes | 2,931,516 | 0 |
| Cached upload bytes | 418,788 | 3,350,304 |

All images are bit-identical. A separate trial with the production 1 ms limit enabled also reaches 128 resident packets in every case. Observed creation CPU times are:

| Backend | Median, ms | Maximum, ms |
| --- | ---: | ---: |
| DXVK 1.10.3 | 0.339 | 0.376 |
| DXVK 2.7.1 | 0.386 | 0.398 |
| DXVK 3.1.1 | 0.326 | 0.434 |

These are component creation times and draw counts, not gameplay FPS or backend performance parity. The first observation still follows the existing warm-up policy.

More meshes intentionally upload into the cache earlier. Consequently, the old assertion that cached upload bytes alone must decrease is inappropriate. CPU/GPU workload fixtures now compare cached-upload bytes plus UP geometry input bytes; image equality, resource bounds and the per-frame 4 MiB cache-data limit remain enforced. GPU fixtures track UP bytes separately from UP draw counts, avoiding mixed units. These counts describe submitted geometry, not measured PCIe traffic.

## Regression checks

- Optimized and ASan/UBSan canonical-scene fixtures cover both consumers, 256 grouped versus 16 separate admissions, sixteen-group buffer counts, nested callbacks, byte/time exhaustion, final-empty-list notification, retries, allocation/lock/unlock failures, eviction, ownership transfer and retained views.
- Actual world binding and all four cache/bulk upload variants pass optimized and sanitizer tests, including fallback and recovery.
- Canonical MRT/depth/cutout images match on DXVK 1/2/3 with MSAA 0/4/8.
- All 24 standard ModelNormals shader cases pass independently of the 24 cold-admission comparisons.
- Point-shadow visibility/recovery matches 2,034 plus 8,136 faces per backend.

Final build, source hashes, exports and real EVENT smoke receipts accompany the DLL. Remaining memory-clear and EVENT peaks are not claimed eliminated; a new game run is needed to measure r47 FPS, upload deferrals, UP bytes and normal flush counts.
