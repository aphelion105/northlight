// Read the exact packed prototype maps on real D3D9 devices; no game window.
#define main compositeFixtureMain
#include "composite_distance_gpu.cpp"
#undef main
#include "probe_texture_upload.h"
#include <array>
using namespace NorthlightGI;
namespace V=LocalProbeVisibility;
static void quad(WorldScene& s,Vec3 c,Vec3 u,Vec3 v){
    const uint32_t b=uint32_t(s.vertices.size());const auto n=normalized(cross(u,v));
    for(auto p:{c-u-v,c+u-v,c+u+v,c-u+v})s.vertices.push_back({p,n,0,0});
    s.triangles.push_back({b,b+1,b+2,0});s.triangles.push_back({b,b+2,b+3,0});
}
static std::shared_ptr<const ProbeLocalVisibility> map(WorldScene s){
    s.materials.emplace_back();BVH b;std::string error;if(!b.build(std::move(s),error))std::abort();return V::build(b,{});
}
static IDirect3DPixelShader9* shader(IDirect3DDevice9* d,const std::string& path){
    const auto code=read(path);IDirect3DPixelShader9* p=nullptr;ok(d->CreatePixelShader((const DWORD*)code.data(),&p));return p;
}
int main(int argc,char** argv){
    assert(argc==5);const bool timing=std::atoi(argv[4]);const unsigned W=timing?1280:257,H=timing?720:129;
    auto window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);assert(api);
    D3DADAPTER_IDENTIFIER9 adapter{};ok(api->GetAdapterIdentifier(0,0,&adapter));printf("DEVICE %s\n",adapter.Description);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_ALPHABLENDENABLE})ok(d->SetRenderState(s,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    for(unsigned i=0;i<16;++i){for(auto s:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(i,s,D3DTEXF_POINT));
        ok(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto s:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(i,s,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE));}
    std::vector<std::shared_ptr<const ProbeLocalVisibility>> maps;
    WorldScene s;quad(s,{2,0,0},{0,32,0},{0,0,32});maps.push_back(map(std::move(s)));
    s={};quad(s,{0,0,-1},{32,0,0},{0,32,0});maps.push_back(map(std::move(s)));
    s={};quad(s,{2,0,8.15f},{0,32,0},{0,0,7.85f});quad(s,{2,0,-8.15f},{0,32,0},{0,0,7.85f});
    quad(s,{2,8.15f,0},{0,7.85f,0},{0,0,.3f});quad(s,{2,-8.15f,0},{0,7.85f,0},{0,0,.3f});maps.push_back(map(std::move(s)));
    auto clear=std::make_shared<ProbeLocalVisibility>();V::uniformTiles(*clear);maps.push_back(clear);
    const Vec3 n=normalized({1,1,.2f}),u=normalized(cross(n,{0,0,1}))*32,v=normalized(cross(n,u))*32;
    s={};quad(s,n*2,u,v);maps.push_back(map(std::move(s)));
    auto missing=std::make_shared<ProbeLocalVisibility>();for(auto& p:missing->texels)p[3]=V::half(-1);maps.push_back(missing);
    const auto managedAtlas=[&](unsigned n){
        const auto size=V::atlas(n);std::vector<ProbeAtlasEntry> entries(size_t(n)*n*n);
        for(size_t i=0;i<entries.size();++i){auto& e=entries[i];e.occupied=e.probe.valid=true;e.probe.localVisibility=maps[i%maps.size()];}
        IDirect3DTexture9* t=nullptr;ok(d->CreateTexture(size.width,size.height,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&t,nullptr));
        NorthlightProbeUpload::LocalVisibilityMirror mirror;assert(mirror.prepare(entries.size()));D3DLOCKED_RECT locked{};
        ok(t->LockRect(0,&locked,nullptr,D3DLOCK_NO_DIRTY_UPDATE));const auto dirty=mirror.write(locked.pBits,size_t(locked.Pitch),n,entries);ok(t->UnlockRect(0));
        RECT rect={LONG(dirty.left),LONG(dirty.top),LONG(dirty.right),LONG(dirty.bottom)};ok(t->AddDirtyRect(&rect));assert(!mirror.changed(entries));return t;
    };
    const auto dimensions=V::atlas(4);
    auto local=managedAtlas(4);ok(d->SetTexture(13,local));
    const float info[]={float(dimensions.columns),1.f/dimensions.width,1.f/dimensions.height,1.f/dimensions.columns};ok(d->SetPixelShaderConstantF(82,info,1));
    Image query{W,H,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(size_t(W)*H*16)};std::vector<float> expected(size_t(W)*H);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const unsigned index=(x+y*W)%64;const Vec3 delta={-12.f+24.f*x/(W-1),-12.f+24.f*y/(H-1),float(int((x*17+y*23)%25)-12)};
        const std::array<float,4> p={delta.x,delta.y,delta.z,float(index)};std::memcpy(query.pixels.data()+(size_t(y)*W+x)*16,p.data(),16);
        expected[size_t(y)*W+x]=index%maps.size()==5?0:V::visibility(*maps[index%maps.size()],delta);}
    auto queries=texture(d,query);ok(d->SetTexture(1,queries));
    auto a=shader(d,std::string(argv[2])+"/VisibilityQuery.bin"),b=shader(d,std::string(argv[2])+"/VisibilityFour.bin");
    IDirect3DSurface9 *target=nullptr,*back=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&back,nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp{0,0,W,H,0,1};ok(d->SetViewport(&vp));
    const Vertex vertices[]={{-.5f,-.5f,0,1,0,0},{float(W)-.5f,-.5f,0,1,1,0},{-.5f,float(H)-.5f,0,1,0,1},{float(W)-.5f,float(H)-.5f,0,1,1,1}};
    IDirect3DQuery9 *start=nullptr,*end=nullptr,*freq=nullptr,*disjoint=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    const auto get=[&](IDirect3DQuery9* q,void* p,unsigned bytes){HRESULT hr;while((hr=q->GetData(p,bytes,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(hr);};
    float maxError=0;
    for(unsigned pass=0;pass<(timing?32u:2u);++pass){const bool four=timing?(pass%4==1||pass%4==2):pass==1;
        ok(disjoint->Issue(D3DISSUE_BEGIN));ok(freq->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));ok(d->BeginScene());ok(d->SetPixelShader(four?b:a));
        const unsigned draws=timing?8:1;for(unsigned j=0;j<draws;++j)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));
        ok(d->EndScene());ok(end->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));UINT64 st=0,en=0,hz=0;BOOL broken=FALSE;get(end,&en,sizeof en);get(start,&st,sizeof st);get(freq,&hz,sizeof hz);get(disjoint,&broken,sizeof broken);assert(!broken&&hz&&en>=st);
        if(timing)printf("VISIBILITY_TIMING pass=%u four=%u draws=%u width=%u height=%u gpuMs=%.6f\n",pass,unsigned(four),draws,W,H,double(en-st)*1000/hz/draws);
        else{ok(d->GetRenderTargetData(target,back));D3DLOCKED_RECT lock{};ok(back->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned y=0;y<H;++y){const auto row=(const float*)((const char*)lock.pBits+y*lock.Pitch);for(unsigned x=0;x<W;++x){const float error=std::fabs(row[x*4]-expected[size_t(y)*W+x]);maxError=std::max(maxError,error);assert(std::isfinite(row[x*4])&&error<.0001f);}}
            ok(back->UnlockRect());}
    }
    // Complete production WorldGI, with matching synthetic depth, SH, residency
    // and temporal data. ABCCBA order reduces clock/thermal ordering bias.
    {
        std::vector<IDirect3DTexture9*> owned;
        target->Release();back->Release();
        ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
        ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&back,nullptr));ok(d->SetRenderTarget(0,target));
        const unsigned N=16;const auto size=V::atlas(N);
        auto fullAtlas=managedAtlas(N);owned.push_back(fullAtlas);ok(d->SetTexture(13,fullAtlas));
        const auto bind=[&](unsigned slot,unsigned w,unsigned h,auto pixel){Image im{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(size_t(w)*h*16)};
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto p=pixel(x,y);std::memcpy(im.pixels.data()+(size_t(y)*w+x)*16,p.data(),16);}
            auto t=texture(d,im);owned.push_back(t);ok(d->SetTexture(slot,t));};
        bind(1,W,H,[&](unsigned x,unsigned){const float z=8+40.f*x/W;return std::array<float,4>{1000.f/999.8f*(1-.2f/z),0,0,0};});
        bind(14,1,1,[](unsigned,unsigned){return std::array<float,4>{0,0,-1,1};});
        bind(11,1,1,[](unsigned,unsigned){return std::array<float,4>{0,0,0,0};});
        bind(10,N*N,N,[](unsigned x,unsigned y){const int a=int(x%N),b=int(y),c=int(x/N);return std::array<float,4>{float(a<8?a:a-16),float(b<8?b:b-16),float(c<8?c:c-16),1};});
        bind(7,N*N,N*6,[](unsigned,unsigned){return std::array<float,4>{100,10000,1,-1000};});
        for(unsigned slot=4;slot<7;++slot)bind(slot,N*N,N,[](unsigned x,unsigned){return std::array<float,4>{.3f+float(x%3)*.5f,0,0,0};});
        bind(8,N*N*3,N,[](unsigned,unsigned){return std::array<float,4>{0,0,0,0};});
        float constants[128][4]{};auto& c=constants;
        c[0][0]=.5f/W;c[0][1]=.5f/H;c[0][2]=.2f;c[0][3]=1000;c[1][0]=c[1][1]=c[1][2]=1;c[2][0]=1;
        for(unsigned i=0;i<4;++i)c[3+i][i]=1;
        c[18][0]=.2f;c[18][1]=.3f;c[18][2]=.4f;c[19][3]=8;c[20][0]=N;c[20][1]=.7f;c[20][3]=1;c[24][3]=5;
        c[33][0]=W*2;c[33][1]=H*2;c[33][2]=W;c[33][3]=H;c[73][2]=1;
        c[82][0]=float(size.columns);c[82][1]=1.f/size.width;c[82][2]=1.f/size.height;c[82][3]=1.f/size.columns;
        ok(d->SetPixelShaderConstantF(0,c[0],128));
        auto old=shader(d,std::string(argv[3])+"/WorldGI.bin"),fast=shader(d,std::string(argv[2])+"/WorldGILocal.bin"),four=shader(d,std::string(argv[2])+"/WorldGILocalFour.bin");
        std::vector<float> previous;float localError=0,changed=0;
        for(unsigned pass=0;pass<(timing?48u:3u);++pass){const unsigned sequence[]={0,1,2,2,1,0};const unsigned mode=timing?sequence[pass%6]:pass;
            const float localInfo[]={mode?float(size.columns):0.f,1.f/size.width,1.f/size.height,1.f/size.columns};ok(d->SetPixelShaderConstantF(82,localInfo,1));
            ok(disjoint->Issue(D3DISSUE_BEGIN));ok(freq->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));ok(d->BeginScene());ok(d->SetPixelShader(mode==0?old:mode==1?fast:four));
            const unsigned draws=timing?8:1;for(unsigned j=0;j<draws;++j)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));
            ok(d->EndScene());ok(end->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));UINT64 st=0,en=0,hz=0;BOOL broken=FALSE;get(end,&en,sizeof en);get(start,&st,sizeof st);get(freq,&hz,sizeof hz);get(disjoint,&broken,sizeof broken);assert(!broken&&hz&&en>=st);
            if(timing)printf("FULL_GI_TIMING pass=%u mode=%u gpuMs=%.6f\n",pass,mode,double(en-st)*1000/hz/draws);
            else{ok(d->GetRenderTargetData(target,back));D3DLOCKED_RECT lock{};ok(back->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> pixels(size_t(W)*H*4);
                for(unsigned y=0;y<H;++y){const auto row=(const uint16_t*)((const char*)lock.pBits+y*lock.Pitch);for(unsigned x=0;x<W*4;++x)pixels[size_t(y)*W*4+x]=half(row[x]);}ok(back->UnlockRect());
                for(size_t i=0;i<pixels.size();++i){assert(std::isfinite(pixels[i]));if(!previous.empty()){const float error=std::fabs(pixels[i]-previous[i]);if(mode==1)changed=std::max(changed,error);else localError=std::max(localError,error);}}
                previous=std::move(pixels);
            }
        }
        if(!timing){assert(changed>.001f&&localError<.0001f);printf("FULL_GI_PARITY localMaxAbs=%.9f legacyDifference=%.9f\n",localError,changed);}
        old->Release();fast->Release();four->Release();for(auto* t:owned)t->Release();
    }
    printf("VISIBILITY_GPU_PASS pixels=%u maxAbs=%.9f\n",W*H,maxError);
    for(auto* q:{start,end,freq,disjoint})q->Release();a->Release();b->Release();target->Release();back->Release();queries->Release();local->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
