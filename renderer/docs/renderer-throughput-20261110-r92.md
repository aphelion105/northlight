# r92: skip unused material capture for opaque shadows

Base: r91, `f7441652d684b1f79c34ad330745404f626c34c0`.
Source identity: `7c7c3bea1192e1bbecde8ade1b18a9090d2748c540a96a33a7513c6965e590e8`.

Sources only. No production DLL or installer was built or installed. A separate
test executable includes and compiles the renderer translation unit. The
existing test.20261110.15 DLL/ZIP and all 191 shader binaries are unchanged.
No game was launched or stopped.

## CPU change

World model capture previously called `GetTexture(0)` and read both sampler
address modes for every accepted draw. Ordinary opaque shadows do not need
those inputs for their result. Capture now omits these reads and the packet's texture
reference when the existing opaque-material proof applies and actor GI is not
updating. A 512-draw eligible batch removes 1,536 API reads and 512 texture
AddRef/Release pairs. Recycled packets use deterministic default addressing.

Fixed alpha tests, shader cutouts, grass masks, the existing float-alpha guard
and actor-GI update frames retain the actual texture and address modes. The
decision to capture actor GI is computed once in `captureModel` and also gates
its sole `appendActor` call, replacing the repeated eligibility call inside
that helper. GI packet construction, its update cadence and shadow selection
remain separate. No new cache, target, shader or production test switch exists.

The available gameplay log is still r88, as identified in the r91 report. Its
655 world captures at frame 635 motivated inspecting this path. That log does
not measure r92 or identify the eligible opaque fraction; no gameplay-frame
gain is extrapolated from the component test.

## Controlled CPU results

One executable, 512 repeated rigid captures per frame, including immutable
source acquisition, packet recycling and the actual WorldRenderer capture,
under the mirror gate. Each process warms 16 frames and measures 32. Four
before/after process pairs alternate order for every scenario and backend.
Values below are medians of those process medians, in milliseconds.

| Backend | Opaque, GI off: before → after | Opaque, GI enabled between updates: before → after |
| --- | --- | --- |
| Native D3D9 | 0.173975 → 0.158300 | 0.175300 → 0.158475 |
| DXVK 2 | 0.140775 → 0.127375 | 0.137925 → 0.126500 |
| DXVK 3 | 0.138400 → 0.125550 | 0.137650 → 0.126275 |

Savings are 11.4–16.8 µs per 512 captures, or 8.3–9.6% of this fixture's CPU
capture work. Game draws, other extension stages, GPU timing and Present are
outside this measurement. It does not establish a whole-extension frame budget.

The old-policy control is injected only into the test's frozen header, forcing
the original material reads. Both policies share the same executable, geometry
and capture implementation; the shared GI-admission cleanup is common to both.
Compilation and sanitizer tests do not overlap the timed runs. The runner can
reuse its executable only after checking its hash, source identity, C++ fixture
hash and the exact overridden header.

## Correctness and protected controls

All **120 processes / 60 paired comparisons** pass on native D3D9 and DXVK 2/3.
Every pair has identical captured geometry/used constant bytes and exact output
for all six point-shadow faces. Fixed cutouts must produce a different shadow
map from the opaque control, so this check exercises real alpha coverage.

The five cases are opaque with GI off, fixed cutout, actor-GI update due,
negative float-alpha texture with the guard active, and GI enabled between
updates. Captured texture/addressing are asserted in each case. The GI-due case
checks material retention and job admission; it does not execute a GI worker or
claim a GI-image comparison.

For the three protected cases, the observed before/after deltas range from
−1.975 to +1.125 µs per 512 captures. No speedup is claimed for those paths.
The original material reads remain active.

Existing checks also pass:

- `test_actor_shadow_radius_wiring.py`: GI packet capture/publication remains
  independent of shadow selection.
- `test_replay_draw_state.py`: O2 and ASan/UBSan checks for replay material,
  alpha recipes, float-alpha safety, equal-depth ordering and state-cache
  invalidation/failure behavior.
- Layout, Python syntax and whitespace checks; production artifact and shader
  binary hashes remain unchanged.

Receipts: `out/optimization-20261110-r92/capture-balanced/validation.json`,
`verified-results.json`, `actor-wiring.log` and `replay-draw-state.log`.

## Reproduction

Run the GPU fixture outside the restricted Windows compiler sandbox. It uses
authored geometry/shaders and never starts the game:

```text
python tests/profile_world_material_capture.py --backend <native-d3d9> --backend <dxvk2-d3d9> --backend <dxvk3-d3d9> --output out/r92-capture
python tests/test_actor_shadow_radius_wiring.py
python tests/test_replay_draw_state.py
```

The last test needs native clang with ASan/UBSan; here it ran in Ubuntu-24.04
through WSL. Performance comparisons run serially, without other compilation
or sanitizer checks running, with four repetitions per scenario by default.
