# Shader audit, 2026-11-15

Reviewed the 20 owned HLSL files and all 228 entries in the 11 offline build
manifests, following their runtime references. These entries are alternatives,
helpers and fallback paths, not 228 simultaneous draws. The appendix identifies
every entry and its disposition. The native shader transformations below are a
separate family-level review: arbitrary client shaders and every possible patch
combination have not been exhaustively GPU-tested.

The latest available gameplay log still identifies `b725ae54`, not the preceding
installed `423f4dad`. Its expensive effects were TemporalLight, WorldComposite,
AO/AOBlur, shadow filtering and GI. See [the pipeline review](pipeline-20261115.md)
for the measured intervals and caveats. This change makes no gameplay FPS claim.

## Accepted changes

`probeIrradiance` used to fetch three SH textures and, during a probe update,
three history texels even when the probe's final weight was zero. `WorldGI`
now skips those reads and the radiance arithmetic. Residency coverage is still
accumulated before visibility, preserving the distinction between a dark room
and unavailable probes. No epsilon cutoff, lower precision, fewer probes,
shorter range, slower update or reduced filter is introduced.

`WorldLightingUnshadowedGI` keeps its original bytecode: adding the branch there
would exceed its 512-slot SM3 limit (510 -> 514). A compile-time `localOnly`
condition removes the branch from that entry. `WorldGI` changes 387 -> 391 static
slots with the same 25 temporary registers. Fewer executed texture reads, not a
smaller static instruction count, produce the gain.

Seven obsolete resources are excluded from the production assets and generated
include overlay. Their canonical source and bytecode remain available to tests:

| Resource | Why no production consumer is needed |
|---|---|
| HdrReduce | Replaced by HdrReduceLinear; retained as the 64-read oracle. |
| HdrBloom | Replaced by axis-specific filters; retained as the filter oracle. |
| LocalShadowResolve, LocalLightingSoft | The selected PCSS route combines visibility and lighting in LocalLightingPcss. |
| RainLens, SnowLens | Current camera particles use WeatherLensParticle; the rain/snow menu settings still work. |
| SourceVisibilityPS | Celestial visibility owns the source mask/history; this older world-fog entry has no caller. |

This removes 25,448 unpacked resource bytes. With the changed GI shader, the final
asset pack is 649,706 bytes (previously 655,209). This is a package/memory reduction,
not a frame-time reduction: these shaders were not being drawn. The exclusion
list is explicit; reference scanning does not automatically delete resources.
A new production caller of an excluded symbol fails compilation until the
resource is restored. Asset tests account for every generated array, verify
runtime bytes exactly and reject unexpected references to the excluded list.

## Whole-source review

