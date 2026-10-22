# Northlight material-Off rotation: measured cold work and cache duplication

Investigation: 23 October 2026. Input: complete user run ending 22 October at 23:49. Baseline: `bc382d9`. No game was launched by the agents.

## Findings and retained changes

The new instrumentation proves an additional material-specific cost even while its master switch is Off: **recognized vertex shaders still eagerly created patched capture/displacement variants**. The log contains 37 registrations in 15 event batches, totaling 45.1834 ms CPU including startup. The largest batch costs **16.0119 ms for 12 sources**. This is distinct from material capture, which remains disabled correctly.

The build now retains bounded immutable shader metadata while the master/domain is inactive and creates those variants only when an enabled material consumer actually needs them. The first enabled draw synchronously receives the same variants; there is no quality-reducing deferred draw. Original shader identity/lifetime, failed creation, reset and the 4,096-entry cap are explicitly tested, including recovery when unsupported pending shaders fill the registry.

Two other exact changes are retained:

- **Terrain directional index aliasing:** when validated fixed-chunk membership proves that the complete point index list is also the directional list, reuse that immutable CPU storage. Mixed/empty/unknown metadata keeps the original filtering. The GPU bytes, order, indices, coverage and fences remain unchanged. A 100-generation fixture improves prepare/write from **223.840 to 25.345 ms total**, about **2.238 to 0.253 ms per generation**. These are component timings, not game FPS.
- **FogTemporal equal-history shortcut:** after all original reprojection/depth checks, equal current/history values do not need the eight-neighbor clamp. The original `lerp` arithmetic is preserved. Neutral production cases improve about **22.8–27.0%**, saving approximately **0.006–0.007 ms**; mixed cases can cost about **0.0009 ms** more. This is a small secondary saving.

The later rotation slowdown is not fully explained by shader registration. At sampled frames 3937/4064, no cold registration occurs, yet GameScene takes **16.745/17.753 ms GPU elapsed**. Query profiling shows **15.110/16.230 ms before the replay query task starts**, versus **3.112/3.862 ms in the query task** and about 0.016/0.030 ms returning to the game. The queue is waiting behind earlier work. Returning a fabricated completed query would not remove that work safely.

New sampled command attribution identifies the actual queued command categories in the next run. GI actor work is also split into decode, geometry/pose, hash and BVH stages. Actual post-change game FPS remains unmeasured until the user runs this DLL.

## Inputs and fair comparison

Frozen directory: `northlight-diagnostics/rotation-off-20261022-2349/` under the game root. Fresh log: 799,203 bytes, SHA-256 `f535b934ad04a85fea8aa34e64cc1e01953d7564d4abecea952f0e5bf2057993`. Its `.prev.log` is the genuine 23:25 run, SHA-256 `14c972be1289ddf8047a4f18cbde375afbc7504a14584c3375ac20762eeefcda`.

Installed DLL SHA-256: `771b7ccda033c2c8683b827d68c54553934a7f62915fe55a633a425b6571a103`. Quality INI SHA-256: `f37ee1a0729eef29cc8c9404af54a4e195881f62b5d80a77a1fe9d915cc6c78b`, unchanged from the preceding run. All 34 material configuration samples have `enabled=0`, overriding saved modes `3/3/3/0`. Cavity and self-shadow remain disabled.

The preceding 20-byte GPU terrain layout is active (`vertexStride=20`, capacity 838,860 vertices). Rollovers reach 13 rather than the previous run's 68, but camera paths, duration and visible working sets differ: this is not a controlled percentage improvement. The current trace still has index-only generation changes rebuilding 776 lists despite zero new owners, which motivated the new aliasing fix.

After the world is active, a rotation window averages **49.62 FPS**, recovers to **113.46–117.28 FPS**, and a later rotation returns to about **51–59 FPS**. A 607 ms loading outlier and the unfocused early startup windows are separate observations and should not be presented as ordinary rotation costs.

## Fifteen investigations in separate directions

### GI, world and streaming

