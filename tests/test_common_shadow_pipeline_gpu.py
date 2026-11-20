#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client timing
"""Actual WorldRenderer: world + detailed actors versus a shared fine cascade.

The candidate lives in a generated test overlay. It is not shipped in the DLL.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'scripts/shaders'),str(ROOT/'tests')]
import northlight_paths as fp
from scripts.shaders.shader_sources import copy_sources, source_files
from shader_compiler import Compiler, rows, validate_assembly
from validate_native_scene_trace import processes


def overlay(sources):
    def replace(old,new):
        count=sum(source.count(old) for source in sources.values())
        assert count==1,('Production hook changed',old,count)
        name=next(name for name,source in sources.items() if old in source)
        sources[name]=sources[name].replace(old,new)
    replace('#pragma once','#pragma once\n#include <cassert>\n#include "common_experiment_compiled.h"')
    replace('#include "world_soft_shadows.inl"','#include "world_soft_shadows.inl"\n#include "common_shadow_renderer.inl"')
    replace('        const bool directionalShadows=directionalShadowWork();',
            '        commonExperiment.stamp(0);\n        const bool directionalShadows=directionalShadowWork();')
    replace('        if(lightingCapture){const unsigned productSource=',
            '        commonExperiment.stamp(1);\n        if(lightingCapture){const unsigned productSource=')
    replace('        bool actorPass[2]={},anyActorPass=false;',
            '        commonExperiment.stamp(2);\n        bool actorPass[2]={},anyActorPass=false;')
    replace('        if(anyActorPass){\n            if(!actorMaps.buildTiles(',
            '        if(commonExperiment.enabled&&anyActorPass){if(!commonPrepare(actorPass))return false;}\n        else if(anyActorPass){\n            if(!actorMaps.buildTiles(')
    replace('                float actorRows[16];NorthlightWorldMath::replayProjection(context.inverseView,am,actorRows);',
            '                if(commonExperiment.enabled&&!commonWorldCasters(am,unsigned(source),result.replayConstantBytes,result.replayConstantCalls))return false;\n                float actorRows[16];NorthlightWorldMath::replayProjection(context.inverseView,am,actorRows);')
    replace('                d->SetVertexShaderConstantF(0,am,4);d->SetPixelShader(replayPS);',
            '                d->SetVertexShaderConstantF(0,am,4);d->SetPixelShader(commonExperiment.enabled?commonExperiment.replay:replayPS);')
    replace('                if(!check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear actor shadow"))return false;',
            '                if(!commonExperiment.enabled&&!check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear actor shadow"))return false;')
    replace('d->SetPixelShader(sharedResolveRequested?sharedDirectionalPS:directionalSoftPS)',
            'd->SetPixelShader(sharedResolveRequested?(commonExperiment.enabled?commonExperiment.filter:sharedDirectionalPS):directionalSoftPS)')
    replace('                    if(!softResolved)break;\n                }\n                const bool fading=',
            '''                    if(!softResolved)break;
                    if(commonExperiment.enabled){
                        const float commonInfo[]={info[0],info[1],info[2],4.25f*96.f/1024};
                        const auto direction=rawSourceDirections[source];const float raw[]={direction.x,direction.y,direction.z,0};
                        d->SetPixelShaderConstantF(72,commonInfo,1);d->SetPixelShaderConstantF(74,raw,1);
                        d->SetTexture(10,commonExperiment.tiles[source]);d->SetTexture(13,commonExperiment.blocks[source]);
                    }
                }
                const bool fading=''')
    replace('        const bool sharedResolved=sharedResolveRequested&&softResolved;',
            '''        if(commonExperiment.enabled){
            // This experiment has no transition/fog route yet. Refuse unsupported
            // frames; do not hide an incorrect fallback in the performance sample.
            if(anyShadowTransition||!sharedResolveRequested||!softResolved||effects.fog)return false;
            d->SetTexture(13,textures[13]);
        }
        const bool sharedResolved=sharedResolveRequested&&softResolved;''')
    replace('        if(profileSampled())logf("WORLD_SHADOW_RESOLVE',
            '        commonExperiment.stamp(3);\n        if(profileSampled())logf("WORLD_SHADOW_RESOLVE')
    return sources


def main():
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('--output',type=Path,default=fp.output_dir()/'common-shadow-pipeline')
    p.add_argument('--backend',type=Path,action='append')
    a=p.parse_args();running=processes()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    inputs=[fp.src('world_renderer.h'),fp.TESTS/'near_shadow_interval_gpu.cpp',
            fp.TESTS/'fixtures/common_shadow_cascade.hlsl',fp.TESTS/'fixtures/common_shadow_renderer.inl',
            fp.TESTS/'fixtures/common_shadow_benchmark.inl',fp.TESTS/'common_shadow_pipeline_gpu.cpp',Path(__file__)]
    inputs+=[f for f in (fp.REPO/'src').rglob('*') if f.suffix in ('.h','.inl','.cpp')]
    inputs.extend(source_files(fp.SHADERS/'world_effects.hlsl'))
    hashes=lambda:{str(f.resolve()):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}
    before=hashes();report={'scope':'Actual WorldRenderer with authored static geometry and animated replay packets. CPU map/selection and GPU maps+filters totals, plus full renderer interval. Not gameplay FPS. Candidate has no direction-history/fog integration.',
                          'inputs':before,'uncontended_timing_valid':not any(x['name'].lower() in ('wow.exe','wow64.exe') for x in running),'results':[]}
    copy_sources(fp.SHADERS/'world_effects.hlsl', out)
    source=out/'common.hlsl';shutil.copyfile(fp.TESTS/'fixtures/common_shadow_cascade.hlsl',source)
    compiler=Compiler(allow_compatible_binary=True);header=['#pragma once','#include <windows.h>'];report['shaders']={}
    for name in ('CommonDirectional','CommonReplayPS','CommonBlocks','CommonTiles','CommonSeedDepth'):
        output=out/(name+'.bin');data,words=compiler.compile(source,name,'ps_3_0',output)
        report['shaders'][name]=validate_assembly(out/(name+'.bin.asm'),'ps_3_0',slot_limit=1152 if name=='CommonDirectional' else 256)
        header.append(f'static const DWORD k{name}Shader[]={{\n'+'\n'.join(rows(words))+'\n};')
    (out/'common_experiment_compiled.h').write_text('\n'.join(header)+'\n')
    overlay_units=('world_renderer.h','world_directional_pass.inl','world_soft_shadows.inl')
    for name, text in overlay({name:fp.src(name).read_text() for name in overlay_units}).items():
        (out/name).write_text(text)
    for name in ('common_shadow_renderer.inl','common_shadow_benchmark.inl'):
        shutil.copyfile(fp.TESTS/'fixtures'/name,out/name)
    fixture=(fp.TESTS/'near_shadow_interval_gpu.cpp').read_text().replace('#include "../src/proxy/renderer.cpp"','#include "renderer.cpp"')
    assert fixture.count('struct WorldScreenEffectsTest {')==1
    fixture=fixture.replace('struct WorldScreenEffectsTest {','struct WorldScreenEffectsTest {\n#include "common_shadow_benchmark.inl"')
    (out/'near_fixture.cpp').write_text(fixture)
    sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
    env=fp.zig_env();env.update(DISABLE_VULKAN_OBS_CAPTURE='1')
    exe=out/'common-pipeline.exe'
    cmd=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"','-I',str(out),*map(str,fp.test_include_flags()),str(fp.TESTS/'common_shadow_pipeline_gpu.cpp'),*(str(fp.src(s)) for s in sources),'-luser32','-luuid','-o',str(exe)]
    with (out/'compile.log').open('w') as log:
        subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
    for i,backend in enumerate(a.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']):
        run=out/str(i);run.mkdir();(run/'dxvk.conf').write_text('d3d9.presentInterval = 0\ndxvk.enableStateCache = False\n')
        env.update(DXVK_CONFIG_FILE=str(run/'dxvk.conf'),DXVK_LOG_PATH=str(run))
        result=subprocess.run([str(exe),str(backend.resolve()),str(run)],cwd=run,env=env,capture_output=True,text=True,timeout=600,creationflags=subprocess.CREATE_NO_WINDOW)
        text=result.stdout+result.stderr;(run/'gpu.log').write_text(text);print(text[-1600:],flush=True)
        assert result.returncode==0 and 'COMMON_PIPELINE_PASS' in text and 'COMMON_MOTION_COVERAGE cases=12' in text,result.returncode
        records=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('PIPELINE ') and 'warm=0 ' in line]
        assert len(records)==48
        summary=[]
        for scenario in range(4):
            for common in (0,1):
                selected=[r for r in records if int(r['scenario'])==scenario and int(r['common'])==common]
                assert len(selected)==6
                fields=('shadowGpuMs','shadowCpuMs','mapsGpuMs','filterGpuMs','fullGpuMs','fullCpuMs','mapDraws','filterDraws','commonStaticRenders','commonStaticReuses')
                summary.append({'scenario':scenario,'common':common,**{key:statistics.median(float(r[key]) for r in selected) for key in fields}})
        report['results'].append({'backend':str(backend),'log':str(run/'gpu.log'),'observed_medians':summary,'samples':records})
    assert hashes()==before,'Source changed during measurement'
    report['pass']=True;report['runtime_acceptance']=False
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    main()