| Source / entry family | Work, existing protections, decision |
|---|---|
| effects.hlsl: AO/HDAO/HBAO/GTAO/ASSAO | Radius/quality variants are compile-time choices. Sky, water and zero-strength rejection already exist; HDR selects alpha-only entries and does not calculate contact bloom. Preserve samples, thin-occluder tests and normalization. |
| effects.hlsl: AOBlur / NoWater | Neutral-area rejection and dry specialization already exist. Tested an additional sky/water tap rejection: identical images but slower on several 4K fixtures. Rejected; production bytecode unchanged. |
| effects.hlsl: Composite / ContactBloom | Legacy/failure path and SDR bloom. Shared world composition owns the normal path; these are not an unconditional second compositor. Keep fallback. |
| ao_actor.hlsl | Prepared contact/normal data, bounded width searches and thin-actor horizon handling. An extra preparation pass is conditional on actor-aware AO. Removing it changes contact quality. Preserve. |
| world_effects.hlsl: probe GI | Accepted zero-weight radiance/history rejection described above. Preserve coverage before visibility and the authored-lighting blend. |
| world_effects.hlsl: normals/distance | Model normals are reused when valid; depth reconstruction handles uncovered pixels. MRT distance output avoids a separate conversion. No removal of animation, depth validation or silhouette fallback. |
| world_effects.hlsl: directional/actor shadows | Tile minima reject definitely lit areas. Blocker search and PCF determine different quantities; they are not interchangeable duplicate passes. Preserve fine actor detail and receiver-plane correction. |
| world_effects.hlsl: lighting/local sources | Unshadowed, fused-GI and shared-world/actor variants already avoid unused work. LocalDirect1/8/16 are workload specializations; changing light counts would change the image. |
| world_effects.hlsl: smoothing/history | Fused TemporalLight remains enabled; DXVK 3.1.1 retains its measured zero-weight specialization. Separate shaders are required for diagnostics/resource failure. Do not run both paths as an A/B comparison in production. |
| world_effects.hlsl: composite/material | Shared receiver/source inputs, neutral-effect masks and distance rejection already exist. Four upsample taps use depth and luminance to preserve edges. Prelit surfaces have a different GI/daylight contract. Keep variants. |
| world_effects.hlsl: fog/clouds/Forever | Spatial support, near fade, zero density, light influence and noise rejection already exist. Density and directional visibility have separate ownership. History and depth-aware blur prevent motion artifacts. Preserve. |
| world_effects.hlsl: LocalFog | Analytic bounded lamp integration replaces marching; lamp range is not emitter radius. Keep the bounded sum/combine pass. |
| world_effects.hlsl: MaterialAudit | Diagnostic-only output; not normal gameplay work. Preserve for capture validation. |
| shadow_transition.hlsl | Current/previous map contributions maintain continuity while cached maps update. Dynamic casters are combined with previous static maps; a simple history reuse would ghost moving casters. Preserve. |
| fog_volume.hlsl | Injection, reprojection, prefix integration and resolve consume different data. Ambient/quiet entries already omit directional/noise work. Two noisy injection draws avoid SM3 temporary overflow; blindly merging exceeds the register budget. |
| local_light_effects.hlsl | Range/backface/relevance rejection precedes PCSS. The one-pass PCSS route is current; two old entries removed from the pack. Native 1/2/3-light helpers reconstruct nonlinear native material endpoints, sharing independent work in the patcher. |
| static_shadow_effects.hlsl | Opaque and no-explicit-depth variants already specialize proven geometry. Full variants retain alpha and clip-plane/depth correctness. No generic removal of DEPTH/clip. |
| surface_effects.hlsl | Native precipitation inheritance and shelter ownership are host decisions. World particles, camera particles, frost and sand are distinct effects. Remove only the old RainLens/SnowLens resources. Implicit derivative texture reads cannot simply move into divergent branches. |
| celestial_disc_effects.hlsl | Disc, halo visibility, veil, terrain mask and sky replacement have different coverage/HDR semantics. Halo history is tiny; do not replace it with repeated full-screen queries. Preserve HDR source energy and alpha. |
| hdr_effects.hlsl | Linear reduction, axis bloom, paired wide bloom, neutral-contrast branch and premultiplied-zero handling already exist. Exposure history, tone mapping and output encoding are required. Remove only the two unused reference resources. |
| aurora_emission.hlsl | Native linear radiance and saturated legacy radiance differ for overbright inputs. Do not collapse their transfers; preserve emission and alpha in SM2/SM3. |
| auto_bump_capture.hlsl | Coverage/range rejection, normal-only and shared-POM variants already exist. Preserve alpha at displaced UVs and distinct layer blending. ModelNormalDepthSeed guards MSAA occlusion; not a redundant color copy. |
| auto_bump_normal.hlsl | Depth/coverage/distance validity and preserved normal magnitude are necessary for uncertain geometry. Keep the existing neutral early exits. |
| auto_bump_shared.hlsl | Compile-time wrapper enabling shared POM outputs; no additional runtime pass by itself. |
| auto_material_height.hlsl | Zero-strength/color-edge branches already omit unused work. Texture-generation input, not a full-screen per-frame effect. |
| auto_normal_generate.hlsl | 9-sample height / model prefilter build cached normal maps. Removing samples changes derived materials; optimize residency before reducing generation quality. |
| auto_normal_preview.hlsl | Tool/preview source, absent from the 228 production manifests. Do not count preview ray steps as frame cost. |
| auto_parallax.hlsl | View/light ray termination and fade already exist. Derivatives are formed before divergence; the previous-height read retains the capture path's established numerical contract. |
| auto_parallax_prefix.hlsl | Shared primitive tags reuse POM UVs. The apparent extra derivative dependencies intentionally keep derivative evaluation outside the cache-hit branch. |
| rain_mask.hlsl | Native rain MRT mask plus conditional depth scrub. The scrub removes stale mask coverage after later geometry; it is not another rainfall simulation. |

## Native/assembled shader families

These do not add entries to the offline manifest count. Inspected the generated
outputs, specialization/caching boundaries and conservative fallback contracts;
their existing numerical behavior is unchanged in this change.

| Code | Finding / disposition |
|---|---|
| model_normal_patch.h, model_normal_mrt_patch.h, model_normal_slice.h | Reuse the native transformed normal in MRT where possible. Replay shaders already slice dead lighting and canonicalize equivalent programs. Keep alpha/depth and backend-specific varying masks. |
| native_material_capture_patch.h | Native alpha slicing and compact control banks already remove RGB work from capture. Unsupported instruction/control-flow cases retain the native program. No unsafe general shader rewrite. |
| auto_parallax_patch.h, auto_bump_vertex_patch.h, displacement_shader_patch.h | Compose existing helpers with the selected native material; shared POM avoids redoing the ray when a primitive tag matches. Preserve original UV derivatives and vertex deformation. |
| native_local_light_patch.h, native_more_light_shader.h | Independent native material instructions/texture reads are shared between lighting endpoints. Nonlinear/saturating material results prevent treating every endpoint as one linear light sum. Pinned r18 Legacy1/2/3 helper bytecode preserves admission of large native materials. Derivative hoisting before range divergence is required for correct texture gradients. |
| hdr_ui_patch.h, sky_alpha_patch.h, aurora_emission_patch.h | Append transfer/alpha semantics while keeping the original shader and temporary lifetimes. Native auxiliary MRT outputs and alpha test are preserved. |
| water_shader_patch.h | Native MRT captures coverage x distance; resolved coverage must be decoded before depth consumers. Preserve replay fallback when MRT is inadmissible. |
| effect_coverage.h, patch_terrain_shadow_coverage.h | MRT coverage protects early overlays and prelit receivers from duplicate lighting. Not another complete scene draw. |
| patch_shadow_shader.h, patch_terrain_shadow.h | Cached projection/coverage variants and native-shadow ownership are separate. Replacing native shadow use is not proof that every original texture fetch was removed; further dead-producer elimination needs native-shader liveness and image evidence. |
| world_lighting_capture.h | Tiny copy/constant shaders exist only for explicit diagnostic captures; retain. |
| ao_actor_stencil.h | Cached constant-white stencil resolve; retain when actor AO consumes it. |

