# Material admission snapshot reuse, 2026-11-14

The old gameplay log still identifies material capture as a CPU hotspot:
2.445 ms median capture, including 0.4252 ms in pose envelopes and 0.2719 ms
in source snapshots. These stages are nested, not additive.

A local-session run as Todesbote in Orgrimmar loaded the fog-fixed `eefc50d0`
build at the existing 3840x2160 HDR/MSAA 2 settings. Material normal-map building
was almost entirely warm, but capture still cost approximately 2.5 ms CPU.
Prepared pose metadata and geometry residency were already working. The next
change therefore removes duplicate warm lookup work rather than lowering detail.

## Change and ownership

`nearestMaterialDepth` can obtain a full tracked skin snapshot during the
read-only coarse admission check. `AutoBump::capture` previously discarded that
snapshot at the start of displacement preparation and queried the same source
again. Preparation now accepts that invocation's snapshot and declaration.

Only device getters separate the two consumers. The same enclosing draw gate
protects the native state; the handoff is local to one `capture` invocation.
Preparation retains both owners before clearing its output, so input/output
aliasing is safe. A target-size reset clears the handoff first. Cold, rigid and
UP preflights retain the existing capture path. Current constants, pose bounds,
material admission and rendering are still evaluated normally. No pose result
or source pointer is reused across draws or frames.

`MATERIAL_ADMISSION_SOURCE reused=` records successful reuse per sampled frame.
This counter should be checked in the next gameplay log before attributing any
frame-time difference to the change.

## GPU and CPU evidence

`test_material_admission_source_gpu.py` compares both policies in one executable,
changing only this handoff. It uses the actual material and normal receiver
capture paths, stock skinned shaders, current poses, all three material modes,
near/mixed/far source bounds, and normal/pressure/recovery phases.

The first run passed 540 comparisons with exact normal, occlusion, scene and
final consumer pixels, preserved state, and equal draw/target counts. The warm
displacement fixture reused 128 snapshots per frame (65 in the mixed case),
instead of repeating their lookup. The controls keep normal and POM behavior.

For 128 draws, displacement CPU medians in milliseconds:

| Workload | Normal | Memory pressure | Recovery |
|---|---:|---:|---:|
| Near: before → after | 0.922 → 0.905 | 0.930 → 0.910 | 0.933 → 0.905 |
| Mixed: before → after | 0.559 → 0.556 | 0.563 → 0.550 | 0.567 → 0.551 |

This is a small component saving, not a demonstrated game-FPS percentage.
Background GPU activity makes GPU timestamp differences unsuitable for a shader
speedup claim. The live baseline and historical log also differ in population,
lighting and time; their FPS must not be compared as a controlled A/B result.

An earlier SSE2 experiment for the second bounds transform passed 160,000 exact
numeric cases but regressed the complete CPU path by roughly 3–6%. It was
discarded. The previously accepted single-bone SSE2 optimization remains intact.
Windows WPR failed to enable its profiling policy (`0xc5585011`); no system
sampling trace was collected and no system policy was changed.

## Reproduction and delivery

Evidence: `.local/out/cpu-live-20261114/`, including preserved old/current logs,
original settings, `baseline-summary.json`, the rejected transform experiment,
the source-reuse GPU report, and `installation.json`.

```powershell
$env:NORTHLIGHT_TEST_ALLOW_RUNNING_GAME='1'
python scripts/run_tests.py --suite source --strict
python scripts/run_tests.py --suite architecture --strict
python scripts/run_tests.py test_material_admission_source_gpu.py --strict
python scripts/build_renderer.py
```

The production DLL and verified asset pair were installed in
`D:\Games\335_engb_mod`. Runtime source identity:
`e3e73767650d9b70834f06a5f59d265370f4fcefb3cb098dc8919df6fc12a159`.
DLL SHA-256: `2bcc8461480556f8df0986af8847776820c6df8ac23ddf1311a189270e6c9aa4`.
The asset bytes are unchanged. Rollback pair:
`renderer-backups\20261114-150450-before-material-source-reuse` in the game root.
The user stopped computer control and chose to perform the candidate gameplay
run themselves. A live FPS improvement is therefore still unverified.
