# Renderer capture, shadow stability and DXVK compatibility follow-up

This publishes the accumulated renderer work after `759f1efc`, through the locally tested r42 source. The crowd report for PR #98 describes 60–70 FPS on the prior version and 17 FPS on the integration. That regression has not been reproduced on the reporting Mac. The current change removes a confirmed backend-dependent batching restriction and includes the preceding CPU, HDR, geometry and lamp corrections.

## Capture on DXVK 1/2 and the legacy backend

`ActorShadows` already exists in upstream main: it is present in the first public 0.3.166 snapshot. The dedicated `ActorShadowDetail` map was added upstream in October for 0.3.209. The integration adds substantial adjacent work: native post-skinning model-normal capture, material relief, and multiple local-light shadow cubes. Point shadows change from off by default with one selected source when enabled to on with a configured limit of 64. Actual work depends on visible sources, caster culling and cache reuse.

The integration previously enabled material batching only for the packaged DXVK3 path, and model-normal batching additionally required `ReadBackNoOverwrite`. That flag is a driver-specific read-lock policy, not the ownership contract of a capture packet. Consequently DXVK1/2 and WoWSilicon's `Backend=legacy` commonly retained repeated immediate target/shader/state transitions.

Opaque material packets now use their existing immutable geometry/constants, admission checks and depth barriers independently of the backend name. Textured normal packets still require the existing spell-resource writer, active state mirror and command stream. Texture writes drain matching packets before mutation; render-target inputs, unsupported programs, unsafe layouts and allocation failures keep their conservative fallback. The read-back lock flag remains enabled only for an actually loaded DXVK >= 3. It was not enabled on DXVK1/2.

A once-per-device `CAPTURE_BATCH_POLICY` line records the selected packet contract and source lock flags. Actual r42 DLL hosts report `material=1 modelNormals=1 textureGuard=1` with flags `0x10` on DXVK1 legacy and DXVK2, and `0x1010` on DXVK3.

## Accumulated renderer changes

- **Command submission and EVENT diagnostics:** distinguish CPU command-thread progress from real pending GPU results; retain bounded polling/timer waits and generation/epoch ownership. Publish a committed short prefix when replay is already asleep instead of waiting for the next 64-command batch. Normal busy batching, FIFO and memory limits remain. Successful completion still requires the real query result; the validated production path uses no queue-fence barrier.
- **Cold geometry and CPU capture:** reuse source/pose envelopes and conservative bounds, improve cheap single-bone work and bounded cold completion, share immutable geometry and constant banks, reject incompatible effect overlays before a full pose read, and keep revision/pose/camera validation. Current poses are not replaced with stale geometry.
- **Reset and material replay:** retain dimension-independent CPU sources and eligible managed-buffer generations across Reset, while invalidating default/unknown/locked resources and rebuilding GPU targets. Retain successful optional shader objects. Read-only native draws no longer split an opaque single-sample material batch; depth writes and explicit copy/target/texture hazards still drain it.
- **Model and shadow shader cost:** conservative output liveness removes unused native lighting/texture calculations from supported programs. Position, normal, coverage, clipping and alpha dependencies remain; unsupported or failed slices retain the original patched program. Constant-epoch reuse copies only proven dirty ranges and keeps packet owners alive.
- **Local-light correctness and reuse:** compose supported native corrections with POM and displacement, reuse compatible vertex/pixel variants, share common material work, and preserve other lights, ambient, fog and alpha. Factory-owned coverage/emission MRT transformations are reapplied after local-light correction. This fixes a confirmed rejection path for character parts whose pixel shaders had already been transformed by coverage. Exact rejected shader hashes from the r40 log were reconstructed and checked on real D3D9/DXVK devices. White cubes and exact zero-gain corrections avoid neutral work without reducing non-neutral shadow filtering.
- **HDR and atmosphere:** retain sky/scene resource reuse and composition fixes; add the optional wide-bloom checkbox and moonlight/fog variants. Glare visibility and fog/terrain occlusion remain part of the rendering changes. Optional moon shaders check both device instruction-slot and temporary-register limits and retain their fallback. Visual effect options can intentionally change the image; pixel-equivalence statements apply to the corresponding optimization oracle, not every effect setting.

The changes do not lower AO quality, shadow resolution, the configured caster distances or geometry fidelity to obtain the reported capture result.

## Measured capture component

