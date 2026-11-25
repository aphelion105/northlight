# Porting Northlight to WowClient64.exe

Date of study: 13 November 2026 year.

**Conclusion: **D3D9-proxi architecture is suitable for a researched x64 client.
It causes the same `Direct3DCreate9` and normal methods `IDirect3DDevice9`.
Northlight's main source is successfully compiled for x64, but complete.
Support requires a shift in access to internal WoW data, client patches,
Additional DXVK x 64 DLL is not sufficient.

The static review confirms that there are appropriate routes in EXE.
Ready x64 DLL wasn't lynching, running a client with Northlight and compatibility
The images were not checked. This is a transferability study, not a report.
Release readiness or performance.

## Researched client

| Property | Meaning |
|---|---|
| File | `WowClient64.exe` from the customer's local installation |
| Size | 13 948 416 Byte |
| SHA-256 | `eb9e7cc49e740e2bd1b370f247348b21483238acc0f5a88131fc267fe3b02586` |
| PE Machine | `0x8664`, AMD64 |
| Optional Header Magic | `0x20b`, PE32+ |
| Preference base | `0x140000000` |
| ASLR | Included `DYNAMIC_BASE` and `HIGH_ENTROPY_VA` |

Comparative local `wow.exe` has Machine `0x14c`, PE32 and base
`0x400000` His SHA-256:
`99155d4d6ed4937a5ab1f5972d11e8605b986561b9f08686ab66af2c582194ec`.

The x64 conclusions refer to the specified EXE Hesh.
Check. All of the following RVA are offsets from the module base;
The real address at ASLR is the current database of loaded EXE plus RVA.

## Confirmed loading D3D9

The names of the two downloaders are compared with the branches.
`CGxDeviceD3d::ILoadD3dLib` and `CGxDeviceD3d9Ex::ILoadD3dLib`.

| Branch | RVA Started Function | VA with preferred base |
|---|---|---|
| D3d | `0x11e720` | `0x14011e720` |
| D3d9Ex | `0x109320` | `0x140109320` |

The two functions shall have the same sequence:

```cpp
LoadLibraryA("d3d9.dll");
GetProcAddress(module, "Direct3DCreate9");
Direct3DCreate9(32); // SDKVersion = 0x20
```

In the D3d branch, the call `LoadLibraryA` is located on RVA `0x11e759`
`GetProcAddress` - `0x11e7af`, call the factory received `0x11e7bf`.
In the D3d9Ex branch, the corresponding RVA: `0x109359`, `0x1093af`, `0x1093bf`.
D3d9Ex is not called `Direct3DCreate9Ex`: in these two
It is `Direct3DCreate9` that is requested for the unsolved downloads.

Northlight is already turning the result of this factory into `Factory`:
[renderer.cpp](../src/proxy/renderer.cpp), function `Direct3DCreate9`.
Existing exports `Direct3DCreate9Ex` separately transmit the challenge without effects;
Its support is not derived from the usual D3D9-proxie.

## Confirmed calls to the device

The x64 client has the following calls from the D3D9 table of methods.
The slot numbers are checked against `IDirect3DDevice9` in the local heading `d3d9.h`
Toolchain. Bytes are increasing with the size of the indicator; order
The methods remain the same.

| Method | Slot | Offset x86 | Offset x64 | RVA call in x64 EXE |
|---|---:|---|---|---|
| `Present` | 17 | `0x44` | `0x88` | `0x122ecd` |
| `EndScene` | 42 | `0xa8` | `0x150` | `0x11f2dc` |
| `DrawPrimitive` | 81 | `0x144` | `0x288` | `0x11c42a` |
| `DrawIndexedPrimitive` | 82 | `0x148` | `0x290` | `0x11c408` |
| `SetVertexShaderConstantF` | 94 | `0x178` | `0x2f0` | `0x120599` |
| `SetPixelShaderConstantF` | 109 | `0x1b4` | `0x368` | `0x1205e1` |

The API convergence preserves the main intercept and dispatch architecture
It doesn't prove the old pattern of drawing, Shader bytecode,
The use of registers, matrices or banks of light.
Captures x86 and x64 with effects.

## Compilation test result

Zig 0.15.2 successfully compiled `src/proxy/renderer.cpp` with parameters:

```text
c++ -target x86_64-windows-gnu -c -O0 -std=c++17 -fno-rtti
-Wno-microsoft-exception-spec
-DIMGUI_USER_CONFIG="northlight_imconfig.h"
```

Ways of inclusion received through `northlight_paths.include_flags()`. Verification
was performed outside the restricted Windows sandbox according to `AGENTS.md`.
The compiler's completion code is `0`; the result is COFF Bigobj version 2
Machine `0x8664`, size 153 852 306 byte.

This confirms the compileability of one main broadcast unit from her
Other `.cpp`,
export `.def`, full lincing, releasing-optimization and performance of DLL
The test is not covered. The diagnostic obproject file is not a production DLL
and does not replace a full-time `scripts/build_renderer.py` with freshness checks on the sheaders.

## Necessary changes

### Collections and exports

[build_renderer.py](../scripts/build_renderer.py) sets `x86-windows-gnu`
both for compiler preflight and for production DLL.
with separate output files, while retaining the fixed version of Zig and existing
Checking the sheaders.

