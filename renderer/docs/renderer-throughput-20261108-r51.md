# Renderer throughput r51 РІР‚вЂќ 2026-11-08

## Input and diagnosis

Frozen gameplay input: `out/perf-20261108-r51/game.log` (1,265,229 bytes), SHA-256 `d7cd013c9a0fb2be0ed847739c03a7e8ffad0148f66178ec407b98d7f80c4450`; original file modified 20:15:34 local time. The DLL identifies source `0f567a79c9c895539594d21508827f6e5ce37b5c3d9c74f17d0e8ea202021323` (r50), DXVK 3.1.1.

The early foreground section runs near 100РІР‚вЂњ115 FPS with zero scene capture. Frames 1321РІР‚вЂњ2041 run near 30 FPS with focus lost and still zero scene capture. Those samples cannot be used as the cost of the active world pipeline. The active-world transition appears around frame 2627. Its 120-frame window has a 2,473 ms maximum and a 380 ms p95. The subsequent foreground world windows are approximately 25 FPS / 39РІР‚вЂњ42 ms. This is a different scene/workload from r50's input; option toggles are sequential, not a controlled effect benchmark.

| Active-world sample | Total extension CPU | Capture CPU | Effects CPU | Total GPU timestamp span | GameScene span | Final normals flush |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 2921 | 15.933 ms | 10.720 ms | 3.542 ms | 28.803 ms | 24.552 ms | 0.033 ms |
| 3175 | 7.937 ms | 4.616 ms | 1.516 ms | 23.639 ms | 19.042 ms | 0.222 ms |
| 3302 | 6.375 ms | 4.430 ms | 0.814 ms | 19.930 ms | 16.586 ms | 0.248 ms |
| 3556 | 4.936 ms | 3.538 ms | 0.522 ms | 18.355 ms | 14.589 ms | 1.106 ms |

Earlier material/normal replays are inside GameScene. Timestamp spans can include queue starvation; they are not hardware utilization percentages. At frame 3048, materials perform 2,605 native draws (1.9567 ms CPU), 2,121 capture setups (1.0226 ms), 1,971 snapshot requests (0.7760 ms) and 2,779 envelope evaluations (1.3079 ms). The source reads are only 8,448 bytes: most requests already hit retained immutable geometry. The r50 shared-key path is active (560 hits in that sample).

### Memory and cache pressure

At frame 2924, available 32-bit virtual space is 348 MiB (largest block 162 MiB), while free physical RAM is over 9 GiB. The critical boundary costs 26.256 ms overall. Its trim reports 21.635 ms, including 12.682 ms clearing 90.8 MiB of snapshots and 3.516 ms clearing replay pools. At frame 3541, another critical boundary costs 7.136 ms. Raising every cache allowance would worsen this virtual-address-space pressure.

After the first trim, the shared GPU scene sits within a few bytes of its 32 MiB aggregate cap. ModelNormals frame 3048 has 1,525 resident and 250 streamed logical draws; it streams 7,922,484 bytes while avoiding another 3,608,136 bytes through source reuse. Frames 3175РІР‚вЂњ3429 continue to stream about 7РІР‚вЂњ8 MiB. UP bytes remain zero: the r49 fallback stream works, but repeated misses still consume CPU, bandwidth and command submissions.

The largest late EVENT is game frame 2926: 56.8662 ms total, 53.1769 ms before replay and 3.6763 ms replay, just after the critical trim. Game frame 2852 reports 45.1459 ms (12.7370 before replay, 32.3484 replay). Backend fence calls remain zero. These changes address some workload feeding those waits; they do not claim that every EVENT or the cold world-entry spike is eliminated.

## Changes

### Lossless narrow resident indices

The common `NativeStore` chooses a 16-bit index buffer only after determining each member's absolute/relative addressing and the maximum encoded index for the entire batch. Values through 65,535 are exact; a value of 65,536, a rebased member exceeding that range, or a wide invalid relative index retains 32 bits. Vertex data and placements are unchanged.

The GPU allowance charges actual requested buffer bytes and live-member bytes. Admission retains the existing conservative 32-bit reservation, upload/time/group budgets, current-frame protection and failure handling. This improves cache density and index upload/fetch bandwidth without another backend-specific renderer or a larger aggregate allowance. Separate/multistream allocations retain their existing representation.

On the stock-M2 component fixture, the r50 canonical path's resident peak changes from 1,759,632 to 1,556,472 bytes (11.5% less) at zero cross-consumer overlap. At full overlap it changes from 879,816 to 778,236 bytes. Uploaded bytes change by the same proportion; draw digest, creation count and fallback coverage are preserved. These are requested D3D buffer sizes, not measured driver backing/RSS or gameplay FPS.

