# northlight-test: requires=windows-gpu,zig,client manual timing
"""Actual material pose producer -> normal capture, old/new policy in one executable."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
import northlight_paths as fp
fp.use_source_modules()
import client_fixtures
from world_scene_builder import Assets
from extract_signatures import variants
from windows_gpu_guard import processes
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--backend',type=Path,action='append',required=True);p.add_argument('--benchmark',action='store_true');p.add_argument('--range-shortcut',action='store_true',help='Compare r107 range publication/query with the shortcut; handoff stays enabled in both policies.');p.add_argument('--source-budget',action='store_true',help='Compare repeated-source CPU charging with separate warm consumers and disabled GPU residency.');p.add_argument('--indexed',action='store_true',help='Use indexed geometry in the source-budget fixture.');p.add_argument('--source-pattern',choices=['repeat','pairs','interleaved'],default='repeat');p.add_argument('--budget-negative-control',action='store_true');p.add_argument('--residency',choices=['disabled','warm'],default='disabled');a=p.parse_args();assert a.residency=='disabled' or a.source_budget;assert (a.source_pattern=='repeat' and not a.budget_negative_control) or a.source_budget;assert not (a.range_shortcut and a.source_budget);assert not a.indexed or a.source_budget
games=[p for p in processes() if p['name'].lower().startswith('wow')];out=a.output.resolve();out.mkdir(exist_ok=True,parents=True);overlay=out/'overlay';overlay.mkdir(exist_ok=True)
source=fp.src('auto_displacement_renderer.h').read_text();marker='if(!reusePose&&(view.low[2]>48.f||view.high[2]<-48.f))publishPose(view);';assert source.count(marker)==1
source=source.replace('class NorthlightAutoDisplacement {','extern bool northlightLocalPoseHandoff;\nclass NorthlightAutoDisplacement {',1).replace(marker,'if(northlightLocalPoseHandoff&&!reusePose&&(view.low[2]>48.f||view.high[2]<-48.f))publishPose(view);');(overlay/'auto_displacement_renderer.h').write_bytes(source.encode())
cpp=ROOT/'tests/material_pose_handoff_gpu.cpp'
if a.range_shortcut:
    header=fp.src('pose_bounds_handoff.h').read_text(encoding='utf-8');before=subprocess.check_output(['git','show','b2385af0:src/replay/pose_bounds_handoff.h'],cwd=ROOT).decode()
    signature='    void publish(';end='    bool outside('
    old=before[before.index(signature):before.index(end)].replace('void publish(','void publishBefore(',1)
    assert header.count(signature)==header.count('return lower>48.01;')==1
    header=header.replace('namespace NorthlightPoseBounds {','extern bool northlightRangeShortcut;\nnamespace NorthlightPoseBounds {',1).replace('void publish(','void publishCurrent(',1)
    wrapper='''    void publish(const NorthlightReplayBounds::Bounds& value,const float* bank,const NorthlightConstantEpoch::Stamp& current,const void* mesh,const void* vs,const void* decl){
        if(northlightRangeShortcut)publishCurrent(value,bank,current,mesh,vs,decl);
        else{publishBefore(value,bank,current,mesh,vs,decl);outsideRange=outside(mesh,vs,decl,current);}
    }
'''
    header=header.replace(end,old+wrapper+end,1).replace('return lower>48.01;','return northlightRangeShortcut?lower>48.01:std::nextafter(lower,-INFINITY)>48.01;',1)
    (overlay/'pose_bounds_handoff.h').write_text(header,encoding='utf-8',newline='\n')
    text=cpp.read_text(encoding='utf-8');assert text.count('northlightLocalPoseHandoff=current;')==text.count('expected+=current&&!anchor')==1
    text=text.replace('bool northlightLocalPoseHandoff=true;','bool northlightRangeShortcut=true;\nbool northlightLocalPoseHandoff=true;',1).replace('northlightLocalPoseHandoff=current;','northlightLocalPoseHandoff=true;northlightRangeShortcut=current;',1).replace('expected+=current&&!anchor','expected+=!anchor',1).replace('rounds=timing?24:2,Count=timing?128:16','rounds=timing?40:2,Count=timing?512:16',1)
    cpp=out/'range-shortcut-gpu.cpp';cpp.write_text(text,encoding='utf-8',newline='\n')
if a.source_budget:
    header=fp.src('model_normal_renderer.h').read_text(encoding='utf-8')
    needle='const bool sharedSource=packet->sharedMesh&&!pending.empty()&&pending.back()->sharedMesh==packet->sharedMesh;';assert header.count(needle)==1
    header=header.replace('class NorthlightModelNormals {','extern bool northlightSharedSourceBudget;\nclass NorthlightModelNormals {',1).replace(needle,needle.replace('=packet->','=northlightSharedSourceBudget&&packet->',1),1)
    check=r"""
