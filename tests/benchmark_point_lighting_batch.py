# northlight-test: requires=windows-gpu,zig manual timing
"""Actual point-correction submission: standalone versus same-input batch."""
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
p.add_argument('--benchmark',action='store_true')
p.add_argument('--allow-game-process',action='store_true')
a=p.parse_args()
if not a.allow_game_process:processes()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);identity=source_identity()
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs=[Path(__file__),fp.TESTS/'point_lighting_batch_gpu.cpp',*a.backend]
hashes={str(path):digest(path) for path in inputs}
exe=out/'point-batch.exe'
units=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-return-type','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),fp.TESTS/'point_lighting_batch_gpu.cpp',*(fp.src(n) for n in units),'-luser32','-luuid','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,command)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
data=bytearray(exe.read_bytes());at=struct.unpack_from('<I',data,0x3c)[0]+22;struct.pack_into('<H',data,at,struct.unpack_from('<H',data,at)[0]|0x20);exe.write_bytes(data)
report={'pass':False,'source_identity':identity,'inputs':hashes,'exe_sha256':digest(exe),'runs':[],'timing':[],'total_northlight_cost_measured':False}
try:
    for index,backend in enumerate(a.backend):
        folder=out/('correctness-'+str(index));folder.mkdir()
        run=subprocess.run([str(exe),str(backend.resolve())],cwd=folder,env={**os.environ,'DXVK_LOG_PATH':str(folder),'DISABLE_VULKAN_OBS_CAPTURE':'1'},capture_output=True,text=True,timeout=240)
        (folder/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8');run.check_returncode();assert 'POINT_BATCH_PASS' in run.stdout
        report['runs'].append({'backend':str(backend.resolve()),'result':run.stdout.splitlines()[-1]});print(report['runs'][-1],flush=True)
    if a.benchmark:
        folder=out/'timing';folder.mkdir()
        run=subprocess.run([str(exe),str(a.backend[-1].resolve()),'timing'],cwd=folder,env={**os.environ,'DXVK_LOG_PATH':str(folder),'DISABLE_VULKAN_OBS_CAPTURE':'1'},capture_output=True,text=True,timeout=300)
        (folder/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8');run.check_returncode();assert 'POINT_BATCH_PASS' in run.stdout
        rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('POINT_BATCH_TIMING ') and 'warm=0 ' in line]
        for soft in (0,1):
            for count in (1,4,16,64):
                entry={'soft':soft,'count':count}
                for shared in (0,1):
                    values=[r for r in rows if int(r['soft'])==soft and int(r['count'])==count and int(r['shared'])==shared];assert len(values)==6
                    entry['shared' if shared else 'standalone']={key:statistics.median(float(r[key]) for r in values) for key in ('cpuMs','gpuMs','samplers')}
                report['timing'].append(entry);print(entry,flush=True)
    assert identity==source_identity() and all(digest(Path(path))==sha for path,sha in hashes.items())
    report['pass']=True
finally:(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
