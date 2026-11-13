# Lunar shadow intensity investigation, 13 November 2026

The user reports building/geometry moon shadows changing intensity approximately
once per second, including with a stationary camera. The installed implementation
already has static direction crossfades lasting 1,000 ms. The period is consistent
with that path, but the existing log cannot independently identify every fade
start/cancellation. Sampled WORLD_LIGHTING diagnostics now include transition
starts/failures and the two lunar cascade fade states/weights.

## Captured evidence

The four captures are in the game's
`northlight-diagnostics/session-37568-266677953-0/`. Captures 2 and 3 have the same
camera position and view/projection constants. 99.11% of half-resolution pixels
also have matching distance/normal inputs within the analysis tolerance.
On those stable pixels, raw shadow visibility has mean absolute difference
0.00557 and p99 0.13086; about 1.55% differ by more than 0.1. Local light and fog
changes are much smaller. The comparison localizes the visible change to shadow
edges; two separated captures do not establish its temporal waveform.

Analysis artifacts are in
`.local/out/test-output/performance-20261113-latest/`, including
`capture-2-3-difference.json` and `shadow-visibility-comparison.png`.
These captures are diagnostics, not FPS measurements or complete-scene replays.

## Proven state defect and correction

`preserveShadowStatic()` samples the previous static cache with POINT/CLAMP,
then the caller renders the next static cache. It restored LINEAR filtering but
left CLAMP addressing on sampler 0. The caller had established WRAP before the
history operation. Thus a direction update could change alpha-tested repeating
caster coverage even without changing its geometry or material.

The helper now restores WRAP on both axes, including its existing checked failure
path. No new texture, geometry draw or shader is introduced. Ordinary opaque
geometry does not use this alpha-texture distinction; this fix alone is not proof
that every reported lunar intensity change is resolved.

The new hidden-device GPU test calls the actual production helper, then renders
the same cutout caster through the production shadow shaders:

| DXVK 3.1.1, 64x64 shadow map | Changed depth pixels |
|---|---:|
| Original helper, unchanged caster after preserving history | 1,785 / 4,096 |
| Corrected helper | 0 / 4,096 |
| Deliberately reintroduced CLAMP negative control | 1,785 / 4,096 |

The existing resource-helper test now checks this state, runs on Windows with
the pinned Zig compiler, and selects the eight shaders actually created by that
helper rather than incorrectly counting later Forever variants. Its 28 allocation
failures, all 52 setup/draw/cleanup failure positions, fallback cadence, bindings
and lifetime checks pass. The new GPU test is in the GPU suite; the resource test
is in the architecture suite.

## Outstanding validation

The broader unchanged transition shader oracle on DXVK 3.1.1 reports ten numeric
envelope failures of roughly 0.00003052 against its 0.00003 tolerance. Its
independent query oracle maximum error is 1.19e-7, and both transition endpoint
and production FP16 endpoint errors are zero. Do not report that run as passing
or silently loosen the tolerance. These failures do not demonstrate the user's
large intensity jump. The targeted sampler regression passes on the same backend.

The sampler fix does not change contact radius, shadow strength, celestial speed
or cache cadence. The user's next run still showed different shadow intensity;
its evidence and the separate cause follow below.

## Follow-up: lamp batches overwrite shadow strength

The two subsequent captures are in
`northlight-diagnostics/session-46680-269393531-0/`, renderer frames 2012 and 2213.
The camera moved slightly, so their final pixels are not a matched same-view
comparison. All 16 sampled WORLD_LIGHTING log records have zero transition
starts/failures, neither moon cascade fading, and weights 1/1. These captures do
not support celestial direction transitions as the cause of the intensity jump.

The per-pass constant banks establish a different defect:

| Capture | Local batches | c115 at MaterialComposite | Surface removal limit |
|---|---|---|---:|
| 1 | LocalDirect16, then an unsubmitted LocalDirect | (1, .470588, .156863, .090909) | 45% |
| 2 | Three LocalDirect batches | (1.25, .5, 1, 1) | 55% |

LocalDirect16 uses c100..115 for lamp colors and reciprocal attenuation width.
ShadowTuning also occupies c115, and was only uploaded before directional
lighting. The final compositor consequently interpreted the last lamp as shadow
settings. In capture 1 its w is below .5, selecting default strength 1 rather than
the configured 1.25. Other lamp widths can instead make RGB become arbitrary
shadow settings. Batch selection depends on projected lamp rectangles, so a
camera change can alter the result even with identical celestial lighting.

The host now restores ShadowTuning after all local direct batches, before
FlashShadow and the final SDR/HDR/material consumers. The optimized 16-lamp path
remains enabled; the correction adds one four-float upload on local-light frames,
with no added draw, texture or shader work. This also prevents a lamp's color
from changing FlashShadow's contact filtering.

`test_shadow_lamp_constants_gpu.py` drives the actual WorldRenderer with the
same 16 lamps, surface and camera through one 16-lamp or two 8-lamp batches.
Before the fix, all 30,720 SDR RGB components differed (maximum 0.060059);
10,240 HDR components differed (maximum 0.031494). The regression also deliberately
reintroduces the bad constant as an image-sensitivity control and covers plain
SDR/HDR, separate SDR/HDR materials and combined HDR materials. It is included in
the GPU suite. With the correction, all five variants have zero pixel difference
between 8/16-lamp batching on DXVK 3.1.1; the deliberate corruption still changes
the image in each variant. Correctness checks do not establish gameplay FPS or prove that
every remaining shadow artifact has been resolved.

Evidence, the preserved game log and standalone before/after outputs are in
`.local/out/test-output/lunar-shadow-20261113-followup/`. In particular,
`lamp-shadow-register-evidence.json` lists each captured pass, c115 and the bank's
SHA-256. The generic `capture-N-constants.f32` has only 79 registers; use the
116-register `capture-N-lighting-pass-M-constants.f32` for this diagnosis.

After restarting with source identity `de52e3629c36`, the user confirmed that the
shadows were correct. The subsequent log contains no shadow-transition failures.
Remaining performance costs are recorded in
[the follow-up performance notes](performance-20261113-followup.md).
