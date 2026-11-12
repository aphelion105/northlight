# r67: warm MRT submission, local-light budgets and monitoring

## CPU changes

`model_normal_native.h` keeps last-used vertex/pixel metadata references and
caches the immutable color-surface description once per frame. Current RT0,
depth surface and viewport are still checked at submission. A target, depth
surface or viewport diversion cancels MRT and retains the complete replay
rollback. No unsupported scene is allowed to commit partial normals.

`native_local_light_renderer.h` first checks the requested ready dual/triple
program. A warm three-light hit needs one pair lookup instead of walking
single, dual and triple variants. Cold admission keeps the original
single-to-dual-to-triple ordering and one new attempt per frame.

These remove repeated CPU work; they are not a measured game-FPS improvement.
The r66 GPU result remains a separate heavy-mesh synthetic comparison. No new
uncontended r67 timing is claimed while WoW is running. EVENT stalls still
require a game run of a new production DLL; a successful offline shader test
cannot prove their elimination.

## Settings

See [settings-overlap-audit-20261109.md](settings-overlap-audit-20261109.md).
Performance now contains monitoring. Resource controls moved into the owning
feature's Fine tuning section. Duplicate character CPU/memory rows are gone.
The two local-light limits have explicit total/subset semantics and a versioned
migration preserves the previous effective selection.

## Performance panel and optional HUD

Both consume the same snapshots. The checkbox **Show compact HUD while
settings are closed (this session)** enables a read-only top-left overlay.
It stays visible when the settings close and takes no keyboard/mouse input.
Visibility is session-only; it is not a renderer-quality INI key.

Displayed values:

- Selected-adapter GPU load; temperature and graphics clock when the installed
  driver's read-only API supplies them. Power is currently unavailable (`-- W`).
- Driver dedicated-memory use/total when available, otherwise this process's
  local-memory use/OS budget, explicitly labeled **VRAM game**.
- System CPU and game-process CPU, system RAM and process working set.
- Presentation-interval FPS, 1% and 0.1% lows, mean/max frame time, and the last
  240 intervals as a graph. History is bounded to 4096 intervals. Lows invert
  the mean of the slowest fraction, with 100/1000-sample warm-up requirements.
- Existing Northlight effects CPU/GPU interval and stream EVENT/queue-idle
  counters, window client size, scene target, output size and the requested
  D3D presentation interval; CPU/GPU model names.
- Existing scene-builder address-space admission and CPU/GPU memory snapshots
  with their age and coverage, below the live metrics.

Unavailable readings are `--`, including unprofiled GPU intervals and
non-streamed EVENT counters. No timestamp queries, blocking GetData calls or
quality controller are enabled by this feature. Effects GPU interval is not
whole-game GPU busy time; effects CPU is elapsed time, not CPU utilization.
Focus changes, diagnostic captures and gaps over two seconds break FPS
sampling. The UI explains these definitions in a collapsed details section.

Hardware collection runs at 1 Hz in a below-normal-priority worker, using
system DLLs loaded from the system directory: DXGI for adapter identity and
process memory, WDDM counters for generic engine load, optional NVAPI for
NVIDIA sensors. Ambiguous adapters stay unavailable. No D3D device calls run
on this worker. Render-side publication is fixed-storage and try-lock only;
reads and text summaries refresh at 4 Hz. Collection expires two seconds
after neither Performance nor HUD requests it. Device reset refreshes
presentation metadata and clears frame history. NVAPI's MIT notice is included
in release-package inputs.

## Validation

Artifacts under `out/perf-20261109-r67/`:

- `validated/validation.json`: 4290 correctness frames on each of native D3D9,
  DXVK 1, 2 and 3 (17160 total), 990 MRT commits per backend. Includes new
  RT0, depth-surface and viewport diversion cases and exact replay rollback.
  The three optimized native/MRT/light headers still match those hashes.
- `quality-final.log`, `settings-service-final.log`, `check-source-shadows.log`:
  defaults/parser, migration/edit/idempotence and bounded source/shadow
  selection including shrinking totals with outgoing fades.
- `check-budget-final.log`, `overlay-contract-completed.log`: source wiring,
  menu input/drafts and embedded font.
- `check-test_live_settings.log`, `check-test_frame_focus_cache-completed.log`,
  `check-test_effect_diagnostic_wiring-final.log`, `check-test_window_size_filter.log`:
  live apply, cached focus lookup, diagnostic exclusions and window filtering.
- `check-monitor.log`: 141 ms hitch, low-percent mean/warm-up, exclusions,
  wraparound, owner lease and stream-counter accounting.
- `check-hardware.log`: actual 32-bit CPU/RAM/adapter/NVIDIA sensor smoke test.
- `ui-confirmation/overlay-gpu/validation.json`: actual panel on private SDR
  and scRGB targets (UI whites 160/320), state restoration, closed-menu HUD,
  input passthrough and screenshot inspection. Hardware values are real;
  fixture FPS and synthetic memory ledgers are not WoW measurements.
- `renderer-final-source.log`: full renderer translation unit compiled at O0;
  overlay O2 linking is covered by the native GPU UI test. No production DLL
  or installer was linked or installed.
- `diff-check-final.log`: clean whitespace check.

The native UI was inspected once, fixed for the clipped fallback VRAM label
and overly long inline methodology, then confirmed on final screenshots.
Windows HDR output switching is not exercised by the private-target test.
