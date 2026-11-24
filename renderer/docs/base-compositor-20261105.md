# Base HDR composition performance, 2026-11-05

**The validated `.11` installer includes the first five follow-up changes.**
See `out/installer-20261105.11/summary.json` for acceptance results. The timings
below describe an earlier compositor build. They do not measure game FPS for `.11`.
CPU changes through the full-cache guard shipped in a separate DLL built from `32110bc3`
at 17:50:28 UTC. The new game log confirms that DLL was used. The subsequent
Entry storage optimization was still source-only at this point. The final sections
record the current build and log evidence.


The base Weather path uses a faster final HDR compositor on DXVK 3. On two
recorded inputs at 3840×2160, its GPU interval fell by
24.5–24.7% and the measured frame processing interval fell by 15.9–16.6%.
Readback frames match the SDR and HDR controls **bit for bit**.
This test measured compositor timings. At this point the production DLL and
package had not been built. The installed `D:/Games/335_engb_mod/d3d9.dll` hash was
`c6600674b1b59e12936e450d84a538cc7503c3957b694873867c3dd147958399`.

## What's changed

Added one non-mandatory sheder `WorldBaseCompositeHdr`: 420 static
SM3 and 17 temporary registers instead of 493 and 22 y `WorldCompositeHdr`.
He doesn't need to read neutral fog/local/AO-alpha entrances and calculate zero compression.
There are no additional textures, render tags and geometry.

Four samples left to restore light at depth boundaries, rebound
Fallback test, lighting, NightBrightness, Weather/horizon haze, transport
a normal fog, protection of alpha layers and emissions, rain masks.
not disconnected from the general route.

The choice of a fast chader requires simultaneous:

- actually selected DXVK 3 or newer confirmed by the existing test
  Backend versions;
- HDR, lack of combined material passage, volume neutral buffers
  fog and current local light;
- own neutral AO-entry or explicit confirmation of the code calling,
  that the Alpha of the texture transmitted is 1. The quality flag itself is such
  It's not a confirmation; Device transmits it for the ConnectAO branch=0
  where a bloom-only passage from an Alpha 1 or clean an Alpha into 1;
- The failure shall preserve the normal passage.

All seven existing sheaders of the composition are preserved by bit.
DXVK 2 and inappropriate entrance footage are used by previous sheders.
Releases a new shader object and allows a re-created creation.

## Couple measurements

One EXE test piece contains the current `WorldRenderer` and both HDR-shaders.
Control prohibits optional sheader after creation of resources; both branches
Act the rest of the common code.
Each launch: 16 warm-up and 24 taken into account SDR/HDR.
The GPU tests were performed consecutively, with a closed game.
GPU-fence outside the measured area.
The adapter in the DXVK log is NVIDIA GeForce RTX 4090, and the results refer to this
The equipment and the loaded backpack, which SHA is stored at JSON.

| DXVK 3, 4K-entry | ♪ The song, it was ♪ | Change | Measured base GPU interval, was ~ steel. | Change |
|---|---:|---:|---:|---:|
| capture 1 | 0,221184 0,166912 ms | −24,54% | 0,350720 0,292352 ms | −16,64% |
| capture 2 | 0,224256 0,168960 ms | −24,66% | 0,358656 0,301568 ms | −15,92% |

Entry: `D:/Games/335_engb_mod/northlight-diagnostics/session-30804-228518734-0`
`capture-{1,2}-{scene.fgr,depth.fgr,constants.f32}` Saved
Pixels of stage, depth and camera; the other conditions are set by the Weather-only stand.
It's not a complete playback: native draw heads, model capture,
the live RESZ, HDR post and waiting for the game line are outside this interval.
CPU and GPU are closing; their duration cannot be folded.
may include addictions and pauses in the delivery of commands.

For each cappache, SHA-256 SDR/HDR readback coincided in all five launches:
Two controls, two quick checks and verification of missing AO confirmation.
In the last test, the actual SetPixelShader counter confirmed the choice
Native also gave identical readbacks with the conventional sheader.
A slight difference in the timemings of Native is the sound of repeat launches.

Machine results and reference lines:

- `out/composite-max-20261105/world-comparison.json` is the final ABBA, two cappchores,
  Native fallsback, SHA footage, times and separate accounts;
- `out/composite-max-20261105/world-final/profile.json` - Phase profiles CPU/GPU
  source identity and SHA entrances; production heders coincide with frozen
  with a copy `world-final/baseline/src`;
- `out/composite-max-20261105/hdr-exact/validation.json` - 12 Pixels comparisons
  limits of depth, odd dimensions, sky, near/far water and 4 K on DXVK/Native;
- `out/composite-max-20261105/hdr-atmosphere/validation.json` - Four more
  Precise comparison of DXVK: regular fog, dense regular fog, smoke, night;
- `out/composite-max-20261105/hdr-coverage/validation.json` — three more accurate
  Comparisons by DXVK: Alpha control, mission protection, rain mask.

