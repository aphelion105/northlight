# Scene normal capture: native MRT versus depth-seeded replay

This is a direction study, not a DLL release or a production MRT activation.
Both alternatives have executable GPU prototypes. The experiment was removed
from production headers/shader manifests after measurement; the r64 composition
changes and all earlier work are retained. No game run, client installation,
DLL build, package or visual-quality reduction was performed.

## Why this stage

The frozen r63 game run in `out/perf-20261109-r64/game.log` reports
1.195–1.746 ms in ModelNormals on the three geometry-ready GPU samples.
Frame 3175 queues 784 receivers and submits 502 normal draws after merging.
Source reads are already zero on that frame. Avoiding replay can remove draw
submission, vertex processing and normal-target rasterization; simply caching
more source bytes cannot remove those costs.

ModelNormals supplies base-lighting receiver normals for sunlight, GI and local
lights. Material AutoBump capture is a different, independently gated path.
Neither prototype disables those consumers or changes AO.

## Executed comparison

Artifacts are in `out/perf-20261109-r65/`:

- `prototype/`: frozen candidate renderer/patch headers, shader bytecode,
  generated header and GPU comparison source. Production has no dependency
  on this directory.
- `validate.py`: builds the frozen experiment and runs native D3D9 and packaged
  DXVK 1/2/3. Run with `--hdr` for FP16 color validation, or `--skip-build
  --timing` for the 4K comparison. Windows compilation/GPU runs require normal
  elevated execution, as specified by AGENTS.md.
- `results.json`, `normal-scene-*-oracle.log`, `normal-scene-*-hdr.log` and
  `normal-scene-*-timing.log`: measurements and comparisons.
- `prototype-sources.json` records the candidate's original inputs;
  `prototype-sha256.json` identifies the archived files, including the adjusted
  standalone bytecode path.

The fixture uses stock human M2 geometry and rigid, skinned, weighted and
four-weight stock VS3 programs, a stock textured native PS, native alpha test,
and real D24S8 depth. It covers model distances 2/20/47/49 and partially
transparent texture coverage. Nonzero coverage is asserted. All four backends
pass both SDR and FP16 color comparisons: 32 scene cases per mode/backend,
with an MRT comparison and a seeded-receiver comparison in each case (512
comparisons total). Native color and MRT model-normal texels are byte exact
in these cases. For depth seeding, hidden texels may change; the CPU oracle
compares the normals retained by WorldNormals' depth acceptance rule. This is
not a full game-scene, material-displacement or temporal-lighting acceptance test.

Representative 3840×2160 timing, 256 indexed skinned-model draws, visible case:

| Backend | Native + separate normals, ms | Native + seeded normals, ms | Native MRT, ms |
| --- | ---: | ---: | ---: |
| Native D3D9 | 0.376840 | 0.421890 | 0.188420 |
| DXVK 1 | 0.126976 | 0.179200 | 0.063488 |
| DXVK 2 | 0.135168 | 0.179200 | 0.064512 |
| DXVK 3 | 0.128000 | 0.174080 | 0.064512 |

These are GPU timestamp intervals, including CPU submission gaps. They are not
exclusive shader execution times or predicted game FPS. The experiment binds
MRT once for a model group; it excludes game-hook/factory/fallback overhead,
clear/readback and MSAA resolves. Two warm rounds precede ten retained samples
per variant, interleaved forward/reverse. CPU submission in the same DXVK 3
case is 0.0991/0.1005/0.0497 ms. The interval saving is therefore partly fewer
commands and earlier command delivery, precisely the work replay duplicates.
The separate fully occluded case also fails to make depth seeding faster.

## Direction 1: conservative scene-depth seed

The prototype fills the existing private normal depth buffer from resolved
scene depth. It uses no new image allocation. Its far boundary exceeds the
consumer's `max(0.006, distance*0.0015)` acceptance tolerance, with an additional
D24 quantization margin. Replay can then reject hidden fragments before their
normal PS runs. It retains the existing nearest-model depth competition.

