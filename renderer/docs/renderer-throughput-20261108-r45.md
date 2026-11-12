# r45: shared indexed geometry and compatible normal draws

The latest client log is an r44 run (`e250a5d3f65b0c5abe02e5bda60dd711d720ae239d3d34ca507fc3a4db9b56e5`), using DXVK 3.1.1 on an RTX 4090 at 3840×2160 with HDR and no MSAA. The frozen input and validation receipts are under `out/perf-20261108-r45`.

The final two 120-frame intervals average 25.000 and 28.280 ms (40.00 and 35.36 FPS). Earlier loading intervals contain multi-second outliers and are not steady-state measurements. Sampled GPU intervals show:

| Frame | Whole interval, ms | GameScene, ms | ModelNormals, ms | Effects, ms |
| --- | ---: | ---: | ---: | ---: |
| 508 | 19.077 | 13.857 | 1.445 | 2.373 |
| 635 | 19.283 | 12.968 | 1.504 | 3.538 |
| 762 | 23.987 | 15.918 | 2.513 | 3.840 |

These are timestamp intervals, which can include starvation between submissions; they do not isolate shader arithmetic. In frame 762 the measured command-stream subset spends 13.7564 ms in 3,107 DrawIndexedPrimitive calls, 3.1019 ms in texture unlocks and 2.4959 ms in texture creation. Frame 635 spends 10.282 ms in 3,001 indexed draws. ModelNormals replays 1,114 packets in frame 762, all GPU-resident, with zero source-read bytes. Additional source uploads are therefore not the main cost of that warm normal pass.

EVENT synchronization still matters: frame 645 totals 33.0237 ms (10.0275 before replay, 22.9722 during replay), and frame 762 totals 13.7964 ms (10.5929 before replay, 3.1931 during replay). These measurements do not establish a polling-only root cause. This change reduces submitted work; it does not bypass EVENT completion or claim those waits eliminated.

## Change

The canonical scene store already packs several meshes into one VB/IB. Previously each mesh retained relative indices and required a different BaseVertexIndex, preventing compatible draws from sharing one submission.

Canonical single-stream batches now rebase valid indices during their existing cold upload. Draws use base zero and the matching minimum-vertex window. Index bounds, backing vertex bytes and the device's MaxVertexIndex are checked; unsupported layouts, malformed ranges and failed capability queries retain the relative-index path. CPU source meshes, GPU buffer sizes, upload admission and cache lifetime remain unchanged. Legacy and separate buffers keep their existing placement convention.

ModelNormals coalesces adjacent triangle-list packets only when their index ranges are contiguous and all consumed state matches: VB/IB, stride, declaration, viewport, raster state, shaders, captured float/int/bool constants and sampled textures/samplers. The primitive limit and vertex-window overflow are checked. Triangle order, poses, cutout coverage, target format and resolution are preserved. No sorting or new GPU buffers are introduced. Existing point-shadow run merging can also use the common base across compatible canonical meshes. Material replay consumes the same placement helpers.

`MODEL_NORMALS` retains its logical packet counts and adds `mergedDraws` and `submittedDraws`, so the next game run can show the actual reduction rather than extrapolating from an authored workload.

## Validation and limits

- Canonical CPU fixtures pass optimized and ASan/UBSan runs, including capability failure, restricted index limits, invalid indices, cache pressure, reconstruction and allocation/lock/unlock failures.
- Canonical real-GPU MRT/depth images match the old layout on DXVK 1.10.3, 2.7.1 and 3.1.1, including MSAA 0/4/8 and cutout coverage.
- All eight stock VS2/VS3 rigid/skinned/weighted/weighted4 programs pass the normal capture, alpha, source mutation, lifetime and state oracles on all three backends. A real M2 split into 16 adjacent draws becomes one draw with bit-identical normal images. Twenty-three state/range changes independently stop merging.
- The extracted production world binding passes optimized and sanitizer checks. All four replay upload variants pass optimized and sanitizer checks, including canonical absolute-index residency, bulk fallback, recovery and effective-address verification.
- Point-shadow cache visibility/recovery is checked separately. Final counts and delivery identity are recorded in the validation receipts.

The 16→1 result describes compatible fixture packets, not a whole-scene speedup. No new gameplay FPS measurement is available. The real r45 reduction must be read from `submittedDraws`, point replay counters and subsequent frame timings. Windows DXVK 1 testing does not validate macOS/MoltenVK. AO, shader output, draw coverage and visual quality are unchanged.
