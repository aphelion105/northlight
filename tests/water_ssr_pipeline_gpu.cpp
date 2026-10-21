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
    assert(argc==2);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"WaterSSRIntegration";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    Image early{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)},final=early,mask{W,H,D3DFMT_G32R32F,8,std::vector<unsigned char>(W*H*8)};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float z=1000,fz=20,m[]={0,0};
        if(y>H/2){const float slope=(2.f*y/H-1)/1.7f;z=fz=5/slope;m[0]=3/slope;m[1]=1;}
        z=projected(z);fz=projected(fz);std::memcpy(early.pixels.data()+(y*W+x)*4,&z,4);std::memcpy(final.pixels.data()+(y*W+x)*4,&fz,4);std::memcpy(mask.pixels.data()+(y*W+x)*8,m,8);}
    auto* earlyTexture=texture(d,early);auto* finalTexture=texture(d,final);auto* maskTexture=texture(d,mask);
    Image foreground=final;const D3DRECT actor={LONG(W*3/8),LONG(H*3/4),LONG(W*5/8),LONG(H*7/8)};
    for(unsigned y=actor.y1;y<unsigned(actor.y2);++y)for(unsigned x=actor.x1;x<unsigned(actor.x2);++x){const float z=projected(2);std::memcpy(foreground.pixels.data()+(y*W+x)*4,&z,4);}
    auto* foregroundTexture=texture(d,foreground);
    NorthlightSurfaceFrame frame;frame.c[0][0]=1.f/W;frame.c[0][1]=1.f/H;frame.c[0][2]=.1f;frame.c[0][3]=1000;frame.c[1][0]=1.3f;frame.c[1][1]=1.7f;frame.c[1][2]=-1;frame.c[1][3]=7;frame.c[2][1]=1;
    frame.c[4][1]=1;frame.c[5][2]=1;frame.c[6][0]=-1;frame.c[7][2]=3;frame.c[7][3]=1;frame.c[8][0]=frame.c[8][1]=frame.c[8][2]=.1f;frame.c[10][2]=1;
    IDirect3DSurface9 *target=nullptr,*readback=nullptr,*depthCopy=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));ok(d->CreateRenderTarget(W,H,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&depthCopy,nullptr));
    IDirect3DSurface9* lateDepth=nullptr;ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&lateDepth,nullptr));
    auto completedFormat=(D3DFORMAT)MAKEFOURCC('D','F','2','4');
    if(std::strstr(argv[1],"SysWOW64"))completedFormat=(D3DFORMAT)MAKEFOURCC('I','N','T','Z');
    auto pixels=[&](IDirect3DSurface9* source,unsigned w,unsigned h){IDirect3DSurface9* read=nullptr;ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));ok(d->GetRenderTargetData(source,read));D3DLOCKED_RECT lock{};ok(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<DWORD> result(w*h);for(unsigned y=0;y<h;++y)std::memcpy(result.data()+y*w,(char*)lock.pBits+y*lock.Pitch,w*4);read->UnlockRect();read->Release();return result;};
    unsigned cases=0;
    {
        NorthlightSurfaceRenderer renderer(d);NorthlightQuality::Settings settings;NorthlightSurfaceLights lights;
        bool lateActor=false,missingCompleted=false;
        auto render=[&]{
            ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));for(unsigned s=1;s<4;++s)d->SetRenderTarget(s,nullptr);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            D3DVIEWPORT9 viewport={0,0,W,H,0,1};ok(d->SetViewport(&viewport));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff14242e,1,0));ok(d->BeginScene());
            if(settings.water)assert(renderer.capture(target,earlyTexture,W,H,settings));
            // DF24 RESZ is exercised on packaged DXVK. The native fixture supplies final R32F depth.
            D3DSURFACE_DESC targetDesc{};ok(target->GetDesc(&targetDesc));bool singleSample=!lateActor&&targetDesc.MultiSampleType==D3DMULTISAMPLE_NONE&&!std::strstr(argv[1],"SysWOW64");
            if(singleSample){ok(d->SetDepthStencilSurface(lateDepth));ok(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0));}
            // Four visible world surfaces are drawn AFTER the first water capture:
            // landscape, building, creature and character. No object-type allowlist.
            const DWORD colors[]={0xffff0000,0xff00ff00,0xff0000ff,0xffffff00};
            for(unsigned band=0;band<4;++band){D3DRECT rect={LONG(band*W/4),0,LONG((band+1)*W/4),LONG(H/2+1)};ok(d->Clear(1,&rect,D3DCLEAR_TARGET|(singleSample?D3DCLEAR_ZBUFFER:0),colors[band],projected(20),0));}
            auto* completed=lateActor?foregroundTexture:singleSample?renderer.completedDepthTarget(W,H,completedFormat,settings):finalTexture;
            if(singleSample&&completed){
                // The early AO snapshot is still sky here. Resolve late Z into the independent SSR target.
                d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZ);d->SetTexture(0,completed);
                for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3})d->SetRenderState(state,0);
                d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
                const float point[3]={};ok(d->DrawPrimitiveUP(D3DPT_POINTLIST,1,point,sizeof point));
                ok(d->SetRenderState(D3DRS_POINTSIZE,0x3f800000));ok(d->SetRenderState(D3DRS_POINTSIZE,0x7fa05000));
                ok(d->SetRenderState(D3DRS_POINTSIZE,0x3f800000));ok(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0));
                // Confirm late native Z, while retaining the original refraction/AO snapshot.
                d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,depthCopy);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(renderer.copyPS);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);ok(renderer.quad(W,H));
                ok(d->GetRenderTargetData(depthCopy,readback));D3DLOCKED_RECT sample{};ok(readback->LockRect(&sample,nullptr,D3DLOCK_READONLY));
                assert(std::fabs(*(float*)((char*)sample.pBits+10*sample.Pitch+10*4)-projected(20))<1e-6);
                assert(*(float*)((char*)sample.pBits+(H-10)*sample.Pitch+10*4)==1);readback->UnlockRect();d->SetRenderTarget(0,target);
            }
            if(lateActor)ok(d->Clear(1,&actor,D3DCLEAR_TARGET,0xffff00ff,1,0));
            // A late actor is absent from the early AO/refraction depth. Completed native Z sees it.
            auto native=missingCompleted?pixels(target,W,H):std::vector<DWORD>{};
            renderer.water(target,maskTexture,lateActor?earlyTexture:finalTexture,W,H,frame,settings,lights,nullptr,missingCompleted?nullptr:completed);ok(d->EndScene());assert(!renderer.failed);++cases;
            if(missingCompleted)assert(pixels(target,W,H)==native);
            if(lateActor){auto visible=pixels(target,W,H);for(unsigned y=actor.y1+2;y<unsigned(actor.y2-2);++y)for(unsigned x=actor.x1+2;x<unsigned(actor.x2-2);++x)assert(visible[y*W+x]==0xffff00ff);}
        };
        // A first allocation still requires space in addition to the reserve.
        simulatedFree=(96ull<<20)+1;d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,target);ok(d->BeginScene());
        assert(!renderer.capture(target,earlyTexture,W,H,settings)&&!renderer.failed&&!renderer.color&&!renderer.depth);ok(d->EndScene());renderer.endFrame();simulatedFree=~std::uint64_t(0);++cases;
        for(unsigned resolution:{100u,50u,25u,73u}){
            settings.waterSSRResolution=resolution;render();
            assert(renderer.reflectionWidth==(W*resolution+99)/100&&renderer.reflectionHeight==(H*resolution+99)/100);
            D3DSURFACE_DESC desc{};ok(renderer.reflectionSurface->GetDesc(&desc));assert(desc.Width==renderer.reflectionWidth&&desc.Height==renderer.reflectionHeight);
            auto result=pixels(renderer.reflectionSurface,desc.Width,desc.Height);unsigned receivers[4]={};
            for(DWORD value:result){unsigned a=value>>24,r=(value>>16)&255,g=(value>>8)&255,b=value&255;if(a<32)continue;
                if(r>3*a/4&&g<a/8&&b<a/8)++receivers[0];if(g>3*a/4&&r<a/8&&b<a/8)++receivers[1];if(b>3*a/4&&r<a/8&&g<a/8)++receivers[2];if(r>3*a/4&&g>3*a/4&&b<a/8)++receivers[3];}
            std::fprintf(stderr,"SSR resolution=%u receivers=%u,%u,%u,%u\n",resolution,receivers[0],receivers[1],receivers[2],receivers[3]);
            for(unsigned count:receivers)assert(count>0);
            // SavedState returns all effect-private samplers to the game's state.
            IDirect3DSurface9* restored=nullptr;ok(d->GetRenderTarget(0,&restored));assert(restored==target);restored->Release();renderer.endFrame();
        }
        lateActor=true;render();renderer.endFrame();lateActor=false;
        // Existing targets consume no new address space: do not charge them again.
        auto* residentColor=renderer.color;auto* residentDepth=renderer.depth;auto* residentSSR=renderer.reflections;
        simulatedFree=(96ull<<20)+1;render();assert(renderer.available&&renderer.color==residentColor&&renderer.depth==residentDepth&&renderer.reflections==residentSSR);renderer.endFrame();simulatedFree=~std::uint64_t(0);
        simulatedFree=(96ull<<20)-1;ok(d->BeginScene());assert(!renderer.capture(target,earlyTexture,W,H,settings)&&!renderer.failed);ok(d->EndScene());renderer.endFrame();simulatedFree=~std::uint64_t(0);++cases;
        render();assert(renderer.color==residentColor&&renderer.depth==residentDepth&&renderer.reflections==residentSSR);renderer.endFrame();
        render();auto* retainedZ=renderer.completedDepthTarget(W,H,completedFormat,settings);assert(retainedZ);renderer.endFrame();settings.waterSSRResolution=100;
        simulatedFree=(96ull<<20)+1;render();assert(renderer.available&&renderer.completedDepth==retainedZ&&!renderer.reflections);renderer.endFrame();simulatedFree=~std::uint64_t(0);
        render();assert(renderer.reflections&&renderer.completedDepth==retainedZ);renderer.endFrame();
        settings.waterSSR=0;render();assert(!renderer.reflections&&!renderer.reflectionScene&&renderer.completedDepthTarget(W,H,completedFormat,settings));renderer.endFrame();
        lateActor=true;render();renderer.endFrame();lateActor=false;
        missingCompleted=true;render();renderer.endFrame();missingCompleted=false;
        settings.waterSSR=1;settings.waterSSRStrength=0;render();assert(!renderer.reflections&&!renderer.reflectionScene&&renderer.completedDepthTarget(W,H,completedFormat,settings));renderer.endFrame();
        settings.waterSSRStrength=100;settings.waterRefraction=0;render();assert(!renderer.color&&renderer.depth&&renderer.reflections);renderer.endFrame();
        settings.waterRefraction=1;settings.waterPointLights=0;lights.count=1;lights.position[0][0]=20;lights.position[0][2]=5;lights.position[0][3]=50;lights.color[0][0]=3;lights.color[0][3]=.02f;
        render();assert(!renderer.pointsPS);renderer.endFrame();settings.waterPointLights=1;render();assert(renderer.pointsPS);renderer.endFrame();
        settings.water=0;render();assert(!renderer.depth&&!renderer.color&&!renderer.reflections&&!renderer.available);renderer.endFrame();
        render();assert(!renderer.depth&&!renderer.reflections&&!renderer.available);renderer.endFrame();
        // Re-enabling after all resources were released must work without a device restart.
        settings.water=1;settings.waterSSRResolution=50;render();assert(renderer.reflections&&renderer.color);renderer.endFrame();
        DWORD levels=0;
        if(SUCCEEDED(api->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_4_SAMPLES,&levels))&&levels){
            IDirect3DSurface9* multisampled=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_4_SAMPLES,0,FALSE,&multisampled,nullptr));
            auto* ordinary=target;target=multisampled;renderer.reset();settings.preset=NorthlightQuality::Preset::Balanced;settings.waterSSRResolution=25;
            render();assert(renderer.resolveSurface&&renderer.reflections);renderer.endFrame();target=ordinary;ok(d->SetRenderTarget(0,target));multisampled->Release();
        }
    }
    std::printf("PASS SSR production pipeline: %u cases; late landscape/building/creature/character receivers, 25/50/73/100%%, disabled pass/resource release, refraction/point toggle, re-enable\n",cases);
    d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,nullptr);drop(lateDepth);drop(depthCopy);drop(readback);drop(target);drop(earlyTexture);drop(finalTexture);drop(maskTexture);drop(foregroundTexture);d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
