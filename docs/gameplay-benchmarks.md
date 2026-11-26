# Live gameplay performance benchmarks

`scripts/benchmark_gameplay.py` runs the same WoW 3.3.5a scenes on the stock
client, Northlight through DXVK, and the source-linked x64 client. It uses the
[loopback fixture and spawn packets](../scripts/northrend_fixture_server.py)
from the Northrend harness. The working client, configuration, logs, and add-on
live under a new output directory. The runner copies the runtime and links the
client archives and prebuilt world cache from local inputs. The fixture uses
its own account and never connects to a realm outside localhost.

| Scene | Workload |
| --- | --- |
| `crowd8` | Fixed camera in Dalaran with eight equipped characters. |
| `crowd128` | Same Dalaran viewpoint with 128 characters in staggered rows. It exercises crowd rendering, enchanted weapons, particles, and equipment lights. |
| `crowd512` | Dense Dalaran M2 workload with 512 equipped characters in 16 rows. It stresses animation, model submission, and the renderer's world passes. |
| `ratchet` | Waterfront road with 32 characters around the player. Half carry enchanted weapons. The add-on rotates left for five seconds, then right for five seconds, bringing different geometry, water, and characters into view. |

## Run on Windows

Use a 3.3.5a client of any locale with `Wow.exe`. The runner uses the locale
folder in Data that holds `locale-<locale>.MPQ`, and records it in each report.
If Data holds several, choose one with `--locale`. The x64 stock and DXVK cases use
`WowClient64.exe` and its companion files. Set `NORTHLIGHT_CLIENT` or pass
`--client` explicitly. For Northlight cases, the client directory must also
hold the appropriate `world-cache/Northrend` and `world-cache/Kalimdor` data.
Only the selected scene's cache is needed. To use clean MPQs with binaries
and caches from a modded installation, pass `--data <clean-client>/Data`.
Use the same archives across cases. The temporary client reuses the installed
profile's saved first-launch consent values when present.

The [checked-in graphics preset](../scripts/fixtures/gameplay-benchmark/gameplay-4k.wtf)
uses the same scene settings in every case. Runs default to 3840×2160 and
1× MSAA, with sound, VSync, and FPS caps off. Use `--resolution 2560x1440`,
`1920x1080` or `1280x720` on a smaller display, or `--graphics` to provide another
preset. A resolution equal to the primary display runs maximized, a smaller one
runs windowed, and a larger one is refused.
The default `--desktop private` runs on an inactive Windows desktop. Use
`--desktop interactive` for a background window on the current desktop if the
driver cannot initialize on an inactive desktop. Use `--desktop foreground`
for a focused run when the client brings its window forward during the
Ratchet sweeps. If Windows leaves the game in the background, click the game
window during warm-up and leave it focused. For 3840×2160, the maximized game
window must have a 3840×2160 client area. If the report shows a smaller area,
select a supported resolution that fits your display. Keep the same focus and
desktop mode across repeats. The runner rejects any run that switches focus or
size during the sample and reports the observed values.

| Case | Runtime arguments and expected backend |
| --- | --- |
| `stock-x86`, `stock-x64` | Stock system D3D9. No `--runtime` needed. |
| `x86-dxvk`, `x64-dxvk` | `--runtime` points to an unpacked proxy payload containing `d3d9.dll` and matching `northlight-assets.bin`. `--backend` points to the matching DXVK D3D9 DLL. The runner requires SDR effects and CommandStream with frame skipping off. |
| `native-d3d9ex` | `--runtime` points to the source-linked `WowClientNorthlight64.exe` and matching assets. The receipt records whether the SDR effects device wrapped D3D9Ex or fell back to passthrough. |
| `native-d3d9ex-hdr` | Same source-linked runtime with Windows HDR enabled on the GPU display. The log must confirm scRGB presentation through D3D9On12 GPU transfer. |
| `native-vulkan` | Same source-linked runtime plus `--vulkan-shaders` pointing to the client-compatible shader directory with `manifest.txt`. Defaults to `VulkanShaders` under the runtime. The runner requires an SDR Vulkan swapchain and a successful Present. |

Keep the runtime profiles and optional Tideglass files beside their DLLs.
The runner loads them from `--runtime` when supplied. See the
[build instructions](development.md) for production x86 and experimental
x64 D3D9 builds. Provide the source-linked runtime for the native cases.

