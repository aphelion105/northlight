# Precipitation through the crypt ceiling

The 2026-11-14 capture from `session-26468-336439796-0`, renderer frame 2996,
shows the Tirisfal crypt. The installed source was
`bd8e5d429a2447972b4edb28ace5a1eda6caf807bc598bdefb3d58516df6dc44`.
Its weather metadata records rain intensity 0.99987, no sky draw, lens exposure
false, and supplemental precipitation fully enabled. The other session folder
with the same PID contains the lighting inputs. Private inputs and working
reports are preserved under `.local/out/rain-occlusion-20261114/`.

## Cause and change

`surface_effects.hlsl` placed rain/snow samples 3.5, 11 and 26 yards along each
view ray. Scene depth rejected particles behind a wall, but could not reject
particles between the camera and that wall beneath an off-screen ceiling.
The camera shelter check controlled lens optics only. Turning off all rain
when the camera is sheltered would also remove rain outside visible doorways.

The existing shelter worker now builds an immutable world-aligned R32F roof
height field from the current GI BVH. Downward rays use the BVH's two-sided,
alpha-tested geometry, independently of the sun/moon or shadow settings.
Each precipitation sample tests its actual world position before applying
animation offsets. Scene depth still clips foreground geometry. Lens exposure
and sand optics keep their separate camera test.

The field is 128 by 128 over 96 yards (64 KiB; 0.75-yard cell spacing). It is
uploaded once per publication and uses one point sample per precipitation layer
inside the existing pass. There is no new geometry draw or full-screen pass.
Stationary cameras do not cause timer-driven rebuilds: eight yards of horizontal
travel, a new BVH generation, a map change or 256 yards vertically requests a
refresh. Cached output is accepted only for the same map/BVH and covered camera.
Unknown coverage suppresses supplemental precipitation while native weather
remains available. Roof boundaries have the field's finite spatial resolution;
very narrow geometry and geometry absent from the resident BVH are limitations.

Weather capture metadata now exports the roof field and whether it is required.
The lab validates and restores those inputs. Legacy captures without a roof
retain their original depth-only replay; they are not silently given a ceiling.

## Checks and reproduction

The crypt cache fixture contains 45,999 triangles around the captured camera
at `(1668.29578, 1695.43372, 127.315063)`. The reconstructed ceiling is at 142.432.
One local CPU stand run took 7.607 ms to request/build/collect the field; this
includes polling and is neither a steady-state frame cost nor a game FPS result.

On DXVK 3.1.1, replaying the captured 3840x2160 depth, constants and HDR scene
changed 4,139 pixels with the old depth-only weather behavior. With the roof
reconstructed from the installed world cache, zero pixels changed in this
interior. This is a precipitation-pass image check, not a whole-frame replay;
the old capture did not record its BVH or roof field. Comparison images and
both FGR outputs are in `crypt-gpu/` under the private report directory.

`test_weather_shelter.cpp` covers floor/roof, exterior, reuse, new BVH, teleport,
map changes and scene lifetime. Its optional arguments
`CLIENT X Y Z OUTPUT` reconstruct a private Azeroth roof fixture from world-cache.
`validate_weather_occlusion_gpu.py --capture WEATHER_JSON --roof ROOF_BINARY`
replays the closed-crypt regression in addition to its synthetic suite.
The GPU suite checks both precipitation kinds and presets, depth bands,
foreground occlusion, off-screen ceilings, open exterior, motion and name masks.
Exterior pixels match the unmasked result exactly. Capture tests exercise the
exported roof field, and lab import tests reject malformed or non-finite data.

Use `NORTHLIGHT_TEST_ALLOW_RUNNING_GAME=1` for independent image checks while
Forever remains open. Do not interpret those runs as comparative game timings.