SHA-256 New Shader:
`ad8a6f65823d68ce23d2304c758ba0952c110c5e25bf910bdef2f956657cf751`.
`tests/test_world_shader_manifest.py` checked all 45 world sheaders, arrays
Headings, SM3-limites and negative control of damaged data.
The metainformation of freshness world/transition has been updated.

## Rejected Options

A common sheader with additional dynamic branches gave the difference of rounding
In active SDR incident; the combined material option also exceeded previous
Specialized SDR distinguished FP16 at selected borders.
Their production systems have not been replaced.

The forced rapid HDR on Native was a small synthetic scene,
But at the game entrance changed 1 896 914 components, to a maximum of 0,00048828125.
Therefore, the passage is limited to DXVK 3; Native retains the original composition.
The experimental results remain in `pilot`, `base-pilot`, `base-exact`,
`sdr-exact2`, `paired2-native` and are not accepted checks.

## Game Check

A line has been added to the diagnostic sampled frame:

```text
WORLD composite rendererFrame=... variant=base-hdr|standard-hdr|standard-sdr width=... height=... fog=... local=... aoAlphaNeutral=... combined=...
```

It `rendererFrame` is connected to the existing `GPU profile frame=...` and
`WorldComposite` This allows verification of the actual selection of the sheader
And his share in the game aisle.
The previous collected game log contains additional effects included
and crossing with another GPU, so it doesn't confirm an increase in the FPS.

Repeated check of the sheader without DLL assembly:

```powershell
python tests/validate_composite_work_gpu.py --base-hdr --reference-directory shaders/compiled --backend <verified-dxvk3-d3d9.dll> --output <new-output-directory>
python tests/profile_base_weather_pipeline.py --base-composite --backend <verified-dxvk3-d3d9.dll> --output <new-output-directory>
```

Additional autohored accidents are selected via
`--cases fog,dense-fog,haze,night,coverage,emission,rain` First stand.

## Continuation without running

At the request of the user, the following changes have been made only to the originals:

- The quick HDR option adds a reading blank for RainMask when proven
  The flag is taken before the frame-local indicator is cleaned; rain gauges are removed.
  The effects and the existing arithmetic of mixing remain the same.
- Reconversion of surface RGB to HDR is replaced by already calculated
  Native RGB, only when the color input of the normal fog is exactly zero.
  `surfaceFogIsZero` Checks the parameters **uploadedFog** including retention and
  The source must be a folded UNORM; negative
  or unconfirmed float source saves a full calculation.
  The fog isn't taken as a blackout. `legacyT`, light and weather don't change.
- Removed 23 excess D3D call from the conventional Weather-only path without soft Shadows:
  Six repeat sampler settings s13 before WorldNormals; six configuration s15,
  which normal WorldLighting does not read; three GI training calls when
  GI does not draw; eight unchanged address/mip/sRGB settings s9/s14 after
  The required filters and settings for active passageways are maintained.

Final protocol c80 now: x is neutral volume fog, y is neutral local
Light, z is the missing rain mask, w is a proven coincidence of native/surface RGB.
Added to future checks for black fog retained colour fog,
identity fog and float source with negative channels. Checks have not been started.
The composition diagnostics were obtained in fields `rainAbsent` and `nativeSurfaceShared`.

Binarians and generalized shader header were deliberately not updated.
The permitted cycle shall first be reassembled `WorldBaseCompositeHdr` and updated
regular freshness of dependent transition drivers, then perform pixel/state
Stends refuse to accept the outdated Base HDR binary for
Checking the modified HLSL.
versions **Not yet measured**The image of the originals is stored in
`out/composite-max-20261105/no-run-followup/before`.

## Second without running

The next set of amendments affects the preparation of D3D without changing the chader
It wasn't about to start either.

- After Temporallight instead of 68 world float4 plus repeated 6 material-float4
  are transferred to API D3D c7.c27: 21 flat4 instead of 74, 336 byte instead of 1184.
  The size of the API arguments, not the measured CPU-GPU traffic.
  A full upload source audit found the low bank records only in
  c7..c17/c27 For source/actor/filter- passes. GI/TemporalLight do not record
  The constants and the material c73.c78 are from WorldLighting.
- In frame without local/positioning light, materials and diagnostics
  The non-use material s9 and normal s14 and their s14 filters are connected.
  **POINT filters s9 are retained without doubt**: subsequent fog/blur/
  Final passageways. FogClouds/FogTemporal tie their own s14s.
- Relinking baselinelight in s12: after WorldLighting this
  The slot doesn't change until the local passageway.
- After point/flash instead of c....c67, the c..c52 is restored.
  ..c12, flash-- c7..c16/c36/c44, local batches — c36..c52.
  c53..c67 remains the same. 240 Byte has not yet been sent to this branch.
