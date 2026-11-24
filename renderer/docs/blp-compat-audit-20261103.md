# Installed texture pack audit, 2026-11-03

StormLib opened patches A, B, C, D, E, G, I, P, and S under `D:/Games/335_engb_mod/Data` in read-only mode. The MPQ table sizes and hashes matched before and after the scan. The packages, client executable, and installed DLL kept their original bytes.

## Archive results

Of 124,045 BLP headers, 124,041 use BLP2. There are 75,631 entries with a nonzero offset or size past the mip chain implied by the dimensions. In the preliminary scan, 72,311 of these entries passed the range and minimum-size checks for ordinary levels. Another 3,446 had other inconsistencies. Archive entries may share names, so these totals count entries across archives.

| Patch | BLP2 | Extra mip fields | No other issues found | Other inconsistencies |
|---|---:|---:|---:|---:|
| patch-A.mpq | 28,756 | 15,842 | 15,766 | 76 |
| patch-B.mpq | 5,956 | 4,950 | 4,913 | 37 |
| patch-C.mpq | 5,876 | 2,160 | 2,148 | 128 |
| patch-D.mpq | 12,729 | 8,999 | 8,600 | 403 |
| patch-E.mpq | 7,416 | 315 | 285 | 33 |
| patch-G.mpq | 29,966 | 12,185 | 9,467 | 2,718 |
| patch-I.mpq | 32,377 | 30,984 | 30,948 | 38 |
| patch-P.mpq | 965 | 196 | 184 | 13 |
| patch-S.mpq | 0 | 0 | 0 | 0 |

An empty space with a non-zero offset in the tail is different from a real extra payload. Legacy fast path below focuses on size, so one calculation of 16 non-zero offsets does not prove hanging. For ringime, the flag mipmaps and the special interpretation of cube spread 6*height are also important. The primary mass audit uses the geometric limit of the normal 2D texture; the native test below reproduces the rules of client function.

## Orda ship

The model `world/wmo/northrend/buildings/winterorc/nd_hordegunship.wmo` and all 30 textures are read according to the actual priority of archives. In the set here, the extra fields have **5 of 30** and the continuous prefix of non-zero sises is longer than the ringime chain of **4 of 30**. The number 28/30 of external messages on these inputs has not been reproduced.

| № | Texture | Active archive | Size | Expected levels | Non-zero sizes in a row |
|---:|---|---|---|---:|---:|
| 0 | `DUNGEONS\TEXTURES\FLOOR\BM_BRSPIRE_RAMPMETAL01.BLP` | patch-B.mpq | 1024x1024 | 11 | 12 |
| 7 | `WORLD\GENERIC\HUMAN\PASSIVE DOODADS\OILRIGSTACKS\MM_OIL_METAL_01.BLP` | patch-D.mpq | 1024x1024 | 11 | 12 |
| 12 | `WORLD\AZEROTH\STRANGLETHORN\BUILDINGS\TROLLWATCHTOWER\TROLLROPE01.BLP` | patch-D.mpq | 128x256 | 9 | 9 |
| 17 | `DUNGEONS\TEXTURES\WINTERORCS\JLO_WORC_GRATE.BLP` | patch-B.mpq | 512x512 | 10 | 11 |
| 25 | `DUNGEONS\TEXTURES\AZJOL\JLO_AZJNUB_ENVMAP.BLP` | patch-B.mpq | 1024x1024 | 11 | 12 |

## Checking client code

A copy of 32- broken `Wow.exe`, SHA-256 `99155d4d6ed4937a5ab1f5972d11e8605b986561b9f08686ab66af2c582194ec` has been taken down. This verification is a specific exe; the findings are not automatically transferred to `WowClient64.exe` or other assemblies.

Dissemble observations (VA):

