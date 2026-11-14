# Character shadow CPU cost and runtime experiment audit

## Evidence

The user run preserved as `.local/out/cpu-live-20261114/candidate-live.log`
loaded source identity `e3e73767650d9b70834f06a5f59d265370f4fcefb3cb098dc8919df6fc12a159`.
It repeatedly toggles **ActorShadows**, while ActorShadowDetail stays at 1.
Those switches disable capture and shadow submission, not just the detailed
filter. Most intervals are too short or straddle settings changes; they are
not a controlled total-FPS comparison.

The actor filter costs about 0.030–0.035 ms GPU in this run. The expensive work
is chiefly CPU capture, preparation and submission. Frame 3937 contains 930
queued model-normal packets (691 submitted after merging) and 662 accepted
world replays. Reducing shadow resolution would not address these CPU costs.

RenderProfile was enabled. Its old actor preparation experiment alternated
10-second windows between the worker and inline rendering-thread preparation:

| Logged mode | Samples | Median selection preparation | Maximum |
|---|---:|---:|---:|
| `ab-inline` | 12 | 0.2335 ms | 0.343 ms |
| `worker` | 12 | 0.0115 ms | 0.014 ms |

Worker CPU time itself had a median of 0.228 ms, with median join wait 0.002 ms
and inline tail 0.009 ms. Offloading overlaps work; it does not eliminate that
work. These samples justify removing the forced inline windows, but do not
establish a corresponding total-FPS gain. The user's toggles and scene changes
prevent interpreting them as a matched benchmark.

## Removed from normal profiling

- The clock-based actor worker/inline switch. Worker eligibility still depends
  on core count, actor shadows, stable selection and worker health. Exception,
  watchdog, low-core and unavailable-thread fallbacks remain.
- Recomputing every sixteenth worker output into a second cache. Exact
  comparisons now run in the external worker test instead of the game.
- Repeating the declaration/handoff work for every published record solely to
  measure a warm lower bound (`handoffWarmUs`, 2.5 microseconds median here).
- Six 256-iteration synthetic draw-gate measurements: recursive lock, mirror
  guard, shader lookup, GetVertexShader, clock and weather-match loops.
- T/U alternation of diagnostic timing scopes. Each RenderProfile sample now
  measures the real path consistently; `DRAWGATE` reports actual counters and
  times, without obsolete synthetic benchmark columns.

No shaders, shadow selection quotas, resolution, update cadence, actor radius,
animation inputs, alpha cutouts or quality settings changed. RenderProfile=0
still avoids the per-draw timing scopes. Ordinary measurements retain their
sampling and overhead calibration.

## Other experiments checked

| Mechanism | Production behavior / decision |
|---|---|
| `DiagReplayProbe` | Explicit additional replay experiment, default 0 and disabled in this run. Retained as an opt-in developer tool; RenderProfile alone cannot enable it. |
| `NORTHLIGHT_EXPERIMENTAL_SHARED_POM` | Explicit environment opt-in; disabled unless value is `1`. No automatic alternation. Retained for separate full-pass experiments. |
| Static dirty-rectangle image verification | Compile-time false; no scratch render or readback in the DLL. |
| Static plan reference comparison | Default false; enabled only by its test entry point. |
| Snapshot, memcmp and mirror-gate counterfactual macros | Default 0; deliberately broken test variants, not extra production work. |
| Constant-cache and mirror-state verification | Functional fault detection/repair; retained because removing it could preserve stale state and change the image. |
| Display switches / frame interval A/B comments | User-operated effect toggles and measurement-window boundaries; no automatic algorithm comparison. |

The audit covered runtime sources, profiling/probing consumers, clock-driven
branches, environment switches, reference paths and benchmark/self-check calls.
Historical reports and test-only reference implementations were preserved.

## Validation and artifacts

Run `scripts/run_tests.py --suite source --strict` and the `architecture` suite,
plus `test_diagnostics_switch.py`, `test_production_draw_timers.py`,
`test_hdr_boundary.py` and `test_ui_water_boundary.py`. Reports and installation
receipts are under `.local/out/shadow-cpu-20261114/`.

`test_prepare_worker_windows.py` runs the actual production preparation block
with random profiling flags and inline fallback cases, comparing every output
bit-for-bit to the reference. It covers 2000 integration frames plus worker
handover, exceptions, wake races, watchdog recovery, cache lifetime, quarantine
and shutdown scenarios. The Windows variants are O2 and UBSan. The existing
supported-host driver retains ASan use-after-free counterfactuals and TSan;
those are not claimed as Windows coverage. Zig's MinGW target accepting the
ASan flag did not provide the expected heap instrumentation in the port trial.

Component correctness and removed operations do not prove live game FPS.
The next user run should show no `ab-inline` windows. Remaining character-shadow
cost includes capture and replay submission and requires further measurements.
