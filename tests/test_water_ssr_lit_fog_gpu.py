# northlight-test: requires=windows-gpu,zig,client
"""Deferred SSR reads final world colors and applies native plus volumetric transmission once."""
from pathlib import Path
import argparse,json,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
folder=fp.out()/'ssr-lit-fog';folder.mkdir(parents=True,exist_ok=True);exe=folder/'lit-fog.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(fp.TESTS/'water_ssr_lit_fog_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
report={'game_launched':False,'runs':[]}
for name,backend in [('native',Path(r'C:\Windows\SysWOW64\d3d9.dll')),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
    result=subprocess.run([str(exe),str(backend)],cwd=folder,capture_output=True,text=True,timeout=60)
    (folder/(name+'.txt')).write_text(result.stdout+result.stderr,encoding='utf-8');report['runs'].append({'backend':name,'returncode':result.returncode,'output':result.stdout});(folder/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(name,result.returncode,result.stdout,flush=True);assert result.returncode==0,result.stdout+result.stderr
