# r89: point-shadow preparation and lighting diagnostic CPU cost

Base: r88, `2d97abd9c5134d980014e7ce61308a405c066db5`.
Production source identity: `268b38d662ddfbd7c6faaf808185740f1600dbfa01732cd3f19f4aa375fe01fb`.

## Changes

Point-shadow preparation no longer reconstructs unused cube slots or clears
already-empty static keeps every frame. Allocation and retained static faces
acquire their map before use; a cube without a map still goes through retirement.
Outgoing resources are released before replacements are allocated, as before.
Selected, prewarmed, failed and mapped slots retain the previous handling.
No new occupancy flag, cache, allocation or quality setting is introduced.

The effects scope now uses the existing `LogFlushBatch`. Sampled light/caster
diagnostics flush together before returning, including on early returns and
exceptions. The final flush remains inside the effects wall-time measurements
and bucket attribution. Formatting, text, synchronization and error accounting
are unchanged. With no log lines the scope performs no file I/O.

No shader or effect calculation changes. GPU passes and independent histories
remain unchanged.

## CPU measurements

One executable contains the previous and current `renderPointShadows` methods;
all other methods and resources are shared. Each light count has four warm-up
and twelve measured ABBA blocks, 256 calls/block, six samples per side. Cubes
are rendered before timing; both methods reuse complete shadows with stale
capture. Timed calls submit no GPU work. Readbacks and assertions are outside
the timed interval. This isolates warm pool preparation, including selection.

RTX 4090, DXVK 3; microseconds per call:

| Selected lights | r88 | r89 |
|---:|---:|---:|
| 0 | 2.047 | 0.244 |
| 1 | 2.075 | 0.312 |
| 4 | 2.203 | 0.470 |
| 16 | 2.837 | 1.250 |
| 64 | 5.656 | 4.693 |

Native D3D9 and DXVK 2 also improve at every tested count. The one-light
saving is only about **0.0018 ms**, despite its large relative percentage.
It does not explain or eliminate the multi-millisecond gameplay spikes.

The actual Windows logger separately writes 48 authored equipment-light
diagnostic lines per iteration to a real temporary FILE. Same-executable ABBA,
four warm-up blocks plus six samples/side, 64 iterations/block:

| Diagnostic write | CPU including final flush | `fflush` calls |
|---|---:|---:|
| Previous | 0.330875 ms | 48 |
| Batched | 0.254823 ms | 1 |

Text is byte-exact. This saves about **0.076 ms / 23%** for that diagnostic
workload, rather than for every gameplay frame. The timing guard reported no
WoW process at the start; the tests launched or stopped no game process.

## Fresh gameplay log

Snapshot: 475,075 bytes, SHA-256
`6852fd08a0bd4f40c4436625c0a04716612bfb6ad7e3cc625cbc13c46e365918`.
The recorded source identity matches r88. The moving route is observational,
not a controlled A/B benchmark; different sampled stages must not be summed.

At renderer frames 508 and 635, CPU capture is 7.825 and 4.977 ms, while
WorldLighting GPU time is 0.100 and 0.032 ms. The latter uses the soft variant,
whose preceding ShadowFilter costs another 0.154 ms. ModelNormals GPU time is
3.167 and 1.161 ms. AO plus blur costs approximately 1.06–1.08 ms. Thus this
log does **not** identify the WorldLighting shader as the largest expense.

Lighting setup reports 0.497 and 0.411 ms in its constants scope. That scope
also logs dozens of equipment/particle lines synchronously, explaining why
its measurement includes diagnostic work. The batch change reduces this
work without moving its cost out of the total CPU measurement.

At frame 635, ModelNormals reuses every captured source (450 handoffs, zero
source-read/upload bytes), but still submits 281 draws after 169 merges.
Material CPU profiling also records 459 normal-build attempts / 0.567 ms;
the cumulative normal cache counters show substantial pending/deferred work
under a 32 MiB budget. These remain more relevant follow-up targets than the
small warm point-pool cost. This change does not increase memory budgets.

The cold frame 514 has upload, static preparation and directional-shadow
spikes. Other slow frames include large command-stream idle/query spans;
their elapsed spans do not establish that a light shader or the query API
itself consumed that CPU time. Full gameplay overhead of 2–3 ms remains
unproven and is not claimed here.

## Checks and reproduction

- `tests/test_point_shadow_stutter_gpu.py --benchmark-preparation-from 2d97abd9 --output <fresh-folder>`:
  native D3D9, DXVK 2 and DXVK 3 pass. Six cube faces remain exact, including
  source/map/limit/pressure changes, partial cycles, stale capture, failed
  draws, recovery, prewarming, static retention, replay bounds and skinning.
  The new probe checks empty-resource invariants and retirement of a partial
  cube without a map and a mapped retry-only slot.
- `tests/test_frame_logging_batch.py --benchmark`: Windows O0/O2, exact
  text/binary output, nested scopes, exceptions, concurrent writers and I/O
  errors pass. Scope-order assertions keep flushing inside the CPU timers.
- Point schedules, effects buckets and frame-stall accounting pass native
  WSL tests; applicable ASan/UBSan runs pass. Extracted production logger
  passes its concurrent-writer and injected-error checks with ASan/UBSan.
- Capture-skip wiring passes. The AO submission fixture was stale: it lacked
  the existing water/no-water shader fields. It now checks all 64 water,
  optional-shader, timing and failure combinations, plus the query ring.
- Production build, shader freshness and Windows package verification pass.

Local receipts: `out/optimization-20261110-r89/`, especially
`point-final/validation.json`, `logging/validation.json`, `log-observations.json`,
`native-tests.log`, `ao-instrumentation.log` and `artifacts.json`.

## Artifacts

Build: `0.3.217-test.20261110.15`, 2026-11-10 09:40:19 UTC. x86 PE, all
17 expected exports, embedded source identity and byte-identical packaged DLL
verified. All 191 precompiled shader binaries are unchanged from r88.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `releases/Northlight-0.3.217-test.20261110.15/d3d9.dll` | 10,853,376 | `2fa39365b7d18f716c59c3382f336ca66b893d2ff09e48a23590ccbd2a413ad7` |
| `releases/Northlight-0.3.217-test.20261110.15-Windows.zip` | 20,821,138 | `b388239f572c8f6baa2e040da51df56238e1cf4bff37eac0de6abee43fd7455e` |

No DLL was installed in the client directory.
