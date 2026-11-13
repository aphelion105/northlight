# Lighting switches during streaming, 14 November 2026

The supplied `northlight-renderer (4).log` comes from `D:\Wow_3.3.5a`,
not the local development client. Its runtime source identity is
`80bb1b6bfd8bd1b5cc478caf91a94298bdee16aaaab33a6890fd075919ce0579`,
so it includes the previous lamp-register and shadow-history fixes.
The Discord video returned HTTP 403 during the initial investigation. The user
subsequently supplied `D:\dl\2026-11-13_22-25-45.mp4`; it has now been inspected
locally. The first two fixes below shipped in v0.3.218.2 before the clip was
available. Exact video-frame correspondence with log timestamps is not known.

The original log is preserved at
`.local/out/test-output/lighting-toggle-20261114/renderer.log`, SHA-256
`cbd92244eb1760a1b51fbf34f148036654243759a0523645df25cab6646b0ff3`.
`log-analysis.json` in the same folder records the geometry transitions.

## Observations

All 174 GPU-budget samples remain at level 0. All 174 native-shadow-provider
samples report `verified=0 runtime=0 configured=0 owned=0`. The sampled world
lighting uses the unshadowed variant, with no directional-shadow transitions.
This does not support attributing these switches to the moon's orbit, the GPU
budget, or the previously fixed shadow-strength register collision.

The player travels through Northrend. Memory admission repeatedly defers the
next geometry build. The old region reaches the 160-unit retention limit and
is retired before its replacement finishes building/uploading. Complete gaps:

| Renderer frames | Geometry unavailable |
| --- | ---: |
| 13687–14624 | 11.516 s |
| 18626–18748 | 1.703 s |
| 19991–20196 | 6.875 s |
| 20272–20542 | 3.063 s |
| 20766–21014 | 2.765 s |
| 21300–21469 | 1.969 s |

For example, frames 20766–21014 retain valid screen resources and continuous
camera/map state, but have no active mesh. The log names expired publication,
mesh and scene-target release, then commits the replacement mesh after 1781 ms
of GPU staging. Several worker admissions fail with only 355–383 MiB of virtual
address space remaining against the 384 MiB stage requirement. Keeping all old
geometry indefinitely or lowering the reserve would undermine the memory fix.

## GI fallback

`worldGIValue` supplies an analytic hemispherical ambient estimate outside the
finite probe lattice. `GridInfo.w` already guards all probe reads when no valid
atlas is resident. However, the host gated the entire GI pass on geometry
readiness. Losing a mesh therefore removed even this analytic estimate from
walls that never had resident probes, causing a global brightness step.

The pass now follows the effective GI setting/strength. Atlas access retains
its independent readiness guard. No extra mesh, retained CPU snapshot, shader
variant or probe allocation is introduced.

The regression in `tests/world_screen_effects_gpu.cpp` renders the production
compositor with the same camera, scene and empty probe coverage, with/without a
resident mesh. Before the host fix, SDR differed by up to 0.127197 in linear
output. Afterward, the images match exactly in SDR/HDR, ordinary/merged GI,
and MSAA 0/2. Disabling GI remains a visible negative control, and zero strength
matches GI disabled. Before/after evidence is under `before/` and `after/` in
the preserved output folder.

## Directional fog without shadow ownership

The fog gate also tested only `geometryReady`. With an unsupported/native shadow
provider, `setSource` binds neutral shadow maps regardless of mesh readiness.
Nevertheless, a resident GI/fog mesh enabled celestial scattering, and retiring
it disabled that scattering again. A valid GI mesh does not establish occlusion
for volumetric sunlight/moonlight.

Direct scattering now requires both geometry and the existing directional-shadow
ownership predicate. The same gate supplies the ordinary fog/cloud shaders and
the imported atmosphere constants, including the Forever volume atlas and its
raymarch fallback. Ambient scattering and extinction remain independent. The
sampled `WORLD_LIGHTING` record now distinguishes `gi`, `probes` and `fogDirect`
from general geometry readiness.

