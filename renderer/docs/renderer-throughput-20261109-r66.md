# r66: production native MRT model normals

Native model draws now write model normals into the existing FP16 normal
texture alongside their native color output. The renderer enables this path
automatically; normal capture still requires its existing material consumer.
Weather or AO alone does not acquire a new model-normal pass.

## Integration

`model_normal_mrt_patch.h` extends the final submitted SM3 pair. It preserves
every original instruction and output, including native color, alpha coverage,
discard and position/depth. One free TEXCOORD carries the post-skin normal and
the actual clip W. The appended PS tail writes a normalized receiver below
the existing 48-yard boundary, or zero. It reuses dead native temporaries.
The VS supports relative bone-palette addressing without allocating constants.
Unsupported control flow, register pressure and exhausted varyings are refused.

The hook runs inside the active POM/displacement/local-light submission scopes,
after their final shader pair is selected. Original shader identity, active
material descriptors and the local-light factory's exact active pair establish
provenance. Private shaders do not replace identities used by geometry or shadow
classification. The local-light factory restores its pair once; MRT avoids
restoring that same pair immediately before the factory does so.

`model_normal_native.h` manages a frame transaction over the existing normal
surface. It checks depth writes, current target/depth/viewport, MRT capabilities,
unused auxiliary outputs and bound slots. Depth-writing non-model occluders
write zero ownership, including those submitted after the last captured model.
Non-depth-writers preserve ownership. All modified RT, mask and shader state
is restored; an actual game draw is never retried after submission.

The immutable capture queue remains a rollback journal. At finalization, a
fully covered MRT frame commits and drops the queue without normal replay.
Any unsupported draw, occupied outputs, unsafe blending/sRGB, changed scene
depth, surface writes, texture hazards or failed binding discards the entire
MRT result and uses the existing full replay. Mixing partial native normals
with replay into a fresh private depth buffer remains prohibited: r65 proved
that this loses near-surface ownership.

Admission starts with a preflight replay frame. Each frame prepares at most
one cold MRT shader pair. Newly encountered pairs discard that frame's partial
MRT result. Repeatedly unsupported scenes stay in preflight and do not
repeatedly pay both GPU paths. Shader and negative-result caches are bounded
to 256 source VS / 512 source PS, retain identities and clear on reset.
No second full-size normal texture or MSAA normal plane is allocated.
MSAA and non-SM3 scenes retain full replay. The same policy is used on all
backends. Existing immediate capture remains when the resource-write guard
contract is unavailable.

`MODEL_NORMALS MRT` logs admission, committed packets, receiver/occluder draws,
cold shader builds and the fallback reason. `committed` is meaningful together
with these counters; a shorter isolated ModelNormals timer does not measure
the work moved into native game draws.

## Validation

`tests/test_model_normal_native_gpu.py` exercises the production queue, final
shader factories and actual D3D9 draws. Final artifacts are in
`out/perf-20261109-r66/validated/validation.json`, with fixture, source, test
and backend hashes and per-backend logs.

- 3,210 frames per backend, 12,840 total: native D3D9, DXVK 1.10.3, packaged
  DXVK 2 and DXVK 3. Each backend commits 810 frames.
- Rigid, skinned, weighted and four-weight stock VS, plus an actual local-light
  VS; SDR and FP16 color; alpha coverage; plain, POM and GPU displacement.
  POM/displacement use neutral height controls to compare the two normal paths
  exactly. The local-light test executes the real single/dual/triple factory.
- Native color is byte-exact. Model normals are byte-exact when no native
  occluder owns the pixel. Occluded pixels correctly become zero regardless
  of occluder draw order, while native color remains byte-exact.
- Occupied RTs, MSAA, depth clears, another draw domain writing the scene DS,
  sampled-texture hazards, reset and injected before/after binding failures
  all preserve whole-frame rollback, state restoration and single submission.
- Existing draw-hook equivalence, terrain bytecode/visibility/state suites
  and optimized/sanitized normal slicing tests pass. The complete renderer
  translation unit compiles; no DLL was linked or installed for this task.

Additional legacy native stress: rigid, skinned and weighted programs pass
the older full replay/G-buffer suite with canonical vertex sharing enabled.
Its four-weight native run fails at `GetRenderTargetData` with
`D3DERR_DEVICELOST` (0x88760868), including a retry. The same failure reproduces
with the frozen pre-MRT header (`legacy-reference-weighted4.log`), so this is
not evidence of an MRT regression. The separate native MRT four-weight oracle
passes on every backend. The older SM2 full native stress also hits this
readback failure; it is not reported as passed. Logs remain in the r66 folder.

## Queue decision

The journal is retained deliberately. A late occupied RT, shader refusal,
texture-write hazard or binding failure still requires complete normal replay
for earlier native draws, with their original geometry, poses and coverage.
The current scene depth cannot recover those inputs. Replaying only the late
exception into fresh private depth is the near-ownership failure established
in r65. A universal immediate fallback against native depth is not implemented
or validated for all private materials, alpha tests, depth writes and MSAA;
removing the journal would weaken the existing whole-frame rollback guarantee.

An isolated negative control in `out/perf-20261109-r66/journal-control.py`
discards geometry after successful native MRT submission. A later occupied-RT
refusal then fails the production rollback oracle, as expected. The control
uses a temporary header; production sources are unchanged. Its result is in
`journal-control/validation.json`.

This is a bounded rollback queue rather than a second copy of every model.
Shared source meshes are referenced; warm resident geometry retains GPU leases
and draw metadata instead of CPU source bytes. Constant banks reuse proven
unchanged inputs. At successful MRT commit, `clearPending` releases geometry,
declarations and sampled-texture references, clears texture-write guards and
recycles packet storage within the existing 8 MiB allowance. It executes no
normal replay. Queue removal is not claimed as an optimization in this change.

The local-light factory admits one cold pair per frame (single, dual, triple).
The color reference is taken after that warm-up; comparing its first frame to
the warmed triple-light frame would compare different lighting recipes.

## Complete-path timing

Measurements include queue capture, native submission, state transitions and
resident replay or MRT commit. They use resident geometry, a 3840x2160 target,
SDR/FP16 targets, alternating warmed blocks, timestamp queries and CPU wall time.
The heavy fixture repeats the 544-vertex stock model sixteen times within each
draw (8,704 vertices). It is synthetic, not a game-FPS measurement. Idle repeats
were taken after compilation finished; the timestamp interval can include
command starvation and is not an isolated shader duration.

For 256 draws, SDR medians in milliseconds:

| Backend | GPU replay | GPU MRT | CPU replay | CPU MRT |
| --- | ---: | ---: | ---: | ---: |
| Native D3D9 | 2.210810 | 1.192960 | 0.598100 | 0.645400 |
| DXVK 1.10.3 | 2.255872 | 1.406976 | 0.320300 | 0.331500 |
| DXVK 2 | 1.490944 | 0.745472 | 0.275100 | 0.293200 |
| DXVK 3 | 1.491968 | 0.763648 | 0.280000 | 0.304700 |

The measured GPU interval improves about 38–50% for this heavy case, while
CPU submission costs up to 0.05 ms more. Small geometry can be dominated by
per-draw RT transitions and may be neutral or slower; the earlier small-mesh
measurements in `out/perf-20261109-r66/final` show that limit. Queue capture has
not disappeared. No claim is made that this removes EVENT stalls or guarantees
an in-game FPS increase. The next game log should establish actual MRT coverage,
fallback reasons and the complete GameScene + ModelNormals cost.
