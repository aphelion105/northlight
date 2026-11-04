#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib
"""Production cube tests with real stock and active-mod lamps on D3D9 and DXVK."""
import argparse
from array import array
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from test_m2_lamp_self_shadow import fixtures
import client_fixtures

def readback_metrics(out,case):
    def pixels(path):
        data=path.read_bytes();magic,w,h,fmt,stride=struct.unpack('<5I',data[:20])
        assert magic==0x31524746 and fmt==116 and stride==16 and len(data)==20+w*h*16
        values=array('f');values.frombytes(data[20:]);return values
    direct=pixels(out/(case.name+'-direct.fgr'))
    energy=[direct[i]+direct[i+1]+direct[i+2] for i in range(0,len(direct),4)]
    lo,hi=float('inf'),-float('inf')
    for phase in range(3):
        corrected=pixels(out/(case.name+f'-receiver-{phase}.fgr'))
        rgb=[v for i,v in enumerate(corrected) if i%4!=3]
        lo=min(lo,min(rgb));hi=max(hi,max(rgb))
    assert max(energy)-min(energy)>.05 and lo>=-.001
    return {'direct_rgb_sum_min':min(energy),'direct_rgb_sum_max':max(energy),'direct_rgb_sum_range':max(energy)-min(energy),
            'corrected_rgb_min':lo,'corrected_rgb_max':hi,'stand_image':str(out/(case.name+'-stand-0.fgr'))}

def character_fixture(out):
    """Actual stock body geometry/weights; the GPU stand supplies controlled poses."""
    fp.use_source_modules()
    from world_scene_builder import Assets, array, unpack
    name='character\\human\\male\\humanmale.m2'
    assets=Assets(archives='stock')
    try:
        model=assets.read(name);skin=assets.read(name[:-3]+'00.skin')
        nv,ov=unpack('<II',model,60);n,o=unpack('<II',skin,4);lookup=[x[0] for x in array(skin,o,n,'<H')]
        n,o=unpack('<II',skin,12);indices=[x[0] for x in array(skin,o,n,'<H')]
        n,o=unpack('<II',skin,20);skin_bones=array(skin,o,n,'<4B')
        n,o=unpack('<II',model,120);bone_combos=[x[0] for x in array(model,o,n,'<H')]
        n,o=unpack('<II',skin,28);subsets=array(skin,o,n,'<10H7f')
        subset=max((s for s in subsets if s[8]>1),key=lambda s:s[5])
        first=subset[4]+(subset[1]<<16);used=indices[first:first+subset[5]];vertices=sorted(set(used));assert max(lookup[i] for i in vertices)<nv
        raw=[bytearray(model[ov+lookup[i]*48:ov+(lookup[i]+1)*48]) for i in vertices]
        # CM2Shared::SetVertices supplies SKIN palette indices to the shader.
        # Resolve through this section's boneComboIndex before compacting its
        # global bones. Keep distinct SKIN vertices even when M2 lookup repeats.
        for v,skin_index in zip(raw,vertices):
            for k in range(4):v[16+k]=bone_combos[subset[7]+skin_bones[skin_index][k]] if v[12+k] else 0
        bones=sorted({v[16+k] for v in raw for k in range(4) if v[12+k]});assert 1<len(bones)<=75
        palette={bone:i for i,bone in enumerate(bones)}
        for v in raw:
            assert sum(v[12:16])==255
            for k in range(4):v[16+k]=palette[v[16+k]] if v[12+k] else 0
        influence=[sum(v[12+k] for v in raw for k in range(4) if v[16+k]==bone) for bone in range(len(bones))]
        pose_bone=max(range(len(bones)),key=lambda b:influence[b])
        remap={v:i for i,v in enumerate(vertices)}
        data=struct.pack('<5I',0x31414c4e,len(raw),len(used),len(bones),pose_bone)+b''.join(raw)+struct.pack('<%dI'%len(used),*(remap[i] for i in used))
        (out/'character.bin').write_bytes(data)
        shutil.copyfile(client_fixtures.four_bone_vs3(),out/'character-vs.bin')
        evidence={'model':name,'model_sha256':hashlib.sha256(model).hexdigest(),'skin_sha256':hashlib.sha256(skin).hexdigest(),
            'archive':assets.origin(name),'vertices':len(raw),'triangles':len(used)//3,'palette_bones':bones,'controlled_pose_bone':pose_bone,
            'geometry':'largest multi-influence stock skin section; original positions, weights, normals and UVs; SKIN indices + section boneComboIndex + M2 bone-combo table resolve the compact palette',
            'animation':'controlled placement and one-bone translation, not a captured gameplay animation'}
        (out/'character.json').write_text(json.dumps(evidence,indent=2)+'\n')
        return evidence
    finally:assets.close()


