// Analytic upright surfaces beyond the old ray limit, and a continuous crown at varying depths.
#define main unused_distance_replay
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
#include <cstdarg>
static void logf(const char* message,...){va_list args;va_start(args,message);std::vfprintf(stderr,message,args);std::fputc('\n',stderr);va_end(args);}
template<class T> static void drop(T*& p){if(p){p->Release();p=nullptr;}}
#include "saved_state.h"
#include "surface_compiled_shaders.h"
#include "quality_settings.h"
#include "surface_frame.h"
// No name stencil is used in this fixture; its production guard is tested separately.
struct NameMaskCompositeBind{ NameMaskCompositeBind(IDirect3DDevice9*,IDirect3DSurface9* depth){assert(!depth);} };
static std::uint64_t simulatedFree=~std::uint64_t(0);
static BOOL surfaceMemoryStatus(MEMORYSTATUSEX* status){const BOOL ok=GlobalMemoryStatusEx(status);if(ok&&simulatedFree!=~std::uint64_t(0))status->ullAvailVirtual=simulatedFree;return ok;}
#define GlobalMemoryStatusEx surfaceMemoryStatus
// Expose resources for lifecycle assertions, leaving every production method unchanged.
#define class struct
#include "surface_renderer.h"
#undef class
#undef GlobalMemoryStatusEx

constexpr unsigned W=256,H=192;
static float projected(float z){return 1000.f/(1000.f-.1f)*(1-.1f/z);}
int main(int argc,char** argv){
    assert(argc>=3);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"SSRTraceOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    auto code=read(argv[2]);IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader((DWORD*)code.data(),&shader));
    IDirect3DSurface9 *target=nullptr,*readback=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    float c[24][4]={};NorthlightQuality::Settings settings;
    c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.1f;c[0][3]=1000;c[1][0]=1.3f;c[1][1]=1.7f;c[1][2]=-1;c[1][3]=7;c[2][1]=1;
    c[4][1]=1;c[5][2]=1;c[6][0]=-1;c[7][2]=3;c[7][3]=1;
    c[23][0]=argc>3?std::atof(argv[3]):settings.waterSSRSteps;c[23][1]=argc>4?std::atof(argv[4]):settings.waterSSRDistance;c[23][2]=settings.waterSSRThickness*.01f;c[23][3]=settings.waterSSRRefineSteps;
    Vertex v[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    unsigned failures=0,cases=0;auto* hierarchy=new NorthlightSurfaceRenderer(d);const bool useHierarchy=argc>5;
    for(float sign:{-1.f,1.f})for(float time:{0.f,7.f,19.f})for(float distance:{20.f,47.f,90.f,350.f,750.f,-1.f,1000.f}){
        c[1][2]=sign;c[6][0]=sign;c[1][3]=time;
        Image color{W,H,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(W*H*16)},depth{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)},mask{W,H,D3DFMT_G32R32F,8,std::vector<unsigned char>(W*H*8)};
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float z=distance<0?30.f+1.37f*(x/16):distance,m[]={0,0},rgba[]={.8f,.2f,.05f,1};
            if(y>H/2){float slope=(2.f*y/H-1)/1.7f;z=5/slope;m[0]=3/slope;m[1]=1;rgba[0]=0;rgba[2]=1;}
            z=projected(z);std::memcpy(color.pixels.data()+(y*W+x)*16,rgba,16);std::memcpy(depth.pixels.data()+(y*W+x)*4,&z,4);std::memcpy(mask.pixels.data()+(y*W+x)*8,m,8);}
        auto* colorTex=texture(d,color);auto* depthTex=texture(d,depth);auto* maskTex=texture(d,mask);
        d->SetDepthStencilSurface(nullptr);ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
        d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(shader);d->SetPixelShaderConstantF(0,&c[0][0],24);
        d->SetTexture(0,colorTex);d->SetTexture(2,maskTex);d->SetTexture(3,depthTex);
        for(unsigned i:{0u,2u,3u}){d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);}
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})d->SetRenderState(state,0);
        d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());
        if(useHierarchy){hierarchy->state();assert(hierarchy->depthBounds(depthTex,W,H));ok(d->SetRenderTarget(0,target));ok(d->SetViewport(&vp));d->SetPixelShader(shader);d->SetTexture(0,colorTex);d->SetTexture(2,maskTex);d->SetTexture(3,depthTex);}
        ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Vertex)));ok(d->EndScene());
        ok(d->GetRenderTargetData(target,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));unsigned expected=0,covered=0,weak=0,wrong=0;
        for(unsigned y=H/2+4;y<H-4;++y)for(unsigned x=W/4;x<3*W/4;++x){float actualDistance=distance<0?30.f+1.37f*(x/16):distance;float sourceY=1-float(y)/H+3*1.7f/actualDistance;if(sourceY<.09f||sourceY>.45f)continue;
            if(distance<0&&(x%16<5||x%16>10))continue;
            auto* p=(float*)((char*)lock.pBits+y*lock.Pitch)+x*4;++expected;covered+=p[3]>.1f;weak+=p[3]<.9f;wrong+=p[2]>p[0];for(unsigned k=0;k<4;++k)assert(std::isfinite(p[k]));}
        ok(readback->UnlockRect());assert(expected>100);const bool pass=distance==1000?covered==0:covered>=expected*.95&&weak<expected*.05&&!wrong;failures+=!pass;++cases;
        std::printf("sign=%.0f time=%.0f surfaceDepth=%.0f expected=%u covered=%u weak=%u wrong=%u %s\n",sign,time,distance,expected,covered,weak,wrong,pass?"PASS":"FAIL");
        for(unsigned i:{0u,2u,3u})d->SetTexture(i,nullptr);colorTex->Release();depthTex->Release();maskTex->Release();
    }
    std::printf("SSR trace: %u cases, %u failures\n",cases,failures);delete hierarchy;shader->Release();readback->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
