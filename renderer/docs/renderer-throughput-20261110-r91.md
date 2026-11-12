# r91: retain depth snapshots and native MRT across depth-preserving draws

Base: r90, `cb1ac709d8f1c599a73199c936369a044642f85a`.
Source identity: `94168ba29e6d0c70b99142e9e01b4aa1f7391dbcf59589a02697131b418d8205`.

Sources only. No production DLL or installer was built or installed. The renderer
translation unit was compiled to a separate object without linking; CPU/GPU test
executables were built separately. The existing test.20261110.15 DLL/ZIP and all
191 shader binaries remain unchanged. No game was launched or stopped.

## What the latest gameplay log actually measures

The closed log is still the r88 build, source
`f21012e9cec082fb71d9329ddc20037c2257d5bd9b0cef9eb1a8afa0093198a8`,
475,075 bytes, last written 2026-11-10 11:23:48 local time, SHA-256
`6852fd08a0bd4f40c4436625c0a04716612bfb6ad7e3cc625cbc13c46e365918`.
It does not measure the accumulated r89–r91 changes.

Frame 635 identifies these costs:

| Scope | Time / work |
| --- | --- |
| CPU capture | 4.977 ms |
| CPU effects | 3.142 ms |
| Material queue flush, CPU exclusive | 0.3541 ms; 19 flushes, 66 packets |
| Material queue flush, GPU interval | 0.3031 ms; 19 spans |
| Material depth preservation | 19 seeds, 17 barriers, 0 read-only skips |
| ModelNormals GPU interval | 1.161 ms; 450 queued, 281 submitted |
| Effects GPU interval | 3.628 ms |
| AO + blur GPU intervals | 0.821 + 0.256 ms |
| WorldLighting GPU interval | 0.032 ms |

These are nested or overlapping scopes, not numbers to add into a frame total.
The 0.995 ms material `nativeDraw` CPU scope includes the original game draws.
Large capture-setup GPU spans can include command starvation. Neither is a
measurement of removable extension work alone. Model capture and replay remain
larger targets than the WorldLighting shader in this sample.

The log does not record the depth function of each barrier. Therefore it cannot
establish how many of those 17 barriers this change will remove on that route.

## Changes

`AutoBump::nativeMayWriteDepth()` now also reads `ZFUNC` through the existing
batched state source. An intervening `EQUAL` draw can only store the existing
depth; a `NEVER` draw cannot pass. Both preserve the queued batch's private depth
snapshot. This avoids a flush, state save/restore, target switches and another
full-resolution depth copy for each such boundary. Unknown state still forces
the conservative flush. Other depth comparisons, explicit depth clears, target
changes and texture-write guards retain their existing barriers.

Native ModelNormals applies the same proof to its existing no-RGB-draw guard.
An interleaved depth/alpha-only draw using `EQUAL` or `NEVER` no longer marks the
depth as changed. Later receivers can keep their native MRT result and omit the
separate normal replay. A no-RGB draw that can change depth still forces the
existing whole-frame rollback before a later RGB receiver.

No new target, mask, cache, shader variant or effect pass is added. Normal and
occlusion calculations and their separate outputs are unchanged.

## Controlled 4K comparison

One executable, 3840×2160, 1x depth, 66 buffered four-bone model draws, an 8×8
tessellated plane. A native draw is inserted after every fourth capture: 16
boundaries, comparable in count to the log's 17 barriers. Its depth function is
explicitly `EQUAL` or `NEVER`; the gameplay log does not prove this distribution.

The old-policy control uses the existing test-only forced-barrier switch. For
these inserted draws it reproduces the previous flush policy. An immediate
capture control is also run. All variants share the executable and resources;
four repetitions alternate their order. Compilation and other test processes
do not overlap the measurements. Values below are medians in milliseconds.

