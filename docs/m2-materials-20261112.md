# M2 material cost вЂ” 12 November 2026

## Last game run

Input: `<client>/northlight-renderer.log`, preserved under
`<out>/m2-optimization-20261112/before-renderer.log`.
The run used renderer 0.3.217, DXVK 3.1.1, 3840Г—2160, and RenderProfile=1.
`log-analysis.json` beside the preserved log contains its SHA-256 and the
individual foreground intervals. Background intervals at about 30 FPS are
excluded; they do not measure the material switch.

With terrain, walls and grass material modes Off:

| M2 mode | Observed frame time | Evidence |
|---|---:|---|
| Normal | 15.099 ms / 66.23 FPS | One complete 120-frame interval |
| Off immediately afterwards | median 13.484 ms / 74.16 FPS | Three complete 120-frame intervals |
| Off before the later M2-only switches | median 14.675 ms / 68.14 FPS | Three complete intervals |
| Displacement | 17.430 ms / 57.37 FPS | One complete interval, includes a 105 ms outlier; median frame 16.145 ms |
| Parallax | 17.446 ms / 57.32 FPS | Incomplete 47-frame settings-change interval |
| Normal | 16.740 ms / 59.74 FPS | Incomplete 55-frame settings-change interval |
| Off immediately afterwards | 14.409 ms / 69.40 FPS | One complete interval |

These sequential switches support the reported M2 cost. They are not a matched
route replay, and the short Parallax window cannot establish a stable FPS.
At sampled Normal frame 27051, material CPU exclusive time is 2.1341 ms:
818 snapshot/pose attempts, 320 capture draws, 69 deferred packets and 18 flushes.
Frame 28702 has 1081 attempts, 388 capture draws and 2.3132 ms material CPU.
CPU and GPU intervals overlap; they must not be added together. Much of the
capture is inside GameScene, outside the deferred MaterialCapture interval.

## Algorithm and changes

A recognized M2 shader is checked for native visibility/material support. Range
admission reads the current constants and immutable source geometry, evaluates
conservative pose bounds, and decides which material consumers can contribute.
Normal maps are cached by source texture. Capture preserves native alpha and
depth behavior; safe packets retain their sources and replay in batches. Other
draws replay immediately. Parallax also patches native shading; displacement
prepares private geometry and preserves current animation and native coverage.

The common path now:

- Tries a range proof once per draw, retaining an unknown result instead of
  repeating a failed snapshot/pose calculation for batch admission.
- Hands the full-layout skin snapshot and declaration directly to that draw's
  deferred capture. Rigid position-only snapshots are never used for replay.
- Uses the existing audited stock single-bone M2 proof before a full envelope
  evaluation. The shortcut proves only a lower depth beyond 48.01; larger
  configured ranges keep the draw, and unknown/mixed/modified cases fall back.
- Continues an admitted cold source once per frame within the existing build,
  operation and vertex budgets, even when its original draw is absent.

No material mode, distance, relief strength, animation precision or shader
quality setting is reduced.

## Validation

`test_displacement_state_reuse.py` compares current transforms, positive and
negative depth, the 48-yard boundary and full-stream handoff; its alternating
component timing compares the existing full envelope with the stock proof.
`test_m2_material_admission.py` runs paired production capture/native/flush
workloads on the configured DXVK device: rigid, buffered skin and unknown pose,
including normal, pressure and recovery. It requires exact normal, occlusion,
scene-color and final-consumer pixels, identical draw counts, fewer constant
reads on unknown proofs, and bounded progress without the original draw.

The source, architecture and GPU suites cover integration and state/image
contracts. Only a matched game run of the built DLL can establish the final
FPS improvement. Component timings do not establish game FPS or a total
Northlight frame budget.

## Follow-up game run (16:38 local)

The second game log is preserved at
`<out>/m2-followup-20261112/before.log`, SHA-256
`c825b9d5cb4ebe982ea3913bd0b8036586d270d590d0786606c70a8b298dde74`.
Its DLL source identity is
`1fe8ff216114a2ad6d09e6d3d5e44ae694fbb501ea409bb52a26e1f746895d00`;
this includes the preceding M2 optimization. The run still shows a material cost.
Terrain/walls/grass stay at 3/3/1; this run switches M2 Displacement, Off,
Displacement. It does not isolate Normal and Parallax.

