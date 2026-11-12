# Renderer throughput r50 РІР‚вЂќ 2026-11-08

## Latest gameplay evidence

Input: `D:/Games/335_engb_mod/northlight-renderer.log`, 1,283,302 bytes, modified 18:58:44 local time. The frozen copy and parsed rows are in `out/perf-20261108-r50`; SHA-256 `027aa439192decbd5465d8de461b77425569b4ee2da1c5ee72175f2c80d8b5d3`. This is the r49 DLL on DXVK 3.1.1.

The low-FPS section is already warm. At frames 1143, 1270, 1524 and 1651, every accepted ModelNormals draw is GPU-resident; UP bytes and streamed draws are zero. At frame 1270, the shared GPU scene charges 81,186,764 bytes against 96 MiB, and the source pool uses about 93 MB against 112 MiB. There is no memory-guard trim transition in this log. Increasing the cache allowance does not address the observed steady-frame cost.

| CPU sample | Total extension | Capture | Effects | Gate | Cleanup |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1270 | 9.354 ms | 6.118 ms | 1.799 ms | 0.814 ms | 0.624 ms |
| 1524 | 8.344 ms | 6.060 ms | 1.028 ms | 0.586 ms | 0.670 ms |
| 1651 | 3.958 ms | 1.841 ms | 1.016 ms | 0.561 ms | 0.540 ms |

Material profiling at frame 1270 reports 2,309 pose-envelope evaluations taking 1.3066 ms; snapshot processing costs 0.5499 ms for 1,604 calls, with zero actual source-read bytes. The 33,797,016 snapshot bytes are logical data served from retained sources, not that much fresh copying/uploading. Capture setup costs 0.7797 ms for 1,603 calls, and native draw work in that material profile costs 1.5684 ms. There is no new subdivision/upload work in that sample.

| GPU sample | Total | GameScene | Final ModelNormals flush | Effects | Remaining tail |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1143 | 30.024 ms | 18.718 ms | 4.357 ms | 4.647 ms | 2.274 ms |
| 1270 | 22.608 ms | 16.374 ms | 1.736 ms | 2.302 ms | 2.170 ms |
| 1524 | 20.607 ms | 15.105 ms | 0.783 ms | 1.967 ms | 2.726 ms |
| 1651 | 14.801 ms | 11.138 ms | 0.839 ms | 0.697 ms | 2.101 ms |

Earlier normal/material replays are inside GameScene. The final ModelNormals bucket is not the entire capture GPU cost; the capped microprofile does not cover every draw. Sequential option changes also change the workload, so these samples do not isolate an effect's causal FPS cost.

The startup EVENT reaches 193.936 ms (168.936 ms before replay and 24.980 ms replay), with additional early 50РІР‚вЂњ66 ms waits. These occur in early frames with the interval focus flag at zero. That flag records any focus loss within its window; it does not prove that the whole interval ran in the background. These samples do not isolate the steady-frame cost, and r50 does not claim to eliminate them or turn the whole run into a steady 60 FPS.

## Changes

1. **Remove discarded skin-bound arithmetic.** Aligned skin bounds compose the inverse camera basis with each bone before taking extrema. The previous implementation also computed a view-axis box and its interval representation, then discarded both. A compile-time specialization retains the exact world-bound arithmetic, numeric guards, outward rounding and cancellation checks while removing that unused work. The public call selects the mode once; no backend-specific rendering path is added.
2. **Share prepared program keys.** Entries for immutable sources retain the prepared program/declaration key instead of allocating and copying it per source. Lookup skips a word comparison only when the shared immutable key has identical pointer identity; independently prepared or generic calls still compare all words, followed by the existing source-identity/control-block proof. Retention does not depend on the lifetime of the prepared metadata cache. New diagnostics expose shared-key hits and fallback comparison bytes.
3. **Centralize resident-cache pressure policy.** The device, world and material paths no longer infer shared policy from a sentinel limit. The existing shared Scene defines the owner/aggregate limits for all backends and standalone consumers: normally 32 MiB material + 64 MiB shadow, demand 32 + 32 MiB, and critical 32 MiB combined. The CPU pool reductions and synchronous critical trim remain. This replaces duplicated policy/critical-zero branching without increasing aggregate allowance.

The 8 MiB / 4,096-entry envelope cap remains conservative: each entry is charged for the full key even when sharing it. Actual allocations fall, but this does not claim more entries fit the charged budget. No extra cache index or refcount-accounting table was introduced.

## Component measurements

Against frozen r49 headers, aligned evaluation over 256 bounds per simulated frame changes from 0.221517 ms to 0.192067 ms (13.3% less). The legacy mode changes from 0.065783 to 0.065859 ms (effectively unchanged). These are component medians over nine alternating rounds; they do not establish a gameplay FPS increase.

For 512 immutable sources sharing one stock shader, allocation requests fall from 3,081 to 2,569 and requested allocation bytes from 1,663,136 to 1,269,920 (512 allocations and 384 KiB removed). Conservative charged bytes change from 1,777,664 to 1,796,096. The complete envelope component's one-run timing changes from 0.202055 to 0.182877 ms; the allocation counts and exact output are the stronger evidence.

## Verification

Validation is performed against frozen production source hashes, with assertions enabled. The skin kernel compares both modes to r49 across 28,672 authored cases, including invalid inputs and stop callbacks; independent deformed vertices also must lie inside the computed bounds. Shared-key tests cover prepared-key eviction, independent equivalent keys, changed programs, full clear and sanitizer checks. Material pose/current-palette tests and actual pressure transitions cover ownership, CPU reclamation, caps and recovery. GPU regressions exercise DXVK 1.10.3, 2.7.1 and 3.1.1.

The existing pose-reuse fixture needed its generated shader header/include path restored and its independent oracle switched to the production aligned mode. The world-pressure fixture now extracts the centralized Scene policy as well as the world boundary methods. These fixture repairs do not relax enclosure, exact-output, allocation-free or COM-thread assertions.

Final receipts and the production DLL are recorded after validation. AO, precision, render resolution, effect strengths and output shaders are unchanged in r50. This Windows component matrix does not establish macOS performance parity.

Completed checks: O2 and ASan/UBSan kernel/key lifetime comparisons; six independent 50,400-vertex safety runs; current-pose and weighted-byte controls; 2,400 metadata/pose comparisons per checked mode; actual material/world pressure and recovery; canonical CPU and world binding; replay upload/lock audit; 24 stream cases with failure controls; 24 standard normal cases; MRT/MSAA, constrained aggregate residency and retained-geometry pixel oracles; local-shadow visibility/recovery on all three DXVK backends.

Production DLL: `out/dll-20261108-r50/d3d9.dll`, 10614272 bytes; SHA-256 `10466b217f10a990c69725470ea17ffa0427669911459675ca5df03eccd470bc`; source identity `0f567a79c9c895539594d21508827f6e5ce37b5c3d9c74f17d0e8ea202021323`, built 2026-11-08 17:57:30 UTC. Compiler preflight, shader freshness, PE32/export validation, final source hashes and 192 real EVENT completions through the final command-stream proxy pass. The installed client is unchanged; no installer was built.
