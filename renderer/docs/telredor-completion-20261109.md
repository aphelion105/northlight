# Telredor follow-up, monitoring and interactive-object shadows

This publication completes the accumulated r44–r69 work after `211dd284`.
The earlier component measurements and their limitations remain in the
individual `renderer-throughput-20261108-r44.md` through
`renderer-throughput-20261109-r67.md` reports. Private game logs, extracted game
assets, videos and generated test executables remain under ignored `out/`.

## Latest local run

The frozen input is `out/perf-20261109-r69/game.log`, source identity
`f76aef5fdd86059d4347a2bb20446fad3db175f271e621bc01522f41380d78e1`,
built at 2026-11-09 17:24:42 UTC. It contains no recorded settings transitions
that establish a matched Northlight-on/off comparison. Loading and unfocused
intervals are not used to claim gameplay FPS.

At sampled frame 1651, model-normal capture queues 694 receivers, reads no
source geometry bytes and uses resident geometry throughout; merging still
leaves 450 replay draws. Its GPU interval is 1.416 ms. Frame 1778 queues 684
receivers and submits 479 draws, with a 1.850 ms interval. These intervals can
include command-supply gaps and are not additive to overlapping CPU timers.

The stale previous-draw shader identity is fixed. The new run instead reports
MRT refusals `domain-depth-write` and `blend-srgb`, with no sampled commits.
EVENT synchronization and producer-side loading gaps remain visible; their
elimination is not claimed.

## Common-path corrections

The normal packet identity is reset at the beginning of the shared draw
preparation method. This covers both FrameDrawGates paths and early exits.
The regression executes that production prologue through both actual hook
paths rather than adding the reset only to a mock.

Model normals remain independent of material-detail enablement. They also
feed base lighting, GI and local lights. The unfinished `autoBump->active()`
guard was removed; the retained regression covers absent, disabled and enabled
material consumers without removing lighting receivers.

A non-model opaque depth writer outside the receiver domain can now use the
existing zero-normal MRT program when it writes the same scene depth surface.
Its original geometry, color, coverage and depth test are preserved. The
existing target, viewport, shader, output-slot and binding checks still apply.
Blended/sRGB depth writers and unsupported cases keep complete replay rollback.
Other depth surfaces and non-depth-writers do not disturb normal ownership.
No additional draw, full-size target or normal-resolution reduction is added.

The shared native-submit helper also retains the fully disabled fast path.
With effects and HDR inactive and diagnostics disabled, it submits the original
draw directly. This removes accidental UI/coverage state reads for all four
Draw forms while retaining the HDR and diagnostic paths.

## Frametime display

The graph reads the latest presentation intervals on every UI/HUD draw, using
a fixed-storage try-lock operation without sorting. FPS and low-percent
summaries retain their 250 ms refresh; hardware sampling remains at 1 Hz in
the background. The graph has millisecond ticks, an explicit frame/time span,
a current-sample marker and an exact per-frame hover value. Peaks are retained.
Both the Performance page and closed-menu HUD use the same history.

Actual SDR/scRGB UI readbacks verify the panel, closed-menu HUD, state restore
and input passthrough. Screenshots were inspected. These are private-target UI
checks, not a Windows HDR output-switch or WoW FPS measurement.

## Tester videos

See [interactive-cage-shadows-20261109.md](interactive-cage-shadows-20261109.md)
for the cage investigation and native-versus-shadow alpha coverage work.
See [castle-entry-video-20261109.md](castle-entry-video-20261109.md) for the
separate Westguard Keep video, available storyboard/HUD observations and the
limits imposed by the unavailable full video stream and corresponding log.

## Validation scope

The initial broad Windows and Linux runs exposed stale source-audit signatures,
obsolete shader inventories and test-fixture dependencies. These were repaired
and rerun; acceptance tolerances, original negative controls, mandatory shader
limits and the pinned Zig version are retained. In particular, shared material
fixtures no longer accidentally require a generated private client-program
header merely to compile unrelated tests.

The historical fog/composite comparison keeps its frozen reference bytecode.
Its current-source receipt is updated for the independently reviewed lighting
and base-HDR additions. The optional moon shaders retain their existing
768-slot compiler and runtime capability gates. Transition resource tests now
derive all eight relevant shader limits from their actual manifest instead of
including the separately admitted moon variants or frozen literal counts.

Correctness checks explicitly permitted alongside the other running WoW client
are not used for performance claims. The default game-process guard remains
strict unless the caller supplies the existing explicit opt-in environment or
command-line option.

The broad runs initially reported Windows 294 pass / 48 fail / 249 skip and
Linux 233 pass / 33 fail / 326 skip. Focused reruns resolved all 68 distinct
failed test names seen across those runs and subsequent follow-ups on at
least one supported host. This is not a claim that the entire suite was
rerun on the final source, or that unavailable platform/tool dependencies
passed. The initial celestial sky-restore tolerance failure did not recur
in isolated baseline/current comparisons or the complete six-family rerun;
its original tolerance remains unchanged.

Final checks include:

| Check | Result |
| --- | --- |
| Native model normals, D3D9 and DXVK 1/2/3 | 4,698 frames per backend; exact RGB/normals and complete rollback |
| Native shadow alpha, PS2/PS3 and four source formats | 11,520 cases; zero mismatched coverage/depth pixels |
| Shadow alpha state/failure restoration | 672 cases, including partial scissor rectangles |
| Actual cage near/actor controls | 252 comparisons per backend on D3D9 and DXVK 3; synthetic current poses |
| Map allocation/admission refusal | Cancellation, cooldown and current-pose recovery; no partial composite |
| Screen effects | SDR/HDR/material state and pixels, MSAA 0/2, exact shared 16-bit index-buffer accounting |
| CPU/settings/replay contracts | Windows monitor; Linux O2 and applicable ASan/UBSan checks; static-plan TSan checks |
| Production build/package | Pinned Zig 0.15.2; shader freshness gates; package verification passed |

The confirmed shadow fix preserves native alpha conversion using a scratch
RT0 matching the original color format and the existing R32 depth target at
RT1. It adds no draw. Its lazy cache uses 4.25 MiB for one SDR format at both
map sizes, with a 34 MiB maximum across all supported formats. Unsupported
hardware retains the proved legacy recipe; temporary allocation refusal
defers the map job. See the cage report for the exact scope and call costs.

The machine-readable [publication receipt](telredor-validation-20261109.json)
records the individual evidence hashes and limitations. Private raw reports
stay under `out/`; no client DLL was installed by this completion task.

## Published build

Source identity: `564eb5b385b6d14a6c55125a15fa8df37fcfc440e302da09df70a4b77ba3a958`.
Production build time: 2026-11-09 18:52:10 UTC.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `renderer/frd9.dll` | 10,830,848 | `806dc05ec38ef1e9cf6eb2ec52049d6c754dae3a6e0dc2e8efd6d5234e8f8900` |
| `releases/Northlight-0.3.217-test.20261109.3-Windows.zip` | 20,808,171 | `e7109cd588a1b18721f9263f1e66ee653ef21b2dae6f0ed82b89f72cd48f8b58` |

The ZIP's `payload/d3d9.dll` is byte-identical to this production DLL.
