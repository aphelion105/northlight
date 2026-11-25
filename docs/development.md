# Development

Run commands from the repository root. Start with [the architecture map](architecture.md) when changing runtime ownership or pass dependencies.

## In-game inspection

**Ctrl+Shift+F12** cycles through shadows, GI, fog volume, normals, SSAO, and
the regular scene. Each view requires Northlight effects to be enabled.

- **Normals** encodes world-space XYZ in RGB, including captured M2 normals.
  The sky is black.
- **SSAO** displays the filtered occlusion factor in grayscale. White means
  unoccluded. The view runs SSAO even when Contact AO is off or another method
  is selected.

**Ctrl+Shift+F4** captures up to four textured UI draws with distinct stage-zero
textures. Reports, geometry, and shader inputs go to a session under the
client's diagnostic directory.

## Layout

| Path | Contents |
|---|---|
| `src/<group>/` | DLL sources, grouped: `proxy` (the D3D9 proxy and device mirror), `core` (settings, memory, logging, profiling), `world`, `replay`, `shadows`, `gi`, `lights`, `sky`, `water`, `gamedata` (tables derived from the game's MPQs), `generated` (headers written by the generators and shader compilers; do not edit) |
| `shaders/` | `*.hlsl` and their `*-shader-build.json` manifests; `shaders/compiled/` holds the compiled `<Entry>.bin` and `.bin.asm` |
| `scripts/` | `build_renderer.py`, `build_environment.py`, `generate_*.py`, `run_tests.py`, `check_layout.py`, `pe_normalized_hash.py`; `scripts/shaders/` holds the shader compilers (`compile_*_shaders.py`, `compile_shaders.cpp`, `disassemble_*.cpp`) |
| `renderer/` | `windows-package/`, `mac-package/`, `linux-package/`, the player installer `northlight_install.py`, the pipeline and packaging tools (`world_*_builder.py`, `extract_*.py`, `migrate_mac_proxy.py`, `build_packages.py`, `package-pins.json`, ...) and the built `frd9.dll` |
| `tests/` | test runners (`test_*.py`), native tests (`test_*.cpp/.h`), release validators (`validate_*.py`, `verify_*.py`) |
| `tests/support/`, `tests/fixtures/` | d3d9.h/windows.h shims for native builds; the archived baselines (old sources of this project, digests) that tests compare against. Real client data (shaders, doodad placements) is read from your client by `tests/client_fixtures.py`, never stored |
| `northlight_paths.py` | the only place that knows where the client, toolchain and outputs are |
| `client-config/` | factory profile `.ini` files for packages and settings reset; personal client profiles may differ |
| `*.py` (top level) | the art layer (MPQ patch) builder `build_art_layer.py` and its steps, `mpq.py`, `client_archives.py`, `renderer_status.py` (install/uninstall on macOS) |
| `out/`, `.local/out/` | ignored build and test output. `northlight_paths.py` resolves the configured output directory |

`tools/` and `backups/` are local and untracked.

The test runner streams each test's output to one log and removes the test's
private OS temporary directory on exit. Write evidence that should outlive
the test to `fp.output_dir()`. The
[Northrend harness](northrend-baseline.md) uses a reusable client stage and
removes per-attempt caches and replay scratch by default.

For the local Forever extraction, missing import fields and preserved research
artifacts, start with [the Forever data audit](forever-data-audit.md).
For the installed build, material/shadow optimizations and old `.local` copies,
see [the integration audit](integration-audit-20261113.md).
For each installed runtime file and its delivery/generation path, see
[the release payload audit](release-payload-audit.md).
For the Northrend lighting switches, streaming gaps and GPU regressions, see
[the lighting investigation](lighting-streaming-20261114.md).
For the latest gameplay profile and image-checked shader experiments, see
[the performance investigation](performance-20261114.md).
For the subsequent CPU source-reuse change and local-session baseline, see
[material admission snapshot reuse](cpu-source-reuse-20261114.md).
For character-shadow CPU costs and removed runtime profiling experiments, see
[the shadow profiling audit](shadow-profile-cleanup-20261114.md).
For the crypt captures, authored-lighting GI blend, lamp/fog fixes and normal
snapshot handoff, see [the lighting and shadow review](lighting-shadow-review-20261114.md).
For precipitation inside the crypt and the world-space shelter field, see
[the weather roof regression](weather-roof-20261114.md).
For the later weather-toggle profile, exact roof-cell reuse and unshipped GPU
experiment, see [weather cost and shelter reuse](weather-cost-20261114.md).
The subsequent native-precipitation path and independent sky/godray controls are
described in [celestial controls and weather ownership](celestial-controls-20261114.md).
For the subsequent gameplay profile and eliminated empty HDR-sky work, see
[empty HDR sky work](empty-hdr-sky-20261114.md).
For the next capture profile and rigid-range/grass packet optimizations, see
[material CPU capture](material-cpu-20261114.md).
For the next whole-pipeline profile, warm-cache evidence and deferred state-block
comparison, see [cache and deferred work](cache-deferred-20261115.md).
For the complete offline shader inventory, unused packaged resources and the
measured GI optimization, see [the shader audit](shader-audit-20261115.md).
For the supplied town/ziggurat and Zangarmarsh logs, queue stalls and sparse
buffer caching, see [the QA performance review](qa-performance-20261115.md).
For the runtime cache inventory, remaining churn and mixed-size material upload
reuse, see [the cache inventory](cache-inventory-20261115.md).
For the subsequent focused shadow profile, terrain classification cache and
copied Forever shader evidence, see [the shadow pipeline review](shadow-pipeline-review-20261114.md).
For the supplied inn/tower captures, room lighting overrides, four-lamp blend
ownership and timing limits, see [the interior lighting review](interior-lighting-20261116.md).
For the subsequent snow/tavern captures, missing material admission, smooth
ground normals and rejected shadow geometry, see [materials and local shadows](materials-and-local-shadows-20261116.md).
For the latest effect/capture profile, RAM/GPU ownership and count-pressure
optimization, see [effects and capture cost](effects-and-capture-cost-20261117.md).
For range-based VB/IB invalidation, model/terrain capture and the controlled
capture comparison, see [range capture](range-capture-20261117.md).
For geometry-alias proofs, current-pose reuse and native-cache maintenance,
see [CPU capture stages](cpu-capture-stages-20261117.md).
For CPU/GPU ownership, GPU-owned height fields and retained CPU consumers,
see [CPU/GPU ownership](cpu-gpu-ownership-20261117.md).
For cold snapshot copies and shared declaration lookup,
see [pipeline buffers and lookup](pipeline-buffers-20261117.md).
For repeated failed-material work and exact negative-cache keys,
see [material refusal cache](cache-refusals-20261117.md).
For hardware GPU BVH/GI experiments, visible differences and device lifetime,
see [GPU BVH feasibility](gpu-bvh-prototype-20261117.md).
For the later disappearing effects and overdarkened interiors, see
[effect continuity and room fill](effect-continuity-and-room-fill-20261116.md).
For compact diagnostic sessions, lossless archive conversion and the current
buffer ownership audit, see [compact captures](compact-captures.md).
For the shared world/actor visibility filter and the remaining fine-cascade work,
see [the shared directional shadow review](shared-directional-shadows-20261114.md).
For the experimental common fine depth cascade and the complete old/new
CPU/GPU comparison, see [the common-cascade experiment](common-near-cascade-experiment-20261114.md).

## Requirements

For the rejected-probe, relight-cache and visibility-plane changes, see
[CPU GI optimization](cpu-gi-optimization-20261117.md).

- macOS on Apple Silicon is the primary development host. Windows can build the DLL with Zig
  and run Windows-native/GPU checks; other tests still need their declared host toolchains.
  Linux may work with zig, clang and Wine on `PATH`, but it is untested.
- Python 3.9 or newer; only the standard library is used.
- [zig 0.15.2](https://ziglang.org/download/) to build the DLL (it cross-compiles to 32-bit Windows).
- clang++ and c++ (Xcode command line tools) for the native tests.
- Optional:
  - a WoW 3.3.5a client (game data, world cache);
  - Microsoft's x64 `d3dcompiler_47.dll` 10.0.26100.9457, for the offline shader compilers
    (`scripts/shaders/compile_*_shaders.py`). Use `Redist\D3D\x64\d3dcompiler_47.dll` from the
    [Windows 11 SDK 10.0.26100.9457](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads)
    and copy it to `d3dcompiler_47-10.0.26100.9457-x64/` under `NORTHLIGHT_TOOLS` (default `tools/`).
    Alternatively, extract `fil3585cb2ea5db13cc0838f8d06b5c9679` from the
    [Microsoft SDK cabinet](https://download.microsoft.com/download/46742ab5-6592-4968-a793-129e7f3bc55a/KIT_BUNDLE_WINDOWSSDK_MEDIACREATION/Installers/61d57a7a82309cd161a854a6f4619e52.cab)
    and rename it to `d3dcompiler_47.dll`. The tools verify its SHA-256. macOS shader builds also require Wine.
    `--compatible-compiler` can use the previously accepted SDK 10.0.17134.12 DLL when the new DLL is absent.
  - StormLib, for reading MPQs;
  - the external archive and `backups/`, for differential tests against old versions.

## Build

```sh
python3 scripts/build_renderer.py           # -> renderer/frd9.dll
python3 scripts/pe_normalized_hash.py renderer/frd9.dll
```

The DLL is delivered with `renderer/northlight-assets.bin`. Install both: the
resource file contains losslessly compressed shader bytecode, weather textures
with their existing mip levels, and the overlay font. The package builder refuses
a missing or mismatched pair. Developer/profile/stand variants write a separate
`<dll-stem>.assets.bin`, so they cannot replace the production resource file;
copy that file as `northlight-assets.bin` into the target game's root (the stand's
module folder). macOS keeps the resources in the game root even though its DLL
is installed under `mods/`.

To deliver the converted Forever atmosphere as well, pass
`--forever-atmosphere <path/to/forever-atmosphere.bin>` to
`python renderer/build_packages.py --platform windows --version <version>`.
The builder requires NFA2 with sky colors, records its hash/counts in BUILD-INFO,
and includes it in the ordinary install/rollback transaction. `verify_packages.py`
checks its framing, metadata and payload digest. Raw Forever research stays local;
the converted `.bin` is an explicit release input, not an implicit workstation file.
Expansion atmosphere/emissive rules and celestial profiles remain in the renderer
and packaged generators/configuration. They are not part of the map-0/1 NFA2 file.

`scripts/build_renderer_assets.py` builds the bundle and lightweight array-access
headers under the configured output directory. Original generated headers remain
the byte-for-byte reference for offline compilation and tests. The build puts its
resource headers first on the include path. No shader source, bytecode, texture
resolution, mip level or rendering quality is changed by packing.

Zig 0.15.2 does not forward GNU `--gc-sections` to its COFF linker. The build records
the pinned driver's actual link command and repeats that inexpensive link with
`/OPT:REF /OPT:NOICF`. This removes unused code while retaining the separate PDB
and distinct function addresses. Compilation remains `-O2`; global `-Os` is not
used without matched performance evidence. Link diagnostics and the map are saved
under the configured output root. The driver runs in a temporary directory there
with the caller's cache paths resolved before changing directories, so its printed
COFF inputs are absolute even when the caches live inside the checkout.

The build first runs the layout check (nothing back at a pre-move path, unique basenames). On
Windows it also compiles a minimal C++ file before starting the renderer build. Zig 0.15.2
can report `error: Unexpected` inside a restricted execution sandbox, even with fresh local
caches. Run Windows builds and native/GPU tests outside that sandbox through the normal
approval mechanism; a failed compiler check saves its diagnostics to `<out>/build-preflight.log`.

The build regenerates the forwarding headers in `src/generated/`. It checks every `shaders/*.hlsl` against its
`*-shader-build.json` and refuses to build if a shader changed without a recompile. The raw
DLL bytes depend on the build directory, because the PDB paths feed the build ID. Compare builds
with `pe_normalized_hash.py`, which zeroes only those fields.

After editing a shader, recompile it (Microsoft's compiler under Wine, natively on Windows; nothing
touches the game):

```sh
python3 scripts/shaders/compile_world_shaders.py   # or compile_shaders, compile_local_light_shaders, ...
```

After changing a shared `world_effects_*.hlsl` module, run all three affected
compilers. Their manifests record the complete local include tree.

```sh
python3 scripts/shaders/compile_world_shaders.py
python3 scripts/shaders/compile_shadow_transition_shaders.py
python3 scripts/shaders/compile_fog_volume_shaders.py
```

A compiler writes `shaders/compiled/<Entry>.bin` and `.bin.asm`, `src/generated/*_compiled_shaders.h`
and the `shaders/*-shader-build.json` manifest. Every shader uses the one toolchain in
`scripts/shaders/shader_compiler.py` (IEEE strict, O3; Wine's builtin compiler is refused).
`build_environment.py` keeps the Wine prefix outside the client folder.

## Tideglass

The optional Tideglass 0.1.0 core uses the cooperative D3D9 API. The export
under `tideglass/` comes from the upstream combo release and is checked against
its SHA-256 manifest before build or packaging. Build the 32-bit core with
CMake, Ninja and the i686 LLVM-MinGW tools on PATH. The combo command also
builds the regular Northlight renderer with the pinned Zig toolchain.

```powershell
python scripts/build_combo.py --storm-source <clean-pinned-StormLib-checkout>
python renderer/build_packages.py --platform windows --version 0.3.218-tideglass-0.1.0 --water-core <out>/tideglass/water-core.dll
python tests/verify_packages.py <out>/packages/Northlight-0.3.218-tideglass-0.1.0-Windows.zip
```

Omit `--storm-source` to let CMake fetch the pinned revision. The installer
preserves both water INIs and generates the reflection archive list from the
client's archive chain. With the art layer enabled, the combo package uses
daylight lighting underwater. Water settings are under **Water** in the
in-game overlay. Changing **Use Tideglass** requires a restart.

Tideglass runs with SDR and HDR output, including multisampled targets.
The HDR path uses the legacy scene before Northlight decodes it into linear
colour. MSAA scene snapshots resolve to single-sample textures while liquid
shading writes to the original target. During rain, the depth-scrubbed rain
mask retains streaks over the composited water. Northlight uses native water
when the DLL is disabled, missing or incompatible. The vendored core carries
local changes for multisample snapshots and state restoration.

## Test

```sh
python3 scripts/affected_tests.py --run          # only the tests your change affects
python3 scripts/run_tests.py test_replay_*       # by name pattern
python3 scripts/run_tests.py                     # full test inventory; inspect skips and requirements
```

Reports go to `<NORTHLIGHT_OUT>/test-output/<run>/<test>/`. Each run gets a unique directory.
`--record <version>` instead keeps that same structure under `renderer/records/validation-<version>/<run>/` (ignored by git; no test reads from it). The test
tags (`requires=`, `slow`, `timing`, `manual`, `known-fail=`), the skip rules, `tests/discovery.txt`
and the rules for writing a test are in [tests/README.md](../tests/README.md). With no client,
toolchain or archive you can still build the DLL (given zig) and run most tests, which are pure
Python or native C++.

### Offline GI transport benchmark

On Windows with a hardware D3D11 adapter, compare the CPU GI solver and
experimental GPU transport variants on the same cached Northrend geometry.

```powershell
python scripts/benchmark_gi.py --output <new-output-folder>
```

The harness runs the frozen CPU reference and the current solver in ABBA order
for surface and grid probe layouts. It checks exact CPU output and equality
across GPU variants, then records timings and CPU/GPU parity per layout.
A parity rejection is retained in the report. These component results do not
establish compatibility with the production renderer or in-game FPS.

## Machine configuration

Nothing in the code names a machine path. Each setting comes from, in order, an environment
variable, then `northlight.local.ini` (copy [northlight.local.ini.example](../northlight.local.ini.example);
it is ignored by git), then a default. `python3 northlight_paths.py` prints what resolves.

| Variable | Default | Used for |
|---|---|---|
| `NORTHLIGHT_CLIENT` | the repository's parent folder, if it holds `Wow.exe` and `Data/` | client data for tests, validators and pipeline scripts, and the client `renderer_status.py` installs into |
| `NORTHLIGHT_ZIG` | `tools/zig-*/zig`, then `zig` on `PATH` (must be 0.15.2) | DLL build, generators, shader tools |
| `NORTHLIGHT_WINE` | WoWSilicon's bundled Wine, then `wine` on `PATH` | shader compilers only |
| `NORTHLIGHT_STORMLIB` | `tools/storm-build/storm.framework/storm` | `mpq.py` |
| `NORTHLIGHT_TOOLS` | `tools/` | third-party downloads (DXVK, StormLib, zig, d3dcompiler_47) |
| `NORTHLIGHT_ARCHIVE` | none | old rollbacks and runtime diagnostics (`<archive>/renderer/`) |
| `NORTHLIGHT_BACKUPS` | `backups/` | old renderer tarballs |
| `NORTHLIGHT_OUT` | `out/` | build and test output |
| `NORTHLIGHT_TEST_OUTPUT_DIR` | set by `run_tests.py` | where a test writes its report |
| `NORTHLIGHT_SHADER_CORPUS` | none | optional captured shader corpus (extra sub-cases) |
| `NORTHLIGHT_DOWNLOADS` | none (then `tools/`) | pinned runtime downloads for `renderer/build_packages.py` (`renderer/package-pins.json`) |
| `NORTHLIGHT_LIVE_CLIENT`, `NORTHLIGHT_STOCK_CLIENT`, `NORTHLIGHT_LIGHTS_STAGE`, `BASE` | none | a few specific tests; see their docstrings (`NORTHLIGHT_STOCK_CLIENT`: a stock 3.3.5a client for the identity and variant tests) |

## Conventions

- **Basenames are the include namespace.** C++ includes use bare names (`#include "world_gi.h"`),
  each `src/<group>/` is one `-I` (`northlight_paths.include_flags()`), and Python finds sources the
  same way, with `northlight_paths.src('world_gi.h')` (sources and shaders) or `tracked()` (also
  scripts and tests). Directories only group files: moving a file between groups needs no edit.
  Every basename must be unique; `scripts/check_layout.py` enforces this.
- A test starts with the tag line and these two lines, then uses `fp.src(...)`,
  `fp.test_include_flags()`, `fp.output_dir()` and so on:
  ```python
  import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
  import northlight_paths as fp
  ```
- Tests never write next to the sources. They write to `fp.output_dir()`.
- `.gitattributes` has `* -text`: hash gates compare raw bytes, so keep LF line endings.
- The pre-commit hook rejects files over 5 MB and game or binary payloads, then checks
  layout, discovery and suite definitions without starting tools. Enable it in each
  clone with `git config core.hooksPath githooks`. The hook tries `python3`, then
  `python`; set `NORTHLIGHT_PYTHON` to select an explicit interpreter.
- `git clean -x` also deletes the ignored local folders (`tools/`, `backups/`, `out/`).

## Validation harness

For a private Windows 3.3.5a client run, fixed Northrend baseline and original
D3D9 trace import, see [Northrend baseline and replay](northrend-baseline.md).

### Windows package live gate

Run the named gate on a trusted Windows GPU runner with the private Northrend
stage and a ZIP built from the current tree. Set both environment variables to
absolute paths and choose a fresh output directory under the stage.

```powershell
$env:NORTHLIGHT_RELEASE_PACKAGE = '<Windows ZIP>'
$env:NORTHLIGHT_LIVE_STAGE = '<private stage>'
python scripts/run_tests.py --suite release-live --strict --jobs 1 --output "$env:NORTHLIGHT_LIVE_STAGE\release-candidate"
```

Set `NORTHLIGHT_RELEASE_BACKGROUND=1` for a release check on an occupied desktop. It skips foreground requests and requires a complete background Present interval for each World case. The expected package payload, camera, crowd, output mode and native capability checks still apply.

The gate runs `verify_packages.py`, then checks both the character menu and
a 128-player Dalaran scene on packaged DXVK 3, packaged DXVK 2 and
system D3D9. The DXVK cases check the active HDR target. System D3D9 checks
the primary SDR backbuffer. Each check uses the staged resolution and MSAA.
An x86 probe uses the renderer's capability checks with the default system
D3D9 adapter. If a required feature is missing, the receipt records the probe
and marks both native cases `UNSUPPORTED` without opening WoW for those cases.
The two packaged DXVK backends still have to complete all their cases.
World cases turn on diagnostic logging in the packaged quality profile. The
default mode requests focus for the private window. Both modes require a
complete warm interval and published equipment and particle lights. The runner
restores the private stage payload after each case.
The receipt records the archive, actual runtime payload hashes, resolved backend,
and identity of the renderer that reached each checkpoint. Use the separate
Northrend replay workflow for image parity and performance comparisons.

Use `scripts/northrend_live_ab.py --stage <private-stage> --output <run-dir>
--package <Windows-ZIP> --feature gamescene` for one-feature live comparisons
on the exact package. The four arms alternate in both orders and reuse the
private client. Pass `--background` to use the same window mode in all four
arms without foreground requests. The receipt includes Present percentiles,
repeat spread, sampled GPU passes, submission counts and query waits.
Live DXVK uses the regular EVENT wait by default. Use
`NORTHLIGHT_EVENT_ADAPTIVE=1` to measure the short-spin mode explicitly.
The live Dalaran comparison favored the regular wait at p95 and p99.

`run_northrend_baseline.py run --return-to-menu --windows 0` extends the
World readiness smoke. The loopback fixture logs completed logout. The
runner also requires a fresh character enumeration and a new rendered
character-menu context before it accepts the transition.

On selected `--light-history-audit` frames, the shader map can read the
actual water resolve, RainScrub, sky background, HDR sky decode and late
weather bindings. The journals carry shader bytecode, render target and
texture descriptions, filters, source MSAA where applicable, and explicit
skip reasons. A resolved output can use a different sample count from its
input. Component GPU tests verify these contracts with controlled inputs.

The existing runner owns selection, prerequisites, logs and the result. Suites are
listed explicitly in [tests/suites.json](../tests/suites.json):

| Suite | Contract | Prerequisites |
|---|---|---|
| source | Layout, harness failure controls, capture wiring, generated shader consistency | Python and Git |
| architecture | Compiled frame-demand, draw-order and capture-lifetime contracts | Zig; host support declared by each test |
| gpu | Hidden-device image and state oracles | Windows GPU, Zig and the configured client |
| packaging | Installer and packaging-tool regressions | Python; StormLib for client/archive entry points |

```sh
python scripts/run_tests.py --suite source --strict
python scripts/run_tests.py --suite architecture --strict --jobs 1
python scripts/run_tests.py --suite gpu --list
python scripts/run_tests.py --suite gpu --strict --jobs 1
python scripts/run_tests.py --suite packaging --strict
```

On Windows, compilation and native/GPU execution run outside the restricted
execution sandbox through the normal approval mechanism, as described in
[AGENTS.md](../AGENTS.md). `--list` never probes or starts a compiler/device.

Each run writes `results.json`, `results.tsv`, separate test outputs and UTF-8 logs.
JSON binds the exact selection and commands to HEAD, the dirty-tree status and
SHA-256 hashes of tracked/new non-ignored inputs. It distinguishes PASS, FAIL,
INCOMPLETE and STALE. Source or HEAD changes during a run invalidate it. The
input identity is a test-tree fingerprint, distinct from the DLL build identity.
Inputs are compared before and after execution; use a checkout that is not being
edited for acceptance runs.

Before relying on saved evidence, check it against the current tree:

```sh
python scripts/run_tests.py --verify-report out/verification/my-run/results.json
```

This also rejects a previously passing report after later edits, including edits
made by another task in the same working tree.

`--strict` succeeds only when every selected test passes. Missing prerequisites,
manual skips and known failures cannot satisfy a strict gate. Without it, the
legacy optional-prerequisite behavior remains available, but skipped coverage is
reported INCOMPLETE. Targeted runs still check the complete discovery inventory.
Every supplied pattern must match; a typo cannot silently narrow coverage.

Use `--output out/verification/<new-name>` to choose a run directory. Existing
directories are refused, including empty ones, so previous evidence cannot be
overwritten. `--update-discovery` updates the test inventory and exits without
running tests. Add a new test to the appropriate named suite when it protects
that suite's contract.

GPU and timing tests run serially after other tests. Independent external
runners or a running game can still contaminate performance measurements; use
an otherwise idle measurement environment for comparisons.

A source/architecture PASS is evidence for those contracts. GPU checks establish
their documented image/state comparisons. Release acceptance still requires
`scripts/build_renderer.py` and `tests/verify_packages.py` on the actual packages.
A live 2–3 ms budget requires matched game runs, including capture work, CPU/GPU
intervals, cold rotation, crowds, waits and unchanged image/quality settings.

