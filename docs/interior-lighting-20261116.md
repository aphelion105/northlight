# Interior lighting and local-light ownership, 2026-11-16

The supplied Wintergarde inn and tower captures expose two independent errors.
The fixes build on `ad06f6aa` (.7), retaining the .6 stable, .6 interior-preview
and .7 local-shadow filtering changes.

## Red interior lighting

The inn floor uses a lit WMO shader with **room lighting overrides**, not the
outdoor light bank. Its native c10 RGB is `(0.439216, 0.419608, 0.474510)` and c11
is zero. The frame context instead has ambient `(0.196078, 0.305882, 0.258824)`
and cyan directional light `(0, 0.360784, 0.478431)`. Subtracting the latter from
the already lit room removes green/blue that the native material never received.
The earlier prelit-only fix cannot handle this lit shader.

The renderer now proves the live COLOR0 equation `c10 + dot(normal,-c12)*c11`,
including MOV aliases, independently of shader hash. The captured material
variant `746d6433532b57dd` is absent from the stock WMO catalog but passes this
proof and the independent world-coordinate admission. A changed c10/c11 RGB
bank marks the depth-writing receiver using stencil bit 0x10. Packed W lanes,
malformed constants, dead RGB equations and unrelated shaders cannot qualify.
This proof does not broaden the global WMO context fallback.

M2 has the opposite bank order: c10 is directional and c11 is ambient. The proof
and comparison both use that layout for known model families. The captured rug
shader `bc62083b7435826c` is checked separately with its actual room constants;
an outdoor M2 bank must not acquire a room mask merely because the rows differ
from WMO's convention.

Marked receivers preserve their explicit native room lighting. Shadowed incoming
daylight, positive GI, shadowed local lighting, AO and atmosphere still apply.
Outdoor ambient subtraction and GI's outdoor-ambient removal do not apply to
the room's authored term. Ordinary outdoor receivers keep their existing path.

On an inn floor rectangle `(900,850)-(1100,1000)`, paired composition changed
mean RGB from `(0.110423,0.023028,0.006570)` to
`(0.264120,0.253709,0.140355)`. The captured pre-composite native scene is
`(0.279220,0.269063,0.149030)`; AO accounts for the remaining attenuation.
The old replay matches the recorded SDR intermediate to mean absolute error
0.0016, within its UNORM quantization/dither differences.

## Light through walls and floors

Ten inn draws and four tower draws report only two corrected sources while
their native WMO bank contains four selected lamps. Their depth-writing layers
use SRCALPHA/ZERO and INVSRCALPHA/ONE. The previous RGB ownership gate rejected
blended WMO layers, leaving them on the limited native endpoint fallback.

Depth-writing WMO layers with ADD and RGB-independent alpha/ONE/ZERO blend
factors now admit the existing source-by-source RGB handoff. Vertex RGB-only
and pixel alpha-independence proofs remain mandatory. All matched native slots
are removed during the real submit and restored afterward; the existing local
pass supplies each selected source with its cube visibility. Unsupported blends,
unknown shaders and non-depth-writing WMO draws retain the fallback.

The supplied cube maps already contain the relevant occluders. Replaying the
recorded point pass on its exact depth/normals/constants/cubes gives over
759,000 fully occluded half-resolution source/pixel samples in the inn. The
tower has both blocked and visible receivers. Clear-cube controls remove no
light; the zero resolved-visibility control bounds the blocked energy. A zero
depth cube is not used as a full-blockage oracle because PCSS changes its filter
footprint with blocker distance.

The four-lamp GPU fixture uses both stock and exact captured VS/PS programs.
All four removed slots match an independently zeroed native bank exactly;
alpha and restored constants match too. The two-slot negative control leaves
measurable lamp energy. D3D9, DXVK 2 and DXVK 3 pass these checks.

## Performance scope and measurements

No game process was running during the paired timing checks. Optimized x86
fixtures use warm ABBA GPU timestamp batches and separate CPU submission
intervals. A/A controls measure run noise. These are component and full
screen-pipeline measurements, **not gameplay FPS** on the reporter's installation.

At 2560x1440, repeated full WorldRenderer runs with material detail enabled:

| Backend | SDR old/new GPU ms | HDR old/new GPU ms |
|---|---:|---:|
| D3D9 | 0.3679 / 0.3702 | 0.3013 / 0.2552 |
| DXVK 2 | 0.3855 / 0.3450 | 0.3049 / 0.2472 |
| DXVK 3 | 0.3794 / 0.3348 | 0.3017 / 0.2335 |

The no-material synthetic case has a small added cost: at most 0.024 ms on
D3D9, 0.006 ms on DXVK 2 and 0.009 ms on DXVK 3 in the repeated runs. It would
be incorrect to claim zero overhead in every configuration. Receiver
classification costs about 0.012 CPU ms per 1,000 draws, with no per-draw
allocations or shader analysis. The supplied captures already had the separate
prelit daylight MRT; it is reused. Rooms without that resource can allocate it
through the existing resource path. No additional geometry replay or point
shadow map is introduced.

At a 256x256 WMO receiver, replacing the old two-light endpoint patch with the
four-slot handoff reduces GPU time from roughly 0.0020–0.0032 ms to
0.00027–0.00045 ms per draw across the three backends. Local-light selection,
cube generation and the deferred local-light pass are unchanged.

## Reproduction and limits

Private artifacts are under `.local/out/lighting-captures-20261116/`, including
`capture-replay/validation.json`, `native-bench/*/gpu.log` and
`pipeline-bench/*/{gpu,control,repeat}.log`. The final M2-layout run is in
`pipeline-model-checked/*/gpu.log` and supplies the table above.

Run `tests/validate_interior_capture.py --capture SESSION --benchmark` with
`NORTHLIGHT_TEST_OUTPUT_DIR` set to a new output directory. It validates completed
capture journals, records input hashes, replays all point sources and compares
ordinary/authored composition on all three backends. The buffers do not contain
VB/IB geometry, a reconstructible whole-scene draw stream or exported stencil;
the paired authored composition is a receiver-path comparison, not a new game
screenshot. Production registration and stencil routing are checked separately
by `test_prelit_receiver_gpu.py`, including its optional
`NORTHLIGHT_CAPTURE_VERTEX` input.
`NORTHLIGHT_CAPTURE_MODEL_VERTEX` adds the exact captured M2 registration case.

`NORTHLIGHT_LIGHTING_BENCH=1` enables the CPU and full-pipeline measurements in
that test. `NORTHLIGHT_LIGHTING_CONTROL=1` selects A/A. For the native WMO checks,
`test_native_local_light_gpu.py` accepts `NORTHLIGHT_CAPTURE_VERTEX` plus
`NORTHLIGHT_CAPTURE_PIXEL`; `NORTHLIGHT_NATIVE_LAYERED_ONLY=1` limits manual
benchmark runs to these cases. Ordinary suite runs retain the complete tests.

## Capture size

The inn ZIP is 476,259,930 bytes (454.2 MiB). Its 6,811 files expand to
1,101,743,024 bytes (1.026 GiB), of which 1,086,147,968 bytes are FGR images.
Exact duplicate contents account for 759,392,588 bytes across 6,042 repeated
files. The trace stores before/after draw thumbnails, shader/constant snapshots
and full-resolution intermediate buffers. ZIP compresses each entry separately
and cannot deduplicate identical files across entries. This is diagnostic
capture volume, not the inn's texture size or a measured VRAM leak.
