// Controlled Weather base capture workload, without AutoBump or a material handoff.
#define main unused_normal_fixture_main
#include "model_normals_gpu.cpp"
#undef main
#include "base_capture_probe.h"
#ifdef NORTHLIGHT_MODEL_NORMALS_RANGE_REFERENCE
#include "range_reference_model.h"
#endif
#include <chrono>
NorthlightCapturePhases::Stats<8> baseCapture,baseRecord,baseFlush;
bool baseProbe=false;
static double captureTicks(const NorthlightCapturePhases::Stats<8>& s){LARGE_INTEGER hz;QueryPerformanceFrequency(&hz);long long total=0;for(auto t:s.ticks)total+=t;return double(total)*1000/hz.QuadPart;}
static void capturePrint(const char* group,const NorthlightCapturePhases::Stats<8>& s){LARGE_INTEGER hz;QueryPerformanceFrequency(&hz);for(unsigned i=0;i<8;++i)if(s.ticks[i])std::printf("CAPTURE_PHASE group=%s phase=%u ms=%.9f\n",group,i,double(s.ticks[i])*1000/hz.QuadPart);}
struct BaseCaptureCalls:CaptureCalls {
    using CaptureCalls::CaptureCalls;unsigned submissions=0,stateReads=0;
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** out)override{++stateReads;return real->GetVertexShader(out);}
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** out)override{++stateReads;return real->GetPixelShader(out);}
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT first,float* out,UINT count)override{++stateReads;return real->GetPixelShaderConstantF(first,out,count);}
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE key,DWORD* out)override{++stateReads;return real->GetRenderState(key,out);}
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream,IDirect3DVertexBuffer9** out,UINT* offset,UINT* stride)override{++stateReads;return real->GetStreamSource(stream,out,offset,stride);}
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** out)override{++stateReads;return real->GetIndices(out);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT b,UINT m,UINT n,UINT s,UINT c)override{++submissions;return real->DrawIndexedPrimitive(t,b,m,n,s,c);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT m,UINT n,UINT c,const void* i,D3DFORMAT f,const void* v,UINT s)override{++submissions;return real->DrawIndexedPrimitiveUP(t,m,n,c,i,f,v,s);}
};
int main(int argc,char** argv){
    assert(argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);const std::string dir=std::string(argv[2])+"/";
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    BaseCaptureCalls calls(raw);DeviceMirror mirror;ExtensionDevice extension(&calls,&mirror);MirrorGuard held(mirror.gate);d=&extension;
    const auto mesh=read(dir+"lamp.mesh");unsigned nv,ni;std::memcpy(&nv,mesh.data()+4,4);std::memcpy(&ni,mesh.data()+8,4);assert(mesh.size()==12+nv*48+ni*4);
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;void* memory=nullptr;
    ok(d->CreateVertexBuffer(nv*48,0,0,D3DPOOL_MANAGED,&vb,nullptr));ok(vb->Lock(0,0,&memory,0));std::memcpy(memory,mesh.data()+12,nv*48);ok(vb->Unlock());
    ok(d->CreateIndexBuffer(ni*4,0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib,nullptr));ok(ib->Lock(0,0,&memory,0));std::memcpy(memory,mesh.data()+12+nv*48,ni*4);ok(ib->Unlock());
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* declaration=nullptr;ok(d->CreateVertexDeclaration(elements,&declaration));auto* shader=vs(dir+"model-vs3-rigid.bin");auto* pixel=ps(dir+"ModelPS.bin");
    IDirect3DTexture9* white=nullptr;ok(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&white,nullptr));D3DLOCKED_RECT lock;ok(white->LockRect(0,&lock,nullptr,0));*static_cast<DWORD*>(lock.pBits)=0xffffffff;ok(white->UnlockRect(0));
    constexpr unsigned W=3840,H=2160,Near=133,Far=310;
    IDirect3DSurface9 *color=nullptr,*depth=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&color,nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&depth,nullptr));
    NorthlightDrawSnapshot::Frame peer{8u<<20,0};const auto identity=+[](void* p,bool)->uint64_t{return reinterpret_cast<uintptr_t>(p);};const auto version=+[](void*,bool)->uint64_t{return 1;};peer.setIdentityProvider(identity);peer.setVersionProvider(version);peer.setSnapshotCacheLimit(8u<<20);
    NorthlightGpuScene::NativeStore store;NorthlightGpuScene::NativeClient cache(store,NorthlightGpuScene::Consumer::Material);auto normals=std::make_unique<NorthlightModelNormals>(d);
    assert(normals->shareCaptureSources(peer,identity,version,nullptr));normals->setGeometryCache(&cache);normals->setDeferredCapture(true);normals->setTrustedPoseSource(extension.trustedConstantSource());
    normals->setRasterStateSource(d,[](void* c,const D3DRENDERSTATETYPE* k,unsigned n,DWORD* v){return static_cast<ExtensionDevice*>(static_cast<IDirect3DDevice9*>(c))->knownRenderStates(k,n,v);});