- `0x006AE900`: Copys the BLP2 title/palitre to an internal object; takes the number of mip levels through `0x006AB700` of size and flag, rather than counting all non-zero releases.
- `0x006AB700`: Final dimensional cycle up to 1; separately recognizes the width development == 6*height.
- `0x006AB760`: Places a mip-pointer table before a leveled pixel vault based on the number of levels by size.
- `0x006B00FF..0x006B012C`: Fast path direct data presentation goes down to zero and records the indicators without checking the index for the calculated number of levels.
- `0x006B00AC..0x006B00E7`: The other decoded path has a clear boundary on the calculated number of levels.

The boundary difference is the real basis for hardening. It does not in itself prove a way out of the entire selected heap block: near the table there's an equalization and storage of pixels. The endless cycle and cause of the heavy fall of the FPS remain unconfirmed.

## Native test and prototype memory correction

From the exe copy, 350 bytes of function `0x006AFFD0..0x006B012D`; SHA-256 fragment `f74ce8ec7778092011d97da5cd9b8e3a38347cd7aead0fbfaf6da05dbe318409`. The fragment is made in a separate x86-wall on a branch of direct mip points, without downloading/starting the game. hashs exe and fragments were checked before execution. The play code was not added to the DLL.

All 30 textures have checked the original version and the flat tail offset/size version only in an internal copy of the title. O0 and O2 with the underbart are gone. Four textures have a reference function that recorded by one index more than the runtime number of levels; no such records have been recorded since the tail was untipped. All active mip-level bit-bits are the same, BLP source bytes remain unchanged. The original function has also been completed on these tests, and there is no oblivion.

This is the first test of a specific fast path and samples not complete game run or FPS validation. The protection then implemented is described below.

## Option through d3d9.dll unchanged EXE and MPQ

The standard D3D9 API does not expose the original BLP header. `IDirect3DDevice9::CreateTexture` receives the dimensions, mip level count, and format. Northlight passes these to the backend and wraps the resource in `src/proxy/renderer.cpp`. Changing `Levels` there cannot fix the client traversal of its BLP pointer table. That traversal runs on the CPU. [Microsoft CreateTexture](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-createtexture).

The implementation in `src/proxy/blp_mip_guard.h` runs from `backend()` in `src/proxy/renderer.cpp`, before the graphics runtime loads and outside DllMain. It replaces **47 bytes** of the existing fast path once with a bounded loop. The loop stops at the first zero size or after `min(client mip count, 16)` entries. The BLP header and pixels retain their original bytes. All branches stay inside the client function, so the patch holds no pointers into Northlight code.

Before writing, the guard verifies the fixed-base x86 PE32 image and the SHA-256 hashes of three blocks: the direct-view function (350 bytes), the header parser (144 bytes), and the mip-count helper (96 bytes). It returns `already-active` when the identical loop is installed. A different signature, layout, or third-party hook leaves the code untouched. The installer saves the original client instructions in memory for comparison and rollback.

The plant uses a preliminary flow census and a non-interator of the current process. The remaining flows are temporarily stopped; their PCs are checked for entry into the replacement area. If he is busy, the stream is unavailable, the census has changed or the limit of 1024 flows has been exhausted, the attempt is stopped. As long as the flows are stopped, there are no heaps, logs, DLL downloads or user locks. The entire saved piece is recompensed before the recording. The error in publishing/refurbishing the page's protection results in a rebound of accurate source bytes before reflows. No change in the suspension counters that owned the code.

In `northlight-renderer.ini`, section `[Renderer]`, `BlpMipGuard=0` shuts down the installation at the next customer start-up; the INI change does not cancel an already completed installation in the current process. Diagnostics: `BLP mip guard status=active` / `already-active` or a specific cause of failure. For an imported-replay assembly, the client code entry is off. No additional MPQ files and no changes Wow.exe are available on the disk.

### Implementation checks