public:
    size_t testSourceCharge()const{return pendingBytes;}
    size_t testSourceBudget()const{
        size_t actual=0,spareActual=0;std::vector<const NorthlightDrawSnapshot::Mesh*> sources;
        for(const auto& p:pending){actual+=sizeof(Pending)+p->ps.capacity()*sizeof(float)+p->mesh.capacityBytes();
            if(p->sharedMesh&&std::find(sources.begin(),sources.end(),p->sharedMesh.get())==sources.end()){sources.push_back(p->sharedMesh.get());actual+=p->sharedMesh->capacityBytes();}}
        for(const auto& p:spare){assert(!p->sharedMesh);spareActual+=packetBytes(*p);}
        assert(spareActual==spareBytes&&actual<=pendingBytes&&pendingBytes+spareBytes<=(8u<<20));return sources.size();
    }
"""
    if a.budget_negative_control:
        assert not a.benchmark;header=header.replace('packet->sharedMesh&&!pending.empty()&&pending.back()->sharedMesh==packet->sharedMesh','packet->sharedMesh',1)
    end=header.rindex('\n};');header=header[:end]+check+header[end:];(overlay/'model_normal_renderer.h').write_text(header,encoding='utf-8',newline='\n')
    text=cpp.read_text(encoding='utf-8');setup=next(line for line in text.splitlines() if 'NorthlightModelNormals normals(d);' in line)
    repeated=setup.replace('        NorthlightModelNormals normals(d);','')
    text=text.replace(setup,'        NorthlightModelNormals normalsBefore(d),normalsAfter(d);\n        for(auto* target:{&normalsBefore,&normalsAfter}){auto& normals=*target;'+repeated+'}'+('\n        DisplacementStateTestAccess::geometry(producer)->setLimit(0);' if a.residency=='disabled' else ''),1)
    text=text.replace('bool northlightLocalPoseHandoff=true;','bool northlightSharedSourceBudget=true;\nbool northlightLocalPoseHandoff=true;',1).replace('northlightLocalPoseHandoff=current;','northlightLocalPoseHandoff=true;northlightSharedSourceBudget=current;auto& normals=current?normalsAfter:normalsBefore;',1).replace('expected+=current&&!anchor','expected+=!anchor',1).replace('rounds=timing?24:2,Count=timing?128:16','rounds=timing?32:2,Count=512',1)
    text=text.replace('calls.submitted=0;','calls.submitted=calls.targetSets=0;',1).replace('const auto queued=normals.testPendingPackets();','const auto charged=normals.testSourceCharge();const auto queued=normals.testPendingPackets();',1).replace('                assert(normals.finishCaptures());if(timing)','                if(!timing){const auto owners=normals.testSourceBudget();assert(owners<=3);}assert(normals.finishCaptures());if(!timing)normals.testSourceBudget();if(timing)',1)
    text=text.replace('queued=%zu submitted=%u captureMs','queued=%zu chargedBytes=%zu submitted=%u targets=%u captureMs',1).replace('expected,queued,calls.submitted,std::chrono','expected,queued,charged,calls.submitted,calls.targetSets,std::chrono',1)
    if a.indexed:
        needle='        auto color=target(';where=text.index(needle)
        text=text[:where]+"""        NorthlightDrawSnapshot::Ref<IDirect3DIndexBuffer9> budgetIndex;ok(d->CreateIndexBuffer(UINT(vertices.size()/48*4),0,D3DFMT_INDEX32,D3DPOOL_MANAGED,budgetIndex.out(),nullptr));void* indexData=nullptr;ok(budgetIndex->Lock(0,0,&indexData,0));for(unsigned i=0;i<vertices.size()/48;++i)static_cast<uint32_t*>(indexData)[i]=i;ok(budgetIndex->Unlock());ok(d->SetIndices(budgetIndex.p));
