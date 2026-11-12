# r82: batched capture state and AO without water

Baseline: r81, `c6e434bc876f9aa557c27d53ecf636a8ce3fc833`.
Upstream comparison: `e19ffee601acbad2b991917109bcb6d8b2f7d5ca`.
Measured and production source identity:
`968a89fe38a26fcd8929bf0138acb0d26ebb7ce352ad554650b2bb302ab27b8c`.

## CPU capture

Model shadow capture reads colour-write, blend and alpha-test state together
through the existing known-only ExtensionDevice mirror. Three mirrored Get
calls and recursive gate entries become one batch. Any unknown member, disabled
mirror, recording or raw scope retains the original individual Get calls and
their error behavior. Fade, blend, alpha, capture admission and budgets remain
independent. Materials, normals and shadows share one existing reader callback;
no state cache, packet fields or synchronization mechanism is added.

The fixture now includes the real ExtensionDevice/DeviceMirror, with the same
wrapper on upstream, r81 and r82. Its absolute timings must not be compared
with r81's earlier direct-device fixture. Ryzen 9950X3D / RTX 4090 / DXVK 3.1.1;
512 repeated rigid captures including source acquisition and packet recycling.
Three ABBA rounds, six runs per side, 32 measured frames after 16 warmup frames.
Medians of run medians, ms:

| Comparison | Before | After |
| --- | ---: | ---: |
| Upstream → r82 | 0.21188 | 0.14990 |
| r81 → r82 | 0.15573 | 0.15033 |
| Same executable, batch off/on | 0.15703 | 0.14978 |

This is about 3.5% faster than r81, or 4.6% in the same-executable control.
All 512 packets, their 884,736-byte admission charge and six point-shadow faces
remain bit-exact. Warm binding queries already hit the mirror on both sides.
There are 512 state batches per measured frame. Native D3D9 and DXVK 2/3 pass
22 on/off state cases each: unknown members, recording, raw/disabled states,
native Get failures, fade, blend and unsupported alpha comparisons.

This measures capture plumbing, not material evaluation, skinning, the game's
native draws or a total CPU frame.

## GPU AO

AOBlurNoWater uses the same bilateral filter helper with water checks compiled
out. It is selected only when the coherent water-mask pointer is absent.
Water and optional shader-creation failure retain AOBlur; reset clears the
optional shader/refusal state. No pass, target, mask, sample-quality setting or
filter tap is added or removed. All 190 existing shader binaries remain
byte-identical. The one new shader uses 315 SM3 slots / 15 temporaries versus
362 / 16 for AOBlur, and does not sample water.

The standalone timing matrix uses 16 ABBA-ordered blocks of 16 draws, discarding
the first four blocks. The production shader matches the measured prototype
byte-for-byte. Across the six filtered dry shapes at 1080p and 4K, the median
new/old ratio is about 0.869: roughly 13% faster. Unoccluded early-return cases
are unchanged. At 4K, representative filter-only timings are:

| Authored input | AOBlur → NoWater, ms |
| --- | --- |
| Depth step | 0.12707 → 0.11693 |
| Rippled surface | 0.14051 → 0.11472 |
| Sparse near-unoccluded AO | 0.13030 → 0.11315 |

These gains belong to active filtering, not the entire frame. Native D3D9 and
DXVK 2/3 each pass 160 correctness cases, including odd sizes, FP16/FP32 input,
MSAA 0/2/4/8 where supported, silhouettes, sky, water, and bright HDR bloom
without water. DXVK 2/3 outputs are bit-exact. Native differs by at most one
FP16 ULP in AO (maximum absolute error 0.000488281); bloom RGB and the water
fallback remain exact.

Full unrolling exceeded SM3's 512-slot limit. Partial unrolling gave no
consistent gain, and early branches around invalid taps slowed the GPU by
23–25%. These prototypes are not shipped.

## Common graph and limits

The corrected r81 fixture is retained: fixed 4K colour/depth, authored casters,
fog and probes, SDR, valid native M2 lamps, explicit point-shadow enable and
per-measured-frame cube/update assertions. Three ABBA rounds, six runs per
side, 32 measured frames after 16 warmup frames:

| Workload | GPU upstream → r82, ms | GPU r81 → r82, ms |
| --- | --- | --- |
| AO/blur/composite | 0.54093 → 0.54277 | 0.54346 → 0.54329 |
| Regional fog | 0.84890 → 0.73448 | 0.73523 → 0.73633 |
| GI, fog, directional/point shadows, lamps | 1.09837 → 1.01683 | 1.01478 → 1.01581 |
| GI, fog, lamps, shadows off | 0.99736 → 0.89549 | 0.89310 → 0.89434 |

GPU differences versus r81 stay within 0.15%; all final SDR images are
bit-exact. This captured depth often takes AOBlur's early return, so the
specialization has no material whole-graph gain here. A separate nearby
authored ripple-depth graph also changes only 0.69650 → 0.69569 ms; it does
not justify claiming a general graph speedup. Graph CPU changes versus r81
range from -0.00025 to +0.00178 ms; unconditional CPU parity is not claimed.
Existing PR lighting/fog semantics differ from upstream.

The graph excludes draw hooks, model/material capture, real caster density,
HDR/weather and presentation; timestamps may include submission gaps. The
capture and graph fixtures must not be added into a total frame cost.
**The total 2–3 ms Northlight target remains unconfirmed.** QA/client logs
remain unchanged and identify r73, not this build.

## Verification and reproduction

Ten related regression scripts pass, including AO math/wiring, capture
admission, fade handling, mirror diagnostics, terrain routing, draw state and
ASan/UBSan. Windows material-geometry checks also pass. Tests use standalone
hidden devices; client files and game processes remain untouched.

```powershell
python tests/benchmark_capture_handoff.py --upstream $upstream --baseline $r81Checkout --raster-states --backend $backend --output $newCaptureOutput --rounds 3
python tests/validate_ao_blur_equivalence_gpu.py --baseline-ref c6e434bc --backend $backend --output $newAoOutput --no-water --benchmark
python tests/benchmark_upstream_pipeline.py --upstream $upstream --baseline $r81Checkout --capture $capture --backend $backend --output $newGraphOutput --rounds 3
```

Receipts: ignored `out/optimization-20261110-r82/`: `capture-comparison/`,
`ao-dry-gpu/`, `ao-native-hdr/`, `ao-dxvk2-hdr/`, `ao-dxvk3-hdr/`,
`ao-variant-equivalence.json`, `shader-retention.json`, `pipeline-comparison/`,
`ao-graph-stress.json`, `regressions/` and `material-geometry.log`.
`test-source-equivalence.json` preserves the exact measured capture test
sources and verifies that their final normalization changed only line endings.

Production Zig 0.15.2 DLL/package `0.3.217-test.20261110.7` pass compiler
preflight, shader freshness, x86 PE/17 exports, payload equality and package
verification. Source identity matches the measured production code; build
time is `2026-11-10 03:39:58 UTC`. Receipts: `artifacts.json` and
`package-verification/verification.json`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `Northlight-0.3.217-test.20261110.7/d3d9.dll` | 10,852,864 | `551085ffe1236ca89248989a8444bc6d50c59e5f034de7917ea5518ac1364773` |
| `Northlight-0.3.217-test.20261110.7-Windows.zip` | 20,820,246 | `a82692bfdac5f248bd134c23c22df4c5491c553389f2c5640794e6c7f818855b` |
