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

constexpr unsigned W=128,H=2160;
static float projected(float z){return 1000.f/(1000.f-.1f)*(1-.1f/z);}
int main(int argc,char** argv){
    assert(argc>=4);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"SSRThinColorOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    auto legacyCode=read(argv[2]);IDirect3DPixelShader9* legacy=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(legacyCode.data()),&legacy));
    auto oracleCode=read(argv[3]);IDirect3DPixelShader9* oracle=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(oracleCode.data()),&oracle));
    Image color{W,H,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(W*H*4)},depth{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)},mask{W,H,D3DFMT_G32R32F,8,std::vector<unsigned char>(W*H*8)};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        const bool leaf=y<H/2&&y%17==3;float z=leaf?20:1000,m[]={0,0};DWORD rgb=leaf?0xffe56808:0xffffffff;
        if(y>H/2){const float slope=(2.f*y/H-1)/1.7f;z=5/slope;m[0]=3/slope;m[1]=1;rgb=0xff102040;}
        z=projected(z);std::memcpy(color.pixels.data()+(y*W+x)*4,&rgb,4);std::memcpy(depth.pixels.data()+(y*W+x)*4,&z,4);std::memcpy(mask.pixels.data()+(y*W+x)*8,m,8);
    }
    auto* source=texture(d,color);auto* finalDepth=texture(d,depth);auto* liquid=texture(d,mask);IDirect3DSurface9* sourceSurface=nullptr;ok(source->GetSurfaceLevel(0,&sourceSurface));
    IDirect3DSurface9* scene=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&scene,nullptr));
    unsigned cases=0,failures=0;
    {
        NorthlightSurfaceRenderer renderer(d);NorthlightQuality::Settings settings;renderer.sourceFormat=D3DFMT_A8R8G8B8;
        float c[26][4]={};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.1f;c[0][3]=1000;c[1][0]=1.3f;c[1][1]=1.7f;c[1][2]=-1;c[2][1]=1;
        c[4][1]=1;c[5][2]=1;c[6][0]=-1;c[7][2]=3;c[7][3]=1;
        auto render=[&](bool old,bool dense,unsigned resolution){
            settings.waterSSRResolution=resolution;settings.waterSSRSteps=64;renderer.boundsUnavailable=old;
            d->SetRenderTarget(0,scene);d->SetDepthStencilSurface(nullptr);d->SetTexture(5,nullptr);renderer.state();d->SetPixelShader(nullptr);d->SetTexture(0,source);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);ok(d->BeginScene());ok(renderer.quad(W,H));ok(d->EndScene());renderer.state();
            c[23][0]=float(settings.waterSSRSteps);c[23][2]=.3f;c[23][3]=4;d->SetPixelShaderConstantF(0,&c[0][0],26);
            IDirect3DPixelShader9* retained=renderer.reflectionPS;if(old||dense)renderer.reflectionPS=dense?oracle:legacy;
            ok(d->BeginScene());assert(renderer.reflectionPass(scene,liquid,finalDepth,W,H,settings));ok(d->EndScene());
            if(old||dense)renderer.reflectionPS=retained;
            D3DSURFACE_DESC desc{};ok(renderer.reflectionSceneSurface->GetDesc(&desc));assert(desc.Width==W&&desc.Height==H);
            ok(renderer.reflectionSurface->GetDesc(&desc));IDirect3DSurface9* cpu=nullptr;ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,nullptr));ok(d->GetRenderTargetData(renderer.reflectionSurface,cpu));
            D3DLOCKED_RECT lock{};ok(cpu->LockRect(&lock,nullptr,D3DLOCK_READONLY));unsigned colored=0,white=0;
            for(unsigned y=desc.Height/2+8;y<desc.Height-8;++y)for(unsigned x=desc.Width/4;x<3*desc.Width/4;++x){DWORD pixel=*(DWORD*)((char*)lock.pBits+y*lock.Pitch+x*4);unsigned a=pixel>>24,r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;if(a<64)continue;colored+=r>2*g&&b<r/8;white+=g>r*3/4||b>r/4;}
            cpu->UnlockRect();cpu->Release();return std::array<unsigned,2>{colored,white};
        };
        for(unsigned resolution:{100u,50u,25u})for(float phase:{0.f,7.f,19.f}){
            c[1][3]=phase;auto before=render(true,false,resolution),reference=render(false,true,resolution),after=render(false,false,resolution);
            // Analytic ray/plane intersection is independent of marching; current hits must preserve its colored geometry.
            const bool pass=reference[0]>10&&after[0]>=reference[0]*.90&&after[0]<=reference[0]*1.10&&!after[1];failures+=!pass;++cases;
            std::printf("resolution=%u time=%.0f old=%u oracle=%u hierarchy=%u white=%u %s\n",resolution,phase,before[0],reference[0],after[0],after[1],pass?"PASS":"FAIL");
        }
    }
    d->SetRenderTarget(0,nullptr);d->SetTexture(5,nullptr);legacy->Release();oracle->Release();scene->Release();sourceSurface->Release();source->Release();finalDepth->Release();liquid->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::printf("SSR thin color: %u cases, %u failures\n",cases,failures);return failures?1:0;
}
