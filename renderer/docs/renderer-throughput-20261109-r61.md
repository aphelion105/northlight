# r61: conservative early receiver-frustum culling

No new game log is available: the client log remains the r57 run, SHA-256 `435e1213014ffdac3b5d7fdcf8a0c1abc5baaf10e867f920c4b7e069cb6a3ab6`. This iteration extends the existing current-pose handoff for supported skinned M2 receivers. It does not camera-cull directional or local-light casters.

## Change

The material producer already computes conservative current-pose bounds before projection. The handoff now also encloses the native DP4(c2..c5, view.xyz1) clip coordinates and records whether an entire box lies outside one homogeneous clip plane. Each component includes a conservative float-cancellation margin. Bounds touching a plane remain eligible.

ModelNormals consumes this proof before pixel-shader lookup, material/sampler capture, constant snapshots and replay-packet preparation. Rejection requires the same raw mesh, shader, declaration, reset epoch and float/integer/boolean constant versions under the held draw gate. The current declaration and N-patch mode are checked. The new frustum branch additionally requires native clipping enabled and solid triangle geometry. Wireframe, points/lines, unsupported projection, invalid bounds or stale metadata retain the existing path.

The existing 48-yard receiver rejection remains unchanged. Only the new clip flag is consumed with explicit opt-in; other handoff consumers keep range-only behavior. The flag fits existing structure padding: no larger handoff, retained resources, pose cache, new shader or backend-specific policy is introduced. The added cost is four interval dot products when an already-valid handoff is published; no additional pose evaluation or vertex scan is performed.

Capture of geometry that can cast an offscreen shadow remains independent. The world, directional-shadow and point-light source paths are unchanged. The optimization skips receiver work that cannot affect its private screen-sized normal target.

## Validation

Evidence: `out/perf-20261109-r61/validation.json` and its referenced logs.

- The CPU oracle passes 160,019 cases at O2 and O0. Every new rejection is safe against both float and long-double independent eight-corner projection tests. It covers both projection-W signs, large-coordinate cancellation, clip-plane boundaries, invalid values, and source/declaration/shader/epoch mismatches. Range-only decisions and structure size are unchanged.
- Actual displacement-producer state/pose/declaration tests and the existing rigid/single-bone range proofs pass.
- Native D3D9, DXVK 1.10.3, 2.7.1 and 3.1.1 pass the expanded weighted-body GPU oracle. Five new receiver cases are culled: left, right, top, bottom and behind the camera. Stale epochs, disabled clipping and wireframe are retained. Enabled/disabled culling yields bit-exact images. Culled cases skip pixel admission reads. An all-offscreen frame exposes no stale normal texture.
- The full existing skinned ModelNormals GPU suite also passes on all four backends, including alpha coverage, current geometry/pose changes, cold/warm residency, merge ordering and state restoration.

## Measurement and delivery

`MODEL_NORMALS` diagnostics now include `poseViewportTests` and `poseViewportCulled`, reset each frame alongside existing pose counters. These allow the next game run to establish activation and actual savings. The authored GPU cases demonstrate removed work and identical output; they do not establish game FPS gains or solve the remaining EVENT tail.

Changes remain in source and are newer than the delivered r59 DLL. No production DLL or installer is built in this iteration. The installed client DLL, configuration and log remain unchanged.