```powershell
$env:NORTHLIGHT_CLIENT = 'D:\Games\WoW-335a'
$out = '.local/out/gameplay-run-01'
python scripts/benchmark_gameplay.py --case stock-x86 --desktop interactive --output "$out/stock-x86"
python scripts/benchmark_gameplay.py --case x64-dxvk --runtime <x64-payload> --backend <DXVK-x64-d3d9.dll> --desktop interactive --output "$out/x64-dxvk"
python scripts/benchmark_gameplay.py --case native-vulkan --runtime <native-x64-runtime> --vulkan-shaders <vulkan-shaders> --desktop interactive --output "$out/native-vulkan"
```

Commands run all four scenes sequentially by default. Add `--scene crowd128 ratchet`
to narrow a run. Use `--dxvk-adapter` to select and verify a DXVK device and
`--keep-client` to retain the private copies for investigation. Give each
repeat a fresh output directory. Without Windows Developer Mode the runner
hard-links the client archives, so the output must be on the client's drive. Run one benchmark at a time on the GPU.

Add `--render-profile` to a Northlight D3D9 run (`x86-dxvk`, `x64-dxvk`,
`native-d3d9ex`, or `native-d3d9ex-hdr`) to collect per-pass timings for every
selected scene. The runner enables `Diagnostics` and `RenderProfile` in the
private quality file and disables the extra `DiagReplayProbe` replay. A profiled
run requires confirmation in `northlight-renderer.log`. Keep the flag consistent
across FPS comparisons because profiling adds measurement overhead. RenderProfile
is off by default and is unavailable in stock and Vulkan cases.

Add `--effects 0` to a Northlight run to set `Effects=0` in the private quality
file. This bypasses all Northlight scene effects while keeping the renderer
loaded, and the report shows `northlight=effects-disabled`. The default is
`--effects 1`. The flag is unavailable in stock cases.

## Read the results

The add-on waits 20 seconds after entering the world. The fixture arms it
once the client has requested the entire roster. The next ten seconds of
game-frame intervals come from `OnUpdate` and `debugprofilestop`. The add-on
sends its timing batches after the window closes. The runner checks the exact
roster packet hash, phase order, frame count, elapsed time, window focus
samples, client size, and backend evidence before marking the run `PASS`.

- `summary.json` lists each scene and its verdict. A failed scene makes the command exit nonzero.
- `<case>/<scene>/result.json` gives the roster hash, runtime and graphics hashes, observed backend, focus mode, and FPS results. Ratchet also reports each sweep separately.
- `client-runtime.log` records the verified client executable bitness, observed D3D9, D3D9Ex or Vulkan path, DXVK version, D3D9On12 status, output mode, and Northlight effects. The same data appears under `runtime` in the JSON reports.
- `frame-times.csv` contains the raw game-frame intervals in milliseconds. `avg_fps` is the frame count divided by elapsed seconds. `low_0_1_fps` is 1000 divided by the mean frame time of the slowest `ceil(frames / 1000)` frames. `worst_ms` records the longest measured interval.
- `server.log`, the launch profile, and available renderer and driver logs are retained for failed runs too.

The DXVK console result reports `northlight` and `dxvk` even when timing fails.
`northlight=effects-enabled` confirms that Northlight wrapped the D3D9 device
with effects enabled. The benchmark disables the Northlight settings overlay,
so the settings panel shortcut is unavailable in the staged client.

The D3D9 report uses the renderer's quality log, applied settings, and effect
toggles. It reports which effects are enabled for the scene. The Vulkan report
uses the contact AO and bloom status after the measurement, with the engine log
as fallback. A client that lacks those controls reports `unavailable`.
`d3d9on12=active` requires a logged GPU transfer through D3D9On12. `loaded`
means the process had the system DLL loaded when sampled after timing.
`unknown` means the runtime evidence was unavailable.

`PASS` qualifies the measurement. Compare repeated measurements on the same
hardware, with the same archives, camera, resolution, graphics and focus mode.
Record differences in HDR and active effects when comparing backends. These are
game-frame intervals. For Present intervals, GPU pass timings, color/depth
parity, or verification of the actual sweep angle and recovery hitches in
Ratchet, use the [Northrend baseline and replay harness](northrend-baseline.md).

Run the fixture and report checks without a client through the source suite.

```powershell
python scripts/run_tests.py --suite source --strict
```
