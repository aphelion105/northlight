# r94: optional AO in the shared compositor, without unused lighting

Base: r93, `7ebaecddaacbcc9d1a2f3746e02382decaa4e32e`.
Source identity: `d7adaf9a3ca197aadeb8b2d6f367b390c85c7bd1bbd91e27365893771d5d1f00`.

## Fresh gameplay diagnosis

The closed 13,625-frame DXVK 3 session identifies the latest r93 DLL:
source `27ae785836809947a41286f42111984c93f6a3c5bf0ea067b05c2cc9e01aab02`,
built 2026-11-10 13:13:12 UTC. Its 5,811,524-byte snapshot is
`out/optimization-20261110-r94/client.log`, SHA-256
`1b54822b30b630c1e0ff6062646f095c355696f9aa273e8ad7a87e7db7001eee`.

The final settings sequence turns GI off at frame 13219, then turns AO on
at 13282. Shadows, world fog, local lights, materials and weather had already
been disabled. The preceding off interval reports 79.73 FPS / 12.542 ms;
the following two AO intervals have median 65.835 FPS / 15.191 ms.
This reproduces the reported roughly 80→65 FPS symptom in one session.

AO-only was still capturing 1,113 model-normal packets and replaying 740
draws at frame 13589. Its GPU normal replay cost about 0.15 ms. The CPU
capture scope was 0.875 ms, but includes other capture work too: that entire
number is not a measured model-normal saving. The reported CPU total was
2.069 ms, including 0.559 ms of effect submission and 0.151 ms cleanup.

The final three GPU samples have median effects cost 1.647 ms. At frame
13589, AO itself is 0.808 ms and its filter 0.243 ms, while the pipeline also
runs WorldNormals, WorldLighting, RemovalSmooth and TemporalLight.
The accompanying EVENT trip has a roughly 2.54 ms median wait, partitioned
into queue delay and replay/backend completion. These spans overlap GPU
execution and must not be added to CPU scopes as independent costs. The
query is a real completion dependency; this revision does not fake it or
change query/command-stream synchronization semantics.

## Shared pipeline change

AO remains an optional input to the existing WorldComposite family. There
is no separate AO compositor, new mask, shader entry, texture or quality
setting. The existing AO algorithm, actor correction and denoise are unchanged.

The compositor's depth-weighted four-tap AO upsample now reconstructs its
sample distance directly from the current scene depth, using the same
canonical texel and depth expression as WorldNormals/TemporalLight. It no
longer needs those passes to produce a distance texture just to apply AO.
The existing combined material/HDR entry keeps its distance-texture read:
material shading already needs that producer, and retaining the read keeps
this existing SM3 entry within its 768-slot device gate.

World rendering now runs the normals, directional/GI, removal smoothing and
lighting-history block only when a consumer needs it. Without one, the same
compositor receives neutral light/baseline inputs and the current AO texture;
lighting history is invalidated. GI at zero strength and materials without
current material data do not activate that block. Fog retains its existing
distance/history producers; this revision specifically removes the AO-only
dependency rather than redesigning fog history.

The model-normal capture admission is latched once per frame. AO alone and
weather alone skip it before shader lookup, constants, snapshots and queued
normal replay. Lighting/material consumers, debug views and pending normal
diagnostics retain capture. The normal lighting path's MRT safety/fallback
rules are unchanged. Existing common composition still handles native fog,
water, effect coverage, names, prelit/unlit receivers and HDR.

## Controlled comparison with r93

The standalone fixture uses the actual WorldRenderer and the actual Device
admission policy. Reference render code and its generated shaders are frozen
from r93; a compatibility accessor is inserted for the current Device test,
without changing the reference render body. There is no game process.

Both executables compile before measurements. Each backend runs
reference/current/current/reference. Each process measures twelve 32-frame
batches; the first four warmup batches are discarded, leaving sixteen samples
per variant/backend. The input scene, depth and already filtered AO are
identical, at 3840×2160. Medians, in milliseconds per frame:

| Backend | Common block CPU submit: r93 → r94 | Common block GPU timeline: r93 → r94 |
| --- | ---: | ---: |
| Native D3D9 | 0.013791 → 0.011266 | 0.607120 → 0.384768 |
| DXVK 2 | 0.013892 → 0.010505 | 0.603648 → 0.394672 |
| DXVK 3 | 0.012619 → 0.009511 | 0.573091 → 0.400335 |

GPU savings are 30.1–36.6% for this common block. The CPU table measures its
submission only, saving about 2.5–3.4 microseconds; it excludes per-model
capture, native game draws, raw AO and denoise. GPU timestamp intervals
include submission gaps. These numbers do not establish gameplay FPS or
prove a 2–3 ms total Northlight budget. The unchanged raw AO/blur cost must
still be included when evaluating AO-only gameplay.

## Validation and reproduction

- Actual Device latching and WorldRenderer checks cover seven effect
  configurations, repeated switches, SDR/HDR, water, debug and diagnostic
  demand on native D3D9, DXVK 2 and DXVK 3. AO-only submits exactly one shared
  composite and no normal, lighting, removal or lighting-history draws.
- The extracted production capture-admission body passes 192 combinations
  under O2 and ASan/UBSan. With no consumer it performs zero shader lookups;
  valid models are admitted when the consumer returns. All original failure,
  applied-frame, master and missing-shader guards remain covered.
- The general world GPU oracle passes MSAA 0/2, effect/geometry reactivation,
  exact neutral inputs, resource failure/retry, state restoration, fog history,
  materials, HDR combined/fallback, names and prelit/unlit receiver checks.
- The AO lab passes 1,440 stage images per backend, across all five methods,
  three qualities, twelve scenes and even/odd dimensions: 4,320 images total,
  zero failures. Colour/depth-guided tent and AO folding audits pass. The live
  resource-switch test passes 200 transitions, allocation/lifetime checks and
  failure/retry; its older mock now includes the existing no-water blur shader.
- Existing shader gates pass: WorldComposite 440 slots, HDR 501, base HDR 425,
  combined material/HDR 761. The shader entry count remains 191. No AO kernel
  or blur binary changed.

Reproduce the paired shared-block checks and timings with Zig 0.15.2 outside
the Windows restricted sandbox, serially without overlapping compilation:

```powershell
python tests/test_ao_optional_layers_gpu.py --reference 7ebaecdd --output out/ao-optional --backend D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll
```

Receipts and raw samples are in `out/optimization-20261110-r94/optional-final/`;
AO lab and general GPU receipts are beside it. No production DLL or installer
was rebuilt or installed. The current r93 DLL remains SHA-256
`1bb79e7629f42f2202689409e9681c1998860a294227da3b54995f90ca00606a`.