| Topic | Evidence and result |
| --- | --- |
| 1. Probe-grid motion, retargets and GI solving | After deduplicating partial publications, 95 observed request/generation jobs remain. Warm static grids reuse all 5,600 probes. Forty-two warm full jobs have median actorWork 2.922 ms and solveWork 1.984 ms, with higher late actor batches. These are periodic background spans, not full retraces every render frame. Existing coverage and scheduling are preserved. |
| 2. Static regions/pages and publication | Nineteen geometry builds and 15 commits occur during real camera-eye travel. Existing page reuse is retained; no unsupported stale-region/crop shortcut was introduced. Durations in background phase logs must not be added directly to foreground frame time. |
| 3. Terrain arena/index duplication | The previous compact vertex format is working, but index regeneration still duplicates all-live lists. **Implemented exact CPU index aliasing** with conservative membership proof. Forty rotating frames compare **77,435,904 identical GPU indices**. Actual directional vector builds fall from 28,000 to 6,696; retained index allocation falls from 14,297,088 to 6,887,424 bytes. |
| 4. Actor decode, pose, hash and BVH | Repeated 12-texture actor batches have approximately 4.8–5.5 ms worker preparation and about 2.1–3.2 ms correction work. The old aggregate does not identify the internal cause. Added `actorDecodeMs`, `actorPoseMs`, `actorHashMs`, `actorBvhMs`; clocks occur at packet/stage boundaries, not per vertex/ray. No speculative persistent texture cache was added. |
| 5. Generation publication, waits and contention | Existing generation/retirement checks and partial publication stay intact. Some isolated slow frames have almost entirely idle replay queues; other rotating samples show a large queued-work delay. These are different stall classes. Root command attribution will distinguish queued draw/create/upload work without weakening fences. |

### Materials and capture

| Topic | Evidence and result |
| --- | --- |
| 6. Effective Off and diagnostic/reference overhead | Every configuration is globally Off, and expensive source/pose/generation/capture/replay/application stages are zero. The nativeDraw scope still measures the game's original draw. At frame 4191 material CPU-exclusive attribution is 0.9546 ms, with separate sampled query CPU about 0.3402 ms. This is not evidence of extra material rendering. |
| 7. Recognized shader creation while inactive | **Measured and fixed.** The old register path built variants regardless of the master/domain switch. Pending metadata now holds the immutable source safely; active consumers compile synchronously before drawing. Existing enabled registration remains eager. `vertexQueued` and `pendingVertices` expose the new behavior alongside actual compile counts/time. |
| 8. Primitive/UV gate and map reuse | No enabled-material interval exists here, so this run cannot measure the preceding oversized-mesh gate or normal-generation savings. Its 8,192 boundary, cache-hit precedence and unchanged output remain covered by CPU/GPU regression checks. No invented enabled-mode speedup is claimed. |
| 9. Snapshot, pose, bounds and packet maintenance | All expensive sampled Off counters remain zero. No source-cache expansion, stale animated bound, dropped texture barrier or changed capture state was introduced. The measured cold registration was outside those draw scopes and is now handled separately. |
| 10. Cold lifetime, reset, failure and registry pressure | Added deferred registration identity/refcount/ABA tests, failure caching and authoritative overflow recovery from the existing device shader registry. Current bytecode is revalidated before recovered compilation. Unsupported pending records cannot permanently block supported consumers; compiled entries keep their original cap. Ordinary unknown hot draws do not perform new bytecode reads. |

### AO and postprocessing

| Topic | Evidence and result |
| --- | --- |
| 11. AO setup versus draw | AOBlurSetup rounds to 0.000 ms GPU in all 11 active-world profiles. CPU setup is 0.0004–0.0008 ms; draw submission is normally 0.0004–0.0005 ms. AOBlur is 0.259–0.378 ms GPU. These samples rule out a repeated multi-millisecond AO setup cost; no quality cut was justified. |
| 12. AO generation/denoise/upsampling | All four AO methods retain their existing algorithms, samples, precision and strengths. The measured ordinary AO work does not explain the 16–18 ms GameScene intervals. Existing regression/bytecode remains unchanged. |
| 13. Active-source fog/cloud/shadow costs | Moon lighting remains real work. Most stages are steady or inflate together with broader command/dependency pressure. Removing active source contribution would alter the image and was rejected. |
| 14. World composition, bloom, water and HDR | No new unnecessary full-frame copy was proven. Composition is cheaper, not more expensive, in one worst rotation sample. HDR RGB, formats, water rejection and reconstruction remain unchanged. |
| 15. Temporal history filtering | An isolated FogTemporal sample reaches 0.376 ms against ordinary roughly 0.041 ms; that alone does not prove arithmetic caused the spike. Independently, the equal-history proof removes an unnecessary neighborhood clamp. **648 exact comparisons** pass. Original `lerp` is deliberately retained for signed-zero/nonfinite arithmetic behavior. Mixed-path branch cost is disclosed. |

