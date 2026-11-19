// Real D3D9 shader registration, water draws and the texture seen by effects.
#define main unusedCompositeMain
#include "composite_distance_gpu.cpp"
#undef main
#include "saved_state.h"
#include <array>
#include <cstdarg>
template<class T> static void drop(T*& p){if(p){p->Release();p=nullptr;}}
static void logf(const char* fmt,...){va_list args;va_start(args,fmt);std::vfprintf(stderr,fmt,args);va_end(args);std::fputc('\n',stderr);}
#include "water_renderer.h"
#include "forwarders.h"

struct Meter:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    uint64_t bytes=0;
    unsigned draws=0,resolves=0;
    void record(UINT w,UINT h,D3DFORMAT fmt,unsigned samples){
        const unsigned stride=fmt==D3DFMT_R32F?4:fmt==D3DFMT_L8?1:fmt==D3DFMT_G32R32F?8:0;
        bytes+=uint64_t(w)*h*stride*samples;
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT fmt,D3DPOOL pool,IDirect3DTexture9** tex,HANDLE* shared)override{
        HRESULT hr=real->CreateTexture(w,h,levels,usage,fmt,pool,tex,shared);if(SUCCEEDED(hr))record(w,h,fmt,1);return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE ms,DWORD q,BOOL lock,IDirect3DSurface9** target,HANDLE* shared)override{
        HRESULT hr=real->CreateRenderTarget(w,h,f,ms,q,lock,target,shared);if(SUCCEEDED(hr))record(w,h,f,ms==D3DMULTISAMPLE_NONMASKABLE?1u<<q:std::max(1u,unsigned(ms)));return hr;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* v,UINT stride)override{++draws;return real->DrawPrimitiveUP(t,n,v,stride);}
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* s,const RECT* a,IDirect3DSurface9* t,const RECT* b,D3DTEXTUREFILTERTYPE f)override{
        ++resolves;return real->StretchRect(s,a,t,b,f);
    }
};
static uint64_t query(IDirect3DQuery9* q){uint64_t n;HRESULT hr;while((hr=q->GetData(&n,sizeof(n),D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(hr);return n;}
using Pixel=std::array<float,4>;
struct Fixture {
    IDirect3DDevice9* d;
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9 *ps=nullptr,*copy=nullptr;
    IDirect3DVertexDeclaration9* decl=nullptr;
    std::vector<DWORD> vsWords,psWords;
    std::vector<NorthlightWaterShaderIdentity> ids;
    Fixture(IDirect3DDevice9* device,const char* copyPath):d(device){
        using namespace NorthlightWaterShaderPatch;
        vsWords={0xfffe0300,0x0200001f,0x80000000,dst(1,0),0x0200001f,0x80000000,dst(6,0),
            0x0200001f,0x80000005,dst(6,1),0x02000001,dst(6,0),src(1,0),0x02000001,dst(6,1),src(1,0),0xffff};
        psWords={0xffff0300,0x0200001f,0x80000005,dst(1,0),0x02000001,dst(8,0),src(2,0),0xffff};
        for(auto* words:{&vsWords,&psWords}){
            auto* data=reinterpret_cast<const Word*>(words->data());std::vector<Word> variant;
            assert(!patch(data,words->size(),variant));
            ids.push_back({fnv1a64(data,words->size()),fnv1a64(variant.data(),variant.size()),unsigned(variant.size()),words==&vsWords});
        }
        ok(d->CreateVertexShader(vsWords.data(),&vs));ok(d->CreatePixelShader(psWords.data(),&ps));
        auto binary=read(copyPath);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(binary.data()),&copy));
        D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
        ok(d->CreateVertexDeclaration(elements,&decl));
    }
    ~Fixture(){drop(decl);drop(copy);drop(ps);drop(vs);}
    HRESULT triangle(float z,float offset,float alpha,unsigned mask){
        const Pixel v[]={{-z,-z,.5f*z,z},{(1.f+offset)*z,-z,.5f*z,z},{-.63f*z,1.15f*z,.5f*z,z}};
        const float color[4]={.375f,.125f,.75f,alpha};
        ok(d->SetPixelShaderConstantF(0,color,1));ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,mask));
        return d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,v,sizeof(Pixel));
    }
    void coverageStrip(unsigned covered,unsigned samples){
        const float z=4381.7827f+float(covered)*.137f;
        const float left=.05f+.9f*float(covered)/float(samples+1),right=left+.9f/float(samples+1);
        const Pixel v[]={{left*z,.5f*z,.5f*z,z},{right*z,.5f*z,.5f*z,z},{left*z,.9f*z,.5f*z,z},{right*z,.9f*z,.5f*z,z}};
        const float color[]={.4f,.7f,.1f,.85f};ok(d->SetPixelShaderConstantF(0,color,1));
        ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,(1u<<covered)-1));
        ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Pixel)));
    }
    std::vector<Pixel> pixels(IDirect3DTexture9* mask,unsigned w,unsigned h){
        IDirect3DSurface9 *out=nullptr,*staging=nullptr;
        ok(d->CreateRenderTarget(w,h,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&out,nullptr));
        ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        ok(d->BeginScene());ok(NorthlightHdr::quadState(d,out,w,h));ok(d->SetTexture(0,mask));ok(d->SetPixelShader(copy));ok(NorthlightHdr::drawQuad(d,w,h));ok(d->EndScene());
        ok(d->SetTexture(0,nullptr));ok(d->GetRenderTargetData(out,staging));D3DLOCKED_RECT lock{};
        ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<Pixel> data(size_t(w)*h);
        for(unsigned y=0;y<h;++y)std::memcpy(data.data()+size_t(y)*w,static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch,w*sizeof(Pixel));
        ok(staging->UnlockRect());drop(staging);drop(out);return data;
    }
};
static std::vector<Pixel> run(Fixture& f,IDirect3DSurface9* game,IDirect3DSurface9* depth,D3DSURFACE_DESC desc,bool scalar,int mixed,bool timing){
    auto* d=f.d;Meter meter(d);NorthlightWaterRenderer water(&meter,f.ids.data(),f.ids.size());
    water.setDistanceBackend(scalar);water.setDistanceCaps(scalar,scalar);water.setMrtCaps(true);
    water.registerVertex(f.vs,f.ids[0].hash,f.vsWords.data(),f.vsWords.size());water.registerPixel(f.ps,f.ids[1].hash,f.psWords.data(),f.psWords.size());
    const unsigned w=desc.Width,h=desc.Height;const D3DVIEWPORT9 vp{0,0,w,h,0,1};
    IDirect3DQuery9 *start=nullptr,*decodeStart=nullptr,*end=nullptr,*freq=nullptr;uint64_t hz=1;
    if(timing){ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&decodeStart));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));ok(freq->Issue(D3DISSUE_END));hz=query(freq);}
    std::vector<double> times,decodeTimes;
    IDirect3DTexture9* result=nullptr;
    for(unsigned frame=0;frame<(timing?64u:1u);++frame){
        if(frame)water.endFrame();
        ok(d->BeginScene());ok(NorthlightHdr::quadState(d,game,w,h));ok(d->SetDepthStencilSurface(depth));
        ok(d->SetVertexDeclaration(f.decl));ok(d->SetVertexShader(f.vs));ok(d->SetPixelShader(f.ps));
        ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE2,5));
        if(scalar&&!timing){
            const unsigned slot=desc.MultiSampleType==D3DMULTISAMPLE_NONE?1:2;
            IDirect3DSurface9* foreign=nullptr;
            ok(d->CreateRenderTarget(w,h,desc.Format,desc.MultiSampleType,desc.MultiSampleQuality,FALSE,&foreign,nullptr));
            ok(d->SetRenderTarget(slot,foreign));
            assert(!water.beginMrt(f.vs,f.ps,w,h,desc.MultiSampleType,desc.MultiSampleQuality,vp));
            IDirect3DSurface9* bound=nullptr;ok(d->GetRenderTarget(slot,&bound));assert(bound==foreign);drop(bound);
            ok(d->SetRenderTarget(slot,nullptr));drop(foreign);
        }
        auto fallback=[&](){water.capture(f.vs,f.ps,w,h,depth,vp,0,0,[&]{return f.triangle(34.125f,.7f,1.f,~0u);});};
        if(mixed==1)fallback();
        if(timing)ok(start->Issue(D3DISSUE_END));
        for(unsigned layer=0;layer<3;++layer){
            if(!timing&&layer==2){
                ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
                ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
            }
            assert(water.beginMrt(f.vs,f.ps,w,h,desc.MultiSampleType,desc.MultiSampleQuality,vp));
            const float z=layer?713.625f:139.789f;
            ok(f.triangle(z,.32f-float(layer)*.17f,layer==2?.002f:.8f,layer==1?0x5555u:~0u));water.endMrt(true);
        }
        if(!timing&&desc.MultiSampleType>D3DMULTISAMPLE_NONMASKABLE){
            const unsigned samples=unsigned(desc.MultiSampleType);
            assert(water.beginMrt(f.vs,f.ps,w,h,desc.MultiSampleType,desc.MultiSampleQuality,vp));
            for(unsigned k=0;k<=samples;++k)f.coverageStrip(k,samples);
            water.endMrt(true);
        }
        water.unbindMrt();if(mixed==2)fallback();
        if(timing)ok(decodeStart->Issue(D3DISSUE_END));
        meter.draws=meter.resolves=0;assert(water.resolveMask());result=water.maskTextureForDepth(0,1);assert(result);
        D3DSURFACE_DESC image{};ok(result->GetLevelDesc(0,&image));
        assert(image.Format==(scalar&&!mixed?D3DFMT_R32F:D3DFMT_G32R32F));
        DWORD write=0;ok(d->GetRenderState(D3DRS_COLORWRITEENABLE2,&write));assert(write==5);
        if(timing)ok(end->Issue(D3DISSUE_END));ok(d->EndScene());
        if(timing){
            const uint64_t finish=query(end),middle=query(decodeStart),begin=query(start);
            if(frame>=16){times.push_back(double(finish-begin)*1000./hz);decodeTimes.push_back(double(finish-middle)*1000./hz);}
        }
    }
    std::sort(times.begin(),times.end());
    std::sort(decodeTimes.begin(),decodeTimes.end());
    std::printf("WATER mode=%u q=%lu compact=%u mixed=%d size=%ux%u bytes=%llu resolveDraws=%u resolves=%u gpuMs=%.6f resolveMs=%.6f\n",
        desc.MultiSampleType,desc.MultiSampleQuality,unsigned(scalar),mixed,w,h,(unsigned long long)meter.bytes,meter.draws,meter.resolves,timing?times[times.size()/2]:0.,timing?decodeTimes[decodeTimes.size()/2]:0.);
    std::fflush(stdout);drop(start);drop(decodeStart);drop(end);drop(freq);
    auto output=timing?std::vector<Pixel>{}:f.pixels(result,w,h);
    if(scalar&&!mixed&&!timing&&desc.MultiSampleType==D3DMULTISAMPLE_NONE){
        water.endFrame();ok(d->SetRenderTarget(0,game));ok(d->SetTexture(3,result));
        assert(!water.beginMrt(f.vs,f.ps,w,h,desc.MultiSampleType,desc.MultiSampleQuality,vp));
        ok(d->SetTexture(3,nullptr));
    }
    water.setMaskDemand(false);assert(!water.maskTextureForDepth(0,1)&&!water.residentTargets());
    return output;
}
int main(int argc,char** argv){
    assert(argc==3||argc==4);const bool timing=argc==4;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"WaterDistanceMask";assert(RegisterClassW(&wc));
    HWND hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(library,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    unsigned modes=0;
    {
    Fixture f(d,argv[2]);
    const unsigned w=timing?3840:193,h=timing?2160:129;
    for(unsigned n=0;n<=16;++n){
        DWORD levels=0;auto ms=D3DMULTISAMPLE_TYPE(n);
        if(FAILED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,TRUE,ms,&levels)))continue;
        for(DWORD q=0;q<(n==1&&!timing?levels:1u);++q){
            if(timing&&n==1)continue;
            IDirect3DSurface9 *game=nullptr,*depth=nullptr;
            ok(d->CreateRenderTarget(w,h,D3DFMT_X8R8G8B8,ms,q,FALSE,&game,nullptr));
            ok(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,ms,q,TRUE,&depth,nullptr));
            D3DSURFACE_DESC desc{};ok(game->GetDesc(&desc));++modes;
            for(int mixed=0;mixed<(timing?1:3);++mixed){
                auto before=run(f,game,depth,desc,false,mixed,timing);
                auto after=run(f,game,depth,desc,true,mixed,timing);
                if(timing){run(f,game,depth,desc,true,mixed,true);run(f,game,depth,desc,false,mixed,true);continue;}
                unsigned wet=0,diff=0;
                for(size_t i=0;i<before.size();++i){
                    auto a=before[i],b=after[i];bool aw=a[0]>.1f&&a[1]>.003f,bw=b[0]>.1f&&b[1]>.003f;
                    wet+=aw;diff+=(aw!=bw||a[0]!=b[0]);if(diff==1&&(aw!=bw||a[0]!=b[0]))std::fprintf(stderr,"pixel %zu old=%g,%g new=%g,%g\n",i,a[0],a[1],b[0],b[1]);
                }
                std::printf("SHORE pixels=%zu wet=%u mismatch=%u\n",before.size(),wet,diff);std::fflush(stdout);
                assert(wet>100&&wet<before.size()-100&&!diff);
            }
            drop(depth);drop(game);
        }
    }
    }
    assert(modes>=2);drop(d);drop(api);FreeLibrary(library);DestroyWindow(hwnd);std::puts("WATER_DISTANCE_PASS");
}
