#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Compare immediate and deferred M2 capture, including full submission cost. Never starts WoW."""
from pathlib import Path
import argparse,json,re,shutil,statistics,struct,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--backend',required=True);p.add_argument('--benchmark',action='store_true')
p.add_argument('--occlusion-off',action='store_true',help='Match a normal-only capture without the auxiliary MRT')
p.add_argument('--reuse-exe',type=Path);p.add_argument('--quick',action='store_true')
p.add_argument('--output-subdir',default='material-deferred')
a=p.parse_args();out=fp.out()/a.output_subdir;out.mkdir(exist_ok=True)
backend=str(Path(a.backend).resolve())

def compile_fixture(width,height,name):
    exe=out/(name+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_MATERIAL_MIRROR',f'-DNORTHLIGHT_OBLIQUE_WIDTH={width}',f'-DNORTHLIGHT_OBLIQUE_HEIGHT={height}',*fp.test_include_flags(),str(fp.TESTS/'auto_normal_oblique_gpu.cpp'),'-luser32','-luuid','-o',str(exe)],env=fp.zig_env(),check=True)
    data=bytearray(exe.read_bytes());at=struct.unpack_from('<I',data,0x3c)[0]+22
    struct.pack_into('<H',data,at,struct.unpack_from('<H',data,at)[0]|0x20);exe.write_bytes(data)
    return exe

def run(exe,source,label,on,samples=2,**flags):
    folder=out/(label+'-'+str(on));folder.mkdir(exist_ok=True)
    for f in source.iterdir():
        if f.name.startswith('source.fgr') or f.name in ('terrain-vs.bin','terrain-ps.bin','PreviewBase.bin'):
            shutil.copy2(f,folder/f.name)
    env=fp.zig_env()
    for k in list(env):
        if k.startswith('NORTHLIGHT_TEST_'):env.pop(k)
    env.pop('NORTHLIGHT_EXPERIMENTAL_SHARED_POM',None)
    env['DXVK_CONFIG']='d3d9.customVendorId = 1002; '+env.get('DXVK_CONFIG','')
    env['NORTHLIGHT_TEST_MSAA']=str(samples)
    env.update({'NORTHLIGHT_TEST_'+k:str(v) for k,v in flags.items()})
    if a.occlusion_off:env['NORTHLIGHT_TEST_MATERIAL_BATCH_NO_OCCLUSION']='1'
    if on:env['NORTHLIGHT_TEST_DEFERRED_CAPTURE']='1'
    variant='model4' if 'weighted4' in source.name else 'modelunsafe' if 'varying' in source.name else 'model'
    r=subprocess.run([str(exe),backend,str(folder)+'/', '12','.7',variant],env=env,cwd=out,capture_output=True,text=True,timeout=180)
    (out/(label+'-'+str(on)+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
    assert r.returncode==0,(label,on,r.stdout[-3000:],r.stderr[-1500:])
    return folder,r.stdout

def compare(folders,pattern):
    files=list(folders[0].glob(pattern));assert files,pattern
    for f in files:assert f.read_bytes()==(folders[1]/f.name).read_bytes(),(str(f), 'not bit exact')
    return len(files)

source=fp.out()/'model-materials-dxvk/3-weighted4-combiners_opaque'
assert source.exists(),'Prepare validate_model_materials.py fixtures first'
exe=a.reuse_exe.resolve() if a.reuse_exe else compile_fixture(320,200,'deferred-check')
records=[]
if not a.reuse_exe:
    sources=[source] if a.quick else sorted((fp.out()/'model-materials-dxvk').glob('[23]-*combiners*'))
    for fixture in sources:
        runs=[run(exe,fixture,'model-'+fixture.name,on) for on in [0,1]];pair=[r[0] for r in runs]
        if fixture==source:assert 'MATERIAL_DEFERRED_MODE mode=1 queued=1 flushed=1' in runs[1][1]
        count=compare(pair,'*.fgr');records.append(dict(fixture=fixture.name,outputs=count,bit_exact=True))
    for samples in [0,4,8]:
        pair=[run(exe,source,'msaa-'+str(samples),on,samples)[0] for on in [0,1]]
        records.append(dict(msaa=samples,outputs=compare(pair,'*.fgr'),bit_exact=True))
    for flag in ['DEFERRED_MUTATION','DEFERRED_BARRIER','DEFERRED_UNUSED','DEFERRED_LIMIT']:
        runs=[run(exe,source,flag.lower(),on,MATERIAL_BATCH_TIMING=1,MATERIAL_BATCH_VALIDATE=1,**{flag:1}) for on in [0,1]];pair=[r[0] for r in runs]
        if flag=='DEFERRED_UNUSED':assert 'MATERIAL_DEFERRED_UNUSED_PASS' in runs[1][1]
        records.append(dict(safety=flag,outputs=compare(pair,'batch-*.fgr'),bit_exact=True))
    _,log=run(exe,source,'deferred-failure',1,MATERIAL_BATCH_TIMING=1,DEFERRED_FAILURE=1)
    assert 'MATERIAL_DEFERRED_FAILURE_PASS' in log
    records.append(dict(safety='DEFERRED_FAILURE',incomplete_capture_hidden=True))
    _,log=run(exe,source,'deferred-pressure',1,MATERIAL_BATCH_TIMING=1,DEFERRED_PRESSURE=1,DEFERRED_MUTATION=1)
    assert 'MATERIAL_DEFERRED_PRESSURE_PASS' in log
    records.append(dict(safety='DEFERRED_PRESSURE',source_cache_released=True))
    _,log=run(exe,source,'deferred-profile',1,MATERIAL_PROFILE=1)
    assert 'stage=deferredFlush' in log and 'rawCaptured=' in log and 'complete=1' in log
    records.append(dict(profiling=True,complete_gpu_coverage=True))
    print('MATERIAL_DEFERRED_EQUIVALENCE',len(records),flush=True)
if a.benchmark or a.reuse_exe:
    exe=a.reuse_exe.resolve() if a.reuse_exe else compile_fixture(3840,2160,'deferred-bench')
    timings=[]
    for repeat in range(3):
        folders={}
        for on in ([0,1] if repeat%2==0 else [1,0]):
            folder,log=run(exe,source,'timing-'+str(repeat),on,MATERIAL_BATCH_TIMING=1,MATERIAL_BATCH_VALIDATE=1)
            m=re.search(r'MATERIAL_BATCH_TIMING .*gpuMs=([\d.]+) submitMs=([\d.]+)',log);assert m
            record=dict(repeat=repeat,deferred=on,gpu_ms=float(m[1]),cpu_ms=float(m[2]));timings.append(record);folders[on]=folder
            print('MATERIAL_DEFERRED_TIMING',record,flush=True)
        assert compare([folders[0],folders[1]],'batch-*.fgr')==(2 if a.occlusion_off else 3)
    result=dict(backend=backend,occlusion_enabled=not a.occlusion_off,runs=timings,checks=records,targets_bit_exact=True)
    for on in [0,1]:result['deferred' if on else 'immediate']={key:statistics.median(r[key] for r in timings if r['deferred']==on) for key in ['gpu_ms','cpu_ms']}
    result['gpu_reduction_percent']=100*(1-result['deferred']['gpu_ms']/result['immediate']['gpu_ms'])
else:result=dict(backend=backend,checks=records)
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('MATERIAL_DEFERRED_PASS',json.dumps(result),flush=True)
