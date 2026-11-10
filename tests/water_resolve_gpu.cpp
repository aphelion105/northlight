// Actual MRT decode on authored coverage; both renderer versions share one device.
#define main unused_distance_main
#include "composite_distance_gpu.cpp"
#undef main
#include "saved_state.h"
#include "hdr_quad.h"
#include "forwarders.h"
#include <chrono>
#include <cstdarg>
template<class T> static void drop(T*& p){if(p){p->Release();p=nullptr;}}
static void logf(const char* format,...){va_list args;va_start(args,format);std::vfprintf(stderr,format,args);va_end(args);std::fputc('\n',stderr);}
#include "baseline_water.h"
#include "candidate_water.h"
struct WaterResolveTestAccess {
    template<class T> static void prepare(T& w,unsigned width,unsigned height,unsigned msaa){assert(w.mrtResources(width,height,D3DMULTISAMPLE_TYPE(msaa),0));}
    template<class T> static IDirect3DSurface9* raw(T& w){return w.twin?w.twin:w.rawSurface;}
    template<class T> static IDirect3DSurface9* mask(T& w){return w.maskSurface;}
    template<class T> static void pending(T& w,bool fallback){w.mrtDirty=true;w.mrtResolved=false;w.captured=fallback;}
};
struct ResolveMeter:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    unsigned fills=0,clears=0,draws=0,targets=0,fault=0;bool refuseFill=false;
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9* s,const RECT* r,D3DCOLOR c)override{++fills;return refuseFill?E_FAIL:real->ColorFill(s,r,c);}
    HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* r,DWORD f,D3DCOLOR c,float z,DWORD s)override{++clears;if(fault==1){fault=0;return E_FAIL;}return real->Clear(n,r,f,c,z,s);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT stride)override{++draws;if(fault==2){fault=0;return E_FAIL;}return real->DrawPrimitiveUP(t,n,p,stride);}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD i,IDirect3DSurface9* s)override{++targets;return real->SetRenderTarget(i,s);}
    void reset(){fills=clears=draws=targets=0;}
};
static uint64_t query(IDirect3DQuery9* q){uint64_t n=0;HRESULT hr;while((hr=q->GetData(&n,sizeof n,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(hr);return n;}
static Image authored(unsigned w,unsigned h,unsigned pattern,bool fallback=false){
    Image out{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(size_t(w)*h*16)};
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const float coverage=pattern==0?0:pattern==1?1:pattern==2?float((x+3*y)%4)*.25f:((x==0||y==0||x==w-1||y==h-1)?1.f:0.f);const float data[]={fallback?33.f:(128.f+float((x+y)%31))*coverage,fallback?1.f:coverage,0,1};std::memcpy(out.pixels.data()+(size_t(y)*w+x)*16,data,16);}return out;
}
static std::vector<unsigned char> readMask(IDirect3DDevice9* d,IDirect3DSurface9* mask,IDirect3DSurface9* staging,unsigned w,unsigned h){
    ok(d->GetRenderTargetData(mask,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> out(size_t(w)*h*8);for(unsigned y=0;y<h;++y)std::memcpy(out.data()+size_t(y)*w*8,static_cast<char*>(lock.pBits)+y*lock.Pitch,w*8);ok(staging->UnlockRect());return out;
}
int main(int argc,char** argv){
    assert(argc==3||argc==4);const bool timing=argc==4;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightWaterResolve";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);auto library=LoadLibraryA(argv[1]);assert(library);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* real=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&real));ResolveMeter meter(real);IDirect3DDevice9* d=&meter;
    auto legacyWords=NorthlightWaterShaderPatch::resolveShader();assert(legacyWords[15]==NorthlightWaterShaderPatch::src(3,0));legacyWords[15]=NorthlightWaterShaderPatch::src(3,0,0x04);IDirect3DPixelShader9* legacyShader=nullptr;
    const auto legacyResult=d->CreatePixelShader(reinterpret_cast<const DWORD*>(legacyWords.data()),&legacyShader);std::printf("WATER_RESOLVE_LEGACY_SHADER hr=%08lx\n",static_cast<unsigned long>(legacyResult));drop(legacyShader);std::fflush(stdout);
    IDirect3DSurface9* back=nullptr;ok(d->GetRenderTarget(0,&back));auto code=read(argv[2]);IDirect3DPixelShader9* copy=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&copy));unsigned cases=0,unsupported=0,baselineRefusedClearPixels=0;
    for(unsigned height:timing?std::vector<unsigned>{1080,2160}:std::vector<unsigned>{64,97})for(unsigned msaa:timing?std::vector<unsigned>{0}:std::vector<unsigned>{0,4}){
        const unsigned h=height,w=timing?h*16/9:h==97?129:96;
        IDirect3DSurface9 *game=nullptr,*aux=nullptr,*depth=nullptr,*staging=nullptr;
        if(FAILED(d->CreateRenderTarget(w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&game,nullptr))){++unsupported;continue;}
        ok(d->CreateRenderTarget(w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&aux,nullptr));ok(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&depth,nullptr));ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        {
        BaselineWaterRenderer before(d);NorthlightWaterRenderer after(d);WaterResolveTestAccess::prepare(before,w,h,msaa);WaterResolveTestAccess::prepare(after,w,h,msaa);
        auto fill=[&](IDirect3DSurface9* target,IDirect3DTexture9* input){ok(NorthlightHdr::quadState(d,target,w,h));ok(d->SetTexture(0,input));ok(d->SetPixelShader(copy));ok(NorthlightHdr::drawQuad(d,w,h));ok(d->SetTexture(0,nullptr));};
        const D3DVIEWPORT9 vp={2,3,w-4,h-6,.1f,.9f};const RECT scissor={3,4,LONG(w-5),LONG(h-6)};
        auto seed=[&](IDirect3DTexture9* input,bool refuse){ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetRenderTarget(0,game));ok(d->SetRenderTarget(1,aux));if(!refuse)ok(d->SetDepthStencilSurface(depth));ok(d->SetViewport(&vp));ok(d->SetScissorRect(&scissor));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR));ok(d->SetTexture(0,input));};
        auto restored=[&](IDirect3DTexture9* input,bool refuse){D3DVIEWPORT9 actual{};RECT rect{};ok(d->GetViewport(&actual));ok(d->GetScissorRect(&rect));assert(!std::memcmp(&actual,&vp,sizeof vp)&&!std::memcmp(&rect,&scissor,sizeof rect));for(unsigned i=0;i<2;++i){IDirect3DSurface9* rt=nullptr;ok(d->GetRenderTarget(i,&rt));assert(rt==(i?aux:game));drop(rt);}IDirect3DSurface9* ds=nullptr;const auto hr=d->GetDepthStencilSurface(&ds);assert(refuse?hr==D3DERR_NOTFOUND:SUCCEEDED(hr)&&ds==depth);drop(ds);IDirect3DBaseTexture9* tex=nullptr;ok(d->GetTexture(0,&tex));assert(tex==input);drop(tex);DWORD value=0;ok(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&value));assert(value);ok(d->GetRenderState(D3DRS_SRCBLEND,&value));assert(value==D3DBLEND_DESTCOLOR);};
        auto fallbackTexture=texture(d,authored(w,h,0,true));
        for(unsigned pattern:timing?std::vector<unsigned>{0,2,1}:std::vector<unsigned>{0,1,2,3}){
            auto inputImage=authored(w,h,pattern);auto input=texture(d,inputImage);
            ok(d->BeginScene());fill(WaterResolveTestAccess::raw(before),input);fill(WaterResolveTestAccess::raw(after),input);ok(d->EndScene());
            for(bool fallback:{false,true}){
                if(!timing){for(bool refuse:{false,true}){
                    std::vector<unsigned char> reference;
                    for(unsigned mode=0;mode<2;++mode){ok(d->BeginScene());auto* mask=mode?WaterResolveTestAccess::mask(after):WaterResolveTestAccess::mask(before);fill(mask,fallbackTexture);seed(input,refuse);meter.refuseFill=refuse;meter.reset();WaterResolveTestAccess::pending(before,fallback);WaterResolveTestAccess::pending(after,fallback);assert(mode?after.resolveMask():before.resolveMask());restored(input,refuse);ok(d->EndScene());meter.refuseFill=false;
                        if(mode)assert(meter.fills==0&&meter.clears==unsigned(!fallback)&&meter.draws==1);const auto pixels=readMask(d,mask,staging,w,h);if(!mode)reference=pixels;else if(!refuse)assert(pixels==reference);
                        for(size_t i=0;i<size_t(w)*h;++i){float data[2],raw[4];std::memcpy(data,pixels.data()+i*8,8);std::memcpy(raw,inputImage.pixels.data()+i*16,16);const float distance=raw[1]>=.5f?raw[0]/raw[1]:fallback?33.f:0.f,coverage=raw[1]>=.5f||fallback?1.f:0.f;const bool matches=data[0]==distance&&data[1]==coverage;
                            // The old manual ColorFill fallback can clear nothing with an MSAA auxiliary target attached.
                            if(!mode&&refuse){baselineRefusedClearPixels+=!matches;continue;}
                            if(!matches)std::fprintf(stderr,"SEMANTIC_MISMATCH msaa=%u pattern=%u fallback=%u refuse=%u mode=%u x=%u y=%u got=(%g,%g) want=(%g,%g)\n",msaa,pattern,unsigned(fallback),unsigned(refuse),mode,unsigned(i%w),unsigned(i/w),data[0],data[1],distance,coverage);assert(matches);}++cases;
                    }
                }}else{
                    IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(frequency->Issue(D3DISSUE_END));const auto hz=query(frequency);
                    for(unsigned block=0;block<16;++block){const unsigned mode=(block/2)%2?1-block%2:block%2;double cpu=0,gpu=0;unsigned fills=0,clears=0,targets=0;
                        for(unsigned frame=0;frame<32;++frame){ok(d->BeginScene());if(frame==0){fill(WaterResolveTestAccess::mask(before),fallbackTexture);fill(WaterResolveTestAccess::mask(after),fallbackTexture);}seed(input,false);WaterResolveTestAccess::pending(before,fallback);WaterResolveTestAccess::pending(after,fallback);meter.reset();ok(start->Issue(D3DISSUE_END));const auto begin=std::chrono::steady_clock::now();assert(mode?after.resolveMask():before.resolveMask());cpu+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();fills+=meter.fills;clears+=meter.clears;targets+=meter.targets;ok(end->Issue(D3DISSUE_END));ok(d->EndScene());gpu+=double(query(end)-query(start))*1000/double(hz);}
                        std::printf("WATER_RESOLVE_TIMING height=%u pattern=%u fallback=%u mode=%u warm=%u cpuMs=%.9f gpuMs=%.9f fills=%u clears=%u targets=%u\n",h,pattern,unsigned(fallback),mode,unsigned(block<4),cpu/32,gpu/32,fills/32,clears/32,targets/32);std::fflush(stdout);
                    }drop(start);drop(end);drop(frequency);
                }
            }
            drop(input);
        }
        // A failed clear/draw restores game state and never exposes an incomplete MRT mask.
        if(!timing)for(unsigned fault:{1u,2u}){NorthlightWaterRenderer broken(d);WaterResolveTestAccess::prepare(broken,w,h,msaa);seed(fallbackTexture,false);WaterResolveTestAccess::pending(broken,false);meter.fault=fault;ok(d->BeginScene());assert(!broken.resolveMask()&&!broken.maskTextureForDepth(0,1));ok(d->EndScene());restored(fallbackTexture,false);assert(!meter.fault);++cases;}
        drop(fallbackTexture);
        ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetTexture(0,nullptr));ok(d->SetRenderTarget(0,back));
        }
        drop(game);drop(aux);drop(depth);drop(staging);
    }
    std::printf("WATER_RESOLVE_PASS cases=%u unsupported=%u timing=%u baselineRefusedClearPixels=%u\n",cases,unsupported,unsigned(timing),baselineRefusedClearPixels);drop(copy);drop(back);real->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
}
