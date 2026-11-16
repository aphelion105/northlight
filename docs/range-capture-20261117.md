# Range generations for geometry capture, 17 November 2026

Following `3d78748f`, capture metadata no longer has to invalidate every mesh
when the client writes to another part of the same VB/IB. The latest frozen
gameplay baseline and RAM/GPU inventory remain in
[effects and capture cost](effects-and-capture-cost-20261117.md).

## Implemented ownership and invalidation

Tracked resources lazily allocate eight-byte generation stamps per 4096-byte
page. A capture range uses the newest generation of all intersected pages and
the last whole-resource invalidation. This is conservative at page boundaries:
unrelated bytes on the same page can still invalidate a mesh. Disjoint pages
retain their previous token without hashing or copying vertices/indices.

The page tables and their owners have a shared 2 MiB logical allocation cap.
Budget or allocation refusal keeps the existing whole-resource generation.
The cap excludes allocator overhead and the additional fixed pointer in each
tracked record. Tables live until their resource is destroyed; no extra CPU
geometry, pixel data or D3D resource is retained by this mechanism.

DISCARD, ProcessVertices, Reset of non-preserved resources, unknown writes,
failed locks/unlocks and nested locks retain conservative invalidation.
READONLY and proven identical replay uploads retain their existing semantics.
MANAGED bytes that already survive Reset keep the existing preservation rule.
Generation rewind clears page stamps from the previous epoch.

Lock admission and coarse revision publication now take the same registry
mutex used by metadata reads. No driver call, spell flush, geometry read or
copy callback runs under that lock. Unlock publishes range changes before
making the last outstanding lock cacheable. This prevents a reader observing
a new generation before its outstanding-lock state is visible.

The same requested range is used for initial lookup and post-copy stability:

- Model/material/normal snapshots cover the complete declared vertex span,
  including stride gaps, and the exact index interval. INDEX16/INDEX32 element
  coordinates are resolved from immutable metadata in the existing batch.
- Position bounds cover the declared position span and keep current pose and
  draw inputs separate from immutable source validity.
- Terrain snapshots use their declared position span and exact index interval
  for every fast-hit and post-copy check. Full draw/layout, lifetime, camera
  admission, decoded-index identity and immutable publication remain required.

Changing a constant palette still requires pose evaluation. A stable range
token does not authorize freezing animation, transforms or native lighting.

The upload audit also found flags-0 locks left on fresh DEFAULT static-caster
buffers after their earlier pool migration, and on the one-time point-palette
ID buffer. These now use the existing `FreshBufferLock` before publication.
Static-caster uploads remain disjoint, and the fake-device oracle rejects any
later write after the buffer becomes drawable. Published buffers retain their
existing synchronization rules; these changes do not contribute to the
range-capture timing table below.

## Measured capture component

`test_range_capture_gpu.py --baseline-ref 3d78748f --pairs 7` compiles frozen and
current production headers, then uses actual mutable D3D9 VB/IB resources and
three shared snapshot consumers. Each run has 96 meshes of 768 vertices, 48-byte
strides, separate index ranges and 64 frames. Timing excludes the first frame,
independent byte checks and image readback, and includes source writes, bound
resource calls, snapshot lookup/copy/decoding and position-bounds capture.

The observed seven-pair alternating O2 medians, total ms over 63 frames:

| Writes | Before | Current | Interpretation |
|---|---:|---:|---|
| Two visible ranges plus unused tail pages | 69.991 | 5.258 | 13.3x for this capture component; about 1.111 to 0.083 ms per frame. |
| Immutable sources | 2.565 | 2.895 | About 0.005 ms/frame additional metadata work in this small component. |
| Every visible mesh changes | 71.088 | 71.891 | About 0.013 ms/frame additional work; all sources correctly recaptured. |
| Full DISCARD each frame | 85.131 | 85.486 | About 0.006 ms/frame additional work; no cross-DISCARD reuse. |

Sparse snapshot misses and bounds scans each fall from 6144 to 222. Position
reads fall from 56,623,104 to 2,045,952 bytes. The terrain contract separately
gets 658 tracked hits out of 768 sparse draws, versus zero for frozen headers;
immutable terrain gets 672 hits and dense/DISCARD terrain gets zero.

These are controlled geometry snapshot/bounds measurements, not an entire
draw-hook, animated-pose, effect-GPU or game-FPS multiplier. Background clients
can affect absolute timings. The sparse case is deliberately useful to range
tracking; the gameplay log must establish how often that condition occurs.
Benchmark reports preserve all individual samples and source hashes.

## Correctness and live acceptance

The real-device oracle independently compares effective indexed vertex inputs
against the authored current VB/IB bytes, allowing production compaction. It
checks position bounds against a separate scan and compares native and snapshot
raster pixels in eight image pairs per write pattern. Terrain comparison walks
native triangles independently, skips repeated-index degenerates and checks
every indexed XYZ. It uses valid ADT chunk coordinates.

The tracked-buffer fixture adds disjoint/overlapping pages, page edges, tail
locks, INDEX16/INDEX32 coordinates, READONLY, DISCARD, nested locks, failures,
GPU mutation, Reset, budget refusal, clock rewind and randomized byte-validity
checks for both VB and IB. Existing unchanged-upload negative controls remain.
The Windows entry point `test_tracked_uploads.py` runs these contracts in O2/O0
and is now part of the architecture suite. No sanitizer coverage is inferred
from that entry point.

The new GPU test belongs to the `gpu` suite. Current-source acceptance uses the
source and architecture suites, affected reset/material/terrain contracts,
real-device image tests and the production DLL build with unchanged shader
freshness and Zig version gates. Private artifacts belong under
`.local/out/range-capture-20261117/` or their runner-owned output directories.

The next game session logs `BUFFER ranges queries/preserved/coarseFallbacks`
and page-table KiB alongside the existing source hits/misses, position scans,
source-read bytes and capture CPU timings. `preserved` counts queries returning
a token older than the coarse resource generation; it is not itself a count
of avoided copies. Confirm gains on the same route, quality and current poses,
including frames where writes really overlap the visible meshes.
