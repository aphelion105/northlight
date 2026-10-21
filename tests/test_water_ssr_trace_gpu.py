# northlight-test: requires=windows-gpu,zig,client
"""Check analytic near/far receivers, stepped crowns and empty sky on the production SSR bytecode."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--baseline',type=Path)
parser.add_argument('--output',type=Path,default=fp.out()/'ssr-trace')
args=parser.parse_args();folder=args.output.resolve();folder.mkdir(parents=True,exist_ok=True)
exe=folder/'trace.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(fp.TESTS/'water_ssr_trace_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
shader=fp.COMPILED/'WaterSSR.bin'
report={'shader_sha256':hashlib.sha256(shader.read_bytes()).hexdigest(),'cases':252,'runs':[],'game_launched':False}
for name,backend in [('native',Path(r'C:\Windows\SysWOW64\d3d9.dll')),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
    modes=[('current',shader,[]),('hierarchy',shader,['64','0','hierarchy'])]
    if args.baseline:modes.insert(0,('baseline',args.baseline.resolve(),['32','128']))
    for mode,path,extra in modes:
        result=subprocess.run([str(exe),str(backend),str(path),*extra],cwd=folder,capture_output=True,text=True,timeout=60)
        (folder/(name+'-'+mode+'.txt')).write_text(result.stdout+result.stderr,encoding='utf-8')
        report['runs'].append({'backend':name,'mode':mode,'returncode':result.returncode,'output':result.stdout.strip()})
        (folder/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(name,mode,result.returncode,(result.stdout.strip().splitlines() or [result.stderr.strip()])[-1],flush=True)
        assert result.returncode==(1 if mode=='baseline' else 0),result.stdout+result.stderr
print('252 SSR trace cases passed; 20/47/90/350/750 yards, stepped crown, sky rejection, both projection signs, three wave phases')
