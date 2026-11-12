# northlight-test: requires=windows-gpu,zig,client manual
"""Fixed-world camera rotation through the actual displacement prepare/draw path."""
from pathlib import Path
import argparse, hashlib, os, re, shutil, subprocess, sys, json, struct
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser();p.add_argument('--backend',default='C:/Windows/SysWOW64/d3d9.dll');p.add_argument('--reuse-exe',type=Path)
p.add_argument('--body-budget',action='store_true',help='Real R0 body shader with forced envelope and vertex budget exhaustion')
p.add_argument('--stress',action='store_true',help='96 rapid yaw changes, including offscreen frames')
p.add_argument('--reference-headers',type=Path,help='Frozen auto_bump/auto_displacement headers for the broken-body negative control')
a=p.parse_args();assert not a.reference_headers or a.body_budget
if a.stress and not a.body_budget:p.error('--stress requires --body-budget; ordinary camera validation retains its fixed-gain oracle')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for p in [*[p for p in fp.SRC.rglob('*') if p.is_file()],Path(__file__),fp.TESTS/'auto_normal_oblique_gpu.cpp']}
if a.reference_headers:inputs.update({str(p):sha(p) for p in a.reference_headers.glob('*.h')})
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True);exe=a.reuse_exe.resolve() if a.reuse_exe else out/'camera.exe'
if not a.reuse_exe:subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200',*(['-I'+str(a.reference_headers.resolve())] if a.reference_headers else []),*fp.test_include_flags(),str(fp.TESTS/'auto_normal_oblique_gpu.cpp'),'-luser32','-luuid','-o',str(exe)],env=fp.zig_env(),check=True)
source=fp.out()/'model-materials-dxvk/3-weighted4-combiners_opaque'
for f in source.iterdir():
    if f.name.startswith('source.fgr') or f.suffix=='.bin':shutil.copy2(f,out/f.name)
if a.body_budget:
    sys.path.insert(0,str(fp.TESTS));import client_fixtures
    shutil.copyfile(client_fixtures.four_bone_body_vs3(),out/'terrain-vs.bin')
env={k:v for k,v in fp.zig_env().items() if not k.startswith('NORTHLIGHT_TEST_')};env['NORTHLIGHT_TEST_CAMERA_DISPLACEMENT']='1'
if a.stress:env['NORTHLIGHT_TEST_CAMERA_STRESS']='1'
if a.body_budget:env['NORTHLIGHT_TEST_BODY_BUDGET']='1'
if a.reference_headers:env['NORTHLIGHT_TEST_BODY_CONTROL']='1'
run=subprocess.run([str(exe),str(Path(a.backend).resolve()),str(out)+'/', '12','.7','model4'],env=env,cwd=out,capture_output=True,text=True,timeout=180)
(out/'gpu.log').write_text(run.stdout+run.stderr);print(run.stdout,flush=True);assert run.returncode==0,run.stderr
assert 'DISPLACEMENT_CAMERA_FACTORY_PASS' in run.stdout
records=[];visible=0
budget=[dict(frame=int(m[0]),normal_gain=float(m[1]),fallback_gain=float(m[2]),scan_vertices=int(m[3]),exhausted_ready=bool(int(m[4]))) for m in re.findall(r'BODY_BUDGET_FRAME frame=(\d+) normalGain=([\d.]+) fallbackGain=([\d.]+) scanVertices=(\d+) exhaustedReady=(\d+)',run.stdout)]
if a.body_budget:
    assert len(budget)==(96 if a.stress else 9)
    if a.reference_headers:assert any(r['normal_gain']!=r['fallback_gain'] and r['scan_vertices'] and not r['exhausted_ready'] for r in budget)
    else:assert all(r['normal_gain']==r['fallback_gain'] and not r['scan_vertices'] and r['exhausted_ready'] for r in budget)
for frame in range(96 if a.stress else 9):
    images=[]
    for oracle in range(3):
        data=(out/f'camera-{frame}-{oracle}.fgr').read_bytes();_,w,h,_,stride=struct.unpack_from('<5I',data);images.append(np.frombuffer(data,dtype='<f4' if stride==16 else '<f2',offset=20).astype(np.float32))
    error=float(np.abs(images[0]-images[1]).max())
    if not a.reference_headers:assert error<.001,error
    negative=float(np.abs(images[1]-images[2]).max());visible+=negative>.001
    records.append(dict(frame=frame,max_abs=error,zero_gain_difference=negative))
assert visible>=min(3,len(records)),visible
if a.reference_headers:assert any(r['max_abs']>.001 for r in records)
assert all(sha(Path(p))==h for p,h in inputs.items()),'Source changed during camera oracle'
(out/'validation.json').write_text(json.dumps(dict(pass_=True,backend=a.backend,body_budget=a.body_budget,reference_headers=str(a.reference_headers) if a.reference_headers else None,inputs=inputs,budget=budget,visible_frames=visible,comparisons=records,scope='Correctness only: real client shaders and production geometry; no gameplay timing.'),indent=2)+'\n',encoding='utf-8');print('CAMERA_GPU_PASS',len(records),'visible',visible,'budget_frames',len(budget))