## Evidence and reproduction

Artifacts are under `.local/out/shader-audit-20261115/`. Generate a current
machine-readable inventory and line references with:

```powershell
python scripts/audit_shaders.py --output .local/out/shader-audit-20261115/after
```

`test_gi_zero_weight_gpu.py` compares frozen `423f4dadec4334a6516b51ff08c48a399a45f09b`
bytecode against the candidate, including all MRT outputs. Fixtures cover valid,
missing, fresh, occluded, mixed and transitioning probes, sky, water, uncertain
normals and the legacy visibility fallback. Default output is production FP16;
`--fp32` adds a higher-precision oracle. Timed runs use a 1920x1080 GI target
(the half-resolution pass for a 3840x2160 frame), 256 repeated draws and 16
paired ABBA blocks after warmup. `--control` measures A/A noise. These are
synthetic pass measurements with Forever running in the background, not full
game frames or an exclusive-GPU benchmark.

The early `gi-long-*` experiment used FP32 targets; only `gi-fp16-*` is the
production-format timing evidence. `ao-trial-check` contains 180 exact-image
comparisons for the rejected AOBlur candidate; its source stays local, not in
the production shader tree.

Final FP16 results below are per-pass GPU timestamps, not frame times. Gains are
the median of paired ABBA block ratios; the two time columns are marginal
medians and therefore need not produce exactly the same percentage. Every
compared output component matched. Separate A/A runs observed apparent changes
between -1.35% and +1.90%.

| Backend | Fixture | Before / after, ms | Paired gain | A/A apparent gain |
|---|---|---:|---:|---:|
| Native D3D9 | open | 0.259670 / 0.228592 | 11.59% | -1.35% |
| Native D3D9 | closed | 0.228144 / 0.188086 | 21.58% | 1.72% |
| Native D3D9 | mixed | 0.286428 / 0.238946 | 14.81% | 0.01% |
| DXVK 2 | open | 0.426978 / 0.382032 | 10.62% | 0.54% |
| DXVK 2 | closed | 0.390104 / 0.264380 | 29.52% | 1.26% |
| DXVK 2 | mixed | 0.487298 / 0.395316 | 18.46% | 1.90% |
| DXVK 3 | open | 0.277840 / 0.256493 | 7.44% | 0.95% |
| DXVK 3 | closed | 0.289320 / 0.223184 | 23.12% | -0.77% |
| DXVK 3 | mixed | 0.326702 / 0.289214 | 14.17% | 1.26% |

Reproduce correctness through the `source`, `architecture` and
`lighting-composite` strict suites, plus `test_world_lighting_gi_gpu.py`,
`test_probe_local_visibility_gpu.py` and `test_world_probe_coverage.py`.
Run `test_gi_zero_weight_gpu.py --fp32` for the additional precision oracle;
use `--benchmark` and then `--benchmark --control` for timing. Set
`NORTHLIGHT_TEST_ALLOW_RUNNING_GAME=1` when leaving Forever open as requested.
Acceptance reports, backend hashes and deployment hashes are stored with the
local artifacts; verify report freshness before reusing their conclusions.

## Entry-by-entry inventory

Static slots count code, not loop iterations or GPU time. `keep` means reviewed
and retained, not newly benchmarked. Runtime references are lexical evidence;
the successful production build and explicit exclusion tests protect the seven
removed symbols. The generated inventory JSON contains all references and
bytecode/source hashes.

