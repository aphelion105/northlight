# Point-light ownership investigation, 2026-11-12

Status: the source-discovery failure is reproduced and repaired against the live
instance; the reported gameplay defect is **not yet accepted as fixed** in the
installed renderer. The user reproduced it on the new build in two captures on
2026-11-13. The native draw producing the changing ground patch was not
captured. No component test below establishes gameplay correctness or FPS.

## Capture evidence

### Installed candidate: source recovery confirmed, image defect remains

`session-57928-188439812-0`, captures 1 and 2, uses source identity
`7530b04d406ede3f2f47771340107f5e72611910625fd8973e73465a56489a1e`.
Both native snapshots contain the target ID `18347664881907401985`, flags 3,
at `(1550.90344, -4324.98291, 24.5650005)`. Its selected light is
`18356672081162141723`, flags 2 (`CastShadow`). Point slot 0 is ready and usable
with a completed six-face publication in both frames. Thus the earlier missing
source and missing shadow selection are repaired in the installed game.

The orange ground patch remains camera dependent: dark in capture 1, bright in
capture 2. The difference already exists in `hdr-base`; `current-local-light`
contains local illumination in both frames. Each journal records 124 admitted
native draws and four image pairs, with no terrain handoff. These are insufficient
to identify the native draw that produced the patch. This is a failed live
acceptance, regardless of the passing source and component tests.

The next diagnostic build adds a main-depth submission census outside receiver
classification. It records original shaders/constants/states and before/after
384-pixel-wide images for up to 2048 submissions, including unclassified and
fixed-function programs. Exceeding the cap marks the capture incomplete.
GPU tests check exact shader/constants, fixed-function admission, MSAA image
colors, unchanged device state and readback failure reporting. The trace is
explicitly diagnostic: synchronous readbacks affect frame timing and observation
ages. It is not an FPS result or complete scene replay.

The first trace build (`71415860`, `session-52412-190417453-0`) still began
after terrain: 135 and 208 submissions, with the orange patch already present
in the first `before` image of capture 1. The common adapter still required
`projectionValid`, and `worldDepth` was cleared every frame. An unknown early
terrain program therefore remained outside the census. The renderer log also
reports zero terrain draws despite recognizing one terrain shader at creation.
This establishes missing early admission, not the identity of the terrain draw.

The census now admits a full-size depth-tested target directly, before shader
projection discovery, and exports fixed-function transforms/material/lights as
well as programmable banks. A regression calls the actual common submit method
with `projectionValid=false`; GPU checks exercise admission with bound full-size
targets, missing depth and mismatched dimensions. This change affects diagnostic
admission only; the native ground lighting issue remains unresolved.

### Earlier candidate

Inputs: `D:/Games/335_engb_mod/northlight-diagnostics/session-3604-184481968-0`,
captures 1–4, and `northlight-renderer.log` from the same client run. The run used
renderer source identity `6761e3ce06f35f50eff07091c35f056d51dbe5500eb7911305d06a97123126cf`.

| Capture | Native scene observations | Recorded native draws | Selected target flags |
|---|---:|---:|---:|
| 1 | 28, valid | 124 | 0 |
| 2 | 29, valid | 124 | 0 |
| 3 | 41, valid | 66 | 0 |
| 4 | unavailable | 54 | 0 |

The same selected source `18356672081162141709` appears near
`(1550.9035, -4324.983, 24.565)`, with range 1–12 and RGB
`(1, 0.470588237, 0.156862751)`. It is a draw-derived light, without `CastShadow`.
It is absent from all four native scene snapshots and from the nearby FGL
placement inventory. The log reports zero selected point shadows and zero cube
attempts. These captures cannot show a shadow that was never scheduled.

The ground's orange patch is already present before the first recorded native
WMO draw. The recorded before/after pairs do not identify its producer. The run
reports `terrainDraws=0`; its terrain registration reports SM3 with zero
recognized point slots. This proves missing terrain coverage, not that the
client has no terrain-local lighting. The journals explicitly declare
`whole_scene_replay=false` and `authentic_source_replay=false`.