- The condition of availability point/flash shall be calculated once; drawing, cleaning and
  The light position is maintained for material, diagnostic capture and debug.
  The preparation of the entrances has been maintained.

The normal Weather-only route gets another 9 remote D3D calls; along with the first
The set is up to 32 calls per frame based on a static count. It's not measuring time.

State control should compare **different WorldRenderers** rather than
Only two sheders inside one EXE.
`tests/profile_base_weather_pipeline.py --world-header <frozen-world_renderer.h>`.
Photo before second correction:
`out/composite-max-20261105/no-run-followup2/before/world_renderer.h`.
The bar retains the selected header, its SHA and the apparent field `world_header_override`.
The normal and control EXE shall receive the same re-assembly sheaders,
Capitches and settings; then compared SDR/HDR readback, CPU, psRegisters and
D3D calls. Full material/fog/point and state-remediation checks remain
new results FPS, GPU-time
Or there's no exact match to this set yet.

Automatic pixel verification of this control has been prepared:

```powershell
python tests/profile_base_weather_pipeline.py --base-composite --compare-current --world-header out/composite-max-20261105/no-run-followup2/before/world_renderer.h --backend <verified-dxvk3-d3d9.dll> --output <new-output-directory>
```

It consistently collects the control and the current measurement EXE, checks
The continuity of common entrances compares SHA SDR/HDR and leaves separate
API/Constant profiles. One launch of each option confirms pixel control;
This control is required for the speed determination by ABBA. **never started.**.

## Third continuation: surface and transcribing/recording

These changes have also been made **without assembly and running.**New numbers
There are no results of speed or confirmation of pixel equality for them.

1. The current R32F deep is already recorded by WorldNormals in `temporalDepth[cur]`
   if `c30.y>0`RemovalSmooth reads the same surface.
   He then re-transmitted it to MRT and re-recorded the same performance distance.
   Now, if this manufacturer is available, the TeamLight leaves RT1 empty:
   The first recording of the current frame is left for the composition and the next story.
   Both programs use the same formula canonical half-grid
   normalizedDept \viewDistance; Normals, current depths, performanceports and constants
   The projection between them is not changed.
   If WorldNormals does not write dissistance, the former MRT Capitallight is retained.
   The calculation itself and the accumulation of light are being carried out in full.
2. The neutral raw fog `(0,0,0,1)` remains in WorldRender
   Re-activated inactive frame no longer changes to this
   Render Target for identical cleaning.
   The sign of neutrality is dropped, including the path of partial error.
3. Similarly, re-cleaning is missing already neutral and valid.
   fog history. In active time processing, the topic is dropped to
   At Reset/resize/releaseGPU both topics
   The history of leaving an inactive area preserves the old
   semantics and weight; fog/weather not switched off.

For 3840×2160 half-res, the surface contains 1920×1080 pixels:

| Repeated operation | Surface size |
|---|---:|
| R32F distribution write in CapitalLight | 8 294 400 Byte |
| Cleaning the neutral raw fog RGBA16F | 16 588 800 Byte |
| Cleaning neutral fog history RGBA16F when this branch is active | 16 588 800 Byte |

The normal Weather-only frame with standard depths is removed
Re-recordance and raw fog cleaning: surface operations
total size 24 883 200 byte per frame. This is **logical size**, not measured
VRAM traffic: compression/fast Clear depends on the driver.
It is also significant that distals after reading RemovalSmooth are larger.
does not return to render-target mode before the next reading.

Full screen copying of the original scene and history of light left: scene
You need it as an unalterable entrance, and history suppresses blinking.
HDR Target requires separate verification of all late spell/UI/celestial- tracks;
Without it, removing such a copy could change the image.

Future checks prepared:

- Comparison frozen/current now includes not only SDR/HDR colour, but also
  R32F dissance and raw fog RGBA16F after each mode.
  additional surfaces to be carried out after the last measured
  frame/fence. The measuring probe adds counters `clears` and `mrtBinds`.
- `--no-directional-source` only deflates the weight of sources in offline fixture,
  To check the old MRT track separately when normals don't make dept.
  It's not a product setting.
- In the full text of the text, the empty boxes are added:
  The history remains the same and the Clear is not repeated.
  re-entry into fog and weight 0,85 retained.
- `distanceFromNormals`, `fogClearSkipped` and
  `fogHistoryClearSkipped` Photo before these corrections:
  `out/composite-max-20261105/no-run-followup3/before/world_renderer.h`.

The exact comparison of R32F (including edges and edges) is required before the changes are adopted.
odd sizes), color frames missing source, fog transfers,
resize/reset, refusals and state recoupling on the back.
The color does not prove the equality of data deptth history.
Current user restriction checks have not yet been performed.

## Fourth continuation: total D3D status

`Device::renderEffects` already holds `SavedState` throughout processing
The effects caused from there `WorldRenderer::render` kept
`D3DSBT_ALL`, four render bars, dept and vewports, and recovered them before
This is now transmitting an external saver:
internal Capture/Apply and their individual requests/rehabilitation are not fulfilled.