- `tests/test_blp_mip_guard.py` Registered in `tests/discovery.txt`; its normal launch does not require client files or GPU. O0/O2, under way, `-Wall -Wextra -Werror`.
- 374 Combinations of ringtime-account 0..20/UINT32_MAX and length of the precursor sizes 0..16, including a fully completed table. PAGE_NOACESSS is placed behind the last authorized entry. The immutability of Payload and undisturbed output cells are checked.
- The errors VirtualProtect/FlushInstructionCache shall be injected into the bench; byte backset and RX recovery shall be checked. The individual flow within the replacement cycle results in `decoder-busy`; the source code is retained. The suspended code stream and parallel heap activity on six streams are checked.
- When there is a clear `--client` display, the stand reads only a local exe copy and copies **three verified fragments** into the reserved storage room of its own PE test. Client entrypoint/imports/ Initiation of the game is not implemented. The address provides its own BSS section of the test; ASLR is disabled only in a compiled test exe. API Windows is not replaced.
- On 30 these BLP vessels, the record of the extra indicator has been removed from four. The displacements of all active mip points coincide with the original function, the BLP base bytes remain unchanged. Only 47 expected bytes in the reserved area of the bench differ after installation.
- 171 Combination of dimensions/flag mipmaps: NPOT, conventional chains, absence of mipmaps and cube strip 6*head. The original function of calculating the number of levels is also being performed. The acceptable and unacceptable firstLevel/LOD. shall be checked.
- Production `frd9.dll` is tested in the same process by a normal `Direct3DCreate9`: O0/O2 stand, DXVK 2.7.1 and 3.1.1 three API/equipment cycles. The installation is indeed performed before API returns. The four MIP levels of A8R8G8B8 pass byte-exact coundrip; clean-up of render target and GPU-readback give exactly expected pixels. A subsystem Party can retain production proxie after FreeLibrary; this is recorded as `proxyResident=1` not issued for actual unloading.
- `tests/blp_mip_guard_module.cpp` Separately collect the same installer into the minimum DLL: confirmed full unloading (`moduleResident=0`) and subsequent execution of the corrected customer code on all 30 samples.

Native D3D9 on RTX 4090 reported INTZ=1, RESZ=0, floatRT=1, SM3=1 and was rejected by the existing Northlight capability test (this is a dialogue on the sent screenhot). This GPU option does not count as PASS. The tests were successfully completed on both DXVK with RESZ=1.

Production DLL **0.3.217** collected by the regular `python scripts/build_renderer.py`, passed `build_packages.check_dll`; size 9,093,120 byte, SHA-256 `fe5a7cfa11e3a492176520ccc7dc012cfe618902b970f85d32a31067a8b310fa`, source identity `547595df8094837b3c879a9bff2df1a0790ecb8cff6f0db867ea8ff7681ee5bc`. The game was not installed. Five related registered checks were passed; three original requested host-tool tests were missed due to the absence of clang++/lldb/objdump and are not counted as PASS.

This is the protection of a specific BLP cycle. A full game run, an early enough time for all possible loading routes and the influence on the FPS has not yet been confirmed. It does not correct arbitrary damage to pixels/models and does not reduce the amount of HD Assets. MPQ-overlay and file correction Wow.exe are not implemented.

## Stony AIO and Project Reforged Preset

