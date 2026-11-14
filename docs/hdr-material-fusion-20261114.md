# Expanded-HDR material fusion, 14 November 2026

The loaded DXVK **v3.1.1** runtime may now combine WorldCompositeHdr and material
detail when legacy highlight expansion is enabled. It keeps their intermediate
color in registers, omitting one full-resolution FP16 write/read and one draw.
The shader already exists; shader bytecode, asset data, MSAA, resolution, effect
sampling and user settings are unchanged. The computation avoids one intermediate
FP16 rounding, but final images are not claimed to be byte-identical.

The former guard applied a measured DXVK 2 regression to every backend. That
regression still reproduces: at 4K/MSAA2, the captured-input composition graph
costs 0.745 ms separate versus 1.048 ms merged on DXVK 2.7.1. Therefore expanded
fusion is enabled only from the actual loaded DXVK v3.1.1 identity, never merely
from Backend=dxvk or a filename. Unknown versions, native D3D9 and load fallback
keep their previous policy. Existing zero-expansion policy remains unchanged.

## Evidence

Latest gameplay log remains the run ending 02:20:51. It reports median 0.4805 ms
WorldComposite and 0.1255 ms MaterialDetail, but those samples precede this change.
There is no matched live FPS result for the new DLL yet.

Local evidence root: `.local/out/test-output/hdr-fusion-20261114/`.
The improved manual probe consumes original scene/depth/camera/normal/distance/
material-height captures from `session-60800-280247484-0`. Lighting, fog and native
draw order are authored. Both variants execute the same HDR graph mode, expansion,
auto exposure, wide bloom, particles and UI. This is a partial-input graph test,
not a replay of all gameplay work. GPU timings can include CPU submission gaps.

RTX 4090, packaged DXVK 3.1.1, RDP, warmed ABBA blocks:

| Input | Separate GPU ms | Fused GPU ms | Difference |
|---|---:|---:|---:|
| Authored 4K | 0.562 | 0.485 | -0.077 ms |
| Authored 4K/MSAA2 | 0.571 | 0.522 | -0.049 ms |
| Captured 4K/MSAA2 | 0.727 | 0.629 | -0.099 ms |
| Expansion 2 EV, 1080p/MSAA4 | 0.177 | 0.165 | -0.012 ms |

`expanded-matrix/report.json` records these measurements. The earlier independent
`initial/` run gives 0.715 -> 0.622 ms on captured inputs. Identical A/A graphs in
`aa-control/` give 0.725 -> 0.724 ms there: the roughly 13% graph reduction exceeds
the adjacent-control variation. CPU submission is about 0.0554 -> 0.0538 ms.
This is not a claim of 13% higher game FPS.

All six probe cases pass finite-output, exact-alpha and RGB tolerance
`0.003 + 0.002*abs(reference)` checks. The maximum absolute HDR-channel difference
is 0.0078125; no channel exceeds the tolerance. A material-off negative control
changes the output, so the saving does not come from deleting material detail.
The cases include odd 129x97 dimensions, sky/name stencil, 2 EV expansion,
1080p/4K, and MSAA0/2/4. The capture and normal/material scene inputs remain local.

## Runtime and regression contracts

- Device caps and optional shader creation still gate fusion. Unsupported caps
  and failed creation use the existing separate shaders.
- Diagnostic captures retain the original passes and input groups.
- Name, self-lit and prelit WMO masks retain their separate equations. Material
  detail must not affect names, self-lit or baked/prelit receivers.
- A failing draw restores native device state and uses the existing WorldRenderer
  failure latch. Recovery is explicit; the test must not assume automatic retry.
- The fused route does not allocate the full-size world/material scratch target
  on a fresh renderer. Previously allocated resources remain warm under the
  existing reset/pressure policy.
- `BACKEND_WORK expandedHdrMaterial=1` records loader eligibility. Actual frame
  use is recorded as `WORLD composite ... combined=1`; material work then belongs
  to WorldComposite, not an additional MaterialDetail draw.

`test_backend_work_policy.py` compiles the actual loader assignment and call-site
policy for versions, load failures and fallback combinations. The existing
`world_screen_effects_gpu.cpp` oracle now compares expanded HDR on the production
WorldRenderer, alongside masks, caps, creation failure, diagnostic fallback,
draw-failure restoration and explicit recovery at MSAA0/2. Its runner supports
explicit process-local RDP adapter overrides and records them.

Reproduce the graph with `scripts/probe_hdr_material_merge.py --backend <dll>
--capture-prefix <capture-1-> --output <new-directory>`; add `--control` for A/A.
Use the same process-local RDP variables documented in the MSAA investigation.
Run source/architecture suites and the production world-screen GPU test on both
packaged backends before installing; build with `scripts/build_renderer.py`.

## Remaining priorities

The largest stable effect remains RemovalSmooth (about 0.658 ms in the last log).
Its earlier loop/branch rearrangements did not beat A/A noise and remain rejected.
Changing its algorithm must preserve silhouettes, darkening limits and temporal
stability; reducing taps or disabling it is not an accepted optimization.
The rejected MSAA-normal fusion also remains separate. The next game profile
should confirm `combined=1` and compare WorldComposite + MaterialDetail together,
then attribute remaining full-resolution HDR traffic and CPU capture costs.
