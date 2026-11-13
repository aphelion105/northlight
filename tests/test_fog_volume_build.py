#!/usr/bin/env python3
# northlight-test:
"""Volume shaders, dependency freshness and the production atlas bytecode agree."""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from compile_fog_volume_shaders import ENTRIES, source_hashes
from shader_compiler import bytecode, validate_assembly

manifest = json.loads((fp.SHADERS / 'fog-volume-shader-build.json').read_text())
assert manifest['sources'] == source_hashes()
expected = set(ENTRIES) | {name + 'Transition' for name in ENTRIES[:4]}
assert set(manifest['shaders']) == expected
header = fp.src('fog_volume_compiled_shaders.h').read_text()
for name, metadata in manifest['shaders'].items():
    data, words = bytecode(fp.COMPILED / (name + '.bin'), 'ps_3_0')
    assert metadata['sha256'] == hashlib.sha256(data).hexdigest(), name
    embedded = re.search(r'k' + name + r'Shader\[\] = \{([^}]+)', header)[1]
    assert list(words) == [int(x, 16) for x in re.findall(r'0x[\da-fA-F]+', embedded)]
    stats = validate_assembly(fp.COMPILED / (name + '.bin.asm'), 'ps_3_0', slot_limit=768 if name.endswith('Transition') else 512)
    assert all(metadata[k] == value for k, value in stats.items()), name
    assert f'k{name}ShaderSlots = {stats["static_instruction_slots"]};' in header
assert 'fog-volume-shader-build.json' in (fp.SCRIPTS / 'build_renderer.py').read_text()
world = fp.src('world_renderer.h').read_text()
assert 'fogVolume.reset();fogVolumeField.reset();fogVolumeKey=0;' in world
assert 'for(auto* texture:fogVolume.texture)inventory.texture(texture,PostTargets);' in world
assert 'else if(effects.fog&&fogTemporalPS&&fogHistory&&debug==0&&!volumeFog)' in world
print(f'PASS {len(expected)} volume kernels: dependency hashes, bytecode, SM3 metrics, lifecycle and memory accounting')