def compile_gpu(out,source_root=None):
    source_root=Path(source_root or fp.REPO).resolve()
    exe=out/'lamp-gpu.exe'
    sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
    root=Path(__file__).resolve().parents[1]
    includes=[str(source_root/Path(flag).relative_to(root)) if flag!='-I' and Path(flag).is_relative_to(root) and Path(flag).relative_to(root).parts[0]=='src' else flag for flag in fp.test_include_flags()]
    source=out/'lamp-gpu-stand.cpp'
    source.write_text((fp.TESTS/'m2_lamp_self_shadow_gpu.cpp').read_text().replace('../src/proxy/renderer.cpp',(source_root/'src/proxy/renderer.cpp').as_posix()))
    cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*includes,source,*(source_root/fp.src(s).relative_to(root) for s in sources),'-luser32','-luuid','-o',exe]
    with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),check=True,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,timeout=300)
    (out/'compile-command.json').write_text(json.dumps({'source_root':str(source_root),'command':list(map(str,cmd)),
        'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()},indent=2)+'\n')
    return exe

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path)
    parser.add_argument('--compile-only',action='store_true')
    parser.add_argument('--backend',type=Path,action='append')
    parser.add_argument('--actor-case',type=Path,action='append',default=[],help='Explicit FGS/FGL lamp fixture to audit with a real skinned character and independent box')
    parser.add_argument('--actor-only',action='store_true',help='Run only explicitly selected actor cases; keep the ordinary lamp matrix separate')
    parser.add_argument('--actor-soft',action='store_true',help='Require the production soft-shadow resolve and consumer for actor cases')
    args=parser.parse_args()
    if (args.actor_only or args.actor_soft) and not args.actor_case:parser.error('--actor-only/--actor-soft requires --actor-case')
    out=fp.output_dir();cases=[] if args.actor_only else fixtures(out/'fixtures');character_fixture(out);exe=compile_gpu(out,args.source_root)
    if args.compile_only:return
    results=[]
    for i,backend in enumerate(args.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']):
        settings=out/f'settings-{i}';settings.mkdir(exist_ok=True)
        for case in cases:
            result=subprocess.run([str(exe),str(backend),str(settings)+os.sep,str(case),'fixture',str(out)],capture_output=True,text=True,env={**os.environ,'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'},timeout=180,cwd=out)
            (out/f'gpu-{i}-{case.name}.log').write_text(result.stdout+result.stderr)
            print(result.stdout+result.stderr,flush=True);assert result.returncode==0
            results.append({'backend':str(backend),'case':str(case),'mode':'fixture','exit_code':result.returncode})
        for case in args.actor_case:
            mode='actor-soft' if args.actor_soft else 'actor'
            actor=subprocess.run([str(exe),str(backend),str(settings)+os.sep,str(case.resolve()),mode,str(out)],capture_output=True,text=True,env={**os.environ,'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'},timeout=180,cwd=out)
            (out/f'actor-{i}-{case.name}.log').write_text(actor.stdout+actor.stderr)
            print(actor.stdout+actor.stderr,flush=True);assert actor.returncode==0
            results.append({'backend':str(backend),'backend_sha256':hashlib.sha256(backend.read_bytes()).hexdigest(),'case':str(case),'mode':mode,'exit_code':actor.returncode,
                'soft_consumer_verified':args.actor_soft,**readback_metrics(out,case)})
    (out/'validation.json').write_text(json.dumps({'pass':True,'game_launched':False,'command':sys.argv,'source_root':str(args.source_root or fp.REPO),'results':results},indent=2)+'\n')

if __name__=='__main__':main()