The medium and tall brazier assets have the same normalized RGB as the target.
Testing their actual M2 files ruled out the proposed bone-0 decoding failure.

Read-only inspection of background client PID 40900 subsequently identified the
actual instance as `TallBrazierNoOmni01`, at model address `2159384656`, with root
translation `(1550.85999, -4325, 21.5900002)`. Its native evaluated light position
is `(1550.9034423828125, -4324.98291015625, 24.565000534057617)`: the target in the
captures. No window/input control, process suspension or game-memory writes
were used. Windows required elevation to read this elevated client.

The installed M2 aliases its single-key timestamp table across seven tracks.
The loaded table contains pointer `0xea5d7de0`, which is unreadable. It equals
`(7 * 1788844832 + 16384) mod 2^32`, consistent with repeated relocation of the
shared pointer. The native evaluated value is valid; the old reader nevertheless
read that unused timestamp before accepting the constant value.

With the timestamp dependency removed for single-key curves, the same instance
appears from sample 16 through 59 (44/44 samples after discovery), with one stable
ID and `RootAnchor | WorldFixture`. The before run has zero matching samples.
These diagnostic runs freeze each individual read for offline inspection and
are **not atomic game snapshots**. Their raw inputs are under
`.local/out/point-architecture/live-40900-{frozen,fixed}.jsonl.memory.bin`.

## Repaired contracts

0. **Constant tracks do not depend on timestamps.** The common track evaluator
   reads and validates the value without dereferencing a single-key timestamp.
   Unreadable values and unreadable multi-key timelines remain rejected. This
   fixes the observed source-discovery failure without model-name exceptions or
   changes to game assets.
1. **Positive observations do not establish absence.** `Snapshot.valid` means
   coherent copied values. The reader has discovery, metadata, pose, distance
   and capacity limits. An omitted ID no longer immediately kills a known
   light. Scene and draw observations retain its ID, shadow intent and fixture
   tag. Without either observation, the existing 250 ms expiry removes it.
   An explicit zero-color observation or an ownership claim still retires it
   immediately. The binary snapshot format is unchanged.
2. **RGB authority is checked for the actual identity.** A current scene entry
   for that ID supersedes its draw witness. An unrelated entry in a partial
   scene does not forbid handoff. The witness still expires after 100 ms and
   must match the selected source and the receiver's actual native slot.
3. **Native placement bindings have a bounded observation lease.** Missing or
   unavailable samples retain the previous pose and claim for at most 100 ms.
   They then stop publishing. A zero-color observation stops publication
   immediately. Claims and effective source publication share that lifetime.
4. **Fixture proof is independent of luminous-shell geometry.** The former
   catalog admitted only two animated Dalaran fixtures with owned shells. The
   new builder accepts supported constant and animated authored point sources
   owned by an exact GameObjectDisplayInfo entry. Runtime still verifies the
   internal name, vertex bytes and pointer-independent structure, and grants
   fixture intent only to root models. NLF1 v3 permits an empty shell set or a
   subset of proven sources; v2 remains readable with its previous validation.
   Shells control only which emitter surfaces are excluded from self-shadowing.

These changes do not turn arbitrary anonymous draw lights into shadow casters
and do not change native attenuation, point-shadow budgets, shader gates or
the opaque geometry that casts shadows.

## Regression and acceptance boundaries

`test_native_light_snapshot.py` exercises omitted observations, stable identity,
bounded expiry, attachment publication and binary archive compatibility.
`test_native_direct_ownership.py` checks partial-inventory handoff, explicit
same-ID precedence, exact matching, ambiguity and freshness. Both are now in
the `architecture` suite.

`test_native_scene_lights.py` tests unreadable constant timestamps against
unreadable values and animated timestamps, and belongs to `architecture`.
`test_native_world_lamps.py` runs the production reader, classification,
publication and selector on actual Dalaran post/wall, medium/tall braziers, and
the small/medium/tall NoOmni variants. The latter reproduce the invalid unused
timestamp pointer. It covers missing placement data, missing shell data, stale catalog
proofs, parent-attached negative controls, omitted samples and v2 compatibility.
Fixture hashes do not replace live-instance evidence.

