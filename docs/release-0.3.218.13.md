# Northlight 0.3.218.13

This update reduces geometry preparation during world entry and removes repeated work from local shadows, animated model capture and fog.

- Point-light transmission geometry uses a bounded loader that accounts for the selected instances and retained triangles. In a Northrend client run, all three nine-tile requests loaded 19,504 triangles each. The preceding run returned empty results for all six requests. World geometry became ready in 10.83 seconds, down from 16.52 seconds across those two entries.
- Local shadows skip terrain outside each moving light's spatial query. Lights with empty terrain share one mesh-wide mask. The spatial tree reserves nodes for its eight-batch leaves. Compatible model draws with overlapping triangle ranges submit their shared triangles once per shadow face.
- Animated material capture resolves the vertex declaration once when building a skin envelope. Material target checks reuse surface descriptions, actor capture defers UV work until a triangle needs it, and the renderer drops an unused replay-bounds cache.
- Volumetric fog creates the shader variants the active pass uses. Temporary allocation failures can recover. Fog-only and zero-strength GI configurations avoid unnecessary mesh uploads.

For a 12-unit light sampled from Northrend, CPU terrain preparation dropped from 17.58 to 2.13 microseconds per refresh in a paired x86 benchmark. The inputs contained 4,533 terrain batches. All 3,840 comparisons produced identical masks, candidate lists and rejection counts. This timing covers terrain preparation for one light.

The two Northrend runs had different crowds and camera paths. They establish a loading improvement in those sessions. A reliable whole-game FPS multiplier still requires a matched route.

## Installation

Close the client and run the installer for your platform. For a manual renderer update, install both `d3d9.dll` and `northlight-assets.bin`. The archives include the converted atmosphere table. Use `SHA256SUMS.txt` to verify downloads.
