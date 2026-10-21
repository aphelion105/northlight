# northlight-test: requires=windows-gpu,zig,client
"""Production early/late water preserves opaque foreground and replaces native tile shading; floor blobs remain refraction."""
from pathlib import Path
import json,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
folder=fp.out()/'water-foreground';folder.mkdir(parents=True,exist_ok=True);exe=folder/'foreground.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(fp.TESTS/'water_foreground_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
mode='regression';report={'game_launched':False,'runs':[]}
for name,dll in [('native',Path(r'C:\Windows\SysWOW64\d3d9.dll')),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
 r=subprocess.run([str(exe),str(dll)],capture_output=True,text=True,timeout=60,cwd=folder);(folder/(mode+'-'+name+'.txt')).write_text(r.stdout+r.stderr,encoding='utf-8');report['runs'].append({'backend':name,'returncode':r.returncode,'output':r.stdout});(folder/(mode+'.json')).write_text(json.dumps(report,indent=2)+'\n');print(name,mode,r.returncode,r.stdout,flush=True);assert r.returncode==0,r.stdout+r.stderr
