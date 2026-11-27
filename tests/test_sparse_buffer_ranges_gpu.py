# northlight-test: requires=windows-gpu,zig,client
"""Render sparse VB/IB updates through the real stream: frozen/current exact pixels."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--x64-backend',type=Path,help='Also check full shadows on this x64 DXVK backend')
args=parser.parse_args()

out=(fp.output_dir()/'sparse-buffer-ranges-gpu').resolve();out.mkdir(parents=True,exist_ok=True)
reference='43c9f510d3b46b9cfec3f43f4f86cf3cdde3a144'
baseline=out/'baseline';baseline.mkdir(exist_ok=True)
with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','archive',reference,'src'],cwd=fp.REPO))) as tar:
    for member in tar:
        if not member.isfile():continue
        target=(baseline/member.name).resolve();assert target.is_relative_to(baseline.resolve())
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(tar.extractfile(member).read())
backend=fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'
report={'pass':False,'reference':reference,'backend_sha256':hashlib.sha256(backend.read_bytes()).hexdigest(),
        'scope':'Real D3D9 stream and indexed rendering; no gameplay FPS claim','images':{}}
variants=[('before',baseline,'x86-windows-gnu',backend),('after',fp.REPO,'x86-windows-gnu',backend)]
if args.x64_backend:
    variants.append(('x64-large',fp.REPO,'x86_64-windows-gnu',args.x64_backend.resolve()))
    report['x64_backend_sha256']=hashlib.sha256(variants[-1][3].read_bytes()).hexdigest()
try:
    for label,root,target,driver in variants:
        includes=[str(root/Path(f).relative_to(fp.REPO)) if Path(f).is_absolute() else f for f in fp.include_flags()]
        exe=out/(label+'.exe');pixels=out/(label+'.bgra')
        calls=[('compile',[fp.zig(),'c++','-target',target,'-std=c++17','-O2','-fno-rtti','-UNDEBUG',
             f'-DNORTHLIGHT_EXPECT_SPARSE={int(label!="before")}',*includes,fp.TESTS/'sparse_buffer_ranges_gpu.cpp','-luser32','-lole32','-o',exe]),
             ('gpu',[exe,driver,pixels])]
        for step,cmd in calls:
            processes()
            env={**fp.zig_env(),'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(out)}
            result=subprocess.run(list(map(str,cmd)),cwd=fp.REPO,env=env,capture_output=True,text=True,timeout=600)
            (out/f'{label}-{step}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
            assert result.returncode==0,(label,step,(result.stdout+result.stderr)[-3000:])
        assert 'SPARSE_GPU_PASS' in result.stdout
        if label=='x64-large':
            assert 'LARGE_SHADOW_GPU_PASS' in result.stdout
            assert pixels.read_bytes()==(out/'before.bgra').read_bytes()
        report['images'][label]=hashlib.sha256(pixels.read_bytes()).hexdigest()
        print(label,result.stdout.strip())
    assert (out/'before.bgra').read_bytes()==(out/'after.bgra').read_bytes()
    report['pass']=True
    print('SPARSE_GPU_EXACT_PASS changed_pixels=0')
finally:
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
