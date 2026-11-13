# House GI visibility prototype

The offline house fixture tests a replacement for six-axis distance moments in
probe interpolation. The production renderer now uses the same local visibility
kernel for both standalone GI and merged unshadowed lighting/GI.
The fixture uses production BVH traversal, colour transport and SH4 evaluation;
primary and direct-light visibility are exact CPU rays, rather than game shadow
maps. It is a diffuse scene without authored WMO lighting.

Each valid probe gets a 9×9 octahedral map of nearby blocking planes. A plane is
stored as `normal / dot(normal, hit - probe)` in FP16 XYZ. Comparing the receiver
against the plane avoids the false occlusion produced by interpolating radial
depths along floors. Vertex samples include the cardinal directions so a small
axis-aligned doorway is not lost between texels. Bilinear visibility, raised to
the fourth power, suppresses interpolation across blocker edges. Receiver-facing
weights reject probes behind the receiving surface.

Visibility depends on geometry, not light colour. Immutable maps survive day/night
changes; an exact-key, bounded cache rejects wrapped slots and resets on a new
immutable BVH. A moving-geometry overlay can replace nearer blockers without
mutating the static map. Missing maps return zero visibility. This path uses CPU
BVH rays and ordinary SM3 texture reads; it requires no hardware RT.

The map costs 648 bytes and 81 rays, limited to 16 world units, per new probe.
The reach covers the eight-unit interpolation cell, including its diagonal.
There are no visibility rays for a retained probe. A uniform FP16 2×2 tile needs
one GPU read; an edge needs four. The default 16³ GPU atlas is 576×576 RGBA16F
(2,654,208 bytes). At the maximum 42³ layout it is 2457×2448, below 4096 per axis.
Payloads and CPU cache entries have separate memory accounting.

Reproduce from the repository root (Windows native/GPU commands require the
normal elevated execution mechanism described in `AGENTS.md`):

```sh
python scripts/render_house_gi.py --rays 128 --output out/house-local
python scripts/render_house_gi.py --rays 1024 --reference --reference-phase day --output out/house-day-reference
python scripts/render_house_gi.py --rays 1024 --reference --reference-phase night --output out/house-night-reference
python scripts/compare_house_gi.py --prototype out/house-local --day-reference out/house-day-reference --night-reference out/house-night-reference
python scripts/render_house_gi.py --benchmark --rays 32 --output out/house-timing
python scripts/run_tests.py test_probe_local_visibility.py test_probe_local_visibility_gpu.py --strict
python tests/test_probe_local_visibility_gpu.py --timing
```

Rendering requires the already-used NumPy and Pillow packages. Images use fixed
exposure 1.15, ACES fitted and sRGB. Before/after pairs use exactly the same
colour probes, materials, direct shadows and camera; only the interpolation
visibility changes. The independent reference traces 256 receiver paths per
pixel without probe interpolation. The comparison checks source hashes and
linear RGB error for both daytime and night views. BVH construction runs even
with assertions disabled; the fixture rejects an unexpected triangle count.

The GPU test checks 33,153 visibility queries against CPU FP16 decoding, tests
the fast and four-read versions, and loads the shipped `WorldGI` bytecode. The
four-read control is compiled separately without publishing it into the renderer
manifest. It also exercises the production managed-texture uploader. The full-pass comparison uses a 16³ atlas,
1280×720 FP16 output, resident probe metadata, SH textures and synthetic depth.
Timings use GPU timestamps, reject disjoint samples and alternate variants;
they are component measurements, not game frame time.

On the tested RTX 4090, the full-pass medians were:

| Backend | Existing six-axis visibility | New uniform fast path | New always-four-read path |
|---|---:|---:|---:|
| System D3D9 | 0.031 ms | 0.043 ms | 0.053 ms |
| DXVK2 | 0.045 ms | 0.068 ms | 0.083 ms |
| DXVK | 0.032 ms | 0.048 ms | 0.055 ms |

The added visibility is more expensive than six-axis moments; the uniform path
saves roughly 14–19% versus always reading four planes in this fixture. These
numbers do not establish performance on older GPUs or complex game scenes.

Remaining limits: SH4 and an eight-unit grid still blur directional bounce and
small local lights. A 9×9 first-hit map can miss a narrow off-axis opening or
extrapolate the plane of a small blocker. The native checks cover thin/slanted
walls, a ceiling and corner, grazing floor receivers, a small doorway, alpha
cutouts, cache collisions, geometry changes and moving overlays. They do not
prove arbitrary geometry or temporal camera motion. Game captures and frame-time
acceptance remain separate from these component checks.

## Runtime ownership

`Probe::localVisibility` holds immutable geometry data shared by the worker,
publications and upload mirror. `ProbeVisibilityCache` is bounded by atlas slots
and is reset when the static BVH changes. Lighting-only relights reuse it. Static
cache misses, including GIThreads prefetch, attach visibility before publication.
Fallback SH may survive a same-map geometry swap, but its old visibility is
removed immediately; missing local data cannot transmit light through new walls.

Moving actors keep the existing bounded colour correction. Their small surfaces
do not become extrapolated structural blocker planes, which would cause large
dark patches behind the player. Static walls, ceilings and terrain own this map.

The D3D owner allocates one managed RGBA16F atlas with the probe resource group.
The upload mirror retains payload identity, skips colour-only publications and
marks the changed tile bounds with `AddDirtyRect`. A failed unlock/dirty update
invalidates the mirror for retry. `uploadedSerial` commits only after local and
ordinary probe uploads finish. Reset/pressure paths release or validate the
managed texture alongside the other probe targets; CPU/GPU memory ledgers include
the new storage.

Only GI owns s13 for this operation; regional fog regains it immediately afterward.
c82 contains tile columns and reciprocal atlas dimensions. A zero column count
preserves legacy capture replay in the standalone GI entry; the live renderer
binds a positive count and the required local atlas. The merged entry specializes
away legacy replay to stay within the baseline SM3 budget (501 slots, 29 temps);
standalone GI uses 377 slots and 25 temps. Caps and shader limits were not raised.

With `Diagnostics=1`, `GI local visibility atlas=...` identifies allocation and
`GI local visibility built=... reused=... failed=...` reports worker cache use.
Existing RenderProfile `GI` and `WorldLightingGI` buckets include the new work.