### Remove middle duplicate opaque normal draws

The existing adjacent-range merge cannot remove repeats separated by other geometry. Before replay, a reverse scan removes only middle identical resident indexed triangle draws in the current batch. It retains the first depth occluder and the last coplanar colour writer. The private pass uses opaque writes and LESSEQUAL depth; after an identical first draw, a middle repeat cannot change depth. Retaining the first avoids increasing fragment work on intervening occluded surfaces; retaining the last preserves coplanar ties. Adjacent identical runs keep their existing one-draw merge.

Equality includes retained immutable VB/IB references, effective placement, geometry dimensions, shaders, declaration, viewport, raster state, every consumed constant bank and sampled texture/sampler state. Packets remain alive through replay because later packets may borrow their constant storage. Draw order of retained work is unchanged, and the filter does not cross flush/texture-write boundaries.

The lookup is an 8 KiB stack-local direct map with one candidate per packet. A match marks the candidate only after proving that it has both an earlier and later identical draw. Hash collisions can only miss a saving. There is no heap allocation, persistent index, eviction policy or per-backend branch. Nonresident streaming/fallback uses the existing path. `MODEL_NORMALS duplicateDraws` counts eliminated logical draws; `mergedDraws` continues to include all saved submissions and `submittedDraws` remains the physical count.

The interleaved real-GPU fixture places two coplanar surfaces with opposite normals in A/B/A order. The output must equal the A-only image and differ from B-only, so retaining the wrong duplicate fails. It changes 33 draws to 18 with bit-exact pixels; the first and last packets are explicitly required to remain. Negative controls cover geometry/placement changes, viewport, raster, constants and resource identities. Existing controls cover integer/boolean and texture/sampler state. CPU replay timings use nine alternating pairs with readback after each frame; they are component submission timings.

## Validation

The CPU scene suite passes in O2 and ASan/UBSan, including index boundaries, mixed-width batches, invalid-index fallback, physical-byte accounting, exact effective vertex addresses, quota boundaries, LRU order, reconstruction, held views, allocation/lock/unlock failures and recovery. The upload and shared-residency oracles now read the declared IB format instead of assuming four-byte indices. Lock policy remains fresh write-once DEFAULT/WRITEONLY with the same flags.

Final GPU receipts, component measurements and production DLL identity are appended after validation. No AO, effect-strength, resolution or output-shader change is part of r51. Remaining critical trims, missing input and backend performance parity are not declared solved by these component checks.

## Frozen r50 comparison

Seven alternating measured pairs after two warmup pairs use identical stock meshes and the r50 NativeStore header. CPU values below cover the complete 96-frame fake-D3D component workload; they exclude shader/GPU driver time and do not represent gameplay frame cost. Draw digest, creation count and fallback bytes match.

| Cross-consumer overlap | r50 / r51 CPU total | r50 / r51 uploaded bytes | r50 / r51 resident peak |
| --- | ---: | ---: | ---: |
| 0% | 2.8400 / 2.6426 ms | 5,278,896 / 4,669,416 | 1,759,632 / 1,556,472 |
| 50% | 2.1974 / 2.1236 ms | 3,785,760 / 3,348,864 | 1,261,920 / 1,116,288 |
| 100% | 1.8470 / 1.7443 ms | 2,639,448 / 2,334,708 | 879,816 / 778,236 |

Final normal filter submission medians across eight programs (nine alternating pairs per program): dxvk1 0.055250 / 0.054350 ms, dxvk2 0.058500 / 0.059200 ms, dxvk3 0.055950 / 0.055950 ms. These small component times do not isolate GPU execution or establish gameplay FPS. The final filter changes the interleaved fixture from 33 to 18 draws while retaining first depth occlusion and last coplanar output.

Completed checks: O2 and ASan/UBSan index/address/cache controls; 26 spare/aggregate cache controls; canonical CPU and actual world binding; replay upload/lock audit; 24 stream cases with failure controls; 24 standard normal cases with interleaved coplanar last-writer and full-state negative controls; MRT/MSAA, constrained aggregate residency and retained-geometry pixel oracles; local-shadow visibility/recovery on all three DXVK backends.

Production DLL: `out/dll-20261108-r51/d3d9.dll`, 10616320 bytes; SHA-256 `5684003c73c85565e8e79760203cd39119299540c0e8e8848e3f9e72474f24e8`; source identity `9eebf0fb5c2b5e5473c19a2355d606f2b946d0a1f3fa44a21f70a3987425920e`, built 2026-11-08 19:15:03 UTC. Compiler preflight, shader freshness, PE32/export validation, final source hashes and 192 real EVENT completions through the final command-stream proxy pass. The installed client is unchanged; no installer was built.
