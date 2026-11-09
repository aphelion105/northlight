# DXVK 1/2 throughput follow-up (r43)

The renderer still tied two optimizations to DXVK 3's CPU buffer-readback flag.
That flag describes `Lock` behavior, not shader equivalence or pipeline warmup.
The device now selects those paths from the runtime actually loaded, with
independent validation requirements. Failed loads, system fallback and unknown
DXVK 1/2 versions retain their previous paths.

| Loaded runtime | Neutral-input HDR compositor | Init-only point pipeline warmup | Queued material-cache readback |
| --- | --- | --- | --- |
| DXVK 1.10.3 | Ordinary shader retained | Enabled | Enabled |
| DXVK 2.7.1 | Enabled | Enabled | Existing path retained |
| DXVK 3.1.1 | Existing path retained | Existing path retained | Existing path retained |
| Native D3D9 | Unchanged | Unchanged | Unchanged |

The existing DXVK >= 3 compositor/warmup eligibility is preserved. Async material
readback remains restricted to the three tested versions. Backend filenames and
configuration labels alone never establish these capabilities. `BACKEND_WORK`
logs all three decisions; source-buffer lock flags and EVENT payload policy are
unchanged.

## Measurements and correctness

One frozen 3840 x 2160 Weather-only scene, actual WorldRenderer, DXVK 2 ABBA:

| GPU interval | Ordinary HDR | Specialized HDR | Reduction |
| --- | ---: | ---: | ---: |
| WorldComposite | 0.236800 ms | 0.172544 ms | 27.1% |
| Measured pipeline | 0.441856 ms | 0.377088 ms | 14.7% |

The SDR/HDR color, current-distance and fog readbacks match exactly. The fixture
also checks enclosing-state restoration, invalid-state fallback and injected HDR
draw failure. Direct compositor tests pass 32 exact comparisons on DXVK 2/3:
depth edges, odd sizes, sky, water, fog, haze, night, emission, coverage, rain,
held/identity fog and floating-point scene inputs. The existing CPU neutrality
proofs and optional-shader failure fallback remain in force.

An isolated first-use test of 42 shadow-cube faces measured 64.37 -> 19.33 ms on
DXVK 1 and 10.94 -> 1.04 ms on DXVK 2 after the existing init-only warmup. These
are single cold/warm submission-to-completion observations, not gameplay FPS or
a latency guarantee. All 42 faces match exactly; bindings, shader constants,
scene balance, disabled-feature zero-work behavior and fault restoration pass.
The helper starts private draws during device initialization, publishes no shadow
cube and adds no polling loop or Present call.

The former DXVK 1 lockable-render-target persistence path fails the fixture's
cache-reload assertion after Reset. Its replacement queues `GetRenderTargetData`
before the EVENT and maps the system-memory destination only after real
completion. This matches the upstream [DXVK 1.10.3 implementation](https://github.com/doitsujin/dxvk/blob/v1.10.3/src/d3d9/d3d9_device.cpp#L785-L837),
which enqueues the copy. The existing one-copy-per-frame limit, DONOTWAIT retry,
four pending-save limit and disk worker remain intact. The final-source tests
pass 21 separate processes on DXVK 1 (eight source formats) and nine on DXVK 2,
covering exact map/mip bytes, cache hits, corruption repair, settings changes,
busy backoff, upload retry and Reset reuse.

The composite GPU fixture also had a `checked` helper that collided with local
map counters in dependent fixtures. Renaming that helper restores compilation
without changing test assertions. A new test compiles the actual loader/device
selectors across 512 combinations; the previous selectors fail its negative
control. Seven focused tests pass. The host-clang lock-model test is skipped
because clang++/c++ is unavailable; the separate production lock audit passes.

## Remaining gaps

Performance parity with DXVK 3 is not established. In this stand the new DXVK 2
pipeline measures 0.377088 ms versus 0.277248 ms on DXVK 3; scene copy and world
lighting account for much of the remaining interval. GPU timestamps also include
command-supply gaps, so these figures do not isolate shader execution alone.

The specialized compositor is deliberately still disabled on DXVK 1: one odd-size
fixture changes one FP16 component by 0.00048828125. The exact comparison threshold
was not relaxed. Its ordinary compositor remains active. Point warmup also does
not cover every native game shader or eliminate all cold compilation.

No AO, shader algorithm, quality setting, shadow coverage or geometry-distance
reduction is included. No WoW session or Mac/MoltenVK measurement was performed.
Machine-readable receipts and frozen inputs are under
`out/perf-20261108-r43/validation.json`; the DLL receipt is under
`out/dll-20261108-r43/build-validation.json`. No installer or client installation
is part of this iteration.

The final production DLL passes PE/export/freshness/identity checks and an
isolated proxy smoke on DXVK 1/2/3: 192 real EVENT generations, correct BOOL/NULL
payload handling and the expected `BACKEND_WORK` decisions. Its SHA-256 is
`b1af25659da5ffcc65ba92d541dbf548adc0cd21704ffa1ea7dac40d48af05a9`.
