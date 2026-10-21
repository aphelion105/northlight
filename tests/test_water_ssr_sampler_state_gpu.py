# northlight-test: requires=windows-gpu,zig,client
"""Production SSR must ignore native mip clamps/bias and restore them after its private pass."""
from pathlib import Path
import argparse,json,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--expect-failure',action='store_true');args=parser.parse_args();folder=fp.out()/'ssr-sampler-state';folder.mkdir(parents=True,exist_ok=True);exe=folder/'sampler-state.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(fp.TESTS/'water_ssr_sampler_state_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
mode='baseline' if args.expect_failure else 'fixed';report={'game_launched':False,'runs':[]}
for name,dll in [('native',Path(r'C:\Windows\SysWOW64\d3d9.dll')),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
 r=subprocess.run([str(exe),str(dll)],capture_output=True,text=True,timeout=60,cwd=folder);(folder/(mode+'-'+name+'.txt')).write_text(r.stdout+r.stderr,encoding='utf-8');report['runs'].append({'backend':name,'returncode':r.returncode,'output':r.stdout});(folder/(mode+'.json')).write_text(json.dumps(report,indent=2)+'\n');print(name,mode,r.returncode,r.stdout,flush=True);assert r.returncode==(1 if args.expect_failure else 0),r.stdout+r.stderr
