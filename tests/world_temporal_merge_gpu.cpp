// Exercise actual renderer history/resource ownership, not a reconstructed graph.
#define NORTHLIGHT_TEMPORAL_MERGE_TEST
#define NORTHLIGHT_REMOVAL_ALIAS_METER_ONLY
#include "world_removal_alias_gpu.cpp"

int main(int argc,char** argv){
    assert(argc==4);std::setvbuf(stdout,nullptr,_IONBF,0);logFile=stdout;
    std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    const bool merged=std::string(argv[3])=="merged";
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,256,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=256;pp.BackBufferHeight=160;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* d=nullptr;
    screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    AliasMeter meter(d,256,160);meter.directory=root;meter.refuseMerge=!merged;
    auto world=std::make_unique<WorldRenderer>(&meter);
    WorldScreenEffectsTest::temporalMergeResources(*world,meter);meter.refuseMerge=!merged;meter.merged=nullptr;
    unsigned allocated=0;
    for(unsigned stage=0;stage<3;++stage){
        unsigned width=stage==1?193:256,height=stage==1?129:160;meter.width=width;meter.height=height;
        // Fail a partial screen allocation, release it, and retry without a
        // double-release of the borrowed smooth pair. Repeat after resize.
        meter.failScreenAt=meter.screenCreates+3;
        assert(!WorldScreenEffectsTest::resources(*world,width,height));WorldScreenEffectsTest::screenRetry(*world);
        meter.failScreenAt=0;const unsigned before=meter.screenCreates;
        WorldScreenEffectsTest::setup(*world,width,height);allocated=meter.screenCreates-before;
        assert(allocated==8);
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
        // Failure must restore all native bindings even with histories in new slots.
        WorldScreenEffectsTest::aliasFixtureFrame(*world,0);meter.failTemporalDraw=true;
        IDirect3DSurface9* oldTarget=nullptr;IDirect3DPixelShader9* oldShader=nullptr;
        screenOk(d->GetRenderTarget(0,&oldTarget));screenOk(d->GetPixelShader(&oldShader));
        IDirect3DBaseTexture9* oldTextures[16]={};DWORD oldFilters[16]={};
        for(unsigned i=0;i<16;++i){screenOk(d->GetTexture(i,&oldTextures[i]));screenOk(d->GetSamplerState(i,D3DSAMP_MINFILTER,&oldFilters[i]));}
        assert(!renderChecked(*world,d,out,depth,width,height,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,99));
        IDirect3DSurface9* newTarget=nullptr;IDirect3DPixelShader9* newShader=nullptr;
        screenOk(d->GetRenderTarget(0,&newTarget));screenOk(d->GetPixelShader(&newShader));assert(oldTarget==newTarget&&oldShader==newShader);
        oldTarget->Release();newTarget->Release();if(oldShader)oldShader->Release();if(newShader)newShader->Release();
        for(unsigned i=0;i<16;++i){IDirect3DBaseTexture9* t=nullptr;DWORD filter=0;screenOk(d->GetTexture(i,&t));screenOk(d->GetSamplerState(i,D3DSAMP_MINFILTER,&filter));assert(t==oldTextures[i]&&filter==oldFilters[i]);if(t)t->Release();if(oldTextures[i])oldTextures[i]->Release();}
        assert(!meter.failTemporalDraw&&!world->screenReady());world->recover();
        for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);d->SetPixelShader(nullptr);d->SetRenderTarget(1,nullptr);
        WorldScreenEffectsTest::aliasReleased(*world);meter.merged=nullptr;scene->Release();ao->Release();depth->Release();out->Release();
    }
    assert(meter.temporalDraws==72&&meter.historyDraws>40);
    assert(merged?(meter.mergedDraws>45&&meter.removals==3):(meter.mergedDraws==0&&meter.removals>50));
    std::printf("WORLD_TEMPORAL_MERGE_PASS merged=%u frames=72 removal=%u temporal=%u mergedDraws=%u fogTemporal=%u halfFp16Targets=%u partialOOM=3 reset=3 resize=1 diagnostics=3 optionalCreateFailure=1 failedDrawRestored=1 timings=not_measured\n",unsigned(merged),meter.removals,meter.temporalDraws,meter.mergedDraws,meter.historyDraws,allocated);
    world.reset();d->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
}
