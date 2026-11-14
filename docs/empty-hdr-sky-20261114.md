# Empty HDR sky work, 14 November 2026

The gameplay run ending at 22:58 loaded source identity
`0c496ddeb8a8924f1899898b818a760c305e7d4d80f7ca6fa5efe517a1e6065b`.
The copied log and analysis are in `.local/out/costs-20261114/`.

## Profile

Across frames 2961..5082, repeated Celestial toggles correlate with GameScene
medians of 3.713 ms enabled (10 samples) and 1.725 ms disabled (7 samples).
Late Celestial markers are almost zero; the work is inside native sky/foreground
draws. These intervals include inline captures and submission gaps. Cameras and
background GPU load are not identical, so the difference is not an accepted
optimization result or a controlled FPS comparison.

Other recurring GPU costs are final composition (about 0.38 ms without material
detail, 0.53 ms with it), AO plus blur (about 0.39..0.57 ms), removal smoothing
(0.25..0.31 ms) and fog marching (about 0.26 ms). Detailed actor-map rendering is
about 0.014 ms here; it is not the largest GPU cost in this run.

With AutoBump restored, CPU capture is around 1.6..2 ms. Late material-profile
medians include snapshots 0.254 ms, bounds/pose 0.208 ms, deferred flush 0.190 ms
and capture setup 0.132 ms. The stage totals are exclusive; only inclusive
bounds/pose time contains the nested pose-envelope work. Existing snapshot
handoff is active (139 reuses per sample),
as are prepared pose metadata and coarse admission. No earlier material
optimization was removed. The current change leaves their quality/work intact.

## Removed work

The HDR sky target contains only extra linear emission. Native sky colour stays
in the game's framebuffer. After clearing the HDR target, ordinary clouds,
rain, smoke and text only attenuate its contents: multiplying a known zero
cannot change it. Previously this still caused geometry replay or an additional
MSAA MRT attachment throughout the scene. The log reports zero early owned
disc draws in the measured scene, despite active foreground HDR transfers.

Device now checks whether an owned emitter has written this target. An attempted
early sun/moon write conservatively marks it before submission; a successful
aurora transfer marks it too. Ordinary sky replay, foreground MRT and fixed-sky
MRT are skipped while it remains empty. A proven aurora can still write an empty
target. Clearing a new frame resets the proof, including reuse of the same COM
pointer. Failed/invalid targets cannot grant emission. Late discs, HDR decode,
bloom, native colour, quality settings and shader bytecode are unchanged.

Separately, additive foreground has ZERO/ONE blending for both captured sky
background and transmission. Its mask replay and SavedState capture/restore
are now omitted. The native target identity and supported blend equation are
still checked; additive background contributions remain active.

`HDR_WORK emptySkySkips` counts skipped ordinary replay calls cumulatively;
`skyEmission` reports the current proof. The foregroundDraws counter should
also fall in scenes with no early HDR emitter. These counters establish use,
not savings in milliseconds.

## Verification

`test_hdr_empty_sky_gpu.py` extracts the production admission/replay methods,
with thin device/context fixtures, and runs real native D3D9, DXVK 2 and DXVK 3.
MSAA 0/2/4 checks require exact FP16 output equality, zero empty-target draws
and target changes, unchanged nonzero emission, aurora wake-up, same-pointer
reset and no state-block creation for additive foreground masks. Existing HDR
sky, sun, storage, boundary and celestial composition oracles protect the
surrounding image/state contracts.

The optional 4K ABBA/BAAB probe uses eight authored translucent full-screen
layers, the same shaders and a native sentinel draw in both variants. In the
first MSAA-2 run, the retained work measured 0.566..1.148 ms versus
0.0225..0.0226 ms after empty replays were omitted. This is an intentionally
bounded component workload, not captured native scene geometry or gameplay
FPS. Forever remained open throughout; the particularly noisy non-MSAA
reference measurements must not be presented as reliable absolute savings.

Final acceptance reports must match the current commit and tree. Earlier trial
reports affected by later test/source edits are not release acceptance.
