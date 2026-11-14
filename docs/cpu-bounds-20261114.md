# CPU pose-bound optimization, 2026-11-14

The latest gameplay log still ends at 02:20:51 on November 14. It predates the
later normal-batch and expanded-HDR changes. Its ten CPU samples show capture
at 2.445 ms median / 4.752 ms p95; the separately measured pose-envelope stage
is 0.4252 ms median / 0.4873 ms p95. These nested measurements must not be added
together. No new gameplay FPS measurement is available.

Source residency and prepared shader metadata already avoid rereading geometry.
Frame 2921 has 752 resident hits, no geometry source bytes read, and 652 pose
metadata hits with no misses. Warm evaluation still calculates the current pose.

## Change

The audited single-bone interval evaluator computes X/Y together with SSE2.
Each DP4 retains its original four multiply/add steps, float value rounding and
double error propagation. In particular, the sum of two absolute float values
is rounded to float before adding the double inherited error. Reassociating
that expression changes the bounds and is not allowed. Z and platforms without
SSE2 keep the scalar calculation. Unsupported numeric inputs still fall back to
the general interpreter.

The compact input path prepares only the bone-index X component consumed by
this kernel. If it declines, the other three components are reconstructed before
calling the interpreter. Generic programs retain all inputs.

Generic envelope bounds reuse the existing `outwardNormal` helper: ordinary
IEEE float neighbors use integer bit steps; boundary, subnormal, infinite and
NaN cases retain CRT `nextafter`. Cache ownership, memory limits, operation/time
quotas, deformation and rendering settings are unchanged.

## Paired CPU measurements

Baseline: `341c80b86a8af08510c92714a8d32abf84db0328`. The reproducible tool is
`scripts/profile_material_bounds.py`; it freezes the old dispatcher and old
single-bone kernel from Git. Each batch has 16 draws and 96 different poses,
real clock overhead, alternating ABBA order, and ten measured batches per side.
It refuses insufficient complete batches and records any discarded budget-limited
batches. Work counts and bitwise bounds are compared with a deterministic clock.

Two independent runs, CPU microseconds per 16-draw batch:

| Bone groups / resident sources | First before → after | Repeat before → after |
|---|---:|---:|
| 1 / 2048 | 3.490 → 3.148 | 3.488 → 3.182 |
| 4 / 512 | 7.543 → 6.708 | 7.567 → 6.770 |
| 32 / 128 | 67.806 → 65.363 | 67.267 → 64.795 |
| 63 / 64 | 135.105 → 129.461 | 133.515 → 129.026 |
| Four-weight control / 64 | 4.123 → 4.056 | 4.098 → 4.063 |

All A/B batches completed the same work. The affected cases improve about
3–11%; this is a CPU component result, not that percentage of game FPS.
The unchanged four-weight path is a control, not an optimization claim.
The independent A/A control differs by up to 5.7% in the shortest case and
about 1% for the larger cases; one heavy A/A batch exhausted its budget and
was excluded. Small timing differences alone are not acceptance evidence.

Local evidence is under `.local/out/test-output/cpu-bounds-20261114/`:
`paired`, `repeat`, `control`; final validation/build reports use `final-*` and
`release`. Inputs and individual samples are retained in JSON.

## Verification and reproduction

`test_single_skin_intervals.py` compares against the general shader interpreter
across 400,000 cases, all four rounding modes and all four FTZ/DAZ combinations,
with O2, O0, and address/undefined sanitizers. It checks exact values/errors and
accepted/fallback behavior. The CPU benchmark also compares 1,920 complete
bound/status/work results with the old code.

The `cpu-bounds` suite covers envelope budgets, palette overrides, metadata
ownership, coarse admission and the weighted evaluator. Run it alongside the
source suite and affected material GPU image/state contracts:

```powershell
python scripts/run_tests.py --suite source --strict
python scripts/run_tests.py --suite cpu-bounds --strict
python scripts/run_tests.py test_m2_material_admission.py test_m2_material_preflight.py --strict
python scripts/profile_material_bounds.py --reference 341c80b8 --output .local/out/test-output/cpu-bounds-repeat
python scripts/profile_material_bounds.py --reference 341c80b8 --control --output .local/out/test-output/cpu-bounds-control
python scripts/build_renderer.py
```

Under this machine's RDP session GPU fixtures need explicit DXVK display
enumeration/device selection. The material fixture honors and records these
process-local environment overrides without changing game configuration.
Forever may remain running; shared-GPU timings are not clean gameplay comparisons.
GPU image equivalence and CPU-only paired timing are separate checks.
