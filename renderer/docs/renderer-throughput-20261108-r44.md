# Latest game log and common CPU paths (r44)

The supplied run uses r43 (`cbc4ba4d...`), DXVK 3.1.1, 3840 x 2160 HDR,
MSAA off, on an RTX 4090. It is not a DXVK 1/2 run. Backend comparisons need
matching game workloads; the log cannot establish their relative game FPS.

## Logged bottlenecks

World-loaded samples/windows, excluding the initial menu/loading frames:

| Observation | Median | Maximum |
| --- | ---: | ---: |
| Mean frame interval per 120-frame window | 21.851 ms | 44.819 ms |
| CPU capture, sampled frames | 3.190 ms | 4.733 ms |
| CPU effects, sampled frames | 2.556 ms | 4.783 ms |
| GPU frame interval | 16.720 ms | 22.027 ms |
| GPU GameScene interval | 10.248 ms | 14.291 ms |
| GPU ModelNormals interval | 1.154 ms | 1.940 ms |
| GPU Effects interval | 3.858 ms | 9.763 ms |
| Selected EVENT frame aggregate, before replay | 7.235 ms | 41.004 ms |
| Selected EVENT frame aggregate, total synchronization | 21.261 ms | 51.401 ms |

These are different sampled domains, not additive costs. Slow EVENT frames are
selected diagnostics, not an unbiased distribution. GPU timestamps include
command-supply gaps. Replay frame 2413 separately reports 9.514 ms across 2,838
measured DrawIndexedPrimitive commands; that includes hooks/backend submission,
not just GPU shader time. The producer's final 600-frame window reports
1.740 ms/frame in snapshot capture and 11.503 ms/frame excluding waits.

Frame 1946 spends 26.608 ms in the memory-guard boundary: snapshot destruction
15.059 ms, pool destruction 3.404 ms and material cap work 3.599 ms. Only
360 MiB of the 32-bit address space is available at the trigger. A later geometry
worker wait lasts 16.359 seconds; this is a worker admission stall, not one
16-second frame. Neither observation is explained by HDR composition alone.

## Changes

The native scene-light collector used linear asset searches and recomputed the
sum of all other assets' retained bytes for every model runtime. The shared CPU
path now indexes assets and maintains an exact byte ledger, including animated
curve growth/eviction and metadata replacement. Reused shared pointers retain
the original list-order fallback. Live model identity, placement, alpha,
attachment, animation and scene-header checks remain in place.

On the 384-model/96-asset CPU fixture, ABBA observations are 0.188579 ->
0.123652 ms per update, a 34.4% reduction including fixture overhead. This is
not a game FPS prediction; concurrent host work may affect component timings.
Light values match the frozen r43 implementation byte for byte through pose,
alpha, header and Reset changes. Actual MPQ lamp/attachment fixtures also pass
at O0 and O2.

The idle Replay pool previously charged only vertex/index vector capacities.
Empty records still contain constant banks and metadata, so their memory could
exceed the stated cap. The existing 48 MiB / 24 MiB limits now include those
records. Active captures and abandoned-worker quarantine retain their ownership
rules. This bounds optional retention; it does not reduce captured geometry,
shadow distance, source count or effect resolution.

These changes run in the common pipeline on DXVK 1/2/3 and native D3D9. No new
backend-specific rendering branch or shader algorithm is introduced. AO and
mandatory critical-memory reclamation remain unchanged.

## Validation and limits

- Nine focused CPU/policy/shader-manifest tests pass without skips.
- The extracted production Replay pool passes 10,000 reuse cycles, COM/byte
  ownership checks and record/snapshot/lowered-cap edge cases with assertions on.
- The memory-guard source audit passes. Its reset-signature lookup was updated
  to the existing `preserveCaptureSources` API; critical trim policy is retained.
- Real cached-scene point-shadow tests pass on DXVK 1/2/3: 2,034 exact faces and
  8,136 recovery faces per backend, including offscreen casters and trim recovery.
  Running alongside WoW was explicitly allowed for correctness; those GPU test
  timings are not used as performance evidence.

The log's remaining EVENT waits, 26.608 ms critical clear and large draw workload
are not claimed eliminated. No new game FPS measurement, client installation,
installer or subagent work is included. Receipts are in
`out/perf-20261108-r44/validation.json` and `log-analysis.json`; the production DLL
receipt is `out/dll-20261108-r44/build-validation.json`.
