# r90: accumulated CPU/GPU work reduction, sources only

Base: r89, `dab43d387ff6e8a8f58a609aeb0d20996d41cf8d`.
Source identity: `bd5d45b8e0f4c64cbf6dc6681da255ade2951215f9a024b4f4692c7b7408873b`.

No production DLL or installer was built, as requested. An isolated renderer
object was compiled to check integration without linking; executable GPU/CPU
fixtures were built separately. No DLL was installed and no game was launched.

## Changes

### Remove earlier identical normal draws

The private ModelNormals replay uses opaque overwrite, depth writes and
`LESS_EQUAL`, without stencil or blending. Keeping the last identical draw
preserves both the final minimum depth and the last writer at equal depth.
The previous implementation retained the first and last copies, even when
only two identical copies existed. It now skips every earlier identical copy.
Packets remain alive until the batch ends because other packets can borrow
their captured constant banks. The obsolete `laterDuplicate` flag is removed.

The existing 4,096-slot table previously hashed geometry without pose. Different
instances of one model displaced each other before an identical pose returned.
The key now samples three captured root-palette rows, or the last captured rows
for another constant layout, and mixes high bits into the table index. A hit
still requires the complete existing geometry/state/constant/texture comparison.
Collisions only miss an optimization; hashes never prove equivalence.

No game color draw is removed. Alpha coverage, raster state, pose, normal/depth
output and original ordering among the remaining draws stay intact.

### Reuse pending material requests in the existing source index

A source whose disk-map request is pending can occupy an ordinary cache entry
with a null normal map. Repeated calls for the same source, recipe and upload
revision return that frame's pending result through the existing lookup.
They avoid repeating texture descriptions, mip accounting, admission and disk
request traversal. Positive and pending insertion share one cleanup path.

Pending entries retain the source, charge its bytes to the existing 128 MiB
source allowance, obey the existing 256-entry limit and allocate no GPU target.
Their LRU use value is zero; they are evicted before usable maps and do not
protect the visible working set. They cannot block admission of a ready map
under a full source budget. Every pending entry expires at `endFrame`.

Upload-revision changes and settings/reset invalidation still apply. The
two-map-per-frame generation/upload limit is unchanged. A job completing in
the middle of a frame becomes usable on the next frame; this intentional
at-most-one-frame readiness delay keeps that texture's appearance consistent
within the frame. No additional cache, target, configuration or budget is added.
`AUTONORMAL pendingCache` distinguishes placeholders from actual cached maps.

### Preserve native MRT across trailing depth-only draws

Draws with no RGB writes, including fixed-function draws with mask 0 or
alpha-only mask 8, need not erase captured model normals. Their stored distance
is checked against final scene depth by the existing consumer. These draws can
now pass through without cancelling an otherwise complete native MRT frame.

A frame-local transaction flag preserves rollback if a later RGB depth writer
appears: it could fail the native depth test against the invisible occluder but
pass replay's private depth test. Interleaved cases therefore retain the full
old replay path. Failed native submission also cancels the transaction.

This removes a whole replay only for the supported trailing case. The latest
gameplay log reports `fixed-pixel` without its RGB mask, so its frequency and
the resulting gameplay saving cannot be inferred from that log.

## Measurements

The capture fixture contains a frozen r89 implementation and the current one
in a single executable, sharing the production mirror, immutable source pool
and resident geometry store. Both omit phase probes. Four warm-up blocks
precede six measured ABBA blocks per side, 32 frames/block, at 4K/MSAA2.
The workload submits 133 near and 310 far stock streetlamp draws; the near
poses repeat at 13 positions. It has no AutoBump producer or pose handoff and
is not a recorded gameplay draw stream. GPU timestamps cover replay, not
preceding native draws, and can include command starvation.

| Backend | Replay draws | Replay GPU interval, ms | Replay CPU submission, ms | Fixture total CPU, ms |
|---|---:|---:|---:|---:|
| Native D3D9 | 133 → 13 | 0.022530 → 0.008190 | 0.043250 → 0.033950 | 0.348800 → 0.339800 |
| DXVK 2 | 133 → 13 | 0.032768 → 0.012288 | 0.048050 → 0.021750 | 0.334925 → 0.307150 |
| DXVK 3 | 133 → 13 | 0.033536 → 0.019456 | 0.032925 → 0.021100 | 0.294475 → 0.283425 |