"""+text[where:]
        text=text.replace(',Vertices/3,false}};',',Vertices/3,true}};',1).replace('return d->DrawPrimitive(D3DPT_TRIANGLELIST,0,Vertices/3);','return d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,args.draw.minimum,Vertices,args.draw.start,Vertices/3);',1)
    if a.source_pattern!='repeat':
        text=text.replace('vertices(Vertices*48);','vertices(Vertices*48*3);',1).replace('for(unsigned i=0;i<Vertices;++i){auto* v=','for(unsigned i=0;i<Vertices*3;++i){auto* v=',1).replace('std::memcpy(v,positions[i%3],12);','std::memcpy(v,positions[i%3],12);const float z=float(i/Vertices)*.0005f;std::memcpy(v+8,&z,4);',1)
        index='(i/2)%3' if a.source_pattern=='pairs' else 'i%3'
        needle='ok(d->SetVertexShaderConstantF(0,bank[0],256));NorthlightAutoDisplacement::CaptureSource source;'
        text=text.replace(needle,'args.draw.start=Vertices*('+index+');args.draw.minimum='+('args.draw.start' if a.indexed else '0')+';'+needle,1)
        if not a.indexed:text=text.replace('return d->DrawPrimitive(D3DPT_TRIANGLELIST,0,Vertices/3);','return d->DrawPrimitive(D3DPT_TRIANGLELIST,args.draw.start,Vertices/3);',1)
    text=text.replace('{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()','{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,40,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()',1).replace('std::memcpy(v+20,normal,12);','std::memcpy(v+20,normal,12);const DWORD color=0xffe0d0c0;std::memcpy(v+40,&color,4);',1)
    # Interleave the real scene draw with captures; deferred replay runs later.
    native='d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,args.draw.minimum,Vertices,args.draw.start,Vertices/3)' if a.indexed else 'd->DrawPrimitive(D3DPT_TRIANGLELIST,args.draw.start,Vertices/3)'
    text=text.replace('                    expected+=!anchor','                    ok('+native+');\n                    expected+=!anchor',1)
    text=text.replace('queued+calls.submitted==Count+1-expected','queued+calls.submitted==2*(Count+1)-expected',1).replace('assert(calls.submitted==Count+1-expected);','assert(calls.submitted==2*(Count+1)-expected);',1)
    text=text.replace('std::vector<unsigned char> reference;','std::vector<unsigned char> reference,nativeReference;',1)
    text=text.replace('auto image=capturedPixels(normals.texture(),128,128);','auto nativeImage=pixels(color);assert(std::any_of(nativeImage.begin(),nativeImage.end(),[](unsigned char v){return v!=0;}));if(nativeReference.empty())nativeReference=nativeImage;else assert(nativeReference==nativeImage);auto image=capturedPixels(normals.texture(),128,128);',1)
    text=text.replace('bank[28][3]=1;','bank[28][3]=1;for(auto& c:bank[11])c=1;bank[6][0]=bank[7][1]=1;bank[12][2]=-1;bank[10][0]=.6f;bank[10][1]=.7f;bank[10][2]=.8f;for(unsigned c=0;c<3;++c)bank[28][c]=1;bank[30][1]=bank[30][2]=1;for(auto& c:bank[25])c=1;',1)
    where=text.index('        auto color=target(')
    text=text[:where]+"""        NorthlightDrawSnapshot::Ref<IDirect3DTexture9> nativeWhite;ok(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,nativeWhite.out(),nullptr));D3DLOCKED_RECT white{};ok(nativeWhite->LockRect(0,&white,nullptr,0));*static_cast<DWORD*>(white.pBits)=0xffb0a080;ok(nativeWhite->UnlockRect(0));ok(d->SetTexture(0,nativeWhite.p));const float nativeFog[]={.12f,.16f,.2f,1};ok(d->SetPixelShaderConstantF(2,nativeFog,1));