This is a clear contract only for this challenge. `SavedState` should be successful and
To belong to the same device; otherwise, there is a way of remaining on its own.
The external RAII site restores the state of the game after early return.
The following celestial/mask- traffic shall establish its own entrances/state;
Fallback branch re-calls `effectState` and `bindEffects`. Shaders, resources,
The authorization and calculation of the lighting shall not change in this extension.

In the old `world-final/profile.json` median HDR `WorldStateCapture` and
`WorldReturn` were 0,01110 and 0,01525 ms. These are **inclusive intervals** s
It's not a clean time to save and not a promise to win.
The timeting has not been removed. One complete storage and application is statistically removed
The state lock for the game call, along with the service calls of D3D.

Control `--keep-world-state-save` has been prepared for the same current source.
The stand still supports frozen header without a new argument.
The counters require that there be no internal Capture/Apply with total storage.
Additional checks outside the measured frames compare RT/depth, viewport,
Sheaders, stream 0/indices/declaration, all pixel texts and sampler states
sRGB, flat constants PS/VS and key render states are tested.
Passage, failure of non-standard external storage and DrawPrimitiveUP error.
Add `callerStateShared` to the sampled log.

Photo before change: `out/composite-max-20261105/no-run-followup4/before`.
These changes and completed checks **were neither collected nor launched.**

## Fifth extension: CPU-take of general passage standards

B `NorthlightModelNormals` Reduction of work in the preparation of deferred draw
Packets. This receiver is used with the main lighting switched off by AutoBump.

- In the past, each package accepted copied all 256 VS flat4 and replayed
  I've been comparing the full bank with the previous one.
  The patched chader analyzer shall once determine the range to be covered.
  Capture, comparison and first download use it. Relative address
  continues to expand reading to the end of the bank; definitions `def` take into account
  Existing analyser. For a change of sheader, comparison is only possible if
  The previous package took over the whole new range, otherwise it's fully loaded.
  Full reading for unconfirmed range verification is saved.
- Notification `pendingChanged` shall be executed when the first person waiting appears
  Texture Dependence Adding Opaque-Packets without Texture
  Texture packages don't change the global write guard.
  The call went to `Device::spellSync`, which also ended the neural control.
  MRT batch. Cleaning the empty line still notifies the code calling;
  `samplesTexture` and the processing of real texture records has not changed.

In old `capture2/profile.json` controlled 443 drow calls without materials
They gave the mid-point CPU 0,2399 ms without internal phase clocks.
Prepared by a workingload not full game frame; microtimers themselves are visible
It's the same thing that affects its duration. The new results were not taken.
Capture-stend does not connect the game callback with control MRT and does not measure
The benefits of saving these series.

Checks of accurate normal/depth result: limited volume
GetVertexShaderConstantF, no notifications with a normal addition of opaque,
shift of narrow/wide sheaders with variable bone 74, first and repeated
Alpha dependence, texture in use and outside texture.
Previous UP/buffer source verifications, changes, validation, fallsback and
D3D. `profile_base_capture.py --normal-header` can be assembled
Future control of the frozen title with the same other entrances.

Photo before change: `out/composite-max-20261105/no-run-followup5/before`.
Shaders and their arithmetic, quality, range, and geometry are preserved.
Collections, checks and measurements of this extension **have not been started.**

## Acceptance `.11` and continuation of the new CPU log

Collected production DLL and Windows ZIP `.11`, source identity
`03d8cffbde64824be0cdddccfb88a8f0e86a4a1d6353bf8c22222293e0ae4e18`.
17 accurate HDR comparisons, eight VS2/VS3- capture options passed
Standard, WorldRender with MASA 0/2, colour/deep/fog and external checks
The package and its six entry points
Checked in-line Python from Unicode Paths.
FPS. For rigid VS, seizure 32 flat4 was confirmed instead of 256.

The new game log contains the same building identity.
`out/cpu-followup-20261105-new-run/{renderer.log,analysis.json,analyze.py}`.
In a late window with a render frame >=6000 mid-point CPU: Capture 1,988 ms,
Notes 1,435 ms, cleanup 0,846 ms.
The slots do not add up; nealDraw contains the original game delivery.
Started with C++-compilation to 13:12:31 UTC; analysis uses
GPU reference ended in 13:10:53 UTC and pixel/state control
completed after the game, in 13:15:27 UTC New A/B No FPS match.

The following correction concerns `DeviceMirror::TargetWrite`: `SetRenderTarget(n)`
Now disabled is proof of absence only for slot n.
RT0 cannot bind a resource in previously empty RT1/2/3. Previous total disability
forced to check all three slots again, even after normal switching
The only colour target in the box is the mid-point of GetRenderTarget - 3721
sample; this is a direction to reduce overhead, not a promise to delete
All these challenges.

