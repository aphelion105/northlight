# Character shadows and tree history, 2026-11-17

The retained fix changes FogTemporal only. Sky reprojects a direction, but the
old filter ignored previous depth there. Bilinear fog history could therefore
mix a former branch's short fog path into clear sky. Sky now uses one validated
history texel. Surface history retains bilinear filtering and reuses the nearest
depth proof from its existing four footprint samples. The early equal-colour
path retains the old signed-zero/nonfinite behavior through the late path.

No passes, targets, renderer CPU work or model captures are added. Only
FogTemporal bytecode changes. Dependent shader manifests were regenerated with
the official compilers; dependent bytecode is unchanged. Final shader SHA-256:
`cc740470424c31397bee18c554019fcd5f869351d2be17d22c37478ef833a9dc`.

## Captured-input measurements

Inputs: capture 2, session-48100-530073171-0, 3840 by 2160. Shifting actual fog and
distance history by 0, 1 and 3 half-resolution pixels reproduces moving silhouettes.
These are isolated filter measurements, not game FPS or a new live capture.
ABBA GPU timestamp medians after warmup, ms per filter:

| Backend | Stationary old/new | Shift 1 old/new | Shift 3 old/new |
|---|---:|---:|---:|
| Native D3D9 | .05745 / .05302 | .06125 / .06044 | .05911 / .05892 |
| DXVK 2 | .06754 / .05473 | .06898 / .06883 | .07070 / .06420 |
| DXVK 3, run 1 | .06832 / .05340 | .06824 / .06308 | .06927 / .06369 |
| DXVK 3, run 2 | .06588 / .05567 | .06797 / .06402 | .06919 / .06467 |

The supplied game log used DXVK 3. Submit cost was unchanged at measurement
resolution; production CPU code is unchanged. These checks do not establish
whole-game frame time. Very small sky holes can still receive foreground fog
from the separate four-tap spatial upsample; the wider-search experiment was
rejected for increasing GPU work.

The new GPU regression checks 20 cases on native D3D9, DXVK 2 and DXVK 3: former
foreground, mixed footprints, rotation, even/odd dimensions, valid surface
history, weight zero and signed zero. The frozen old shader must reproduce the
fault in four negative controls per backend. An additional equivalence probe
found 512 non-sky cases bit identical across 16/32-bit input/output and MSAA
0/2/4/8. That probe's overall equality verdict intentionally fails for its two
sky scenarios; their changed behavior is tested independently by the regression.

## Character-shadow performance gate remains open

The capture reports nine small actor draws admitted for GI only. Quality's
MinSkinnedTriangles=100 can reject multi-bone model sections while rigid
one-bone attachments survive. Old captures lack geometry exports sufficient
to label each rejected draw as a particular anatomical part.

Restoring all parts with threshold zero failed the load constraint. A synthetic
stock-skinning fixture using production capture/render code at 48 actors raised
CPU render submission from about .102 to .140 ms and GPU from .200 to .252 ms.
Moving these parts into world cascades alone also regressed in crowds. A further
coalescing prototype reduced draws, but its final world-cascade variant still
raised combined capture/render CPU from .257 to .265 ms and GPU from .267 to
.282 ms at 48 actors. It also lowers shadow texel density. Neither prototype nor
either setting change is retained. This is component evidence, not game FPS.

Character thresholds, detailed-map settings and client installation are unchanged
by this fix. The incomplete-character-shadow issue remains open. Raw experiments
and rejected source are in .local/out/shadow-tree-20261117; the final fog review
is fog-final-review.json. Baseline renderer commit: 3d78748f27ee2cd39a6ea3d36904023d4f104503.
