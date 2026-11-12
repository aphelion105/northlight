# northlight-test: requires=windows-gpu,zig manual timing
"""Compare production water decode with a frozen checkout on one device."""
from pathlib import Path
import argparse,hashlib,json,os,re,statistics,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'scripts'),str(ROOT/'tests')]
import northlight_paths as fp
from build_renderer import source_identity
from windows_gpu_guard import processes
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--backend',type=Path,action='append',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--benchmark',action='store_true')
p.add_argument('--allow-game-process',action='store_true')
a=p.parse_args()
if not a.allow_game_process:processes()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
identity=source_identity();digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
for name,root in [('baseline',a.baseline.resolve()),('candidate',ROOT)]:
    text=(root/'src/water/water_renderer.h').read_text(encoding='utf-8')
    if name=='baseline':text=text.replace('NorthlightWaterRenderer','BaselineWaterRenderer')
    marker='class '+('BaselineWaterRenderer' if name=='baseline' else 'NorthlightWaterRenderer')+' {'
    assert text.count(marker)==1
    (out/(name+'_water.h')).write_text(text.replace(marker,marker+'\n    friend struct WaterResolveTestAccess;'),encoding='utf-8',newline='\n')
exe=out/'water-resolve.exe'
cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-return-type','-I',out,*fp.test_include_flags(),fp.TESTS/'water_resolve_gpu.cpp','-luser32','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
data=bytearray(exe.read_bytes());at=struct.unpack_from('<I',data,0x3c)[0]+22;struct.pack_into('<H',data,at,struct.unpack_from('<H',data,at)[0]|0x20);exe.write_bytes(data)
report={'pass':False,'source_identity':identity,'baseline_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.baseline,text=True).strip(),'shared_dependencies':'Both class snapshots use current SavedState and water_shader_patch.h, including the native-compatible resolve shader. This isolates clear placement.','inputs':{str(path):digest(path) for path in [Path(__file__),fp.TESTS/'water_resolve_gpu.cpp',out/'baseline_water.h',out/'candidate_water.h',*a.backend]},'exe_sha256':digest(exe),'runs':[],'timing':[],'total_northlight_cost_measured':False}
try:
    for index,backend in enumerate(a.backend):
        folder=out/('correctness-'+str(index));folder.mkdir()
        command=[str(exe),str(backend.resolve()),str(ROOT/'shaders/compiled/HdrCopy.bin')]
        run=subprocess.run(command,cwd=folder,env={**os.environ,'DXVK_LOG_PATH':str(folder),'DISABLE_VULKAN_OBS_CAPTURE':'1'},capture_output=True,text=True,timeout=240)
        (folder/'run.log').write_text(run.stdout+run.stderr);run.check_returncode();assert 'WATER_RESOLVE_PASS' in run.stdout
        report['runs'].append({'backend':str(backend.resolve()),'result':run.stdout.splitlines()[-1]});print(report['runs'][-1],flush=True)
    if a.benchmark:
        folder=out/'timing';folder.mkdir()
        command=[str(exe),str(a.backend[-1].resolve()),str(ROOT/'shaders/compiled/HdrCopy.bin'),'timing']
        run=subprocess.run(command,cwd=folder,env={**os.environ,'DXVK_LOG_PATH':str(folder),'DISABLE_VULKAN_OBS_CAPTURE':'1'},capture_output=True,text=True,timeout=300)
        (folder/'run.log').write_text(run.stdout+run.stderr);run.check_returncode();assert 'WATER_RESOLVE_PASS' in run.stdout
        rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('WATER_RESOLVE_TIMING ') and 'warm=0 ' in line]
        for height in (1080,2160):
            for pattern in (0,2,1):
                for fallback in (0,1):
                    entry={'height':height,'pattern':pattern,'fallback':fallback}
                    for mode in (0,1):
                        values=[r for r in rows if int(r['height'])==height and int(r['pattern'])==pattern and int(r['fallback'])==fallback and int(r['mode'])==mode];assert len(values)==6
                        entry['candidate' if mode else 'baseline']={key:statistics.median(float(r[key]) for r in values) for key in ('cpuMs','gpuMs','fills','clears','targets')}
                    report['timing'].append(entry);print(entry,flush=True)
    assert identity==source_identity()
    report['pass']=True
finally:(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',newline='\n')
