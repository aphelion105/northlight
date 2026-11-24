# 60 additional Northlight CPU optimizations

Series 2026-11-02 / 2026-11-03. These are separate 60 changes on the surface of the previous series; the reference state is kept in `.local/out/cpu60-followup-20261102/baseline/src`. Changed 13 reference files. Full ledger from SHA-256 to/after: `.local/out/cpu60-followup-20261102/optimizations.json`.

## Checks

General run: **256 PASS, 1 FAIL, 225 SKIP**, without timeout. The only FAIL is `test_world_screen_effects_gpu.py`, assert anggregate admission under the artificial restriction VA. The same binarium was run separately by MSAA 0 and 2; then a permanent isolated reassembly run passed **1 PASS, 0 FAIL**. The cause of the first non-permanent malfunction is not proven; the reference FAIL is saved. The result for the 257 available repeat tests is: **257 PASS**. SKIP is not considered to be successful tests.

Official results: `.local/out/test-output/20261102-234143/results.tsv` and `.local/out/test-output/20261102-235644/results.tsv`. Additional repetition of the same GPU binary: `.local/out/cpu60-followup-20261102/gpu-screen-isolated-2/results.json`.

All eight GPUs that previously demanded a closed game are now checked: AO actor, AO pipeline, AO quality, HDR diffusion, projected draw audition, settings overlay, world lighting culture, world screen effects. During the final run, Wow was closed by the user.

Additionally performed with non O0/O2 included assert for dynamic/moving probes, complete and built constant banks, pose groups, actor testes, partial lights, world GI and mesh plan. carry-over actor deformation/scene job, texture worker and mesh pages are done through Zig; texture worker and paging have passed ASan/UBSan. These are separate tests over prerequisities normal runner.

The new registered `tests/test_cpu_optimizations.py` checks all 65 536 half values of bit-beat; all 65 536 values of four 16-bit formats; unaligned/padded rows and SIMD tails; BC oracle for archival decoder, uniform/mixed secetors, alpha and block summs; page evaluation, blockboards, shrink/regrow, Aliasing and IndexMap epochs.

Client building: 5 594 Shader, 5 136 supported programs, 738 304 comparisons, including Strict, errors and aliasing. Separately, 432 constant actor sequences, exact vertices, normals, UV, materials, hash and Alpha BVH. After the final adjustment, GI is repeated O0/O2 dynamic/moving regressions; after inline differencing is repeated O0/O2 constant/pose regresions.

## CPU A/B

The Median of the nine alternate rounds in one native stand; the reference points before this series and the current ones are collected by one Zig 0.15.2/O2. The production SSE2 memcmp is used. This is the time of the components that are not game FPS; small changes about percent do not support a steady acceleration.

| Scenario | Do, ms | After, ms. | Change |
|---|---:|---:|---:|
| chunked_resize_assign_copy | 289.572800 | 16.074800 | -94.45% |
| spooled_push_read_resize | 9.099600 | 7.994900 | -12.14% |
| full_float_registers | 25.619000 | 24.751200 | -3.39% |
| bounded_float_registers | 36.708200 | 27.516700 | -25.04% |
| projected_overlays | 28.486700 | 28.361800 | -0.44% |
| RGB565 | 36.449200 | 4.599200 | -87.38% |
| ARGB1555 | 41.852300 | 4.804600 | -88.52% |
| XRGB1555 | 37.538900 | 4.359300 | -88.39% |
| ARGB4444 | 36.908800 | 4.308400 | -88.33% |
| dynamic_candidates_and_reuse | 6.730600 | 6.495700 | -3.49% |

The first version of the GI filter has been slower and removed: the final code retains a more favourable sorting of small arrays, while the big ones limit it through nth_element and the sort of the prefix. The final dimension log: `.local/out/cpu60-followup-20261102/benchmark-final.log`; playback is `build_benchmark.py` and `benchmark.cpp` in the same folder.

## Register of changes 60