[frd9.def](../src/proxy/frd9.def) contains x86-alias species
`Direct3DCreate9=Direct3DCreate9@4` A table is required for x64
The rules for the decoration of conventional C-exports x64 are described.
in [Microsoft Learn](https://learn.microsoft.com/en-us/cpp/build/reference/decorated-names?view=msvc-170).

### Access to internal WoW data

This layer is the main volume of adaptation. Current addresses, dimensions
The indicators, field offsets and signatures refer to the x86 client.

| Subsystem | Found Tier | Source |
|---|---|---|
| Map and camera | Base `0x400000`, x86, global variable `0xb7436c`, 4- byte indicators | [world_context.h](../src/world/world_context.h) |
| Camera and view stack | Indexes `uint32_t`, fixed vtable identifiers, chamber and graphic device offsets | [world_camera.h](../src/world/world_camera.h) |
| Global Light and Heaven | Block `0xd38b00`, additional global variables and x86-code checks | [wmo_context.h](../src/world/wmo_context.h), [celestial_context.h](../src/sky/celestial_context.h) |
| Weather | Index `World::s_weather` for `0xcd7544`, 32-bit range of indicators and old signatures | [weather_memory.h](../src/sky/weather_memory.h) |
| Scene and sources M2 | `ScenePointer=0xcd754c`, object addresses `uint32_t`, fixed structural displacements and code checks | [native_scene_lights.h](../src/lights/native_scene_lights.h) |

We need to compare the elements in x64 EXE, restore them layout and confirm them.
Recalculation of all addresses by the same delta or mechanical replacement
`uint32_t` on `uint64_t` not sufficient: the indicators and scalar fields differ
Subject to ASLR, addresses should be allowed relative to current status.
Checks of customer conformity shall be maintained and checked
that's the new layout.

Until this layer is adapted, the availability of a working D3D9-proxie does not provide
The context for full world lighting, atmosphere, GI and fixed lamps.

### Patches of client instructions

Separate audits require:

- [blp_mip_guard.h](../src/proxy/blp_mip_guard.h) test PE32/I386 and BLP patch.
- [native_light_stay.h](../src/proxy/native_light_stay.h) - change in branch to `QueryLights`.
- [native_more_lights.h](../src/proxy/native_more_lights.h) and [native_more_lights_code.h](../src/proxy/native_more_lights_code.h) - x86 instructions, addresses and positioning of additional lamps.
- [glue_lighting_selection.h](../src/proxy/glue_lighting_selection.h) is the interference with the character selection screen.
- [native_shadow_control_windows.h](../src/proxy/native_shadow_control_windows.h) is a fixed base and code checks prior to the control of obtrusive shadows.

Several implementations are clearly disabled outside x86 and return `requires-x86`.
It should first be checked whether the corrected problem remains in x64 clients;
The necessary patches will be implemented and tested under his instructions and ABI.

### PE and memory photos

[dxvk_engine_name.h](../src/proxy/dxvk_engine_name.h) now reads native-width
headers and import thunks for both PE32 and PE32+.

[snapshot_store.h](../src/stream/snapshot_store.h) Determine code range
with a base assumption `0x400000`. It should be adapted to the new
It's the image, keeping the code separated and the data altered.
[client_image.h](../src/stream/client_image.h) for imported images
Accepts only I386/PE32 fixed base; x64 stand support
Reproduction would require a separate extension of the contract.

### Installation and dependence

[build_packages.py](../renderer/build_packages.py), function `check_dll`
Now rejects the renderer DLL with Machine, different from `0x14c`.
Explicit architecture packages corresponding to DXVK and verification of actual
The joint installation of x86 and x64 in one folder has to be decided.
Separate: The normal download of both clients requests one name `d3d9.dll`.

Researched x64 EXE has imports `dbghelp.dll`, `DINPUT8.dll`,
`MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll` and `XvidDecoder.dll`.
In the local client folder `dbghelp.dll` and `dinput8.dll` have Machine `0x14c`
a `XvidDecoder.dll` - `0x8664`. In the preparation of x64 installation it is necessary
Check the import authorization and architecture of downloadable DLL; available
x86-modules cannot be considered compatible relationships.
The downloader was not reproduced in the study.

## Proposed transfer sequence

1. Add x64 collection and exports, collect a separate DLL and confirm chain
   Check the installation of the device,
   dispatch drow teams, `Present` and `Reset`.
2. Compare game chaders with D3D9 data; determine which existing
   The detection and capture techniques are appropriate for this assembly.
3. Move and check the map, camera and view stack, then sky, global
   Light, weather, scene M2 and light sources.
   Confirm their input data.
4. Check the need for customer corrections and carry over
   Patches, PE service processing and diagnostics.
5. Perform applicable source/architecture/GPU- checks, regular production
   Collection and testing of x64 packs.
   Change of map, loss/displacement of device and match of image.

According to available data, the compiler part and D3D9 entry look substantial.
It is easier to adapt the internal structures of WoW. The full transfer date has not yet been estimated:
The necessary amount of x64-layout recovery has not yet been established.

## Additional research materials

Key results, hashs and addresses are maintained above and not dependent on time
Detailed local records are available
`fp.output_dir()` for research `x64-client-investigation` in this
Installation - `.local/out/test-output/x64-client-investigation/`:

- `findings.json` is a summary of confirmed results.
- `pe-inventory.json` - PE headings, sections, imports and selected lines of the three EXEs.
- `d3d-loaders-x64.asm.txt` - complete review of the two D3D9 downloads.
- `gx-device-x64.asm.txt` - Distributed portions of the graphics device.
- `draw-calls-x64.asm.txt` call candidates for vtable offset; coincidence
  The displacement itself does not confirm the type of object.
  See the table above.
- `x64-compile-command.txt`, `x64-compile-probe.log`, `renderer-x64-probe.obj`
  - a team, a journal, and the result of a successful compilation.

Initial attempt `-fsyntax-only` completed `FileNotFound` and retained
separately in `x64-syntax-probe.log`; the positive result relates to
subsequent compilation with `-c -O0`Dumps, copies of EXE and obact file are available
in a ignored catalogue of results and are not the originals, or
published project components.
