# Telredor camera rotation stutter, 5 November

Researched `qa_report/northlight-renderer.log`, 6 886 strings, SHA-256 `44f88d1bb1472624335f5ae013c30003d5dfd979f3417b3332d459b75ef58560`. BUILD source `32110bc36c1ede9d94f3a495a0c5bf0f5734a1fb3fc609c431a0b7667cdf0d13`, 17:50:28 UTC. RTX 3080, DXVK 3.1.1, 2560×1440, HDRof, MSAA 0. This new run; the previous report from SHA `d53fb948...` relates to another record and assembly.

The image of the log, its identity and reproducible review are kept in `out/telredor-rotation-20261105/{renderer.log,snapshot.json,analyze.py,analysis.json}`. Production DLL and the installer were not collected in this passageway, nor was the game launched.

## What really slows down

| Area | Observation in a new log | What does that mean? |
|---|---|---|
| Point shadows | Framework 1524: 162.927 ms CPU bucket, 17 760 draws; 17 706 replay + 54 union; 6 426 684 caster triangles | The main measured surge is the re-send of shadow geometry. This is not the cost of one fullscreen compositor shader. |
| Effects / GPU | same frame 1524: Effects 168.252 ms, all GPU frame 198.852 ms; GameScene 27.548 ms | The length is confirmed by the GPU timemps. It may include waiting for commands; this is not proof of 168 ms pure chader aristmetic. |
| Caster geometry boundaries | Example: 820 packets, only 573 valid; 292 Cold Visits, 45 experiments = 45 ready = 45 expired, 2 048 entries at 2.57 MIB | Cash caches into the amount of entries and loss of source unerhip proof, long before bytes limit. Unbounded caster is conservatively going to all sides of every suitable cube. |
| Synchronous buffer locks | windows ending in stream frame 1200 / 1800 / 2400: SyncMs 13.869 / 16.738 / 13.088 per frame; 5 sync storms; by the end of 24 473 No Shadow passes | Repeated Lock/Unlock Interrupt parallel command delivery. At peak storm: 324 sync calls/frame. |
| Refusal of full mirror | 913 732.5 MIB total failure length / 24 473 MIB failure 37.34; full-shadow it max 24 MIB, large allowance 36 MIB already occupied by 34.5 MIB | A full mirror is too big for an old journey. It is not necessary to reserve a complete large buffer and its ring for small records. For the old log, you cannot define the exact buffer ID, the ranges and the flags of each failure. |

Frame 1542 at Focus=1 occupied by 295.843 ms: scene 46.008 ms, effects 249.120 This isn't just a rare profile: `sampled=0`, queryCalls=0In the window 120 to scale 1564: median 76.300 ms, p95 241.705 ms, max 295.784 ms. Other window before frame 1924: median 73.624 ms, p95 194.310 (in thousands of ms.

These values cannot be added: CPU submission, GPU elapsed time and expectations of the producer are partly described as one critical chain. The removal of expensive shadow shipping can in itself reduce buffer waits time; the removal of waits can also move waiting to Present with an emphasis on GPU.

Individual bare/loading footage 425–428 with seconds delays were not mixed with rotation in a loaded world. The last windows of Focus=0 were also not used as proof of the smoothness of foreground games. Line slow/sample is not a complete sample to calculate global p99.

## Correction 1: Ready source environments are undergoing reconstruction of Mesh

In `src/replay/replay_envelopes.inl` both lookup now use a full mmutable `replayIdentity` or `sourceIdentity`, along with the exact position-program/declaration key. Hash selects bucket but does not replace the comparison. Untracked geomery retains the previous check of raw adress and wek control lock.

The stored source environment does not contain a current position or camera. When re-established, the Mesh optiond memo is released; the current constants/pose/inverseView are again involved in the calculation of the bows. The end of the web is no longer depreciating the completed mmutable environment. Cash does not hold the heavy vertex/index Payload.

Limiteds 2 048 entries / 8 M &amp; B, work budgets and sceduler are not increased; unknown and unsupported geometry is still being conservatively processed. In existing background logs, `sourceIdentityHits` and `sourceReconstructedHits` are added to see actual overuse in the next game slot.

Control of actual production job schedule: 48 rotations, 60 928 re-created pactes, 1 900 source features:

| Result | Frozen old | Corrigendum |
|---|---:|---:|
| Valid current-pose bounds | 0 | 60 928 |
| Cube-face submissions | 365 568 | 61 372 |
| Source vertices rebuilt | 827 488 | 0 |

This is at 5.956 times the number of shipments on the edge of this scenario. Independent tests of all vertices confirm the current position/chamber. In GPU control 144/144, R32F cube faces coincide byte with the dispatching of all objects to all sides: draws 6 912 1 350, 53 409, without empty shadow pixels.

