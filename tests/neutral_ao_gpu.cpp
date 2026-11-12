// Actual Device::renderEffects and HDR finish, including their late fallback.
#define main unused_screen_effects_main
#include "world_screen_effects_gpu.cpp"
#undef main
bool northlightForceNeutralAO=false; // Test-overlay control: the previous eager clear.
struct NeutralMeter:ForwardIDirect3DDevice9 {
    IDirect3DSurface9* ambient=nullptr;IDirect3DSurface9* target=nullptr;IDirect3DPixelShader9* pixel=nullptr;
    WorldRenderer* world=nullptr;unsigned clears=0,fault=0;bool injected=false;
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* p)override{if(!n)target=p;return real->SetRenderTarget(n,p);}
    HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* r,DWORD f,D3DCOLOR c,float z,DWORD s)override{clears+=target==ambient&&(f&D3DCLEAR_TARGET);return real->Clear(n,r,f,c,z,s);}
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* p,IDirect3DPixelShader9** out)override{
        if(!injected&&((fault==2&&p==kWorldCompositeHdrShader)||(fault==4&&
           (p==reinterpret_cast<const DWORD*>(kHdrBloomWidePairHorizontal)||p==reinterpret_cast<const DWORD*>(kHdrBloomWideHorizontal))))){injected=true;*out=nullptr;return E_FAIL;}return real->CreatePixelShader(p,out);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* p)override{pixel=p;return real->SetPixelShader(p);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT stride)override;
};
struct PipelineOffTest {
    static bool final(WorldRenderer& w,IDirect3DPixelShader9* p){return p&&(p==w.hdrFinalPS||p==w.hdrBasePS);}
    static std::vector<unsigned char> read(IDirect3DDevice9* d,IDirect3DSurface9* p){D3DSURFACE_DESC desc{};screenOk(p->GetDesc(&desc));IDirect3DSurface9* staging=nullptr;
        screenOk(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&staging,nullptr));screenOk(d->GetRenderTargetData(p,staging));D3DLOCKED_RECT lock{};screenOk(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        const unsigned bytes=desc.Format==D3DFMT_A16B16G16R16F?8:4;std::vector<unsigned char> result(desc.Width*desc.Height*bytes);
        for(unsigned y=0;y<desc.Height;++y)std::memcpy(result.data()+y*desc.Width*bytes,static_cast<char*>(lock.pBits)+y*lock.Pitch,desc.Width*bytes);screenOk(staging->UnlockRect());staging->Release();return result;
    }
    static std::vector<unsigned char> run(Device& d,NeutralMeter& meter,unsigned fault,bool wide,bool aoOn,bool sdr,bool debug){
        constexpr unsigned W=640,H=360;d.clearFrame();++d.frame;auto& w=*d.world;WorldScreenEffectsTest::fixtureContext(w);
        w.quality.gi=w.quality.worldShadows=w.quality.worldFog=w.quality.localLights=w.quality.autoBump=w.quality.equipmentLights=0;
        w.quality.contactAO=aoOn;w.quality.weather=1;w.quality.nightBrightness=w.quality.horizonHaze=0;w.effects={false,false,false};w.pipelineActive=true;w.gpuDiagnosticCaptures=4;
        d.enabled=d.masterEnabled=d.projectionValid=d.terrain=true;d.applied=d.failed=false;d.contactAO=aoOn;d.worldDebug=debug?1:0;
        d.nearZ=1;d.farZ=1000;d.scaleX=d.scaleY=1;d.worldMinDepth=0;d.worldMaxDepth=1;
        d.hdr.active=!sdr;d.hdr.settings.autoExposure=false;d.hdr.settings.legacyExpansion=0;d.hdr.settings.bloom=1;d.hdr.settings.wideBloom=wide;d.hdr.settings.autoPeak=false;
        d.hdrAtUiBoundary=true;d.hdrUiPhase=d.hdrSkyValid=false;
        if(!d.hdrLegacyBack)screenOk(d.ext->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&d.hdrLegacyBack));
        if(!d.hdrPresentBack)screenOk(d.ext->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&d.hdrPresentBack,nullptr));
        assert(d.resources(W,H,D3DFMT_X8R8G8B8));assert(w.resources(W,H,D3DFMT_A16B16G16R16F)&&w.screenReady());
        IDirect3DSurface9* depth=nullptr;screenOk(d.depthTex->GetSurfaceLevel(0,&depth));screenOk(d.ext->SetDepthStencilSurface(depth));screenOk(d.ext->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,.75f,0));screenOk(d.ext->SetDepthStencilSurface(nullptr));depth->Release();d.captured=true;
        screenOk(d.ext->SetRenderTarget(0,d.aoSurface));screenOk(d.ext->Clear(0,nullptr,D3DCLEAR_TARGET,0xffff00ff,1,0));
        screenOk(d.ext->SetRenderTarget(0,d.aoRawSurface));screenOk(d.ext->Clear(0,nullptr,D3DCLEAR_TARGET,0xff00ffff,1,0));
        const auto poison=read(d.ext,d.aoSurface);meter.ambient=d.aoSurface;meter.world=&w;
        screenOk(d.ext->SetRenderTarget(0,d.hdrLegacyBack));D3DVIEWPORT9 vp{0,0,W,H,0,1};screenOk(d.ext->SetViewport(&vp));screenOk(d.ext->Clear(0,nullptr,D3DCLEAR_TARGET,0xff407090,1,0));
        if(fault==1)w.valid=false;if(fault==5)w.failed=true;meter.fault=fault;meter.clears=0;
        screenOk(d.ext->BeginScene());d.renderEffects();screenOk(d.ext->EndScene());assert(d.applied&&!d.failed);
        if(fault==2||fault==3)assert(meter.injected);const bool lazy=!northlightForceNeutralAO&&!aoOn&&!sdr&&!debug&&(fault==0||fault==4);
        assert(meter.clears==unsigned(!aoOn&&!sdr&&!debug&&!lazy));if(lazy)assert(read(d.ext,d.aoSurface)==poison);
        auto result=read(d.ext,sdr||debug?d.hdrLegacyBack:d.hdrPresentBack);
        if(!sdr&&!debug){ // Ensure the shared AO scratch is exercised by nonzero HDR bloom.
            IDirect3DPixelShader9* bright=nullptr;screenOk(d.ext->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&bright));screenOk(d.ext->BeginScene());
            screenOk(NorthlightHdr::quadState(d.ext,d.hdrPresentBack,W,H));screenOk(d.ext->SetPixelShader(bright));const float color[]={4,2,1,.37f};screenOk(d.ext->SetPixelShaderConstantF(0,color,1));screenOk(NorthlightHdr::drawQuad(d.ext,W,H));
            screenOk(d.hdrBegin(false));screenOk(d.ext->EndScene());bright->Release();auto finished=read(d.ext,d.hdrPresentBack);result.insert(result.end(),finished.begin(),finished.end());
            if(fault==4)assert(meter.injected);
        }
        std::printf("NEUTRAL_AO_CASE eager=%u fault=%u wide=%u ao=%u sdr=%u debug=%u clears=%u\n",unsigned(northlightForceNeutralAO),fault,unsigned(wide),unsigned(aoOn),unsigned(sdr),unsigned(debug),meter.clears);
        return result;
    }
};
HRESULT NeutralMeter::DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT stride){auto hr=real->DrawPrimitiveUP(t,n,p,stride);
    if(fault==3&&!injected&&world&&PipelineOffTest::final(*world,pixel)){injected=true;return E_FAIL;}return hr;
}
int main(int argc,char** argv){assert(argc==3);std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightNeutralAO";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=640;pp.BackBufferHeight=360;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* raw=nullptr;screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    for(unsigned scenario=0;scenario<10;++scenario){std::vector<unsigned char> reference;
        for(bool eager:{true,false}){northlightForceNeutralAO=eager;NeutralMeter meter(raw);NorthlightHdr::Presentation presentation;presentation.logical=pp;presentation.settings.requested=false;raw->AddRef();
            auto* d=new Device(&meter,api,presentation);auto pixels=PipelineOffTest::run(*d,meter,scenario<4?scenario:scenario==8?4:scenario==9?5:0,scenario==4||scenario==8,scenario==5,scenario==6,scenario==7);if(eager)reference=pixels;else assert(reference==pixels);d->Release();}
    }
    std::vector<unsigned char> sequence;for(bool eager:{true,false}){northlightForceNeutralAO=eager;NeutralMeter meter(raw);NorthlightHdr::Presentation presentation;presentation.logical=pp;presentation.settings.requested=false;raw->AddRef();auto* d=new Device(&meter,api,presentation);std::vector<unsigned char> pixels;
        for(bool on:{false,true,false}){auto frame=PipelineOffTest::run(*d,meter,0,false,on,false,false);pixels.insert(pixels.end(),frame.begin(),frame.end());}
        if(eager)sequence=pixels;else assert(sequence==pixels);d->Release();}
    raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);std::puts("NEUTRAL_AO_GPU_PASS actualDevice=1 exactPixels=1 poisonedAO=1 earlyAndPartialFallback=1 bloomScratch=1");
}