#ifdef NORTHLIGHT_MODEL_NORMALS_RANGE_REFERENCE
    auto before=std::make_unique<NorthlightModelNormalsReference>(d);
    assert(before->shareCaptureSources(peer,identity,version,nullptr));before->setGeometryCache(&cache);before->setDeferredCapture(true);before->setTrustedPoseSource(extension.trustedConstantSource());
    before->setRasterStateSource(d,[](void* c,const D3DRENDERSTATETYPE* k,unsigned n,DWORD* v){return static_cast<ExtensionDevice*>(static_cast<IDirect3DDevice9*>(c))->knownRenderStates(k,n,v);});
#endif
    IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));
    auto wait=[](IDirect3DQuery9* q){UINT64 v=0;const auto at=GetTickCount();HRESULT hr;while((hr=q->GetData(&v,sizeof v,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-at<10000);SwitchToThread();}ok(hr);return v;};
    const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,nv,0,ni/3,true};auto draw=[&]{return d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,nv,0,ni/3);};
    std::vector<unsigned char> reference,stateReference;using Clock=std::chrono::steady_clock;
    const auto runFrame=[&](auto& normals,unsigned frame,unsigned mode,unsigned block){
        normals->endFrame();peer.clearFrame();store.advance(frame+1);baseCapture.clear();baseRecord.clear();baseFlush.clear();baseProbe=frame%2==0;
#ifdef NORTHLIGHT_MODEL_NORMALS_RANGE_REFERENCE
        baseProbe=false;
#endif
        ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,color));ok(d->SetDepthStencilSurface(depth));D3DVIEWPORT9 vp{0,0,W,H,0,1};ok(d->SetViewport(&vp));
        ok(d->SetVertexDeclaration(declaration));ok(d->SetStreamSource(0,vb,0,48));ok(d->SetIndices(ib));ok(d->SetVertexShader(shader));ok(d->SetPixelShader(pixel));bind(0,white);
        for(auto k:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(k,FALSE));
        ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
        float bank[256][4]={};bank[2][0]=bank[3][1]=bank[4][2]=bank[5][2]=1;bank[4][3]=-.5f;bank[28][3]=1;for(unsigned bone=0;bone<75;++bone)bank[31+bone*3][0]=bank[32+bone*3][1]=bank[33+bone*3][2]=1;ok(d->SetVertexShaderConstantF(0,bank[0],256));float pixelBank[224][4]={};ok(d->SetPixelShaderConstantF(0,pixelBank[0],224));
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff204080,1,0));ok(d->BeginScene());
        double captureMs=0,nativeMs=0;const auto begin=Clock::now();
        for(unsigned i=0;i<Near+Far;++i){float pose[3][4]={{1,0,0,float(int(i%13)-6)*.6f},{0,1,0,-4},{0,0,1,i<Near?15.f:80.f}};ok(d->SetVertexShaderConstantF(31,pose[0],3));
            auto at=Clock::now();normals->capture(shader,draw,false,&geometry);captureMs+=std::chrono::duration<double,std::milli>(Clock::now()-at).count();
            at=Clock::now();ok(draw());nativeMs+=std::chrono::duration<double,std::milli>(Clock::now()-at).count();
        }
        const auto queued=normals->testPendingPackets(),culled=normals->testRangeCulled();
        // The bounded range proof may decline during cold shader compilation.
        // Such draws retain their ordinary capture, rather than losing geometry.
        assert(queued>=Near&&queued+culled==Near+Far);
        calls.submissions=0;const auto readsBefore=calls.stateReads;ok(frequency->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));const auto flushBegin=Clock::now();
        // Match private extension replay: invalidate the mirror around save/replay/restore.
        {ExtensionDevice::RawScope privateWork(extension);assert(normals->finishCaptures());}
        const auto finish=Clock::now();const auto stateReads=calls.stateReads-readsBefore;ok(end->Issue(D3DISSUE_END));ok(d->EndScene());const auto a=wait(start),b=wait(end),hz=wait(frequency);
        const double gpu=double(b-a)*1000/hz,flushMs=std::chrono::duration<double,std::milli>(finish-flushBegin).count(),totalMs=std::chrono::duration<double,std::milli>(finish-begin).count();
        baseProbe=false;
        if(frame==0||frame==16||frame==17||frame%32==31){auto image=capturedPixels(normals->texture(),W,H);assert(std::any_of(image.begin(),image.end(),[](unsigned char v){return v!=0;}));
            if(reference.empty())reference=std::move(image);else assert(image==reference);
            // Both replay instances are warm before comparing backend banks.
            // Cold native shader creation may initialize previously unused rows.
            if(frame>=128){float vertex[1024],pixel[896];ok(raw->GetVertexShaderConstantF(0,vertex,256));ok(raw->GetPixelShaderConstantF(0,pixel,224));
                std::vector<unsigned char> state;const auto* v=reinterpret_cast<const unsigned char*>(vertex);const auto* p=reinterpret_cast<const unsigned char*>(pixel);state.insert(state.end(),v,v+sizeof vertex);state.insert(state.end(),p,p+sizeof pixel);
                if(stateReference.empty())stateReference=std::move(state);else assert(state==stateReference);}}
        if(frame>=16)assert(normals->readSourceBytes()==0&&normals->cachedDraws()==queued);
