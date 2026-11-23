#!/usr/bin/env python3
# northlight-test: requires=zig
"""Screen-coherent lamp batches, the LocalDirect16 merge and the selection's off-screen preference: native tests (plain and
ASan/UBSan), the cost model pinned to the shader manifest, and the renderer wiring."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from scripts.shaders.shader_sources import world_source_text
import json,re,subprocess,tempfile
from source_units import source_text
HERE=Path(__file__).resolve().parent
windows=sys.platform=='win32'
compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if windows else ['clang++']
variants=[('O2',['-O2']),('O0',['-O0'])] if windows else [
    ('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]
with tempfile.TemporaryDirectory(prefix='batches-',dir=fp.output_dir()) as tmp:
    for label,flags in variants:
        exe=Path(tmp)/('test-'+label+('.exe' if windows else ''))
        subprocess.run([*compiler,'-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_local_light_batches.cpp'),'-o',str(exe)],env=fp.zig_env() if windows else None,check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
# The cost model's slot counts are the compiled shaders'.
manifest=json.loads((HERE.parent/'shaders'/'world-shader-build.json').read_text())
def slots(entry):
    def find(o):
        if isinstance(o,dict):
            if isinstance(o.get(entry),dict) and 'static_instruction_slots' in o[entry]:return o[entry]['static_instruction_slots']
            for v in o.values():
                r=find(v)
                if r is not None:return r
    return find(manifest)
header=fp.src('local_light_batches.h').read_text()
model=dict(re.findall(r'(\w+Slots)=(\d+)',header))
assert int(model['DirectSlots'])==slots('LocalDirect'),model
assert int(model['Direct16Slots'])==slots('LocalDirect16'),model
assert int(model['FogSlots'])==slots('LocalFog'),model
assert slots('LocalDirect16')<=512
# Renderer wiring.
cpu=source_text(fp.src('world_renderer.h'))
assert 'localScissorView=NorthlightLocalLightScissor::view(' in cpu
assert cpu.index('localScissorView=NorthlightLocalLightScissor::view(')<cpu.index('a.lights=active?localLightTracker.update(')
assert 'selectContinuous,debug==0?&localScissorView:nullptr,' in cpu  # debug views keep the plain order
assert cpu.index('NorthlightLocalLightBatches::arrange(localLights,localScissorView)')<cpu.index('NorthlightLocalLightScissor::pairedBatchRects<NorthlightLocalLightSelection::FogBatchSize>')
assert cpu.index('NorthlightLocalLightBatches::arrange(localLights,localScissorView)')<cpu.index('localLights.fog[i][0]=sigmaAt(')
local=fp.src('world_local_lighting.h').read_text()
loop=local[local.index('bool drawDirect('):local.index('bool drawFog(')]
assert 'NorthlightLocalLightBatches::wide(a,b)' in loop and 'localLights.directBatch<2*N>(firstLight)' in loop
assert 'd->SetPixelShaderConstantF(84,batch.position[0].data(),2*N);d->SetPixelShaderConstantF(100,batch.color[0].data(),2*N);' in loop
assert loop.count('profileSplit(d,profile,target);')==2
assert 'if(!profile||!profile->recording()||!resume||profileSplitFailed)return;' in local  # sample frames only
assert 'localLighting.reset();' in cpu
assert 'drop(profileSplitSurface);' in local and 'drop(localDirect16PS);' in local
shader=world_source_text(fp.src('world_effects.hlsl'))
lo16=shader[shader.index('float4 LocalDirect16('):shader.index('float3 removalScale(')]
assert 'uniform float4 LampPos[16]:register(c84),uniform float4 LampColor[16]:register(c100)' in lo16
assert 'return float4(result*(LocalLightInfo.y*(1-LocalLightInfo.z*sunlit)),0);' in lo16
print('wiring ok')