## Correctness and memory guarantees

Terrain aliasing is internal to the CPU index cache. It retains uint32 GPU output and the original allocation/fence/write/reset behavior. Generation and fixed-chunk changes still invalidate the decision. Unknown/empty/out-of-grid metadata and mixed captures use the original filter. Tests cover reversed/duplicate owners, immutable replacement, map reset, straddlers, missing owners, exceptions, busy/unsupported queries, three rollovers and Lock/Unlock failure recovery. O2/O0 runs preserve all 77.4 million compared indices. Original randomized arena, actor immutability, handoff and 16/32-bit index boundary suites also pass.

The 8.83x terrain timing improvement measures 100 generations of 700 snapshots, preparing/writing 2,150,400 GPU indices per generation. It is not a whole-frame result. Each aliased entry adds a bounded Boolean flag; actual allocated vector capacities remain accounted against the existing index limit. The existing 20-byte GPU arena, vertex cap and index width are unchanged.

Lazy material registration adds no bytecode copy or additional GPU variants while inactive. Pending entries retain the original immutable shader, bounded by the existing 4,096-entry registry. No compiled success is evicted to admit another record. Failure/overflow recovery, source replacement, reset, allocation failure and COM cleanup are tested. The actual GPU fixture registers while Off, enables materials, and compares the first enabled native/material outputs against the installed baseline across shader families and MSAA. All 140 capture checks pass: 100 M2 checks plus 20 terrain and 20 WMO checks, including rigid, skinned and weighted families, alpha, mutation and state restoration. Together with FogTemporal, 788 GPU validation cases pass. The release builds as x86 PE32 with all 17 original D3D9 exports.

Only FogTemporal bytecode changes. Its integrated hash is `492128b1154ec3e10f526b12c5bdf56946b405f79fa9d3d3fdbbb82761891d8b`, matching the independently tested binary. The other 23 world shader entries remain identical to `bc382d9`; local, AO and material shader binaries are unchanged. The selected shader uses 252 SM3 slots and 13 temporaries. The 594-case matrix plus 54 repeated production-image cases covers FP16/FP32, HDR, odd/even grids, MSAA, rejection paths, equal/unequal/mixed history, signed zero and nonfinite stress.

## New profiling and next run

`CSTREAM REPLAY frame=... measuredMs=... clockReads=... maxCmd=... top=...` measures actual replay commands on one frame in 127. The five largest command totals can distinguish native draws, creates, uploads, state setters and query tasks in the large `beforeReplayMs` interval. It uses about 8 KiB of fixed per-device metadata; ordinary/disabled frames add no timing clocks. Reporting happens after Present retirement.

Those totals include command hook/backend elapsed work and can include waits. They exclude queue idle, express work, retirement and out-of-command logging. Short commands include timer overhead; `clockReads` makes the diagnostic cost visible. They overlap query/GPU/other CPU stages and must not be added to them. Command order, skip policy, HRESULTs, exact-generation query completion and message pumping remain unchanged.

Use `MATERIAL_COLD vertexQueued/pendingVertices/vertexSources` to verify that Off queues metadata without compiling. Use terrain `directionalAliases/indexCacheMiB` with actual list builds and unchanged GPU counts. Read actor decode/pose/hash/BVH as worker elapsed spans, which can include preemption, rather than foreground frame additions.

```powershell
python tests/test_displacement_state_reuse.py
python tests/test_query_poll_latency.py
python tests/validate_material_deferred.py --baseline-ref bc382d9 --alpha-test --texture-guard --lazy-registration --backend D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll
python tests/validate_fog_temporal_equivalence_gpu.py --baseline-ref bc382d9 --backend D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll --output out/.svn/fog-temporal-repro
python scripts/build_renderer.py
```

The separate package contains the release DLL, English reports, hashes, worker reports and machine-readable evidence. The installed DLL and settings are preserved. Keep the same scene, camera radius and orbit path for the next global-Off comparison. Later large scene/queue delays and isolated producer stalls remain explicit follow-up targets; no claim that every rotation hitch is eliminated is made.
