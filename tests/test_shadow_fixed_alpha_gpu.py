#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client manual
"""Stock native alpha coverage versus exact production shadow MRT and frozen old shaders."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets
from extract_signatures import variants
import windows_gpu_guard

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend',type=Path,action='append',required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--allow-running-game',action='store_true',help='Explicit correctness-only run; records the concurrent game and makes no timing claim')
args=parser.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources=[Path(__file__),ROOT/'tests/shadow_fixed_alpha_gpu.cpp',ROOT/'tests/fixtures/shadow_fixed_alpha_before.h']
sources += [fp.src(name) for name in ['shadow_alpha.h','shadow_alpha_draw.h','shadow_alpha_target.h','world_compiled_shaders.h','local_light_compiled_shaders.h','patch_shadow_shader.h']]
inputs={str(p.relative_to(ROOT)):sha(p) for p in sources}
assets=Assets(archives='stock');programs=[]
try:
    for version in ['ps_2_0','ps_3_0']:
        name='shaders\\pixel\\'+version+'\\combiners_mod.bls'
        data=next(variants(assets.read(name)))[2]
        path=out/(version+'.bin');path.write_bytes(data);programs.append(path)
finally:assets.close()
env={**fp.zig_env(),'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'}

def run(tag,command):
    with (out/(tag+'.log')).open('w') as log:
        result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300)
    text=(out/(tag+'.log')).read_text(errors='replace')
    if result.returncode:print(text[-4000:],flush=True)
    result.check_returncode();return text

exe=out/'shadow-fixed-alpha.exe'
run('compile',[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),ROOT/'tests/shadow_fixed_alpha_gpu.cpp','-luser32','-o',exe])
records=[]
for index,backend in enumerate(args.backend):
    backend=backend.resolve()
    for program in programs:
        for format_ in [21,22,113,116]:
            processes=windows_gpu_guard.processes(allow_game=args.allow_running_game)
            tag=f'gpu-{index}-{program.stem}-{format_}'
            text=run(tag,[exe,backend,program,format_])
            line=next(line for line in text.splitlines() if line.startswith('SHADOW_FIXED_ALPHA_PASS'))
            assert 'currentMismatchPixels=0 ' in line
            print(backend.name,program.stem,format_,line,flush=True)
            records.append({'backend':str(backend),'backend_sha256':sha(backend),'program':program.name,'native_format':format_,'log':tag+'.log','result':line,'concurrent_game':[p for p in processes if p['name'].lower().startswith('wow')]})
assert all(sha(ROOT/path)==digest for path,digest in inputs.items()),'Oracle sources changed during validation'
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'native_programs':{p.name:sha(p) for p in programs},'cases':records,'allow_running_game':args.allow_running_game,'scope':'Exact independent native-vs-shadow binary coverage and R32 depth pixels, stock PS2/PS3 Combiners_Mod, source ARGB8/XRGB8/FP16/FP32 to R32, COLOR0 opacity, >/>= threshold boundaries, frozen-old negative control, full/partial-scissor enabled coverage and in-draw rect preservation, RT0/RT1/viewport/mask/alpha restoration including setup/draw failure, all-eight bounded 256/1024 scratch slots and interleaved format reuse/reset. No timings or game FPS claim.'},indent=2)+'\n')