#ifdef NORTHLIGHT_MODEL_NORMALS_RANGE_REFERENCE
        std::printf("CAPTURE_COMPARE_FRAME mode=%u block=%u warm=%u captureMs=%.9f flushCpuMs=%.9f replayGpuMs=%.9f nativeSubmitMs=%.9f totalCpuMs=%.9f queued=%zu culled=%u sourceBytes=%zu cached=%u submitted=%u stateReads=%u\n",mode,block,unsigned(block<4),captureMs,flushMs,gpu,nativeMs,totalMs,queued,culled,normals->readSourceBytes(),normals->cachedDraws(),calls.submissions,stateReads);
#else
        std::printf("CAPTURE_FRAME frame=%u warm=%u profiled=%u captureMs=%.9f flushCpuMs=%.9f replayGpuMs=%.9f nativeSubmitMs=%.9f totalCpuMs=%.9f queued=%zu culled=%u sourceBytes=%zu cached=%u stateReads=%u\n",frame,unsigned(frame<16),unsigned(frame%2==0),captureMs,flushMs,gpu,nativeMs,totalMs,queued,culled,normals->readSourceBytes(),normals->cachedDraws(),stateReads);
        if(frame%2==0){capturePrint("capture_inclusive",baseCapture);capturePrint("record",baseRecord);capturePrint("flush",baseFlush);}
#endif
    };
#ifdef NORTHLIGHT_MODEL_NORMALS_RANGE_REFERENCE
    for(unsigned block=0;block<16;++block){const unsigned mode=(block/2)%2?1-block%2:block%2;
        for(unsigned i=0;i<32;++i){if(mode)runFrame(normals,block*32+i,mode,block);else runFrame(before,block*32+i,mode,block);}}
    before.reset();
#else
    for(unsigned frame=0;frame<40;++frame)runFrame(normals,frame,0,0);
#endif
    normals.reset();cache.clear();white->Release();shader->Release();pixel->Release();declaration->Release();vb->Release();ib->Release();start->Release();end->Release();frequency->Release();color->Release();depth->Release();d=nullptr;raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);std::puts("BASE_CAPTURE_PROFILE_PASS identicalProfiledPixels=1");
}
