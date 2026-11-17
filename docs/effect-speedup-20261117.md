# Latest gameplay effects and imported fog

The completed client log was frozen before editing as
`.local/out/effect-speedup-20261117/gameplay-before.log`, SHA-256
`a6367c6c1799aa297f4f2ac7e73c0483eef18cd5380a6279ee8f8b6012f0c04f`.
It identifies source `c4f91893e9d34395ebf13a4c9996e1d5dd42e9f9f53707309caaeaec3fac7a20`,
DXVK 3.1.1, 3840x2160 and MSAA 2. The working tree started at `fe00d60a`;
the log does not establish build equivalence with that newer tree.
Shader sources and binaries from task entry are preserved under `baseline/`.

The extracted measurements are in `profile-summary.json` in that directory.
The GPU effect interval has median 9.437 ms, p95 14.009 ms. The GPU frame has
median 21.392 ms; its native scene interval is 9.442 ms and model-normal replay
is 1.262 ms. CPU capture has median 4.440 ms and effect submission 3.425 ms.
CPU and GPU overlap; independent medians must not be added.

| Effect interval | Median ms | Notes |
|---|---:|---|
| FogMarch | 4.202 | 61 samples; imported fallback dominates the expensive frames |
| PointShadow | 1.870 | 2.666 among the 59 samples with positive work; p95 6.500 |
| WorldComposite | 0.653 | Full-resolution composition |
| AO | 0.360 | Default AO method |
| TemporalLight | 0.352 | Includes merged removal smoothing |
| LocalLights | 0.259 | 0.279 among samples with positive work |
| GI | 0.236 | Probe shading; CPU probe solving is separate |
| AOBlur | 0.202 | Depth/normal-aware denoising |
| ShadowFilter | 0.153 | Directional visibility filtering |
| HDRBase | 0.115 | Separate HDR stage |
| WorldNormals | 0.102 | Half-resolution normals, separate from native model replay |
| FogBlur / FogTemporal | 0.079 / 0.050 | Separate fog stages |

The final part of the log also contains geometry upload refusals: 354 MiB of
aggregate free virtual space but only a 30 MiB contiguous block, below a 33 MiB
request. Frames with absent geometry and fewer active effects cannot demonstrate
an optimization. This change does not resolve that allocation failure.

## Implemented

The imported actor-fog fallback called the exact fine actor-map integral for
every interval. Regional fog already had an independently checked, conservative
64x64 minimum map and an interval rejection helper. The imported path now has
optional noisy/quiet variants that use that helper too. An interval that may
contain a caster still executes the original fine integral.

The existing wide-map readiness and camera proof gate selection. Imported
intervals also check their own projected endpoints: coordinates outside the
proven +/-32 domain go directly to the fine integral. Device instruction caps,
shader creation failure, unavailable tiles and shadow transitions retain the
ordinary actor shader. Reset releases both additional shader resources.
`FOG imported ... tiledShader=` reports whether the new fallback was selected;
the volume-atlas route is reported separately.

The non-transition imported shaders also avoid four regional metadata reads
outside the field. Coverage is exactly zero there and the original shader
already selected the imported global ground. The transition variants retain
their original operation ordering to stay within 32 SM3 temporaries.

No texture resolution, sample count, shadow caster, light, noise octave,
integration interval or update rate is reduced. The new path reuses existing
images; it adds two optional shader programs, not another target or pass.

## Measured complete fog pass

`final-timing/imported-fog-sparse/validation.json` binds the before/after
binaries, sources and backend. The hidden DXVK stand renders the entire imported
pass into 1920x1080 pixels (the game's half-resolution effect size), with the
same 48 intervals, three medium layers and optional animated 3D noise. ABBA
blocks alternate both shaders, discard warmup and use GPU timestamps.
The before binaries are the frozen task-entry production binaries.

| Authored actor map | Noise | Before ms | After ms | Speedup |
|---|---|---:|---:|---:|
| Empty | Off | 3.560 | 1.256 | 2.83x |
| Empty | On | 4.085 | 1.662 | 2.46x |
| Sparse, including a one-texel strip | Off | 3.485 | 1.569 | 2.22x |
| Sparse, including a one-texel strip | On | 4.089 | 2.046 | 2.00x |
| Distributed casters | Off | 3.414 | 2.197 | 1.55x |
| Distributed casters | On | 4.069 | 2.672 | 1.52x |
| Fully covered | Off | 3.598 | 3.548 | 1.01x |
| Fully covered | On | 4.067 | 4.111 | 0.99x |

These are authored component workloads, not a gameplay FPS measurement or a
guaranteed multiplier for every camera. Full-coverage work remains essentially
unchanged. The earlier per-block skipping experiment increased dense costs by
17–40% and was rejected.

## Quality and regression checks

`test_imported_fog_sparse_gpu.py` is in the GPU suite. It compares the complete
production shaders with nonzero fog and verifies that casters actually change
the result. Native D3D9 and DXVK cases cover noisy/quiet fog, sparse/thin/dense
casters, field boundaries and ray ranges 128/1000/4000. Alpha must remain exact;
maximum permitted RGB error is 0.00015. The complete-pass comparisons observed
at most 7.45e-9 error.

The same test runs the independent sorted-grid-plane CPU integral against GPU
visibility in FP16 and FP32. Rays include both signs, zero/near-zero axes,
diagonals, depth crossings, empty/full maps and long intervals that force the
fine fallback. The FP32 error remains approximately 6.6e-6; FP16 error stays
within its existing 0.0006 allowance. The actual GPU tile reduction is checked
against CPU minima. The ordinary midpoint destructive control remains available
in `validate_fog_actor_interval_gpu.py` without `--sparse`.

The shader manifest check verifies capability gates and resource release for
both variants. Run the source and GPU suites plus the production builder;
acceptance reports must match the final input tree.

## Other effects: measured experiments and remaining work

Two additional exact shader experiments did not justify a production change:

- Two composite taps per loop iteration preserved images but changed the
  4K/MSAA-2 HDR graph from 0.603680 to 0.601760 ms. Other cases were similarly
  unchanged; static slots increased from 761 to 861. Kept only in private output.
- Four fixed AO tap pairs instead of eight selections preserved all tested
  pixels. Heavy authored cases remained effectively unchanged, and the distant
  case regressed. Kept only in private output.

The remaining dominant target is point-shadow geometry. For example, frame
10287 submits 703 static and 1934 replay draws and 3,909,652 triangles for 12
selected lamps. All 779 replay bounds are valid, so treating missing bounds as
the cause would be wrong. The committed mesh groups ordinary static triangles
in 133.3-unit cells, much larger than many local light ranges. Finer conservative
caster ranges need an actual CPU/GPU and image comparison before changing that
tradeoff; more batches can increase submission cost.

AO/blur, temporal removal, GI and full-resolution composition still need
algorithmic work before claiming a multiple-fold improvement for each effect.
No such whole-pipeline claim is established by this patch.
