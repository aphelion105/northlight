# Experimental HDR presentation

Set ExperimentalHDR=1 in the Renderer section and restart. HDR requires a supported packaged DXVK backend, CommandStream=1, windowed presentation, no MSAA and a working HDR output. A failed capability check leaves SDR active and records the reason in the renderer log. The default is off.

The client continues to see its original 8-bit logical backbuffers. Northlight converts the completed legacy world frame to linear scRGB and converts supported UI draws separately. HDRPaperWhite controls scene white, HDRUIWhite controls UI white, and HDRMaxNits, HDRContrast and HDRExposure control the bounded artistic transfer. This is a presentation conversion of the legacy scene; it does not recover highlights already clipped by the original renderer.

Logical backbuffer counts, readbacks, swapchain queries, cooperative level, Reset/resize, original shader bindings and untouched constants are covered by the isolated integration oracle. The rare-font boundary check uses a recording device to keep world/SSR work before UI; that host check does not rasterize water.

Run tests/verify_hdr_integration_gpu.py with the two plain DXVK DLL paths, a UI pixel shader captured from your own client, and an output directory. All generated test shaders, DLL copies and logs stay under that output directory. Captured game shaders are never committed. The desktop HDR toggle, Alt-Tab, overlays, Magnifier and visible monitor output still require manual testing.

## External HDR tuning

Northlight's world/UI separation happens inside the D3D9 proxy: it converts the world before recognized UI draws and applies HDRUIWhite to supported UI shaders. HDRContrast and HDRExposure affect the world transfer. This internal boundary does not expose a ReShade shader-insertion interface.

[RenoFX documents separate scene/UI brightness](https://github.com/clshortfuse/renofx#separate-scene-and-ui-brightness) through the [RenoDX Upgrade addon](https://github.com/OopyDoopy/renodx/tree/main/src/addons/upgrade), ReShade addon support, scRGB output and a compatible insertion point before UI rendering. That is a candidate for external tuning, but this PR does not implement or validate that handoff. An effect inserted after UI composition receives the combined image, so Northlight's internal separation alone does not establish independent external UI controls.

Addon compatibility validation must establish the insertion order and scene/UI isolation, ownership of the upgraded presentation format, Reset/resize and desktop HDR transitions for the exact 32-bit client, DXVK, ReShade and addon combination. Existing Northlight-only GPU checks do not cover those combinations.
