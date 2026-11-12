# r87: released 0.3.216 versus the current PR

## What was compared

Release `v0.3.216`, tag commit
`366194414331b2cd9cc9750b1f7fd24c93b5291e`, versus the complete PR sources
at r86 (`400a0280d3f8c734ddf0a5ca8ed685532b44973c`) and the point-light
submission change below. This is the published release, not current main.

The Windows ZIP was downloaded from the official release. Its SHA-256 is
`5a7189f20d340c81ff959bde9ad405967848917212c4ee1b566590558a8e4ec2`, matching
the release page. The extracted 4,338,176-byte DLL has SHA-256
`7cfd5dead435da8c5a4d8032e6ce7a039914de23893925cbf00bee388c0b75d6`.
Thirteen of the comparison's release shader binaries were checked
byte-for-byte inside that published DLL.

For stage access, the test compiles the unchanged release-tag sources and
the complete current sources into isolated executables, with test friendship
and optional profiler hooks. The official DLL itself was not executed inside
the game. The common configuration, camera bank, scene/depth capture,
authored casters and light inputs are shared. The release has no HDR or new
material-capture implementation to enable. No client installation or game
launch/stop is performed.

**This is not a complete client-frame replay or an all-features gameplay
measurement.** The expanded graph runs actual world/AO, HDR output and
rain/snow/sand optics together. Native draw recording, AutoBump material
generation, animation/pose capture, native water capture, scene streaming,
UI and Present are outside that graph. Model-normal capture is additionally
profiled in a separate stock-asset fixture. Those separate measurements
must not be added to manufacture a total frame cost.

## Method

RTX 4090, DXVK 3, 3840x2160 captured scene/depth. Four common configurations;
the busiest has GI, directional/one selected point shadow, eight local lights
and fog. Its GI cache is prefilled; live probe updates and moving actor
capture are not timed. Three ABBA rounds, six process samples per side, 48 frames per
process with the first 16 excluded. Primary totals have no internal stage
queries. Separate intrusive profiles record QPC and production timestamp
boundaries; those GPU intervals also include command-supply gaps.

The shader math and filtering of the accumulated PR differ from 0.3.216;
release/PR images are not asserted identical. The before/after optimization
checks require identical current-branch images. HDR/rain/snow/sand are
explicit separate configurations; mutually exclusive weather is never
combined into an impossible scene.

## Implemented improvement

The older one-light implementation has less point-light submission work.
Our implementation supports many independent corrections, material normals,
selection fades and diagnostic capture. Restoring the old fusion of local
lights into temporal directional lighting would remove that separation.

Instead, consecutive point-light corrections now establish their shared
textures, sampler states and output state once. Every light still uploads
its own complete c0-c11 bank (including selection gain), binds its own cube,
selects its shader, scissors and draws independently. The flag is local to
one uninterrupted loop; no persistent state cache is added. Standalone
calls and diagnostic capture retain complete setup, and the surrounding
WorldRenderer scope still restores caller state.

The shared flag is set only after every common binding succeeds, including
the material constant, texture and sampler calls that were formerly
unchecked. A failed setup skips that source and the next source retries it.

Sampler-setting calls become 41 per batch, from 41 per light. A same-executable
standalone/shared comparison passes 70 bit-exact cases on each of native
D3D9 and DXVK 2/3: 1/4/16/64 lights, ordinary/PCSS filtering, per-light colors
and fades, water, material normals and first-source setup failures. Shadow schedule/face-cycle O2 and
ASan/UBSan, capture-skip wiring and shadow-removal checks also pass.

The base-capture profiler's vertex lookup marker was brought up to date
with the existing compact-vertex API. This changes instrumentation only.

## Limits and cold work

The first frame remains much more expensive than the warmed graph in both
versions. This harness deliberately includes resource/shader initialization
inside that first interval. Separate cold-stage records identify WorldUpload
(which includes resource initialization) and the first SunNear draw as the
main spikes. These are first-process observations with existing driver
caches, not a controlled cold-cache compilation benchmark or a reproduced
castle-entry hitch.

The new point-shadow CPU stage is still more expensive than the old
single-cube path. It retains multiple-source selection, static-content
tracking, native-fixture ownership and conservative replay bounds. This pass
does not remove those correctness features to match an individual old stage.
The warmed complete common graph is the appropriate shared-work comparison.

The 2-3 ms total gameplay budget remains unconfirmed. Dense fullscreen PCSS
corrections alone can exceed 2 ms at 64 lights in the stress fixture; that
case intentionally defeats normal light scissoring. No global speed or
stutter-free guarantee follows from these stand measurements.

