# Performance investigation, 14 November 2026

The local client loaded runtime source
`acbffa8dcdb8719132e10d33a5c07f82e38a0e7eefea01872c594342c5437b1f`
(release 0.3.218.3), DXVK 3.1.1, RTX 4090, 3840x2160, HDR and MSAA 2.
The user requires unchanged visual quality, rather than lower effect settings.

Evidence is in `.local/out/test-output/performance-20261114/`:

- `initial/`: original current/previous logs, INIs and their hashes. Both logs
  had `Diagnostics=0 RenderProfile=0`. The 1000 ms submission at frame 13565
  coincides with an explicit diagnostic export and is excluded from gameplay
  performance conclusions.
- `profile-before.log` and `profile-before-analysis.json`: the user's subsequent
  run with only `Diagnostics=1 RenderProfile=1` enabled. Graphics settings were
  preserved. Frames below 500 are excluded from the following summaries.
- The capture inputs remain in the client diagnostic session
  `session-60800-280247484-0`; weather inputs are in `session-60800-280252343-0`.

## Measured costs

Twenty sampled GPU frames provide these elapsed command-stream intervals:

| Interval | Median ms | p95 ms |
| --- | ---: | ---: |
| Entire sampled GPU frame | 11.5935 | 16.375 |
| Native scene, including inline captures | 5.271 | 8.531 |
| Deferred model normals | 0.7085 | 2.261 |
| Effect block | 3.6705 | 4.468 |
| Removal smoothing | 0.658 | 0.694 |
| World composite | 0.4805 | 0.509 |
| AO | 0.349 | 0.370 |
| AO blur | 0.1995 | 0.246 |
| Fog march | 0.2905 | 0.299 |

These intervals are nested, and their independent medians must not be added.
GPU timestamps include submission gaps; they are not isolated shader execution
times or a whole-session FPS benchmark. Ten CPU samples have a median capture
cost of 2.445 ms and effect submission cost of 2.1415 ms. Material profiling
reports 0.4252 ms median in pose envelopes and 0.2719 ms in snapshots. The
profiling instrumentation itself performs thousands of clock reads per sampled
frame, so CPU timings should not be treated as uninstrumented frame costs.

Model-normal logs retain the previous snapshot reuse, compact shaders and draw
merging. Native MRT is rejected with `reason=target-msaa`; this is an existing
fallback, not evidence that an optimization disappeared. All 24 sampled
`SKY_BACKGROUND` records report `result=drawn`.

## Removal smoothing experiments

The most expensive stable individual effect is `RemovalSmooth`. Its 24-tap
filter reads normals, distance, lighting, baseline, scene and AO. Experiments
under `removal-branch/` keep its radius, weights, taps and accumulation order:

- A dynamic branch skips colour inputs when the computed tap weight is zero.
- A one-tap loop reduces shader register usage.
- An earlier rejection also skips normals when they cannot affect the weight.
- A four-tap loop changes scheduling, retaining the same 24 samples.

The first three variants pass exact FP16 image comparisons on all 13 authored
controls and the captured input set. These captures are partial pass inputs;
the harness explicitly records neutral AO when an AO export is absent. They
are not authentic whole-scene replays. The four-tap variant passes the authored
controls but its captured-input run times out; that run is incomplete.

No variant has been accepted into production. Another client,
`D:\Battle.net\Games\World of Warcraft\_forever_\Wow.exe`, remains active during
these runs. The user explicitly authorized GPU tests with this client running;
do not require closing it. Set `NORTHLIGHT_TEST_ALLOW_RUNNING_GAME=1` for these
standalone tests. Their private devices do not attach to or modify the game.

`scripts/benchmark_removal_smoothing.py` repeats the frozen-input comparison,
alternates ABBA/BAAB order, and includes identical-bytecode A/A controls. The
five-round report in `paired-background/validation.json` contains 20 successful
comparisons, all with zero changed output bytes and no nonfinite output.
Adjacent timed blocks give these median candidate/baseline ratios:

| Variant | Ratio | Interpretation |
| --- | ---: | --- |
| Identical bytecode (A/A control) | 0.9780 | Background/order variability |
| Skip colour inputs at zero weight | 0.9841 | No gain beyond the control |
| One-tap loop | 0.9919 | No repeatable gain |
| Four-tap loop | 0.9837 | No gain beyond the control |

The original four-tap captured-input timeout was followed by successful shorter
paired batches. It is not an image failure. These experiments do not establish
an optimization; do not ship their binaries or claim a gameplay FPS gain.

## AO experiments

