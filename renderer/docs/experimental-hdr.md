# Experimental HDR presentation

Set ExperimentalHDR=1 in the Renderer section and restart. HDR requires a supported packaged DXVK backend, CommandStream=1, windowed presentation, no MSAA and a working HDR output. A failed capability check leaves SDR active and records the reason in the renderer log. The default is off.

The client continues to see its original 8-bit logical backbuffers. Northlight converts the completed legacy world frame to linear scRGB and converts supported UI draws separately. HDRPaperWhite controls scene white, HDRUIWhite controls UI white, and HDRMaxNits, HDRContrast and HDRExposure control the bounded artistic transfer. This is a presentation conversion of the legacy scene; it does not recover highlights already clipped by the original renderer.

Logical backbuffer counts, readbacks, swapchain queries, cooperative level, Reset/resize, original shader bindings and untouched constants are covered by the isolated integration oracle. The rare-font boundary check uses a recording device to keep world/SSR work before UI; that host check does not rasterize water.

Run tests/verify_hdr_integration_gpu.py with the two plain DXVK DLL paths, a UI pixel shader captured from your own client, and an output directory. All generated test shaders, DLL copies and logs stay under that output directory. Captured game shaders are never committed. The desktop HDR toggle, Alt-Tab, overlays, Magnifier and visible monitor output still require manual testing.