## Results

| Common mode | CPU release → PR, ms | GPU release → PR, ms | GPU change |
| --- | --- | --- | --- |
| 0 | 0.022475 → 0.014275 | 0.712448 → 0.543744 | -23.7% |
| 1 | 0.029800 → 0.024750 | 1.126880 → 0.734720 | -34.8% |
| 2 | 0.048700 → 0.035650 | 1.382312 → 1.013504 | -26.7% |
| 3 | 0.034275 → 0.026200 | 1.260024 → 0.894192 | -29.0% |

Mode 0 has AO/world lighting, mode 1 adds fog, mode 2 has GI/shadows/local
lights/fog, and mode 3 keeps GI/local lights/fog with shadows off. All PR changes
are present in these binaries. CPU numbers describe this submission fixture
and exclude game-side capture and simulation.

The complete-graph CPU intervals are short enough to vary with host
scheduling. The same-executable point-batch results give stronger evidence
for that specific CPU change than cross-process submission medians.

| Additional PR passes on mode 2 | CPU, ms | GPU, ms | Draws | Copies |
| --- | ---: | ---: | ---: | ---: |
| HDR | 0.086375 | 1.193216 | 31 | 6 |
| Rain + lens (SDR) | 0.098975 | 1.134592 | 26 | 6 |
| HDR + rain/lens | 0.127250 | 1.358592 | 33 | 7 |
| HDR wide bloom + rain/lens | 0.138000 | 1.375488 | 34 | 7 |
| HDR + snow/frost | 0.139250 | 1.453824 | 34 | 7 |
| HDR + sand optics | 0.073125 | 1.205248 | 33 | 6 |

Both common and expanded graphs are bit-exact against r86 in all 120
before/after readbacks. Their one-point-light workload does not demonstrate
the multi-light batch saving; the isolated same-executable comparison below
does. CPU deltas between these very short process-level samples can be noisy.

### Stage breakdown

Separate intrusive mode-2 profiles, medians across four process samples per
side. Fog is grouped across its differently split march/filter/temporal
labels. These are not additive CPU/GPU totals.

| Stage group | CPU release → PR, ms | GPU release → PR, ms |
| --- | --- | --- |
| AO | 0.007375 → 0.007750 | 0.186248 → 0.185344 |
| AO blur | 0.001275 → 0.001400 | 0.100352 → 0.069632 |
| Upload | 0.004825 → 0.003400 | 0.000000 → 0.000000 |
| Directional shadow | 0.008950 → 0.009325 | 0.010240 → 0.010240 |
| Point shadow | 0.010750 → 0.022650 | 0.036864 → 0.028160 |
| Normals | 0.013450 → 0.004900 | 0.049408 → 0.047104 |
| World lighting | 0.001325 → 0.001625 | 0.084992 → 0.030720 |
| GI | 0.000800 → 0.000900 | 0.068608 → 0.060416 |
| Local lights | 0.000700 → 0.002075 | 0.013312 → 0.021504 |
| Point correction | 0.001400 → 0.002350 | 0.014336 → 0.020480 |
| Removal/temporal | 0.002150 → 0.001975 | 0.078848 → 0.068352 |
| Fog | 0.004975 → 0.005625 | 0.414720 → 0.251904 |
| Composite | 0.004550 → 0.001400 | 0.319488 → 0.228352 |

The release wins individual local/point stages, while the PR saves more
in fog, normals/lighting setup, temporal/composite work and AO blur. A local
light now stays outside temporal directional history; the additional target
and correction behavior are retained. Point-shadow CPU work remains a
known small regression in this fixture.

Expanded HDR+rain stage profile (median of two processes):

| Added boundary | CPU, ms | GPU elapsed, ms |
| --- | ---: | ---: |
| HDRPrepare | 0.010300 | 0.000000 |
| Precipitation | 0.006400 | 0.082944 |
| WeatherLens | 0.072375 | 0.090112 |
| HDRPost | 0.015900 | 0.176128 |

### Point submission change

Same executable, DXVK 3, 1920x1080 input/fullscreen light rectangles, six
samples per side after four warm-up ABBA blocks. Each block averages 16
batches. GPU shader work is unchanged; the CPU interval includes a counting
wrapper. Ordinary/PCSS corrections remain separate draws.

