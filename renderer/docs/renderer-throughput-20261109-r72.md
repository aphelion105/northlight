# r72: cube storage reuse and combined point culling

Baseline: `489c30a054946838986c51351b456e8d759d804c`.
Production source identity:
`ad7adb2bd7892fb1ab6464a474ce19c9d2c74a16b76cbedcf673e787735d14fe`.
Built with Zig 0.15.2 through `scripts/build_renderer.py`, x86 Windows,
2026-11-09 20:38:26 UTC. See the
[validation receipt](renderer-throughput-20261109-r72-validation.json).

## Selected changes

An immediately replaced point-light source can reuse its slot's cube texture
and six surface handles. Retirement still resets content keys, refresh state
and partial cycles. Restoring a retained static cache transfers this same
storage to the restored source; retained caches themselves own no cube.
Every new/restored source must complete a fresh six-face draw before use.
Missing capture or a failed face cannot publish the old contents.

This adds no resource pool, idle-cube retention, quality option or light
budget. Only same-map replacements reuse storage. Unselected slots, map
changes, memory-pressure retirement and a zero limit retain their release
behavior. Resident cube memory is unchanged; allocation churn is reduced.

Point-candidate sphere rejection and face clipping now share one prepared
culler. Source position/radius validation and radius squaring happen once
per source update. Bounds are validated once; extent preparation happens
only after passing the sphere. Static/terrain batches no longer construct
and copy a temporary replay-bounds object. Unknown inputs retain the old
fail-open behavior. Full content-digest clipping remains independent of
the sphere and still uses all six faces.

A partial face refresh now constructs matrices and prepares clip planes only
for its requested faces. The default six-face update is unchanged.

## Verification

The baseline/current real-renderer fixtures pass on native D3D9 and DXVK
1/2/3. Across five alpha recipes and merged/unmerged geometry, 40 nonempty
R32 cubes (240 faces) have matching complete-cube 64-bit hashes. Existing
point-pass assertions also pass for static retention, white maps, current
poses, bounded geometry, uploads and partial refreshes.

On each backend, eight alternating-source handoffs create eight new cube
textures on the baseline and zero on the new implementation, confirmed by
device-call interception. The six-face results equal fresh-allocation
controls for every handoff: 192 face comparisons over four backends. The
sources produce different images, preventing a stale-content false pass.
The fixture also exercises interruption of a partial cycle, retained static
restoration, missing capture, an injected Clear failure, exact cold recovery,
map/pressure retirement and release at a zero limit.

Portable optimized and ASan/UBSan tests pass, including 174,335 base culling
cases plus subset, invalid-source and sphere/plane-boundary checks against
the frozen prior sphere helper and scalar clip reference. The same culling
checks pass in a native x86 Zig build. Point selection, schedule, extracted
replay admission and frozen hotpath comparisons pass. The admission harness
now models the requested-mask signature and combined sphere test.

Native x86 ABBA component observations, 163,840 checks/sample, four warmups
and six retained samples/variant:

| Requested faces | Box spread | Previous, ms | Combined, ms |
|---|---:|---:|---:|
| 1 | 15 | 3.828900 | 3.445950 |
| 1 | 64 | 1.955950 | 1.765250 |
| 1 | 4000 | 0.606050 | 0.614500 |
| 6 | 15 | 15.388450 | 15.035550 |
| 6 | 64 | 5.215000 | 5.158800 |
| 6 | 4000 | 0.601200 | 0.614100 |

Spread 15 is mostly near the source; 64 mixes inside/outside candidates;
4000 is entirely sphere-rejected in this fixture. The final version defers
extent calculation until sphere acceptance; an earlier candidate did that
work before rejection. Gains depend on distribution. The tiny all-rejected
component is about 1–2% slower in this run; no universal speedup is claimed.
The user requested concurrent-WoW tests. These observations and deterministic
allocation counts do not establish game FPS or isolated GPU-time savings.

This is a focused regression run, not a full repository suite. No HLSL or
compiled shaders changed. Tested images need no visual tolerance. The exact
castle-video issue remains deferred. The local game log is unchanged since
the preceding iteration.

## Delivery

`0.3.217-test.20261109.6`: production DLL and verified Windows installer.
All 17 exports/ordinals match; the package payload is byte-identical to the
new DLL. Artifact SHA-256 values and sizes are in the receipt. No client DLL
was installed and no game was started by this iteration.
