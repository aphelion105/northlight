# Visible-sun capture and late-veil transfer follow-up

The `.7` client session entered the requested character autonomously, completed the stationary [HDR/SDR comparison](hdr-optimization-wave4-20261031.md), then changed only the camera for a visible-sun capture. The character did not travel. These camera frames are excluded from the stationary comparison. Private game captures and account information are not published.

## Capture findings

The first upward view was obstructed by the animated mount and did not adequately contain the compact solar core. A subsequent first-person view contains the actual native-direction sun: approximately `(0.228, 0.228, 0.947)`, rather than the lower lighting-source direction printed in the general world-camera log. Native and rendered tangent radii both equal approximately `0.0416426`.

The FP16 pre-display scene is finite. Its maximum RGB is approximately `(23.766, 21.781, 22.406)`; automatic exposure is about `+0.260 EV`. Pixels whose maximum RGB exceeds one occupy 0.735% of the frame; those exceeding two occupy 0.273%. Using the logged body bounds, the median maximum channel falls from about 22.75 inside half a body radius to 0.758 at 1–2 radii, 0.469 at 2–4, and 0.276 at 4–8. This distinguishes the compact emitter from the broad tail. It does not measure panel nits or prove occlusion in every scene. SDR-converted desktop screenshots alone cannot establish the HDR bright-area size.

Private capture: `session-14540-32033875-0/capture-1-hdr-linear-scene.fgr`; numerical summary: `.local/hdr-autonomous-20261031/package7-visible-sun-summary.json`. The complete session log SHA-256 is `d07fa768bdefce75826b27208758f9b24c1986ec17da4827d925a2e28e2413f8`.

## Late-veil correctness fix

`CelestialVeilPS` still applied the nonlinear shoulder to a decoded tint. Its HDR output now decodes the authored shoulder's result, matching the corrected sky halo. The ordinary SDR transfer is retained. The main linear HDR pipeline already omits this separate veil to avoid duplicate bloom; this fix closes the alternate FP16 shader path and is not claimed as the explanation of the captured solar core or an FPS optimization.

The new independent GPU oracle fails on the previous production shader before the fix. With the fix, both DXVK 2.7.1 and 3.1.1 pass 43 sun cases, including 147,456 veil reference channels across white/warm/blue tints and four weights. Maximum reference error is `0.000923097`; the retained wrong-order control reaches `0.965911`. Alpha is exact; a completely hidden source contributes zero over ordinary geometry and clear sky. The existing raw-depth horizon, compact source and excessive-energy controls remain active.

The production build, nine-workload composition graph, 2776 Frame cases per backend and production HDR/SDR/MSAA8 proxy matrix pass. The sun runner records source, generated production header, harness, executable, backend, compiler and counterfactual hashes. Private acceptance: `.local/hdr-autonomous-20261031/wave5-final`.

## Rejected bloom experiment

Reordering bright extraction to calculate its common scalar before multiplying RGB passes numerical acceptance but leaves static slots and temporary registers unchanged. The paired visible-sun replay shows no consistent timing improvement: DXVK 2 no-MSAA/MSAA8 `0.155051/0.547136 → 0.155157/0.547285 ms`; DXVK 3 `0.150912/0.549397 → 0.153365/0.541867 ms`. It is reverted and not counted as an optimization.

## Remaining measured priorities

1. Final HDR grading, MSAA target traffic and presentation: the `.7` tail is about 0.49 ms longer than SDR. Use the native-MSAA presentation stand and captured post replay; preserve per-sample edges, logical readbacks, UI and recovery. The previously tested explicit resolve was slower and is not shipped.
2. Shared fog/cloud work: FogMarch is about 0.90 ms and FogClouds about 0.61–0.62 ms in both modes. These dominate effect generation but do not explain the HDR-only delta. Preserve numerical radiance, sampling budgets, occlusion and temporal behavior when testing reductions.
3. CPU admission/state inspection: measured HDR admission is about 0.05 ms and finishing about 0.02 ms. Require measured savings and invalidation/failure controls before introducing state caches. Finish is nested inside boundary time; transfer time includes native submission.

There are seven accepted mechanisms in the preceding two optimization waves. This follow-up adds a correctness fix and rejects a performance candidate. Sixty independently validated optimizations per direction have not been established. The remaining full-frame overhead, representative night/interior coverage and deferred two-monitor test stay open.
