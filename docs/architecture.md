# Architecture and ownership

The current renderer is an x86 D3D9 proxy in the client process. This map describes
the implemented boundaries. Historical investigations and release reports remain
in `renderer/docs/`; they do not replace the current contracts or prove current performance.

## Repository map

| Path | Responsibility |
|---|---|
| `src/proxy/` | D3D9 adapters, mirrored state, native draw admission and capture orchestration |
| `src/stream/` | Ordered command transport, replay thread and real completion/query semantics |
| `src/core/`, `src/settings/` | Frame requirements, budgets, profiling and settings policy |
| `src/replay/` | Immutable source snapshots, geometry identity, residency and lifetime |
| `src/world/` | World context, consumer planning, resource ownership and owned pass execution |
| `src/shadows/`, `src/lights/` | Directional/local shadow selection, maps, light identity and scheduling |
| `src/gi/` | CPU acceleration, probe work and immutable publication |
| `src/sky/`, `src/water/` | Sky, weather, fog and water producers/consumers |
| `src/overlay/` | Settings and monitoring UI |
| `src/generated/`, `src/gamedata/` | Generated headers and derived tables; retain their producer/freshness checks |
| `src/imgui/` | Vendored UI dependency |
| `shaders/` | Shader sources/manifests; bytecode and assembly in `compiled/` |
| `scripts/` | Development commands; shader tools in `shaders/`, tuning tools in `dev_tuning/` |
| `renderer/` | Installer, client-data pipeline, package payloads and release packaging |
| `tests/` | Python drivers and native fixtures; shared shims in `support/`, frozen inputs in `fixtures/` |
| `docs/` | Current architecture and development instructions |
| `.github/workflows/` | Automated source-contract checks |
| configured output root | Private build/test artifacts, separated by run; never source inputs |

Root Python modules are existing installer entry points/imports and
`northlight_paths.py`. New development commands belong in `scripts/`.
`check_layout.py` rejects stray root source files, including ignored copies.
Source basenames form the existing include namespace and must stay unique.

## Runtime contracts

1. **One native submit owner.** Four `Draw*` adapters preserve their D3D9 argument
   tuples. `drawHook` owns preparation, fault boundaries and the shared tail.
   `captureGameDraw` preserves terrain-before-material ordering and the current
   material → normal → world → water sequence. Raw and wrapped callbacks have
   different consumers; UP draws retain their separate memory/lifetime rules.
2. **Potential demand and available inputs are separate phases.**
   `NorthlightFrameRequirements::make` is the shared consumer plan. Capture can
   ask for potential materials before composition knows whether they exist.
   AO-only does not acquire model normals or lighting. Fog-only owns its R32F
   distance pair through `WorldDistance`, without model normals or lighting.
   Lighting frames share that pair with WorldNormals/TemporalLight. A producer
   change invalidates history before the pair rotates; fog blur always reads the
   current dry distance and validates water independently.
3. **Resources follow consumers.** `world_screen_resources.inl` owns screen
   groups; `world_geometry_resources.inl` owns geometry targets. Groups remain
   warm until reset, resize or pressure. Frame-local/transient sharing requires
   compatible formats and disjoint lifetimes, with one owner and safe fallback.
4. **Worker inputs are immutable.** CPU jobs consume held source generations and
   publish complete results. The D3D owner performs device calls. Reset, pressure,
   resource mutation and pointer reuse cannot retain stale poses or identities.
5. **Completion is real.** EVENT/query availability, source mutation barriers and
   resource leases are correctness contracts. Neither an optimization nor a
   fixture may treat unfinished work as completed.

## Evidence for a change

| Boundary changed | Required checks |
|---|---|
| Frame demand/resource ownership | `architecture` suite; isolated AO/fog/GI/shadow/material combinations |
| Draw/capture orchestration | Four-adapter oracle, draw gates, capture handoff and terrain bounds |
| Shader/pass/state behavior | `gpu` suite plus the affected shader-specific oracle and freshness gate |
| Installer/public Python paths | `packaging` suite and verification of the actual produced archives |
| Harness, layout or selection | `source` suite, including intentional failure controls |

Run the affected tests as well as the named contracts. Named suites are focused
gates, not the full test inventory. Commands and report semantics are in
[development.md](development.md#validation-harness).

A passing report belongs to its recorded source tree. Changed inputs or HEAD
invalidate it. An INCOMPLETE run does not establish missing coverage. Use
`--strict` for acceptance and keep the generated JSON with the reviewed change.

## Remaining architectural work

The common consumer plan and capture entry are implemented foundations. Complete
full capture-inclusive CPU/GPU budgeting, compact
hot/cold packet storage, further normal replay elimination and measured queue/wait
improvements remain separate work. `WorldRenderer` still contains much of the
pass executor; moving resources into files did not complete its decomposition.

The GPU controller includes deferred captures/effects, while in-scene capture
remains outside that interval. The CPU controller does not cover all distributed
draw-hook work. These controllers do not prove a total Northlight budget.
Component tests and worker timings cannot establish game FPS or a 2–3 ms total.
That claim needs matched live routes with identical quality, output and current poses.
