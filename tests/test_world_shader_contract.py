#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Coordinate admission shared by command recording and renderer registration."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir();exe=out/'world-shader-contract.exe'
for opt in ('-O0','-O2'):
    subprocess.run([str(fp.zig()),'c++','-std=c++17',opt,'-UNDEBUG','-Wall','-Wextra','-Werror',
                    *fp.test_include_flags(),str(fp.TESTS/'test_world_shader_contract.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True)
world=fp.src('world_renderer.h').read_text()
stream=fp.src('stream_device.h').read_text()
renderer=fp.src('renderer.cpp').read_text()
assert 'triggerTags(p->hash,code,tokens*4)' in stream
assert 'triggerTags(hash,words.data(),size)' in world
assert 'else if(tags&NorthlightShaderTags::kWmo)' in world
assert 'if(!(tags&NorthlightShaderTags::kWorld))return;' in world
assert 'm2ShaderFamily(h)==3||world->recognizesWmo(*out)||contains(kTerrainVS,h)' in renderer
print('recording + world context + geometry + independently proven native bank use shared admission PASS')
