# r93: reduce pose dispatch clocks and diagnostic formatting

Base: r92, `d24ec9ab3794a12200e148c237fe582f5a852215`.
Source identity: `27ae785836809947a41286f42111984c93f6a3c5bf0ea067b05c2cc9e01aab02`.

## Fresh gameplay evidence

The closed 3,476-frame DXVK 3 session in
`D:/Games/335_engb_mod/northlight-renderer.log` identifies the r92 DLL:
source `7c7c3bea1192e1bbecde8ade1b18a9090d2748c540a96a33a7513c6965e590e8`,
built 2026-11-10 11:58:30 UTC. The 1,809,194-byte snapshot is
`out/optimization-20261110-r93/client.log`, SHA-256
`ff6883c7d0d2925f5dac2c7f45e6d8a5667c621273e600c65d276bea979cc4ae`.
This is the Northrend route, not the deferred castle reproduction.

At frame 3302, capture is 3.286 ms, effects 2.068 ms, cleanup 0.474 ms,
and the reported CPU total is 6.453 ms. Frame 3175 totals 7.903 ms.
The remaining material CPU costs at frame 3302 include:

| Scope | Calls | Exclusive CPU ms |
| --- | ---: | ---: |
| Model original draw submission | 1047 | 0.8320 |
| Pose envelopes | 1225 | 0.6158 |
| Deferred normal flush | 33, replaying 95 packets | 0.5563 |
| Model capture setup | 951 | 0.2818 |
| Model snapshots | 913 | 0.2251 |
| Model bounds/pose outer work | 913 | 0.1846 |

These are nested profiler scopes; their inclusive times must not be added.
Original draw submission includes the game's own backend work. Lighting
constants cost about 0.37–0.48 ms in the sampled frames and include repeated
floating-point diagnostic output; the log does not split all that time into
formatting, light math and queue work. Model MRT still declines this route
with `depth-only-before-rgb`, so its potential savings are not realized here.

Frame 508 has a separate 44.372 ms first `visibilityAudit` scope. This audit
already uses asynchronous occlusion queries without a blocking readback.
Its first call creates diagnostic resources and submits the audit; the
available scope does not identify which operation caused the cold stall.
This revision does not claim to fix that event or the full 2–3 ms budget.

## Changes and controlled CPU results

Material envelope dispatch now carries one timer through fast lookup and
fallback. Previously a rejected fast lookup destroyed its timer and started
another one. The cold path checks the remaining cheap budget before changing
to build work; the ready fallback keeps its existing evaluation check.
The timer's status reference remains alive until timer destruction. Current
pose evaluation, exact owner/key checks, build limits, and the separate
batched shadow path remain intact.

Short deferred log lines format into a 2 KiB stack buffer once, then copy
into their owned string. Oversized lines retain the existing allocated
fallback. The ordinary logger similarly formats short lines into a stack
buffer and writes their exact length with `fwrite`, including embedded NULs.
Its `%s` fast path, lock, counters, flushing and long-line fallback remain.
Neither change truncates output or introduces a cache or render resource.

Each comparison includes frozen r92 and current code in one Windows x86
executable, with balanced before/after ordering. Four warmup blocks are
discarded; the remaining eight blocks per variant produce these medians:

| Measured scope per batch | r92 ms | r93 ms | Change |
| --- | ---: | ---: | ---: |
| 64 cold pose dispatches, build quota exhausted | 0.010152 | 0.007743 | −23.7% |
| 64 warm weighted pose dispatches | 0.022488 | 0.017310 | −23.0% |
| 64 warm single-bone dispatches | 0.029852 | 0.030028 | +0.6%, effectively unchanged |
| Format/allocate 48 deferred diagnostic lines | 0.194279 | 0.098031 | −49.5% |
| Log 48 equipment-light rows, including FILE writes/final flush | 0.255860 | 0.220184 | −13.9% |

Clock reads per 64 dispatches drop 256→192 cold and 320→192 weighted;
single-bone remains 128. Deferred format calls drop 96→48. All measured warm
pose batches complete the same 4,096 valid evaluations. The two logging
benchmarks use different rows and scopes: their savings are not additive
whole-frame estimates. No gameplay FPS improvement is inferred from these
component measurements.

A scalar stateblock cache removed backend queries but did not show a
consistent CPU improvement across native D3D9 and DXVK 2/3. A larger FILE
buffer was also neutral. Neither experiment is retained.

## Validation and reproduction

- O0/O2 dispatch checks compare exact bounds over changing poses, counted
  clocks and exhausted budgets. The existing material-envelope fixture
  checks 5,510 exact bounds plus interleaved unrelated work and all quota
  behavior. The general bounds suite passes 203 cases in O2 and ASan/UBSan.
- O0/O2 formatting checks cover empty, boundary and long lines, UTF-8,
  literal percent sequences, finite/nonfinite numbers, embedded NULs and
  text/binary FILE output. Deferred allocation/format failures, concurrent
  logger writers, I/O errors, nested/exceptional flush batches and timing
  accounting pass. The native fixtures pass ASan/UBSan as well.
- The renderer translation unit compiles to an isolated x86 object. The
  deferred-material GPU checks pass on native D3D9 (depth-only subset;
  DF24 is unsupported there), DXVK 2 and DXVK 3. Native-MRT integration
  checks compare exact RGB/normals, occlusion, HDR and failure paths on
  all three backends. These are correctness checks, not GPU speed claims.

Reproduce the CPU comparisons from the repository root, with the required
client fixtures available and Zig 0.15.2, outside the Windows restricted
sandbox:

```powershell
python tests/profile_envelope_dispatch.py --reference d24ec9ab --output out/r93-envelope
python tests/profile_deferred_log.py --reference d24ec9ab --output out/r93-deferred
python tests/profile_logger_format.py --reference d24ec9ab --output out/r93-logger
```

Run performance comparisons serially without overlapping compiler/test work.
Receipts and raw samples for this run are under
`out/optimization-20261110-r93/`; GPU material receipts are in
`.local/out/r93-material-{0,1,2}`. The new profile scripts are manual tests
because they require an explicit frozen reference and output directory.

No production DLL or installer was built or installed. The current r92
`renderer/frd9.dll` remains SHA-256
`6afa2af7fb0500e21f6c3c8849fb7728a158fd0ef91c78c067a6de56b9b43c59`;
the existing test.20261110.15 ZIP and all 191 shader binaries are unchanged.
No game was launched or stopped.
