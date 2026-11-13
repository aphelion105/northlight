# Native light optimization, 2026-11-13

The optimized path preserves the eight-light defaults, local/shadow budgets and
GI quality. This is a measured component improvement, not a game FPS result.
Benchmarks use standalone fixtures and do not install the candidate into the client.

## Measurement

`scripts/benchmark_native_more_lights.py` compiles the saved previous manager and
the current manager into the same fixture. The baseline directory also retains
its previous shader transformer. Both builds run in ABBA order, serially on
native D3D9 and DXVK, on an NVIDIA GeForce RTX 4090 (10de:2684). Each case has
two warmup batches, twelve measured batches of 64 submissions, a quad or a
1536-vertex grid, and either authoritative cached constants or device getters.
Every case compares its image with an eight-light reference using scoped RGB
removal for the requested limit/ownership. Nonblack output is required.

CPU samples measure instrumented submission wall time, excluding waits and
readback. GPU timestamps cover the batch interval, including submission gaps;
they are not an isolated shader-ALU measurement. Results include raw samples.
The small benchmark does not cover game capture, streaming, crowds, complete
material coverage or whole-frame performance.

The following CPU times are microseconds per submission: medians across the
model/terrain cases and both repetitions, with cached constants and the grid.

| Case | D3D9 before | D3D9 after | DXVK before | DXVK after |
|---|---:|---:|---:|---:|
| All eight lights | 0.192 | 0.119 | 0.259 | 0.188 |
| Minimum native limit | 0.227 | 0.064 | 0.207 | 0.037 |
| No additional RGB | 0.196 | 0.070 | 0.257 | 0.046 |
| All extra RGB handed to Northlight | 0.229 | 0.083 | 0.207 | 0.056 |
| One extra RGB source handed over | 0.226 | 0.165 | 0.207 | 0.212 |
| Already-expanded shader, full limit | 0.136 | 0.075 | 0.103 | 0.038 |

The partial DXVK case has no established improvement; the small difference
must not be presented as an acceleration. Fully active GPU intervals are
essentially unchanged. For the dense minimum/empty/full-handoff cases, the
interval median falls from about 0.160 to 0.147–0.152 us on D3D9 and from about
0.192 to 0.176 us on DXVK. Absolute values and timestamp quantization make
whole-game extrapolation inappropriate.

After 520 unsupported shader registrations, the previous manager rebuilds the
ready source variant on the next draw; the new manager does not. The first
submission falls from 115.85 to 3.6 us on D3D9 and from 104.85 to 3.7 us on
DXVK. This control exercises source-cache eviction, not a cold GPU driver cache.

The fixture reads 5006 distinct client variants. Both builds accept the same
511 programs (223 model/WMO, 288 terrain, including 197 already expanded).
The combined emitted-program FNV-1a digest is `8d8ee385f05c5f20` in both builds
and backends. Benchmark images have zero maximum difference from their scoped
reference. The separate sum-of-original-draws oracle has maximum error
`2.98023224e-08`.

Local raw evidence is in
`.local/out/native-light-optimization/final/benchmark.json` and its per-run logs.
The baseline manager SHA256 is
`2912348c808f0e7c3807cdd43096fa615c60908d719f035f051e48558814e063`;
the baseline transformer SHA256 is
`e3d5b6426b83d69ab874a70ba4029cd70369a5cbbd26723c8eb62f4cb6cfc5ef`.

## Architecture changes

- Capability metadata is independent of bounded resource caches. Original
  variants and private derived variants each have a 512-entry cache. Unsupported
  original registrations retain no COM objects and cannot evict ready originals.
  Capability metadata survives resource eviction so later draws can rebuild.
- The original shader is used when it already computes the requested result:
  minimum limit, empty extra RGB or a complete extra-RGB handoff. Those cases
  remove private bindings and extra vertex-light arithmetic together.
- Full-limit already-expanded programs with no RGB handoff need no extra-bank
  read. Other RGB-only decisions borrow a proven mirror span or read the device;
  live state is not assumed from the public shader identity.
- Constants are copied only for a limit/ownership change. Only the changed
  register span is written and restored. Shadow-only bindings do not enable an
  otherwise unused RGB handoff.
- Source parsing collects constant references in one operand pass and rejects
  missing required light banks before the expression proof. Emitted shaders and
  accepted coverage remain identical to the baseline corpus.
- Restoration handles setters that fail after changing the backend, retries a
  transient restore once and runs on submission exceptions. Persistent restore
  failure stops this manager until successful device Reset; it cannot silently
  continue with private state left bound.

The GPU fixture protects cache pressure, full-state restoration, partial setter
failures, exceptions and persistent-failure recovery. The architecture suite
protects the shared draw integration. Source, affected GPU, packaging and actual
release archive verification remain release gates.

Remaining costs are intentional: proven active lamps still need illumination;
partial limits retain eight-light arithmetic; private material variants require
an actual bound-shader query; GI and shadow visibility retain their independent
calculations. No new shader-count variants, approximate visibility or quality
budget reductions were introduced.
