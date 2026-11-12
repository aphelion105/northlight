# r63 — shared base lighting and draw-state overhead

## Evidence from the r62 game run

The frozen log is `out/perf-20261109-r63/game.log`, SHA-256
`3b1ee5135333b517dc21c663503c2e43619eeaa115bf4a56862884ecf58708a7`.
It identifies r62, DXVK 3.1.1, scRGB HDR, 3840×2160. No game session was
started for this iteration. Settings changes provide four repeated AO-only
comparisons after GI, world shadows, fog, local lights, materials and weather
were disabled:

| Interval ending at frame | AO | Mean frame, ms |
| --- | --- | ---: |
| 2265 | on | 16.057 |
| 2342 | off | 10.479 |
| 2412 | on | 15.681 |
| 2489 | off | 10.605 |
| 2559 | on | 16.018 |
| 2654 | off | 10.504 |
| 2741 | on | 15.846 |
| 2862 | off | 10.585 |

These intervals average 15.901 versus 10.543 ms: approximately 5.36 ms added
to the game frame. The camera records around the AO-only comparisons retain
the same position and direction. This is evidence of shared pipeline cost,
not evidence that the AO shader alone costs 5.36 ms.

At GPU profile frame 2540, AO/blur are 1.255/0.478 ms. World setup, normals,
lighting, removal smoothing, temporal light and composition are respectively
0.140, 0.106, 0.155, 0.020, 0.054 and 0.598 ms. Model-normal replay is another
0.563 ms. The capture queues 1,088 logical draws, submits 815 merged draws,
reads no source geometry bytes and fetches 62,090 constant registers. CPU
capture is 0.900 ms, preparation 1.932 ms; these scopes overlap. Sampled
profiling itself accounts for an estimated 1.084 ms of clock reads.

GPU timestamp intervals include submission gaps. CPU/GPU numbers overlap,
and completed GPU query frames need not coincide with the CPU report frame.
They must not be added to manufacture an exclusive CPU/GPU decomposition.

## How the common lighting path works

Activating an effect also activates scene/depth preparation, opaque model
normal capture, half-resolution world normals, directional relighting and
baseline lighting, spatial/temporal stabilization, and full-resolution
composition. AO contributes its own half-resolution evaluation and blur.
The base light replaces authored sun/moon shares, preserves native ambient
and fog transport, and uses normals to avoid lighting discontinuities.
Disabling shadows does not make that relighting neutral; omitting it would
change the image.

## Changes

1. `WorldLightingUnshadowed` specializes the existing lighting function for
   disabled world shadows. The previous shader still transformed receivers
   and filtered a neutral all-lit shadow map, and read neutral actor
   visibility. Those results are exactly one. The specialization retains
   source colors, sun/moon replacement, material normals, baseline output,
   histories and composition. Shadow-enabled rendering retains the ordinary
   or soft shader. Creation failure falls back to the ordinary shader; Reset
   releases and retries the optional shader. `WORLD_LIGHTING` reports selection.
2. Mask restoration skips a setter only when the current, active device mirror
   certifies the exact saved value. It does not assume a native draw left the
   state unchanged. Unknown, failed, raw, recording and disabled-mirror paths
   retain the setters. No new state cache or backend-specific policy was added.
3. Fixed-function HDR sky admission rejects already known programmable shader
   bindings before entering a raw scope. This avoids invalidating the mirror
   for a transfer that `fixedDraw` necessarily rejects. Unknown bindings keep
   the existing authoritative admission and fallback.

## Validation and measured scope

The pinned-compatible Microsoft compiler reduced the unshadowed shader from
426 to 230 static slots and 16 to 7 temporary registers. Shadow samplers 2, 3
and 12 are absent. The two existing lighting shaders only renumbered literal
registers: normalized executable instructions and literal values match the
previous binaries. The other 46 existing shader binaries are unchanged.

ABBA runs use the production WorldRenderer and a captured 3840×2160 scene,
with separate reference/current executables differing in lighting selection.
All six readbacks (SDR/HDR color, current distance and fog) match **bit for bit**
within each backend, including temporal frames and restoration checks.

| Backend | Neutral-PCF light, ms | Specialized light, ms | Reduction |
| --- | ---: | ---: | ---: |
| Native D3D9 | 0.05120 | 0.02381 | 53.5% |
| DXVK 1.10.3 | 0.03174 | 0.02458 | 22.6% |
| DXVK 2.7.1 | 0.06144 | 0.02560 | 58.3% |
| DXVK 3.1.1 | 0.03174 | 0.02458 | 22.6% |

Values are medians of the HDR stage medians from the two runs per variant.
These are small absolute savings in an isolated pipeline; they do not predict
game FPS. This stand excludes native game draw hooks and model-normal capture.
Evidence: `out/perf-20261109-r63/backend-validation.json` and its per-run logs.
The retained test is `tests/test_world_unshadowed_lighting_gpu.py`.

The CPU regression extracts the production admission/restoration methods and
uses the actual DeviceMirror with its independent mock backend. It covers all
nine shader-binding combinations, invalidation/control modes, all 16 changed
state combinations, failed setters and zero backend queries, in O2 and
ASan/UBSan builds. Existing name-mask, unlit receiver and EVENT-poll tests are
also checked. No visual quality setting, AO algorithm, caster admission or
shadow coverage rule was reduced.

The production DLL was built with `scripts/build_renderer.py`; freshness,
production markers and D3D9 exports passed. Its isolated DXVK 1/2/3 smoke
passed 64 real EVENT generations per backend, including BOOL/NULL completion
payloads with the command stream active. This smoke has effects disabled and
does not validate gameplay frame times. The installed game DLL was unchanged.
Delivery: `out/dll-20261109-r63/d3d9.dll`.

## Remaining EVENT and scene work

The completion path is active: the largest wait is startup frame 2,
157.278 ms, of which 149.578 ms precede replay and 7.693 ms are replay work.
The largest later wait is 29.567 ms at frame 438. AO-only frame 2540 waits
6.079 ms, including 2.658 ms before replay and 3.413 ms in replay. These are
different phases; optimizing query polling alone cannot remove the time before
replay. No completion is fabricated, no stale payload is returned, and no
wait-policy change is made in r63.

The remaining large candidates are native draw-hook/state traffic and
model-normal capture/replay, followed by full-resolution HDR composition.
The r62 pose-to-viewport handoff has zero tests in the AO-only samples because
there is no material-producer handoff there. Extending conservative receiver
culling to that path needs separate current-pose validation and must keep
off-camera shadow casters independent. EVENT tails and the complete AO-only
FPS loss are **not claimed resolved**; they require the next game log.
