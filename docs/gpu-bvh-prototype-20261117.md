# GPU BVH feasibility, 17 November 2026

Two hardware CS5 builders and two GI execution layouts are implemented in
`scripts/gpu_bvh_prototype.cpp/.hlsl`, driven by `scripts/prototype_gpu_bvh.py`.
They are standalone experiments. The production D3D9 renderer, GI worker and
release 0.3.218.11 are unchanged.

## Implemented work

- GPU scene bounds, Morton keys and tiled/global bitonic sorting; no CPU sort.
- Balanced Morton tree and binary radix LBVH with parallel topology and
  bottom-up, atomic completion. Leaves contain 2, 4 or 8 triangles.
- Refit, including a moved-geometry oracle; device readbacks verify permutation,
  complete acyclic topology, parent/child bounds and primitive containment.
- GPU opacity classification, indexed geometry, bilinear sRGB/alpha sampling,
  wrap/mirror/clamp, shading/geometric normals, closest and any-hit queries.
- Sun/sky diffuse GI with three bounces, 32 rays, SH projection and distance
  moments. Both one thread per probe and one group per probe are measured.
- Production CPU SAH and four-thread SolvePool comparators. Region-cache checks
  use the runtime 16 MiB placement / 8 MiB decoded-source limits, provider and
  revision callbacks, including same-region and eight-unit adjacent builds.

LBVH topology follows the common-prefix approach in
[Karras, HPG 2012](https://research.nvidia.com/publication/2012-06_maximizing-parallelism-construction-bvhs-octrees-and-k-d-trees).
The implementation is an independent HLSL experiment.

## Workload and measurement limits

Windows, RTX 4090, x86 Zig 0.15.2 O2, verified Microsoft shader compiler. One
hardware D3D11 device/context is created per executable invocation and reused
for every build/refit/ray/probe job. The Python driver launches a separate
executable for each independent scene. It never launches or modifies the game.

The real scene uses the installed Northrend cache around
(1328.78, -3192.43, 167.61), radius 288 XY / 320 Z: 512,401 source triangles,
494,984 vertices and 81,494,016 material bytes. The CPU SAH contains 512,329
nondegenerate primitives. GPU intersection applies the same area rejection.
Packed input SHA-256:
`0e1757d59ae962f6bc15494d6fc62684b9bdf580d488cb4356d8c67f72b2804e`.

Raw runs are private under `.local/out/gpu-bvh-20261117/`. `review/results.json`
binds the sources, compiler, executable, packed input and logs; `measured/`
contains the preceding repeat. Both accurately report REJECTED, not production
acceptance. The ordinary oracle is a separate, narrower fixture contract.

Three measured repetitions follow two GI warmups. CPU scene copying is outside
the CPU build interval. GPU packing, allocation and upload are separate costs.
Timestamp queries reject disjoint intervals; GPU wall time includes 1 ms Sleep
polling. Process CPU milliseconds have scheduler-tick quantization, so raw
process cycles are also recorded without converting cycles to time. Structural
validation, reference queries, image exports and their readbacks are outside
the reported kernel intervals. GI result readback is reported separately.

The earlier balanced-tree group-per-probe path took roughly 32 ms for 512 probes
with two-triangle leaves. LBVH reduced it to approximately 5 ms, including the
prototype's polling. Four CPU threads take approximately 25 ms for the same
sun/sky probes. Eight-probe work is still much faster on CPU: roughly .3 ms
versus several milliseconds on GPU. Production prefetch currently uses groups
of eight; applying the large-batch multiplier there would be incorrect.

The tree kernel takes about .4-.5 ms versus about 200 ms for a flat CPU build.
This is not a 400x worker speedup: cold packing/upload costs about 35-40 ms,
device setup about 150 ms once, and decoding/cropping/placement assembly remain
CPU work. The actual region cache reused 134 BLAS on the identical-region turn,
but approximately 472,212 triangles remained in its flat fallback tree. That
stage still took about 190-200 ms. The cache comparison includes loading and
assembly and must not be directly divided by the GPU kernel interval.

Logical GPU buffer allocation is 133,985,064 bytes, including fixture buffers
and the largest leaf variant's pool. This is not physical VRAM residency. The
benchmark retains CPU references for comparison; it establishes no RAM/RSS
reduction. Production CPU weather/local-light occlusion consumers still exist.

## Visible differences

The comparison exports GPU readbacks, raw float images, PPM images and 512 probe
outputs. The surface view is an orthographic material/normal slice near a
disagreeing ray, with identical host Lambert shading. It is not the game's final
frame. Probe previews evaluate +Z irradiance through the production evaluator.

On the real geometry preview, seven triangle IDs differ but all 32,768 RGB8
pixels agree; maximum linear RGB error is 2.76e-7. All 512 +Z probe preview
colours also agree after RGB8 conversion. Maximum linear irradiance difference
is .000134185; valid flags agree. Sampled visibility differs by at most .0000371.
The 8,192-ray strict attribute check still reports three geometric-normal
differences on neighbouring triangles: shading normals, albedo and back-face
flags agree. Equal triangle IDs alone are not required by the visual oracle.
These small differences are not evidence of a visible gameplay defect.

The synthetic threshold case deliberately puts constant UVs exactly at alpha
0.5. Barycentric/sampling roundoff changes coverage on 1,383 / 32,768 preview
pixels. Five of 512 probes differ by more than .001 in +Z irradiance, maximum
.011557. Valid flags still agree. Sampled visibility can differ by .593. This
case shows sensitivity at a numerical boundary, not its frequency in the game;
both CPU and GPU exhibit speckling there. It must not be described as the real
scene's image error or hidden by loosening every test tolerance.

## Device lifetime and migration decision

D3D11 supplies [compute dispatch, shared group memory and atomics](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-compute-shader).
The renderer can keep drawing through D3D9 while a persistent D3D11 worker owns
compute resources. Recreating the device per frame/job is neither implemented
nor intended. Normal worker ownership would retain it for the renderer session.

One-time creation does not eliminate scheduling cost. The two API devices use
the same GPU; Windows can [interleave different graphics contexts](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-preemption).
There is no measured simultaneous D3D9/D3D11 frame comparison here. Existing
D3D9 vertex/index buffers cannot simply become these compute buffers;
[cross-API sharing is restricted to supported textures](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-opensharedresource).
The experiment explicitly uploads separate geometry/material data.

Automatic whole-GI/BVH migration remains disabled. Real-scene visual differences
are small enough to justify continuing, but current evidence does not cover
small-batch latency, mixed rendering contention, point/additional lights,
aurora, moving overlays, static-path/relight caches, local-visibility generation,
D3D9 atlas publication, cancellation, device-loss/recovery or Wine/Mac/Linux
delivery. A selective large-batch backend is the next useful runtime boundary;
switching every eight-probe job to this device would regress latency.

## Run

Use fresh output paths; existing directories are refused. Windows compilation
and native/GPU execution require the normal elevated tool approval from AGENTS.

```powershell
python scripts/run_tests.py --suite gpu-bvh-prototype --strict
python scripts/prototype_gpu_bvh.py --output .local/out/bvh-review-new --scenes 6 7 --rounds 3 --measure-rejected
```

Scene 6 needs the installed Northrend world cache. Scene 7 is the alpha boundary.
`--measure-rejected` collects remaining diagnostics after numerical failures;
the report remains REJECTED and the process exits 2. Structural corruption still
fails immediately. Production shader compiler pins and freshness gates are not
changed; compute bytecode stays in the experiment output directory.
