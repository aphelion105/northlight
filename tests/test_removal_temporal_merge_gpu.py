# northlight-test: requires=windows-gpu,zig,client
"""Two-pass/fused lighting image sequences and ABBA GPU timings; no game FPS claim."""
import argparse,hashlib,json,os,struct,subprocess,sys
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parent)]
import northlight_paths as fp
from validate_removal_distance_gpu import fixture,image
from windows_gpu_guard import processes

p=argparse.ArgumentParser(__doc__)
p.add_argument('--candidate',type=Path,default=fp.COMPILED/'TemporalLightMerged.bin')
p.add_argument('--reference-fused',type=Path,help='Compare two fused shaders instead of the legacy two-pass path')
p.add_argument('--output',type=Path)
p.add_argument('--benchmark',action='store_true')
p.add_argument('--timing-batch',type=int,choices=[32,128,256],default=32,help='Longer GPU batches reduce background-load and clock noise')
p.add_argument('--control',action='store_true')
p.add_argument('--capture-prefix')
p.add_argument('--backend',type=Path,default=fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')
a=p.parse_args();out=(a.output or fp.output_dir()/'removal-temporal-merge').resolve();out.mkdir(parents=True,exist_ok=True)
baseline=out/'baseline';baseline.mkdir(exist_ok=True)
for name in ['RemovalSmooth','TemporalLight','WorldDistance']:
 (baseline/(name+'.bin')).write_bytes(subprocess.check_output(['git','show','6e981fe2362a4584a64e93d5b2040e00b6849e24:shaders/compiled/'+name+'.bin'],cwd=fp.REPO))
backend=a.backend.resolve()
source=fp.TESTS/'removal_temporal_merge_gpu.cpp';exe=out/'merge.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',str(source),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
prefixes=[]
for name in ['flat','edges','odd','sky','no-source','fog','dense-fog','near-far','normal-edges']+(['4k','4k-lit','4k-coherent','4k-shadow'] if a.benchmark else []):
 prefix=fixture(out,name,3840 if name.startswith('4k') else 137,2160 if name.startswith('4k') else 79)
 c=list(struct.unpack('<272f',Path(prefix+'constants.f32').read_bytes()))
 for i in range(16):c[53*4+i]=float(i%5==0)
 c[57*4]=.85;c[15*4+3]=.8
 Path(prefix+'constants.f32').write_bytes(struct.pack('<272f',*c));prefixes.append(prefix)
 if name in ('4k-lit','4k-coherent','4k-shadow'):
  image(Path(prefix+'light.fgr'),1920,1080,113,'4e',((-.2,-.1,.05,.2 if name=='4k-shadow' or name=='4k-coherent' and x<480 else 1.) for y in range(1080) for x in range(1920)))
 if name=='normal-edges':
  image(Path(prefix+'normals.fgr'),68,39,113,'4e',((0.,0.,1.,0.) if x<10 else ((1.,0.,0.,1.) if x%13<6 else (0.,0.,1.,1.)) for y in range(39) for x in range(68)))
if a.capture_prefix:prefixes.append(str(Path(a.capture_prefix).resolve()))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(path):sha(path) for path in [source,Path(__file__),fp.TESTS/'removal_distance_gpu.cpp',fp.TESTS/'validate_removal_distance_gpu.py',a.candidate,backend,*([a.reference_fused] if a.reference_fused else []),*baseline.glob('*.bin'),
 *(Path(prefix+suffix) for prefix in prefixes for suffix in ['constants.f32','scene.fgr','depth.fgr','light.fgr','baseline.fgr','normals.fgr','ao.fgr'])]}
rows=[]
for prefix in prefixes:
 processes();command=[str(exe),prefix,str(baseline),str(a.candidate.resolve()),str(backend),'control' if a.control else 'compare']+([str(a.reference_fused.resolve())] if a.reference_fused else [])
 result=subprocess.run(command,cwd=out,env={**fp.zig_env(),'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(out),'NORTHLIGHT_TIMING_BATCH':str(a.timing_batch)},capture_output=True,text=True,timeout=180)
 (out/(Path(prefix).name+'gpu.log')).write_text(result.stdout+result.stderr);result.check_returncode()
 assert 'REMOVAL_TEMPORAL_MERGE_PASS' in result.stdout
 events=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
 rows.append({'prefix':prefix,'events':events,'origin':'saved-inputs-with-authored-history' if a.capture_prefix and prefix==str(Path(a.capture_prefix).resolve()) else 'authored-fixture','whole_scene_replay':False});print(Path(prefix).name,events[0],flush=True)
assert all(sha(Path(path))==h for path,h in inputs.items())
(out/'validation.json').write_text(json.dumps({'passed':True,'inputs':inputs,'rows':rows,'control':a.control,'timing_batch':a.timing_batch,'reference_fused':str(a.reference_fused) if a.reference_fused else None,'scope':'Authored multi-frame smoothing/temporal graph; background Forever remains; not gameplay FPS.'},indent=2)+'\n')
