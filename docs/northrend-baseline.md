# Northrend baseline and D3D9 replay

The Windows fixture runs the actual x86 WoW 3.3.5a client against a single Python process on `127.0.0.1`. The server performs login, lists one Human character and places it at `(5846.67, 604.13, 657.0)` on map 571 with fixed orientation and a 12:00 login time. The client uses its own MPQs. The fixture covers the empty square and a crowd of equipped player models. It needs only the local client assets and simulates the rendering workload. The login fixture implements the required packet layouts from [TrinityCore 3.3.5](https://github.com/TrinityCore/TrinityCore/tree/3.3.5/src/server/game/Server) and the [WoW SRP/ARC4 protocol reference](https://docs.rs/wow_srp/0.7.0/wow_srp/wrath_header/index.html).

## Stage and measure

Run from the repository root. Create one private stage and reuse it for subsequent runs. The stage links MPQs and the selected Northrend world-cache inputs to the installed files. On Windows without symbolic-link privileges it uses same-volume NTFS hard links. The executable, DLLs, logs, realm configuration and writable client state live in the private tree. The runner checks the linked input hashes before each live run. The live trace budget starts at 2 GiB and adds 1 GiB per crowd update packet. The frozen live and replay config carries the installed HDR settings, per-display profiles, `DxvkEngineNameWorkaround` and `DxvkPresentWaitWorkaround`. The fixture disables the Northlight settings window and WoW tutorials. Run GPU commands outside the restricted Windows sandbox as described in [AGENTS.md](../AGENTS.md).

```powershell
python scripts/run_northrend_baseline.py stage --backend <DXVK-x86-D3D9.dll> --output <stage> --resolution 3840x2160 --msaa 2
python scripts/run_northrend_baseline.py run --stage <stage> --output <stage>/baseline-a
python scripts/run_northrend_baseline.py run --stage <stage> --output <stage>/baseline-a-repeat
```

The runner starts its own loopback server and its private `Wow.exe`, submits the fixture login to the owned window, waits for the expected world camera, GI and static mesh readiness, collects three complete warm windows, then closes that process. It checks the **actual** HDR target dimensions and MSAA against the staged profile. A smaller desktop or an occupied window can require a smaller `--resolution`. `--target menu` is a device-startup smoke. `--manual` leaves character selection to the operator while retaining the same readiness checks. The fixture account and password are both `NORTHLIGHT`.

Timing uses focused windows by default. `--background` starts the client without requesting focus and selects intervals whose renderer reports `focus=0`. The receipt identifies this mode and the private profile's configured `maxfpsbk`. Compare background timings under the same desktop load and video configuration.

Set `$env:DXVK_FILTER_DEVICE_NAME = 'NVIDIA GeForce RTX 4090'` for RTX 4090
measurements. A filtered run requires DXVK's device-creation log to identify
one matching adapter and records its name and log hash. Replay of a filtered
capture inherits the filter and requires the same adapter.

Each run binds two OS-assigned loopback ports for login and world traffic. The
runner writes the login port to the private `Config.wtf` and locale realmlist,
then restores the staged realmlist when the run ends. The private client starts
with sound disabled, including when reusing an older stage. The run report
records both ports and the hashes of the launch configuration files.

Use `--windows 0` for a World smoke that validates scene readiness and output mode without requiring a foreground timing window. The report marks this run as `smoke` and excludes it from FPS evidence.

Compare the reported `mean_ms`, individual window p95, readiness intervals, actual camera and backend/config hashes. Check the private `Wow_d3d9.log` for the selected presentation mode. Record prerequisites and quality settings when comparing two stages. This scene has its own workload and FPS baseline. Run capture and replay separately from live timing and keep desktop load comparable across repeats.

New diagnostic builds retain the ordered QPC deltas and the Present ordinals
in each `FRAME interval` row. `frame_timing` combines full windows in the selected mode
after world readiness and reports p50, p95, p99, the five slowest Presents,
and the share over the 60 FPS and 30 FPS frame budgets. Older logs retain their
window aggregates and report raw timing as unavailable. The report verifies
the selected log prefix after closing the client, so a later Reset cannot move
the recorded warm-up boundary. These CPU arrival intervals include pacing and
waits. GPU pass times come from separate queries.

### Equipped crowd

Pass `--scene crowd` to `run` or `end-to-end` on the same private stage. The default is 128 remote players. Use `--crowd-size 50..128` to run a smaller roster. The fixture places players in staggered rows and sends up to 32 objects per update packet. It cycles eight armored appearances with luminous weapons and permanent enchants. Some carry a pair of weapons. Models, textures, idle animation and particles come from the installed MPQs, including Northlight's equipment light records. Use Quality with `EquipmentLights` and `CommandStream` enabled. The runner requires a client name request for every player and Northlight output from both native equipment lights and accepted particle draws. The report records the roster packet hash and peak light counts. Combat and player movement are outside this workload.

Pass `--scene rain` to request heavy rain from the loopback server while using the same Dalaran spawn. The runner only accepts this case after observing the weather packet, native precipitation draws and a populated rain mask. Use `--light-history-audit` in an end-to-end capture to record the actual RainScrub and late weather bindings on the selected Presents. [WoW 3.3.5 weather packet layout](https://github.com/TrinityCore/TrinityCore/blob/3.3.5/src/server/game/Server/Packets/MiscPackets.cpp)

`--scene shoreline` moves that weather fixture to the river edge in `Northrend_30_19`. The checkpoint additionally requires a water draw and a resident water mask target. The runner uses the shoreline spawn for both live and replay camera checks.

Use `run --camera-turn --windows 0` to require a heading change after cold World entry. The loopback fixture sends a same-position teleport with a fixed heading. The runner requires the client's acknowledgement and before/after camera observations near the spawn. `end-to-end --camera-turn` selects the parity frames after the same checkpoint. [WoW 3.3.5 movement packet layout](https://github.com/TrinityCore/TrinityCore/blob/3.3.5/src/server/game/Server/Packets/MovementPackets.cpp)

`--scene interior` starts near the viewpoint of the retained inn capture. Its checkpoint requires multiple ready point-shadow maps and work in two refresh intervals. With light-history audit enabled, the trace also preserves the source and cube bindings on the selected Presents.

`--scene terrain` reuses the heavy terrain QA spawn in the private client. Its checkpoint requires a terrain material capture sample with positive shader submissions and zero D3D errors. The fixture uses the current baseline's camera, video profile and fixed time. Keep timing comparisons within that configuration and use the grass/displacement and WMO receiver fixtures for their image contracts.

An existing `--frame-parity` stage also supports untraced timing runs, using the same frozen noon clock. Run both scenarios with the same stage and DLL to compare the crowd's cost.

```powershell
python scripts/run_northrend_baseline.py run --stage <parity-stage> --output <parity-stage>/solo-timing --scene solo
python scripts/run_northrend_baseline.py run --stage <parity-stage> --output <parity-stage>/crowd-timing --scene crowd
```

For a binary A/B, pass `--live-dll` and `--live-assets` to an untraced `run`. The runner verifies the loaded shader bundle, records both candidate hashes and restores the staged files after the client exits.

### Ratchet camera pan

`ratchet-pan` uses a separate Kalimdor stage. The test starts on the waterfront road inside Ratchet, with the player surrounded by 32 idle characters in eight appearances. Half use enchanted weapons. Northlight must report water draw coverage, equipment and particle lights, and active CommandStream replay with frame skipping disabled before measurement begins.

```powershell
python scripts/run_northrend_baseline.py stage --scene ratchet-pan --x64 --dll <Northlight-x64-d3d9.dll> --assets <northlight-assets.bin> --backend <DXVK-x64-D3D9.dll> --output <stage> --resolution 3840x2160 --msaa 1
python scripts/run_northrend_baseline.py run --scene ratchet-pan --windows 0 --dxvk-direct-adapter --stage <stage> --output <stage>/pan-a --timeout 120
```

The runner uses an inactive Windows desktop to keep focus fixed. The staged client add-on allows 20 seconds for warm-up after world entry. It then marks four stationary seconds and rotates the camera in opposite directions for 4.5 seconds each. Each sweep must cover at least 300 degrees in Northlight's camera log. The report includes the median, p95, maximum and slowest Present intervals for each phase and the three seconds after the camera stops. A sweep or the recovery period fails if its maximum exceeds twice the stationary median (bounded to 33.3–50 ms) or its p95 exceeds 1.35 times the stationary p95 (at least 33.3 ms). Keep the resolution, MSAA and DXVK adapter settings fixed across repeats. The report joins the slowest Presents to logged EVENT waits when available.

For a DXVK compiler A/B, add `--dxvk-compiler-threads 8` to the candidate run. Use `--dxvk-gpl-only` to test the background pipeline optimization separately. The runner records the settings confirmed by the DXVK log under `dxvk_pipeline_options`.

On x64 DXVK 3.1.1 with CommandStream, new native local-light shader variants get 50 ms for background compilation before first use. Draws use an eligible existing variant or the original shader during that interval. Set `NORTHLIGHT_DXVK_NATIVE_SHADER_GRACE=0` for the immediate-first-use control.

### M2 stress benchmark

`--scene m2-stress` places 512 equipped characters in 16 dense rows within the starting view. The runner uses an inactive Windows desktop and starts timing after it verifies the spawn hash, name queries for at least three-quarters of the roster, and two dense draw samples. On x86 each sample requires at least 6,000 audited M2 draws in the ready world. On x64 it requires at least 6,000 compact draws in a completed model-normal pass. The report records those draw counts alongside the background frame times. Use a stage with `Diagnostics=1` and use the same staged quality and video settings on both sides of an A/B. Allow time for the roster and complete 120-frame timing windows.

```powershell
python scripts/run_northrend_baseline.py run --stage <stage> --output <stage>/m2-stress-run --scene m2-stress --timeout 240
```

Use `--windows 0` to check scene readiness or `--dxvk-direct-adapter` when DXVK cannot enumerate a display on the inactive desktop.

For the native M2 diagnostic, run the same stage with `--windows 0 --native-m2-audit-samples 3` and Diagnostics enabled in its frozen profile. The report joins the game-thread decoder with accepted model-normal packets by producer frame. It requires a verified client, repeated ready-world samples with a versioned source, and matching audited palette rows. Loading samples and rejected draws remain visible in the report. The warm samples do not require foreground focus or provide FPS evidence. The imported replay lacks the original M2 call stack.

The report also includes a grouped M2 census when the additional client signatures match. Each sampled producer frame records shader and software draws, observed fixed-function alpha tests, triangle counts, verified instances and failures of the range or palette checks. `multiInstance` counts draws with at least two instances and a matching palette in dispatcher order. `world_ready` comes from the joined model-normal sample on the same producer frame. Add `--native-m2-require-grouped` on the 128-player crowd scene to require the same number of warm grouped samples. A grouped sample qualifies when every observed shader draw matches and at least one draw contains multiple instances.

Add `--native-world-batch-audit` to require warm samples of the WMO and ADT
producer audit. It checks the native caller and the owning record, then
compares the D3D9 range after accounting for Gx vertex and index pool offsets.
Each qualifying sample has verified ranges and finite, ordered group-local
WMO boxes. For ADT the producer reads the source chunks used to prepare the
batch and checks that the native bounds enclose the height extrema from
each 145-float MCVT payload, within float rounding.
The render thread checks the combined native bounds against the indexed
positions from the same draw. The gate requires every audited ADT draw to
have valid native bounds and a matching consumer check. `unattributed` counts tagged world shaders
whose caller was outside the supported WMO and ADT paths.

## Capture from launch

Get the official [apitrace 14.0 x86 bundle](https://github.com/apitrace/apitrace/releases/tag/14.0) for the parser and its runtime dependencies. Prepare a byte-exact source checkout for the private instrumentation build. The private loader points the external D3D9 wrapper at `northlight-capture.dll` by exact path. The distinct filename lets `Wow.exe` continue to resolve the outer `d3d9.dll` after querying startup video capabilities. After a successful texture creation, the private wrapper emits `NLTEX1` with the COM pointer returned to the game and the inner identity in the trace. The official parser and the existing D3D9 call serialization are used for the full trace.

```powershell
git -c core.autocrlf=false clone --branch 14.0 --recurse-submodules https://github.com/apitrace/apitrace.git <apitrace-source>
python scripts/build_native_retrace_host.py --upstream <apitrace-source> --cmake <cmake.exe> --ninja <ninja.exe> --output <tool-build>
python scripts/build_renderer.py --stand-replay
python scripts/run_northrend_baseline.py run --stage <stage> --output <stage>/capture --capture-wrapper <tool-build>/build/wrappers/d3d9.dll
```

`validation.json` binds the capture wrapper, live Northlight DLL, original client PE, closed binary trace, snapshot journal and observed client-code proof. A capture stops at the first confirmed Northrend static-ready checkpoint. A changed target size, failed shutdown, missing or incomplete sidecar or mismatched identity leaves `INCOMPLETE`. The renderer excludes this diagnostic capture from its ordinary FPS intervals.

## Check the captured scene

```powershell
python scripts/run_northrend_baseline.py replay --capture <stage>/capture --output <stage>/replay --tool-root <official-win32-bundle> --host <tool-build>/build/northlight-native-retrace.exe --timeout 300
```

The official parser reads the entire binary trace and emits the dispatch rows used by the exact marker/draw join, with its own bounded output. The join and the stand independently enforce device, thread, sequence, epoch and retirement. WoW's extra startup capabilities factory is confined to the observed adapter and MSAA queries and must retire before the first draw. The host checks texture witnesses against the immediately preceding creation and resolves snapshot identities through that exact live generation. Drawn geometry follows the original full trace. The replay runner checks transport completion, Northrend GI and static mesh readiness, the captured camera, and output size/MSAA. Use the frame-parity profile to check the rendered pixels as well.

The replay renderer verifies the native shadow provider against the observed WoW code and reads the current provider state from the joined snapshots. It can then submit Northlight directional shadows on frames where the game handed off the work. Historical captures without the shadow code proof keep the fallback, and the replay build does not write the provider state into its host process.

Each attempt writes to a new output path. Keep the reusable stage with its
`baseline.json` and linked inputs for future runs and replays. The live run
moves the closed renderer log into its evidence directory, restores the
stage's previous `WTF` and `Cache`, and removes the state generated by the
attempt. Replay drops its temporary `stand/world-cache` links, generated client
cache and the regenerable text trace dump after the tool exits.

Keep the run reports, closed logs, capture trace and snapshot journal, parity
images, and replay plan. `--keep-work` on `run`, `replay` or `end-to-end` retains
scratch for debugging. With this option, the live attempt moves its generated
configuration and cache to `generated-WTF` and `generated-Cache` in the output.

Parity output uses [compact captures](compact-captures.md) within each attempt.
Keep the `frame-parity/.capture-blobs` directory with the image references.
The harness reads legacy raw outputs and validates compact image bytes with the
same parity checks. Use `--raw-parity` for tools that require raw files.

## Check Reset and resize

```powershell
python tests/validate_native_scene_trace.py --tool-root <official-win32-bundle> --backend-dll <DXVK-x86-D3D9.dll> --backend-kind dxvk --northlight-dll renderer/frd9.dll --output <new-output>
python tests/validate_native_retrace_positive.py --tool-root <official-win32-bundle> --backend-dll <DXVK-x86-D3D9.dll> --stand-dll renderer/frd9-stand-replay.dll --stand-assets renderer/frd9-stand-replay.assets.bin --host <tool-build>/build/northlight-native-retrace.exe --client <client.exe> --reset-resize --msaa 2 --output <new-output>
```

The first command compares three frames before and three after `Reset` for AA0 and 2× MSAA, with apitrace above Northlight, below Northlight, and with DXVK alone. The second adds an explicit window resize, the stand's typed resource aliases, and real EVENT and OCCLUSION query checks. Both use a hidden authored scene, match every RGB byte at the six Presents, and verify the format and sample count of the depth-tested target setup through the trace. Raw depth after `Reset` remains unmeasured. The saved Dalaran trace covers World rendering and resolved depth during ordinary frames.

The bridge accepts fresh typed surface references from `GetBackBuffer`, `GetRenderTarget` and `GetDepthStencilSurface` when a successful getter returns a previously retired address. Prior interface aliases remain retired. The captured Dalaran shutdown exercised this path through the final device release after the trailing Reset.

## Compare resolved color and depth at 2× MSAA

```powershell
python scripts/build_renderer.py --profile-symbols
python scripts/build_renderer.py --stand-replay
python scripts/run_northrend_baseline.py stage --backend <DXVK-x86-D3D9.dll> --dll renderer/frd9-profile.dll --assets renderer/frd9-profile.assets.bin --output <parity-stage> --frame-parity
python scripts/run_northrend_baseline.py end-to-end --stage <parity-stage> --output <parity-stage>/candidate-check --capture-wrapper <tool-build>/build/wrappers/d3d9.dll --tool-root <official-win32-bundle> --host <tool-build>/build/northlight-native-retrace.exe --timeout 300
```

For an intermediate comparison, use `run --capture-wrapper ... --draw-probe
MIN,VERTICES,START,COUNT` on an existing parity stage. After the runner arms
the capture, the private wrapper reads a native-resolution region of the
legacy back buffer immediately before and after the first matching indexed
triangle list with base vertex zero. The capture receipt pins both images and
records the exact apitrace call number. The reads synchronize rendering and
resolve MSAA, so this mode is for image diagnosis only.

```powershell
python scripts/northrend_trace_probe.py --capture <draw-capture> --backend <staged-DXVK.dll> --tool-root <official-win32-bundle> --output <draw-comparison> --draw-witness
```

This comparison verifies the draw arguments against the trace, runs it through
the same DXVK binary and effective configuration, and counts RGB differences
on both sides of the draw. The strict full-frame acceptance remains in the
Northlight `replay` command.

The first Dalaran witness used 128 actors, 3840×2160 and 2× MSAA. Call
`8617362` submitted the selected 780-triangle section. In the 768×1024
region, live and stock DXVK differed at 220,828 pixels before the draw and
218,854 afterward. The draw made 1,974 previously different pixels equal and
introduced zero new RGB differences in that region.

Add `--resize-before-parity` to `end-to-end` to exercise real WoW resets.
After World entry, the runner asynchronously reduces the private window by
160×90, waits for the corresponding successful D3D9 Reset, then restores its
original size. Both Presents share one authenticated Reset epoch at the
restored size. The
capture receipt records the two back-buffer transitions, and replay checks
the selected epoch against the joined trace. With the 4K stage, the
transitions are 3840×2160 → 3680×2070 → 3840×2160.

`end-to-end` admits a staged live DLL and stand DLL built from the current
renderer tree. It captures, replays and writes a summary that identifies and
hashes both reports. The summary distinguishes live readiness, the verified
trace join, stand execution and the exact parity verdict. Use `replay --stage`
to replay a nested `candidate-check/capture` separately.

Add `--return-to-menu` to trace the character menu before and after the two
World parity frames. The live capture waits for a rendered menu before entering
World and keeps tracing through the server's logout acknowledgement and rendered
return to character selection. Replay verifies the two menu transitions around
the certified World frames. Color and depth parity applies to the World frames.

To reuse the frozen client for a new build, pass
`--live-dll renderer/frd9-profile.dll --live-assets renderer/frd9-profile.assets.bin`
to `end-to-end`. The runner verifies and retains the candidate DLL and its
matching shader bundle with the capture. It loads the pair in the existing
private client and restores the staged pair when WoW exits.

The parity profile fixes the native game clock at 12:00. The supported client clamps the server's speed of zero to its normal clock rate. Staging verifies the stock WoW 12340 hash and changes the four-byte speed floor in the private executable so it accepts zero. The runner verifies the exact noon value in every World snapshot between the selected Presents, and replay uses those same joined snapshots. The manifest records both the stock and diagnostic executable hashes.

The profile also freezes command streaming with frame skipping disabled. After the static-world checkpoint and 1.8 seconds of settling, live WoW records two Presents eight ordinals apart. The compiler requires the exact Presents and a World snapshot on each in the same Reset epoch. The stand exports the same ordinals and validates the full trace through release, including any Reset outside the sampled window. At each Present the GPU captures the full scRGB RGBA16F buffer after Northlight output composition and UI, resolved with `StretchRect`, and the actual Northlight scene depth resolved from D24S8 through RESZ into INTZ and point-copied to R32F. This covers the resolved images. It leaves the individual MSAA samples and stencil unmeasured.

The replay exits with code 1 on missing or invalid evidence or any bit difference. Its `validation.json` records the trace call, mode, image hashes, changed pixel count and first changed coordinate for each plane. `frame-parity/` retains the full pixel planes and the GPU journals. The selected frames also retain the linear world input to the HDR output pass, its exposure history when available, and the game's native color buffer when the HDR path has not overwritten it. The `HDR parity` lines report the effective display parameters. The diagnostic runs are excluded from FPS evidence.

### Run the background parity corpus

Reuse the same private linked stage and the production/replay DLL and asset
pairs from one source snapshot. Run the cases serially through the shared
offline GPU slot. For RTX 4090 background runs alongside an open WoW session,
set `DXVK_FILTER_DEVICE_NAME` to `NVIDIA GeForce RTX 4090`, set
`NORTHLIGHT_TEST_ALLOW_RUNNING_GAME=1`, and pass `--background`. Keep the
verified device-creation lines with the receipts.

Use a separate output directory for each case. Pass `--fixed-visual-clock
--match-geometry-start --fixed-gpu-budget --fixed-gi-publication
--light-history-audit --fixed-static-stream --fixed-shadow-bounds` to
`end-to-end` along with the paths from the example above. Use
`--light-history-between` for the additional per-Present history and
directional shadow fallback audit. If updates drift between the two timelines,
`--align-far-shadow-presents` requests the same far-cascade refresh boundaries
in live and replay and verifies the selected cadence in both logs.

| Case | Extra arguments | Readiness evidence |
| --- | --- | --- |
| Cold Northrend | `--scene solo --camera-turn` | Fixture acknowledgement and measured camera turn before the selected Presents |
| Dalaran crowd and Reset | `--scene crowd --crowd-size 128 --resize-before-parity` | All 128 names requested, renderer light census, and two successful Resets with restored 2× MSAA |
| Interior lights | `--scene interior` | Multiple ready point-shadow maps and at least two refresh intervals |
| Heavy terrain | `--scene terrain` | Terrain material submissions without D3D errors |
| Water and rain | `--scene shoreline` | Verified weather packet, precipitation mask and native water mask |
| Menu cycle | `--scene solo --return-to-menu` | Rendered character menu on both sides of the World interval in capture and replay |

Review the two selected frame verdicts together with the scene readiness
receipt. For heavy terrain, retain the material draw audit for any WMO or
alpha-tested path being qualified. The terrain checkpoint certifies terrain
submissions. For the native backend, record the device caps before requesting
2× MSAA. A confirmed missing RESZ is an unsupported case for the scene-depth
oracle.

Capture reports from format 2 also freeze the combined world-normal images
for both Presents. Replay requires a current, nonempty world-normal export
and exact agreement with the live plane at its half-resolution size. The
world-normal result is recorded separately from the resolved color and depth.

Compare two completed replay outputs for one capture with `compare-replays`.
The command checks their trace join, tool and asset identities, exact Present
calls and saved plane hashes. An A/A comparison requires the same DLL and
matching replay settings, including the visual clock. An exact pixel mismatch
returns `FAIL` with separate color and depth verdicts.

When the native color target is available, each Present also reports the
image format, sample count, changed region and pixel overlap with the
resolved depth and final color differences. The comparison ignores the
unused X8 byte of an X8R8G8B8 target and records both RGB and storage
differences. New captures freeze the native image receipt and digest.
Native color is diagnostic evidence alongside the strict color and depth
verdicts.

For a capture and replay on the same visual timeline, add
`--fixed-visual-clock` to `end-to-end`. The equivalent two-command workflow
uses `run --fixed-visual-clock` and then the usual `replay`. This opt-in uses
the Present ordinal at 60 Hz for authored lamp pulses, local and point-light
fades, the directional cascade anchor and transitions, probe activation
and blending, atmosphere transitions, cloud drift, and HDR exposure adaptation.
The game clock is fixed separately at 12:00. Profiling and scheduling
continue to use real time.
Replay inherits the visual clock from the capture report. Applying the flag to
an older trace is a same-trace replay experiment. The report records the clock
mismatch with the original capture and the strict paired gate requires both
clocks to match. Each selected Present also exports its ordered local-light
inputs in `capture-*-frame-parity-lights.jsonl`.

Add `--match-geometry-start` to `replay` or `end-to-end` with the fixed visual
clock. The runner verifies the frozen client log and joins the World checkpoints
in every Reset epoch leading to the image frames. The official trace certifies
each checkpoint's World snapshot, epoch and first Present after Reset. The same
two image frames still belong to a single epoch. The stand stages resources
ahead of each checkpoint and holds geometry consumption and actor shadow
selection until the corresponding capture Present.
At the checkpoint the stand may spend one additional staging budget on pending
mesh pages and materials. The regular memory admission and resource checks
still apply. `REPLAY_GEOMETRY_STAGING` reports this extra work and its result.
An unready or missing checkpoint produces `INCOMPLETE`. `compare-replays`
prints the checkpoint and treats a change in this option as A/B.

Use `replay --far-shadow-interval 1` to isolate the effect of retaining far
cascades. The runner checks the value the stand loaded from its private settings.
With the normal interval, `--align-far-shadow-presents` adds a refresh at every
interval boundary of the fixed Present timeline. Missing poses, invalid maps,
and content changes still use the existing refresh and fallback rules. This is
an opt-in stand diagnostic. Use the same setting in both halves of an A/A.

For a live and replay comparison of temporal lighting, add
`--light-history-audit` to `end-to-end`, or to the `run` command that creates
the live capture. Replay inherits the audit and reports `live_lighting_comparison`
for the same two certified Presents. For two replays of an older capture, pass
the option to both replays. The audit reads directional light
before and after GI, the screen-space visibility resolve and the previous
light/distance pair before local light and fog reuse those surfaces.
With `--fixed-visual-clock`, the audit also compares the per-Present
`LOCAL_INPUTS` logs through both selected Presents, including warm-up.
`live_local_light_inputs` records the first mismatch and number of affected
Presents for each stage, from native light snapshots and decoded draw banks
through equipment and particle publication to the selected constants.
With `--match-geometry-start`, the stand also uses that audit to admit the
first CPU World scene at the recorded Presents across Reset epochs.
The CPU scene seeds the lighting history before the renderable mesh is ready.
The trace join certifies both checkpoints.
Add `--fixed-static-stream` to the fixed-clock, geometry-matched audit to
isolate wall-clock variation in the static directional caster cache. At the
static update, both runs wait for the current CPU request to settle and stage
GPU models with a separate 2 MiB byte budget. The same memory guards apply.
Replay inherits the policy and reports the resident model content keys at the
two selected Presents. At the geometry checkpoint this policy keeps the
supplemental 32 MiB budget and uses the remaining staged materials as its
texture count limit without a wall-clock cutoff.
These runs are for image validation.
Add `--fixed-shadow-bounds` to use the caster operation, vertex and call
limits without wall-clock cutoffs in the same audit. The report checks the
ordered current-pose bounds and the work sent to the cascade and actor maps.
Its timeline reports the first shared Present with different work and the
Presents with evidence from only one side.
The same audit records each actor map's projection, ordered packet list and
history-based routing counts. The report identifies the first Present where
that routing differs.
Replay inherits the option from a matching capture. It can also run it on an
older trace for A/A diagnosis. The live report has no matching bounds-policy
evidence for that older trace.
The pre-GI image is skipped when the directional and GI passes are fused.
`compare-replays` verifies the saved hashes and reports pixel counts for
these inputs alongside the strict presented-color and depth verdicts.
Each selected Present also reports the captured material normal and
occlusion targets, the linear HDR image and exposure in `composition`.
The report identifies the first changed pixel in each available target
and the first coordinate that also changed in presented color. New live
captures freeze these product receipts. Earlier captures retain the
comparison with `composition_live_evidence_frozen` set to false.

On the 128-player Dalaran capture after Reset, Presents 290 and 298 each
differed at 97 pixels in the material normal target and at one pixel in both
linear HDR and scRGB output. At `(226, 575)` the final blue channel differed
by one FP16 step. Replacing only `BumpNormalNative` with the shader bytecode
from the captured build removed all of these differences. Resolved depth,
material occlusion and exposure were already exact. The verified normal-only
shader uses 131 static slots, up from 116 in the variant that differed and
below the generic `BumpNative` at 284 slots.

`light_history_overlap_with_raw` counts coordinates changed in both the raw
light and each of pre-GI light, directional visibility, and prior light.
History or visibility inputs that were unused are reported as skipped.
Temporal lighting accepts the current value for channels within the FP16
rounding band. Larger differences still use the reprojected history blend.
Use `--light-history-predecessors N` with the audit in both runs to collect
these, the directional projections, and the GI draw inputs for up to eight
Presents before each selected image. The report binds the expanded window to
the native trace calls. Final color and depth are still judged on the original
image pair. Extra GPU readbacks make this a diagnostic run.

The light-history audit also reads the full-resolution resolved scene input
used by the screen-effects pass. `scene_input_color` compares that copy and
records how many changed RGB pixels overlap with the presented HDR difference.
The existing `native_color` field reads the legacy target at the Present
boundary. The input copy and the final target serve different checkpoints.
The fixed visual clock also advances the weather blend and lens animation by
Present ordinal. The selected wet frames log the phase and blend consumed by
the late-weather pass.

The same clock times actor packet capture and GI refresh requests. The paired
audit compares the Presents where each side captured actors, queued their
refresh, and uploaded a probe atlas. Add `--fixed-gi-publication` to `end-to-end`
with `--fixed-visual-clock`, `--light-history-audit`, and `--match-geometry-start`
to wait for runnable GI work and publish complete near and coarse probes
together. The stand also waits for CPU geometry at the captured scene start.
The runner checks publication evidence and rejects timeouts. Use this mode
for image comparison. Performance runs keep asynchronous publication.
Add `--fixed-gpu-budget` to certify the full effects level in both processes.
The private quality file sets `GpuBudgetMs=0` for the capture. Replay checks
its hash and both logs must confirm the override. This avoids GPU-load-driven
quality transitions on the image timeline.
Add `--light-history-between` to audit the seven intermediate frames as well
as the two selected Presents. The journal includes the resolved temporal
light and the history read by the next frame. Final image certification still
uses the two selected Presents. On the second Present the capture also compares
HDR color just after world effects and just before exposure and tone mapping.

New paired captures also report `live_point_comparison`. They record the cube
bound at each submitted local point correction, its six R32F faces, the shader
constants and scissor, and the accumulated local-light image immediately
before and after those corrections. The ordered light roster relates source
identities across processes. A ready cube skipped by the white-map or scissor
gate never enters the sampled set.

For a model detail comparison, use the same certified Present calls in the
backend-only `d3dretrace` and the Northlight stand. Compare unscaled crops in
the backend-only image, the stand's `native-color`, `hdr-linear`, and final
color. Check silhouette, small texture features, and emissive edges at a
matched pose. A change in contrast can hide details even when the silhouette
is intact. `--model-bump 0` sets `AutoBumpModels=0` in the private stand
configuration for a follow-up replay.

Use the stock DXVK trace probe to follow individual backbuffer pixels through
specified D3D9 calls. It checks the saved trace and DXVK hashes and writes the
RGB samples to `samples.json` without saving the snapshot images. The probe
uses the effective DXVK settings from the capture log and verifies them in
its own log. It pins replay to the device created by DXVK during capture
and records the selected device and log hash for both runs. Use the exact
call numbers from the trace window, including the certified Present.

```powershell
python scripts/northrend_trace_probe.py --capture <capture> --backend <stage>/client/backend_d3d9.dll --tool-root <official-win32-bundle> --output <new-output> --point 1799,1482 --point 2069,1477 --calls 8513905 8513907 8538024 8538753
```

In the game overlay, **Materials & parallax → Material modes → Material
normal filter** selects **Legacy** (`AutoNormalFilter=0`) or **Filtered**
(`AutoNormalFilter=1`, the preset default). The selection applies to generated
material maps for terrain, walls, grass and models in the active normal,
parallax or displacement mode. The replay equivalents are `--normal-filter 0`
and `--normal-filter 1`. Replay verifies the loaded override.

When checking the Dalaran house crease, compare Present 282 and 290 on the
3840×2160 crowd trace with 2× MSAA and `--fixed-visual-clock`. Render both
filter settings with the same DLL and shader bundle. The crease runs from
approximately (795, 409) to (979, 471) in the original frame. Fit the two
depth planes on either side and use their intersection as the reference.
Compare the lighting transition in `hdr-linear` relative to a GI-off replay.
Both material filters produced about 0.22 px RMS deviation at the crease
after the GI fix, down from 0.60 px in the original. The switched-filter
replays had identical resolved depth and identical native color and world
normals in the wall crop. Full-frame HDR parity is a separate gate.

The same audit records the raw pivot, the cascade anchors, planned and
sampled projections, and the static cache projection and signature.
Reused maps report the pass in which their projection was committed.
The comparison identifies which matrix elements differ at each Present.
It also verifies the bytecode, constant bank and bound probe textures from
the actual GI draw. A separate GI pass includes the current RGB coefficients,
visibility moments, residency metadata, previous coefficients and local
visibility. The report records which textures or registers differ even when
the two draws name the same atlas publication number.
When a resident atlas carries solver evidence, its journal includes the
static solve's sun direction and sun/sky radiance, request and lighting generation.

```powershell
python scripts/run_northrend_baseline.py compare-replays --reference <old-replay-directory> --candidate <new-replay-directory> --output <new-report.json>
```

## Pass timings on the stand

Reuse a certified capture and run each stand DLL in its own output directory.
Keep the retrace executable, shader bundle, DXVK and requested Presents fixed.
Stage with `Diagnostics=1` and `RenderProfile=1` in `[Quality]`. The profile
validates the trace join, clock, camera and output mode, and collects
CPU and GPU pass timings without exporting pixel planes. Where point shadows
run, the validation JSON includes ordering and submission time, caster
triangles and counts for submitted, coalesced and instanced draws.
Leave two Presents after the last sample so GPU queries can finish. Keep pixel
parity runs separate to avoid including readback stalls in the timing samples.

Add `--no-world-shadows --no-world-fog` to a profiling replay to measure
the same trace with those two screen effects disabled. The runner changes
the private quality file and verifies both settings in the renderer's
`QUALITY` log. Use `WORLD MEMORY resources` to compare logical screen
target sizes between DLLs. Image parity uses the captured effects.

To measure the cost of local shadow coverage, add `--point-shadow-limit 4`
to a replay invocation. The override applies to that run's private quality
file. The report records the override and verifies the value loaded by the
renderer. Use the same DLL with the frozen quality file for the control.

Use `--point-shadow-adaptive` and `--no-point-shadow-adaptive` in separate runs
to compare refresh scheduling on the same DLL and trace. Use a profile step of
1 to sample both alternating sets of cube slots. `SHADOW_REFRESH` reports how
many complete cubes were held by the adaptive rule at each sample.

For an A/B comparison of draw cost, pass `--shadow-refresh-cpu-ms 0` in both
replays. The host verifies the private setting in `QUALITY`. The default 3 ms
budget can retain complete point-shadow cubes after a directional refresh.
Check `POINT workload` and `SHADOW_REFRESH` for equivalent submissions before
comparing GPU intervals. Keep the default budget for a separate scheduling run.

Use `--legacy-normal-declarations` and `--no-legacy-normal-declarations` to
compare vertex declaration ownership in the model normal queue on the same
stand DLL. `MODEL_NORMALS replay` reports CPU submission time, declaration
reads and packets sharing a retained reference.
The stand exports `model-normals` on the selected diagnostic Presents
alongside the combined world normals.

Use `--legacy-normal-pose-declaration` and `--no-legacy-normal-pose-declaration`
to compare how the early pose bounds check verifies the current declaration
against the material handoff. `MODEL_NORMALS pose layout` counts owning getters
and borrowed mirror reads. Both settings also export the two normal buffers in
image parity mode for `compare-replays`.

Use `--legacy-event-spin` and `--no-legacy-event-spin` to compare the two
D3D9 EVENT wait policies on the same DLL. The default gives a pending GPU
event on DXVK 3.1.1 a 2 ms spin window. The second switch tries a 200 µs window
on the stand. In a step-1 profile,
`CSTREAM QUERYFRAME` records wall time before, inside and after replay.
`CSTREAM EVENT_CPU` records replay-thread CPU cycles inside the real
`GetData` wait, the measured trip count and the selected policy. Match frame
numbers and compare the tail of the wall-time distribution alongside cycles.
`CSTREAM EVENT_QUEUE` splits the time before the callback into the producer
publication, arrival at a replay command boundary, retirement of commands
before the exact END, and entry into the callback. The row counts commands
outstanding at the producer snapshot and separately counts commands retired
between mailbox observation and dispatch. Ordered fallback trips still appear
in `CSTREAM QUERYFRAME`.

`MODEL_PREPARE` records the actor preparation worker on the same sampled
renderer frame. It preserves the published record count, records finished by
the worker, remaining records handed to inline preparation, wake count, and
times for the handoff, worker work and join. Check `worker` and `watchdog` before
comparing times. `-1` indicates a phase with no measurement. Worker time can
overlap renderer work. Add only disjoint intervals when estimating latency.
The producer checks for a sleeping preparation worker after every 48
publications, using the same mutex as the wait. `wakes` counts actual
notifications. `notifyUs` includes the batch checks. A partial final batch
can still pass to inline preparation at the join.

On the Dalaran crowd trace at 4K and 2× MSAA, two runs per EVENT wait policy on
frames 246–280 put median cycles per frame at 9.31 million with the default
and 2.26 million with the experiment. EVENT wall-time p95 was 48.58 ms and
84.12 ms respectively. Requested waits of about 50 µs included observed
wakes around 1 ms on this host. The early timer stays a stand experiment.

Compare dynamic directional terrain submission with
`--legacy-directional-terrain` and `--no-legacy-directional-terrain`.
`WORLD directional terrain` records the input batches, actual draw calls
and material bindings for each sampled frame. Group the timing samples by
input batch count because near-only and near-plus-far refreshes can fall on
different frames between runs.

Use `--legacy-directional-replay` and `--no-legacy-directional-replay` to
compare individual submissions and grouped replays. `WORLD directional replay`
reports logical caster packets, physical draws, triangles and native alpha
target scopes for the cascades and the character map.
With grouped replay active, compare disjoint opaque index ranges using
`--directional-index-pairs` and `--no-directional-index-pairs`.
`WORLD directional index pairs` counts candidates, submissions and index
buffer reuse by directional caster pass. The switch controls both pairs and
ordered batches of three or four ranges. `cascadeBatchPackets` and
`actorBatchPackets` count logical packets in the wider draws. Point pairs
are reported separately by `POINT_INDEX_PAIRS`.

Use `--point-index-batches` and `--no-point-index-batches` to compare point
shadow batches of up to four index ranges with the pair-only path.
`POINT_INDEX_BATCHES` reports the larger draws and the packets they cover.

Use `--point-index-admission-reuse` and `--no-point-index-admission-reuse`
to compare cached and per-face index admission on the same trace. The
`POINT_INDEX_ADMISSION` row counts actual eligibility tests and reused
results separately from the number of submitted draws.

Use `--point-effective-pose` and `--no-point-effective-pose` to compare
adjacent rigid range merging with the same stand DLL. In profile mode,
`POINT_EFFECTIVE_POSE` counts the merges for each sampled Present. In image
mode, the validation records that count for every captured Present with point
shadow work, and `compare-replays` records the policy and samples for each run.

`--normal-palette-instancing` and `--no-normal-palette-instancing` control
ModelNormals palette replay. A profile includes `MODEL_NORMALS palette instances`
for each sample. Image validation records the active policy and the instanced
draw and packet counts at captured Presents. `compare-replays` compares the
model and world normal products by hash alongside the usual frame planes.

Keep `--normal-palette-instancing` enabled in both runs to isolate
`--normal-palette-binding-reuse` versus `--no-normal-palette-binding-reuse`.
`inheritedDraws` counts instanced draws that inherited active streams.
`skippedBinds` counts redundant setup calls omitted. Compare these with the
instanced draw and packet counts and the ModelNormals CPU and GPU timings.

With palette instancing enabled, use `--normal-palette-instance-rows` and
`--no-normal-palette-instance-rows` to compare moving the additional shared
vertex constants into each instance. Compare total physical ModelNormals
draws, the ordered capture and point shadow workload, and the captured
normal and color products. In the game, select this opt-in with
`NORTHLIGHT_NORMAL_PALETTE=2`.

Use `--point-mixed-reuse` and `--no-point-mixed-reuse` to compare reuse of
the opaque portion of mixed point shadow faces. In the game, enable it
with the environment variable `NORTHLIGHT_POINT_MIXED_REUSE=1`. The profile row
`POINT_MIXED_BAKE` counts reused faces, packets and triangles, and extra
draws used to populate the cache. Add `bakeDraws` to physical replay draws
when comparing submission counts. Compare the original triangle and packet
counts by adding the reused work to the new draw counts.

Use `--profile-fixed-work` in both profile runs when wall-clock admission or
GPU resource addresses change the workload between identical replays. It uses
operation and vertex budgets for pose bounds and orders opaque point casters
by resource occurrence in the captured draw list. Buffer uploads use the
existing attempt and byte limits, and the 200 ms GI actor capture interval
follows the replay Present counter. In the private replay profile the runner
sets `GpuBudgetMs=0` and `ActorShadowCpuMs=0`. The adaptive controllers cannot
change fog sampling or the actor shadow radius between the two runs. The
quality receipt verifies both values in the renderer log. The runner also certifies the recorded
World geometry checkpoints across Reset epochs. The validation receipt requires
the stand to confirm the mode and those checkpoints. Check A/A draw and coverage counters
before using the same profile for an A/B timing.

Add `--fixed-visual-clock` to both fixed-work profile runs to derive visual
time from the Present ordinal. GPU timestamps and the CPU timers still
measure elapsed time. The receipt records the selected
`profile_visual_clock_hz`.

The ModelNormals coverage receipt records the count and ordered digest of
original draw ordinals that reached the normals output on each sampled
Present. It counts every logical instance in a grouped submission and omits
redundant packets that never draw. Compare the receipt on the same trace
alongside pass settings and draw counts. The receipt allows comparison when
the culling stages divide work differently but submit the same packets.

Use `--profile-sdr` with `--profile-frames` for paired GPU timings on an
adapter that lacks the captured HDR output path. The runner disables HDR in
its private renderer INI and checks the observed SDR resolution and MSAA.
Compare runs with the same adapter and output mode. HDR image parity still
requires the captured HDR output path.

Cached index admission is opt-in on the stand. At 4K and 2× MSAA the two
run orders cut the median eligibility tests from 5,634 to 163 and from
5,709 to 165 on matching Dalaran frames. Both modes issued 2,900 replay
draws per sampled frame. Median point submission changed from 1.278 to
1.234 ms and from 1.273 to 1.401 ms. All 132 compared cube faces matched.

Use `--point-replay-state-reuse` and `--no-point-replay-state-reuse` to compare
geometry binding reuse across faces of each point shadow cube. `POINT_REPLAY_STATE`
reports animated faces, geometry binding calls and vertex constant uploads.

Use `--crowd-bounds-operations` and `--no-crowd-bounds-operations` to compare
crowd-scaled and fixed bounds operation limits. Both modes keep the same time
budget. Check `POINT workload` for unbounded packets and submitted replay draws.

Add `--point-shadow-audit` with `--profile-frames` to count caster packets
by face and assess possible batches. This diagnostic pass adds CPU work.
With image readback, the option exports all six faces of each ready point
cube. Compare two such outputs with `compare-replays`. It joins cubes by
source ID and reports face hashes for matching committed projections.

```powershell
python scripts/run_northrend_baseline.py replay --stage <parity-stage> --capture <crowd-capture> --output <new-profile-directory> --profile-frames 238:280:3 --stand-dll <stand-dll> --stand-assets <stand-assets> --host <tool-build>/build/northlight-native-retrace.exe --tool-root <official-win32-bundle> --timeout 300
```

Repeat runs with the same build to estimate timing variability. Compare cold
work separately from warm frames, using caster counts and bounds coverage to
check the workload. The retrace host feeds D3D9 commands and can leave GPU idle
gaps inside the measured intervals. Use these measurements for stand comparisons
of individual passes and keep gameplay FPS evidence separate.

Use `--legacy-normal-range-bank` and `--no-legacy-normal-range-bank` with
the same stand DLL and inputs to compare reading normal range constants from
the mirror or borrowing them from the material bank. On
`--capture-work-frame 282`, the per-draw `range_read_registers` and
`range_handoff_registers` count the two sources. Use a separate
`--profile-frames 282:290:2` run for the timing comparison.

`--capture-work-frame 130` selects an earlier World Present in the same
fixed-clock Dalaran trace. The compiler joins it to its original trace call
and World snapshot. Image readbacks still use the two original parity frames.
The audit reports per-draw VB/IB lock spans, whole-buffer fills and replay copy
views alongside each consumer's snapshot charges. Buffer identities are local
to the run. Compare logical lock spans separately from driver transfer or
residency measurements.

The same audit groups capture work by registered shader class and mirrored
alpha-test state in `summary.capture_classes`. It covers draws admitted to
the capture dispatcher and preserves an unknown class and state. The rows
include draw and primitive counts, UP submissions, per-consumer packet and
register counts, and VB/IB read spans. The M2 class is based on shader identity
and known skinning metadata. Proof of a grouped M2 submission comes from the
live producer's dispatch and palette evidence.

Compare `--legacy-shadow-projection-bank` and
`--no-legacy-shadow-projection-bank` on the same stand DLL. With
`--capture-work-frame 282`, the world consumer records the projection
registers read through the getter and those borrowed from the same draw's
material bank. Compare the sums and packet counts at the same draw ordinals.
Use `--no-gi-dynamic-probes` in both arms to compare packet admission with
static GI and without the wall-clock actor capture cadence. An unmodified
quality profile still exercises the full scene.

Compare `--legacy-shadow-packet-bank` and
`--no-legacy-shadow-packet-bank` to isolate float reads for the shadow replay
packets themselves. The World lane reports them in `range_read_registers`
and `range_handoff_registers`; projection retains its own counters. Both the
initial capture and later dirty-row spans can copy from the certified
same-draw bank when it covers the request. The packet owns the copied data.
The ordinary getter still supplies uncovered rows and independent self-checks.
Use the same trace and options on both sides and compare the produced cube
faces with `--point-shadow-audit`.

Compare the two-frame VB/IB copy admission with the requested-span policy
using `--legacy-copy-admission` and `--no-legacy-copy-admission` on the same
stand DLL. `--profile-frames 130:290:8` reports cumulative read locks, fill
and served byte counts, and current CPU copy charges at the sampled
Presents. A separate `--capture-work-frame 130` run records exact VB/IB
spans and takes an address-space sample at that frame.

## First verified run

On 19 November 2026 the live build `11c309d23205` ran with DXVK 3.1.1, 3840×2160, 2× MSAA and immediate presentation. Two runs with the same inputs and camera averaged 11.19 and 10.12 ms over three warm 120-frame windows each. Camera-to-GI readiness was 3.42 and 3.16 s. Camera-to-static readiness was 4.22 and 3.88 s.

The separate diagnostic capture contained 616,362 native draws, 535 Presents, 1,298 frame-context snapshots and 3,512 texture identity witnesses. The x86 replay host completed the original stream with 537 real query calls. Northlight produced a Northrend GI atlas and committed static geometry on the stand. The capture contained zero `Reset` calls. This run preceded the frame-parity profile.
