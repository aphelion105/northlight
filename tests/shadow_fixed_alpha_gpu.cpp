// Native fixed alpha-test coverage versus the production shadow replay shader.
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include "shadow_alpha.h"
#include "shadow_alpha_draw.h"
#include "shadow_alpha_target.h"
#include "fixtures/shadow_fixed_alpha_before.h"
#include "patch_shadow_shader.h"
#include "world_compiled_shaders.h"
#include "local_light_compiled_shaders.h"
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
static constexpr D3DRENDERSTATETYPE alphaStates[]={D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1};
struct SavedState {
    IDirect3DDevice9* d;IDirect3DSurface9* targets[2]={};D3DVIEWPORT9 viewport{};RECT scissor{};DWORD scissorEnabled=0,states[5]={};
    explicit SavedState(IDirect3DDevice9* device):d(device){
        ok(d->GetRenderTarget(0,targets));const auto second=d->GetRenderTarget(1,targets+1);assert(SUCCEEDED(second)||second==D3DERR_NOTFOUND);
        ok(d->GetViewport(&viewport));for(unsigned i=0;i<5;++i)ok(d->GetRenderState(alphaStates[i],states+i));
        ok(d->GetScissorRect(&scissor));ok(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissorEnabled));
    }
    void unchanged(){
        for(unsigned i=0;i<2;++i){IDirect3DSurface9* target=nullptr;const auto hr=d->GetRenderTarget(i,&target);assert(SUCCEEDED(hr)||hr==D3DERR_NOTFOUND);assert(target==targets[i]);if(target)target->Release();}
        D3DVIEWPORT9 after{};ok(d->GetViewport(&after));assert(!std::memcmp(&viewport,&after,sizeof after));
        RECT rect{};DWORD enabled=0;ok(d->GetScissorRect(&rect));ok(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled));assert(!std::memcmp(&scissor,&rect,sizeof rect)&&enabled==scissorEnabled);
        for(unsigned i=0;i<5;++i){DWORD state=0;ok(d->GetRenderState(alphaStates[i],&state));assert(states[i]==state);}
    }
    ~SavedState(){for(auto* target:targets)if(target)target->Release();}
};
// Setters fail after applying their change: restoration cannot assume that a
// failed driver call left the previous binding in place.
struct FaultDevice {
    IDirect3DDevice9* d;int fail,call=0;
    bool fault(){return call++==fail;}
    HRESULT GetRenderTarget(DWORD slot,IDirect3DSurface9** out){return fault()?E_FAIL:d->GetRenderTarget(slot,out);}
    HRESULT GetViewport(D3DVIEWPORT9* out){return fault()?E_FAIL:d->GetViewport(out);}
    HRESULT GetScissorRect(RECT* out){return fault()?E_FAIL:d->GetScissorRect(out);}
    HRESULT GetRenderState(D3DRENDERSTATETYPE key,DWORD* out){return fault()?E_FAIL:d->GetRenderState(key,out);}
    HRESULT SetRenderTarget(DWORD slot,IDirect3DSurface9* target){auto hr=d->SetRenderTarget(slot,target);return fault()?E_FAIL:hr;}
    HRESULT SetViewport(const D3DVIEWPORT9* viewport){auto hr=d->SetViewport(viewport);return fault()?E_FAIL:hr;}
    HRESULT SetScissorRect(const RECT* rect){auto hr=d->SetScissorRect(rect);return fault()?E_FAIL:hr;}
    HRESULT SetRenderState(D3DRENDERSTATETYPE key,DWORD value){auto hr=d->SetRenderState(key,value);return fault()?E_FAIL:hr;}
};
static std::vector<unsigned char> read(const char* path){std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
struct Vertex {float position[4],color[4],uv[2];};
int main(int argc,char** argv){
    assert(argc==3||argc==4);std::setvbuf(stdout,nullptr,_IONBF,0);
    auto dll=LoadLibraryA(argv[1]);assert(dll);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(dll,"Direct3DCreate9"));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    D3DPRESENT_PARAMETERS present{};present.Windowed=TRUE;present.hDeviceWindow=window;present.BackBufferWidth=present.BackBufferHeight=32;
    present.BackBufferFormat=D3DFMT_X8R8G8B8;present.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&present,&d));
    constexpr unsigned W=256,H=4;
    const D3DFORMAT nativeFormat=argc==4?D3DFORMAT(std::atoi(argv[3])):D3DFMT_A8R8G8B8;const unsigned nativeBytes=nativeFormat==D3DFMT_A16B16G16R16F?8:nativeFormat==D3DFMT_A32B32G32R32F?16:4;
    IDirect3DSurface9 *target=nullptr,*stage=nullptr,*shadowTarget=nullptr,*shadowStage=nullptr;
    ok(d->CreateRenderTarget(W,W,nativeFormat,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(W,W,nativeFormat,D3DPOOL_SYSTEMMEM,&stage,nullptr));
    ok(d->CreateRenderTarget(W,W,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&shadowTarget,nullptr));ok(d->CreateOffscreenPlainSurface(W,W,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&shadowStage,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
    for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(key,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    IDirect3DTexture9* texture=nullptr;ok(d->CreateTexture(W,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));
    D3DLOCKED_RECT lock{};ok(texture->LockRect(0,&lock,nullptr,0));for(unsigned x=0;x<W;++x)static_cast<DWORD*>(lock.pBits)[x]=D3DCOLOR_ARGB(x,255,255,255);ok(texture->UnlockRect(0));
    ok(d->SetTexture(0,texture));for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(0,key,D3DTEXF_POINT));
    ok(d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(0,key,D3DTADDRESS_CLAMP));
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_COLOR,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* declaration=nullptr;ok(d->CreateVertexDeclaration(elements,&declaration));ok(d->SetVertexDeclaration(declaration));
    const DWORD vertex[]={0xfffe0300,0x0200001f,0x80000000,0x900f0000,0x0200001f,0x8000000a,0x900f0001,0x0200001f,0x80000005,0x90030002,
        0x0200001f,0x80000000,0xe00f0000,0x0200001f,0x8000000a,0xe00f0001,0x0200001f,0x80000005,0xe0030002,0x0200001f,0x8000000b,0xe0010003,
        0x05000051,0xa00f0000,0x3f800000,0,0,0,0x02000001,0xe00f0000,0x90e40000,0x02000001,0xe00f0001,0x90e40001,
        0x02000001,0xe0030002,0x90e40002,0x02000001,0xe0010003,0xa0000000,0xffff};
    std::vector<uint32_t> patched;assert(NorthlightShadowShader::patch(reinterpret_cast<const uint32_t*>(vertex),std::size(vertex),patched));
    IDirect3DVertexShader9 *nativeVS=nullptr,*shadowVS=nullptr;ok(d->CreateVertexShader(vertex,&nativeVS));ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&shadowVS));
    auto code=read(argv[2]);const auto info=NorthlightShadowAlpha::analyze(reinterpret_cast<const uint32_t*>(code.data()),code.size()/4);
    assert(info.outputKnown&&info.outputMode==NorthlightShadowAlpha::TextureColor&&!info.discard);
    assert(NorthlightShadowAlpha::fixedTestMode(info,false,128,false,unsigned(nativeFormat))==NorthlightShadowAlpha::Texture);
    IDirect3DPixelShader9 *nativePS=nullptr,*directionalPS=nullptr,*localPS=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&nativePS));ok(d->CreatePixelShader(kShadowReplayPSShader,&directionalPS));ok(d->CreatePixelShader(kLocalShadowPSShader,&localPS));
    IDirect3DPixelShader9 *frozenDirectional=nullptr,*frozenLocal=nullptr;
    ok(d->CreatePixelShader(kFrozenDirectional,&frozenDirectional));ok(d->CreatePixelShader(kFrozenLocal,&frozenLocal));
    IDirect3DPixelShader9* shadowPS=directionalPS;NorthlightShadowAlpha::Targets targets(d);
    ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,1));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,4));
    float zeros[4]={};ok(d->SetPixelShaderConstantF(2,zeros,1));
    Vertex quad[4]={};for(unsigned i=0;i<4;++i){quad[i].position[0]=(i&1?1.f:-1.f)-1.f/W;quad[i].position[1]=(i&2?-1.f:1.f)+1.f/H;quad[i].position[2]=.25f;quad[i].position[3]=1;quad[i].uv[0]=i&1?1.f:0;quad[i].uv[1]=i&2?1.f:0;std::fill_n(quad[i].color,4,1.f);}
    auto draw=[&](bool native,float cutoff,unsigned recipe){
        ok(d->SetRenderTarget(0,native?target:shadowTarget));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetVertexShader(native?nativeVS:shadowVS));ok(d->SetPixelShader(native?nativePS:shadowPS));
        ok(d->SetViewport(&vp));const RECT scissor={37,1,211,3};ok(d->SetScissorRect(&scissor));
        ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,native));const float material[]={float(NorthlightShadowAlpha::recipe(recipe)),NorthlightShadowAlpha::fixed(recipe)?1.f:0.f,0,cutoff};if(!native)ok(d->SetPixelShaderConstantF(0,material,1));
        ok(d->BeginScene());
        SavedState before(d);
        auto submit=[&]{
            if(!native&&NorthlightShadowAlpha::fixed(recipe)){
                IDirect3DSurface9* current[2]={};ok(d->GetRenderTarget(0,current));ok(d->GetRenderTarget(1,current+1));
                D3DSURFACE_DESC desc{};ok(current[0]->GetDesc(&desc));assert(desc.Format==nativeFormat&&current[1]==shadowTarget);
                for(auto* surface:current)surface->Release();DWORD mask=15;ok(d->GetRenderState(D3DRS_COLORWRITEENABLE,&mask));assert(mask==0);
                ok(d->GetRenderState(D3DRS_COLORWRITEENABLE1,&mask));assert(mask==before.states[3]);
                RECT rect{};DWORD enabled=0;ok(d->GetScissorRect(&rect));ok(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled));assert(!std::memcmp(&rect,&before.scissor,sizeof rect)&&enabled==before.scissorEnabled);
            }
            return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex));
        };
        ok(native?submit():NorthlightShadowAlpha::draw(d,recipe,[&](D3DFORMAT format,UINT width,UINT height){return targets.acquire(format,width,height);},submit));
        before.unchanged();
        ok(d->EndScene());auto* readback=native?stage:shadowStage;ok(d->GetRenderTargetData(native?target:shadowTarget,readback));
        D3DLOCKED_RECT pixels{};ok(readback->LockRect(&pixels,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> mask(W*H);for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){std::uint32_t value;std::memcpy(&value,static_cast<unsigned char*>(pixels.pBits)+y*pixels.Pitch+x*(native?nativeBytes:4),4);if(!native&&value)assert(value==0x3e800000u);mask[y*W+x]=value!=0;}ok(readback->UnlockRect());return mask;
    };
    unsigned cases=0,oldErrors=0,currentErrors=0;
    for(bool scissor:{false,true})for(auto* pixel:{directionalPS,localPS})for(float opacity:{0.f,.25f,.5f,.75f,1.f})for(auto function:{D3DCMP_GREATER,D3DCMP_GREATEREQUAL})for(unsigned ref:{0u,1u,63u,64u,127u,128u,191u,254u,255u}){
        ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor));
        shadowPS=pixel;const float sphere[]={1000000,0,0,0};ok(d->SetPixelShaderConstantF(1,sphere,1));
        for(auto& v:quad)v.color[3]=opacity;ok(d->SetRenderState(D3DRS_ALPHAFUNC,function));ok(d->SetRenderState(D3DRS_ALPHAREF,ref));
        auto expected=draw(true,0,0);const float cutoff=(float(ref)+(function==D3DCMP_GREATER?.5f:0.f))/255.f;
        const unsigned mode=NorthlightShadowAlpha::fixedTestMode(info,true,ref,function==D3DCMP_GREATEREQUAL,unsigned(nativeFormat));
        assert(NorthlightShadowAlpha::fixed(mode)&&NorthlightShadowAlpha::recipe(mode)==NorthlightShadowAlpha::TextureColor);
        shadowPS=pixel==directionalPS?frozenDirectional:frozenLocal;auto old=draw(false,cutoff,0);
        shadowPS=pixel;auto current=draw(false,cutoff,mode);
        unsigned mismatches=0;for(unsigned i=0;i<expected.size();++i){oldErrors+=old[i]!=expected[i];currentErrors+=current[i]!=expected[i];mismatches+=current[i]!=expected[i];}
        if(mismatches)std::printf("MISMATCH local=%u opacity=%g func=%u ref=%u pixels=%u\n",unsigned(pixel==localPS),opacity,unsigned(function),ref,mismatches);++cases;
    }
    assert(oldErrors>0);assert(currentErrors==0);
    // Exercise an existing RT1, non-default masks/viewport, every setup call,
    // unavailable scratch and a failed draw through the production scope.
    IDirect3DSurface9* marker=nullptr;ok(d->CreateRenderTarget(W,W,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&marker,nullptr));
    ok(d->SetRenderTarget(0,shadowTarget));ok(d->SetRenderTarget(1,marker));D3DVIEWPORT9 restricted={3,2,128,2,.1f,.8f};ok(d->SetViewport(&restricted));
    const unsigned mode=NorthlightShadowAlpha::fixedTestMode(info,true,127,false,unsigned(nativeFormat));
    for(int failure=0;failure<19;++failure){SavedState before(d);FaultDevice fault{d,failure};unsigned draws=0;
        const auto hr=NorthlightShadowAlpha::draw(&fault,mode,[&](D3DFORMAT f,UINT w,UINT h){return targets.acquire(f,w,h);},[&]{++draws;return D3D_OK;});
        assert(FAILED(hr)&&draws==0);before.unchanged();}
    {SavedState before(d);assert(FAILED(NorthlightShadowAlpha::draw(d,mode,[](D3DFORMAT,UINT,UINT)->IDirect3DSurface9*{return nullptr;},[]{assert(false);return D3D_OK;})));before.unchanged();}
    {SavedState before(d);assert(FAILED(NorthlightShadowAlpha::draw(d,mode,[&](D3DFORMAT f,UINT w,UINT h){return targets.acquire(f,w,h);},[]{return E_FAIL;})));before.unchanged();}
    ok(d->SetRenderTarget(1,nullptr));marker->Release();
    auto* reused=targets.acquire(nativeFormat,W,W);assert(reused&&reused==targets.acquire(nativeFormat,W,W));
    assert(targets.acquire(nativeFormat,1024,1024)&&!targets.acquire(nativeFormat,1280,1280));
    assert(targets.bytes()==std::size_t(256*256+1024*1024)*nativeBytes);
    const D3DFORMAT formats[]={D3DFMT_A8R8G8B8,D3DFMT_X8R8G8B8,D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F};
    IDirect3DSurface9* warmed[8]={};unsigned slot=0;
    for(UINT side:{256u,1024u})for(auto format:formats){warmed[slot]=targets.acquire(format,side,side);assert(warmed[slot]&&warmed[slot]==targets.surface(slot));++slot;}
    assert(targets.bytes()==34u*1024*1024);
    for(unsigned repeat=0;repeat<32;++repeat)for(unsigned i=0;i<8;++i){const unsigned slot=(i+repeat)%8,side=slot<4?256:1024;assert(targets.acquire(formats[slot%4],side,side)==warmed[slot]);}
    assert(targets.bytes()==34u*1024*1024);
    targets.reset();assert(targets.bytes()==0);
    std::printf("SHADOW_FIXED_ALPHA_PASS cases=%u oldMismatchPixels=%u currentMismatchPixels=%u pixelsPerCase=%u nativeStateRestored=1 scissorPartialRect=1 faultCases=21 directionalAndLocal=1 nativeFormat=%u shadowFormat=R32F cacheSizes=256,1024 cacheSlots=8 maxScratchMiB=34 scratchWrites=0\n",cases,oldErrors,currentErrors,W*H,unsigned(nativeFormat));
    targets.reset();d->SetTexture(0,nullptr);d->SetVertexDeclaration(nullptr);d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);
    frozenLocal->Release();frozenDirectional->Release();localPS->Release();directionalPS->Release();nativePS->Release();shadowVS->Release();nativeVS->Release();declaration->Release();texture->Release();shadowStage->Release();shadowTarget->Release();stage->Release();target->Release();d->Release();api->Release();DestroyWindow(window);FreeLibrary(dll);
    return 0;
}
