#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
'''Compare actual production material flush to its unelided bindings; no GPU/game.'''
from pathlib import Path
import hashlib,json,re,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
out=fp.output_dir()/'material-batch-bindings';out.mkdir(parents=True,exist_ok=True)
source=fp.TESTS/'test_material_batch_bindings.cpp';reference=fp.TESTS/'displacement_state_reuse.cpp'
watched=[p for p in fp.SRC.rglob('*') if p.is_file()]+[source,reference,Path(__file__),fp.REPO/'northlight_paths.py']
inputs={str(p.resolve()):sha(p) for p in watched}
helper=reference.read_text().split('int main(){')[0]
assert helper.count('struct MaterialCaptureStateTestAccess {')==1
helper=helper.replace('struct MaterialCaptureStateTestAccess {','struct MaterialCaptureStateTestAccess {\n    static void replayBindings();')
(out/'material_bindings_reference.h').write_bytes(helper.encode())
header=fp.src('auto_bump_renderer.h').read_text()
old=header
old=old.replace('        bool frequencyKnown=false,viewportKnown=false,auxiliaryKnown=false,boundAuxiliary=false;\n        D3DVIEWPORT9 boundViewport{};\n','')
freq='            if(needed(frequencyKnown)){const HRESULT result=d->SetStreamSourceFreq(0,1);step(result);frequencyKnown=SUCCEEDED(result);}'
vp='''            if(needed(viewportKnown&&!std::memcmp(&boundViewport,&packet->viewport,sizeof boundViewport))){
                const HRESULT result=d->SetViewport(&packet->viewport);step(result);if(SUCCEEDED(result)){boundViewport=packet->viewport;viewportKnown=true;}}'''
aux='''            // A render-target bind may reset the viewport. Elide MRT1 only
            // when that reset is equivalent to the captured full-size viewport.
            const bool defaultViewport=!packet->viewport.X&&!packet->viewport.Y&&packet->viewport.Width==width&&packet->viewport.Height==heightPixels&&packet->viewport.MinZ==0.f&&packet->viewport.MaxZ==1.f;
            if(SUCCEEDED(hr)&&needed(auxiliaryKnown&&boundAuxiliary==packet->occlusion&&defaultViewport)){
                hr=d->SetRenderTarget(1,packet->occlusion?occlusionTarget:nullptr);
                if(SUCCEEDED(hr)){auxiliaryKnown=true;boundAuxiliary=packet->occlusion;viewportKnown=false;}}'''
for current,replacement in [(freq,'            step(d->SetStreamSourceFreq(0,1));'),(vp,'            step(d->SetViewport(&packet->viewport));'),(aux,'            if(SUCCEEDED(hr))hr=d->SetRenderTarget(1,packet->occlusion?occlusionTarget:nullptr);')]:
    assert old.count(current)==1;old=old.replace(current,replacement)
old=old.replace('                    // Keep no binding assumptions across the UP fallback.\n                    frequencyKnown=viewportKnown=auxiliaryKnown=false;','')
control=out/'control';control.mkdir(exist_ok=True);(control/'auto_bump_renderer.h').write_bytes(old.encode())
records=[]
for optimization in ['-O2','-O0']:
    cells={}
    for name,folder in [('control',control),('current',out)]:
        exe=out/f'fixture-{name}-{optimization[1:]}.exe'
        command=[str(fp.zig()),'c++','-target','x86-windows-gnu',optimization,'-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I',str(folder),'-I',str(out),*fp.test_include_flags(),str(source),'-luser32','-luuid','-o',str(exe)]
        subprocess.run(command,env=fp.zig_env(),check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        (out/f'{name}-{optimization[1:]}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0,result.stdout+result.stderr
        assert 'MATERIAL_BINDINGS_PASS cases=24' in result.stdout
        rows=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in result.stdout.splitlines() if line.startswith('MATERIAL_BINDINGS_CASE ')]
        assert len(rows)==24;cells[name]=rows;records.append({'mode':optimization,'name':name,'command':command,'exe_sha256':sha(exe),'rows':rows})
    for a,b in zip(cells['control'],cells['current']):
        for field in ['reset','shape','failure','success','draws','hash','resident','up','restored']:assert a[field]==b[field],(field,a,b)
        for field in ['viewportCalls','frequencyCalls','auxiliaryCalls']:assert int(b[field])<=int(a[field]),(field,a,b)
        if b['shape']=='0' and b['failure']=='0':
            assert int(b['frequencyCalls'])<int(a['frequencyCalls']) and int(b['auxiliaryCalls'])<int(a['auxiliaryCalls']) and int(b['viewportCalls'])<int(a['viewportCalls'])
# Counterfactuals must fail the independent state oracle.
negative_records=[]
for name,text in [('up',header.replace('frequencyKnown=viewportKnown=auxiliaryKnown=false;','')),
                  ('viewport',header.replace('boundAuxiliary==packet->occlusion&&defaultViewport','boundAuxiliary==packet->occlusion'))]:
    folder=out/('negative-'+name);folder.mkdir(exist_ok=True);(folder/'auto_bump_renderer.h').write_bytes(text.encode());exe=folder/'fixture-unsafe.exe'
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I',str(folder),'-I',str(out),*fp.test_include_flags(),str(source),'-luser32','-luuid','-o',str(exe)],env=fp.zig_env(),check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True);(folder/'result.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    if name=='up':assert result.returncode!=0 and 'MATERIAL_BINDING_FREQUENCY_MISMATCH' in result.stderr,result.stdout+result.stderr
    else:
        assert result.returncode==0,result.stdout+result.stderr
        rows=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in result.stdout.splitlines() if line.startswith('MATERIAL_BINDINGS_CASE ')]
        assert len(rows)==24 and any(a['hash']!=b['hash'] for a,b in zip(cells['control'],rows)), 'Unsafe MRT elision escaped the viewport-reset oracle'
    negative_records.append({'name':name,'stdout':result.stdout,'stderr':result.stderr,'rejected':True})
assert all(sha(Path(p))==h for p,h in inputs.items()),'Source/helper changed during final validation'
(out/'validation.json').write_text(json.dumps({'inputs':inputs,'inputs_unchanged':True,'records':records,'production_flush':True,'unelided_control':True,'negative_controls':negative_records,'timings_claimed':False,'gpu_or_game_launched':False},indent=2)+'\n')
print('PASS exact material draw state/order, resident/UP fallback, viewport/MRT effects, failures/refcounts/restoration; fewer redundant bindings.')
