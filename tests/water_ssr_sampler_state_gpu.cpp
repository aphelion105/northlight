// Exercise the production capture/pass/resource lifecycle on a separate D3D9 device.
#define main northlight_unused_distance_replay
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

constexpr unsigned W=192,H=128;
static float projected(float z){return 1000.f/(1000.f-.1f)*(1-.1f/z);}
int main(int argc,char** argv){
    assert(argc==2);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"SSRInheritedMipState";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    Image depth{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)},mask{W,H,D3DFMT_G32R32F,8,std::vector<unsigned char>(W*H*8)};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float z=20,m[]={0,0};if(y>H/2){const float slope=(2.f*y/H-1)/1.7f;z=5/slope;m[0]=3/slope;m[1]=1;}z=projected(z);std::memcpy(depth.pixels.data()+(y*W+x)*4,&z,4);std::memcpy(mask.pixels.data()+(y*W+x)*8,m,8);}
    auto* completed=texture(d,depth);auto* liquid=texture(d,mask);IDirect3DSurface9* scene=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&scene,nullptr));
    unsigned cases=0,failures=0;
    {
        NorthlightSurfaceRenderer renderer(d);NorthlightQuality::Settings settings;NorthlightSurfaceLights lights;NorthlightSurfaceFrame frame;
        frame.c[0][0]=1.f/W;frame.c[0][1]=1.f/H;frame.c[0][2]=.1f;frame.c[0][3]=1000;frame.c[1][0]=1.3f;frame.c[1][1]=1.7f;frame.c[1][2]=-1;frame.c[1][3]=7;frame.c[2][1]=1;frame.c[4][1]=1;frame.c[5][2]=1;frame.c[6][0]=-1;frame.c[7][2]=3;frame.c[7][3]=1;frame.c[8][0]=frame.c[8][1]=frame.c[8][2]=.1f;frame.c[10][2]=1;settings.waterGlints=0;settings.waterPointLights=0;
        std::vector<DWORD> reference;
        for(bool fresh:{true,false})for(unsigned maxMip:{0u,1u,3u})for(float bias:{0.f,1.f,-1.f}){
            if(fresh)renderer.reset();d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,scene);D3DVIEWPORT9 vp={0,0,W,H,0,1};d->SetViewport(&vp);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff102040,1,0));ok(d->BeginScene());assert(renderer.capture(scene,completed,W,H,settings));D3DRECT upper={0,0,LONG(W),LONG(H/2+1)};ok(d->Clear(1,&upper,D3DCLEAR_TARGET,0xffe04008,1,0));renderer.water(scene,liquid,completed,W,H,frame,settings,lights,nullptr,completed,true);
            DWORD biasBits=0;std::memcpy(&biasBits,&bias,sizeof bias);ok(d->SetSamplerState(5,D3DSAMP_MAXMIPLEVEL,maxMip));ok(d->SetSamplerState(5,D3DSAMP_MIPMAPLODBIAS,biasBits));ok(d->SetSamplerState(5,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR));
            renderer.waterReflections(scene,liquid,completed,W,H,frame,settings,nullptr);ok(d->EndScene());assert(!renderer.failed&&renderer.reflections);
            DWORD restoredMip=0,restoredBias=0,restoredFilter=0;ok(d->GetSamplerState(5,D3DSAMP_MAXMIPLEVEL,&restoredMip));ok(d->GetSamplerState(5,D3DSAMP_MIPMAPLODBIAS,&restoredBias));ok(d->GetSamplerState(5,D3DSAMP_MIPFILTER,&restoredFilter));assert(restoredMip==maxMip&&restoredBias==biasBits&&restoredFilter==D3DTEXF_LINEAR);
            IDirect3DSurface9* cpu=nullptr;ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,nullptr));ok(d->GetRenderTargetData(renderer.reflectionSurface,cpu));D3DLOCKED_RECT lock{};ok(cpu->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<DWORD> pixels(W*H);for(unsigned y=0;y<H;++y)std::memcpy(pixels.data()+y*W,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*4);cpu->UnlockRect();cpu->Release();
            unsigned hits=0;for(auto color:pixels)hits+=(color>>24)>64;if(reference.empty()){assert(hits>1000);reference=pixels;}
            const bool equal=pixels==reference;failures+=!equal;++cases;std::printf("fresh=%u maxMip=%u bias=%.0f hits=%u referenceEqual=%u restored=1 %s\n",unsigned(fresh),maxMip,bias,hits,unsigned(equal),equal?"PASS":"FAIL");renderer.endFrame();
        }
    }
    d->SetTexture(5,nullptr);d->SetRenderTarget(0,nullptr);scene->Release();completed->Release();liquid->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);std::printf("SSR inherited mip state: %u cases, %u failures\n",cases,failures);return failures?1:0;
}
