# Rotation performance, cache retention and upstream synchronization

This report compares the user's 23 October run of `cc69996` with the subsequent optimization candidate. The game is not launched by validation. Offline component timings are not measured game FPS improvements. Installed DLLs, configuration, addons and world-cache assets remain untouched.

## Evidence from the fresh run

The installed DLL SHA-256 matches the previous delivery: `5cc0b3fc5ece8f2400fef27eefb5349a85b9289d2e4150263310f8b466fb6d4d`. The frozen renderer log has 43 geometry builds. Material settings change during the run, so material-enabled and disabled samples must not be combined into a controlled FPS comparison.

| Measured stage | Observed cost | Interpretation |
| --- | --- | --- |
| Geometry worker build | Median 360 ms; maximum 516 ms | Asynchronous latency and CPU contention, not 360 ms added to every frame |
| Shadow planning | Median 123.358 ms per geometry build | Largest measured worker phase; coarse source membership invalidates repeated preparation |
| Local scene preparation | Median 105.223 ms per geometry build | Includes repeated transformation of shared vertex positions |
| Far terrain loading | Median 68.937 ms per geometry build | Source decode and exact crop/assembly remain significant |
| Page preparation | Median 64.965 ms per geometry build | No page memo hits in this run |
| Foreground terrain capture | Median 0.306 ms; sampled maximum 9.919 ms | Cold/evicted working sets cause spikes; warm captures are much cheaper |
| GPU effects interval | Median 2.852 ms | FogMarch 0.912 ms and FogClouds 0.681 ms are larger than AO 0.1025 ms; these interval medians are not an additive frame budget |

Material-disabled frame 2921 still spends 8.427 ms in terrain capture and 9.299 ms in total world capture. Materials therefore cannot explain all rotation stalls. Later scene frames also report large producer-empty queue intervals, including 46.161 ms at frame 3315. Earlier 400–500 ms waits occur before valid scene measurements and are excluded from claims about rotation-stage costs.

The material GPU cache reaches its 4,096-entry limit with very few resident meshes: frame 1651 has 160 resident and 3,936 probation entries, with 249 evictions. Count pressure was able to evict already uploaded geometry while unused probation records remained. The normal-map generation and shader-cache counters do not establish continuous regeneration as the main cost. Cavity and self-shadowing are disabled in this captured configuration, so this run cannot verify their visible contribution.

## Changes and proof

### Preserve resident material geometry under count pressure

An intrusive probation queue lets count-only admission retire an unused probation record before a resident mesh. Byte pressure keeps the original global LRU policy. Current-frame geometry remains protected; upload limits, cache count and byte caps are unchanged. Both queue links are charged inside the existing byte cap. Logs distinguish resident/probation evictions, count/byte pressure and queue metadata bytes.

The frozen `cc69996` 4,096-entry pressure reproduction preserves all 160 resident meshes. Return warmups drop from 160 to zero, avoiding 10,500,480 bytes of UP submissions and subsequent uploads in that fixture. The policy CPU benchmark falls from 5.49005 to 4.16975 ms, a 24.05% component reduction. Fully resident binding changes from 0.99890 to 1.00955 ms, a 0.01065 ms increase within overlapping sample ranges. These are fake-D3D policy measurements, not GPU execution or whole-frame speedups.

### Reuse validated terrain inputs

The terrain remap already validates every referenced source index and every unique finite XYZ position. Its subsequent bounds classifier now reuses that proof instead of rechecking the same shared vertices for every triangle. Public classification entry points retain their original checks. The frozen full-capture oracle compares exact positions, indices, bounds, chunk coverage and rejection reasons for indexed and user-pointer inputs, including invalid indices, NaN/Inf, degenerate geometry and both primitive topologies.

The existing retention regression also exposes a real working-set regression: the shared source identity overhead leaves only 4,171 of 4,200 two-chunk draws within 32 MiB, producing 232 warm misses and 2,324,640 reread bytes. Fourteen 32-bit draw-key fields are now stored losslessly in seven 64-bit words. Lifetime tokens, revisions and provider addresses keep their full width. The exact contract is reversible and saves 56 bytes per entry; no cache limit or comparison field is removed. The unchanged regression now retains all 4,200 draws in 33,551,112 bytes, with 8,400 warm hits and zero misses, rereads or evictions.

