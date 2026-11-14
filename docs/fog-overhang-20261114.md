# Orgrimmar volume cutoff, 14 November 2026

The reported fog edge follows a regional height mask, not the celestial source.
The old atlas and fallback shaders set density to zero whenever a sample is
below the regional field's height. That field rasterizes the **highest** opaque
terrain/WMO triangle in each XY column. A roof therefore removes all fog below
it, including the open street tens of yards beneath the roof.

## Evidence

Native FGR captures in the configured client's `northlight-diagnostics`:

- `session-46680-269393531-0/capture-1-*` (capture 2 has the same edge).
- `session-60800-280247484-0/capture-1-*`.

Both have the cutoff in **opacity**, not just fog RGB. Rebuilding the 64x64
regional field from the client's nine cached Kalimdor geometry tiles and
raymarching a constant medium with no light/shadow input reproduces the
zero-opacity mask with 99.15% and 99.31% agreement, respectively. Thin boundary
differences remain from sampling/filtering. This is a density-support diagnostic,
not an exact replay of the uncaptured imported atmosphere constant bank.

At world XY `(1552, -4440)`, the cache contains an upward WMO surface at
Z=64.349, a downward ceiling at Z=41.038 and an upward street surface at
Z=10.267. At `(1560, -4440)`, the corresponding roof is Z=62.174, ceiling
Z=42.365 and street Z=7.780. The latest captured camera is Z=8.493. Using the
roof as the bottom of all atmosphere explains the empty part of the square.

Local evidence is under `.local/out/fog-placement-20261114/`:
`contact.png`, `capture-stats.json`, `ground-support.png`, the reconstructed
`session-*-field.fgr` files and `field.cpp`/`field.exe`. Captures and extracted
game geometry remain local.

## Correction and regression contract

Remove the below-envelope binary gate from `foreverFog` and
`volumeInjectPoint`. Retain region coverage, authored layer height profiles,
distance curves, density/noise, scene/water endpoints and all shadow lighting.
The separate regional artistic fog path is unchanged. No pass, texture or
allocation is added. This does not introduce a 3D interior occupancy model;
existing regional coverage masks remain the available spatial exclusion.

`test_fog_volume_gpu.py` adds 288 medium cases across both production paths and
all eight injection variants. It checks against numerical Beer-Lambert
integration of the authored profile and verifies:

- raising or splitting the roof envelope cannot change absolute-profile alpha;
- changing the source direction/shadow changes RGB, with bit-identical alpha;
- shaded fog still has nonzero scattering when the authored shade color is nonzero;
- zero regional coverage stays empty, outside-field fallback stays active;
- absolute and terrain-relative height profiles retain their attenuation.

Negative control on the previous compiled shaders failed at the first roof
case: measured transmittance 1 versus expected 0.367879. The complete atlas
test also retains its existing water, odd-size, history, noise, actor-shadow and
shadow-transition comparisons. Offscreen results do not establish whole-game
FPS or replace a new gameplay capture.

## Validation environment

On the RTX 4090 through packaged DXVK 3.1.1 and 2.7.1, each backend passed
288 medium cases plus the existing 795 atlas cases. The maximum transmittance
error against the medium oracle was 0.002330 (absolute); the roof/light
invariance assertions are bit-exact. Native D3D9 device coverage is unavailable
in the current RDP session and is not included in these results. Forever is
allowed to remain open; these are correctness checks, not timing comparisons.

The full screen executor also passes with MSAA off/2x. Final acceptance reports
are retained locally in `final-source`, `final-contracts-dxvk3` and
`final-volume-dxvk2` under the evidence directory above. The initial
`pre-contracts` run is not acceptance: it found stale NightBrightness text
assertions (the literal removal bound and adjacent register uploads), now
updated to the existing production contract. No night-lighting behavior changed.
