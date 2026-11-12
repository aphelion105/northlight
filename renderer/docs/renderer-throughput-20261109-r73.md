# r73: one-pass point PCSS

Baseline: `814a816d0a01510778285d97609b7245c6c0bdee`.
Production source identity:
`0a26077130a6a384c86a6767e0add00c5699c4fb8812b7cfac9136a5ee7b423e`.
Zig 0.15.2 production build, 2026-11-09 21:09:28 UTC.
See the [validation receipt](renderer-throughput-20261109-r73-validation.json).

## Change

Point PCSS visibility and its lighting correction now run in one shader.
The previous path wrote a half-resolution FP16 visibility target, switched
back to the lighting target, then sampled that same pixel. The fused path
keeps the cube bound and performs one additive RGB draw into the existing
target. It needs no RT0 query/switch, point visibility allocation or pair of
directional filtering shaders. Directional shadows retain their own shared
visibility target when required.

Distance/near rejection is shared and happens before material decoding.
Contribution relevance is tested before expensive PCSS work. The original
four base samples, sixteen blocker samples and up to sixteen filter samples,
cube resolution, PCSS radius and geometric-normal bias are retained.

The base four-tap loop remains a loop in SM3 bytecode. A direct inline
candidate exceeded the 512-slot limit; reusing the distance/range checks
lets the selected fused shader fit at 511 slots and 18 temporary registers.
The existing compiler, strict arithmetic, cube surcharge and SM3 gates remain.
The ordinary PCF shader occupies 353 slots instead of 469; this is a bytecode
footprint observation, not a claim of fewer executed GPU instructions.

The ordinary PCF fallback survives unavailable/failed optional shader binds,
and the fused shader follows existing Reset retention/retry rules. Point PCSS
no longer depends on directional visibility allocation/backoff.

`LocalLightingSoft` remains byte-identical for replay of older captures that
contain the resolved texture. Production uses `LocalLightingPcss`. New captures
record the exact cube binding and sampler state, referring to the six faces
already exported by `pointSources`, without duplicate cube readbacks.

## Verification

The real point-light submission path is compared with the frozen baseline
resolve/consumer shaders on native D3D9 and DXVK 1/2/3. Each backend runs 96
RGBA comparisons: even/odd grids, FP16/FP32 destinations, near/far/sky/water,
different light ranges, grazing/reversed normals and material-detail inputs.
The permitted test bound is one FP16 step plus 2e-6; all 384 cases actually
match exactly, with zero changed components. Alpha remains exact.

Device interception confirms 768 baseline draws versus 384 fused draws, two
RT0 switches per baseline case versus zero for the fused call, zero RT0
queries and no auxiliary texture creation in the fused call. Shader-bind
failure falls back to exact ordinary-PCF output without invalidating the cube.

The prior PCF, resolve and legacy-consumer binaries also pass 48 independent
shader-oracle cases each on native D3D9 and DXVK 3 (including MSAA 0/4), with
zero changed components. Full PCSS contact/penumbra, toggle and fallback
fixtures pass on native D3D9 and DXVK 2/3. The legacy directional probe fails
its finite-[0,1] check on DXVK 1; it uses unchanged world-effects source.
The independent `--fusion-only` comparison retains all point-pass checks and
passes on that backend. This does not establish directional-probe support
there.

Actual stock Stormwind/Dalaran lamps with a 620-triangle, 46-bone stock
character pass four soft-consumer functional cases on native D3D9/DXVK 3.
Controls cover three poses, moving/articulated silhouettes, a separate box,
retained foreign static casters, actor-off and removal. These are controlled
stands, not gameplay or the complete city matrix.

Production Reset checks pass on native/DXVK 3 for retention on/off, failed
Reset, two retries, source mutation and current poses. Capture tests pass
on the same backends with exact depth/state/constants, 22 API-failure controls
and complete cube-input metadata for the fused consumer.

The user authorized tests beside a running WoW. All GPU timestamps from the
small shader stands are excluded from performance conclusions. No isolated
GPU-time or game-FPS gain is claimed. The exact castle-video task remains
deferred. The newly supplied base-game shadow-pop video is a separate follow-up.

## Delivery

`0.3.217-test.20261109.7`: verified production DLL and Windows installer,
with identical DLL payload and all 17 expected exports/ordinals. Sizes and
SHA-256 values are in the receipt. No client installation or game launch.