Across nine alternating baseline/candidate runs, the full cold-capture fixture improves from 29.8086 to 28.0897 ms (5.77%). Read bytes, capture count and geometry checksums are identical. This measurement includes both validation reuse and compact identity creation.

### Reuse geometry and shadow preparation

The loader reuses exact transformed positions already present in the accepted vertex remap. Far terrain provenance groups adjacent accepted triangles by the existing four-by-four terrain-cell grid. Exact original triangle membership and placement identify each range; local complete-chunk BLAS proofs are unchanged. Optional provenance allocation failure retains the original geometry path.

The first per-MCNK candidate matched all geometry/ray/probe outputs but exhausted the existing 8 MiB preparation allowance and regressed total CPU time. Testing the larger distances found the underlying waste: every small prepared range reserved space for at least 64 records, often retaining only one to eight. Geometry-only prepared ranges now start at one record and grow normally; ordinary full-plan preparation keeps its existing reserve. Actual capacity remains accounted and preflighted. This restores useful reuse at all three distances from the log without increasing memory limits or adding an adaptive quality mode.

| Reach | Orbit preparation + bake, old → new | Reduction | Orbit shadow plan, old → new |
| --- | ---: | ---: | ---: |
| 928 yd | 359.587 → 325.471 ms | 9.49% | 82.591 → 48.968 ms |
| 1,856 yd | 464.306 → 413.565 ms | 10.93% | 121.991 → 69.196 ms |
| 2,048 yd | 478.233 → 427.086 ms | 10.70% | 140.156 → 71.565 ms |

These are medians from three alternating runs at each reach, compared to the genuine shipped `cc69996` shared pipeline. All 63 region comparisons match nine canonical outputs, including scene/material data, plan/page signatures, 516,096 trace/occlusion queries per side and each complete 5,600-probe, 64-ray, three-bounce bake. This changing-coverage fixture is not an in-game yaw/FPS measurement.

Tradeoffs remain: cold preparation+bake costs increase by 1.42%, 3.93% and 2.27%; unchanged-region costs increase by 1.61%, 4.53% and 7.00%. The intended benefit is reuse while coverage changes. Peak preparation charges remain at or below 8 MiB at every tested distance. Debug and optimized tests also cover 500 loader and 500 preparation allocation-fault sites, 149 denial cases, exact fallback output and retry recovery per configuration.

## Validation and synchronization status

The full suite also found a stale production cost model for local lights. The compiled eight- and sixteen-light shaders use 253 and 373 instruction slots, while the planner still estimated 244 and 364. The corrected model chooses one sixteen-light pass in a boundary case where the old estimates chose two eight-light passes. The adjacent rejection case remains covered. Light count, shader programs, scissor coverage and output quality are unchanged; this is a scheduling correction, not a measured game-FPS gain.

Test maintenance is recorded separately from performance work. Initial complete Windows and native Linux runs exposed outdated source anchors, a historical draw oracle predating the debug sky policy, obsolete private-cache assumptions, an outdated shader comparison fixture and an invalid assumption that preparation metadata must outweigh the old allocation reserve. Repairs retain positive image checks, exact failure/state traces, collision/identity and memory accounting, historical shader guards and real sanitizer runs. The shader baseline was regenerated with the recorded Microsoft compiler rather than accepting a new source hash alone. Twelve GPU comparisons pass with maximum error 1.79e-7; 594 FogTemporal comparisons are bit exact. The material fixture passes 274 cases, including visible cavity/sun/moon channels, and the local-shadow fixture passes 144 cases.

The live `shadow-range-profiles.ini` differs from the tracked profile exactly as it did at `cc69996`: the user's Elwynn override is absent locally. The profile-copy equality test therefore fails independently of these changes. Personal settings and published defaults are preserved. The exact pinned StormLib source tree and a historical camera capture archive are unavailable; these prerequisites remain explicit coverage gaps. The game and active-HDR/in-game appearance are not certified by offscreen tests.

Work in progress: targeted differential checks precede a full Windows/WSL test gate. Latest upstream commits will be fetched and merged only after that first gate, followed by the same full coverage again. Existing PRs are being checked for actual ancestry and unique patches before regrouping. No force push or upstream-main merge is planned. Water/SSR PR #93 remains closed and withdrawn.

Detailed raw evidence is retained under `northlight-diagnostics/log-opt-sync-20261023`, including frozen input hashes, log rows, successful and failed attempts, component reports and test manifests. Final DLL hashes, exact test coverage, limitations and PR heads will be added after both gates complete.