`ao-pairs/` tests the classic AO path, which is active in the measured profile.
Grouping its eight taps into pairs or groups of four preserves the sample
locations, weights and accumulation order. Both `AOOnly` candidates pass all
nine authored depth/water/sky controls with zero changed FP16 output bytes on
the installed DXVK 3 backend at 3840x2160 input / 1920x1080 effect resolution.
Their per-case timings do not show a consistent improvement. The paired
`AOContactBloom` candidate was compiled but not validated; no claim is made for
it. Production shader bytecode remains unchanged.

These are component image checks, not captured gameplay replays. Background
GPU scheduling affects the timestamp intervals. Keep the raw per-case timing
logs rather than deriving a game FPS improvement from a favourable sample.

## Model-normal capture

The accepted source change compares only proven shader-read float registers when
the enclosing constant banks differ. Native lighting/UV values in unused gaps
previously split otherwise identical normal draws. Unknown read sets retain the
strict whole-bank comparison. The replay tracks the first packet actually
submitted, so a later shader that reads those gaps receives the correct bank.
No geometry, normal precision, coverage, resolution or sample count changes.

`normals-unused-paired/` and `normals-unused-checked5/` compare the old and current
renderer in one executable, in 16 ABBA blocks of 32 frames (first four blocks
warm up). A stock mesh is drawn at 13 repeated poses while native ambient c11
varies across 128 draws. This deliberately exercises the affected workload;
it is not a replay of the user's entire scene.

| Counter / interval | Before | Current, repeated run |
| --- | ---: | ---: |
| Queued / resident draws | 128 | 128 |
| Submitted normal draws | 128 | 13 |
| CPU replay submission | 0.0300 ms | 0.0224 ms |
| GPU replay interval | 0.0174 ms | 0.0102 ms |

Normal pixels and restored native float banks match exactly. The real gain in
this small-mesh fixture is about 7 microseconds, not a demonstrated gameplay FPS
increase. A matched game run must establish how often its draws meet this rule.
Existing live logs show 191 submitted normal draws from 301 queued at frame 2921;
they do not establish how many the new comparison will eliminate.

`tests/model_normal_merge.inc` checks changed live/unused/unknown registers and
a later alpha-tested shader that starts reading an ignored native-light row.
The deliberately broken header in `normals-previous-negative/` fails the exact
image assertion, demonstrating that the regression control detects the missing
bank upload. Existing merge, program-sharing, depth/alpha/bias and last-copy
ordering controls also pass in `normals-unused-checked5/`.
`normals-unused-native/` passes 11,988 native color/normal, fallback and state
cases on each of system D3D9 and the installed DXVK 3 backend.

An earlier alpha-tested vertex-slicing experiment in `alpha-normals/` passed
image checks but did not improve paired timing (0.1321 ms GPU both ways). It was
reverted. Its private headers are research evidence, not production inputs.

## Base WorldRenderer

`base-world-hdr-current/` exercises the production weather-only compositor on
the saved 3840x2160 scene/depth/camera with the installed DXVK backend. After
warmup, it performs one full-screen draw and one offscreen scene copy. GPU
interval medians are 0.2401 ms SDR and 0.3144 ms HDR. CPU stage medians sum to
0.0212 / 0.0341 ms respectively, including instrumentation. The driver verifies
state restoration, invalid caller-state fallback and failed-draw restoration.

Existing optimizations remain active: one enclosing state save, the specialized
HDR base shader, and no unused distance/fog history allocations. The old stand
still required those removed targets; its assertions now explicitly verify their
absence on the current weather-only path. The full game profile uses
`standard-hdr` because fog and local lighting are active; forcing the base shader
there would remove effects and is not an optimization. This stand excludes
native capture, live MSAA depth resolve, world streaming, AO and full lighting.

The large remaining live costs are normal capture, removal smoothing and the
full compositor, with additional CPU spikes in geometry/material streaming and
shadow submission. The minimal base path alone does not explain them. Do not
infer total FPS from the stand or add its timings to the live profile.

Production shader bytecode and the asset bundle remain unchanged. Profiling
stays enabled in the local client's quality INI for the next matched run; its
original file is preserved in `initial/`. Restore only the two diagnostic
overrides when measurement is finished, preserving any subsequent user edits.

## Follow-up: expanded-HDR material fusion

The [HDR material fusion report](hdr-material-fusion-20261114.md) records a
repeatable reduction of about 0.09–0.10 ms in the 4K/MSAA2 captured-input graph
on DXVK 3.1.1. The loaded-runtime gate now permits the existing combined shader
with highlight expansion on that version. DXVK 2 retains its guard because its
MSAA regression still reproduces. This changes the selector, not shader
bytecode/assets or quality settings; a new game profile is still required.

## Follow-up: CPU pose bounds

The [CPU bounds report](cpu-bounds-20261114.md) records the measured capture
hotspot, exact interval arithmetic checks, paired CPU results and the remaining
uncertainty in total game FPS. The single-bone bounds path performs less input
preparation and processes two axes together without changing its numeric bounds.
