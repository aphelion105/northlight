# Optional equipment light patch

`python renderer/build_equipment_emitters.py --client CLIENT --output STAGING`
reads the user's own 3.3.5a MPQs and creates `equipment-lights-staged.mpq`
plus a report with source archives, model hashes, bone anchors, particle/texture
provenance and exclusions. It never installs anything. StormLib uses the normal
`NORTHLIGHT_STORMLIB` / `northlight.local.ini` path configuration.

Only native v264 models under `Item/ObjectComponents` and spell models referenced
by `ItemVisualEffects.dbc` qualify. A compact, continuously enabled additive
particle must supply a literal colour track and a decodable literal texture.
The new light stays on that particle's original animated bone. Existing lights,
geometry, animations, materials and particle data remain intact.

The 12340 [native material mapping](https://github.com/SatyPardus/wowdecomp/blob/b1c71b8bd108823bca952f16c6f83a7ad33b9648/Engine/Source/Model2/M2Model.cpp#L7394)
maps particle blend 3 to NoAlphaAdd and 4 to Add; blend 2 is ordinary alpha.
The [native light update](https://github.com/SatyPardus/wowdecomp/blob/b1c71b8bd108823bca952f16c6f83a7ad33b9648/Engine/Source/Model2/M2Model.cpp#L2513)
transforms each light through its bone and scene view inverse. These are version
3.3.5a/build 12340 references, not a later client's particle enum.

Particle flag 0x10 is supported: the 12340 particle renderer applies the
emitter transform at draw time, so the light follows the same original bone.
For world-space particle trails the light approximates the current emitter
anchor rather than illuminating every old particle separately.

Every generated native record is read back with the existing track parser and
every MPQ entry is reopened and byte-compared. Generated model binaries belong
to the user's client and must not be committed or redistributed as source.

For a deliberate live test, install the staged MPQ in a verified free patch slot
and enable `[Quality] EquipmentLights=1` in the matching DLL. The native engine
must expose the light in a proven Model VS2/3 draw; Northlight then reuses the
same bounded local-light and fog passes. Dynamic sources share `LocalLightLimit`,
cap at eight, and fade out after disappearing from native draws.

This is an average colour/opacity and compact artistic falloff, not individual
particle simulation. Shader-only glows, ribbons, replacement colours, burst,
multitexture and nonconstant emitter state are excluded. An equipment model with
no qualifying source receives no guessed white light. Native VS1 and sources
not selected into native point-light constants remain unsupported.
