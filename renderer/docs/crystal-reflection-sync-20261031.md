# Dun Niffelem crystal reflection shimmer

The reported Storm Peaks view is near map coordinates 62.4, 60.7. The local
`frostgianticeshard` assets have an opaque unlit base followed by two
SRCALPHA/INVSRCALPHA reflection passes over the same submesh. Displacing only
the base makes those authored layers intersect it as the camera moves.

The existing M2 overlay synchronization admitted only one SM3 lit base and one
unlit additive overlay. It now also admits the exact audited SM2/SM3 single-bone
lit/unlit `diffuse_t1` and `diffuse_env` programs and alpha blending. Reflection
UVs still come from the original shader; the displaced mesh, height texture and
strength come from the successful base draw. Environment UV generation need
not qualify for independent height inference. Source identity, declaration,
draw range, clip matrix, used bone transforms and frame lifetime must match.

`tests/test_crystal_overlay_gpu.py --backend <plain-D3D9-or-DXVK-DLL>` extracts
the original shard04 topology, texture and shader variants from local MPQs.
Its hidden-device oracle checks 12 camera rotations, SM2 CPU height and SM3
CPU/GPU height, lit/unlit shaders, alpha/additive blending, exact overlay depth,
constant restoration and refusal after a pose change. Each of native D3D9,
DXVK 2.7.1 and DXVK 3.1.1 passes 144 cases. Unsynchronized native geometry
fails the depth comparison in all 144 cases on each backend. Compiling the same
test against the saved pre-change production headers fails overlay admission.

Geometry, displacement state reuse, private capture depth and material batch
binding regressions pass. The x86 DLL also builds from isolated commit
`66d0e6ed` plus this fix. Isolation avoids unrelated edits occurring in the
shared working directory during validation.

The fixture scales the authentic mesh into an authored camera setup and uses a
simple UV pixel shader for the depth oracle. It does not replay the complete
game frame or certify the screenshots' final appearance. No game was launched;
installed DLLs, INIs and MPQs were not changed. Extracted assets remain ignored
local test output.

## 2026-11-01: rewritten model buffers

An unchanged tracked VB/IB rewrite can reuse private displacement geometry
whose source snapshot still carries an older write revision. Overlay matching
previously compared against that cached revision, rejecting the current
reflection layer even though the successful base used the same mesh.

The prepared draw now retains its current source identity before geometry
canonicalization. A successful base records that identity separately from its
cached mesh. Overlay admission still requires the exact current source,
declaration, draw range, clip matrix and pose; a write after the base is refused.

The expanded crystal fixture passes 576 cases each on native D3D9, DXVK 2.7.1
and DXVK 3.1.1. It covers unchanged buffers and alternating equal-content VB/IB
rewrites, verified geometry aliases, immediate/deferred bindings, camera/pose
changes and writes after the base. The preserved pre-fix header fails the same
overlay-admission assertion. Existing displacement state and material binding
checks also pass. This remains an offline GPU reproduction, not an in-game
before/after acceptance.

The DLL is built from an isolated source copy because concurrent work changed
the same overlay implementation during validation. That separate generalization
is preserved in the working directory and excluded from this tested DLL. The
isolated source identity is
`f5ea8cccea37a7af9f332bb642556aac86ad138c86740e6cb3c4880118bacf03`.
DLL SHA256:
`229d2666177f922e5f561ab0229038f338a48ee154226f0c9dfe32a4e5c5e7eb`.
It was installed in the configured local client with WoW stopped. The prior DLL
is retained under `renderer-backups/crystal-provenance-20261101-115003-UTC/`.
