#!/usr/bin/env python3
# northlight-test: requires=zig
"""Bounded native-light bridge: executable checks and actual renderer wiring."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='northlight-equipment-') as tmp:
    for flags in (['-O0'], ['-O2']):
        exe = Path(tmp)/('test.exe' if sys.platform == 'win32' else 'test')
        subprocess.run([str(fp.zig()), 'c++', '-std=c++17', *flags, '-Wall', '-Wextra', '-Werror',
                        *fp.test_include_flags(), str(fp.TESTS/'test_equipment_lights.cpp'), '-o', str(exe)],
                       check=True, env=fp.zig_env())
        subprocess.run([str(exe)], check=True)

renderer = fp.src('renderer.cpp').read_text()
world = fp.src('world_renderer.h').read_text()
assert 'world->registerEquipmentShader(*out,m2ShaderFamily(h)==3?words.data():nullptr,m2ShaderFamily(h)==3?words.size():0)' in renderer
assert 'if(world->equipmentLightsOn())world->captureEquipmentLights(vs,worldDepth);' in renderer
capture = world[world.index('    void captureEquipmentLights('):world.index('    unsigned spellLayerSetting()')]
assert capture.index('equipmentCapture.take(actor,') < capture.index('GetVertexShaderConstantF(17,bank,11)')
assert 'const bool actor=isSkinnedShader(shader)' in capture
assert 'equipmentLights.admit(bank,it->second,actor)' in capture
assert capture.index('ds.p!=mainDepth') < capture.index('GetVertexShaderConstantF(17,bank,11)')
assert capture.index('std::isfinite(v)') < capture.index('GetVertexShaderConstantF(17,bank,11)')
assert 'equipmentLights.output.data()' in world and 'equipmentLights.publish(GetTickCount(),' in world
assert 'GetRenderTargetData' not in capture and 'LockRect' not in capture
assert 'actorCandidates=%u' in world and 'domainRejected=%u' in world and 'selfYards=%.2f' in world
print('actual renderer: native Model identity gate, actor reservation, proven-slot bank dedup, main depth/projection gate, bounded constant reads, source diagnostics, existing local/fog batches, no texture/GPU readback PASS')
