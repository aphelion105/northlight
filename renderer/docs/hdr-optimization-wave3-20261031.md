# HDR optimization follow-up — 31 October 2026

This follow-up starts from the complete `.5` installer (`fdb37ace`). The solar transfer, raw-clear-depth horizon occlusion, native Ctrl+R ordering and direct two-texture UI fixes remain in place. Intentional Crystalsong foliage is unchanged. This document records independent changes separately from test cases and rejected experiments.

## Accepted work reductions

| Change | Removed work | Validation |
| --- | --- | --- |
| Batch stable quad state within each public HDR operation | Six repeated state setups in the seven-pass automatic-exposure/bloom finish; 240 API calls, with target/input hazard transitions retained | Hostile incoming game state, all auto/bloom/world-valid combinations, injected failure at every output transition and exact restoration |
| Bilinear metering reduction | 64 point samples become 16 exact 2×2 averages; static SM3 slots 330 → 91 | Both 64→8 and 8→1, constant/checker/impulse/noise/ramp; maximum FP32 reference error 0.000001431 |
| Dedicated vertical bloom kernel | Five point samples and the unused bright-extraction selection become four half-texel linear samples; slots 71 → 20 | Original 1,4,6,4,1 kernel, clamped edges, 1×1, odd and even dimensions; maximum FP32 error 0.000000477 |
| Neutral-contrast branch | Skip logarithm/exponent work when contrast is exactly one | Independent CPU grading formula: 270 combinations of source intensity, contrast 0.5/0.999/1/1.001/2, exposure and peak; exact alpha |
| Disabled-blend inspection | Avoid source/destination blend reads while blending is disabled, for fixed-function and programmable UI | Enabled/disabled blend, sRGB on/off, original state restored; failed sRGB read submits no draw |

The shader and UI changes are in `4ff690a2`; state batching is in `822a1d30`. The shared presentation/UI branch #94 includes `cd8e4def`. Its renderer does not use the full linear Frame pipeline; that implementation remains in #98.

These are five independent mechanisms, not 495 optimizations. Only the named reductions are established. Sixty accepted changes per direction have not been produced.

## Measurements and limits

The controlled final post-only benchmark compares identical harnesses and archived HDR headers from `fdb37ace`. It records source/header/backend/executable hashes and CPU submission independently from GPU elapsed time. It excludes the game, effect generation and presentation.

| 3840×2160 component | DXVK 2 reference → candidate | DXVK 3 reference → candidate |
| --- | --- | --- |
| HDR post, no MSAA, with legacy mirror | 0.17468 → 0.16668 ms | 0.16454 → 0.16324 ms |
| HDR post, MSAA8, without legacy mirror | 0.39832 → 0.39174 ms | 0.39087 → 0.39168 ms |
| Two UI state substitutions, no GPU draws, blend disabled | 0.000347 → 0.000319 ms | 0.000243 → 0.000236 ms |

The MSAA8 difference is small and DXVK 3 does not improve in this run. The quad-batching-only experiment reduced native CPU submission by approximately 0.001–0.0015 ms on DXVK 3; the combined final benchmark does not reproduce that CPU gain. Do not add the component figures, infer a whole-frame gain or claim the remaining HDR overhead is solved. Tiny UI timings also depend on compiler code layout and CPU scheduling.

The existing stationary client evidence still identifies FogMarch (approximately 1.20 ms) and FogClouds (0.63–0.68 ms) as major shared SDR/HDR costs. Reducing the original 8× MSAA, fog sampling budget, geometry coverage or effect quality is not part of this change.

## Rejected experiments

* Three-sample vertical bloom: hardware interpolation quantized the 1.2-texel weights. A 64-unit impulse produced 3.984375 instead of 4 in a tail sample. The four-sample version uses exactly representable half-texel weights and passes the stricter absolute-error oracle.
* Cloud march exit after its final possible emitting interval: FP16/FP32 outputs were identical across the authored controls, but a normal scenario regressed by approximately 17%. Applying it to actor variants also exceeded the existing 600-slot gate (607 slots). The production world shader is unchanged.
* Conditional omission of sRGB writes: reduced call count but repeatedly worsened the isolated two-texture CPU cost from about 0.000243 to 0.000383 ms per two substitutions on DXVK 3, including reversed executable order. These edits were removed. Disabled-blend reads were measured separately and retained.

## Reproduction and evidence

Run the complete renderer build, nine-workload composition graph, HDR Frame/sun numerical oracles and production proxy matrix:

```text
python tests/verify_hdr_pipeline.py --output <isolated-output>
```

Frame acceptance passes 495 cases per backend, including 113 fixed-function UI comparisons/fallbacks, 32 state/failure cases, 25 filter cases, 16 UI-state cases and 270 independent grading cases. Sun acceptance retains 28 cases per backend, wrong-depth and excess-energy negative controls and SDR/HDR shoulder comparisons. Boundary ownership, diagnostic gating/capture wiring and log-parser checks pass.

Compare post-processing and isolated UI costs against a specific existing commit:

```text
python tests/verify_hdr_frame_gpu.py --benchmark --reference-ref fdb37ace --dxvk2 <dll> --dxvk3 <dll> --output <isolated-output>
```

The fog runner now accepts `--backend` and `--baseline-ref`, records source/backend hashes and checks the actual process inventory before GPU work. Its identical-source FP16/FP32 control passes on DXVK 2. It remains an authored fog replay, not a whole captured game frame.

Private evidence is under `.local/hdr-autonomous-20261031`: `wave3-final`, `wave3-final-benchmark`, `quad-cpu-paired`, `ui-elision-isolation`, the rejected cloud experiment and the preserved complete `.5` client log. Raw account/game logs and captures are not published. The complete installer retains all previously integrated features.
