# r83: cheaper conservative point-shadow culling

Baseline: r82, `1dccf33fb804a21fd793c6fa831c138a385e3d4b`.
Upstream: `e19ffee601acbad2b991917109bcb6d8b2f7d5ca`, confirmed as
`origin/main` during this run.
Measured and production source identity:
`b7fc43d3fabc143918334d6dc0f99000b1743ad6d3efc19b80b7d20131828bff`.

## CPU

The shared prepared cube clipper previously checked floating-point overflow
separately for every column and plane of every requested face and candidate.
It now proves ordinary coordinates safe once per candidate. The old individual
guards remain the fallback for extreme inputs. Sphere rejection, clip planes,
cancellation margins, invalid-input behavior and requested-face masks stay the
same across static, terrain, live, replay and content-key paths.

The proof stores one additional double in the existing prepared clipper:
eight times the largest absolute finite matrix coefficient. With
`E = max(1, abs(bounds))` and `M = max(abs(matrix))`, a column's magnitude is
at most `4*M*E`, and a plane's magnitude at most `8*M*E`. Requiring
`8*M*E <= FLT_MAX/64` leaves rounding slack below the old column `/32` and
plane `/16` guards. Nonfinite matrices still fail open. No new cache,
allocation, pass, shader, mask or effect dependency is introduced.

Windows x86, Zig 0.15.2, O2, frozen r82 and r83 in the same executable:
8192 authored boxes repeated 20 times per sample; 16 ABBA blocks, first four
discarded, six measured samples per side. Median CPU milliseconds per
163,840 candidate masks, with identical masks checked before timing:

| Box spread / light radius | Requested faces | r82 → r83, ms |
| --- | ---: | --- |
| 32 / 48 | 6 | 12.92800 → 10.37830 |
| 32 / 48 | 2 | 5.81130 → 4.81655 |
| 32 / 48 | 1 | 3.63705 → 3.10325 |
| 96 / 48 | 6 | 1.64705 → 1.35680 |
| 96 / 48 | 2 | 1.02500 → 0.91495 |
| 96 / 48 | 1 | 0.90645 → 0.82830 |

The prepared-mask loop improves 8.6–19.7%. Preparation happens before that
timing. A second ABBA test includes preparation on every update, perturbing
one matrix coefficient and using a non-inlined update to prevent hoisting:

| Candidates per preparation | r82 → r83, ms per 4096 updates |
| ---: | --- |
| 0 | 0.51475 → 0.57535 |
| 1 | 0.73920 → 0.72055 |
| 8 | 2.27490 → 1.73790 |
| 64 | 17.72640 → 12.27130 |

Preparation alone costs about 15 ns more per update. One candidate is roughly
flat; eight and 64 improve about 24% and 31% in this fixture. An initial scalar
maximum introduced a serial dependency and cost about 107 ns more just to
prepare; four independent float reductions retain vectorization and replace
that prototype. Neither test measures a complete capture, point-shadow pass
or frame. The clipper grows from 3160 to 3168 bytes, without heap allocations.

## GPU investigation

Two isolated prototypes were measured and excluded from production:

- An opaque-only point caster shader reduced static instructions but did not
  provide a consistent GPU improvement over the existing runtime branch.
- Unrolling standard four-tap PCF grew LocalLighting from 353 to 461 SM3
  instruction slots, for only 0–1.2% in the authored 256² timing fixture.

Those fixtures use 16 ABBA blocks with four warmup blocks, FP32 output
comparisons and separate clipping/blending or empty/checkered/blocked cube
controls. Sub-microsecond caster timings and roughly 0.00136 ms receiver
timings do not justify another shader variant or a larger shader. Neither
prototype is shipped. R16 temporal-distance storage was considered but not
implemented or benchmarked; safe far-range handling needs wider validation.

All production GPU shaders remain byte-identical to r82.

## Complete graph and verification

| Workload | CPU upstream → r83, ms | GPU upstream → r83, ms |
| --- | --- | --- |
| AO/blur/composite | 0.02095 → 0.01338 | 0.54170 → 0.54297 |
| Regional fog | 0.02973 → 0.02175 | 0.84886 → 0.73526 |
| GI, fog, directional/point shadows, lamps | 0.04405 → 0.03563 | 1.09820 → 1.01811 |
| GI, fog, lamps, shadows off | 0.03210 → 0.02513 | 0.99686 → 0.89566 |

