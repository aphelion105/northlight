# northlight-test: requires=windows-gpu,zig,client timing
"""Exact mixed-size disk-map uploads, frozen/current allocation counts and CPU submission timings."""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes
out=(fp.output_dir()/'material-staging-sizes').resolve();out.mkdir(parents=True,exist_ok=True)
reference='418c3f34acd6289b9d884e608a48c8f6e9eda897'
(out/'reference_persistence.h').write_bytes(subprocess.check_output(['git','show',reference+':src/water/material_map_persistence.h'],cwd=fp.REPO))
exe=out/'staging.exe'
report={'pass':False,'reference':reference,'scope':'Real GPU pixel/queue contract; CPU upload timings exclude target creation and readback; not game FPS','backends':{}}
try:
    command=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-I',out,
             *fp.test_include_flags(),fp.TESTS/'material_staging_sizes_gpu.cpp','-luser32','-o',exe]
    result=subprocess.run(list(map(str,command)),env=fp.zig_env(),capture_output=True,text=True,timeout=600)
    (out/'compile.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode==0,(result.stdout+result.stderr)[-3000:]
    backends={'native':Path(os.environ.get('SystemRoot','C:/Windows'))/'SysWOW64/d3d9.dll',
              'dxvk3':fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll',
              'dxvk2':fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'}
    for name,backend in backends.items():
        assert backend.is_file(),backend
        processes()
        env={**fp.zig_env(),'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(out)}
        result=subprocess.run([str(exe),str(backend),str(out/name)],env=env,cwd=out,capture_output=True,text=True,timeout=300)
        (out/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0,(name,(result.stdout+result.stderr)[-3000:])
        assert 'MATERIAL_STAGING_SIZES_PASS' in result.stdout
        rows=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        assert len(rows)==10 and all(row['exact'] for row in rows)
        medians={label:statistics.median(r['upload_ms'] for r in rows if r['current']==current and not r['cross_aspect']) for label,current in [('before',False),('after',True)]}
        report['backends'][name]={'sha256':hashlib.sha256(backend.read_bytes()).hexdigest(),'rows':rows,'median_ms_per_96_uploads':medians}
        print(name,'96 uploads: allocations 96 -> 1, exact pixels; median CPU ms',medians,flush=True)
    report['pass']=True
finally:
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