Reset, state-block apply, RawScope and unknown changes are still
The error setter causes a general invalient.
The calls made do not restore the old flags: every changed slot
is dropped separately and reading of evidence is prohibited during the recording.
Public HRESULT, exit signs and COM links have not changed.

The full portable Mirror kit was run by O0/O2 and Asan+UBSan via a clang++ on WSL.
On 1500 RT0 shifts old 4500 additional backend GetRenderTarget
deleted; one affected slot shall be rechecked at RT1/2/3 change.
The title is compiled and falls on a new limitation on the number of requests.
Saved checks on the input grid, errors retained null, Reset,
:: State Blocks, RawScope, foreign stream and unplugged mirror.
Evidence: `out/cpu-followup-20261105-new-run/validation.json`.

This last correction is only in the source; `.11` has not been re-assemblyed yet.
Shaders, permits, light parameters and geometry have not changed.

## Continuation of CPU: Preparation of model range verification

`NorthlightModelNormalRange::Cache` Now saves exact reference intervals
The position and the maximum absolute coordinates together with an unalterable interference.
Their previous calculation was repeated for every warm draw.
The projection, limb/overfill verification and arithmetical verification of DP4 remained ongoing.
For proven source mixed-bone, verification immediately retains capture:
It cannot cut off such a mix and no longer reads seven pose/project registers.
The key still checks the owner of the mix and offsets position/indices;
The position results are not cached. Limited 512 records saved, CPU metadata
increased by 24 KiB for the whole table; geometry is not strongly referenced.

The state-of-the-art CPU test went through O0/O2 including all 75 bones, position change,
projection, layout, reuse of source address and reading error.
Added control of 1000 warm mixed-bone calls without reading the constant;
The old headline compiles and falls on this control. 120000 Comparisons
All the decisions to cut (26289 cut-offs) were consistent with the image to correct.

In a separate x86 Windows Zig O2 measurement of seven warm sources, one of which
mixed, mid-point of nine successive rounds of 100000 calls: 124,779
106,614 ns for verification, about -14,6%. The case of the accident: 9,014 ~ 8,939 ns.
This is the cost of CPU checks only, not the measurement of the game FPS.
`out/cpu-range-followup-20261105/{validation.json,differential-bench.log,check.py}`.
Edits remain in the source; DLL and Installer were not re-assemblyed after `.11`.

## CPU continuation: maintenance of general geometry buffers

In `NativeStore`, searching for one GPU-buffer next to each other was full-time.
`entries_` In the case of displacement and transfer of possession between Material/Shadow.
The buffer keeps the beginning of its fur list, and the recording is the next player.
These rounds are limited to 16 buffer participants instead of the whole table up to 8192 records.
The removal of the participant updates the list until the buffer is released; the walk is retained
The next entry before removing the last link.
There has been no change in staffing, memory limits and rules for the preservation of common resources.
Additional entry fields are taken into account in the current budget `sizeof(Entry)`.

Regular `test_canonical_gpu_scene.py` passed O2 and ASan/UBSan: base bytes
Real M2 customer closure 0/50/100%, reconstruction
Source, conflict, errors Create/Lock/Unlock, limits and re-entry.
Added head removal/mid/bonetail control, transmission
The remaining lots and the last release held by native viewers.
The bytes, the number of downloads and the suitability of the held buffers are maintained.

CPU-comparison with the image of the title before change: nine pairs in rotation
In case, both consumers; side with 512 residant mixes are cleared,
The rest of the table belongs to another consumer. 8000 General records
median cleaning 3138,5 → 2178,4 μs (-)30,6%) with the preservation of the right of the other
I'm passing through the furs. 2791,0 → 1659,8 μs (-)40,5%All 108 Launchs shall be checked
A saved set of furs, their bytes and no new downloads in cleaning.
It's an isolated CPU operation with a Fake D3D, not a game FPS and no GPU measurement.
Evidence: `out/cpu-native-batches-20261105/current/validation.json` and
`out/cpu-native-batches-20261105/{comparison.json,compare.py,maintenance_bench.cpp}`.
DLL and Installer remain `.11`, this correction is only in the source for now.

## CPU continuation: single LRU clearance passage

After a reduction in the search for neighbors `NativeStore::trim`, still started searching.
each next victim from the head of the common LRU, repeating a long prefix
The position now remains within the call.
Before removing the next element `erase`, moves it along a still living LRU;
That's what you need when you remove the buffer's neighbors, and when you move out of another passageway.
In `preserve`, since preservation transmits records only from the purified
The way you choose the oldest records remains the same.
is recovered when leaving the region; there are no new fields for mess and excretions.

O2 and ASan/UBSan passed through the regular set.
release, order after touch, the alien precursor LRU and the transmission that
replaces the next entry directly from another user.
Exact byte contents, resident meshes, upload volume, and limits match the control.
The negative control compiles. Its byte-release assertion fails because removal leaves
the cached position stale. The version under test passes the assertion.

