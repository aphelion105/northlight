# northlight-test: requires=windows-gpu,zig,client
"""Prove that unknown first liquid variants neither contaminate the snapshot nor split the surface."""
from pathlib import Path
import json,os,subprocess,sys
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler,rows
out=fp.out()/'material-gpu';out.mkdir(parents=True,exist_ok=True)
compiler=Compiler(allow_compatible_binary=True);header=['#include <windows.h>\n']
for entry,target in [('NativeWater','vs_3_0'),('OtherWater','vs_3_0'),('NativeColor','ps_3_0')]:
    data,words=compiler.compile(fp.TESTS/'water_material_fixture.hlsl',entry,target,out/(entry+'.bin'))
    header.append('static const DWORD k'+entry+'[]={\n'+'\n'.join(rows(words))+'\n};\n')
(out/'material_fixture_compiled.h').write_text(''.join(header));exe=out/'material.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'water_material_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
runs=[]
for name,backend in [('native',Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
    for mode in ['baseline','fixed']:
        p=subprocess.run([str(exe),str(backend),*(['--baseline'] if mode=='baseline' else [])],cwd=out,capture_output=True,text=True,timeout=60);(out/(name+'-'+mode+'.txt')).write_text(p.stdout+p.stderr);print(name,mode,p.returncode,p.stdout.strip(),flush=True);runs.append({'backend':name,'mode':mode,'returncode':p.returncode,'output':p.stdout.strip()});assert p.returncode==(1 if mode=='baseline' else 0),p.stdout+p.stderr
(out/'validation.json').write_text(json.dumps(runs,indent=2))
