# Complete Windows test installer - 2026-10-31

Version: `0.3.216-test.20261031.4`. Production code snapshot: `a08bb8ce`.

| Artifact | Identity |
| --- | --- |
| Archive | `Northlight-0.3.216-test.20261031.4-Windows.zip` |
| Archive bytes | 19,726,367 |
| Archive SHA256 | `8d65b000e528cacd2361d384547191ad23121026523b2d4b0facda118d9bf3f9` |
| DLL SHA256 | `c3ed455d49bfb4f95a08eb44cedd3ad8640c97ef73bdf2ea2b7d723260964e0f` |
| Embedded source identity | `1bb28133ce6199db2c8768bfc4b459267c61be447ef5068607e122d1b54b138e` |

## Included changes

This installer supersedes `.3` and adds capture-driven glare fixes and HDR composition optimizations. It combines the published #98 integration, the previously local all-PR assembly and the new linear HDR pipeline. The merge preserves native actor stencil ownership on original draws and keeps HDR sky replay separate.

- Full HDR world/material composition, display-aware live SDR/scRGB output, automatic exposure, independent UI brightness and peak calibration, optional legacy highlight expansion and bloom. Include the distant-horizon and native Ctrl+R boundary fixes, direct screen-blend geometry, lazy sky capture, deferred SDR mirrors and eligible sampler-based legacy decoding.
- Existing vegetation/grass, native present-wait workaround, local-source shadows, settings/Diagnostics, material relief, fog recovery, native sun/cloud-order recovery, bounded EVENT waits, shadow-cache transitions, bounded scene storage, AO quality variants and the 31 previously validated preparation/kernel optimizations.
- Actor ownership AO from #112, with the combined GTAO compatibility work; asynchronous persistent material maps from #97; native herb/gameobject/prop emission from #114, including the latest World models without GameObjectDisplayInfo rows (`ed22e9a9`); and the Telredor point-shadow visibility work.
- The corresponding installer, cache/art builders and dependency closure. Proprietary game assets, generated world caches and private captures are not distributed.

PR #94 retains the independently buildable presentation/UI subset (`cbdb7d97`). PR #110 receives the shared raw-depth horizon occlusion fix (`1ce59953`). PR #98 carries this complete integration. Previously recorded exclusions of the new HDR pipeline and actor AO describe older snapshots and do not describe this package.

## Validation

- Release Windows x86 DLL builds; shader manifests pass the build's provenance checks.
- Offline HDR graph: nine scenarios on each DXVK backend, using controlled native MSAA inputs, actual world/material bytecode, projected effects and final UI. Finite output, unchanged alpha, UI over clear sky/horizon, zero warm target allocations and reduced eligible decode draws pass. Maximum reference difference is below 0.012 scRGB. Timings are component measurements, not whole-game FPS.
- Raw-depth celestial occlusion, bounded HDR halo energy and omission of the duplicate HDR veil; native alpha-only UI attenuation and optional combined world/material application. All nine workload timings pass the unchanged regression gate. Actual WorldRenderer tests pass caps, creation-failure, diagnostic and expansion fallbacks on DXVK 2/3 with MSAA0/2. The combined shader requires 676 slots; unsupported devices retain two passes. Legacy expansion also retains two passes after a measured DXVK 2 regression.
- Sun oracle: 16 cases per backend, including horizon and energy negative controls. Five celestial replay families pass on both backends. See [capture findings and exact component timings](hdr-glare-performance-20261031.md); these do not establish whole-game FPS or panel luminance.
- HDR Frame: 36 numerical cases per backend, including exposure timing, headroom, legacy expansion, logical SDR mirror, MSAA and fixed-function transfer.
- Production proxy: 1,468 checks across HDR, SDR and MSAA8/triple-buffer modes on DXVK 2.7.1 and 3.1.1, including Reset/resize, both Present paths, authored UI, logical readbacks and partial writes.
- Carried-forward feature acceptance from `.3` (unchanged feature implementations): actor AO has 40 independent ray-truth/color/contact cases and 160 production AO/blur renders per backend. Actor stencil: 50 cases per backend, including MSAA, occupied native MRTs, NameMask and failure handling.
- Carried-forward material-map acceptance from `.3`: separate-process GPU produce/consume, corruption repair, settings invalidation and equivalent-input reuse on both backends. CPU store tests cover restart, bounded queues, corruption and unavailable storage.
- NameMask, celestial veil, sun hue and command-stream checks pass. Earlier actor-stencil sanitizer, eleven emitter-staging cases and transactional settings acceptance remain associated with their original snapshots. Package verification passes payload hashes, pinned runtimes/backends, packaged entry points and installer contracts.

Fresh capture analysis, numerical/GPU acceptance and timing evidence are retained in `.local/hdr-glare-20261031`. Earlier feature acceptance is retained under the publication checkout's `.local/all-feature-hdr-check` and `.local/all-feature-tests`. The archive's BUILD-INFO.json and payload-manifest.json authenticate the packaged files.

This is a test installer. Day/night/interior tuning and the HDR full-frame overhead target remain incomplete; the extra actor-AO cost retains #112's documented tradeoff. Physical two-monitor migration remains deferred. This packaging run does not launch WoW or install files into the game client.