The same 108 CPU runs use 512 resident meshes, tables with 512, 4096, or 8000 entries,
and nine alternating pairs for each case and consumer. The baseline includes the
previous buffer-neighbor optimization. At 8000 entries, the median clear time fell
from 2238.2 to 789.9 µs (64.7%). Retaining shared buffers took 1690.5 → 479.6 µs (71.6%).
Shaders and geometry match the baseline. This isolated CPU cache benchmark uses
a mock D3D device and reports CPU time.
Evidence: `out/cpu-native-trim-20261105/{comparison.json,negative-control.json}`
and `out/cpu-native-trim-20261105/current/validation.json`.
The DLL and the installer were not re-assemblyed after `.11`.

## Continuation of the CPU: growth and removal of the array of common buffers

`NativeStore` left two square sites: `reserve(size+1)` before
each new buffer and search with all the next `unique_ptr` at its side
The mass is now growing geometrically; the place for the next addition.
It is still reserved to GPU work.
its place is the last element whose index is updated and released
The buffer remains in the local owner until the array changes are completed.
The order of drawing and LRU is set by the records, not by this array of owners.
No new fields were added to the mix; the additional index refers to the buffer.
The GPU-limites are saved, and the array capacity is limited by the number of cache records.

O2 was completed and ASan/UBSan. case of 80 independent was added
with the buffers moved in the LRU order, partial transfer of ownership,
An inventory and refilling of the array with living old nal viewes.
The exact bytes of the actual M2 are stored.
Negative control without updating the displaced buffer index
is compiled and falls on the owner &apos; s index compliance check.

216 Comparative CPU launches: 512/2048/4096 small indexed bags,
1/16 per buffer, both consumers, c/ without general resources,
Nine alternate pairs in case. Fake D3D and C++ allocations are used.
For 4096 individual buffers without shifting the median of the creation 3167,4 1955,6mx,
Peeling 3945,0 ~ 690,4mx. with removal of cleaning 2913,8 465,6mx.
Number of C++-allocations in creation 32769 \ 28682: delta 4087 corresponds to
The growth of the array 4096 of 9 excretions. No new all locations during cleaning.
Grouping of 16 checked separately; these numbers are not game FPS
Evidence: `out/cpu-native-batch-slots-20261105/`
(`comparison.json`, `negative-control.json`, `current/validation.json`).
Edits remain in the source; production DLL and installer `.11` were not re-assembly.

## Continuation of CPU: Automatic release of location upon seizure

`NativeStore::room` Repeated head search LRU for each displacement at
A long prefiguring of the other user's records is again.
It's been going through the same records multiple times, already inside the new one.
Now this path uses the same scoped search as `trim`.
The removal of the neighbours is moving forward; the set-up restores
The external search position on the live LRU is not created without pressure on the cache.
The way to limit the number of entries continues to be a global preference for probation;
Protection of the current frame, order of permissible victims and all limits retained.
There are no new fields for the mix, memory and GPU resources.

O2 and ASan/UBSan passed. New control checks failure in absence
permissible victims of the current frame, repeat in the next frame, exact choice of two
The oldest permissible fur, the immutability of someone else's prefix and byte of a new mess.
Negative control without protection of the current frame is compiled and dropped on
Mandatory refusal verification; protection option passed.

216 steam CPU launch: 512/2048/4096 resident mix, 1/16 mesh on the buffer,
0/3072 probation records of another consumer at the beginning of LRU, both users,
9 pairs of alternate in case. Requesting a place to mix around 2 MIB.
with the release of about 3/4 old workstation.
Deleted from the timeting; after it checks the exact bytes, the victim selection,
Number of offsets/checks/creates and memory records. All fields match base.
At 4096 individual mites and préfixes 3072 midway clearance 26981,5 579,5mx;
In a grouping of 16 to 1998,6 × 285,4mx. Without any other precursor, the winnings are small.
This is an isolated CPU test.
With a Fake D3D, not a game FPS or GPU-time.
Evidence: `out/cpu-native-room-20261105/{comparison.json,negative-control.json}`
and `out/cpu-native-room-20261105/current/validation.json`.
The DLL and the installer were not re-assemblyed after `.11`.

## CPU continuation: Filled cache with current frame protection

If all records are already used in the current frame, `room` must refuse
Every next new mess I've ever seen has been a complete round-up.
LRU to prove the same failure.
The records used are the LRU suffix: the current number of the oldest
The element proves the absence of a permissible victim in the whole table.
Now, this case refuses for a constant period of time, keeping statistics.
The challenges that allow the current frame to be removed remain.
The previous path. Rewinding/replicating the counter shuts down the evidence;
There's no new fields on the mess and no allocations.

Established O2 and ASan/UBSan passed with accurate M2 bytes.
Rewinding and 32- overcrowded frame number: later entry
remains an acceptable victim, although the head of LRU has the current number again.
Precise identity hits and post-clarAll security checks.
without checking the order of personnel compiled, but mistakenly refused.
Allowable displacement; the regular version passes.

