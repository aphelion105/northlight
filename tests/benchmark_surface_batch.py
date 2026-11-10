# northlight-test: requires=windows-gpu,zig manual timing
"""Compare standalone and caller-protected surface passes on one native device."""
from pathlib import Path
import argparse,hashlib,json,os,re,statistics,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'scripts'),str(ROOT/'tests')]
import northlight_paths as fp
from build_renderer import source_identity
from windows_gpu_guard import processes
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--backend',type=Path,action='append',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--benchmark',action='store_true',help='Time the final backend after all correctness runs.')
p.add_argument('--allow-game-process',action='store_true')
a=p.parse_args()
if not a.allow_game_process:processes()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
identity=source_identity();digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs=[Path(__file__),fp.TESTS/'surface_batch_gpu.cpp',fp.TESTS/'surface_effects_gpu.cpp',fp.TESTS/'composite_distance_gpu.cpp']
inputs+=[b.resolve() for b in a.backend]
hashes={str(path):digest(path) for path in inputs}
world=fp.src('world_renderer.h').read_text();start=world.index('struct NameMaskCompositeBind {');end=world.index('\n};',start)+3
(out/'lens_name_mask.h').write_text('#include "name_stencil.h"\n'+world[start:end]+'\n',newline='\n')
exe=out/'surface-batch.exe'
cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-return-type','-I',out,*fp.test_include_flags(),fp.TESTS/'surface_batch_gpu.cpp','-luser32','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
data=bytearray(exe.read_bytes());at=struct.unpack_from('<I',data,0x3c)[0]+22;struct.pack_into('<H',data,at,struct.unpack_from('<H',data,at)[0]|0x20);exe.write_bytes(data)
report={'pass':False,'source_identity':identity,'inputs':hashes,'exe_sha256':digest(exe),'runs':[],'timing':[],'game_fps_measured':False,'total_northlight_cost_measured':False}
try:
    for index,backend in enumerate(a.backend):
        folder=out/('correctness-'+str(index));folder.mkdir()
        run=subprocess.run([str(exe),str(backend.resolve())],cwd=folder,env={**os.environ,'DXVK_LOG_PATH':str(folder),'DISABLE_VULKAN_OBS_CAPTURE':'1'},capture_output=True,text=True,timeout=180)
        (folder/'run.log').write_text(run.stdout+run.stderr);run.check_returncode();assert 'SURFACE_BATCH_PASS' in run.stdout
        report['runs'].append({'backend':str(backend.resolve()),'result':run.stdout.splitlines()[-1]});print(report['runs'][-1],flush=True)
    if a.benchmark:
        folder=out/'timing';folder.mkdir()
        run=subprocess.run([str(exe),str(a.backend[-1].resolve()),'timing'],cwd=folder,env={**os.environ,'DXVK_LOG_PATH':str(folder),'DISABLE_VULKAN_OBS_CAPTURE':'1'},capture_output=True,text=True,timeout=180)
        (folder/'run.log').write_text(run.stdout+run.stderr);run.check_returncode();assert 'SURFACE_BATCH_PASS' in run.stdout
        rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('SURFACE_BATCH_TIMING ') and 'warm=0 ' in line]
        for height in (1080,2160):
            for weather in range(4):
                entry={'height':height,'weather':weather}
                for mode in (0,1):
                    values=[r for r in rows if int(r['height'])==height and int(r['weather'])==weather and int(r['mode'])==mode];assert len(values)==6
                    entry['shared' if mode else 'standalone']={key:statistics.median(float(r[key]) for r in values) for key in ('cpuMs','gpuMs','captures','applies','draws','copies')}
                assert entry['shared']['captures']==entry['shared']['applies']==1
                assert all(entry['shared'][key]==entry['standalone'][key] for key in ('draws','copies'))
                report['timing'].append(entry)
        report['timing_backend']=str(a.backend[-1].resolve());report['order']='ABBA';report['warmup_blocks']=4
    assert source_identity()==identity and all(digest(Path(path))==sha for path,sha in hashes.items())
    report['pass']=True
finally:(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report['timing'],indent=2))
