# Aurora integration into PR #98

PR: https://github.com/lepola/northlight/pull/98

The integration joins remote `b4a9e5cf` with Aurora commits `b02d24fc` and
`f8af2060`. The earlier GI settings commit `54baeabf` is already an ancestor.
The active stutter chat's uncommitted point-palette work is not included.

The sole source conflict was the four native draw adapters in renderer.cpp.
The newer `captureGameDraw`/raw submission route is retained. All four enabled
adapters populate the native draw tuple even when spell and equipment-light
features are disabled, so aurora identity does not depend on those features.
The production aurora transfer remains inside the common native submit route.

## Result

- Whole native Northrend aurora contributes its RGB emission to directional GI,
  including daylight; extra green/purple emission retains the sunset/moonrise gate.
- AuroraStrength in Weather controls the entire visible source and GI together:
  0 off, 100 normal, 200 double radiance. Precipitation/fog toggles are independent.
- Native ribbons emit in HDR and respond to the existing legacy highlight EV.
  Complete native sky is decoded once with native/celestial excess kept linear.
- Native geometry/UV/alpha and shader admission stay authoritative. The diffuse
  CPU source uses curated positions and a coarse captured mip; offscreen native
  animation holds its last complete observation. Live final artistic acceptance
  and whole-game performance are not claimed.

## Validation

The combined production paths pass 147,456 draw-orchestration cases at O2 and O0,
plus four rejected fault/order negative controls. Shared terrain publishing passes
128 cases per optimization level with old failed-proof/missing-publisher controls.
Capture-skip wiring, settings overlay, aurora source/patch tests and 42 directional
GI transport/cache/removal cases pass. HDR sky replay/MRT passes 432 cases on each
of native D3D9, DXVK 2 and DXVK 3, including state/failure restoration.

On the source branch, both supplied captures additionally pass 1,152 colour/alpha/
EV/intensity cases per backend, native daytime RGB source half/double/off checks,
and all-day orbital policy. The source/limitations are recorded in
`aurora-native-hdr-validation-20261111.json`.

Pinned Zig 0.15.2 production build, shader freshness, x86 DLL admission/exports,
Windows package construction and normal package verification pass for this
combined tree. Raw logs are in `out/aurora-integration/`.

Production source identity: `c982fb015a6535614103653a23c8da68ca8a4280d7485d27ac4382f3a029bd15`.
DLL SHA-256: `c626c974edc417e35417ee9beeb2b42dc40f3960c02f281a863dfc71441df503`.
Package: `out/aurora-integration/packages/Northlight-0.3.217-aurora-pr-20261111-Windows.zip`.

The main stutter agent should merge the pushed integration HEAD into its own
committed work before building its next version, preserving the current point
palette optimization. It must not rebuild from the older aurora source branch
and accidentally drop the newer capture optimizations.