| Family / entry | Slots / temps | Disposition | First runtime reference |
|---|---:|---|---|
| aurora / AuroraLinear-ps_2_0 | 38 / 5 | keep | src/sky/aurora_emission_patch.h:16 |
| aurora / AuroraLegacy-ps_2_0 | 32 / 3 | keep | src/sky/aurora_emission_patch.h:16 |
| aurora / AuroraLinear-ps_3_0 | 37 / 5 | keep | src/sky/aurora_emission_patch.h:16 |
| aurora / AuroraLegacy-ps_3_0 | 33 / 3 | keep | src/sky/aurora_emission_patch.h:16 |
| auto-bump / BumpOpaque | 280 / 10 | keep | src/water/auto_bump_renderer.h:785 |
| auto-bump / BumpLayer | 281 / 12 | keep | src/water/auto_bump_renderer.h:785 |
| auto-bump / BumpWall | 285 / 11 | keep | src/water/auto_bump_renderer.h:785 |
| auto-bump / BumpGrass | 282 / 12 | keep | src/water/auto_bump_renderer.h:785 |
| auto-bump / BumpWallOpaque | 281 / 12 | keep | src/water/auto_bump_renderer.h:785 |
| auto-bump / BumpComposite | 494 / 14 | keep | src/water/auto_bump_renderer.h:785 |
| auto-bump / BumpNative | 283 / 12 | keep | src/water/native_material_capture_patch.h:62 |
| auto-bump / BumpSharedOpaque | 287 / 11 | keep | src/water/auto_bump_renderer.h:787 |
| auto-bump / BumpSharedLayer | 288 / 12 | keep | src/water/auto_bump_renderer.h:787 |
| auto-bump / BumpSharedWall | 292 / 11 | keep | src/water/auto_bump_renderer.h:787 |
| auto-bump / BumpSharedGrass | 289 / 12 | keep | src/water/auto_bump_renderer.h:787 |
| auto-bump / BumpSharedWallOpaque | 288 / 12 | keep | src/water/auto_bump_renderer.h:787 |
| auto-bump / BumpSharedComposite | 501 / 15 | keep | src/water/auto_bump_renderer.h:787 |
| auto-bump / BumpSharedNative | 290 / 12 | keep | src/water/native_material_capture_patch.h:62 |
| auto-bump / BumpTerrainOpaque | 303 / 10 | keep | src/water/auto_bump_renderer.h:785 |
| auto-bump / BumpTerrainLayer | 304 / 12 | keep | src/water/auto_bump_renderer.h:785 |
| auto-bump / BumpSharedTerrainOpaque | 310 / 11 | keep | src/water/auto_bump_renderer.h:787 |
| auto-bump / BumpSharedTerrainLayer | 311 / 12 | keep | src/water/auto_bump_renderer.h:787 |
| auto-bump / AutoNormal | 497 / 4 | keep: cached generation | src/water/auto_normal_cache.h:169 |
| auto-bump / AutoNormalModel | 167 / 6 | keep: cached generation | src/water/auto_normal_cache.h:169 |
| auto-bump / ParallaxUV | 147 / 8 | keep | src/water/auto_parallax_patch.h:40 |
| auto-bump / ParallaxSharedUV | 172 / 9 | keep | src/water/auto_parallax_patch.h:40 |
| auto-bump / BumpNormalOpaque | 128 / 7 | keep | src/water/auto_bump_renderer.h:786 |
| auto-bump / BumpNormalLayer | 129 / 9 | keep | src/water/auto_bump_renderer.h:786 |
| auto-bump / BumpNormalGrass | 130 / 9 | keep | src/water/auto_bump_renderer.h:786 |
| auto-bump / BumpNormalTerrainOpaque | 151 / 7 | keep | src/water/auto_bump_renderer.h:786 |
| auto-bump / BumpNormalTerrainLayer | 152 / 7 | keep | src/water/auto_bump_renderer.h:786 |
| auto-bump / BumpNormalNative | 131 / 9 | keep | src/water/native_material_capture_patch.h:62 |
| auto-bump / ModelNormalCapture | 13 / 1 | keep | src/water/native_material_capture_patch.h:62 |
| auto-bump / ModelNormalDepthSeed | 25 / 2 | keep | src/proxy/model_normal_renderer.h:646 |
| celestial-disc / CelestialDiscPS | 431 / 12 | keep | src/sky/celestial_disc_renderer.h:137 |
| celestial-disc / CelestialSkyPS | 81 / 6 | keep | src/sky/celestial_disc_renderer.h:214 |
| celestial-disc / CelestialHaloVisibilityPS | 209 / 12 | keep | src/sky/celestial_disc_renderer.h:156 |
| celestial-disc / CelestialVeilPS | 189 / 6 | keep | src/sky/celestial_disc_renderer.h:597 |
| celestial-disc / CelestialTerrainVS | 6 / 2 | keep | src/sky/celestial_disc_renderer.h:52 |
| celestial-disc / CelestialTerrainPS | 1 / 0 | keep | src/sky/celestial_disc_renderer.h:53 |
| fog-volume / VolumeInject | 375 / 9 | keep | src/sky/fog_volume.h:24 |
| fog-volume / VolumeInjectQuiet | 268 / 9 | keep | src/sky/fog_volume.h:24 |
| fog-volume / VolumeInjectActor | 503 / 12 | keep | src/sky/fog_volume.h:24 |
| fog-volume / VolumeInjectActorQuiet | 408 / 12 | keep | src/sky/fog_volume.h:24 |
| fog-volume / VolumeTemporal | 205 / 12 | keep | src/sky/fog_volume.h:27 |
| fog-volume / VolumePrefix | 74 / 4 | keep | src/sky/fog_volume.h:27 |
| fog-volume / VolumeResolve | 139 / 4 | keep | src/sky/fog_volume.h:27 |
| fog-volume / VolumeInjectAmbient | 267 / 9 | keep | src/sky/fog_volume.h:25 |
| fog-volume / VolumeInjectAmbientQuiet | 162 / 8 | keep | src/sky/fog_volume.h:25 |
| fog-volume / VolumeInjectTransition | 429 / 16 | keep | src/sky/fog_volume.h:25 |
| fog-volume / VolumeInjectQuietTransition | 324 / 15 | keep | src/sky/fog_volume.h:25 |
| fog-volume / VolumeInjectActorTransition | 558 / 17 | keep | src/sky/fog_volume.h:25 |
| fog-volume / VolumeInjectActorQuietTransition | 463 / 16 | keep | src/sky/fog_volume.h:25 |
| hdr / HdrUi-ps_2_0 | 24 / 3 | keep | src/proxy/hdr_color.h:111 |
| hdr / HdrUi-ps_3_0 | 23 / 3 | keep | src/proxy/hdr_color.h:111 |
| hdr / HdrParticle-ps_2_0 | 15 / 3 | keep | src/proxy/hdr_color.h:111 |
| hdr / HdrParticle-ps_3_0 | 15 / 3 | keep | src/proxy/hdr_color.h:111 |
| hdr / HdrSkyResidual-ps_2_0 | 5 / 1 | keep | src/proxy/hdr_sky_foreground.h:55 |
| hdr / HdrSkyResidual-ps_3_0 | 4 / 1 | keep | src/proxy/hdr_sky_foreground.h:55 |
| hdr / HdrWorld | 41 / 4 | keep | src/proxy/hdr_color.h:272 |
| hdr / HdrOverlay | 25 / 3 | keep | src/overlay/settings_overlay.cpp:438 |
| hdr / HdrFixedUi | 40 / 4 | keep | src/proxy/hdr_color.h:224 |
| hdr / HdrFixedAdditive | 25 / 4 | keep | src/proxy/hdr_color.h:224 |
| hdr / HdrFixedModulate | 3 / 1 | keep | src/proxy/hdr_sky_foreground.h:30 |
| hdr / HdrFixedUi2 | 66 / 4 | keep | src/proxy/hdr_color.h:224 |
| hdr / HdrFixedSky2 | 43 / 4 | keep | src/proxy/hdr_color.h:224 |
| hdr / HdrMeter | 30 / 4 | keep | src/proxy/hdr_frame.h:112 |
| hdr / HdrReduce | 330 / 6 | exclude production; keep oracle | none |
| hdr / HdrReduceLinear | 91 / 5 | keep | src/proxy/hdr_frame.h:112 |
| hdr / HdrDecodeWorld | 84 / 5 | keep | src/proxy/hdr_frame.h:112 |
| hdr / HdrCopy | 1 / 0 | keep | src/proxy/hdr_frame.h:112 |
| hdr / HdrLegacyComposite | 141 / 8 | keep | src/proxy/hdr_frame.h:112 |
| hdr / HdrAdapt | 50 / 5 | keep | src/proxy/hdr_frame.h:112 |
| hdr / HdrBloom | 66 / 4 | exclude production; keep oracle | none |
| hdr / HdrBloomHorizontal | 60 / 4 | keep | src/proxy/hdr_frame.h:112 |
| hdr / HdrBloomVertical | 20 / 4 | keep | src/proxy/hdr_frame.h:112 |
| hdr / HdrBloomWideHorizontal | 38 / 5 | keep | src/proxy/hdr_frame.h:53 |
| hdr / HdrBloomWideVertical | 30 / 4 | keep | src/proxy/hdr_frame.h:54 |
| hdr / HdrBloomWidePairHorizontal | 54 / 7 | keep | src/proxy/hdr_frame.h:53 |
| hdr / HdrBloomWidePairVertical | 75 / 5 | keep | src/proxy/hdr_frame.h:54 |
| hdr / HdrBloomWideResolve | 37 / 5 | keep | src/proxy/hdr_frame.h:55 |
| hdr / HdrWorldLinear | 76 / 6 | keep | src/proxy/hdr_frame.h:112 |
| hdr / HdrWorldLinearOnly | 52 / 4 | keep | src/proxy/hdr_frame.h:112 |
| hdr / HdrLegacyEncode | 24 / 4 | keep | src/proxy/hdr_frame.h:112 |
| hdr / HdrCalibration | 1 / 0 | keep: diagnostic | src/overlay/settings_overlay.cpp:350 |
| hdr / HdrLayer | 42 / 5 | keep | src/proxy/hdr_color.h:73 |
| local-light / LocalShadowVS | 9 / 2 | keep | src/world/world_point_rendering.inl:128 |
| local-light / LocalPaletteShadowVS | 22 / 2 | keep | src/world/world_point_rendering.inl:667 |
| local-light / LocalShadowPS | 68 / 4 | keep | src/world/world_point_rendering.inl:129 |
| local-light / LocalLighting | 353 / 14 | keep | src/world/world_point_rendering.inl:130 |
| local-light / LocalShadowResolve | 363 / 17 | exclude production; keep oracle | none |
| local-light / LocalLightingSoft | 250 / 9 | exclude production; keep oracle | none |
| local-light / LocalLightingPcss | 511 / 18 | keep | src/world/world_point_rendering.inl:1059 |
| local-light / NativeLocalLighting1 | 172 / 15 | keep | src/lights/native_local_light_renderer.h:91 |
| local-light / NativeLocalLighting2 | 213 / 19 | keep | src/lights/native_local_light_renderer.h:91 |
| local-light / NativeLocalLighting3 | 247 / 19 | keep | src/lights/native_local_light_renderer.h:91 |
| local-light / NativeLocalLightingLegacy1 | 152 / 12 | keep | src/lights/native_local_light_renderer.h:126 |
| local-light / NativeLocalLightingLegacy2 | 277 / 14 | keep | src/lights/native_local_light_renderer.h:126 |
| local-light / NativeLocalLightingLegacy3 | 410 / 14 | keep | src/lights/native_local_light_renderer.h:126 |
| effects / AO | 314 / 13 | keep | src/proxy/renderer.cpp:1216 |
| effects / AOContactBloom | 289 / 9 | keep | src/proxy/renderer.cpp:1213 |
| effects / ContactBloom | 29 / 4 | keep | src/proxy/renderer.cpp:1227 |
| effects / HDAOContactBloom | 265 / 12 | keep | src/proxy/renderer.cpp:1213 |
| effects / HBAOPlusContactBloom | 319 / 10 | keep | src/proxy/renderer.cpp:1213 |
| effects / GTAOContactBloom | 492 / 17 | keep | src/proxy/renderer.cpp:1213 |
| effects / HDAOContactBloomMedium | 274 / 12 | keep | src/proxy/renderer.cpp:1214 |
| effects / HBAOPlusContactBloomMedium | 319 / 10 | keep | src/proxy/renderer.cpp:1214 |
| effects / GTAOContactBloomMedium | 492 / 17 | keep | src/proxy/renderer.cpp:1214 |
| effects / HDAOContactBloomLow | 274 / 12 | keep | src/proxy/renderer.cpp:1214 |
| effects / HBAOPlusContactBloomLow | 319 / 10 | keep | src/proxy/renderer.cpp:1215 |
| effects / GTAOContactBloomLow | 493 / 17 | keep | src/proxy/renderer.cpp:1215 |
| effects / AOBlur | 377 / 17 | keep | src/proxy/renderer.cpp:1225 |
| effects / AOBlurNoWater | 330 / 16 | keep | src/proxy/renderer.cpp:1226 |
| effects / Composite | 359 / 14 | keep | src/proxy/renderer.cpp:1228 |
| effects / AOActorPrepare | 278 / 12 | keep | src/proxy/renderer.cpp:483 |
| effects / HBAOActorContactBloom | 403 / 19 | keep | src/proxy/renderer.cpp:486 |
| effects / GTAOActorContactBloom | 495 / 24 | keep | src/proxy/renderer.cpp:486 |
| effects / HBAOActorOnly | 377 / 19 | keep | src/proxy/renderer.cpp:485 |
| effects / GTAOActorOnly | 470 / 25 | keep | src/proxy/renderer.cpp:485 |
| effects / RainMaskMRT | 5 / 1 | keep | src/proxy/renderer.cpp:2300 |
| effects / RainScrub | 9 / 2 | keep | src/proxy/renderer.cpp:2301 |
| effects / AOOnly | 263 / 9 | keep | src/proxy/renderer.cpp:1220 |
| effects / HDAOOnly | 240 / 12 | keep | src/proxy/renderer.cpp:1220 |
| effects / HDAOOnlyMedium | 250 / 13 | keep | src/proxy/renderer.cpp:1221 |
| effects / HDAOOnlyLow | 250 / 13 | keep | src/proxy/renderer.cpp:1221 |
| effects / HBAOPlusOnly | 294 / 11 | keep | src/proxy/renderer.cpp:1220 |
| effects / HBAOPlusOnlyMedium | 294 / 11 | keep | src/proxy/renderer.cpp:1221 |
| effects / HBAOPlusOnlyLow | 294 / 11 | keep | src/proxy/renderer.cpp:1222 |
| effects / GTAOOnly | 467 / 18 | keep | src/proxy/renderer.cpp:1220 |
| effects / GTAOOnlyMedium | 467 / 18 | keep | src/proxy/renderer.cpp:1221 |
| effects / GTAOOnlyLow | 467 / 18 | keep | src/proxy/renderer.cpp:1222 |
| effects / ASSAOOnly | 466 / 14 | keep | src/proxy/renderer.cpp:1220 |
| effects / ASSAOOnlyMedium | 462 / 14 | keep | src/proxy/renderer.cpp:1221 |
| effects / ASSAOOnlyLow | 361 / 13 | keep | src/proxy/renderer.cpp:1222 |
| effects / ASSAOContactBloom | 492 / 13 | keep | src/proxy/renderer.cpp:1213 |
| effects / ASSAOContactBloomMedium | 499 / 13 | keep | src/proxy/renderer.cpp:1214 |
| effects / ASSAOContactBloomLow | 387 / 12 | keep | src/proxy/renderer.cpp:1215 |
| shadow-transition / ForeverFogNoNoiseTransition | 408 / 20 | keep | src/world/world_screen_resources.inl:56 |
| shadow-transition / ForeverFogActorNoNoiseTransition | 533 / 21 | keep | src/world/world_screen_resources.inl:57 |
| shadow-transition / ForeverFogTransition | 490 / 30 | keep | src/world/world_screen_resources.inl:52 |
| shadow-transition / ForeverFogActorTransition | 617 / 32 | keep | src/world/world_screen_resources.inl:53 |
| shadow-transition / DirectionalShadowTransitionCurrent | 552 / 21 | keep | src/world/world_shadow_transition.inl:41 |
| shadow-transition / DirectionalShadowTransitionPrevious | 762 / 28 | keep | src/world/world_shadow_transition.inl:42 |
| shadow-transition / WorldFogTransition | 525 / 22 | keep | src/world/world_shadow_transition.inl:43 |
| shadow-transition / WorldFogActorTransition | 653 / 25 | keep | src/world/world_shadow_transition.inl:45 |
| shadow-transition / WorldFogActorTiledTransition | 716 / 26 | keep | src/world/world_shadow_transition.inl:46 |
| shadow-transition / FogCloudsTransition | 519 / 23 | keep | src/world/world_shadow_transition.inl:44 |
| shadow-transition / FogCloudsActorTransition | 645 / 25 | keep | src/world/world_shadow_transition.inl:47 |
| shadow-transition / FogCloudsActorTiledTransition | 710 / 26 | keep | src/world/world_shadow_transition.inl:48 |
| shadow-transition / MoonFloorFogTransition | 579 / 24 | keep | src/world/world_renderer.h:2488 |
| shadow-transition / MoonFloorFogActorTransition | 707 / 27 | keep | src/world/world_renderer.h:2488 |
| shadow-transition / MoonFloorFogActorTiledTransition | 768 / 28 | keep | src/world/world_renderer.h:2488 |
| static-shadow / StaticCasterVS | 15 / 3 | keep | src/shadows/static_shadow_gpu.h:494 |
| static-shadow / StaticCasterInstancedVS | 15 / 3 | keep | src/shadows/static_shadow_gpu.h:500 |
| static-shadow / StaticCasterPS | 10 / 1 | keep | src/shadows/static_shadow_gpu.h:494 |
| static-shadow / StaticCasterFastPS | 5 / 1 | keep | src/shadows/static_shadow_gpu.h:496 |
| static-shadow / StaticCasterOpaqueFastPS | 1 / 0 | keep | src/shadows/static_shadow_gpu.h:497 |
| static-shadow / StaticCasterOpaquePS | 6 / 1 | keep | src/shadows/static_shadow_gpu.h:498 |
| surface / RainQuality | 250 / 7 | keep | src/water/surface_renderer.h:74 |
| surface / SnowQuality | 461 / 7 | keep | src/water/surface_renderer.h:74 |
| surface / RainPerformance | 179 / 6 | keep | src/water/surface_renderer.h:74 |
| surface / SnowPerformance | 321 / 6 | keep | src/water/surface_renderer.h:74 |
| surface / RainLens | 139 / 4 | exclude production; keep oracle | none |
| surface / SnowLens | 127 / 5 | exclude production; keep oracle | none |
| surface / WeatherLensParticle | 227 / 6 | keep | src/water/surface_renderer.h:135 |
| surface / FrostVignette | 55 / 4 | keep | src/water/surface_renderer.h:111 |
| surface / SandLens | 47 / 4 | keep | src/water/surface_renderer.h:124 |
| world / WorldAOCompositeHdr | 417 / 16 | keep | src/world/world_renderer.h:5253 |
| world / FogCloudsAmbient | 309 / 15 | keep | src/world/world_renderer.h:739 |
| world / ForeverFogNoNoise | 352 / 16 | keep | src/world/world_screen_resources.inl:54 |
| world / ForeverFogActorNoNoise | 480 / 19 | keep | src/world/world_screen_resources.inl:55 |
| world / WorldDistance | 29 / 3 | keep | src/world/world_screen_resources.inl:40 |
| world / ForeverFog | 447 / 30 | keep | src/world/world_screen_resources.inl:50 |
| world / ForeverFogActor | 564 / 32 | keep | src/world/world_screen_resources.inl:51 |
| world / DirectionalShadow | 490 / 20 | keep | src/world/world_soft_shadows.inl:28 |
| world / WorldLightingSoft | 256 / 8 | keep | src/world/world_soft_shadows.inl:29 |
| world / ActorShadowUniform | 253 / 10 | keep | src/world/world_renderer.h:2381 |
| world / WorldNormals | 346 / 22 | keep | src/world/world_screen_resources.inl:34 |
| world / WorldNormalsNoDistance | 324 / 22 | keep | src/world/world_renderer.h:6099 |
| world / WorldLighting | 442 / 17 | keep | src/world/world_screen_resources.inl:33 |
| world / WorldLightingUnshadowed | 235 / 8 | keep | src/world/world_renderer.h:6205 |
| world / WorldLightingUnshadowedGI | 510 / 26 | keep: SM3 budget; bytecode unchanged | src/world/world_renderer.h:6215 |
| world / ActorShadow | 345 / 10 | keep | src/world/world_renderer.h:2381 |
| world / ActorTiles | 90 / 4 | keep | src/world/world_renderer.h:2381 |
| world / WorldGI | 391 / 25 | optimized: zero-weight GI | src/world/world_screen_resources.inl:43 |
| world / WorldFog | 471 / 22 | keep | src/world/world_screen_resources.inl:59 |
| world / WorldFogAmbient | 340 / 14 | keep | src/world/world_renderer.h:745 |
| world / WorldFogActor | 599 / 23 | keep | src/world/world_screen_resources.inl:64 |
| world / FogBlur | 233 / 8 | keep | src/world/world_screen_resources.inl:59 |
| world / FogClouds | 465 / 22 | keep | src/world/world_screen_resources.inl:65 |
| world / FogCloudsActor | 592 / 25 | keep | src/world/world_screen_resources.inl:64 |
| world / FogTemporal | 252 / 13 | keep | src/world/world_screen_resources.inl:66 |
| world / LocalDirect | 304 / 8 | keep | src/world/world_screen_resources.inl:45 |
| world / LocalDirect1 | 206 / 8 | keep | src/world/world_renderer.h:732 |
| world / LocalDirect16 | 416 / 8 | keep | src/world/world_screen_resources.inl:45 |
| world / LocalFlash | 447 / 8 | keep | src/world/world_screen_resources.inl:46 |
| world / FlashShadow | 390 / 13 | keep | src/world/world_flash_rendering.inl:22 |
| world / RemovalSmooth | 227 / 18 | keep | src/world/world_screen_resources.inl:33 |
| world / TemporalLight | 210 / 13 | keep | src/world/world_screen_resources.inl:34 |
| world / TemporalLightMerged | 402 / 21 | keep | src/world/world_screen_resources.inl:17 |
| world / TemporalLightMergedZeroWeight | 408 / 20 | keep | src/world/world_screen_resources.inl:14 |
| world / LocalFog | 382 / 6 | keep | src/world/world_screen_resources.inl:61 |
| world / LocalFogCombine | 10 / 3 | keep | src/world/world_screen_resources.inl:61 |
| world / SourceVisibilityPS | 285 / 7 | exclude production; keep oracle | none |
| world / WorldComposite | 475 / 22 | keep | src/world/world_screen_resources.inl:31 |
| world / WorldUnlitComposite | 332 / 13 | keep | src/world/world_renderer.h:5261 |
| world / MaterialComposite | 296 / 10 | keep | src/world/world_screen_resources.inl:69 |
| world / WorldCompositeHdr | 495 / 22 | keep | src/world/world_renderer.h:5249 |
| world / WorldBaseCompositeHdr | 429 / 16 | keep | src/world/world_renderer.h:5254 |
| world / WorldUnlitCompositeHdr | 362 / 14 | keep | src/world/world_renderer.h:5250 |
| world / MaterialCompositeHdr | 333 / 10 | keep | src/world/world_renderer.h:5251 |
| world / WorldMaterialCompositeHdr | 699 / 22 | keep | src/world/world_renderer.h:5258 |
| world / WorldPrelitComposite | 480 / 21 | keep | src/world/world_renderer.h:5265 |
| world / WorldPrelitCompositeHdr | 500 / 21 | keep | src/world/world_renderer.h:5265 |
| world / MaterialAudit | 253 / 8 | keep: diagnostic | src/world/material_pixel_audit.h:29 |
| world / ShadowVS | 9 / 2 | keep | src/world/world_screen_resources.inl:72 |
| world / ShadowCacheVS | 10 / 2 | keep | src/world/world_screen_resources.inl:72 |
| world / ShadowPS | 5 / 1 | keep | src/world/world_screen_resources.inl:71 |
| world / ShadowReplayPS | 59 / 4 | keep | src/world/world_screen_resources.inl:71 |
| world / ShadowUnion | 34 / 2 | keep | src/world/world_point_rendering.inl:151 |
| world / ActorTilesWide | 330 / 4 | keep | src/world/world_renderer.h:2385 |
| world / WorldFogActorTiled | 660 / 24 | keep | src/world/world_renderer.h:2385 |
| world / FogCloudsActorTiled | 657 / 26 | keep | src/world/world_renderer.h:2385 |
| world / MoonFloorFog | 516 / 24 | keep | src/world/world_renderer.h:2488 |
| world / MoonFloorFogActor | 644 / 26 | keep | src/world/world_renderer.h:2488 |
| world / MoonFloorFogActorTiled | 705 / 27 | keep | src/world/world_renderer.h:2488 |
| world / UnifiedDirectionalShadow | 831 / 21 | keep | src/world/world_soft_shadows.inl:17 |
| world / WorldLightingShared | 252 / 8 | keep | src/world/world_soft_shadows.inl:18 |
