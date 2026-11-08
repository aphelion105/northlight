// Actual Frame opt-in, fallback and lifetime checks. No game interaction/timings.
#undef NDEBUG
#pragma clang diagnostic ignored "-Wreturn-type"
#define main unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main
static bool denyBloomMemory=false;
static BOOL WINAPI bloomMemoryStatus(LPMEMORYSTATUSEX info){return denyBloomMemory?FALSE:GlobalMemoryStatusEx(info);}
#define GlobalMemoryStatusEx bloomMemoryStatus
#include "hdr_frame.h"
#undef GlobalMemoryStatusEx
#ifdef NORTHLIGHT_COMPACT_REFERENCE
#define Frame CompactFrame
#include "hdr_frame_reference.h"
#undef Frame
#endif
#pragma clang diagnostic ignored "-Wmicrosoft-exception-spec"
#include "forwarders.h"

struct BloomDevice:ForwardIDirect3DDevice9 {
    unsigned allocation=0,failAllocation=0,shaderFailure=0,drawFailure=0,wideDraws=0,mrtFailure=0;
    bool allocating=false,singleTarget=false,noMasks=false,mrtBound=false;IDirect3DPixelShader9* wide[3]{};IDirect3DPixelShader9* bound=nullptr;
    explicit BloomDevice(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    void clearWide(){for(auto*& shader:wide){if(shader)shader->Release();shader=nullptr;}}
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps)override{const auto hr=real->GetDeviceCaps(caps);if(SUCCEEDED(hr)){if(singleTarget)caps->NumSimultaneousRTs=1;if(noMasks)caps->PrimitiveMiscCaps&=~D3DPMISCCAPS_INDEPENDENTWRITEMASKS;}return hr;}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD slot,IDirect3DSurface9* target)override{
        if(slot==1&&((target&&mrtFailure==1)||(!target&&mrtBound&&mrtFailure==2))){mrtFailure=0;return E_FAIL;}
        const auto hr=real->SetRenderTarget(slot,target);if(slot==1&&SUCCEEDED(hr))mrtBound=target!=nullptr;return hr;
    }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state,DWORD value)override{
        if(mrtBound&&state==D3DRS_COLORWRITEENABLE1&&mrtFailure==3){mrtFailure=0;return E_FAIL;}return real->SetRenderState(state,value);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DTexture9** t,HANDLE* shared)override{
        if(allocating&&++allocation==failAllocation){*t=nullptr;return E_OUTOFMEMORY;}
        return real->CreateTexture(w,h,l,u,f,p,t,shared);
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{
        unsigned kind=(code==reinterpret_cast<const DWORD*>(kHdrBloomWideHorizontal)||code==reinterpret_cast<const DWORD*>(kHdrBloomWidePairHorizontal))?1:(code==reinterpret_cast<const DWORD*>(kHdrBloomWideVertical)||code==reinterpret_cast<const DWORD*>(kHdrBloomWidePairVertical))?2:code==reinterpret_cast<const DWORD*>(kHdrBloomWideResolve)?3:0;
        if(kind&&shaderFailure==kind){*out=nullptr;return E_FAIL;}
        auto hr=real->CreatePixelShader(code,out);if(kind&&SUCCEEDED(hr)){if(wide[kind-1])wide[kind-1]->Release();wide[kind-1]=*out;(*out)->AddRef();}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader)override{
        if(mrtBound&&mrtFailure==4){mrtFailure=0;return E_FAIL;}bound=shader;return real->SetPixelShader(shader);
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD slot,IDirect3DBaseTexture9* texture)override{
        if(mrtBound&&slot==0&&mrtFailure==5){mrtFailure=0;return E_FAIL;}return real->SetTexture(slot,texture);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT first,const float* values,UINT count)override{
        if(mrtBound&&first==2&&mrtFailure==6){mrtFailure=0;return E_FAIL;}return real->SetPixelShaderConstantF(first,values,count);
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride)override{
        if(bound&&(bound==wide[0]||bound==wide[1]||bound==wide[2])){++wideDraws;if(drawFailure==unsigned(bound==wide[0]?1:bound==wide[1]?2:3)){drawFailure=0;return E_FAIL;}}
        return real->DrawPrimitiveUP(type,count,data,stride);
    }
};
static std::vector<unsigned char> image(IDirect3DDevice9* d,IDirect3DSurface9* source,unsigned w,unsigned h){
    IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->GetRenderTargetData(source,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<unsigned char> out(w*h*8);for(unsigned y=0;y<h;++y)std::memcpy(out.data()+y*w*8,static_cast<char*>(lock.pBits)+y*lock.Pitch,w*8);
    ok(staging->UnlockRect());staging->Release();return out;
}
int main(int argc,char** argv){
    assert(argc==2);HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,256,256,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=256;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));BloomDevice d(raw);
    NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;assert(!settings.wideBloom);
    auto values=NorthlightHdrSettingsContract::defaults();settings.apply(values);assert(!settings.wideBloom);
    values[NorthlightHdrSettingsContract::WideBloom]=1;settings.apply(values);assert(settings.wideBloom);
    settings.wideBloom=false;settings.paperWhite=80;settings.maxNits=1000;
    D3DCAPS9 actualCaps{};ok(raw->GetDeviceCaps(&actualCaps));
    IDirect3DTexture9* sentinel=nullptr;ok(raw->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&sentinel,nullptr));
    unsigned cases=0;
    for(unsigned capability:{0u,1u,2u}){frame.reset();const bool singleTarget=capability==1;d.singleTarget=singleTarget;d.noMasks=capability==2;const bool paired=!singleTarget&&!d.noMasks&&actualCaps.NumSimultaneousRTs>=2&&(actualCaps.PrimitiveMiscCaps&D3DPMISCCAPS_INDEPENDENTWRITEMASKS);
    for(unsigned w:{128u,257u,64u}){
        const unsigned h=w==257?129:w;
        IDirect3DSurface9 *source=nullptr,*destination=nullptr;ok(raw->CreateRenderTarget(w,h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&source,nullptr));ok(raw->CreateRenderTarget(w,h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&destination,nullptr));
        Image input{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(w*h*16)};
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){float color[]={.1f,.2f,.3f,1};if(x>=w/2&&x<w/2+5&&y>h/2-5&&y<h/2+5){color[0]=8;color[1]=4;color[2]=2;}std::memcpy(input.pixels.data()+(y*w+x)*16,color,16);}
        auto* textureInput=texture(raw,input);IDirect3DPixelShader9* copy=nullptr;ok(raw->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCopy),&copy));
        ok(raw->BeginScene());ok(NorthlightHdr::quadState(raw,source,w,h));ok(raw->SetPixelShader(copy));ok(raw->SetTexture(0,textureInput));ok(NorthlightHdr::drawQuad(raw,w,h));ok(raw->EndScene());raw->SetTexture(0,nullptr);textureInput->Release();copy->Release();
        auto run=[&](auto& f){ok(raw->BeginScene());ok(f.ensure(&d,w,h));
            ok(raw->SetTexture(1,sentinel));ok(raw->SetSamplerState(1,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));ok(raw->SetSamplerState(1,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));ok(raw->SetSamplerState(1,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));ok(raw->SetRenderState(D3DRS_COLORWRITEENABLE1,5));
            ok(f.finish(&d,source,destination,settings,0,true,false));ok(raw->EndScene());
            IDirect3DBaseTexture9* seen=nullptr;ok(raw->GetTexture(1,&seen));assert(seen==sentinel);seen->Release();DWORD value=0;
            ok(raw->GetSamplerState(1,D3DSAMP_MINFILTER,&value));assert(value==D3DTEXF_LINEAR);ok(raw->GetSamplerState(1,D3DSAMP_MAGFILTER,&value));assert(value==D3DTEXF_LINEAR);ok(raw->GetSamplerState(1,D3DSAMP_ADDRESSU,&value));assert(value==D3DTADDRESS_WRAP);ok(raw->GetRenderState(D3DRS_COLORWRITEENABLE1,&value));assert(value==5);
            IDirect3DSurface9* extra=nullptr;const auto target=raw->GetRenderTarget(1,&extra);assert((target==D3DERR_NOTFOUND||SUCCEEDED(target))&&!extra);
            return image(raw,destination,w,h);};
        frame.invalidateExposure();settings.wideBloom=false;auto compact=run(frame);const auto baseBytes=frame.payloadBytes();auto exposure=image(raw,frame.exposureSurface(),1,1);
