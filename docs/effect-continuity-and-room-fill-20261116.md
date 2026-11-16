# Effect continuity and room fill, 2026-11-16

This follows the material relief fix. Private evidence and acceptance artifacts
are in `.local/out/effect-continuity-followup/`; personal client settings are
preserved, including GIBrightness=-100 and GIStrength=240.

## Disappearing effects

The supplied renderer log is from session 52332, build be1b2b08. The initial
world mesh commits at renderer frame 1017. Its successor cannot finish: the
worker reports 104 allocation retries over 118250 ms, with entry headroom
around 457 MiB and the 256 MiB reserve denying further allocations. The old
region remains drawable until camera movement exceeds its valid coverage.
At frame 5897 geometry readiness becomes zero and its GPU mesh/targets retire.
This is a replacement-generation memory peak, not evidence of another Reset:
the log's only Reset is in the menu before entering the world.

Within-region texture sharing already removed repeated fixture copies. A new
region still allocated another copy of pixels retained by the active CPU BVH.
LocalSceneCache now accepts the held immutable previous BVH for same-map texel
reuse. It hashes unique pixel blocks, checks exact byte equality and shares only
their immutable storage. Geometry, texture dimensions, addressing, material
metadata and lamp ownership remain local to each region. Changed bytes acquire
new storage. The logical 256 MiB material limit, process reserve and geometric
coverage checks remain in force. The worker releases its reference after the
build; each accepted material owns its shared pixels independently.

The actual-worker-move stand uses the real Dalaran cache, production worker and
4K/MSAA 8 device. At 457 MiB entry headroom it requires a replacement after a
48-unit camera move while continuously checking that the old mesh remains
drawable. It checks shared storage, commits the replacement and requires the
256 MiB reserve afterward. The unit fixture also checks changed-pixel isolation,
distinct lamp ownership and lifetime after the old scene is released.

## Interior darkness

The prior room-shadow change attenuated the complete authored WMO room bank
with GI occlusion. That bank includes baked/fill/emissive illumination which
cannot be isolated by an outdoor probe estimate. Worse, occlusion was derived
from the brightness-adjusted correction: GIBrightness=-100 made every resident
probe look fully occluded regardless of measured irradiance.

Explicit authored room banks now retain their fill; direct lamp shadows and
incoming shadowed daylight keep their separate owners. Ordinary baked receivers
can still use measured probe occlusion, now independent of the user's indirect
brightness setting. A black measured probe remains occluded, a bright one stays
visible even at -100 brightness, and -100 still adds no indirect bounce.

The GPU tuning sweep also exposed a clamp/fade ordering error: above 100% GI
strength, partial probes could remove the full ambient before becoming resident.
The correction is now clamped before residency blending, including the analytic
sky fallback. Missing probes, uncertain added bounce, black pixels, sky and water
retain their existing contracts. The GI and colour-edge GPU checks cover signed
brightness, extended influence, partial residency, SDR/HDR and independent lamps.

## Evidence limits and test delivery

The supplied diagnostic exports contain incomplete lighting journals with
staging allocation failures; their exported passes are partial evidence, not a
complete frame replay. The production log establishes the replacement stall.
The standalone GPU checks establish image/resource contracts, not game FPS.
Current acceptance reports must match the final source tree; older PASS reports
are historical evidence only. The installed DLL/assets pair and preserved client
settings are recorded in `installed.json` under the private artifact directory.

Live test: walk through the previously failing Dalaran route into an interior at
4K/MSAA 8, compare the room fill with the previous build, then take two captures
and keep moving. Check that shadows, GI and material relief continue after both
captures and after crossing into the next streamed region.
