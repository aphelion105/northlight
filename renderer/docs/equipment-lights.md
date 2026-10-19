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

For a deliberate live test, install the staged MPQ in a verified free patch slot.
`EquipmentLights` defaults to 1 in Quality and 0 in Balanced/Performance; an
explicit `[Quality] EquipmentLights=0/1` overrides that preset. The native engine
must expose the light in a proven Model VS2/3 draw; Northlight then reuses the
same bounded local-light and fog passes. Dynamic sources share `LocalLightLimit`,
cap at eight, and fade out after disappearing from native draws.

This is an average colour/opacity and compact artistic falloff, not individual
particle simulation. Shader-only glows, ribbons, replacement colours, burst,
multitexture and nonconstant emitter state are excluded. An equipment model with
no qualifying source receives no guessed white light. Native VS1 and sources
not selected into native point-light constants remain unsupported.

When more than two anchors qualify, keep the two strongest proven sources and
report omitted sources. This preserves the model instead of excluding it after
support for more particle flags is added. Capture deduplicates semantic light
banks across a frame and reserves half its reads/banks for skinned draws. This
does not identify a source as belonging to the player; nearby creatures and
animated scenery may share that draw type.

## Mounts

`python renderer/build_mount_emitters.py --client CLIENT --output STAGING`
reads mounted-aura creature entries from Spell.dbc, resolves them through this
server's `Cache/WDB/LOCALE/creaturecache.wdb`, then reads CreatureDisplayInfo and
CreatureModelData. A creature entry is not a display ID. Uncached entries are
reported rather than guessed. `--creature-cache PATH` selects another cache.

Only actual additive particle colours and textures qualify. The two strongest
anchors keep their original bones, emission-rate and enabled-state tracks.
Models with existing lights are preserved. A shared model with display-specific
particle replacement colours is excluded when no universal light colour exists.
Shared NPC uses of a mount model also receive its native light. Existing source
bytes, particles, geometry, DBC and WDB remain unchanged; the MPQ contains M2 only.

## Spells

`python renderer/build_spell_emitters.py --client CLIENT --output STAGING
--exclude-report EQUIPMENT_REPORT` stages a separate, conservative spell patch.
The default accepts only exclusively referenced missile models with luminous,
finite particle tails and no ribbons. Read-only DBC references prove the model's
owners; nothing edits or packs Spell.dbc. Original animated rate and enabled
tracks drive the new light. A 1 ms global colour refresh keeps the 12340 native
light updater committing changing intensity.

The current client yields only one such safe holiday missile. General cast,
impact, aura and area models are deliberately excluded: the native engine can
stop their particles without stopping M2 lights. `--audit-broad` is for reports
only and its output is marked unsuitable for installation. Adding live spell
illumination broadly uses the live draw path below; these M2 patches do not
provide that capability.

## Live particle lighting

With `CommandStream=1`, `EquipmentLights=1` also samples accepted native Particle, ParticleUnlit and
single-texture additive Model draws. The shader's proven position, COLOR0 and
UV instructions supply the current particle position and colour. Texture RGB
and alpha come from successful CPU uploads, including native BC formats and
sRGB sampler state. No GPU texture or buffer readback is started. Unsupported
shaders, missing uploads, multitexture effects, UI draws and ordinary alpha
surfaces stay unlit.

Sampling is bounded to 32 additive draws and 16 triangles per draw. Nearby
samples form compact lights, sharing the eight-source cap with native item and
mount lights. Overlapping native sources are suppressed. Unlike native lamps,
particle lights have no TTL or selection fade: only this frame's accepted
submissions contribute, including draws deferred by SpellLayer. This preserves
the effect's authored opacity and end of life without touching Spell.dbc.

Cold Blood 14177 is the reference case: its cast and aura use the native
right-hand attachment and blue particle textures. The read-only fixture and
native shader/texture tests cover this asset chain. Runtime diagnostics report
`PARTICLELIGHT` accepted draws, cache misses and selected source RGB/position;
`PTEX` reports the bounded replay texture cache. A new dynamic buffer may miss
its first draw until a subsequent successful upload; stale bytes never supply a
light. In-game appearance and frame cost still require a client comparison.

All these sources share `EquipmentLights`, `LocalLightLimit` and the eight-source
Northlight admission cap. Those limits do not cap the game's own per-model
native light updates. Generated archives need an in-game performance comparison.
