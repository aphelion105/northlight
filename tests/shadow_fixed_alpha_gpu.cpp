// Native fixed alpha-test coverage versus the production shadow replay shader.
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    // Mixed packets in one cube face. Each cell has its own draw, so a missed
    // transition cannot be hidden by a later opaque fullscreen overwrite.
    const D3DFORMAT otherFormat=nativeFormat==D3DFMT_X8R8G8B8?D3DFMT_A8R8G8B8:D3DFMT_X8R8G8B8;
    const auto modeAt=[&](size_t index){
        if(index%5==0)return 0u;
        return NorthlightShadowAlpha::fixedTestMode(info,true,unsigned(index%4)*63,index%3==0,
            index%32>=24?otherFormat:nativeFormat);
    };
    constexpr unsigned count=128;
    ok(d->SetRenderTarget(0,shadowTarget));ok(d->SetVertexShader(shadowVS));ok(d->SetPixelShader(localPS));
    ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
    D3DVIEWPORT9 full={0,0,W,W,0,1};ok(d->SetViewport(&full));
    IDirect3DQuery9* completion=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&completion));
    const auto batch=[&](bool grouped,bool comparePixels,unsigned& scopes,double& submitMs,double& completeMs){
        scopes=0;ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));
        D3DRECT seeded={0,0,W/4,W};ok(d->Clear(1,&seeded,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());
        SavedState before(d);
        const auto started=std::chrono::steady_clock::now();
        auto scope=[&](size_t i,auto submit){
            scopes+=NorthlightShadowAlpha::fixed(modeAt(i));
            return NorthlightShadowAlpha::draw(d,modeAt(i),[&](D3DFORMAT f,UINT w,UINT h){return targets.acquire(f,w,h);},submit);
        };
        auto submit=[&](size_t& at){
            const size_t i=at++;
            const unsigned mode=modeAt(i);
            const float material[]={float(NorthlightShadowAlpha::recipe(mode)),float(NorthlightShadowAlpha::fixed(mode)),0,mode?.5f:-1.f};
            ok(d->SetPixelShaderConstantF(0,material,1));
            for(unsigned v=0;v<4;++v){
                quad[v].position[0]=float((i%16)+(v&1))*.125f-1.f-1.f/W;
                quad[v].position[1]=1.f-float((i/16)+(v>>1))*.25f+1.f/W;
                quad[v].position[2]=.25f;quad[v].color[3]=.7f;
            }
            return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex));
        };
        if(grouped)ok(NorthlightShadowAlpha::drawSequence(d,count,modeAt,scope,submit));
        else for(size_t i=0;i<count;)ok(scope(i,[&]{return submit(i);}));
        ok(d->EndScene());ok(completion->Issue(D3DISSUE_END));
        const auto submitted=std::chrono::steady_clock::now();
        HRESULT ready;while((ready=completion->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){
            assert(std::chrono::steady_clock::now()-started<std::chrono::seconds(20));SwitchToThread();}
        ok(ready);const auto completed=std::chrono::steady_clock::now();
        submitMs=std::chrono::duration<double,std::milli>(submitted-started).count();
        completeMs=std::chrono::duration<double,std::milli>(completed-started).count();
        before.unchanged();std::vector<DWORD> pixels;
        if(comparePixels){
            ok(d->GetRenderTargetData(shadowTarget,shadowStage));D3DLOCKED_RECT mapped{};ok(shadowStage->LockRect(&mapped,nullptr,D3DLOCK_READONLY));
            pixels.resize(W*W);for(unsigned y=0;y<W;++y)std::memcpy(pixels.data()+y*W,static_cast<char*>(mapped.pBits)+y*mapped.Pitch,W*4);
            ok(shadowStage->UnlockRect());const auto covered=std::count(pixels.begin(),pixels.end(),0x3e800000u);assert(covered>W*W/8&&covered<W*W);
        }
        return pixels;
    };
    unsigned individual,grouped;double cpu,wall;
    for(bool blend:{false,true}){
        ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));
        ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_MIN));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend));
        const auto reference=batch(false,true,individual,cpu,wall);assert(batch(true,true,grouped,cpu,wall)==reference);
        unsigned preserved=0;for(unsigned y=0;y<W;++y)preserved+=std::count(reference.begin()+y*W,reference.begin()+y*W+W/4,0u);
        assert(blend?preserved==W*W/4:preserved<W*W/4);
        assert(individual==102&&grouped==8);
    }
    if(std::getenv("NORTHLIGHT_ALPHA_BENCHMARK")){
        for(unsigned pair=0;pair<16;++pair)for(unsigned side=0;side<2;++side){
            bool optimized=bool((pair&1)^side);unsigned scopes=0;batch(optimized,false,scopes,cpu,wall);
            std::printf("ALPHA_TIMING pair=%u grouped=%u scopes=%u submitMs=%.6f completeMs=%.6f\n",pair,unsigned(optimized),scopes,cpu,wall);}
    }
    completion->Release();ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    targets.reset();
    std::printf("SHADOW_ALPHA_BATCH_PASS draws=%u singleScopes=%u groupedScopes=%u pixels=exact minBlend=1\n",count,individual,grouped);
    // Exercise an existing RT1, non-default masks/viewport, every setup call,
    // unavailable scratch and a failed draw through the production scope.
    IDirect3DSurface9* marker=nullptr;ok(d->CreateRenderTarget(W,W,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&marker,nullptr));
    ok(d->SetRenderTarget(0,shadowTarget));ok(d->SetRenderTarget(1,marker));D3DVIEWPORT9 restricted={3,2,128,2,.1f,.8f};ok(d->SetViewport(&restricted));
    const unsigned mode=NorthlightShadowAlpha::fixedTestMode(info,true,127,false,unsigned(nativeFormat));
    for(int failure=0;failure<19;++failure){SavedState before(d);FaultDevice fault{d,failure};unsigned draws=0;
        const auto hr=NorthlightShadowAlpha::draw(&fault,mode,[&](D3DFORMAT f,UINT w,UINT h){return targets.acquire(f,w,h);},[&]{++draws;return D3D_OK;});
        assert(FAILED(hr)&&draws==0);before.unchanged();}
    for(int failure=0;failure<4;++failure){
        SavedState before(d);FaultDevice transition{d,failure};
        const unsigned modes[]={mode,0u,NorthlightShadowAlpha::fixedTestMode(info,true,63,true,unsigned(nativeFormat))};
        unsigned submitted=0;
        const auto hr=NorthlightShadowAlpha::drawSequence(&transition,3,[&](size_t i){return modes[i];},
            [&](size_t i,auto draw){return NorthlightShadowAlpha::draw(d,modes[i],[&](D3DFORMAT f,UINT w,UINT h){return targets.acquire(f,w,h);},draw);},
            [&](size_t& at){++at;++submitted;return D3D_OK;});
        assert(FAILED(hr)&&submitted==(failure==0?1u:2u));before.unchanged();
    }
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
