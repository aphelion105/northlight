# Development

Run commands from the repository root. Start with [the architecture map](architecture.md) when changing runtime ownership or pass dependencies.

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
| `out/` | build and test output (ignored) |

`tools/` and `backups/` are local and untracked.

## Requirements

- macOS on Apple Silicon is the primary development host. Windows can build the DLL with Zig
  and run Windows-native/GPU checks; other tests still need their declared host toolchains.
  Linux may work with zig, clang and Wine on `PATH`, but it is untested.
- Python 3.9 or newer; only the standard library is used.
- [zig 0.15.2](https://ziglang.org/download/) to build the DLL (it cross-compiles to 32-bit Windows).
- clang++ and c++ (Xcode command line tools) for the native tests.
- Optional:
  - a WoW 3.3.5a client (game data, world cache);
  - Wine and Microsoft's redistributable `d3dcompiler_47.dll` 10.0.17134.12 (x64), for the shader
    compilers only (`scripts/shaders/compile_*_shaders.py`): put the DLL in
    `tools/d3dcompiler_47-10.0.17134.12-x64/` (it is not in git; `shader_compiler.py` pins its SHA-256);
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
under the configured output root.

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

A compiler writes `shaders/compiled/<Entry>.bin` and `.bin.asm`, `src/generated/*_compiled_shaders.h`
and the `shaders/*-shader-build.json` manifest. Every shader uses the one toolchain in
`scripts/shaders/shader_compiler.py` (IEEE strict, O3; Wine's builtin compiler is refused).
`build_environment.py` keeps the Wine prefix outside the client folder.

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

