# Wintergarde Keep player shadow report, 18 November 2026

The supplied 2560×1440 Northlight capture shows the player on the tower stairs
at renderer frame 21710. The local-light pass removes the sampled torch's full
direct contribution on the player. The model already has visible color in the
native scene image, before Northlight adds shadowed local light.

## Captured receiver check

The archive contains the compact lighting and native draw journals, depth,
normals, point cubes, per-pass constants and images. There is no standalone
renderer log in the ZIP. The available local log's captured view is from a
different camera position, so its startup identity does not identify this frame.

Replay `LocalLightingPcss` with the exact captured shader, constants, depth,
normals, baseline, material input and six cube faces. The bytecode SHA-256 is
`143284332c572508d0049f69425c1a0a17d742b7e6e4c9f8e435d620b77e5712`,
which also matches `shaders/compiled/LocalLightingPcss.bin`. On native D3D9 and
DXVK, the actual correction matches the forced fully blocked control exactly
at these points. A white cube produces zero correction.

| Full-size pixel (x, y) | Surface | Pass 10 red correction | Fraction of forced-blocked correction |
| --- | --- | ---: | ---: |
| 1280, 704 | Helmet | −0.184326 | 100% |
| 1280, 741 | Shoulder | −0.153442 | 100% |
| 1274, 780 | Upper body | −0.058563 | 100% |
| 1280, 900 | Cape | −0.023636 | 100% |
| 1280, 945 | Cape | −0.025024 | 100% |

`lighting-after-local` shows the positive direct term at those same points.
`current-local-light` retains at most 0.000367 per RGB channel there after all
local passes. The torch
visible at the left of the screenshot is a different source (pass 12, cube
slot 5). Its wall sample (717, 449) and lit floor sample (551, 684) keep their
direct light, while (725, 630) matches its fully blocked control.

The nearby skinned draws use native directional RGB `(0.506, 0.533, 0.659)`
and ambient RGB `(0.282, 0.298, 0.369)` in VS c10 and c11. Their point-bank
journal marks all four native slots as `renderer-owned-direct`, including the
slot for the source in pass 10. The existing receiver contract preserves the
independent native room lighting when it applies local-light visibility. See
[interior lighting and local-light ownership](interior-lighting-20261116.md).

## Reproduction

The archive SHA-256 is
`d7b7e11279debc093e4b22c4d599418f940a987c31590c371f3c1b860d3e6dce`.
Unpack the compact session with `scripts/capture_files.py unpack`, then use
`tests/captured_point_pass_gpu.cpp` with the `capture-1-` prefix, one pass per
invocation, and the shader index from the journal's `shader-binding` entry.
Use `(firstPass, count, cubeSlot)` of `(10, 1, 3)` and `(12, 1, 5)` to reproduce
the spatial controls. For the 1280×720 correction buffers, take the integer
half of each listed full-size coordinate. The original archive and local
replay report are under `.local/out/player-lamp-regression/`.

The result establishes local receiver behavior for the sampled frame. A
comparison against earlier native room lighting requires a matching older
capture. The draw journal does not contain a complete scene replay.
