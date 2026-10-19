# Light-only WotLK model patches

Build an optional MPQ from the player's own WoW 3.3.5a files:

```sh
python renderer/build_emitter_patch.py --client /path/to/WoW --output out/emitter-patch
```

The command writes `patch-y.mpq`, a hash-pinned catalog and an audit report. It does
not install files or launch the client. Inspect the report before installing the
MPQ, and regenerate Northlight's light cache after changing model assets. Keep a
backup of any existing patch with the same name.

The generator accepts MD20 version 264, preserves existing native lights and
keeps original geometry, material and animation payloads byte-for-byte. New lights
use an appended static identity bone; the client treats a negative light-bone
index as 65535, so it is not a valid sentinel. Separate luminous regions can use
up to 16 reviewed lights. Unknown revisions fail their hash pin. Unlit/broken
variants and unsupported animated sources are skipped rather than guessed.

The optional catalog contains 182 locally reviewed model patches, including 35
additional models with 69 sources. Nine additions have multiple separated light
regions. Ordinary native sources still take precedence. Generated models retain
the authored source IDs so parent-WMO duplicate suppression continues to work.

The audit covered 11,261 world M2 files, 1,985 root WMOs and 106 available maps.
This does not imply complete instance support: Northlight also needs compatible
scene geometry for an instance. The current builder must be given the relevant
maps; native model lights and Northlight's cached lighting are different paths.

This generator is original MIT-licensed Northlight code. ComfyAtmosphere was a
conceptual reference; no GPL source or Classic assets were copied. Game model
payloads are not covered by the software's MIT license and are deliberately not
included in the repository. Generate and retain those files locally.