| № | File | Shortened CPU-work | Checks |
|---:|---|---|---|
| 1 | `src/gi/temporary_geometry.h` | Re-access to the current disk page passes through the recent-slot without scanning four slots. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 2 | `src/gi/temporary_geometry.h` | The beaver for the existing disk page is unfiltered before full reading. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 3 | `src/gi/temporary_geometry.h` | A new disk page is placed in a cache without rereading the newly recorded bytes. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 4 | `src/gi/temporary_geometry.h` | The growth of the disk array fills out page ranges instead of push_back per element. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 5 | `src/gi/temporary_geometry.h` | Reading/recording the zero range preserves the validation of the boundaries and skips the seek. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 6 | `src/gi/temporary_geometry.h` | The new page is posted once in a resident book; the file record and resize use the same finished data. | `test_temporary_geometry.py`, `test_cpu_optimizations.py` |
| 7 | `src/gi/adaptive_geometry_array.h` | Copying the chunked mass is using block ranges instead of push_back each element. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 8 | `src/gi/adaptive_geometry_array.h` | Flat -> chunks transfer ranges, reducing the elemental targeting. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 9 | `src/gi/adaptive_geometry_array.h` | Chunked resize fills the continuous remainder of each block. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 10 | `src/gi/adaptive_geometry_array.h` | Chunked assign fills the blocks; the value is saved in advance for aliasing. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 11 | `src/gi/adaptive_geometry_array.h` | Append ranges of forward peterators copy blocks, including chunked sources; input interators retain a flow path. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 12 | `src/gi/adaptive_geometry_array.h` | For arithmetical neural blocks, the already zero memory of VirtualAlloc is used without re-value-initialize. | `test_temporary_geometry.py`, `test_cpu_optimizations.py`, `test_world_mesh_pages.py` |
| 13 | `src/gi/world_dynamic_probes.h` | Two booms re-use capability between observations instead of re-allocation. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 14 | `src/gi/world_dynamic_probes.h` | The empty material boards are removed from the list for which spatial requests are made. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 15 | `src/gi/world_dynamic_probes.h` | The search for a minimum distance ends at zero distance. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 16 | `src/gi/world_dynamic_probes.h` | Full weight inside 12 yards returns without sqrt. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 17 | `src/gi/world_dynamic_probes.h` | The result of the selected solve is recorded through the found Solved; new emplace notes do not create or wipe an empty Probe. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 18 | `src/gi/world_dynamic_probes.h` | The mass of candidates is immediately reserved until the background candidate seiling, without an initial growth chain. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 19 | `src/gi/world_dynamic_probes.h` | If there are more than two candidates, only the right prefix is sorted; smaller arrays retain a more profitable full sort. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 20 | `src/gi/world_dynamic_probes.h` | The mass mismatched is pre-reserved to the number of candidates selected. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 21 | `src/gi/world_dynamic_probes.h` | "Cached-only" does not build a pool of candidates on solve. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 22 | `src/gi/world_dynamic_probes.h` | Sorting the age of Mismatched is missed after the solve budget has been exhausted. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 23 | `src/gi/world_dynamic_probes.h` | The candidate maintains a stable index unordered_map on Solved, excluding re-search at reuse/stale blending. | `test_world_dynamic_probes.cpp`, `test_world_moving_replay.cpp` |
| 24 | `src/replay/replay_constants.h` | knownFloatSpan checks the extreme registers before comparing the entire palette with a knownly different edge. | `test_replay_constants.cpp`, `test_replay_constant_ranges.cpp` |
| 25 | `src/replay/replay_constants.h` | A complete float-bank uses bulk/chunk span disferencing instead of a SSE-comparison of each register one by one; hot helper is incorporated into the code that causes it. | `test_replay_constants.cpp`, `test_replay_constant_ranges.cpp` |
| 26 | `src/replay/replay_constant_ranges.h` | A fully known float band uses bulk/chunk search for irty span. | `test_replay_constant_ranges.cpp`, `test_replay_pose_groups.cpp` |
| 27 | `src/replay/replay_constant_ranges.h` | The first update of the bank after reset immediately replicates the range used without per-register validity checks. | `test_replay_constant_ranges.cpp`, `test_replay_pose_groups.cpp` |
| 28 | `src/replay/replay_constant_ranges.h` | The first projected overlay replicates three segments without looking for differences in an unknown bank. | `test_replay_constant_ranges.cpp`, `test_replay_pose_groups.cpp` |
| 29 | `src/replay/replay_constant_ranges.h` | The bank's full coverage is marked allKnown without bypassing the words validity mask. | `test_replay_constant_ranges.cpp`, `test_replay_pose_groups.cpp` |
| 30 | `src/replay/replay_pose_groups.h` | For identical indicators of Captured float/bool/int banks are not re-comparisoned; ranges checks are maintained. | `test_replay_pose_groups.cpp` |
| 31 | `src/lights/particle_lights.h` | The new sample to the camera is reused when refilling clusters. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 32 | `src/lights/particle_lights.h` | The already busy trackers are missing before they can figure out the distance. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 33 | `src/lights/particle_lights.h` | The mass of candidates does not wear off the unused Light; each element read is recorded in its entirety. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 34 | `src/lights/particle_lights.h` | Only the prefix used from a maximum of 24 shall be sorted to produce eight lights. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 35 | `src/lights/particle_lights.h` | The history of the previous publication is stored only by sourceId instead of full 48- byte Light. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 36 | `src/lights/particle_lights.h` | The story of the particle IDs is copied from an already built immediate without repeated membership scans. | `test_particle_lights.cpp`, `test_particle_lights.py` |
| 37 | `src/replay/actor_texture.h` | BC units with single endpoint devices do not calculate unused interpolated RGB entries. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 38 | `src/replay/actor_texture.h` | BC3 with zero alpha sectors does not build an interpolated alpha palette; block sum receives alpha0*16. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 39 | `src/replay/actor_texture.h` | The single-color selictor in the full BC block gives RGB subs by direct multiplying instead of bypassing four colors. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 40 | `src/replay/actor_texture.h` | RGB565 decodes SSE2 with eight pixels. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 41 | `src/replay/actor_texture.h` | ARGB1555/XRGB1555 Decode SSE2 with eight pixels with exact alpha semantics. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 42 | `src/replay/actor_texture.h` | ARGB44444 decodes SSE2 with eight pixels. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 43 | `src/replay/actor_texture.h` | The solid 16- bit lines are combined in one linear range, without separate SIMD tails per line. | `test_cpu_optimizations.py`, `test_actor_texture_worker.py`, `test_actor_texture.cpp` |
| 44 | `src/replay/actor_deformation.h` | Kernel ops is reserved for a known number of transactions in the compilation. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 45 | `src/replay/actor_deformation.h` | The analysis of the recorded components excludes the defusing of temp registers that are fully recorded before first reading. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 46 | `src/replay/actor_deformation.h` | Normal half is converted through precise IEEE-bits without ldexp; special values remain the same. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 47 | `src/replay/actor_deformation.h` | Whited skinning takes the only literal c0 directly from findings without reading the API bank. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 48 | `src/replay/actor_deformation.h` | Single skinning with the only 4.0 definition doesn't scan it for every string of palette. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 49 | `src/replay/actor_deformation.h` | Identity swizzle without a modification copys float4 without four dynamic indices. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 50 | `src/replay/actor_deformation.h` | Broadcast swizzle reads and modifys one component, then fills the float4. | `test_actor_pose_kernels.py`, `test_actor_deformation.py`, `test_cpu_optimizations.py` |
| 51 | `src/gi/world_gi.h` | ===TexelView===TexelView does not compare the entire texels array with a common index. | `test_world_gi.cpp`, `test_shared_mesh_preparation.py` |
| 52 | `src/gi/world_gi.cpp` | Median BVH leaf does not compute centroid banks that are not used without separation. | `test_world_gi.cpp`, `test_world_mesh_pages.py`, `test_shared_mesh_preparation.py` |
| 53 | `src/gi/world_gi.cpp` | SAH Binning pre-selects the active axes instead of repeated axis-policy checks for each triangle. | `test_world_gi.cpp`, `test_world_mesh_pages.py`, `test_shared_mesh_preparation.py` |
| 54 | `src/gi/world_gi.cpp` | Wrap addissing for power-of-two size uses the mask instead of integer modulo. | `test_world_gi.cpp`, `test_world_mesh_pages.py`, `test_shared_mesh_preparation.py` |
| 55 | `src/gi/world_gi.cpp` | sRGB lookup table is requested once on a billinear sample instead of four hits to the static guard. | `test_world_gi.cpp`, `test_world_mesh_pages.py`, `test_shared_mesh_preparation.py` |
| 56 | `src/world/world_mesh_plan.cpp` | Prepared by the immutable material identities are borrowed without extracting and copying the second vector. | `test_world_mesh_plan.cpp`, `test_shared_mesh_preparation.py`, `test_world_mesh_pages.py` |
| 57 | `src/world/world_mesh_plan.cpp` | Both scratch hash tables (Content calls and groups) limit the actual number of triangles. | `test_world_mesh_plan.cpp`, `test_shared_mesh_preparation.py`, `test_world_mesh_pages.py` |
| 58 | `src/world/world_mesh_pages.h` | Content anchor hashs already copied page vertices instead of three re-calls to reference, including disk geometry. | `test_world_mesh_pages.py`, `test_cpu_optimizations.py`, `test_shared_mesh_preparation.py` |
| 59 | `src/world/world_mesh_pages.h` | Batch preflight doesn't look for missaning vertices in remap when the page is known to be reopened. | `test_world_mesh_pages.py`, `test_cpu_optimizations.py`, `test_shared_mesh_preparation.py` |
| 60 | `src/world/world_mesh_pages.h` | IndexMap memorizes the last hash slot, repeated find/insert same key does not repeat probing; clear is disabled shortcut. | `test_world_mesh_pages.py`, `test_cpu_optimizations.py`, `test_shared_mesh_preparation.py` |

## Compilation

Production DLL: `renderer/frd9.dll`, release 0.3.216, x86 PE proxie, 9,089,024 bytes. Collected by the regular `python scripts/build_renderer.py`, Zig 0.15.2, with the constant Shader freshness gates; `build_packages.check_dll` passed. DLL was not installed in the game.

Source identity: `7cae1d5ab9da4c8f6ad8af8d5eba91c567a81c9f8ecb9fed9cce0d77052a0c30`.

DLL SHA-256: `d5b45f0c1fbb35d7dcda04c20cef1d87b06263d9c222b05d0444f6cdb5d10c65`.

The machine readable sum: `.local/out/cpu60-followup-20261102/validation.json`.

The GI booms maintain the capacity of two observations to reduce allocations; they are included in memory accounting. Geometry restrictions, solve budget, candidate seiling, pixel visibility and format have not diminished.
