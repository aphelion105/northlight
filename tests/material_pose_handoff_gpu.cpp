// Actual material prepare -> receiver capture, with the shared envelope budget exhausted.
#define main unused_model_normal_main
#include "model_normals_gpu.cpp"
#undef main
#include "device_mirror.h"
#include <cstdarg>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* format,...){va_list args;va_start(args,format);std::vprintf(format,args);std::puts("");va_end(args);}
#include "auto_displacement_renderer.h"
#include <chrono>
bool northlightLocalPoseHandoff=true; // Only the test overlay adds a policy control.
struct DisplacementStateTestAccess {
    static void frame(NorthlightAutoDisplacement& p,uint64_t frame){p.gpuScene->setFrame(frame);}
    static void refuseShared(NorthlightAutoDisplacement& p){p.envelopeBudget.maxOperations=0;p.poseEnvelopes.chargeCheapPreparation(UINT64_MAX);}
    static bool registered(const NorthlightAutoDisplacement& p,IDirect3DVertexShader9* s){return p.materials.count(s)&&p.materials.at(s).envelopeSkin&&p.materials.at(s).rangeKind==NorthlightModelNormalRange::Envelope;}
    static unsigned scan(const NorthlightAutoDisplacement& p){return p.poseVerticesThisFrame;}
    static auto& sources(NorthlightAutoDisplacement& p){return p.snapshots;}
    static auto* geometry(NorthlightAutoDisplacement& p){return &p.gpuScene->materials;}
};
struct PoseCalls:CaptureCalls {
    using CaptureCalls::CaptureCalls;unsigned submitted=0;
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT stride)override{++submitted;return real->DrawPrimitiveUP(t,n,p,stride);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT m,UINT v,UINT n,const void* i,D3DFORMAT f,const void* p,UINT stride)override{++submitted;return real->DrawIndexedPrimitiveUP(t,m,v,n,i,f,p,stride);}
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE t,UINT first,UINT n)override{++submitted;return real->DrawPrimitive(t,first,n);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT b,UINT m,UINT v,UINT first,UINT n)override{++submitted;return real->DrawIndexedPrimitive(t,b,m,v,first,n);}
};
static void waitEvent(IDirect3DQuery9* q){ok(q->Issue(D3DISSUE_END));DWORD start=GetTickCount();HRESULT h;while((h=q->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-start<30000);Sleep(0);}ok(h);}
int main(int argc,char** argv){assert(argc==4||argc==5);const bool timing=argc==5;std::setvbuf(stdout,nullptr,_IONBF,0);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto* api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=128;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    {PoseCalls calls(raw);DeviceMirror mirror;ExtensionDevice extension(&calls,&mirror);MirrorGuard held(mirror.gate);d=&extension;
        auto words=read(argv[2]);auto* shader=vs(argv[2]);auto* pixel=ps(argv[3]);
        D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};IDirect3DVertexDeclaration9* declaration=nullptr;ok(d->CreateVertexDeclaration(elements,&declaration));
        constexpr unsigned Vertices=1536;std::vector<unsigned char> vertices(Vertices*48);const float positions[3][3]={{-.3f,-.3f,0},{.3f,-.3f,0},{0,.3f,0}},normal[]={0.f,0.f,1.f};
        for(unsigned i=0;i<Vertices;++i){auto* v=vertices.data()+i*48;std::memcpy(v,positions[i%3],12);v[12]=128;v[13]=64;v[14]=32;v[15]=31;v[17]=1;v[18]=2;v[19]=3;std::memcpy(v+20,normal,12);}
        IDirect3DVertexBuffer9* vb=nullptr;void* memory=nullptr;ok(d->CreateVertexBuffer(UINT(vertices.size()),0,0,D3DPOOL_MANAGED,&vb,nullptr));ok(vb->Lock(0,0,&memory,0));std::memcpy(memory,vertices.data(),vertices.size());ok(vb->Unlock());ok(d->SetStreamSource(0,vb,0,48));ok(d->SetVertexDeclaration(declaration));ok(d->SetVertexShader(shader));ok(d->SetPixelShader(pixel));
        auto color=target(128,128,D3DFMT_A8R8G8B8,4);IDirect3DSurface9* depth=nullptr;ok(d->CreateDepthStencilSurface(128,128,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,color.s));ok(d->SetDepthStencilSurface(depth));viewport(color);
        for(auto k:{D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(k,FALSE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_CLIPPING,TRUE));ok(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));
        NorthlightAutoDisplacement producer(d);producer.setTrustedConstantEpochSource(extension.trustedConstantSource());producer.registerMaterial(shader,reinterpret_cast<const DWORD*>(words.data()),words.size()/4,4);assert(DisplacementStateTestAccess::registered(producer,shader));
        auto identity=+[](void* p,bool)->uint64_t{return reinterpret_cast<uintptr_t>(p);};auto version=+[](void*,bool)->uint64_t{return 1;};auto& sources=DisplacementStateTestAccess::sources(producer);sources.setIdentityProvider(identity);sources.setVersionProvider(version);sources.setSnapshotCacheLimit(16u<<20);
        NorthlightModelNormals normals(d);normals.setDeferredCapture(true);normals.setTrustedPoseSource(extension.trustedConstantSource());normals.setGeometryCache(DisplacementStateTestAccess::geometry(producer));assert(normals.shareCaptureSources(sources,identity,version,nullptr));
        NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,Vertices,0,Vertices/3,false}};
        IDirect3DQuery9 *event=nullptr,*begin=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));
        if(timing){ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));}
        unsigned cases=0;uint64_t frame=0;for(unsigned shape=0;shape<8;++shape){std::vector<unsigned char> reference;
            const unsigned rounds=timing?24:2,Count=timing?128:16;
            for(unsigned round=0;round<rounds;++round)for(unsigned turn=0;turn<2;++turn){const bool current=((round+turn)&1)!=0;northlightLocalPoseHandoff=current;producer.endFrame();DisplacementStateTestAccess::frame(producer,++frame);DisplacementStateTestAccess::refuseShared(producer);normals.endFrame();calls.submitted=0;
                ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));float bank[256][4]={};bank[2][0]=bank[3][1]=1;bank[4][2]=bank[5][2]=shape>=6?-1.f:1.f;bank[4][3]=-.5f;bank[28][3]=1;
                unsigned expected=0,proofs=0;ok(d->BeginScene());UINT64 hz=0,a=0,b=0;BOOL invalid=FALSE;
                if(timing){ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));ok(begin->Issue(D3DISSUE_END));}
                const auto start=std::chrono::steady_clock::now();
                for(unsigned i=0;i<=Count;++i){const bool anchor=i==0,isFar=!anchor&&(shape==1||shape==2&&i%2==0||shape==4||shape==6),offscreen=!anchor&&shape==5;const float z=(anchor?2.f:isFar?80.f:shape==3||shape==7?48.f:20.f)*(shape>=6?-1.f:1.f);
                    for(unsigned b=0;b<75;++b){const unsigned row=31+3*b;bank[row][0]=bank[row+1][1]=z;bank[row+2][2]=1;bank[row][3]=z*((anchor?-.5f:offscreen?4.f:.5f)+i*.001f);bank[row+1][3]=0;bank[row+2][3]=z;}
                    ok(d->SetVertexShaderConstantF(0,bank[0],256));NorthlightAutoDisplacement::CaptureSource source;producer.prepare(shader,4,1,args,nullptr,&source);assert(source.mesh&&source.declaration);
                    proofs+=source.pose.outsideRange||source.pose.outsideViewport;
                    if(shape==4&&!anchor){for(unsigned b=0;b<75;++b)bank[33+3*b][3]=20;ok(d->SetVertexShaderConstantF(0,bank[0],256));}
                    normals.capture(shader,[&]{return d->DrawPrimitive(D3DPT_TRIANGLELIST,0,Vertices/3);},false,&args.draw,nullptr,D3DFMT_UNKNOWN,nullptr,0,&source.mesh,source.declaration,&source.pose);
                    expected+=current&&!anchor&&(shape==1||shape==2&&i%2==0||shape==6);
                }
                const auto queued=normals.testPendingPackets();assert(normals.testRangeCulled()==expected&&queued+calls.submitted==Count+1-expected);
                const auto captured=std::chrono::steady_clock::now();
                assert(normals.finishCaptures());if(timing){ok(end->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));}const auto submitted=std::chrono::steady_clock::now();ok(d->EndScene());waitEvent(event);
                if(timing){ok(frequency->GetData(&hz,sizeof hz,0));ok(begin->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(disjoint->GetData(&invalid,sizeof invalid,0));assert(!invalid&&hz&&b>=a);}
                auto image=capturedPixels(normals.texture(),128,128);if(reference.empty())reference=image;else assert(reference==image);assert(calls.submitted==Count+1-expected);
                if(round>=1)assert(!normals.readSourceBytes()&&!DisplacementStateTestAccess::scan(producer));
                std::printf("LOCAL_POSE_FRAME shape=%u round=%u current=%u candidates=%u proofs=%u culled=%u queued=%zu submitted=%u captureMs=%.6f totalCpuMs=%.6f gpuMs=%.6f\n",shape,round,unsigned(current),Count,proofs,expected,queued,calls.submitted,std::chrono::duration<double,std::milli>(captured-start).count(),std::chrono::duration<double,std::milli>(submitted-start).count(),timing?double(b-a)*1000/hz:0);++cases;
            }
        }
        for(auto* q:{event,begin,end,frequency,disjoint})if(q)q->Release();vb->Release();declaration->Release();pixel->Release();shader->Release();depth->Release();color.read->Release();color.s->Release();color.t->Release();std::printf("LOCAL_POSE_GPU_PASS cases=%u actualProducerConsumer=1 forcedRefusal=1 nearBoundaryStaleRetained=1 exactPixels=1\n",cases);
    }
    raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