The isolated 640-draw mutable-source fixture uses 3840×2160 and 2× MSAA, alternating immediate/deferred order, two warm rounds and four retained pairs. WoW was closed for these timing runs.

| Windows backend | GPU immediate / deferred, ms | CPU submission immediate / deferred, ms |
| --- | ---: | ---: |
| DXVK 1.10.3 | 13.868032 / 5.275648 | 3.251100 / 4.085400 |
| DXVK 2.7.1 | 12.597248 / 4.442112 | 3.219800 / 4.082000 |

Target switches fall from 1280 to 44 with exact immediate/deferred pixels. GPU time falls about 62% and 65%, respectively. CPU submission increases about 0.8 ms in this deliberately changing-source fixture because it records immutable packets. These are component measurements, not gameplay FPS, a Mac result, or additive savings across the renderer.

## Validation and limits

The current capture policy passes 64 actual setup combinations with separate old-material and old-normal gate negative controls. The material mirror/sampler/admission/packet/reuse CPU suites and the lock-policy audit pass. ModelNormals checks include all eight real VS2/VS3 programs on DXVK1/2/3, exact immediate/deferred capture, alpha coverage, changing poses, buffered/UP sources, mutation/revision checks, viewport/depth transitions, state restoration and failure paths. Native D3D9 also passes the focused model-normal regression. Opaque and guarded-alpha material suites pass on DXVK1/2/3 across the existing MSAA, depth, source mutation and fallback cases.

The final production DLL passes 64 real EVENT generations on each of DXVK1 legacy, DXVK2 and DXVK3 (192 total), including TRUE/NULL payloads, current build identity, active command stream and the expected capture-policy flags. Pinned Zig 0.15.2 production compilation, shader freshness, PE32 exports and payload checks pass. Earlier per-iteration evidence covers shader slicing, Reset, character-body bounds, native light composition, point cubes and command-stream sanitizer variants. Those historical runs are scoped to their frozen inputs; they do not establish a full-suite pass for the present accumulated tree.

The full historical model-normal camera wrapper on DXVK1 does **not** pass its 3× relative jump-improvement threshold: baseline/control/fixed counts are 51/21/20. The capture implementation is unchanged from r41, and the same fixture's immediate/deferred image and lifetime/state oracles pass for all eight programs. The threshold was not relaxed. This remains a separately recorded visual-metric limitation.

Long EVENT waits are not declared eliminated. The r40 log includes 495.6744 ms of real backend-pending replay before entering the world, with effects off and focus lost; the new short-prefix publication does not prove that wait avoidable. Cold/native loading work and all visible lamp flicker still require matching gameplay captures. Mac/WoWSilicon/MoltenVK has not run on this Windows workstation.

Publication checks additionally pass ten focused tests with zero failures/skips, all 48 world shader manifest/bytecode/header checks, and the full 22-run moon-floor field/renderer/shader matrix on native D3D9 and DXVK2/3. The inventory contains 576 tests; this is not a claim that all 576 were rerun.

The test inventory is synchronized with the added scripts. Tests that require explicitly supplied camera/source fixtures are marked manual; unsupported requirement spellings are corrected. The world-manifest test now checks the already-declared optional moon-shader budget and its runtime device-capability guard. No numerical accuracy or timing acceptance threshold was relaxed.

The build identity, DLL hash, component numbers and local evidence hashes are in [the receipt](renderer-throughput-20261108.json). The local artifact is `out/dll-20261108-r42/d3d9.dll`; no DLL, client asset, QA capture, downloaded backend or installer archive is committed. No installer was rebuilt and the installed game was not changed.

## Reproduction

Use the repository's configured Zig 0.15.2 and a legally supplied client where a fixture requires one. Run Windows compilation and native/GPU checks outside the restricted execution sandbox as described in AGENTS.md.

```text
python scripts/build_renderer.py
python tests/test_capture_batch_policy.py
python tests/test_sparse_publish.py --output out/validation-sparse
python tests/test_upload_locks.py
python tests/test_local_light_transform_freshness.py
python tests/test_world_shader_manifest.py
```

The native-model and deferred-material GPU runners require their documented prepared camera/material fixtures; those game-derived files are intentionally external. Use `tests/test_model_normals_gpu.py --help` and `tests/validate_material_deferred.py --help` for the backend and input arguments. Upstream DXVK1 is a Windows compatibility stand-in and does not replace a real Mac crowd comparison.
