# Automatic material relief

The renderer infers bounded normal, height and cavity detail from existing diffuse texture mips. Original MPQs remain read only. The static-surface path supports signature-checked terrain, WMO walls/floors and DetailDoodad grass; unsupported shaders retain native rendering.

AutoBump enables the system. AutoBumpTerrain and AutoBumpWalls select Off (0), Normal maps (1) or Normal maps plus POM (2). Grass uses Off or Normal maps. Strength, distance, POM depth/steps, self-shadow strength/steps and cavity strength are documented in the quality INI. Restart after editing the standalone branch's INI; the separate menu integration supplies live controls.

Generated maps use a bounded cache and limited per-frame generation. POM patches a proven native diffuse shader and shares its displaced UV with the generated material detail. Full-resolution directional detail is applied after temporal lighting. Original alpha, masks, fog and draw bindings are preserved. Automatic height inference cannot identify authored material intent, so painted highlights or vegetation colors may be mistaken for geometry.

The cache budgets compressed payloads by their native format, keeps a small working set warm and avoids repeated sampler changes. The optional tests/validate_auto_material_performance.py compares paired GPU output and API counts against this branch's committed pre-optimization material baseline. These fixture counts are not an in-game frame-rate measurement.

Run scripts/bump_recon.py to prepare native shader fixtures from your own client, then tests/validate_auto_bump.py with a plain D3D9/DXVK DLL path. Fixtures, textures, previews and logs stay in ignored output. The regression's two-layer WMO vertex program is the actual MapObjSpecular variant 30, rather than a renamed sibling-folder alias. Shader identities are hashes; no original game shader or texture is shipped.

Real geometry displacement, animated M2 support and signed-height improvements are separate linked extensions. These effects do not change collision geometry or the world-cache meshes used for static shadows and GI.

MSAA capture uses a private single-sample depth surface and restores the original target/depth bindings. Transient allocation failures are retried. Generated normal maps and retained source references use separate bounded budgets, with a 32 MiB map budget under world-loading pressure and hysteretic recovery. Material modes remain enabled during cache trimming. Native normal-map generation preserves the programmable vertex constant bank.

Generated maps use an adaptive 32..96 MiB/256-entry budget, with a separate 128 MiB limit on retained source textures. Pressure shrinks the working set without disabling relief. Normal generation preserves the full programmable vertex-constant bank; MSAA capture uses a private single-sample depth surface and restores the original targets. Transient allocation failures are retried. Saturated shadow rays stop at their exact maximum, and immediate repeated normal lookups use a bounds-checked recent entry.