The production GPU fixture compares a resident mesh without shadow ownership,
an absent mesh, and a resident mesh with owned, fully lit shadow maps. Before
this fix the first two fog images differed by 0.00982666; the unowned mesh was
identical to the owned/direct-lit control. Evidence is in `fog-before-valid/`.
`fog-before/` failed on a fixture assumption about geometry, before comparing
images, and is not evidence of the rendering defect.

After both fixes, the full screen-effects GPU test passes on DXVK 3.1.1 with
MSAA 0/2. All three fog paths have zero difference across mesh availability;
the owned-shadow controls remain visibly different (0.00982666 for ordinary
fog, 0.00154114 for imported raymarch, 0.0014801 for the imported volume).
The GI comparisons still match exactly. Evidence: `both-fixes/`.

The seven new image comparisons also pass on native D3D9 without MSAA, but
the complete native run subsequently fails the existing c154–c156 state-restore
sentinel in the volume-cache fixture. The previously built, unchanged test
binary fails the same sentinel (`native-prior-control/`). Do not report full
native-D3D9 acceptance from these results. The supplied gameplay log uses
DXVK 3.1.1, on which the full test passes.

## Sky background switches in the local video

The clip is 1920x1080, 24 fps, 281 frames (11.708 seconds), SHA-256
`42b24c13f85019b8ba594d735c037d8be46095aa32e777216cf8a87d419e9951`.
Its original and extracted frames are preserved in the investigation folder's
`video/` directory. `switches.png` pairs frames 12/13, 37/38 and 74/75;
`overview.png` shows the flight from Wintergarde Keep toward The Carrion Fields.
The background abruptly alternates between a brighter teal horizon and a dark
horizon band at approximately 0.54, 1.58, 2.50 and 3.13 seconds. Nearby buildings
remain comparatively stable. `brightness.csv` records per-frame region luma;
camera movement and video encoding mean these values are observations, not
an image oracle. The clip therefore warrants a separate sky-path investigation
rather than assuming the two geometry-dependent lighting gates explain it all.

`composeSkyBackground()` closes the sky before the first non-sky foreground
draw. It latched that boundary and then called `celestialContext()`, requiring
both a recognized world draw and valid celestial-disc data. An unclassified
foreground draw before terrain/WMO/actors can thus skip the background for the
whole frame. Deferring the latch would instead repaint native names, particles
or scene grading later, violating existing foreground ownership.

The boundary now uses a separate read-only `skyBackgroundContext()`. If world
context is already available it uses that camera. Otherwise it reads the current
validated sky-phase camera, requires the same map as the last world context,
and reuses only the validated projection and atmosphere palette. It neither
sets world capture valid nor borrows the previous frame's camera basis, and
does not require a visible/valid sun or moon. The original foreground latch
remains. Sampled `SKY_BACKGROUND` records report context availability and the
draw/skip result for subsequent gameplay confirmation.

`tests/test_sky_background_order.py` failed on the original boundary when an
early foreground draw lacked world context. It now exercises both production
methods and passes: current-camera movement, map/cold-start/fault rejection,
unchanged world admission, and foreground/scene-grading preservation. Evidence
is under `.local/out/test-output/sky-background-20261114/{before,after}`.
`tests/test_sky_environment_gpu.py` also passes all 96 shader cases and 16
production-boundary cases on system D3D9, DXVK 2.7.1 and DXVK 3.1.1, including
SDR/HDR, supported MSAA modes, geometry preservation and device-state restore.
The adjacent sky-only recovery test had stale extraction boundaries and a host
compiler requirement even though Windows uses Zig. Those harness issues are
corrected, and the test is now part of the architecture suite so it cannot
silently fall outside routine camera/ownership checks.

The skipped sky pass is reproduced in isolation; attributing the recorded
switches specifically to this gate remains an inference until a new gameplay
log/video confirms it. These component checks do not establish game FPS.
Genuine probe/caster coverage can still change when memory pressure retires
geometry; the admission stalls need separate profiling rather than weakening
memory safety.