144 Comparative CPU launch: 512/2048/8192 records, bytes or numbers
Records, one/two owners, nine alternate pairs in case.
A series of 256 failures for new bags in one frame; in the next frame
Check the normal tolerance.
The GPU resources are not created at 8192 records and byte limits.
The median of the 13808,2 series is 19,5mx; with two owners 13998,5 19,6mx.
It's a load CPU failure control, not a game FPS or GPU-time.
Evidence: `out/cpu-native-protected-20261105/{comparison.json,negative-control.json}`
and `out/cpu-native-protected-20261105/current/validation.json`.
Edits are only in the source; DLL and Installer `.11` have not yet been reassembled.

## Separate DLL and game run 18:03 UTC

On request, the production DLL is collected by the regular `python scripts/build_renderer.py`
All compiler/shader
freshness/layout gates have passed.
`out/cpu-dll-followup-20261105/d3d9.dll`, 10105344 Byte, SHA256
`3861b4c1cbe396d583513efc8d0cdeb1b2b1bcd0a397bf30eb84b21de9b2ca5ea`.
Source identity `32110bc36c1ede9d94f3a495a0c5bf0f5734a1fb3fc609c431a0b7667cdf0d13`,
Build `2026-11-05 17:50:28 UTC`; the exact reference points are retained in `baseline/src`.
Receipt: `out/cpu-dll-followup-20261105/validation.json`.

The new client log ends 18:03:02 UTC, frame3446, and contains exactly
This identity is an unchangeable copy and reproducible analysis:
`out/cpu-game-followup-20261105-1803/{renderer.log,snapshot.json,analyze.py,analysis.json}`.
Many on/off are selected on `DRAWGATE applied` in the same measured frame;
The screen of character selection is excluded from sceneDraws. 9 on and
2 of samples; late group after frame2413 - 7 on samples, of which only
3 with full CPU timers, offloading Capture2843 and Shutdown3446 are deleted.

| CPU phase, late on samples | Median, ms |
|---|---:|
| draw gate | 0,628 |
| capture | 3,116 |
| effects | 1,355 |
| cleanup | 0,768 |
| Total (not the sum of independent mid-terms) | 5,863 |

It's a sampled T frame with tooling, not the net value of each.
Intervals are embedded in the rest of the pipeline: they cannot be repeated.
Add to sceneMs or GPU timestamp totals.
the number of scene draws (2365–2388) sceneMs at on is 15,21–16,18 ms,
at f in frame3048 - 3,229 ms. The late shift intervals F10 show
52–56 FPS on versus 120–121 FPS off, but this is an uncontrolled surveillance.
A comparison of the same camera. To approve a new FPS win relative to
You can't run the previous DLL on this run.

GPU timestamp GameScene in the late band has a median of 12,216 ms, Effects —
3,716 ms, separately assigned MaterialCapture - 0,102 ms. GameScene includes
Interlived Northlight Capture and waiting for commands, not just a regular
GPU's game or clean employment. In the frame3302 release total27,639ms is related to
tal10,995 ms and CPU final9,960 ms; this is not proof of an expensive sheader.

The following CPU measurement directions:

1. Re-adaptation/maintenance and model verification.
   In the full set on samples of the mid-point D3D calls 28836, GetRender State7136,
   SetRenderTarget3916 and GetRenderTarget2441; mirrorAnswered103709
   MirrorForwarded6264. These are counters that do not measure the time of each method.
   Domain4 separates CaptureSeup0,337 ms, admission0,248, poseEnvelope0,245
   Snapshot0,232 and bowsPose0,193 (exclusive CPU medians, 9 samples).
   NativeDraw0,537ms is a reference draw game that cannot be declared removed
   Overhead. Reduced need for repeat evidence and state transitions,
   Keep the current pos and order draw.
2. Clearup0,768ms is the next significant CPU Interval; you need to distinguish between
   The log does not per se provide
   evidence that all of this time belongs to NativeStore.
3. Stream upload copies are a smaller share of CPU time. The median copy took 0.0955 ms,
   Lock took 0.0462 ms, and Unlock took 0.1247 ms, with about 1.88 MB across 334 calls.
   Synchronous GetData waits can have causes beyond copying.

In the last two sampled frames, material already has 60 reidentDraws,
UpDraws0, uploadedBytes0, creation0. Therefore, the following changes to Cold-entry
You shouldn't be allowed to take out the main cost of a warm scene.

## Continuation of CPU after DLL: recording of cache in table node

`NativeStore` has previously selected a separate `Entry` via unique_ptr and then the node
Unordered_multimap. Now Entry is placed directly in the node of the table:
minus one sheap allocation and one entry. Node address retained
at rehash; Entry prohibits copying.
is checked before the node is created, so failure of the protected full cache is greater
It doesn't create a temporary Entry.
The GPU download has not changed; the conservative old metada charge is saved.

