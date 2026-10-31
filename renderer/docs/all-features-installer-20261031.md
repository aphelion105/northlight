# Complete Windows test installer — 2026-10-31

Version: `0.3.216-test.20261031.3`. Production code snapshot: `8ac01fbd`.

| Artifact | Identity |
| --- | --- |
| Archive | `Northlight-0.3.216-test.20261031.3-Windows.zip` |
| Archive bytes | 19,721,103 |
| Archive SHA256 | `a0d98d4b98c3fde2f067ef36b409140b4608afc30e107de2f8eba67b516f9a5f` |
| DLL SHA256 | `d0a4a6545037ab7d194c7b364beb4dae39d529d1dc4aedfacba9055a1651a8cd` |
| Embedded source identity | `961d6804c16c8cef508fab1097e168ae7da5877b34cb406b170114f529af01a6` |

## Included changes

This installer supersedes the optimization-only `.2` package. It combines the published #98 integration, the previously local all-PR assembly and the new linear HDR pipeline. The merge preserves native actor stencil ownership on original draws and keeps HDR sky replay separate.

- Full HDR world/material composition, display-aware live SDR/scRGB output, automatic exposure, independent UI brightness and peak calibration, optional legacy highlight expansion and bloom. Include the distant-horizon and native Ctrl+R boundary fixes, direct screen-blend geometry, lazy sky capture, deferred SDR mirrors and eligible sampler-based legacy decoding.
- Existing vegetation/grass, native present-wait workaround, local-source shadows, settings/Diagnostics, material relief, fog recovery, native sun/cloud-order recovery, bounded EVENT waits, shadow-cache transitions, bounded scene storage, AO quality variants and the 31 previously validated preparation/kernel optimizations.
- Actor ownership AO from #112, with the combined GTAO compatibility work; asynchronous persistent material maps from #97; native herb/gameobject/prop emission from #114, including the latest World models without GameObjectDisplayInfo rows (`ed22e9a9`); and the Telredor point-shadow visibility work.
- The corresponding installer, cache/art builders and dependency closure. Proprietary game assets, generated world caches and private captures are not distributed.

PR #94 retains the independently buildable presentation/UI subset (`f069c3ca`). PR #98 carries this complete integration. Previously recorded exclusions of the new HDR pipeline and actor AO describe older snapshots and do not describe this package.

## Validation of this production snapshot

- Release Windows x86 DLL builds; shader manifests pass the build's provenance checks.
- Offline HDR graph: eight scenarios on each DXVK backend, using controlled native MSAA inputs, actual world/material bytecode, projected effects and final UI. Finite output, unchanged alpha, UI over clear sky/horizon, zero warm target allocations and reduced eligible decode draws pass. Maximum reference difference is below 0.012 scRGB. Timings are component measurements, not whole-game FPS.
- HDR Frame: 36 numerical cases per backend, including exposure timing, headroom, legacy expansion, logical SDR mirror, MSAA and fixed-function transfer.
- Production proxy: 1,468 checks across HDR, SDR and MSAA8/triple-buffer modes on DXVK 2.7.1 and 3.1.1, including Reset/resize, both Present paths, authored UI, logical readbacks and partial writes.
- Actor AO: 40 independent ray-truth/color/contact cases and 160 production AO/blur renders per backend. Actor stencil: 50 cases per backend, including MSAA, occupied native MRTs, NameMask and failure handling.
- Persistent material maps: separate-process GPU produce/consume, corruption repair, settings invalidation and equivalent-input reuse on both backends. CPU store tests cover restart, bounded queues, corruption and unavailable storage.
- Actor-stencil optimized/sanitizer checks, NameMask and command-stream wiring, eleven attached-emitter staging cases, and transactional HDR settings checks pass. Package verification passes payload hashes, pinned runtimes/backends, packaged entry points and installer contracts.

Machine-local evidence is retained under the publication checkout's `.local/all-feature-hdr-check`, `.local/all-feature-tests` and CPU logs. The archive's BUILD-INFO.json and payload-manifest.json authenticate the packaged files.

This is a test installer. Day/night/interior tuning and the HDR full-frame overhead target remain incomplete; the extra actor-AO cost retains #112's documented tradeoff. Physical two-monitor migration remains deferred. This packaging run does not launch WoW or install files into the game client.