Checked the current [Project Reforged download page](https://projectreforged.github.io/wotlk/downloads/) downloaded from its reference `patcher_selection.ini` and definition of Stony AIO patches in three branches:

| Branch | Audited committee | Original_TABLE band recordings |
|---|---|---:|
| st0ny-main | `4b79c6443a377f2250533dcefb2c40e6f244f1cb` | 327 |
| st0ny-dev | `6105fc8e05d717a436d715c5b2b95980af165f88` | 444 |
| st0ny-dev-y38fix | `cfa4db64f41f9bfde9bdb86148bb78999b3102d2` | 315 |

The reviewed definitions do not find a separate tailing mip field fix or limit the research BLP cycle. The variable range tables do not cross functions `0x6AE900`, `0x6AB700` and `0x6AFFD0..0x6B012D`. This is a test of pathograph source, not evidence of all external DLLs that it can load.

The preset `wmocube` replaces the missing WMO processing in `CMap::SafeOpen` on terror cube; it does not correct BLP. `laa` increases the available address space, `timer` changes the choice of time source; this is another task. In dev, the reference `CreateBlpTexture() failed read` refers to the recording of errors as part of the ASset loading. Paragraphs `awesome`, `lexara`, `lux`, `luxvelum` are downloaders of individual DLLs rather than built-in BLP corrections. Their specific ringtime versions have not been checked by this audit.

Primary sources: [main apply_patches.ps1](https://github.com/Raz0r1337/St0nys-AIO-WoW-EXE-Patcher/blob/4b79c6443a377f2250533dcefb2c40e6f244f1cb/apply_patches.ps1#L2954), [main Reforged preset](https://github.com/Raz0r1337/St0nys-AIO-WoW-EXE-Patcher/blob/4b79c6443a377f2250533dcefb2c40e6f244f1cb/apply_patches.ps1#L4491), [dev source](https://github.com/Raz0r1337/St0nys-AIO-WoW-EXE-Patcher/blob/6105fc8e05d717a436d715c5b2b95980af165f88/apply_patches.ps1), [download preset](https://pub-0f05631d243e4046993fc02ca7be9542.r2.dev/Wrath%20of%20the%20Lich%20King/mods/presets/patcher_selection.ini).

SHA-256 download preset: `d2f7d2e0b7e454b4b141532c7d45f7fe20f1ccaba7f70e8bc3102fc6e10baf72`. Patcher has not been launched. The installed Wow.exe still has SHA-256 `99155d4d6ed4937a5ab1f5972d11e8605b986561b9f08686ab66af2c582194ec`; the previously studied cycle is not limited. Details are stored in `.local/out/blp-compat-20261103/st0ny-review.json` and `reforged-patcher_selection.ini`.

## What the current installer will do

Only read-only `gather_inputs -> step_digests -> plan` of `scripts/install_world_cache.py`. The plan sees nine new archives and appoints `mode=full`: reassembly the geometry Azeroth/Kalimdor/Expansion01/Northrend and lights/fog/celestial. Current cache was collected before adding these archives.

A complete installation will bring Northlight's static data into line with new models/textures, but the current Installer code **does not normalize BLP in MPQ reference**. Its scene beilder creates its own FGS materials; the original downloader WoW continues to read the original BLP. Such a run is not a verified correction of the problem under consideration.

Automatic compatibility can be done in a separate step of the installation: auditing, normalizing only verified cases, checking the continuity of existing levels, then transactional recovery. The override-archive option retains the original packages, but conventional MPQ files will require copying the full Payload of the corrected BLP; it's not a small patch from the same headlines. Integrating into the existing art layer also requires that account be taken of the archival priority and the other person's patch-z. Direct rewriting A.S. can't be given as a normal re-assembly of cache.

## Play and Data

All the ways below are relative to the repository:

- `.local/out/blp-compat-20261103/audit_patches.py` and `patch-audit.json`: mass read-only audit and hash MPQ tablets.
- `.local/out/blp-compat-20261103/find_gunship.py`, `extract_ship.py`, `horde-gunship.json`: actual ship's materials/servicers; local samples in `samples/`.
- `.local/out/blp-compat-20261103/client-recon.json`, `Wow-texture.dis.txt`, `Wow-blp-decode.dis.txt`: PE and disassembly; for runtime-outputs only the record Wow.exe shall be used.
- `.local/out/blp-compat-20261103/run_native.py`, `native_blp_direct.cpp`, `native-direct-results.json`, `native-direct-O0.log`, `native-direct-O2.log`: native test.
- `.local/out/blp-compat-20261103/installer_plan.py`, `installer-plan.json`: update plan without installation.
- `tests/test_blp_mip_guard.py`, `tests/blp_mip_guard.cpp`, `tests/blp_mip_guard_module.cpp`: Registered native and non-binding production/GPU/unload Oracle, normal launch: `python tests/test_blp_mip_guard.py`.
- `.local/out/blp-compat-20261103/guard-proxy-dxvk2/validation.json` and `guard-proxy-dxvk3/validation.json`: Final O0/O2 tests with actual textures and production DLL, without WoW.
- `.local/out/blp-compat-20261103/blp-guard-validation.json`: summary of DLL, hash, tests and limitations; `release/d3d9.dll` - complete copy of production `renderer/frd9.dll`.