Live acceptance still requires all of the following from the same current
build and scene:

- The reported instance is discovered with stable identity and a justified
  fixture classification, including when its mesh is not visible.
- It reaches point-shadow selection; six valid cube faces and the actual
  ground/character receiver consume that source and cube.
- The draw producing the ground patch is identified, including terrain shader,
  original constants and before/after contribution. Zero recorded terrain draws
  cannot count as successful terrain handoff.
- The native receiver and renderer contribution agree on the source parameters
  and ownership for that frame; no light is removed natively without its
  replacement being rendered. The previous selection used during native draws
  versus the later composition selection remains a boundary to audit.
- A matched live camera sequence shows stable illumination and moving caster
  shadows. Offline images and synthetic constant banks alone do not satisfy it.

## Exact ground draw and shared admission (2026-11-13)

The early trace from session `37216-191280359-0` finally starts before ground
rendering. Capture 1 draw 15 creates the orange patch; capture 2 draw 14 draws
the same 2594-primitive surface without it. Both use vertex program
`66fdafb957c26432` and pixel program `ae13943b9382925b`. The vertex bytes match
active `patch-E.mpq` MapObjSpecular_T1 VS3 variants 9/19/29. This is WMO ground,
not a Terrain.bls program. Its identity is absent from the shipped world/WMO
tables. The extra c236..246 light bank is zero in both captured draws.

In the bright draw c20/c24 contains the brazier. In the dark draw that source
is absent from the native four-slot bank. Thus native camera-dependent light
selection changes the ground lighting while the unknown shader bypasses the
renderer camera/capture/native ownership gates. The earlier positive source
discovery and six-face point-shadow selection did not establish receiver use.

`world_shader_contract.h` now proves rigid WMO coordinates from the complete
program. `shader_tags.h` shares this admission with stream recording and world
registration. The geometry path and independently proven native RGB handoff
consume that admission; no capture hash is added to a whitelist. Existing
identity-backed legacy programs retain their path. Dynamic admission does not
authorize unproved fog, alpha, native banks, material families or c10..12 light
fallback. The main camera/target tests still apply at draw time.

The initial active-client regression checked 117 unique WMO programs,
including 21 missing from the old world/WMO admission. It rejects 234 altered
coordinate programs and accepts 117 material edits. The GPU owner regression
includes the actual MapObjSpecular_T1 program and checks that present/absent
native slots produce identical owned lighting, preserved alpha/fog and exact
state restoration on both D3D9 and DXVK. These are component checks; current
live captures after installation remain required for game acceptance.

## Empty captures and world-snapshot admission (2026-11-13, 09:08)

All four captures in session `60528-219590593-1` contain `world_frame=0`,
`passes=0` and zero-filled skipped products. The runtime reports repeated
`CITY context rejected camera=Matrix`, zero full world passes and no effects.
These captures do not validate point shadows or the disappearance fix.

The loaded build reports source identity
`851f278eeb5d9057aea1f07b3b1dc8590f6e2278a6f97a8f11f492ca677c8463`.
Its coordinate admission also accepts active CDiffuse_T1, Diffuse_T1 and
ShadowMap variants, confirmed by matching log hashes to active client bytes.
Coordinates alone are therefore insufficient to trigger the world snapshot.
The producer consumed its sole World trigger on the first matching shader,
without checking depth state or viewport band. WMO context admission had the
same omission; the existing model path already checked those states.

Both producer and renderer now require the shared depth-tested main-world
band before that draw can establish the world camera. Invalidated producer state
is recovered through ordered device Gets; failed reads fail closed. Sky, horizon, shadow/auxiliary and depth-off draws leave the
World trigger available for the subsequent actual world draw. The strict
camera matrix and basis checks are unchanged. `test_world_snapshot_domain.py`
reproduces an invalid sky stack followed by a valid world stack and verifies
recorded-camera playback after live memory is gone. The client corpus also
includes the coordinate-compatible non-MapObj families.

The exact first triggering draw was not exported in the failed live session.
Thus the admission defect is reproduced, but attributing every live Matrix
rejection to it and accepting the original lamp fix still require a new run.
