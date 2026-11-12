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
