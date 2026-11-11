# Aurora / GI integration handoff

Worktree: `D:/dev/Projects/Northlight/.local/aurora-gi-20261111`.
Branch: `codex/aurora-gi-20261111`; base `6b87cafa`; initial GI commit `54baeabf`.
Destination chat: local `01a1c14a-bb92-79d3-adfd-adeb63952299`.

The user selected option B: native ribbons with shared additional visible
emission and directional GI. The previous uniform ambient tint is superseded.
See `aurora-gi-spec-20261111.md` for the current contract. Work is isolated from
the main checkout's ongoing stutter optimizations. Final live acceptance is
still pending; do not describe the older offline previews as game screenshots.

## Final behavior

- GIStrength 0..240, default 120; GIBrightness -100..200, default 0. Signed
  brightness scales measured GI, with the existing residency and darkening
  safeguards. Explicit old settings retain their numeric response.
- AuroraStrength 0..200, default 100, controls the whole native and twilight
  emission together with its GI. 0 disables the aurora, 100 is normal, 200 doubles
  source radiance. Located in Weather. Surface illumination requires GI.
- Actual capture: seven draws, two embedded skybox emitter families. Curated
  signatures plus exact native shader/declaration semantics bind the assets.
  Missing/ambiguous evidence retains native material without invented light.
- Added emission is twilight-gated by the same renderer celestial clock. It is
  projected to 32 world sectors. The optional transport sidecar stays inside
  the existing 4 MiB relight-cache budget; ordinary cache layout is unchanged.
- Complete native sky is decoded once into the existing FP16 buffer; separate
  linear emission retains aurora and celestial headroom. Ordinary sky/cloud
  layers attenuate that extra emission, including screen/multiplicative blends.
  This supersedes the first narrow material calibration, which the second live
  capture showed was insufficient for hue and horizon brightness. No additional
  full-size render target is allocated; additive native sky replay is skipped.
- Visible geometry/UV/alpha stays native. Source integration observes at most
  8 Hz, uses a coarse CPU mip and runs geometry/projection on one worker.
  Offscreen sources retain the last complete observation until selection/reset;
  offscreen native animation is not extrapolated. This is diffuse transport,
  not a pixel-exact integration of full-resolution anisotropic textures.

## Integration

Cherry-pick both the initial GI change and the B follow-up. Preserve newer
main-branch renderer and GI optimizations during conflict resolution.

`world_renderer.h` now accepts immutable `Lighting::aurora` snapshots; do not
restore the old uniform `skyRadiance` addition. `world_gi.cpp` records angular
escape responses, and the cache/dynamic fallback keys account for the source.
The GI shader formula and c66.y ownership from the initial commit are unchanged.

`renderer.cpp` performs strict sky-material admission. SDR scales the native material and adds E. HDR scales its SDR baseline without
E, then adds the native calibrated emissive excess and E through the dedicated
replay. `hdr_sky_residual.h` transports
foreground transmission. The production `SkyForeground` instance uses residual
MRT mode; its default mode remains available for existing callers/tests.
`Frame::beginSkyResidual` resets ownership at sky initialization, and
`prepareLegacy` adds the complete native sky exactly once, including inline and
MSAA paths. Early celestial policy 4/5 means sun/moon residual; late policy 2/3
still means full HDR. Preserve all of these changes together during integration.
The CPU texture mip cache remains enabled independently of EquipmentLights.

Recompile destination-modified world shaders from the combined source; do not
replace newer optimized bytecode wholesale. Aurora shaders have their own
manifest/compiler gates. Build via `python scripts/build_renderer.py` and retain
normal shader freshness, Zig 0.15.2 and package checks.

## Evidence and reproduction

Original capture: `D:/Games/335_engb_mod/northlight-diagnostics/session-18884-52095218-0`.
Copied material evidence: worktree `out/aurora-b-captures/`.
`out/aurora-hdr-repro/validation.json` proves the generic-transfer contrast loss
on native D3D9, DXVK 2 and DXVK 3 using the captured shader bytecode.

