# Renderer throughput r55 — reuse current skin bounds

## Latest game run

Analyzed the r54 / DXVK 3.1.1 run frozen at `out/perf-20261108-r55/game.log`, SHA-256 `2f292d3dcfcc9da5d405c1d9a2bc39c56e0b7739a7dbe13a590821c5e5df5262`. The installed source identity is `aea7bf459ce07d0aebabb3afc2c7f910ff71893546a59be34e13c8190a5cde3a`.

The last two foreground windows average 31.892 and 31.105 ms (31.36 and 32.15 FPS). World entry is substantially slower: the preceding window contains a 2445.279 ms maximum. Menu samples are excluded from the steady gameplay comparison. Different camera/workloads prevent a controlled comparison with older runs.

| Sample | Frame 1778 | Frame 1905 |
| --- | ---: | ---: |
| GameScene timestamp interval | 15.939 ms | 16.857 ms |
| ModelNormals timestamp interval | 2.199 ms | 3.156 ms |
| Effects timestamp interval | 5.359 ms | 4.155 ms |
| Logical ModelNormals draws | 1034 | 1187 |
| Submitted ModelNormals commands | 634 | 800 |
| Current-pose range culls | 35 | 87 |

GPU timestamp intervals contain submission gaps; they are not exclusive GPU execution or utilization measurements. At CPU frame 1905, capture is 6.370 ms and effects 4.333 ms, with 12.302 ms total instrumented work. Material source/setup, pose envelopes and native submissions remain relevant; summing CPU and GPU intervals would double-count overlap.

r54 vertex sharing is active: 22 created programs and six shared source entries at frame 1905. Pixel sharing remains at two programs and seven shared source entries.

## Cache findings

Warm normal geometry is entirely resident in these samples: zero source reads, streaming uploads or UP fallback. Aggregate native geometry remains within 96 MiB without pressure trims. Increasing its capacity would not address these measured warm samples.

`AUTONORMAL` reports 32 entries / 33,428,792 bytes against 32 MiB, 43,501 hits, 81 evictions, 84 allocations and 29 resource reuses. `generated=6` is a cumulative build counter, not six builds per frame. This map cache is distinct from native geometry residency. Occupancy and cumulative eviction counts establish pressure, but do not by themselves establish per-frame thrashing or explain all 827 normal-build-stage attempts. Refused/pending map admission also enters that stage.

Current-pose preparation already supplies hundreds of ready bounds while comparatively few reach receiver range culling. Investigation found an actual discarded result rather than a need for another cache.

## Change

The material producer computes conservative current skin bounds for both audited four-weight body layouts, using r0 or r1 as position. Its normal-receiver handoff previously accepted only the r0 evaluator. The receiver classifier independently had the same restriction. r1 therefore paid for bounds that its normal pass could not consume.

Both use the existing `SkinEnvelope::supports` proof through the existing cached range classification. Native projection still requires the original four consecutive DP4 writes, c2–c5 bindings and allowed DEF instructions. Modified projection, palette/projection DEF overrides and unsupported programs retain capture. The separate cheap rigid/uniform-bone proof only admits its original two kinds.

An already-computed view envelope that proves native clip.w beyond the existing 48-yard receiver limit now sets the existing far-handoff flag. The normal pass can reject it before render-state admission, pixel/vertex preparation, packet/constant capture and geometry submission. Near and boundary-crossing draws retain their bounds and capture; a changed source, native shader, declaration or float/integer/boolean/reset epoch invalidates the proof. N-patches retain the existing consumer guard.

The common path is used on DXVK 1, 2 and 3. There is no new cache, persistent pose entry, per-draw shader scan, backend branch or memory-budget increase. Native scene geometry and caster coverage keep their existing paths; AO, resolution and lighting precision are unchanged.

## Remaining waits

Foreground EVENT waits still reach 35.7384 ms at frame 1553 (11.6975 before replay, 24.0128 in replay) and 35.5569 ms at frame 1736 (17.8004 / 17.7352). Startup frame 2 reaches 199.3216 ms. These are synchronization intervals, not proof that an individual shader executes for that duration. This change reduces work feeding the queue; it does not claim that the waits or gameplay stutters are eliminated.

## Validation

- Full displacement state/admission suite, including actual buffered `prepare` for both r0/r1 programs: current pose, near/boundary retention, far promotion, denied proof, source lifetime and epoch invalidation pass. The old test backend now supplies its missing shader-creation method; near null-diffuse assertions follow the existing map-before-state admission order.
- CPU native range proof passes for both real body programs, including modified projection/DEF rejection and source/epoch guards. Independent material-pose comparison passes 120 poses/source generations at O0 and O2 with bit-identical conservative bounds.
- 24 GPU range cases: SM2/SM3 × four native program families × DXVK 1.10.3 / 2.7.1 / 3.1.1. Current bounds and promoted-far paths retain exact normal/depth pixels. Near geometry adds pixels, disjoint far candidates match the anchor, and stale promoted proofs retain capture. An intentionally cold PS mirror proves far admission makes no PS request.
- Six full GPU cases (two affected weighted families × three backends): exact capture, cutouts, MSAA, source mutation, state restoration, creation-failure fallback, batching and shader sharing pass. Six sampled lighting CSV pairs are byte-identical to r54; these are not whole-game frame comparisons.
- Production compiler preflight, shader freshness, PE32/export validation and all 779 final production file hashes pass. The final DLL completes 64 real hardware EVENT generations on each backend (192 total), with the command-stream proxy active in isolated hosts.

Receipts and commands are in `out/perf-20261108-r55/`. The installed r54 DLL and latest game log remain byte-identical. No game control, installer, subagent, publication or sleep operation is included.

Production DLL: `out/dll-20261108-r55/d3d9.dll`, 10629120 bytes; SHA-256 `f05ef1ce90dfa3d49ae457572e20e2598c7db67f5c05e340c988edc97377281a`; source identity `b7aff6a8cd23e19bf9b3a9c9936ac24275a94e26d69ff2f1a403601e360740ee`, built 2026-11-08 22:01:49 UTC. Gameplay FPS and remaining EVENT waits still require the next actual game run.
