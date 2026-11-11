# northlight-test: requires=windows-gpu,zig,client
"""Replay the seven native draws captured on 2026-11-11; requires that capture."""
import argparse,json,os,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes
processes()
fp.use_source_modules()
from world_scene_builder import Assets,blp_mips
parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--capture',type=Path,default=Path(__file__).resolve().parents[1]/'out/aurora-b-captures/session-18884-52095218-0')
parser.add_argument('--scene',type=Path);parser.add_argument('--depth',type=Path)
args=parser.parse_args();capture=args.capture.resolve()
assert bool(args.scene)==bool(args.depth)
rows=[d for line in (capture/'aurora-native.jsonl').read_text().splitlines() if (d:=json.loads(line)).get('event')=='draw']
assert len(rows)==7
(capture/'draw-args.bin').write_bytes(b''.join(struct.pack('<4I',*[r[k] for k in ['primitive_count','first','min_index','num_vertices']]) for r in rows))
assets=Assets()
try:
    for i,name in enumerate(['auroralight_yg_b','auroramask_b_orange']):
        blob=assets.read('environments\\stars\\'+name+'.blp')
        _,w,h,start,size=next(m for m in blp_mips(blob) if m[1:3]==(32,16))
        (capture/f'texture-upload-{i}.bin').write_bytes(blob[start:start+size])
finally:assets.close()
out=fp.output_dir()/'aurora-native-capture';out.mkdir(exist_ok=True)
exe=out/'aurora-native-capture.exe'
run=subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-return-type','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(fp.TESTS/'aurora_native_capture_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),capture_output=True,text=True)
(out/'compile.log').write_text(run.stdout+run.stderr);print(run.stderr[-2000:],flush=True);run.check_returncode()
results=[]
for name,backend in [('native',Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
    folder=out/name;folder.mkdir(exist_ok=True)
    scene=[str(args.scene.resolve()),str(args.depth.resolve())] if args.scene else []
    run=subprocess.run([str(exe),str(backend),str(capture),*scene],cwd=folder,capture_output=True,text=True,timeout=90,env=dict(os.environ,DXVK_LOG_PATH=str(folder),DISABLE_VULKAN_OBS_CAPTURE='1'))
    (folder/'run.log').write_text(run.stdout+run.stderr);print(name,run.returncode,run.stdout+run.stderr,flush=True);run.check_returncode();results.append(dict(backend=name,result=run.stdout))
(out/'validation.json').write_text(json.dumps(results,indent=2))
