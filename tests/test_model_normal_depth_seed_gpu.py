# northlight-test: requires=windows-gpu,zig,client manual timing
"""Paired private-depth seed -> actual WorldNormals; no game control."""
import argparse,hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
import northlight_paths as fp
fp.use_source_modules()
import client_fixtures
from world_scene_builder import Assets
from extract_signatures import variants
from windows_gpu_guard import processes
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--backend',type=Path,action='append',required=True);p.add_argument('--timing',action='store_true');p.add_argument('--format',choices=['R32F','DF24','INTZ'],action='append');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);games=processes()
shutil.copyfile(client_fixtures.four_bone_body_vs3(),out/'body.bin');assets=Assets()
try:
 for family,name in [('opaque','pixel.bin'),('mod','alpha.bin')]:
  (out/name).write_bytes(next(code for i,_,code in variants(assets.read('shaders\\pixel\\ps_3_0\\combiners_'+family+'.bls')) if i==0))
finally:assets.close()
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs={str(f):sha(f) for f in [*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.inl'),ROOT/'tests/model_normals_gpu.cpp',ROOT/'tests/model_normal_depth_seed_gpu.cpp',Path(__file__).resolve(),out/'body.bin',out/'pixel.bin',out/'alpha.bin']}
exe=out/'depth-seed.exe';command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',*fp.test_include_flags(),'-I'+str(ROOT/'tests'),ROOT/'tests/model_normal_depth_seed_gpu.cpp',fp.src('world_gi.cpp'),'-luser32','-luuid','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,command)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
results=[]
for index,backend in enumerate(a.backend):
 for fmt in a.format or ['R32F']:
  processes();r=subprocess.run(list(map(str,[exe,backend.resolve(),out/'body.bin',out/'pixel.bin',out/'alpha.bin',fmt,*(['timing'] if a.timing else [])])),env={**os.environ,'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_CONFIG':'d3d9.customVendorId = 1002; d3d9.supportDFFormats = True'},capture_output=True,timeout=240)
  name=f'gpu-{index}-{fmt}.log';(out/name).write_bytes(r.stdout+r.stderr);print(name,(r.stdout+r.stderr).decode(errors='replace')[-700:],flush=True);r.check_returncode();assert b'MODEL_NORMAL_DEPTH_SEED_PASS' in r.stdout
  results.append(dict(backend=str(backend.resolve()),backend_sha256=sha(backend),format=fmt,log=name,sha256=sha(out/name)))
assert all(sha(Path(f))==h for f,h in inputs.items()),'Source changed during oracle'
(out/'validation.json').write_text(json.dumps(dict(pass_=True,timing=a.timing,concurrent_games=games,inputs=inputs,results=results,command=list(map(str,command)),scope='Authored DXVK D24/RESZ, native directly bound INTZ or R32F depth -> production normal replay -> WorldNormals. Paired full seed+replay timestamps; not gameplay FPS or whole-frame budget.'),indent=2)+'\n',encoding='utf-8',newline='\n')
