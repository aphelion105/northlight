"""Compare pressure fallback and bounded demand residency, including live eviction.

Native MPQ-derived terrain shaders, authored mesh, exact color/normal/occlusion
comparison at MSAA 0/2. No game process is started.
"""
from pathlib import Path
import argparse,json,re,shutil,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--fixture',required=True,type=Path);p.add_argument('--backend',required=True,type=Path)
a=p.parse_args();out=fp.output_dir()/'material-pressure-policy';out.mkdir(parents=True,exist_ok=False)
env=fp.zig_env();env.update(NORTHLIGHT_TEST_PRESSURE='1',NORTHLIGHT_TEST_MESH_GRID='24',NORTHLIGHT_TEST_CAPTURE_DRAWS='40',NORTHLIGHT_TEST_DEFERRED_CAPTURE='1',NORTHLIGHT_TEST_MATERIAL_BATCH_TIMING='1',NORTHLIGHT_TEST_MATERIAL_BATCH_VALIDATE='1')
def run(label,command,flags=None):
    r=subprocess.run(list(map(str,command)),cwd=out,env={**env,**(flags or {})},capture_output=True,text=True,timeout=180)
    (out/(label+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');assert r.returncode==0,(label,r.stdout[-4000:],r.stderr[-4000:]);return r.stdout
exe=out/'pressure-policy.exe'
run('compile',[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_TEST_MATERIAL_PRESSURE_POLICY=1','-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200',*fp.test_include_flags(),fp.TESTS/'auto_normal_oblique_gpu.cpp','-luser32','-o',exe])
files=[*a.fixture.glob('source.fgr*'),*(a.fixture/n for n in ['terrain-vs.bin','terrain-ps.bin','PreviewBase.bin'])];results=[]
for samples in [0,2]:
    folders={}
    for policy in ['critical','demand']:
        label=f'msaa{samples}-{policy}';folder=out/label;folder.mkdir();folders[policy]=folder
        for f in files:shutil.copyfile(f,folder/f.name)
        flags={'NORTHLIGHT_TEST_MSAA':str(samples)}
        if policy=='demand':flags['NORTHLIGHT_TEST_DEMAND_PRESSURE']='1'
        text=run(label,[exe,a.backend.resolve(),str(folder)+'/',12,.7],flags)
        if policy=='demand' and samples:assert 'MATERIAL_CRITICAL_RELEASE_PASS' in text
        timing=re.search(r'MATERIAL_BATCH_TIMING .*gpuMs=([\d.]+) submitMs=([\d.]+)',text);assert timing
        results.append(dict(msaa=samples,policy=policy,gpu_ms=float(timing[1]),submit_ms=float(timing[2])))
    names=[f.name for f in folders['critical'].glob('batch-*.fgr')];assert len(names)>=2
    for name in names:assert (folders['critical']/name).read_bytes()==(folders['demand']/name).read_bytes(),(samples,name)
(out/'validation.json').write_text(json.dumps(dict(pass_=True,exact_color_normal_occlusion=True,cases=results),indent=2)+'\n');print(json.dumps(results,indent=2))