"""+text[where:]
    cpp=out/'source-budget-gpu.cpp';cpp.write_text(text,encoding='utf-8',newline='\n')
shader=out/'body-vs.bin';shutil.copyfile(client_fixtures.four_bone_body_vs3(),shader)
pixel=out/'native-ps.bin';assets=Assets()
try:pixel.write_bytes(next(code for index,_,code in variants(assets.read('shaders\\pixel\\ps_3_0\\combiners_opaque.bls')) if index==0))
finally:assets.close()
inputs={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.inl'),ROOT/'tests/model_normals_gpu.cpp',*sorted((ROOT/'tests').glob('model_normal_*.inc')),ROOT/'tests/material_pose_handoff_gpu.cpp',Path(__file__).resolve(),*overlay.glob('*.h'),cpp,shader,pixel]}
exe=out/'pose-handoff.exe';command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-return-type','-Wno-microsoft-exception-spec','-I',overlay,'-I'+str(ROOT/'tests'),*fp.test_include_flags(),cpp,fp.src('world_gi.cpp'),'-luser32','-luuid','-o',exe]
env={**fp.zig_env(),'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(out),'PYTHONUTF8':'1'}
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,command)),stdout=log,stderr=subprocess.STDOUT,check=True,env=env,timeout=300)
results=[]
for index,backend in enumerate(a.backend):
    processes();r=subprocess.run([str(exe),str(backend.resolve()),str(shader),str(pixel),*(['timing'] if a.benchmark else [])],env=env,cwd=ROOT,capture_output=True,timeout=180)
    (out/f'gpu-{index}.log').write_bytes(r.stdout+r.stderr);print((r.stdout+r.stderr).decode(errors='replace')[-2000:],flush=True);
    if a.budget_negative_control:
        assert r.returncode and b'actual<=pendingBytes' in r.stderr;print('SOURCE_BUDGET_NEGATIVE_CONTROL_PASS',flush=True)
    else:r.check_returncode();assert b'LOCAL_POSE_GPU_PASS' in r.stdout
    results.append({'backend':str(backend.resolve()),'sha256':hashlib.sha256(backend.read_bytes()).hexdigest()})
assert all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==h for f,h in inputs.items()),'Source changed during the oracle'
(out/'validation.json').write_text(json.dumps({'pass':True,'actual_producer_consumer':True,'timing':a.benchmark,'range_shortcut':a.range_shortcut,'source_budget':a.source_budget,'indexed':a.indexed,'source_pattern':a.source_pattern,'negative_budget_control':a.budget_negative_control,'residency':a.residency,'explicit_frame_clock':True,'concurrent_games':games,'gpu_timing_scope':'Full producer/capture/replay timestamp timeline, including CPU command-supply gaps and shared GPU contention.','inputs':inputs,'results':results,'command':list(map(str,command)),'scope':'Authored workload with forced budget refusal; not a gameplay performance measurement.'},indent=2),encoding='utf-8')
