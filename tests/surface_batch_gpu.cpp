// Actual surface passes: enclosing snapshot versus standalone restoration.
#define NORTHLIGHT_SURFACE_HELPERS_ONLY
#define NORTHLIGHT_TEST_REAL_NAME_MASK
#include "surface_effects_gpu.cpp"
#include "forwarders.h"
#include <chrono>
#include <memory>
struct NorthlightWeatherLensTestAccess {
    static void frost(NorthlightSurfaceRenderer& s){s.lensState.frost=.6f;}
    static void copy(NorthlightSurfaceRenderer& to,const NorthlightSurfaceRenderer& from){to.lensState=from.lensState;to.sandState=from.sandState;to.motion=from.motion;}
};
struct BatchCounts {unsigned captures=0,applies=0,draws=0,copies=0;};
struct BatchBlock:IDirect3DStateBlock9 {
    IDirect3DStateBlock9* real;BatchCounts& counts;ULONG refs=1;
    BatchBlock(IDirect3DStateBlock9* p,BatchCounts& c):real(p),counts(c){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(id!=IID_IUnknown&&id!=IID_IDirect3DStateBlock9)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n){real->Release();delete this;}return n;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out)override{return real->GetDevice(out);}
    HRESULT STDMETHODCALLTYPE Capture()override{++counts.captures;return real->Capture();}
    HRESULT STDMETHODCALLTYPE Apply()override{++counts.applies;return real->Apply();}
};
struct BatchMeter:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    BatchCounts counts;unsigned fault=0;
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type,IDirect3DStateBlock9** out)override{
        IDirect3DStateBlock9* p=nullptr;const auto hr=real->CreateStateBlock(type,&p);if(SUCCEEDED(hr)){++counts.captures;*out=new BatchBlock(p,counts);}else *out=p;return hr;
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{if(fault==3){fault=0;*out=nullptr;return E_OUTOFMEMORY;}return real->CreatePixelShader(code,out);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT stride)override{++counts.draws;if(fault==1){fault=0;return E_FAIL;}return real->DrawPrimitiveUP(t,n,p,stride);}
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* a,const RECT* ar,IDirect3DSurface9* b,const RECT* br,D3DTEXTUREFILTERTYPE filter)override{++counts.copies;if(fault==2){fault=0;return E_FAIL;}return real->StretchRect(a,ar,b,br,filter);}
};
static std::vector<unsigned char> batchPixels(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DSurface9* resolved,IDirect3DSurface9* staging,unsigned w,unsigned h,unsigned bytes){
    ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<unsigned char> pixels(size_t(w)*h*bytes);for(unsigned y=0;y<h;++y)std::memcpy(pixels.data()+size_t(y)*w*bytes,static_cast<const char*>(lock.pBits)+y*lock.Pitch,size_t(w)*bytes);ok(staging->UnlockRect());return pixels;
}
static NorthlightSurfaceFrame batchContext(unsigned w,unsigned h,unsigned weather,double time){
    NorthlightSurfaceFrame c{};c.c[0][0]=1.f/w;c.c[0][1]=1.f/h;c.c[0][2]=.1f;c.c[0][3]=1000;
    c.c[1][0]=1.3f;c.c[1][1]=1.7f;c.c[1][2]=-1;c.c[1][3]=float(time);c.c[2][1]=1;
    c.c[3][0]=weather<3?.8f:0;c.c[3][1]=weather==1;c.c[4][1]=1;c.c[5][2]=1;c.c[6][0]=-1;c.c[7][2]=3;c.c[7][3]=1;c.c[8][0]=16;c.c[8][1]=2;c.c[8][2]=9;c.c[9][1]=.1f;return c;
}
static void batchDraw(NorthlightSurfaceRenderer& s,IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DTexture9* depth,IDirect3DTexture9* scene,IDirect3DSurface9* sceneSurface,IDirect3DSurface9* mask,unsigned w,unsigned h,unsigned weather,double time,unsigned mode,NorthlightStateBlockPool& pool){
    auto c=batchContext(w,h,weather,time);NorthlightQuality::Settings q{};s.setMotion(c,weather+1,q,time);
    SavedState outer(d,&pool);assert(outer.ok);SavedState invalid(nullptr);BatchMeter other(d);SavedState different(mode==3?&other:nullptr);
    const SavedState* enclosing=mode==1?&outer:mode==2?&invalid:mode==3?&different:nullptr;
    if(weather<2)s.precipitation(target,depth,w,h,c,NorthlightQuality::Preset::Quality,mask,enclosing);
    if(weather==2)s.sandLens(target,w,h,time,1,mask,1,1,enclosing);
    else if(weather<2)s.lens(target,scene,sceneSurface,w,h,weather==1,time,.8f,mask,1,0,1,true,weather==1?1.f:0.f,enclosing);
}
static uint64_t batchQuery(IDirect3DQuery9* q){uint64_t n=0;HRESULT hr;const auto started=std::chrono::steady_clock::now();while((hr=q->GetData(&n,sizeof n,D3DGETDATA_FLUSH))==S_FALSE){assert(std::chrono::steady_clock::now()-started<std::chrono::seconds(10));Sleep(0);}ok(hr);return n;}
int main(int argc,char** argv){
    assert(argc==2||argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);const bool timing=argc==3;
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* real=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&real));BatchMeter meter(real);auto* d=&meter;
    IDirect3DSurface9* back=nullptr;ok(d->GetRenderTarget(0,&back));unsigned cases=0,unsupported=0;
    const auto heights=timing?std::vector<unsigned>{1080,2160}:std::vector<unsigned>{97};
    const auto formats=timing?std::vector<D3DFORMAT>{D3DFMT_A16B16G16R16F}:std::vector<D3DFORMAT>{D3DFMT_A8R8G8B8,D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F};
    const auto samples=timing?std::vector<unsigned>{0}:std::vector<unsigned>{0,4};
    for(unsigned size:heights)for(auto format:formats)for(unsigned msaa:samples){
        const unsigned h=size,w=timing?size*16/9:129,bytes=format==D3DFMT_A8R8G8B8?4:format==D3DFMT_A16B16G16R16F?8:16;
        IDirect3DSurface9 *target=nullptr,*aux=nullptr,*mask=nullptr,*resolved=nullptr,*staging=nullptr,*sceneSurface=nullptr;IDirect3DTexture9 *scene=nullptr,*depth=nullptr;
        if(FAILED(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&target,nullptr))){++unsupported;continue;}
        ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&aux,nullptr));ok(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&mask,nullptr));
        ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));ok(d->CreateOffscreenPlainSurface(w,h,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        ok(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&scene,nullptr));ok(scene->GetSurfaceLevel(0,&sceneSurface));
        Image z{w,h,D3DFMT_R32F,4,std::vector<unsigned char>(size_t(w)*h*4)};const float farDepth=1;for(size_t i=0;i<z.pixels.size();i+=4)std::memcpy(z.pixels.data()+i,&farDepth,4);depth=texture(d,z);
        const D3DVIEWPORT9 viewport={2,3,w-4,h-6,.1f,.9f};
        auto seed=[&]{ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(mask));const D3DVIEWPORT9 full={0,0,w,h,0,1};ok(d->SetViewport(&full));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_STENCIL,0x7f223344,1,0));D3DRECT left{0,0,long(w/2),long(h)};ok(d->Clear(1,&left,D3DCLEAR_STENCIL,0,1,0x80));ok(d->SetRenderTarget(1,aux));ok(d->SetViewport(&viewport));ok(d->SetTexture(0,depth));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR));};
        auto restored=[&]{D3DVIEWPORT9 vp{};ok(d->GetViewport(&vp));assert(!std::memcmp(&vp,&viewport,sizeof vp));IDirect3DSurface9* rt=nullptr;ok(d->GetRenderTarget(1,&rt));assert(rt==aux);drop(rt);ok(d->GetDepthStencilSurface(&rt));assert(rt==mask);drop(rt);IDirect3DBaseTexture9* t=nullptr;ok(d->GetTexture(0,&t));assert(t==depth);drop(t);DWORD v=0;ok(d->GetRenderState(D3DRS_SRCBLEND,&v));assert(v==D3DBLEND_DESTCOLOR);};
        if(!timing){
            for(unsigned weather=0;weather<4;++weather)for(unsigned protectedNames=0;protectedNames<2;++protectedNames)for(unsigned fault=0;fault<4;++fault){
                std::vector<unsigned char> reference;unsigned oldDraws=0,oldCopies=0;
                for(unsigned mode=0;mode<4;++mode){NorthlightSurfaceRenderer s(d);NorthlightWeatherLensTestAccess::frost(s);NorthlightStateBlockPool pool(d);seed();const auto background=batchPixels(d,target,resolved,staging,w,h,bytes);if(format==D3DFMT_A8R8G8B8)assert(background[0]==0x44&&background[1]==0x33&&background[2]==0x22&&background[3]==0x7f);meter.fault=fault;meter.counts={};
                    ok(d->BeginScene());batchDraw(s,d,target,depth,scene,sceneSurface,protectedNames?mask:nullptr,w,h,weather,1,mode,pool);ok(d->EndScene());restored();meter.fault=0;
                    const auto result=batchPixels(d,target,resolved,staging,w,h,bytes);
                    if(!mode){reference=result;oldDraws=meter.counts.draws;oldCopies=meter.counts.copies;}else{if(result!=reference){unsigned changed=0;for(size_t i=0;i<result.size();++i)changed+=result[i]!=reference[i];std::printf("BATCH_DIFFERENCE format=%u msaa=%u weather=%u mask=%u fault=%u mode=%u bytes=%u\n",unsigned(format),msaa,weather,protectedNames,fault,mode,changed);std::ofstream a("reference.raw",std::ios::binary),b("candidate.raw",std::ios::binary);a.write(reinterpret_cast<const char*>(reference.data()),reference.size());b.write(reinterpret_cast<const char*>(result.data()),result.size());}assert(result==reference);assert(meter.counts.draws==oldDraws&&meter.counts.copies==oldCopies);}
                    if(mode==1)assert(meter.counts.captures==1&&meter.counts.applies==1);
                    if(!fault&&weather<3){unsigned changed=0;const unsigned channel=bytes/4;
                        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto at=(size_t(y)*w+x)*bytes;assert(!std::memcmp(result.data()+at+3*channel,background.data()+at+3*channel,channel));if(protectedNames&&x<w/2)assert(!std::memcmp(result.data()+at,background.data()+at,bytes));else changed+=std::memcmp(result.data()+at,background.data()+at,bytes)!=0;}assert(changed>10);}
                    ++cases;
                }
            }
        }else{
            IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(frequency->Issue(D3DISSUE_END));const auto hz=batchQuery(frequency);
            for(unsigned weather=0;weather<4;++weather){NorthlightSurfaceRenderer s(d),initial(d);NorthlightWeatherLensTestAccess::frost(initial);NorthlightStateBlockPool pool(d);
                for(unsigned block=0;block<16;++block){const unsigned mode=(block/2)%2?1-block%2:block%2;NorthlightWeatherLensTestAccess::copy(s,initial);double cpu=0,gpu=0;BatchCounts total{};
                    for(unsigned frame=0;frame<32;++frame){seed();ok(d->BeginScene());ok(start->Issue(D3DISSUE_END));meter.counts={};const auto begin=std::chrono::steady_clock::now();batchDraw(s,d,target,depth,scene,sceneSurface,nullptr,w,h,weather,double(frame)/60,mode,pool);cpu+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();const auto counts=meter.counts;ok(end->Issue(D3DISSUE_END));ok(d->EndScene());gpu+=double(batchQuery(end)-batchQuery(start))*1000/double(hz);total.captures+=counts.captures;total.applies+=counts.applies;total.draws+=counts.draws;total.copies+=counts.copies;}
                    std::printf("SURFACE_BATCH_TIMING height=%u weather=%u mode=%u warm=%u cpuMs=%.9f gpuMs=%.9f captures=%u applies=%u draws=%u copies=%u\n",h,weather,mode,unsigned(block<4),cpu/32,gpu/32,total.captures/32,total.applies/32,total.draws,total.copies);
                }
            }drop(start);drop(end);drop(frequency);
        }
        ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetRenderTarget(0,back));d->SetTexture(0,nullptr);drop(target);drop(aux);drop(mask);drop(resolved);drop(staging);drop(sceneSurface);drop(scene);drop(depth);
    }
    std::printf("SURFACE_BATCH_PASS cases=%u unsupported=%u timing=%u\n",cases,unsupported,unsigned(timing));drop(back);real->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
}
