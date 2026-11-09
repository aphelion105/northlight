# Renderer throughput r52 — shared receiver programs

## Evidence from the latest game run

Analyzed the r51/DXVK 3.1.1 log frozen under `out/perf-20261108-r52/game.log`. SHA-256: `541785e71d94ae975c3c4dca5fc727a8c3a5fc36c6e22819acdf74851823967b`.

The enabled foreground windows average 30.9–31.4 ms (about 32 FPS). Later windows with effects inactive are around 15 ms. These sequential feature toggles are not a controlled A/B benchmark and cannot establish individual effect costs.

| Observed stage | Evidence |
| --- | --- |
| Capture CPU | 5.925–6.945 ms in full-effect samples; native draw wrapper about 1.0–1.1 ms, pose envelope 0.94–1.03 ms, snapshot/setup another ~1 ms |
| ModelNormals GPU span | About 1.8–2.7 ms in representative active samples; prior early flushes can also occur inside GameScene |
| Effects GPU span | 2.4–5.8 ms in representative samples; shadow filtering, removal smoothing and composition remain material costs |
| EVENT | Startup total 206.5343 ms: 190.8713 before replay and 15.6546 in replay. Active totals include 43.0897 ms (34.6203 before replay) and 41.3437 ms. No queue fence calls. These are wait/queue spans, not GPU busy time |
| World entry | Frame 382: 2433.830 ms, effects unapplied, 1992.546 ms frame queue idle. Asset/producer gaps around texture upload/creation are visible. This is not attributed to normal replay or geometry preparation |

The native GPU scene is approximately 54–73 MB against a 96 MiB aggregate allowance in the sampled warmed frames. Geometry becomes fully resident, sourceReadBytes stays zero and UP fallback is zero. This run has no memory-guard trims or pressure transitions. Increasing residency limits would not remove this warmed workload. The r51 nonadjacent duplicate filter reports zero matches here, while existing adjacent merging removes roughly 258–360 draws per sample.

## Change

Different native RGB programs can produce the same normal/depth receiver calculation. Previously each original PS owned a separate receiver object and metadata node, so PS identity prevented command merging even when the generated calculation was identical.

Compact receiver programs now get deterministic input, temporary and literal register names. Only the existing capture-control external binding can be remapped. Used literal values, source modifiers, instructions, write masks and interpolation declarations remain exact. Unused literal/sampler declarations disappear. The full depth interpolator declaration remains intact for DXVK 2 linkage. Flow, matrix operations, relative references, extra external constant bindings and other unsupported forms keep the existing program.

The existing source maps share stable receiver nodes after exact final-bytecode, control-binding and compact-VS-contract comparison. A hash only shortlists candidates; `GetFunction` supplies exact bytes for cold collision checks. Aliases retain their original source lifetime, while the owning node retains the receiver shader. Maps survive target reset and are released together at renderer destruction. No new cache container or persistent bytecode copy is added. Warm source lookup does not re-read or scan bytecode.

This lets existing state reuse and adjacent merging work across equivalent materials on the same path for DXVK 1/2/3. New cumulative `pixelShaders` and `sharedPixelShaders` log fields allow the next game run to show actual reuse. Neither weather nor the other consumers are disconnected from scene capture.

## Measurements and validation

The local diagnostic/prepared corpus has 22 distinct source programs and 38 successfully generated source/mode outputs, reducing to 20 exact transformed program keys. Its 15 compact opaque diagnostic sources reduce to one identical program. This establishes bytecode redundancy, not its frequency in the latest game scene.

The GPU fixture changes native RGB writes, unused input declarations, input register allocation, control-register placement and unused literal bits across eight real-client-derived native variants. The reference disables both canonicalization and sharing. With the change, eight receiver objects become one and 16 adjacent submissions become one, with bit-exact output and exact native PS restoration. Target/resource reset preserves sharing.

| Backend | Reference capture + replay CPU median | Current | Difference |
| --- | ---: | ---: | ---: |
| DXVK 1.10.3 | 0.03220 ms | 0.02740 ms | -14.9% |
| DXVK 2.7.1 | 0.03125 ms | 0.03060 ms | -2.1% |
| DXVK 3.1.1 | 0.02990 ms | 0.02730 ms | -8.7% |

These are medians across eight stock VS cases, each with nine alternating paired component samples. Absolute differences are small. They are not gameplay FPS or pure GPU duration measurements; driver shader caching may already deduplicate its own internal work.

Passed:

- O2 and ASan/UBSan CPU controls: bijective register renaming, literal NaN/signed-zero bits, idempotence, depth declaration lanes, unsupported external/matrix programs, allocation failures and 20,000 malformed canonical-program mutations; existing VS/shadow liveness controls also pass.
- 24 complete normal capture cases: SM2/SM3 × rigid/single-bone/weighted/four-bone × DXVK 1/2/3. Exact pixels, native alpha/cutout, full fallback on creation failure, high bone constants, MRT/MSAA, stale-depth rejection, state restoration, batching and coplanar first-depth/last-writer ordering pass.
- Forced identity collision, differing control binding and differing compact-VS contract remain separate.
- Six streaming cases across all three backends (SM2 four-bone and SM3 single-bone), each with three allocation/query failure modes, retain exact pixels.

Receipts and source hashes are under `out/perf-20261108-r52/`. No installer, installation, game control, subagent, repository publication or sleep action is part of this iteration. The next game run must establish actual merged submissions, shared receiver count and frame-time changes. Remaining long waits are not claimed eliminated. AO, effect strength, resolution, geometry and lighting/shadow coverage are unchanged.

Production DLL: `out/dll-20261108-r52/d3d9.dll`, 10618880 bytes; SHA-256 `523345eb31847dd21513155e5a3b6b7190c538dc2e5068c79a5c207906e0c1d7`; source identity `445ab7e4a13a804fea393063987772633eb2c5f20a47042cb70e508ff64b9b7b`, built 2026-11-08 20:08:39 UTC. Compiler preflight, shader freshness, PE32/export validation, all 779 final production source hashes and 192 real EVENT completions through the final production command-stream proxy pass. The installed DLL and latest game log remain unchanged.