| Filter | Lights | CPU standalone → shared, ms | GPU standalone → shared, ms | Sampler sets |
| --- | ---: | --- | --- | --- |
| Ordinary | 1 | 0.000400 → 0.000428 | 0.012800 → 0.012960 | 41 → 41 |
| Ordinary | 4 | 0.001694 → 0.001038 | 0.034816 → 0.035072 | 164 → 41 |
| Ordinary | 16 | 0.006569 → 0.003519 | 0.119232 → 0.119232 | 656 → 41 |
| Ordinary | 64 | 0.026250 → 0.012566 | 0.539360 → 0.513344 | 2624 → 41 |
| PCSS | 1 | 0.000372 → 0.000384 | 0.040000 → 0.040256 | 41 → 41 |
| PCSS | 4 | 0.001719 → 0.001019 | 0.136800 → 0.136064 | 164 → 41 |
| PCSS | 16 | 0.006566 → 0.003294 | 0.592768 → 0.611168 | 656 → 41 |
| PCSS | 64 | 0.026362 → 0.012391 | 2.388672 → 2.374048 | 2624 → 41 |

### Model-normal capture

A separate 4K/MSAA2 stock streetlamp fixture submits 133 near and 310 far
draws through the production state mirror/source pool/GPU geometry cache.
It excludes AutoBump and pose/source handoff. The lower-overhead mode still
uses per-draw timers; the phase-instrumented mode is slower and is not used
as the headline cost. Native draw submission is accounted separately.

| Component | Median, ms | p95, ms |
| --- | ---: | ---: |
| captureMs | 0.199400 | 0.260600 |
| flushCpuMs | 0.041500 | 0.059700 |
| replayGpuMs | 0.033280 | 0.035840 |
| nativeSubmitMs | 0.051300 | 0.059700 |
| totalCpuMs | 0.342200 | 0.441000 |

310 far draws are rejected; 133 use cached geometry and warm source readback
is zero bytes. This measures a new feature absent from 0.3.216, not a
speedup over an equivalent old capture path.

### First-frame observations

| Mode-2 first frame | CPU release → PR, ms | GPU release → PR, ms |
| --- | --- | --- |
| Resource/shader startup, existing driver caches | 24.987 → 22.916 | 10.747 → 9.665 |

These warm-up observations remain outside the steady-state results. They
show why the sub-2 ms warmed graph must not be reported as a lag-free client.

## Reproduction

```powershell
python tests/benchmark_upstream_pipeline.py --upstream $releaseTagCheckout --baseline $r86Checkout --capture $privateCapture --backend $dxvk3 --output $newGraphOutput --rounds 3 --profile-stages --branch-features
python tests/benchmark_point_lighting_batch.py --backend $native --backend $dxvk2 --backend $dxvk3 --output $newPointOutput --benchmark --allow-game-process
python tests/profile_base_capture.py --backend $dxvk3 --output $newCaptureOutput
python tests/test_shadow_removal_smoothing.py
python tests/test_capture_skip_wiring.py
# Under WSL/clang++:
python3 tests/test_point_shadow_schedule.py
```

The configured running-game allowance was retained. No WoW process was found
in the recorded local process check. GPU measurement workloads and CPU builds
were serialized. Reused upstream/r86 executables were accepted only after
source-identity, harness/backend and executable-hash checks; all final timing
runs are new. Optional CPU/GPU stage instrumentation is excluded from primary
unprofiled totals. All 191 precompiled shader binaries are unchanged.

Receipts are ignored under `out/optimization-20261110-r87/`: release audit,
`final-verified-comparison/`, `comparison-verification.json`, `point-final/`,
`base-capture-final/`, schedule/wiring/removal logs, shader retention,
`artifacts.json`, package verification and PR verification. Preliminary
comparisons and the unused intermediate package are not final artifacts.

## Production artifacts

Measured/production source identity: `bf0861de30b3ee2f061669a5e1cffda53665f8aed87de4edb261c6395460acfa`.
Build time: `2026-11-10 08:05:41 UTC`. Zig 0.15.2 compiler preflight, shader
freshness, x86 PE/17 exports, exact installer DLL payload and release-package
verification pass. Six existing compiler warnings remain. Client files
were not installed or modified.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `releases/Northlight-0.3.217-test.20261110.13/d3d9.dll` | 10,853,376 | `f3b5357fadf8154cde138e1c4964732f8e992cc0830155ad469bb5f2bfc5663d` |
| `releases/Northlight-0.3.217-test.20261110.13-Windows.zip` | 20,820,523 | `c7253e888b039dd0df169f941bcecf24f967dc23dcbee989a7336abb4844b4f8` |
