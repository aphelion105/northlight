#define main unusedNormalMain
#include "model_normals_gpu.cpp"
#undef main
#include <chrono>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char*,...){}
#include "auto_bump_renderer.h"
unsigned northlightReadOnlyPolicy=0; // The test overlay alone adds this control.
struct MaterialCaptureStateTestAccess {
    static void warm(NorthlightAutoBump& b,IDirect3DTexture9* source){assert(b.normals.resolve(source,true));}
    static unsigned equal(const NorthlightAutoBump& b){return b.equalDepthQueued;}
    static unsigned queued(const NorthlightAutoBump& b){return b.queued;}
    static unsigned depthBarriers(const NorthlightAutoBump& b){return b.depthBarriers;}
    static unsigned textureBarriers(const NorthlightAutoBump& b){return b.textureBarriers;}
    static void failSnapshot(NorthlightAutoBump& b){assert(b.deferredSnapshots.configureBudget(1,0));}
    static unsigned refused(const NorthlightAutoBump& b){return b.deferredFallback;}
};
struct Calls:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    unsigned targets=0,draws=0,samplers=0,render=0;
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* s)override{++targets;return real->SetRenderTarget(n,s);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE p,INT b,UINT m,UINT v,UINT i,UINT n)override{++draws;return real->DrawIndexedPrimitive(p,b,m,v,i,n);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE p,UINT m,UINT v,UINT n,const void* i,D3DFORMAT f,const void* s,UINT stride)override{++draws;return real->DrawIndexedPrimitiveUP(p,m,v,n,i,f,s,stride);}
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD s,D3DSAMPLERSTATETYPE k,DWORD* v)override{++samplers;return real->GetSamplerState(s,k,v);}
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE k,DWORD* v)override{++render;return real->GetRenderState(k,v);}
};
static void waitQuery(IDirect3DQuery9* q){ok(q->Issue(D3DISSUE_END));auto at=GetTickCount();HRESULT h;while((h=q->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-at<30000);Sleep(0);}ok(h);}
struct Fixture {
    static constexpr unsigned W=321,H=201;
    Target scene,final;IDirect3DPixelShader9* consumer=nullptr;IDirect3DSurface9 *depth=nullptr,*msaaColor=nullptr;
    IDirect3DVertexShader9* vertex[2]={};IDirect3DPixelShader9* pixel=nullptr;
    IDirect3DVertexDeclaration9* declaration=nullptr;IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
    IDirect3DTexture9 *texture=nullptr,*unrelated=nullptr;
    std::vector<unsigned char> data;std::vector<WORD> indices;uint64_t revision=1;
    unsigned scenario;bool alpha,equal,blend,up,supported;
    Fixture(unsigned kind,const char* rigid,const char* skin,const char* ps,const char* consume,bool expectBatch):scenario(kind),alpha(kind!=10),equal(kind!=6),blend(kind==7),up(kind==3||kind==11),supported(expectBatch){
        final=target(W,H,D3DFMT_A16B16G16R16F,8);consumer=::ps(consume);
        scene=target(W,H,kind==14?D3DFMT_A16B16G16R16F:D3DFMT_A8R8G8B8,kind==14?8:4);
        const auto samples=kind==12?D3DMULTISAMPLE_2_SAMPLES:D3DMULTISAMPLE_NONE;
        ok(d->CreateDepthStencilSurface(W,H,kind==16?D3DFMT_D24X8:D3DFMT_D24S8,samples,0,kind==21,&depth,nullptr));
        if(samples)ok(d->CreateRenderTarget(W,H,scene.format,samples,0,FALSE,&msaaColor,nullptr));
        vertex[0]=vs(rigid);vertex[1]=vs(skin);pixel=::ps(ps);
        D3DVERTEXELEMENT9 e[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,40,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
        ok(d->CreateVertexDeclaration(e,&declaration));data.resize(12*48);indices.resize(12);geometry();
        ok(d->CreateVertexBuffer(UINT(data.size()),0,0,D3DPOOL_MANAGED,&vb,nullptr));ok(d->CreateIndexBuffer(24,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr));writeGeometry();
        ok(d->CreateTexture(32,32,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));ok(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&unrelated,nullptr));writeTexture(false);
    }
    IDirect3DVertexShader9* vsCurrent()const{return vertex[scenario==3||scenario==11];}
    void geometry(){
        const float xy[6][2]={{-.9f,-.9f},{.9f,-.9f},{-.9f,.9f},{-.9f,.9f},{.9f,-.9f},{.9f,.9f}};
        for(unsigned i=0;i<12;++i){auto* v=data.data()+48*i;const float z=i<6?(scenario==2?2.95f:3.f):(scenario==0?3.001f:scenario==2?3.01f:3.f);
            const float p[]={xy[i%6][0]*z,xy[i%6][1]*z,z},n[]={i<6?0.f:.3f,0,1},uv[]={(xy[i%6][0]+1)*.2f+(i<6?0:.6f),(xy[i%6][1]+1)*.5f};
            std::memcpy(v,p,12);v[12]=255;std::memcpy(v+20,n,12);std::memcpy(v+32,uv,8);const DWORD color=0xffffffff;std::memcpy(v+40,&color,4);indices[i]=WORD(i);
        }
    }
    void writeGeometry(){void* p=nullptr;ok(vb->Lock(0,0,&p,0));std::memcpy(p,data.data(),data.size());ok(vb->Unlock());ok(ib->Lock(0,0,&p,0));std::memcpy(p,indices.data(),24);ok(ib->Unlock());}
    void writeTexture(bool changed){D3DLOCKED_RECT r{};ok(texture->LockRect(0,&r,nullptr,0));for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){const unsigned a=(scenario==4||scenario==5)?126+(x%5):scenario==17?(x&1?255:0):192;
        reinterpret_cast<DWORD*>((char*)r.pBits+y*r.Pitch)[x]=D3DCOLOR_ARGB(a,(x*8+y*4+(changed?64:0))&255,(x*x+7*y)&255,(255-x*8)&255);}NorthlightMaterialTextures::cache().upload(texture,texture,32,32,0,D3DFMT_A8R8G8B8,r.pBits,size_t(r.Pitch)*32,r.Pitch,true,true,D3DPOOL_MANAGED);ok(texture->UnlockRect(0));}
    void bind(){
        ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetRenderTarget(0,msaaColor?msaaColor:scene.s));ok(d->SetDepthStencilSurface(depth));viewport(scene);
        ok(d->SetVertexDeclaration(declaration));ok(d->SetVertexShader(vsCurrent()));ok(d->SetPixelShader(pixel));ok(d->SetStreamSource(0,vb,0,48));ok(d->SetIndices(ib));::bind(0,texture);::bind(14,nullptr);
        for(auto k:{D3DRS_ALPHABLENDENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(k,FALSE));
        ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
        ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,alpha));ok(d->SetRenderState(D3DRS_ALPHAREF,127));ok(d->SetRenderState(D3DRS_ALPHAFUNC,scenario==5?D3DCMP_GREATEREQUAL:D3DCMP_GREATER));
        ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_CLIPPING,TRUE));ok(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));
        ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend));
        float bank[256][4]={};bank[2][0]=bank[3][1]=1;bank[4][2]=500.f/499.5f;bank[4][3]=-.5f*bank[4][2];bank[5][2]=1;bank[6][0]=bank[7][1]=1;bank[11][0]=.7f;bank[11][1]=.6f;bank[11][2]=.5f;bank[28][3]=1;
        for(unsigned b=0;b<75;++b)bank[31+b*3][0]=bank[32+b*3][1]=bank[33+b*3][2]=1;
        ok(d->SetVertexShaderConstantF(0,bank[0],256));float psBank[224][4];for(unsigned i=0;i<224;++i)for(unsigned c=0;c<4;++c)psBank[i][c]=i<3?0.f:float(i*4+c)*.001f;ok(d->SetPixelShaderConstantF(0,psBank[0],224));
    }
    void pose(unsigned draw){if(scenario!=20)return;const float z=draw&1?20.f:3.f;
        const float rows[3][4]={{z/3*.4f,0,0,(draw&1?.5f:-.5f)*z},{0,z/3*.8f,0,0},{0,0,1,z-3}};ok(d->SetVertexShaderConstantF(31,rows[0],3));}
    HRESULT draw(){return up?d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,12,4,indices.data(),D3DFMT_INDEX16,data.data(),48):d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,12,0,4);}
    ~Fixture(){drop(consumer);drop(final.read);drop(final.s);drop(final.t);drop(vb);drop(ib);drop(declaration);for(auto*& v:vertex)drop(v);drop(pixel);NorthlightMaterialTextures::cache().erase(texture,texture);drop(texture);drop(unrelated);drop(depth);drop(msaaColor);drop(scene.read);drop(scene.s);drop(scene.t);}
};
struct Result {std::vector<unsigned char> normal,occlusion,color,final;double cpu=0,gpu=0;unsigned queued=0,equal=0,targets=0,draws=0;};
// Production supplies known native raster/sampler state. Benchmark both policies
// with the same mirror; correctness scenarios also exercise the query fallback.
struct KnownState {
    inline static KnownState* active=nullptr; // One device, like the helpers' d.
    DWORD raster[256]={},sampler[16][14]={};
    void read(){for(unsigned i=0;i<256;++i)d->GetRenderState(D3DRENDERSTATETYPE(i),raster+i);for(unsigned s=0;s<16;++s)for(unsigned k=1;k<14;++k)ok(d->GetSamplerState(s,D3DSAMPLERSTATETYPE(k),sampler[s]+k));}
    static bool states(void* p,const D3DRENDERSTATETYPE* k,unsigned n,DWORD* v){assert(p==d&&active);auto& s=*active;for(unsigned i=0;i<n;++i){assert(unsigned(k[i])<256);v[i]=s.raster[k[i]];}return true;}
    static bool samplers(void* p,DWORD stage,unsigned mask,DWORD* v){assert(p==d&&active);auto& s=*active;assert(stage<16);for(unsigned i=0;i<13;++i)if(mask&(1u<<i))v[i]=s.sampler[stage][i+1];return true;}
};
static Result run(Calls& calls,Fixture& f,const std::vector<unsigned char>& vsBytes,const std::vector<unsigned char>& psBytes,unsigned phase,unsigned count,bool measure){
    NorthlightAutoBump bump(d);auto s=NorthlightQuality::Settings{};s.autoBump=1;s.autoBumpModels=f.scenario==15?2:1;s.autoBumpTerrain=s.autoBumpWalls=s.autoBumpGrass=0;s.autoBumpStrength=75;s.autoBumpDistance=f.scenario==2?3:48;s.autoCavity=f.scenario==20;s.autoParallaxShadows=0;s.autoParallaxDepth=0;s.autoModelDisplacementStrength=0;
    bump.configure(s);bump.setProjection(1,1,1);bump.setDeferredCapture(true,f.supported&&f.scenario!=22);bump.setTextureWriteGuard(f.scenario!=13);
    KnownState known;if(measure){KnownState::active=&known;bump.setRasterStateSource(d,&KnownState::states);bump.setSamplerStateSource(d,&KnownState::samplers);bump.setEmptyTargetSource(d,[](void* p){assert(p==d);return true;});}
    bump.setGeometryTracking([](void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));},[](void*,bool){return uint64_t(1);},nullptr);
    bump.registerVertex(f.vsCurrent(),NorthlightWaterShaderPatch::fnv1a64((const uint32_t*)vsBytes.data(),vsBytes.size()/4),(const DWORD*)vsBytes.data(),vsBytes.size()/4,3);bump.registerPixel(f.pixel,NorthlightWaterShaderPatch::fnv1a64((const uint32_t*)psBytes.data(),psBytes.size()/4));
    if(f.scenario==11)MaterialCaptureStateTestAccess::failSnapshot(bump);
    IDirect3DQuery9 *begin=nullptr,*end=nullptr,*frequency=nullptr,*event=nullptr,*disjoint=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));if(measure){ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));}
    Result r;
    // Prepare identical maps/resources on both policies. A cold run is checked
    // as well; measured frames always include the same fully active material.
    for(unsigned frame=0;frame<3;++frame){bump.endFrame();
        if(frame==1&&phase){bump.setMemoryPressurePolicy(true,true);bump.trimMemory(true);}if(frame==2&&phase==2)bump.setMemoryPressurePolicy(false,false);
        f.writeTexture(false);f.bind();ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
        ok(d->BeginScene());if(frame)MaterialCaptureStateTestAccess::warm(bump,f.texture);
        if(f.equal){ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,0));f.pose(0);ok(f.draw());if(f.scenario==20){f.pose(1);ok(f.draw());}ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));}
        if(measure)known.read();const auto beforeTargets=calls.targets,beforeDraws=calls.draws;float original[224][4];ok(d->GetPixelShaderConstantF(0,original[0],224));
        if(measure&&frame==2){ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));ok(begin->Issue(D3DISSUE_END));}
        auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<count;++i){
            if(i==count/2&&f.scenario==8){bump.beforeDepthChange();ok(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,0));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(f.draw());ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));}
            if(i==count/2&&f.scenario==9){bump.beforeTextureWrite(f.texture);f.writeTexture(true);}
            if(i==count/2&&f.scenario==18){auto before=MaterialCaptureStateTestAccess::queued(bump);bump.beforeTextureWrite(f.unrelated);assert(before==MaterialCaptureStateTestAccess::queued(bump));}
            if(f.scenario==10){ok(d->SetRenderState(D3DRS_ZFUNC,i%3==1?D3DCMP_LESSEQUAL:D3DCMP_EQUAL));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,i%3!=1));}
            f.pose(i);bump.beginDraw();NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,12,0,4,true},f.up?f.indices.data():nullptr,D3DFMT_INDEX16,f.up?f.data.data():nullptr,f.up?48u:0u};
            bump.capture(f.vsCurrent(),[&]{return f.draw();},f.up,&args);ok(bump.nativeDraw([&]{return f.draw();},f.vsCurrent(),f.pixel));
        }
        IDirect3DTexture9* released=nullptr;if(f.scenario==19){released=f.texture;ok(d->SetTexture(0,nullptr));f.texture=nullptr;released->Release();}
        assert(bump.finishCaptures());if(released){released->AddRef();f.texture=released;}auto stop=std::chrono::steady_clock::now();if(measure&&frame==2){ok(end->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));}ok(d->EndScene());waitQuery(event);
        r.cpu=std::chrono::duration<double,std::milli>(stop-start).count();r.targets=calls.targets-beforeTargets;r.draws=calls.draws-beforeDraws;r.queued=MaterialCaptureStateTestAccess::queued(bump);r.equal=MaterialCaptureStateTestAccess::equal(bump);
        float after[224][4];ok(d->GetPixelShaderConstantF(0,after[0],224));if(std::memcmp(original,after,sizeof original))for(unsigned i=0;i<224;++i)if(std::memcmp(original[i],after[i],16))std::printf("CONSTANT_MISMATCH scenario=%u phase=%u frame=%u policy=%u c%u before=%g,%g,%g,%g after=%g,%g,%g,%g\n",f.scenario,phase,frame,northlightReadOnlyPolicy,i,original[i][0],original[i][1],original[i][2],original[i][3],after[i][0],after[i][1],after[i][2],after[i][3]);assert(!std::memcmp(original,after,sizeof original));
        IDirect3DSurface9 *rt=nullptr,*ds=nullptr;ok(d->GetRenderTarget(0,&rt));ok(d->GetDepthStencilSurface(&ds));assert(rt==(f.msaaColor?f.msaaColor:f.scene.s)&&ds==f.depth);drop(rt);drop(ds);
        if(measure&&frame==2){UINT64 a=0,b=0,hz=0;BOOL invalid=TRUE;ok(begin->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(frequency->GetData(&hz,sizeof hz,0));ok(disjoint->GetData(&invalid,sizeof invalid,0));assert(!invalid&&hz&&b>=a);r.gpu=double(b-a)*1000/hz;}
        assert(bump.texture());auto normal=capturedPixels(bump.texture(),Fixture::W,Fixture::H);if(frame)assert(std::any_of(normal.begin(),normal.end(),[](unsigned char v){return v!=0;}));
        std::vector<unsigned char> occlusion;if(auto* t=bump.occlusionTexture()){Target target;target.t=t;target.w=Fixture::W;target.h=Fixture::H;target.bytes=4;target.format=D3DFMT_A8R8G8B8;ok(t->GetSurfaceLevel(0,&target.s));ok(d->CreateOffscreenPlainSurface(target.w,target.h,target.format,D3DPOOL_SYSTEMMEM,&target.read,nullptr));occlusion=pixels(target);drop(target.read);drop(target.s);}
        if(f.msaaColor)ok(d->StretchRect(f.msaaColor,nullptr,f.scene.s,nullptr,D3DTEXF_NONE));auto color=pixels(f.scene);
        std::vector<unsigned char> final;
        {SavedState saved(d);assert(saved.ok);ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));
            ok(d->SetRenderTarget(0,f.final.s));ok(d->SetPixelShader(f.consumer));::bind(0,f.scene.t);::bind(9,bump.texture());
            ok(d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));ok(d->SetSamplerState(9,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
            ok(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));ok(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
            for(auto k:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(k,FALSE));
            const float control[]={.75f,48,0,0};ok(d->SetPixelShaderConstantF(73,control,1));quad(f.final);final=pixels(f.final);assert(saved.restore());}
        // Keep every warm-up/cold frame in the exact oracle, not just the last.
        r.normal.insert(r.normal.end(),normal.begin(),normal.end());r.occlusion.insert(r.occlusion.end(),occlusion.begin(),occlusion.end());r.color.insert(r.color.end(),color.begin(),color.end());r.final.insert(r.final.end(),final.begin(),final.end());
        if(f.scenario==11&&northlightReadOnlyPolicy==2&&f.supported)assert(!r.equal&&MaterialCaptureStateTestAccess::refused(bump)>0);
        if(northlightReadOnlyPolicy==2&&f.supported&&f.equal&&!f.blend&&f.scenario!=11&&f.scenario!=12&&f.scenario!=13&&f.scenario!=15&&f.scenario!=16&&f.scenario!=22)assert(r.equal>0);
        if(f.scenario==6||f.scenario==7||f.scenario==13||f.scenario==15||f.scenario==16||f.scenario==22)assert(!r.equal);
        if(f.scenario==8&&r.equal)assert(MaterialCaptureStateTestAccess::depthBarriers(bump)>0);
        if(f.scenario==9&&r.equal)assert(MaterialCaptureStateTestAccess::textureBarriers(bump)>0);
    }
    for(auto* q:{begin,end,frequency,event,disjoint})if(q)q->Release();return r;
}
int main(int argc,char** argv){assert(argc==6||argc==7);const bool negative=argc==7&&std::strcmp(argv[6],"negative")==0;const bool benchmark=argc==7&&!negative;const bool expectBatch=std::strstr(argv[1],"dxvk")!=nullptr;std::setvbuf(stdout,nullptr,_IONBF,0);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);
    D3DDISPLAYMODE display{};ok(api->GetAdapterDisplayMode(0,&display));if(FAILED(api->CheckDeviceFormat(0,D3DDEVTYPE_HAL,display.Format,D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_TEXTURE,D3DFORMAT(MAKEFOURCC('D','F','2','4'))))){std::puts("MATERIAL_EQUAL_DEPTH_UNSUPPORTED_DF24: production seed cannot run; unchanged immediate fallback");api->Release();FreeLibrary(module);DestroyWindow(window);return 77;}
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    {Calls calls(raw);d=&calls;NorthlightMaterialTextures::enabled.store(true);auto rigid=read(argv[2]),skin=read(argv[3]),pixel=read(argv[4]);unsigned checked=0;
        for(unsigned scenario=0;scenario<(benchmark||negative?1:23);++scenario){Fixture fixture(scenario,argv[2],argv[3],argv[4],argv[5],expectBatch);
            if(negative)fixture.equal=false;
            for(unsigned phase=0;phase<(negative?1u:3u);++phase)for(unsigned round=0;round<(benchmark?20u:1u);++round){Result results[2];for(unsigned turn=0;turn<2;++turn){const unsigned current=(turn+round)&1;northlightReadOnlyPolicy=current?(negative?1:2):0;results[current]=run(calls,fixture,scenario==3||scenario==11?skin:rigid,pixel,phase,benchmark?128u:8u,benchmark);}
                const bool same=results[0].normal==results[1].normal&&results[0].occlusion==results[1].occlusion&&results[0].color==results[1].color&&results[0].final==results[1].final;
                if(!same)std::printf("MATERIAL_EQUAL_MISMATCH scenario=%u phase=%u round=%u\n",scenario,phase,round);assert(same);
                for(unsigned current=0;current<2;++current){const auto& r=results[current];std::printf("EQUAL_FRAME scenario=%u phase=%u round=%u current=%u queued=%u equalQueued=%u targets=%u draws=%u cpuMs=%.6f gpuMs=%.6f\n",scenario,phase,round,current,r.queued,r.equal,r.targets,r.draws,r.cpu,r.gpu);}
                ++checked;
            }
        }
        std::printf("MATERIAL_EQUAL_DEPTH_PASS comparisons=%u warmupColdIncluded=1 targetsColorConstants=exact\n",checked);
    }
    raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
