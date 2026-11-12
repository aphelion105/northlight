# r59: repeated material metadata misses and mip accounting

No new game run is present. The client still has r57 (`f1b40f920654387a3100ed79b11d30a36f7c93fa2dc9accfae7bc0ca44382e70`), and `northlight-renderer.log` remains 680,540 bytes, SHA-256 `435e1213014ffdac3b5d7fdcf8a0c1abc5baaf10e867f920c4b7e069cb6a3ab6`, modified November 9 at 01:44:41 local. The r58 analysis remains the source for game costs; these changes have not been measured in gameplay.

## Why this area

At frame 1524 the r57 run records 947 normal-cache requests, including 857 misses, with approximately 0.0881 ms in NormalCache and 0.3785 ms in NormalBuild. These are requests, not unique textures or generated maps. The material CPU-pixel table contains only 16 records at its frame-1200 snapshot, with no evictions. Increasing its 128-record capacity is not supported by this evidence.

## Shared changes

`NorthlightMaterialTextures::Cache` remembers one absent raw identity. Repeated version/read checks for the same untracked texture no longer scan all 128 records. Inserting any identity clears this hint immediately. The existing positive recent hint is also updated on insertion. A removed or invalidated key cannot make a previously absent key exist; owner changes, pool reset, revisions and pixel availability keep their existing behavior. This stores one pointer and retains no extra texture references or pixel data. It does not cache failed normal-map admission or asynchronous disk-pending status.

`NorthlightAutoNormalMemory::mipBytes` now evaluates square power-of-two chains up to 16384 with an exact geometric sum, including explicit level counts and DXT compression. The final 2x2 and 1x1 compressed levels each retain a complete block. Rectangular, non-power-of-two and larger dimensions use the existing loop. Format estimates, budget thresholds, LRU behavior, resource limits and generated maps are unchanged.

Both changes use the common CPU path for native D3D9 and DXVK 1/2/3. There are no shader, AO, shadow, image-quality or EVENT waiting-policy changes in this iteration.

## Component measurements

Native x86 release builds compare frozen r58 code with the current code. Alternating paired blocks reduce ordering bias; values are medians of paired blocks, not frame measurements.

| Metadata population | Consecutive misses for one key | r58 ns/query | r59 ns/query |
|---:|---:|---:|---:|
| 16 | 1 | 11.5010 | 11.8230 |
| 16 | 8 | 11.5608 | 2.4852 |
| 16 | 1024 | 11.3737 | 1.1900 |

Empty and full tables show the same pattern. Alternating unknown keys has a small extra hint-check cost; repeated misses benefit. The actual run does not record consecutive-key batch sizes, so the benchmark is not an estimate of game savings.

| Mip input group | r58 ns/call | r59 ns/call |
|---|---:|---:|
| Square, explicit full chain | 14.6989 | 5.0444 |
| Square, three levels | 5.5976 | 5.0770 |
| Square, implicit full chain | 7.1707 | 5.0491 |
| Rectangular fallback | 13.5609 | 14.4135 |

The gains are small in absolute CPU time. They do not explain or resolve the previously observed multi-millisecond EVENT tail. The existing r58 categorized cache-deferral and HDR fallback diagnostics still need a new game run before choosing a more invasive admission or rendering change.

## Validation

Evidence is in `out/perf-20261109-r59`.

- The metadata lookup oracle compares exact pixels, hashes, versions, lifetimes, revisions, counters, table size and bytes against linear lookup through 48,000 randomized probes, owner replacement, copies, partial uploads, reset, clear, invalidation and bounded eviction. It runs at O2 and O0. Its generated linear reference was independently checked to match the frozen r58 header exactly after reversing namespace renaming.
- A negative control deliberately keeps the absent-key hint after insertion. The test rejects it after moving the positive recent hint away, proving that insertion invalidation is exercised.
- Mip accounting matches an independent iterative oracle in 15,552 cases, including 27 formats, explicit partial/excess level counts, zero dimensions, compressed block rounding, non-square dimensions and the 16384 fast-path boundary.
- Existing normal-cache linear/index state and ownership tests pass at O2/O0 with ordinary and deliberately colliding hashes.
- Existing pixel-reduction scalar/SSE2/debug tests pass with exact RGBA, alpha, padding, revisions, failure and allocation handling. Particle texture tests also pass.
- ASan/UBSan pass for the new lookup oracle, atlas uploads/copies and actual command-stream material/particle texture tests. The stream tests cover committed upload order, future-frame pixel isolation, reset and ownership behavior.

Production inputs are frozen in `final-source-hashes.json`; only the two headers above differ from the validated r58 production inputs. Separate sanitizer and command-stream receipts record upload ordering checks and any final artifact validation.

## Delivery

Production build uses `python scripts/build_renderer.py`, pinned Zig 0.15.2 and existing shader-freshness/export/package gates. The final DLL passes isolated real-EVENT smoke tests with 64 generations on each of DXVK 1.10.3, 2.7.1 and 3.1.1. These smoke tests verify the production proxy and query payloads, not game FPS or disappearance of the game EVENT tail.

- DLL: `out/dll-20261109-r59/d3d9.dll`, 10,661,888 bytes.
- SHA-256: `b00bdec81db2d2ff166d483ba46d18b1bac81603e6db54d3f890a4ba015a3fba`.
- Source identity: `bb693a494a6a8b38b090ddaef8c49d2a8f3c6612af03a46de6cee5f97c96b57b`.
- Build time: November 9, 2026, 00:55:37 UTC.
- Final receipt: `out/dll-20261109-r59/final-validation.json`.

The installed client DLL, game log and client configuration remain untouched. No installer was built.
