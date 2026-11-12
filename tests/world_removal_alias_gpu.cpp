// Actual WorldRenderer lifetime proof; built against dedicated and aliased
// resource headers, then compare every exported stage byte-for-byte.
#define main unused_world_screen_oracle
#include "world_screen_effects_gpu.cpp"
#undef main

struct AliasMeter:WorldScratchMeter {
    IDirect3DPixelShader9 *removal=nullptr,*temporal=nullptr,*bound=nullptr;
    IDirect3DSurface9 *scratch=nullptr,*history=nullptr;
    unsigned frame=0,removals=0,temporalDraws=0,historyDraws=0;bool dedicated=false;
    std::string directory;
    using WorldScratchMeter::WorldScratchMeter;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{
        auto hr=WorldScratchMeter::CreatePixelShader(code,out);if(SUCCEEDED(hr)){
            if(code==kRemovalSmoothShader)removal=*out;if(code==kTemporalLightShader)temporal=*out;
        }return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader)override{bound=shader;return WorldScratchMeter::SetPixelShader(shader);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT n,const void* vertices,UINT stride)override{
        const char* name=nullptr;
        if(bound&&bound==removal){
            IDirect3DSurface9* target=nullptr;screenOk(real->GetRenderTarget(0,&target));
            assert(target!=history);assert(dedicated?target!=scratch:target==scratch);target->Release();++removals;name="removal";
        }else if(bound&&bound==temporal){++temporalDraws;name="temporal";}
        else if(bound&&bound==fogTemporal){++historyDraws;name="fog-temporal";}
        const auto hr=WorldScratchMeter::DrawPrimitiveUP(type,n,vertices,stride);
        if(name&&SUCCEEDED(hr)){
            IDirect3DSurface9* target=nullptr;screenOk(real->GetRenderTarget(0,&target));
            assert(NorthlightWorldDiagnostics::dump(real,target,directory,name,frame));target->Release();
        }
        return hr;
    }
};
#ifndef NORTHLIGHT_REMOVAL_ALIAS_METER_ONLY
int main(int argc,char** argv){
    assert(argc==4);std::setvbuf(stdout,nullptr,_IONBF,0);logFile=stdout;
    std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    const bool dedicated=std::string(argv[3])=="dedicated";
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,256,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=256;pp.BackBufferHeight=160;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* d=nullptr;
    screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    AliasMeter meter(d,256,160);meter.directory=root;meter.dedicated=dedicated;
    auto world=std::make_unique<WorldRenderer>(&meter);
    unsigned allocated=0;
    for(unsigned stage=0;stage<3;++stage){
        unsigned width=stage==1?192:256,height=stage==1?128:160;meter.width=width;meter.height=height;
        // Fail a partial screen allocation, release it, and retry without a
        // double-release of the borrowed smooth pair. Repeat after resize.
        meter.failScreenAt=meter.screenCreates+3;
        assert(!WorldScreenEffectsTest::resources(*world,width,height));WorldScreenEffectsTest::screenRetry(*world);
        meter.failScreenAt=0;const unsigned before=meter.screenCreates;
        WorldScreenEffectsTest::setup(*world,width,height);allocated=meter.screenCreates-before;
        assert(allocated==(dedicated?10u:8u));
        IDirect3DSurface9* out=nullptr;screenOk(d->CreateRenderTarget(width,height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&out,nullptr));
        const float rgb[]={.4f,.3f,.2f,1},ambient[]={0,0,0,1},z[]={1000.f/999.f-1000.f/(999.f*20)};
        auto* scene=screenTexture(d,width,height,rgb);auto* ao=screenTexture(d,width/2,height/2,ambient);auto* depth=screenTexture(d,width,height,z,1);
        for(unsigned frame=0;frame<24;++frame){
            WorldScreenEffectsTest::aliasFixtureFrame(*world,frame);meter.frame=stage*24+frame+1;
            auto owners=WorldScreenEffectsTest::removalScratch(*world);meter.scratch=owners.first;meter.history=owners.second;
            // Vary the receiver depth/silhouette while preserving identical
            // deterministic inputs for both resource-lifetime variants.
            D3DLOCKED_RECT lock{};screenOk(depth->LockRect(0,&lock,nullptr,0));
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){float distance=x<width/2+frame%5?8.f:28.f;float value=1000.f/999.f-1000.f/(999.f*distance);std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&value,4);}screenOk(depth->UnlockRect(0));
            if(frame==6)assert(world->requestNormalGpuDiagnosticCapture());
            assert(renderChecked(*world,d,out,depth,width,height,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,meter.frame));
            assert(NorthlightWorldDiagnostics::dump(d,out,root,"final",meter.frame));
        }
        for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);d->SetPixelShader(nullptr);d->SetRenderTarget(1,nullptr);
        WorldScreenEffectsTest::aliasReleased(*world);scene->Release();ao->Release();depth->Release();out->Release();
    }
    assert(meter.removals>50&&meter.temporalDraws==72&&meter.historyDraws>40);
    std::printf("WORLD_REMOVAL_ALIAS_PASS dedicated=%u frames=72 removal=%u temporal=%u fogTemporal=%u halfFp16Targets=%u partialOOM=3 resets=3 resize=1 diagnostic=3 historyNeverScratch=1 timings=not_measured\n",unsigned(dedicated),meter.removals,meter.temporalDraws,meter.historyDraws,allocated);
    world.reset();d->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
}
#endif
