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
    assert(argc==2);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"SSRLitFogOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    Image depth{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)},mask{W,H,D3DFMT_G32R32F,8,std::vector<unsigned char>(W*H*8)};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float z=20,m[]={0,0};if(y>H/2){const float slope=(2.f*y/H-1)/1.7f;z=5/slope;m[0]=3/slope;m[1]=1;}z=projected(z);std::memcpy(depth.pixels.data()+(y*W+x)*4,&z,4);std::memcpy(mask.pixels.data()+(y*W+x)*8,m,8);}
    auto* finalDepth=texture(d,depth);auto* liquid=texture(d,mask);
    IDirect3DSurface9* scene=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&scene,nullptr));
    auto pixels=[&](IDirect3DSurface9* source,unsigned w,unsigned h){IDirect3DSurface9* cpu=nullptr;ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,nullptr));ok(d->GetRenderTargetData(source,cpu));D3DLOCKED_RECT lock{};ok(cpu->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<DWORD> result(w*h);for(unsigned y=0;y<h;++y)std::memcpy(result.data()+y*w,(char*)lock.pBits+y*lock.Pitch,w*4);cpu->UnlockRect();cpu->Release();return result;};
    unsigned cases=0;double reference=0;
    for(float nativeT:{1.f,.5f})for(float airT:{-1.f,1.f,.25f,0.f}){
        NorthlightSurfaceRenderer renderer(d);NorthlightQuality::Settings settings;NorthlightSurfaceFrame frame;NorthlightSurfaceLights lights;
        frame.c[0][0]=1.f/W;frame.c[0][1]=1.f/H;frame.c[0][2]=.1f;frame.c[0][3]=1000;frame.c[1][0]=1.3f;frame.c[1][1]=1.7f;frame.c[1][2]=-1;frame.c[1][3]=7;frame.c[2][1]=1;
        frame.c[4][1]=1;frame.c[5][2]=1;frame.c[6][0]=-1;frame.c[7][2]=3;frame.c[7][3]=1;frame.c[8][0]=frame.c[8][1]=frame.c[8][2]=.1f;frame.c[10][2]=1;
        frame.fog[0][1]=nativeT;frame.fog[0][2]=1;frame.fog[0][3]=1;frame.fog[1][0]=frame.fog[1][1]=frame.fog[1][2]=.1f;settings.waterGlints=0;settings.waterPointLights=0;
        Image fog{W/2,H/2,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(W*H/4*16)},distance{W/2,H/2,D3DFMT_R32F,4,std::vector<unsigned char>(W*H/4*4)};
        for(unsigned y=0;y<H/2;++y)for(unsigned x=0;x<W/2;++x){float rgba[]={.3f,.2f,.1f,std::max(0.f,airT)},z=20;if(y>H/4){const float slope=(2.f*(2*y+1)/H-1)/1.7f;z=5/slope;}std::memcpy(fog.pixels.data()+(y*W/2+x)*16,rgba,16);std::memcpy(distance.pixels.data()+(y*W/2+x)*4,&z,4);}
        auto* fogTexture=texture(d,fog);auto* fogDistance=texture(d,distance);
        d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,scene);D3DVIEWPORT9 vp={0,0,W,H,0,1};d->SetViewport(&vp);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff444444,1,0));ok(d->BeginScene());
        assert(renderer.capture(scene,finalDepth,W,H,settings));renderer.water(scene,liquid,finalDepth,W,H,frame,settings,lights,nullptr,finalDepth,true);ok(d->EndScene());assert(!renderer.reflections&&!renderer.reflectionPS);
        // The final world composite has gold/red color, unlike the native gray capture.
        D3DRECT upper={0,0,LONG(W),LONG(H/2+1)};ok(d->Clear(1,&upper,D3DCLEAR_TARGET,0xffe04008,1,0));auto before=pixels(scene,W,H);
        ok(d->BeginScene());renderer.waterReflections(scene,liquid,finalDepth,W,H,frame,settings,nullptr,airT<0?nullptr:fogTexture,airT<0?nullptr:fogDistance);ok(d->EndScene());assert(renderer.reflections&&!renderer.failed);
        auto after=pixels(scene,W,H),reflected=pixels(renderer.reflectionSurface,W,H);double delta=0;unsigned colored=0;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const auto at=y*W+x;assert((before[at]>>24)==(after[at]>>24));if(y<H/2)assert(before[at]==after[at]);
            unsigned a=reflected[at]>>24,r=(reflected[at]>>16)&255,g=(reflected[at]>>8)&255,b=reflected[at]&255;if(a>128){assert(r>2*g&&b<r/8);++colored;}
            if(y>H/2+3&&x>W/4&&x<3*W/4)delta+=int((after[at]>>16)&255)-int((before[at]>>16)&255);
        }
        assert(colored>100);if(airT==0)assert(before==after);
        if(nativeT==1&&airT<0)reference=delta;
        const double expected=nativeT*(airT<0?1:airT),ratio=delta/reference;assert(reference>1000&&std::abs(ratio-expected)<.025);
        std::printf("nativeT=%.2f airT=%.2f expectedDelta=%.3f actualDelta=%.3f colored=%u PASS\n",nativeT,airT,expected,ratio,colored);++cases;
        d->SetTexture(6,nullptr);d->SetTexture(7,nullptr);fogTexture->Release();fogDistance->Release();
    }
    d->SetTexture(5,nullptr);d->SetRenderTarget(0,nullptr);scene->Release();finalDepth->Release();liquid->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::printf("PASS final-lit SSR: %u cases; deferred trace, final color, native/air fog once, alpha/opaque preservation\n",cases);return 0;
}