#ifdef NORTHLIGHT_COMPACT_REFERENCE
        NorthlightHdr::CompactFrame reference;assert(run(reference)==compact);reference.reset();++cases;
#endif
        settings.wideBloom=true;d.wideDraws=0;auto wide=run(frame);if(wide==compact||d.wideDraws!=3)std::fprintf(stderr,"WIDE_INITIAL_FAIL width=%u single=%u draws=%u compact=%u\n",w,unsigned(singleTarget),d.wideDraws,unsigned(wide==compact));assert(wide!=compact&&d.wideDraws==3);assert(image(raw,frame.exposureSurface(),1,1)==exposure);
        const unsigned bw=(w+15)/16,bh=(h+15)/16;const auto extra=(std::uint64_t(paired?2*((bw+1)/2):bw)*h+2ull*bw*bh)*8;assert(frame.payloadBytes()==baseBytes+extra);++cases;
        settings.wideBloom=false;assert(run(frame)==compact&&frame.payloadBytes()==baseBytes);++cases;
        // Fail each shader, each target allocation, each draw, or VA admission.
        const unsigned allocations=paired?4:3;
        for(unsigned failure=1;failure<=allocations+7;++failure){
            d.clearWide();d.allocation=0;d.failAllocation=failure<=allocations?failure:0;d.allocating=true;d.shaderFailure=failure>allocations&&failure<=allocations+3?failure-allocations:0;d.drawFailure=failure>allocations+3&&failure<=allocations+6?failure-allocations-3:0;denyBloomMemory=failure==allocations+7;
            settings.wideBloom=true;assert(run(frame)==compact);assert(frame.payloadBytes()==baseBytes);assert(image(raw,frame.exposureSurface(),1,1)==exposure);++cases;
            d.allocating=false;d.failAllocation=d.shaderFailure=d.drawFailure=0;denyBloomMemory=false;
            settings.wideBloom=false;assert(run(frame)==compact);settings.wideBloom=true;assert(run(frame)==wide);settings.wideBloom=false;assert(run(frame)==compact);++cases;
        }
        if(paired)for(unsigned failure=1;failure<=6;++failure){
            settings.wideBloom=false;assert(run(frame)==compact);settings.wideBloom=true;d.mrtFailure=failure;
            assert(run(frame)==compact&&d.mrtFailure==0&&frame.payloadBytes()==baseBytes);++cases;
            settings.wideBloom=false;assert(run(frame)==compact);settings.wideBloom=true;assert(run(frame)==wide);++cases;
        }
        settings.wideBloom=true;settings.bloom=0;run(frame);assert(frame.payloadBytes()==baseBytes);settings.bloom=1;
        run(frame);frame.saveAndReset(raw);assert(frame.payloadBytes()==0);assert(run(frame)==wide);assert(image(raw,frame.exposureSurface(),1,1)==exposure);++cases;
        // Production lends the AO pair to compact bloom. Wide must neither use
        // nor overwrite it, including when optional allocation fails.
        IDirect3DTexture9* borrowed[2]{};IDirect3DSurface9* borrowedSurface[2]{};
        for(unsigned i=0;i<2;++i){ok(raw->CreateTexture(w/2,h/2,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&borrowed[i],nullptr));ok(borrowed[i]->GetSurfaceLevel(0,&borrowedSurface[i]));}
        auto borrowedRun=[&]{ok(raw->BeginScene());ok(frame.finish(&d,source,destination,settings,0,true,false,.18f,nullptr,borrowed[0],borrowedSurface[0],borrowed[1],borrowedSurface[1]));ok(raw->EndScene());return image(raw,destination,w,h);};
        settings.wideBloom=false;assert(borrowedRun()==compact);auto scratch=image(raw,borrowedSurface[1],w/2,h/2);
        settings.wideBloom=true;assert(borrowedRun()==wide&&image(raw,borrowedSurface[1],w/2,h/2)==scratch);++cases;
        settings.wideBloom=false;borrowedRun();settings.wideBloom=true;d.allocation=0;d.failAllocation=1;d.allocating=true;
        assert(borrowedRun()==compact);d.allocating=false;d.failAllocation=0;settings.wideBloom=false;borrowedRun();settings.wideBloom=true;assert(borrowedRun()==wide);++cases;
        for(unsigned i=0;i<2;++i){borrowedSurface[i]->Release();borrowed[i]->Release();}
        // Keep wide resources alive into the next ensure() to exercise resizing.
        source->Release();destination->Release();
    }
    }
    raw->SetTexture(1,nullptr);sentinel->Release();
    frame.reset();d.clearWide();assert(frame.payloadBytes()==0);raw->SetPixelShader(nullptr);raw->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
    std::printf("HDR_WIDE_TOGGLE_PASS cases=%u defaultOff=1 byteExactCompact=1 fallback=shader/target/draw/VA/MRT cap1=exact sampler1=restored MRT1=detached exposureStable=1 resize=1 reset=1 release=1 timings=not_measured\n",cases);
}