Against r82 in separate interleaved baseline rounds:

| Workload | CPU r82 → r83, ms | GPU r82 → r83, ms |
| --- | --- | --- |
| AO/blur/composite | 0.01340 → 0.01350 | 0.54197 → 0.54173 |
| Regional fog | 0.02233 → 0.02210 | 0.73334 → 0.73331 |
| GI, fog, directional/point shadows, lamps | 0.03643 → 0.03475 | 1.01248 → 1.01427 |
| GI, fog, lamps, shadows off | 0.02678 → 0.02685 | 0.89378 → 0.89359 |

All final SDR images match r82 bit-for-bit in each workload, with unchanged
draw/copy/target counts. GPU variation stays within 0.18% of r82. CPU improves
0.001675 ms in the shadow workload; other changes range from -0.000225 to
+0.000100 ms. These tiny graph changes do not establish a general frame gain.
The existing PR's lighting/fog semantics differ from upstream, whose AO-only
GPU result is slightly faster (about 0.24%) in this run.

The common graph uses fixed 4K captured scene/depth with authored casters,
lamps, fog and probes, valid enabled point-shadow cubes, SDR output, three
ABBA rounds, six runs per side, and 32 measured frames after 16 warmup frames.
It excludes draw hooks, material/model capture, real caster density,
HDR/weather and presentation. GPU timestamps can include submission gaps.
Do not add this graph's timing to the culling microbenchmark.
**Total Northlight cost of 2–3 ms remains unconfirmed.** Available client
logs still identify r73, not this build.

Prepared clipping matches the independent scalar reference in 174,437 cases
under O2 and ASan/UBSan, including adjacent floats around the new proof's
threshold, arbitrary matrices, invalid bounds and overflow. Native D3D9 and
DXVK pass the full-renderer point-terrain, cache reuse, face union, visibility
transition and failure controls. The receiver oracle passes 648 scenarios
per backend with exact lit/blocked results. Point-shadow math/artifact checks
and native ASan/UBSan also pass. Two stale test callers/assertions were updated
to the existing fused PCF/PCSS API; production filtering is unchanged.

```powershell
python tests/benchmark_point_clip.py --baseline $r82Checkout/src/lights/point_light_shadow.h --output $newClipOutput
python tests/test_point_clip_prepared.py
python tests/test_point_light_shadow.py
python tests/test_point_terrain_coverage_gpu.py
python tests/test_local_shadow_receiver_gpu.py --backend $backend --compatible-compiler
python tests/benchmark_upstream_pipeline.py --upstream $upstream --baseline $r82Checkout --capture $capture --backend $backend --output $newGraphOutput --rounds 3
```

CPU sanitizer checks above run in WSL; native/GPU checks and builds run outside
the restricted Windows sandbox. Receipts are under ignored
`out/optimization-20261110-r83/`: `clip-release/`, `clip-ship-correctness.log`,
`point-math-final.log`, `point-ship/`, `receiver-validation/`,
`pipeline-ship/`, `point-shader.json`, `pcf-gpu.json`,
`shader-retention.json`, `artifacts.json` and
`package-verification/verification.json`. Tests use standalone devices;
installed client files and game processes remain untouched.

Production DLL/package `0.3.217-test.20261110.8` pass Zig 0.15.2 compiler
preflight, shader freshness, x86 PE/17 exports, exact installer payload and
release-package verification. DLL source identity matches the final measured
graph and current source. Build time: `2026-11-10 04:42:51 UTC`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `Northlight-0.3.217-test.20261110.8/d3d9.dll` | 10,853,376 | `eb24b9f0e46fd03166f870d028fefbc3b0c3b58423ce3405aa2c461f24c691cf` |
| `Northlight-0.3.217-test.20261110.8-Windows.zip` | 20,820,386 | `85735e635bed7ca206634b5a6cd5829ac4f859b695f5d99fb86db330bbebd88f` |
