# northlight-test: requires=windows-gpu,zig manual timing
"""Pending normal maps reuse the bounded source index until the next frame."""
from pathlib import Path
import argparse,hashlib,json,os,re,statistics,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'scripts'),str(ROOT/'tests')]
import northlight_paths as fp
from build_renderer import source_identity
from windows_gpu_guard import processes
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--backend',type=Path,action='append')
p.add_argument('--reference',type=Path,help='Include a frozen auto_normal_cache.h for same-executable ABBA')
a=p.parse_args();initial=processes();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
identity=source_identity();sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
backends=a.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll']
inputs=[Path(__file__),ROOT/'tests/material_pending_frame_gpu.cpp',ROOT/'tests/auto_normal_reuse_gpu.cpp',ROOT/'tests/composite_distance_gpu.cpp',ROOT/'tests/removal_distance_gpu.cpp',*backends]
flags=[]
if a.reference:
 inputs.append(a.reference.resolve());(out/'pending_reference.h').write_bytes(a.reference.read_bytes());flags=['-DNORTHLIGHT_PENDING_REFERENCE=1','-I'+str(out)]
hashes={str(path.resolve()):sha(path) for path in inputs};exe=out/'pending-frame.exe'
cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',*flags,*fp.test_include_flags(),ROOT/'tests/material_pending_frame_gpu.cpp','-luser32','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
report={'pass':False,'source_identity':identity,'inputs':hashes,'command':list(map(str,cmd)),'exe_sha256':sha(exe),'cases':[],'game_launched':False,'game_present_at_start':any(row['name'].lower().startswith('wow') for row in initial),'whole_frame_time_measured':False}
try:
 for index,backend in enumerate(backends):
  processes();folder=out/str(index);folder.mkdir()
  run=subprocess.run([str(exe),str(backend.resolve()),str(folder)],cwd=folder,env={**fp.zig_env(),'DXVK_LOG_PATH':str(folder),'DISABLE_VULKAN_OBS_CAPTURE':'1'},capture_output=True,text=True,timeout=180)
  (folder/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8');run.check_returncode();assert 'MATERIAL_PENDING_FRAME_PASS' in run.stdout
  entry={'backend':str(backend.resolve()),'correctness':True}
  if a.reference:
   rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('MATERIAL_PENDING_TIMING ') and 'warm=0 ' in line];entry['timing']=[]
   for pending,count in ((pending,count) for pending in (0,1) for count in (1,8,32)):
    result={'pending':bool(pending),'textures':count}
    for current in (0,1):
     values=[float(r['cpuMs']) for r in rows if int(r['pending'])==pending and int(r['textures'])==count and int(r['current'])==current];assert len(values)==6
     result['current_ms' if current else 'before_ms']=statistics.median(values)
    entry['timing'].append(result)
  report['cases'].append(entry);print(json.dumps(entry),flush=True)
 report['pass']=True
finally:
 report['inputs_unchanged']=source_identity()==identity and all(sha(Path(path))==value for path,value in hashes.items());report['pass'] &= report['inputs_unchanged']
 (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert report['inputs_unchanged']