Established O2 and ASan/UBSan passed, including new stress: pending links before
hash, 2048 foreign boxes, batch/LRU links, tracked/untracked sources,
both consumer, sparse unership transfer, refill and held views with precision
Bytes. Recept `out/cpu-native-entry-20261105/current/validation.json`.
Additional check is made for failure of allocation node after displacement: previous number
victims, accurate budget, successful retrie and identity hits, no GPU calls.
Remote preliminary allocation can no longer refuse to allow;
It's a deliberate change in the order of allocation failures, not a promise of the past.
The state of each possible OOM point.

108 Comparative CPU launches (9 alternate pairs, 512/2048/8192 records,
One/two consumer, Fake D3D):

| Load | Before | After |
|---|---:|---:|
| allocations, 8192 records | 16397 | 8205 |
| allocations, 256 protected failures | 256 | 0 |
| Registration 8192, one consumer, mid-mix | 1415,0 | 1280,2 |
| Registration 8192, two consumer, mid-mix | 1454,0 | 1227,7 |
| clear8192, one consumer, mid-way max | 715,7 | 629,6 |

At 512 entries, clear was slightly slower for one consumer (26.4 → 30.8 µs).
At 2048 and 8192 entries, clear was faster. The cases, counters, and commands are in
`out/cpu-native-entry-20261105/comparison.json`. The benchmark isolates CPU metadata.
benchmark, not game FPS or GPU time. Production DLL after this change
I wasn't, I wasn't.

## CPU continuation: Do not save pixel replay for rejected draw

The next passage is aimed at meassured captureSetup/admission. earlier
`NorthlightAutoBump::capture` Read 12 states replay prior to range verification
Skinned geometry, now read ALPHATEST/ALPHABLEND and SRC/DEST
Only when the native blending is activated. The remaining values are only needed before
Direct replay. If they're successful recording deferred packet, they're all
Not read: packet stores the desired replay values, and flush stores native
Fallback saves the right ones after failure.
Immediate replay values, just like the normal immediate path.

In the opaque passageway with ALPHABLENDENABLE=FALSE seven blind factors/operations
they don't read, change or recover: they don't affect this draw.
The very native blind enable is still recovering.
[Alpha Blending State](https://learn.microsoft.com/en-us/windows/win32/direct3d9/alpha-blending-state).
The recovery of all states has been checked on this device, including:
Saved by non-standard sampler/constant/stream bandings.

In `ReplayState`, the initial entry of zeros in 36 float constants and two
8-DWORD sampler banks. Successful `save()` completes all consumed
the values before the device is changed; when they are valid, they are not read.
Sampler keys is now static constantpr.
The range, proof, geometry,
The order draw, filtering and chader aritmetic did not change.

Checks:

- A complete displacement/material CPU oracle passed O2 and ASan/UBSan. New
  Checks the absence of replay-only quaeries at long distance skinned draw
  c 1x/2x MASA and resize; the previous heading fails.
  `out/cpu-admission-split-20261105/validation.json`.
- 275 GPU cases on each branch, DXVK3, actual stock VS/PS terrain/wall/grass
  POM, alpha layers, cavity and matrial shadows. All 276 saved flat4
  readbacks/material surfaces, cumulative 289406976 byte, coincided by bit.
  The Native state restoration and 6 POM call-account controls have also passed.
  `out/cpu-admission-split-20261105/gpu/validation.json`.
- 216 Comparative CPU launches, 9 alternate pairs: opaque/cutout
  Direct backend/warm mirror, 0/80/100% distant draw. Test uses production
  Capture and native submission with autohored 3-vertex skinned source.
  parts of PS constants deliberately refuse to GPU replay; its time
  Doesn't represent all successful win draw. Budget lock is determinated,
  So the OS planner doesn't change the set of rejected draw; elapped is measured
  This is the real steadi_lock. `out/cpu-admission-split-20261105/comparison.json`.

On distant opaque draws, admission and replay render-state reads fell from
19 to 9, including the unchanged visibility and depth checks. For near opaque
draws in the immediate path, they fell from 19 to 12. For distant cutouts, reads fell
from 19 to 11. Near cutouts required the same number of reads. With a warm mirror,
this reduces extraction and copying of known values. Forwarded calls may remain the same.

With a warm mirror, the median CPU time for 200 draws with 80% distant opaque draws fell
from 227.6 to 222.3 µs (about 2.3%). With all distant opaque draws, it fell from 229.6 to 223.4 µs.
Near cutouts with a warm mirror were effectively unchanged at 257.1 → 257.4 µs.
The CPU time improvement is small. Halving the counters does not imply
Two accelerations, initial version with dynamic bypass
The state mask showed regressions and was replaced by short fixed lists.
You can't take a new game FPS out of these tests. DLL/installer wasn't going to be.
