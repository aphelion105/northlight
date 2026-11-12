# r77: retain native normal capture across blended depth writers

Baseline: `e9728f0cefba6291a266136b9bf7a3a27a6b53fa`.
Production source identity:
`532a6876e6e74a3e08a447224673fff4d73ca181f68368ae9c88733c3a287771`.

The new rotation QA uses the r73 DLL (source identity
`0a26077130a6a384c86a6767e0add00c5699c4fb8812b7cfac9136a5ee7b423e`),
DXVK 3.1.1, AMD, 2560x1440. Across its 82 interval records, the median
window mean is 33.251 ms with effects and 16.173 ms without effects.
Enabled CPU samples have median capture 10.36 ms and effects submission
4.41 ms. GPU timestamp spans can include command starvation; they do not
prove that every spike in a short effect is shader execution.

All 41 MRT records with work roll back with `blend-srgb` (34 other records
have no probes). A late sample replays 678 submitted model draws after
merging 804 queued draws.
The old reason combines two conditions, so this log does not identify
which state triggered each rollback. YouTube loaded the page but no video
buffer; the exact rotations were not visually verified.

## Change

Opaque receivers retain their existing normal XYZ and linear depth W.
Other depth writers output invalid zero RGB with alpha one in the existing
normal target. Under ADD with source ONE/SRCALPHA and destination
ZERO/INVSRCALPHA, this clears covered normals independently of native
colour/alpha. Supported blended occluders can therefore preserve the MRT
transaction and avoid the separate normal replay. Blended receivers,
sRGB writes and unsupported blend operations retain whole-frame replay;
their diagnostic reasons are now distinct.

The private FP16 target uses `ColorFill` before binding it. This avoids
switching all native render targets merely to clear that texture. Drivers
that reject the fill retain the previous save/Clear/restore path.
No new target, mask, normal encoding or compiled shader asset is needed.
HDR sky capture, materials and local lighting keep separate outputs and
calculations. Two repeated `active()` branches in material admission are
also removed; the initial frame-latched check remains.

## Validation

Native D3D9 and DXVK 2/3 each pass 6,558 transaction cases: 19,674 total.
Native RGBA and HDR sky output match exactly; valid normal/depth lanes
match exactly, while invalid zero-XYZ pixels ignore W. Controls include
four accepted blend pairs, rejected additive/subtractive/sRGB states,
non-depth writers, POM, displacement, triple local lighting, occupied MRTs,
viewport/depth/target changes, MSAA and failures before/after mutation.
Each backend also passes 60 forced `ColorFill` failures through `Clear`.
The WorldNormals consumer rejects zero XYZ with W=1. Its existing pose,
range, viewport and state checks pass. Material replay passes 275 cases.
The Zig 0.15.2 production build and Windows package
`0.3.217-test.20261110.2` pass freshness, payload and export verification.

Three alternating before/after pairs on RTX 4090 / DXVK 3.1.1 measure 256
resident draws at 3840x2160, followed by a source-alpha depth writer.
Values are median milliseconds for capture, native submission and replay
or MRT commit only, excluding the rest of the game and effects:

| Receiver / vertices per draw | HDR | GPU before → after | CPU before → after |
| --- | --- | --- | --- |
| Plain / 544 | off | 0.141312 → 0.104448 | 0.2968 → 0.3037 |
| Plain / 544 | on | 0.154624 → 0.117760 | 0.2970 → 0.3000 |
| Plain / 2176 | off | 0.422912 → 0.240640 | 0.2926 → 0.3019 |
| Plain / 2176 | on | 0.434176 → 0.252928 | 0.2997 → 0.2980 |
| Three local lights / 2176 | off | 0.564224 → 0.347136 | 0.5290 → 0.4579 |
| Three local lights / 2176 | on | 0.563200 → 0.358400 | 0.5101 → 0.4557 |

The small plain case has a slight CPU cost; this is not a universal CPU
win. These controlled subset results establish neither AMD timing nor
whole-game FPS, and do not establish that every rotation stutter is fixed.
Raw QA, shader fixtures, binaries and detailed measurement receipts stay
in ignored `out/rotation-20261110-r77/`.