| Backend / inserted draw | CPU submit before → after | GPU interval before → after |
| --- | --- | --- |
| DXVK 2 / EQUAL | 0.337725 → 0.169475 | 10.370432 → 8.055872 |
| DXVK 2 / NEVER | 0.343613 → 0.178988 | 8.260160 → 7.043072 |
| DXVK 3 / EQUAL | 0.328113 → 0.225588 | 9.721664 → 8.337664 |
| DXVK 3 / NEVER | 0.320407 → 0.218694 | 7.955968 → 7.050816 |

In every case: 66 packets retained, depth seeds **17 → 1**, barriers **16 → 0**.
Color, normal and occlusion readbacks are bit exact against both controls.
The interval includes all fixture native draws, repeated material overdraw,
capture and flush; it is not the standalone copy cost or a gameplay frame.
CPU submission and GPU elapsed time must not be added. These results do not
establish the requested 2–3 ms whole-extension gameplay budget.

## Validation and limits

- CPU raster/mirror suite passes, including 96 depth-enable/write/function
  combinations and conservative read failures in both state-source modes.
  Private-depth/state restoration passes at O0 and O2.
- Native MRT: **8,838 cases per backend** on native D3D9, DXVK 2 and DXVK 3;
  2,866 committed frames per backend. Exact native RGB and canonical normal
  readbacks, HDR sky, alpha coverage, material/local-light composition,
  before/after injected failures, target changes and whole-frame rollback pass.
  Four added cases cover interleaved no-RGB/alpha-only EQUAL/NEVER draws;
  EQUAL also passes over untouched background. Existing changing-depth
  interleaved cases must still roll back.
- DXVK 2/3 material suites: **41 checks each**, including ordered LESS/EQUAL/
  GREATER captures, depth clears, real depth-writing barriers, pose changes,
  source mutations, packet limits, seed failure and resource pressure.
  The 32-draw EQUAL/NEVER controls reduce 32 seeds to 1 with exact readbacks.
- Native D3D9 on this GPU reports DF24 unsupported. Its **12 focused checks**
  validate the immediate fallback and exact images, not active 1x batching or
  a performance gain. The full legacy material harness was attempted and
  stopped: it assumes DF24 and also constructs a multi-stream declaration
  rejected by native D3D9. The new `--depth-only` mode isolates the relevant
  depth/fallback checks. The full native MRT suite above remains independent.
- The benchmark runner now checks the actual executable's width, height and
  sample count, including when `--reuse-exe` is used.

Receipts are local under `out/optimization-20261110-r91/` and the configured
test-output directory's `r91-material-*` / `r91-depth-bench-*` folders.
`verified-results.json` records source/input/executable identities and confirms
the existing package and shader binaries were not changed.

## Reproduction

Prepare the stock model-material fixtures using `validate_model_materials.py`
and a stock human receiver fixture for the MRT runner, as in r90. Then run the
following outside the restricted Windows compiler sandbox:

```text
python tests/test_material_raster_batch.py
python tests/test_material_private_depth.py --output out/r91-private-depth
python tests/test_model_normal_native_gpu.py --fixture <stock-human-fixture> --backend <d3d9-backend> --output out/r91-native-mrt
python tests/validate_material_deferred.py --single-sample --backend <dxvk-backend> --quick --output-subdir r91-material-check
python tests/validate_material_deferred.py --single-sample --depth-only --backend <native-d3d9> --quick --output-subdir r91-native-depth
python tests/validate_material_deferred.py --single-sample --backend <dxvk-backend> --quick --benchmark --benchmark-depth-case 12 --capture-draws 66 --mesh-grid 8 --output-subdir r91-equal
python tests/validate_material_deferred.py --single-sample --backend <dxvk-backend> --quick --benchmark --benchmark-depth-case 13 --capture-draws 66 --mesh-grid 8 --output-subdir r91-never
```

Cases 12/13 insert a draw every fourth capture; cases 10/11 insert one between
every pair. Repeat on both DXVK backends. The benchmark requires DF24 support;
unsupported native D3D9 should use the focused correctness checks.