`tests/test_aurora_native_capture_gpu.py --capture <copied-session-folder>`
requires the captured native frame and the same local game assets. It validates
all seven draws/two families, unknown-draw rejection, camera rotation, native UV
animation, incomplete subsets, in-flight reset, shader/constants restoration,
and 96 CPU/GPU color/alpha/headroom cases per backend.

New HDR checks: `test_hdr_sky_residual_gpu.py` (18 cases/backend),
`test_hdr_sky_replay_gpu.py` (432 cases/backend), `verify_hdr_frame_gpu.py`
(2,893 cases/backend) and the six celestial composition families.

Other checks: `test_aurora_source.py`, `test_aurora_transport.py`,
`test_aurora_illumination.py`, `test_settings_overlay.py`,
`test_gi_settings_dependencies.py`, plus existing GI/cache/dynamic/moving tests.
Initial shader/settings/composition results remain in
`aurora-gi-validation-20261111.json`; they describe the initial commit only.
Current B results and remaining runtime limits are recorded separately.

The temporary native-capture DLL was backed up and restored after capture.
Every later installation must retain its own prior DLL/hash; another chat owns
current production rollout, version, push and PR integration.

## Previous complete-sky build and runtime window

Production source identity: `5602770c92821723feebb874eb30a30b38629d885b20a4731c9bd50826464093`.
DLL SHA-256: `27632f5f52da7d897742ca80c66c50ae2b05a03c7087544308f83f6602206324`.
`aurora-b-validation-20261111.json` records that earlier B/HDR stage.
`aurora-native-hdr-validation-20261111.json` records the final whole-source stage.
The isolated Windows package passed normal package verification.

The previous narrow-calibration build (`ca5e6069...`) was tested live, then
restored. Its daylight capture proves seven native HDR draws per frame but
shows pale ribbons and a dim horizon; it does not validate the current fix.
At 09:55 UTC the current DLL was temporarily installed for the next capture;
backup and exact hashes are in `out/aurora-b-hdr-sky/install.json`.
Restore only if the installed DLL still matches this build and the client is
closed. Keep the main chat informed when this temporary window ends.

## Native emitter HDR / legacy expansion follow-up

The user's next two captures (`session-51868-57644875-0` and
`session-51868-57648843-0`) confirmed native sky composition but exposed a
remaining omission: native aurora below-one pixels supplied no HDR excess,
and the existing `HDRLegacyExpansion=0.50` did not reach their shader.
The follow-up calibrates native additive masks as HDR emission and passes the
live EV through AuroraEmission.w, independently of AuroraStrength and the
sunset gate. The complete-sky baseline and shared twilight E remain separate;
legacy expansion changes the native display emission, not transported E.

The previous DLL was restored after these captures; hashes/restore record are
in `out/aurora-b-hdr-sky/restore.json`. The new build/commit and installation
record are reported in the native HDR validation supplement. Preserve this
follow-up when integrating b02d24fc. Do not claim live visual acceptance from
its coarse-mip scene replay.

## User-approved whole-source intensity and Weather

AuroraStrength now scales the entire native source, its visible SDR/HDR
emission and native RGB GI, plus the existing twilight E. 0 hides the aurora
and removes its GI; 100 is normal; 200 doubles source radiance. Native light
is present outside the twilight interval. The worker projects native RGB and
the twilight basis together in one six-channel traversal, then publishes the
same 32-sector RGB snapshot used by existing GI transport/cache code.

The native SDR draw also receives the common intensity, including HDR mode;
the HDR residual subtracts that scaled baseline. Runtime control uses two
contiguous PS constants, both saved/restored. Keep shader header/manifest and
the patcher/renderer update together. The slider is now in Weather and remains
independent of the enhanced precipitation and fog toggles.

Final whole-source production identity: `e95e2b9ae3a55d9bd567460c3c9baffc571ed028f9db7b2436631a888846b6c4`.
DLL SHA-256: `5908988d42d510b65d5521198a04e915244baa7945498c4ac392212a725a3d65`.

## PR integration

Use the final integration HEAD from PR #98 for future builds. It preserves
the newer captureGameDraw route; see `aurora-pr-integration-20261111.md` for
combined-tree validation, build identity and the conflict resolution.