Precise proof and commands: `out/telredor-rotation-20261105/bounds-identity/REPORT.md`. A separate evaluation of the CPU itself by the bows sceduler did not accelerate: warm pass about +0.036 ms; restoring valid banks also has a value. The win is designed to eliminate repeated beilds and an unnecessary shadow submission background. These tests do not measure the game's FPS or prove that each Cold packet of the old log was a reconstruction of the known source.

## Corrigendum 2: background log mirror for oversold dinamic buffers

Added to `src/stream/buffer_range_shadow.h`; connected to existing Lock/Unlock in `stream_proxies.h`; for DYNAMIC, the DYNAMIC buffers are greater than 24 MIB is maintained one active interval of accurate CPU bytes: up to 4 MIB on the buffer and up to 8 MB is cumulatively inclusive, including old and new allocation during growth. The memory counter is included in page memory accounting.

A repeat Lock of a known interval receives the same bytes through the existing Scratch/Block, and Unlock turns out to be a normal recording of the range. The row owns its own extrahead bytes and does not read the variable mirror. A full buffer or new ring is not required for re-distribution.

A new interval after a valid DEFAULT-pool DISCARD can be initialized asynchronously while all records are stored after DISCARD. This corresponds to D3D9 contract: DISCARD vertex/index buffer devalues the entire buffer, even with partial Lock. Source: [Microsoft D3DLOCK](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dlock). The existing flags normalization is in place for managed floor, so its DISCARD does not grant this exception.

The unknown data without such proof remain synchronous. The gap/displacement of the range, excess of the limit and allocation failure are dropped proof; READONLY reads backend; ProcessVertics clears mirror and prohibits further re-use. Native Unlock failure also has disabilities. A separate Busy mode and a re-checking after a pumped Budget wait protects against nusted Lock/Unlock and GPU records during allification.

In the CSTREAM line, a logShow bytes/cap and hits/stores/refused. the next game log will show the actual hit rate. The number of failures of the old log does not allow advance confirmation that all 13–17 ms waits will disappear.

Checks `tests/buffer_range_shadow_scenarios.h` through actual production command line and deterministic D3D9 backend:

- 12 000 random band changes, DISCARD, gaps, innovation and allocation refusion with independent bytes model.
- 400 repeated VB re-locks without SyncLock/Unlock; frozen Old falls to check the absence of these sync calls.
- 16 x 32 DISCARD/append writes without sync, 32 IB re-locks without sync; all bytes are checked by READONLY via backend.
- The unknown range and the GPU-written buffer remain exactly SyncLock + SyncUnlock.
- The present pumped budget wait: nosed Lock/Unlock is rejected, quawed ProcessVertices is disabled with pending fast path, outer lock uses backend.
- Zig O2 and the apparent UBsan PASS; complete former command-stream sube and other sube PaSS. In addition, this GCC 14 ASan/UBSan has undergone new tests and all previous suite, including customized uploads, lifetime, presure, overlap/rename, reset and randomized exculpability.

Receipts and precise commands: `out/telredor-rotation-20261105/stream/validation.json`, `verify.py`, `real-asan-validation.json`, `real_asan.py`. Zig portable testes are performed via WSL, the real ASan/UBSan - GCC 14.2.0-19 in an already installed local Docker image `sha256:76e563191d1ade120313a8736df24154d21da5155c0756f147c0b01bd19d9087`, with a back-to-back source and an unconnected network. Windows render integration has been separately compiled without DLL lens.

The audit found that combined `-fsanitize=address,undefined` from the Zig used did not include ASan: only UBSan was in the binarium. The initial marking of the ASan was corrected, the apparent UBSan was repeated, and the real ASan independently checked GCC with negative ringtime control. This clarification also applies to background reports; their final report is shared by Windows Zig UBsan and Linux GCC ASan/UBSan.

## What has not yet been proven

The image was not simplified: Shadow management, caster set, dissance, faces, refresh cadence, AO, weather and lighting did not cut. Shadow texels and buffer bytes were monitored, but this is not a complete rerun of the Telredor original scene.

In the warm old gameScene lounge, it is about 22–27 ms GPU; the CPU frame also remains visible. The fiks aim at confirmed major spikes and barriers rather than promise zero value Northlight. To estimate the totals, p95/p99/FPS requires a new game run after a future authorized assembly. This source-only passage cannot honestly declare an ideal framework achieved.

Existing `renderer/frd9.dll` retained: SHA-256 `3861b4c1cbe396d583513efc8d0cdeb1b2b1cd0a397bf30eb84b21de9b2ca5ea`, mtime 17:51:46 UTC. New changes in the source are not included.