Admission proves a canonical clip-Z/clip-W shader pair and checks matching
projection constants, depth range, triangle topology, clipping and zero bias
across queued receivers. Unknown/mixed projections retain ordinary replay;
shader/setup failure restores the cleared private depth path. A previous
hazard flush also prevents late seeding of an already populated normal buffer.

It preserves accepted receiver data in the fixture but keeps all CPU commands
and vertex work, adds a 27-slot full-resolution depth-writing draw, and can
benefit only hidden pixel work. The measured total increases. It is rejected
for production; no dormant seed shader or per-frame eligibility scan remains.
This measurement does not prove it loses on every conceivable GPU/scene.

## Direction 2: emit normals in the native draw

The prototype preserves the original VS outputs and the entire native PS,
including native color, alpha and kills. Extra interpolants carry the native
post-skin normal and clip position. An unused MRT receives the same normalized
normal and view distance as replay. Beyond capture range it writes zero rather
than discarding the native fragment. It rejects output-slot/input-semantic
collisions, depth-output shaders and unavailable resources; these rejection
checks are exercised. It currently supports the tested PS3 families.

This removes the duplicate model draw in the supported isolated group and is
the preferred direction. It is not yet connected to the game's native draw.
The following integration constraints are material, rather than additional
backend-specific rendering policies:

1. **Final shader provenance.** `materialNativeDraw` can replace the material
   pair, `nativeLocalSubmit` can replace it again for lamp shadows, and
   `effectCoverageDraw` can own an auxiliary output. `nativeLocalDraw` already
   validates the original/private POM and displacement pair. An outer MRT
   wrapper would break that identity proof; an inner wrapper must preserve
   all final outputs and use the final deformation. Extend existing private
   pair provenance and build a combined program once per compatible pair.
   Do not add an independent per-draw shader-recognition stack.
2. **Coverage and depth are shared.** MRT uses the native depth surface;
   separate replay has private depth. Native LESS/EQUAL, interpenetrating
   occluders near the consumer tolerance, displacement and target/depth clears
   need whole-scene comparisons. Exact isolated model output is insufficient.
3. **No naive partial replay.** The GPU fixture demonstrates a near native-MRT
   model overwritten by a far fallback model replayed into fresh private
   depth: 268–269 near pixels are lost, across all tested families/backends. That can
   produce incorrect normals and unstable lighting. It is an experiment
   demonstrating an integration hazard, not evidence of a current game bug.
4. **Smallest safe fallback transaction.** Initially retain the existing
   immutable queue until the effects boundary. Commit native MRT only when
   every eligible receiver and relevant state transition is covered. On any
   unsupported pair/hazard, discard the partial MRT contents, clear normal
   color/private depth and replay the entire original queue. Never replay
   only unsupported draws over the native result. This can reuse the current
   normal texture, without a second full-size normal plane. It initially
   retains CPU queue cost; the isolated timing must not be presented as its
   real-world saving. A regularly incomplete frame must not repeatedly pay
   both paths; admission/cache evidence must prevent that regression.
5. **RT ownership and MSAA.** Respect all occupied auxiliary slots and shader
   outputs; verify simultaneous RT/format capabilities. HDR must preserve its
   final native color shader. The latest game log has sourceMSAA=0. With MSAA,
   an auxiliary surface must match sample count and its resolve/edge semantics
   and memory cost need separate measurement. Start with the existing full
   replay for these cases; do not silently allocate another 4K MSAA FP16 plane.

The next production implementation should therefore start with the existing
native-pair factory/provenance and a complete-frame fallback transaction, using
the same policy on native D3D9 and all DXVK versions. Its acceptance criteria
are unchanged native color/depth, correct normal ownership with mixed models
and occluders, no loss of local shadows, and a measured net saving including
hooks, fallback and queue retention. No EVENT stall is claimed fixed by this
study.
