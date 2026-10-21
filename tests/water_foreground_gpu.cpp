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
static std::vector<DWORD> readPixels(IDirect3DDevice9* d,IDirect3DSurface9* source,unsigned w,unsigned h){IDirect3DSurface9* cpu=nullptr;ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,nullptr));ok(d->GetRenderTargetData(source,cpu));D3DLOCKED_RECT lock{};ok(cpu->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<DWORD> out(w*h);for(unsigned y=0;y<h;++y)std::memcpy(out.data()+y*w,static_cast<char*>(lock.pBits)+y*lock.Pitch,w*4);cpu->UnlockRect();cpu->Release();return out;}
static bool actor(unsigned x,unsigned y){if(y<78||y>103)return false;const int span=12+(103-int(y))*2;return std::abs(int(x)-124)<span;}
int main(int argc,char** argv){
    assert(argc==2);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"WaterOpaqueAndBlob";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9* scene=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&scene,nullptr));unsigned cases=0;
    for(bool maskHole:{false,true})for(int sign:{-1,1})for(unsigned resolution:{100u,50u,25u})for(auto preset:{NorthlightQuality::Preset::Quality,NorthlightQuality::Preset::Balanced,NorthlightQuality::Preset::Performance}){
        Image depth{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)},mask{W,H,D3DFMT_G32R32F,8,std::vector<unsigned char>(W*H*8)};
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float z=20,m[]={0,0};if(y>H/2){const float slope=(2.f*y/H-1)/1.7f;z=5/slope;m[0]=3/slope;m[1]=1;}if(actor(x,y)){z=2;if(maskHole)m[0]=m[1]=0;}z=projected(z);std::memcpy(depth.pixels.data()+(y*W+x)*4,&z,4);std::memcpy(mask.pixels.data()+(y*W+x)*8,m,8);}
        auto* completed=texture(d,depth);auto* liquid=texture(d,mask);NorthlightSurfaceRenderer renderer(d);NorthlightQuality::Settings settings;settings.preset=preset;settings.waterSSRResolution=resolution;settings.waterGlints=0;settings.waterPointLights=0;NorthlightSurfaceLights lights;NorthlightSurfaceFrame frame;
        frame.c[0][0]=1.f/W;frame.c[0][1]=1.f/H;frame.c[0][2]=.1f;frame.c[0][3]=1000;frame.c[1][0]=1.3f;frame.c[1][1]=1.7f;frame.c[1][2]=float(sign);frame.c[1][3]=7;frame.c[2][1]=1;frame.c[4][1]=1;frame.c[5][2]=1;frame.c[6][0]=float(sign);frame.c[7][2]=3;frame.c[7][3]=1;frame.c[8][0]=frame.c[8][1]=frame.c[8][2]=.1f;frame.c[10][2]=1;
        std::vector<DWORD> earlyReference,lateReference;
        for(unsigned pattern:{0u,1u}){
            d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,scene);D3DVIEWPORT9 vp={0,0,W,H,0,1};d->SetViewport(&vp);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff406080,1,0));
            // A real opaque actor is in front of water; a native shadow decal is on the submerged bed.
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)if(actor(x,y)){D3DRECT rect={LONG(x),LONG(y),LONG(x+1),LONG(y+1)};ok(d->Clear(1,&rect,D3DCLEAR_TARGET,0xffe020a0,1,0));}
            D3DRECT blob={20,100,61,119};ok(d->Clear(1,&blob,D3DCLEAR_TARGET,0xff030609,1,0));ok(d->BeginScene());assert(renderer.capture(scene,completed,W,H,settings));
            // Deliberately extreme native water tiles must be fully replaced inside accepted geometry.
            for(unsigned y=H/2+1;y<H;y+=8)for(unsigned x=0;x<W;x+=8){D3DRECT rect={LONG(x),LONG(y),LONG(std::min(x+8,W)),LONG(std::min(y+8,H))};ok(d->Clear(1,&rect,D3DCLEAR_TARGET,((x/8+y/8+pattern)&1)?0xffffffff:0xff000000,1,0));}
            D3DRECT top={0,0,LONG(W),LONG(H/2+1)};ok(d->Clear(1,&top,D3DCLEAR_TARGET,0xffd08010,1,0));
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)if(actor(x,y)){D3DRECT rect={LONG(x),LONG(y),LONG(x+1),LONG(y+1)};ok(d->Clear(1,&rect,D3DCLEAR_TARGET,0xffe020a0,1,0));}
            renderer.water(scene,liquid,completed,W,H,frame,settings,lights,nullptr,completed,true);auto early=readPixels(d,scene,W,H);renderer.waterReflections(scene,liquid,completed,W,H,frame,settings,nullptr);ok(d->EndScene());assert(!renderer.failed&&renderer.reflections);auto late=readPixels(d,scene,W,H);auto hits=readPixels(d,renderer.reflectionSurface,renderer.reflectionWidth,renderer.reflectionHeight);
            unsigned opaque=0,tileIndependent=0,reflected=0;for(unsigned y=H/2+3;y<H-2;++y)for(unsigned x=2;x<W-2;++x){const unsigned i=y*W+x;if(actor(x,y)){assert(early[i]==0xffe020a0&&late[i]==0xffe020a0);++opaque;}else if(pattern){assert((early[i]&0xffffff)==(earlyReference[i]&0xffffff));assert((late[i]&0xffffff)==(lateReference[i]&0xffffff));++tileIndependent;}}
            for(unsigned y=0;y<renderer.reflectionHeight;++y)for(unsigned x=0;x<renderer.reflectionWidth;++x){const unsigned sx=std::min(W-1,x*W/renderer.reflectionWidth),sy=std::min(H-1,y*H/renderer.reflectionHeight);if(actor(sx,sy)&&actor(std::min(sx+2,W-1),std::min(sy+2,H-1))&&actor(sx>2?sx-2:0,sy>2?sy-2:0))assert((hits[y*renderer.reflectionWidth+x]>>24)==0);reflected+=(hits[y*renderer.reflectionWidth+x]>>24)>64;}
            // Dark native floor shadows survive refraction before SSR; they are a separate optical contribution.
            auto brightness=[](DWORD p){return int((p>>16)&255)+int((p>>8)&255)+int(p&255);};assert(brightness(early[110*W+40])+20<brightness(early[110*W+74]));assert(opaque>1000&&reflected>300);
            if(!pattern){earlyReference=early;lateReference=late;}std::printf("sign=%d preset=%u res=%u pattern=%u opaquePreserved=%u tileIndependent=%u SSRhits=%u nativeBlobVisible=1 PASS\n",sign,unsigned(preset),resolution,pattern,opaque,tileIndependent,reflected);++cases;renderer.endFrame();
        }
        completed->Release();liquid->Release();
    }
    d->SetRenderTarget(0,nullptr);scene->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);std::printf("PASS water foreground/native blob/tile material: %u cases\n",cases);return 0;
}
