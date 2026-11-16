# Material relief and bright room shadows, 2026-11-16

The investigation uses completed native diagnostic captures in
`session-65440-507627578-0`, `session-66284-513529906-0` and
`session-39448-516558609-0`. These are partial pass exports, not a whole-scene
geometry replay. Compact image references are read through `capture_files.py`.

## Relief rejected by receiver marking

A tavern WMO draw (native trace 10 in session 65440) has pixel program
`67f4ef731d4efc4a`, depth writes on, blending off, stencil enabled, function
ALWAYS and reference 0x10. This is the renderer's authored-light receiver tag.
Both material capture and native POM rejected any enabled stencil. Thus our
own marking disabled relief while registration and range/pose work still ran.
The earlier catalogue/fallback fix did not exercise this real draw state.

Material admission now distinguishes write-only marking from a stencil test.
ALWAYS with one-sided stencil can proceed. Private material draws disable
stencil writes and restore the original enable state; native POM retains the
marking state. EQUAL and two-sided tests remain rejected. Depth/stencil
admission precedes geometry snapshots and pose scans.

A second defect appeared when exercising the complex captured WMO variant
`mapobjuspecular_t1`, index 69. Its native GPU-height shader fits, but the
private material variant cannot use GPU height within the shader budget.
The old code allocated/uploaded a subdivided GPU-height mesh, then silently
used the original flat geometry for material depth. Preparation now chooses a
height representation supported by both consumers. This variant uses cached
CPU offsets; simpler programs retain GPU height. Both the native and private
passes use the same mesh. Cache matches cannot reuse an incompatible height
representation. No additional geometry replay or per-frame subdivision was
introduced.

`test_material_receiver_stencil_gpu.py` uses enabled-client WMO bytecode, the
captured declaration's attribute set and a real Dalaran floor texture. It
compares marking on/off at MSAA 1 and 8, requires visible POM changes and changed
material depth, checks indexed/UP equality and the zero-geometry POM fallback.
The output with receiver marking must match its unmarked control exactly.
The existing replay-state test also covers failed POM state restoration and
rejects a real stencil coverage test.

## Authored illumination washing out local shadows

The room-lighting change below is superseded by
[effect continuity and room fill](effect-continuity-and-room-fill-20261116.md).
It incorrectly treated the complete room bank as occludable outdoor ambient,
and brightness-derived occlusion overdarkened interiors at GIBrightness=-100.
The material admission and compatible displacement fixes above remain in place.

The point-map replay already established actual occlusion at the brazier.
The separate prelit/GI image in session 66284 has occlusion alpha between
0.546 and 1, with median 0.671. However, the compositor multiplied this term
by `1 - authoredReceiver`; explicit room lighting ignored all that occlusion.
This left the surface illuminated after its lamp contribution was shadowed.

The compositor now scales the room's own transported RGB by resident GI
occlusion, preserving its hue. It does not subtract unrelated outdoor RGB.
Current local radiance and incoming daylight are added independently, and
missing probes or zero GI mix preserve native illumination. User GI strength
and brightness settings are retained. At the user's GIBrightness=-100, zero
indirect radiance is now honoured on these receivers too.

The extended colour/edge GPU test checks SDR/HDR, ordinary prelit and authored
receivers, missing/resident probes, several occlusion values, and a separately
coloured lamp. Black remains black; local light survives full ambient removal.

## Acceptance scope

Artifacts are under `.local/out/relief-shadow-followup/`. Acceptance reports
must match the final source tree. Component image comparisons do not establish
game FPS. The captured gameplay log contains manual live previews disabling
walls/terrain/grass later in the run, but earlier enabled frames also rejected
WMO relief; disabled settings alone do not explain the problem. The client
quality file still selects its preset defaults for relief on the next launch.

Manual test: compare tavern walls/floor at a grazing angle, then check the
brazier shadow in a dark room and take two captures. The existing Reset recovery
fix is retained. No quality reduction or change to the user's GI settings is
part of this fix.
