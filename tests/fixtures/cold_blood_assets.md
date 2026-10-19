# Cold Blood read-only fixture

`cold_blood_assets.json` records the active client and stock archive provenance for
Spell14177 → SpellVisual4371 → castKit3770/effect126 and stateKit3771/effect121.
Both kits use the right-hand attachment22. Their effect models have 4 particles,
10 local bones and 20 mesh vertices each. The eight textures have native BC mip
chains including levels no larger than64×64. Colours come from their real texture
and particle lifetime data.

Build the optional offline decoder with the configured Zig toolchain:

```powershell
zig c++ -std=c++17 -O2 -Isrc/replay tests/cold_blood_texture_probe.cpp -o out/cold_blood_texture_probe.exe
python scripts/build_spell_asset_manifest.py --output out/cold-blood-manifest.json --local-fixtures out/cold-blood-fixtures --native-decoder out/cold_blood_texture_probe.exe
python tests/test_spell_asset_manifest.py
python tests/test_spell_asset_manifest_client.py
```

Set `NORTHLIGHT_CLIENT`, `NORTHLIGHT_STORMLIB` and `NORTHLIGHT_ZIG` for the local
machine. The client test compiles and executes the native decoder at O0 and O2.
Extracted mip/vertex bytes and decoded RGBA stay in ignored `out/`; Git contains
only the minimal frozen DBC graph, model/texture hashes, decoder reference, code
and tests. The full local report includes bone and particle tracks. Per-mip
`rgba_sha256` uses the BLP decoder;
`runtime_reference` uses the exact ActorTexture decoder on the most detailed mip
no larger than64. Their RGB565 rounding can differ.

The local mesh fixture is original M2 input, not a runtime particle draw capture.
Its model-local bones are not character hand bones. Particle blend4 means
SRCALPHA/ONE; the raw mesh material blend enum is a separate namespace.

Constant model rate/enable tracks do not encode external aura retirement. These
models have no decay sequence159. Current-frame accepted particle submissions
must govern runtime lights, including particles still drawn during their tail.
The active medium model already has two native blue lights from an earlier local
patch; stock has none. That fact is recorded for runtime deduplication. The tool
writes no client, DBC, M2, BLP or MPQ files.