Replay GPU intervals improve **2.75× / 2.67× / 1.72×** respectively. The
fixture's complete CPU improvement is much smaller; capture and native
submission still occur. Nineteen readbacks/backend are bit-exact against r89.
All 133 near captures remain queued/resident, all 310 far draws remain culled,
and warm source reads remain zero bytes.

A second same-executable ABBA fixture measures 512 normal-cache requests/frame,
including frame-end cleanup. Sources are real managed textures, disk admission
is deliberately held pending, and no GPU work occurs in the timed pending loop.
DXVK 3 CPU milliseconds:

| Distinct textures | Previous pending requests | Current pending requests |
|---:|---:|---:|
| 1 | 0.018977 | 0.002685 |
| 8 | 0.022774 | 0.013663 |
| 32 | 0.023056 | 0.016698 |

The one-texture case improves about **7.1×**; the mixed cases improve **1.7×**
and **1.4×**. Native D3D9 and DXVK 2 also improve in these pending cases.
Already-ready one-texture hits cost 0.002600 → 0.002988 ms per 512 requests;
8/32-texture warm hits are within approximately 2% of the control. The extra
null-map check has a small warm-hit cost; it is not hidden by the pending result.
These standalone timings do not include the game's resource wrappers.

These are component results, **not** a multiple-fold speedup of an entire
gameplay frame. The overall 2–3 ms overhead goal remains unverified. The new
source bundle has deliberately not been packaged or installed for a game run.

## Validation

- ModelNormals native/DXVK 2/DXVK 3 oracles pass with exact normal/depth pixels,
  including 32 last-copy cases for near/equal/far/subtexel depth, alpha clipping
  and depth bias, plus geometry/state/constant mismatch controls.
- Native MRT passes **7,878 scenarios/backend**, including HDR, material/local
  light composition, trailing mask 0/8 draws, interleaved rollback, target/depth
  changes, pre/post-call failures, color fills and exactly-once native calls.
- Pending-map tests cover source ownership after caller release, revision and
  recipe separation, next-frame ready uploads against fresh GPU pixels,
  settings/reset, full source budgets and eviction before a ready map.
- Normal target reuse/lease/visible-working-set tests pass on all three backends.
  Native and DXVK 3 disk tests pass RGBA/DXT1/DXT5, process restart, settings,
  corruption repair, mip reload, busy readback and upload retry cases.
- Extracted actual normal-cache implementations pass **12,566 comparisons/run**
  against the earlier linear/indexed references: O2 and ASan/UBSan, both normal
  and forced-collision hashing, including allocation failures and frame wrap.
- The complete `renderer.cpp` translation unit compiles as an isolated x86
  object; no DLL link or package step runs. All 191 precompiled shader binaries
  remain unchanged from r89.

Local evidence: `out/optimization-20261110-r90/`:
`final-checks.json`, `capture-ready-{0,1,2}/profile.json`,
`native-mrt-verified/validation.json`, `pending-final-all/validation.json`,
`batch-validation.json` and `normal-index.log`.

Reproduce capture A/B with `tests/profile_base_capture.py --renderer-reference
<r89-model_normal_renderer.h> --backend <dll> --output <fresh-directory>`;
pending-map A/B uses `tests/test_material_pending_frame_gpu.py --reference
<r89-auto_normal_cache.h> --backend <dll> --output <fresh-directory>`.
References are byte-exact headers from the base commit. The ordinary GPU
oracles also run without reference headers. Only test artifacts are generated.

The existing r89 `test.20261110.15` DLL and installer are unchanged:
DLL SHA-256 `2fa39365b7d18f716c59c3382f336ca66b893d2ff09e48a23590ccbd2a413ad7`;
Windows ZIP SHA-256 `b388239f572c8f6baa2e040da51df56238e1cf4bff37eac0de6abee43fd7455e`.
