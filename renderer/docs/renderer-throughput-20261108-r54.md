# Renderer throughput r54 — shared vertex programs

## Latest game run

Analyzed the r53/DXVK 3.1.1 run frozen under `out/perf-20261108-r54/game.log`, SHA-256 `b930080635174fddc904f09d9ec1c788b31f6d9d6cfc6096044b69dc14d5cfee`.

The final foreground windows average 30.659 and 26.491 ms (32.62 and 37.75 FPS). The camera/workload differs from the preceding run; this is not a controlled r52/r53 speed comparison.

- Capture CPU is 5.552 / 5.384 ms at frames 1524 / 1651. Effects CPU is 3.848 / 4.785 ms. Native draw wrappers, snapshot/setup and pose envelopes remain material costs.
- ModelNormals submits 541 / 539 commands from 856 / 865 logical draws in those samples. GPU intervals are 2.193 / 2.951 ms; GameScene intervals are 13.581 / 13.175 ms. Timestamp intervals include submission gaps and are not GPU utilization measurements.
- Geometry is resident, with zero original-source reads, streaming or UP fallback in these warm samples. The scene is charged 58.5–68.0 MB against its 96 MiB aggregate native allowance. Raising that limit is not indicated by these samples.
- r52 PS reuse remains active: two created receiver pixel programs and seven shared source entries.
- Material-map attempts still often produce no capture: frame 1651 has 774 normal-build-stage attempts and 42 deferred material captures. `AUTONORMAL` occupancy was not emitted during the short gameplay portion, so these counters alone do not prove a particular map-budget refusal or cache size. The material-map cache is separate from geometry residency.
- Active EVENT frame 1440 totals 34.2668 ms: 25.0719 before replay and 9.1835 in replay. Early menu/startup waits reach 486.6708 ms total. This iteration does not claim to eliminate them or attribute wait intervals to exclusive GPU execution.

## Change

The compact normal receiver VS retained a native FOG0 output even though this private pass already disables fog. Its declaration and dependent arithmetic are now removed by the existing liveness slicer. The supported stock corpus loses four executable bytecode instructions per compact program. Native scene shaders, weather/fog consumers and shadow VS slicing retain their existing behavior.

Equivalent compact programs also differed in input/output/temporary register names. Cold normalization now assigns deterministic names by declaration semantics and first temporary use. It preserves every surviving operation, modifier, mask, constant binding, DEF/DEFI/DEFB literal, relative palette address and address-register operand. The complete depth interpolator declaration remains intact for DXVK 2 linkage. Unsupported programs retain the existing path.

Existing VS source maps reuse exact final shader objects after hash/size prefiltering and a byte-for-byte `GetFunction` check. Each original source keeps its own Vertex metadata and owned shader COM reference. Range classification, native-source identity and current-pose handoff therefore remain tied to the actual source. This differs from aliasing those metadata records.

Existing adjacent merging, redundant-draw keys and binding deltas now recognize shared shader identity. Captured ranges, constants, declarations, raster state and sampled inputs still must match. PS and VS reuse share the same exact-bytecode helper. No new cache container, persistent bytecode copy, backend policy, resource limit or warm bytecode scan is introduced. `vertexShaders` and `sharedVertexShaders` are cumulative diagnostics for the next run.

## Evidence and measurements

Among 486 distinct stock VS3 source programs from nine diffuse/specular families, the same 270 supported programs produce 124 unique r53 compact programs and 20 unique r54 programs. All 270 lose exactly four arithmetic instructions. This is a corpus result, not the actual game draw-frequency mix; a driver may already eliminate some unused output work.

The GPU fixture renames input/output/temp registers in eight native VS variants. Each keeps its own native metadata. Compared with frozen r53, receiver program creations fall from eight to one and 16 adjacent submissions become one. Native VS restoration and captured pixels are exact.

| Backend | r53 capture/replay CPU median | r54 |
| --- | ---: | ---: |
| DXVK 1.10.3 | 0.02815 ms | 0.02520 ms |
| DXVK 2.7.1 | 0.03905 ms | 0.03490 ms |
| DXVK 3.1.1 | 0.03455 ms | 0.02880 ms |

These are medians across four native VS cases, each using nine warmed component samples. Reference/current run in separate processes in that order. Absolute differences are small; they do not establish whole-frame FPS, pure GPU busy-time, GPU utilization or Mac parity.

## Validation

- O2 and ASan/UBSan: bijective register names, duplicate/missing declarations, unchanged relative constant usage, depth RGBA linkage, disabled receiver fog versus preserved shadow fog, idempotence, allocation failure and 20,000 malformed canonical-VS mutations. Existing PS and shadow controls also pass.
- 24 current program/backend cases (SM2/SM3 × rigid/single-bone/weighted/four-bone × DXVK 1/2/3), plus 12 frozen-r53 SM3 reference cases. Exact pixels, alpha cutout, MSAA, compact creation failure/full fallback, high-bone constants, stale-depth rejection, native state restoration, batching and coplanar first-depth/last-writer ordering pass.
- All 12 r53/r54 sampled camera/pose lighting CSV pairs are byte-identical. These are sampled lighting observations rather than complete frame comparisons.
- Six current streaming cases across the three backends, each with three allocation/query failure modes, retain exact pixels.

Receipts, the local-only client corpus, commands, source hashes and measurements are under `out/perf-20261108-r54/`. AO, resolution, material precision, deformation rules and lighting/shadow/weather coverage are unchanged. No installer, installed-client modification, game control, subagent or repository publication is part of this iteration. Actual shared VS counts, submitted draws and frame times must be checked in the next game run.

Production DLL: `out/dll-20261108-r54/d3d9.dll`, 10629120 bytes; SHA-256 `8b22b8433f96406dc0eab38fda0e42b75f6059cd71ac0b9e849faf670900f037`; source identity `aea7bf459ce07d0aebabb3afc2c7f910ff71893546a59be34e13c8190a5cde3a`, built 2026-11-08 21:28:35 UTC. Compiler preflight, shader freshness, PE32/export validation, all 779 final production source hashes and 192 real EVENT completions through the final production command-stream proxy pass. The installed DLL and latest game log remain unchanged.