| M2 phase | Complete foreground windows | Median window mean | Material CPU exclusive |
|---|---:|---:|---:|
| Displacement before Off | 5 | 18.115 ms / 55.20 FPS | 1.990 ms |
| Off | 2 | 16.426 ms / 60.88 FPS | 0.213 ms |
| Displacement afterwards | 1 | 17.804 ms / 56.17 FPS | 1.873 ms |

M2 therefore adds about 1.4-1.7 ms to the observed frame and about 1.7 ms
of material CPU work. These are sequential windows, not a deterministic route
replay. Login/empty and partial setting-change intervals are excluded.
At the sampled M2 frame 12192, pose-envelope work is 0.353 ms, deferred flush
0.300 ms, capture setup 0.210 ms, source lookup 0.201 ms, other bounds work
0.137 ms, local envelope 0.103 ms and fallback vertex evaluation 0.100 ms.
There are 711 capture-setup attempts, 646 snapshots and 505 envelope calls.
Immutable meshes are warm: zero source bytes read, zero mesh uploads/creates;
225 envelope operation refusals still lead to the local/fallback paths, and
8189 of the 8192 fallback vertex allowance are used. GPU mesh upload is not
this sampled bottleneck. Deferred `MaterialCapture=0` excludes inline work
inside GameScene; it does not establish free materials.

## Follow-up implementation and validation

- A lookup-only metadata query and completed immutable skin envelopes provide
  an optional broad phase. It uses the existing audited view-axis evaluator,
  outward padding, current palette and exact source/program/layout ownership.
  A separate 100 us / 4096-attempt frame cap cannot consume the original build,
  operation, vertex or exact-evaluation allowances. Cold, unsupported, invalid
  or exhausted proofs retain the original path.
- Displacement tries this proof before saving capture state. Distant sources
  can avoid both exact world-axis preparation and capture setup. Surviving
  models retain the exact aligned envelope and displacement fade. A known-only
  palette hint skips optional work for near draws; it never proves rejection.
  NPatch retains the old path. Declining the optional proof preserves the late
  exact proof used for batching.
- Normal/Parallax range admission also hands the raw source and a current far
  proof to the model-normal receiver. Exact constant epochs, shader,
  declaration and source identity are required; changed poses cannot consume
  an earlier result. Near draws avoid publishing unused projection bounds.
- `MATERIAL_POSE_COARSE` reports attempts, successful bounds, range rejections
  and time so that the next game log can verify use of the shortcut.

`test_material_pose_coarse.py` checks coarse enclosure against exact aligned
bounds and interpreted client vertices in four rounding modes, including
invalid input, program/source replacement, palette definitions and independent
budget exhaustion. Its equal-work timing disables clock deadlines in both
policies and does not measure game FPS. `test_displacement_state_reuse.py`
also exercises actual buffered prepare, early range publication, NPatch,
constant-epoch invalidation and the original forced-budget fallback.

`test_m2_material_preflight.py` runs 540 paired comparisons on the configured
DXVK backend: all three modes, near/mixed/far poses, and normal/pressure/recovery.
It includes material capture, model-normal receiver capture, native draws and
flush. Normal/receiver, occlusion, scene and final-consumer pixels must match
exactly, including cold and warm-up frames, with native target/constants restored.

In the follow-up paired run, 128 far draws reduce CPU time for this chain by
about 27-29% in Normal, 30-35% in Parallax and 17-18% in Displacement. Mixed
workloads improve about 5-6%, 17-19% and 2-6%, respectively. Near-only timings
remain close (roughly -3% to +5% depending on mode/pressure); this change targets
rejected work rather than the shading of nearby models. GPU query results are
recorded separately; these numbers must not be presented as game FPS or a
whole-frame speedup. A new game run of the resulting DLL remains necessary.
