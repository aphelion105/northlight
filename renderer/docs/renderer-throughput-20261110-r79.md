# r79: point-face composition and upstream comparison

Baseline: `8a90f4005b20b6d737d737d5aa46f37244c0ecdf` (r78).
Production source identity:
`012ed7aa438c34abf6c611153b8d376143114b2b3e2f558a38fa652bfadedcf2`.
Upstream: `e19ffee601acbad2b991917109bcb6d8b2f7d5ca`, a separate clean
checkout of `lepola/northlight` main, without PR #98.

## Point composition

Copy the cached static face into the existing R32F cube face. Dynamic
casters then merge into that face using [D3D9 MIN blending](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dblendop),
instead of rendering a scratch face and drawing a separate fullscreen
union. Static-only faces need just the copy. The device's R32F cube blending
support is queried once. Unsupported blending or failed copying keeps the
old scratch/union path; even a copy that writes before failing is overwritten.

No texture, mask, shader bytecode or quality setting is added. The existing
scratch resource remains available for fallback. GI, AO, fog and lighting
calculations remain independent. Face scheduling and logical caster weights
are unchanged. One existing initialization draw warms MIN blending; the
three-draw warmup does not grow.

## Comparison

`tests/benchmark_upstream_pipeline.py` builds each checkout's actual renderer
and compiled shaders. Its private header overlay only grants test access.
An optional point INL from r78 isolates this change from earlier PR work.
Inputs and executable hashes are recorded, and production source identities
must remain unchanged throughout the run.

RTX 4090, DXVK 3.1.1, 3840x2160 captured scene/depth/constants; authored
caster geometry, probe and regional fog field. Common settings use SDR,
AO/blur and, where enabled, eight lamps with one PCF point shadow. Upstream
does not provide the PR's HDR, model normal capture or multi-shadow pool.
There are three sequential ABBA rounds per case, six runs per implementation,
32 measured frames after 16 warmup frames per run. Values are medians of
run medians, in milliseconds:

| Common workload | CPU upstream → PR | GPU upstream → PR | Draws upstream → PR | RT bindings upstream → PR |
| --- | --- | --- | --- | --- |
| AO/blur/composite | 0.02115 → 0.01333 | 0.54067 → 0.54268 | 7 → 7 | 31 → 20 |
| With regional fog | 0.02985 → 0.02158 | 0.85127 → 0.73517 | 12 → 11 | 35 → 24 |
| With GI, directional shadows, lamps and point shadow | 0.03500 → 0.03487 | 1.01626 → 0.98714 | 19 → 24 | 39 → 38 |

The base GPU difference is 0.4%, treated as equivalent. CPU improves by
37% and 28% in the first two cases and is equivalent in the last case;
the latter GPU intervals improve by 14% and 3%. These results compare
equal inputs/settings, not identical implementations. The base image is
exact. Existing PR fog differences produce mean RGB differences below
3.88/255 and maxima up to 88/255; fog-enabled images are not equivalent.
These image differences are not introduced by r79.

With all other PR code fixed, replacing only r78's point implementation:

| CPU r78 → r79 | GPU r78 → r79 | Draws | Copies | RT bindings |
| --- | --- | --- | --- | --- |
| 0.03688 → 0.03615 | 0.98483 → 0.98509 | 29 → 24 | 0 → 5 | 43 → 38 |

The small CPU difference is about 2%; GPU is unchanged within noise.
Five union draws and their bindings are eliminated. Final SDR output is
byte-exact across every before/after run.

CPU covers render submission and the outer save/restore. GPU timestamps
span the common graph and can include submission gaps. Initialization,
uploads, readback and waits are outside the measured interval. This fixture
excludes native draw hooks, model/material capture, actual caster density,
HDR, weather and presentation. It does **not** establish a 2–3 ms budget
for the entire Northlight renderer, game FPS, AMD performance or default
multi-shadow/soft-shadow costs. The latest available game/QA logs still
identify r73; there is no post-r78 gameplay measurement.

## Validation and reproduction

Native D3D9 and DXVK 2/3 pass exact six-face controls, copy failures before
and after mutation, face reuse, source isolation, partial refresh and
visibility transitions. Cold versus warmed initialization passes exact
42-face comparison and the existing state/fault matrix, now including blend
factors and operation. The old cold fixture dereferenced an uncreated white
texture; it now explicitly creates that texture before use.

The broader point-shadow stutter suite passes on native D3D9 and DXVK 2/3,
including native alpha formats, reconstructed skin poses, retained resources
and toggles. CPU schedule/face-cycle checks pass optimized and ASan/UBSan
builds; source selection passes O0/O2. The Zig 0.15.2 production DLL and
Windows package `0.3.217-test.20261110.4` pass source/shader freshness,
17-export, payload equality and package verification. No client files were changed.

Run the comparison with a separate unchanged upstream checkout, the saved
`capture-1-scene.fgr`, `capture-1-depth.fgr`, `capture-1-constants.f32`, and
an x86 D3D9 backend:

```powershell
python tests/benchmark_upstream_pipeline.py --upstream $upstream --capture $capture --backend $backend --output $newOutput --before-ref 8a90f400 --rounds 3
```

Receipts and raw logs: ignored `out/optimization-20261110-r79/`, particularly
`comparison-verified/validation.json`, `comparison-verified/pixels.json`,
`cold-verified/validation.json` and `coverage-final/point-terrain-gpu/`.
