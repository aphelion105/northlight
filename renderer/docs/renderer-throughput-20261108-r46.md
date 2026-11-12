# r46: cold subdivision and snapshot cache allocations

The frozen input is `out/perf-20261108-r46/game.log`: an r45 run, source `177a356c4be530da5e76bf9a973d499f3c16f2554b25120b7387aad9a9e548e8`, DXVK 3.1.1, RTX 4090, 3840×2160 HDR, MSAA 0. DXVK 1/2 are validated offline; this game log does not measure their FPS.

## What the game log establishes

The last three world intervals average 24.499, 36.031 and 29.052 ms (40.82, 27.75 and 34.42 FPS). Login/loading intervals are excluded from this comparison.

The r45 merge path is active: frame 635 submits 544 ModelNormals draws for 819 packets (275 merged); frame 762 submits 417 for 597 packets (180 merged). This is an observed command reduction, not an isolated FPS comparison.

Frame 635 reports 18.547 ms renderer CPU time: 8.276 ms capture, 5.749 ms effects and 3.831 ms cleanup, plus the draw gate. Its measured indexed-draw command subset costs 14.0495 ms across 2,330 calls. Material subdivision alone costs 1.962 ms for two cold builds. Sampled GPU intervals are 16.073 ms GameScene, 3.763 ms ModelNormals and 4.082 ms effects. GPU timestamp intervals can include starvation between submissions and are not isolated shader execution costs; overlapping CPU/GPU measurements must not be added.

Frame 825 has a directly measured 27.033 ms memory-guard boundary. The trim itself takes 21.824 ms: CPU snapshots account for 12.934 ms, idle replay records for 3.730 ms and material cleanup for 3.208 ms. The nearby EVENT at game frame 827 totals 53.6827 ms, of which 49.2670 ms is before replay and 4.4057 ms is during replay. This demonstrates a substantial CPU cleanup stall; it does not assign all 49.2670 ms to that one trim.

## Shipped changes

1. Subdivision decodes and validates each referenced skin input once per build, instead of repeating it for generated vertices. It reuses the existing decoded-attribute storage, caches bone-ID rounding and calculates boundary-edge flags once per triangle. The interpolation and accumulation order, output indices, skin weights, original corners, cutout geometry and failure checks are preserved. Unreferenced invalid skin data remains irrelevant.
2. Shared snapshot entries live directly in `unordered_map` nodes. This removes the separate heap allocation and pointer indirection for each entry. Node addresses remain stable across rehashes, preserving the intrusive LRU, alias lookup and prediction. Declaration ownership transfers explicitly when an entry moves into its node. Identity/revision checks, cache limits, payload ownership and critical cleanup policy are unchanged.

Both changes are in the common CPU capture/preparation path used by DXVK 1/2/3. No shader, AO, target resolution, geometry coverage or shadow distance changes are included.

## Component measurements

Against the frozen r45 subdivision implementation, paired CPU measurements give:

| Workload | r45, ms | r46, ms | Ratio |
| --- | ---: | ---: | ---: |
| Model | 4.721 | 3.555 | 0.753 |
| WMO grid | 2.716 | 2.708 | 0.997 |
| WMO seams | 3.917 | 3.810 | 0.973 |

The clear saving is in model preparation, about 24.7% in this authored component workload. These are not gameplay FPS measurements. Grid differences are small and should not be treated as a reliable speedup.

For 3,800 snapshot entries, allocation calls fall from 19,012 to 15,212. Measured population time is essentially unchanged (30.842 versus 30.873 ms); clear time changes from 18.467 to 18.168 ms. The allocation reduction is deterministic; the small timing difference does not establish elimination of the game cleanup spike.

A separate experiment overlapped critical CPU destruction with owner-thread cleanup and joined before returning. It measured 13.699 versus 13.618 ms for serial cleanup, so it was rejected. Its retirement implementation and policy changes are absent from the production DLL. The existing mandatory synchronous critical clear remains in place.

## Validation

- Optimized and ASan/UBSan CPU oracles cover snapshot key equality, lookup/prediction/LRU lifetime, shared sources, declaration ownership, allocation failure, retirement and reconstruction.
- Subdivision compares 301 cases and 75,756,872 bytes against frozen r45, including float bone-ID tolerance and unreferenced malformed skin input. Output is byte-exact.
- A destructive declaration-move control is rejected by the ownership oracle.
- Actual material pressure/recovery checks and the memory-policy/order and upload-lock audits pass.
- DXVK 1.10.3, 2.7.1 and 3.1.1 pass canonical MRT/depth/cutout tests including MSAA 0/4/8, all 24 stock ModelNormals shader cases, and point-shadow visibility/recovery checks (2,034 faces plus 8,136 recovery faces per backend).

Final source hashes, build/export checks, real EVENT smoke results and the DLL identity are recorded under `out/perf-20261108-r46` and `out/dll-20261108-r46`. GPU checks cover correctness; no new r46 game FPS or EVENT-latency result is claimed. Material setup/flush intervals and the remaining large synchronous cleanup remain priorities for subsequent profiling.
