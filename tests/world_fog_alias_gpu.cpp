// Reuse the full renderer fixture and per-pass image captures.
#define NORTHLIGHT_REMOVAL_ALIAS_METER_ONLY
#include "world_removal_alias_gpu.cpp"
struct FogAliasMeter:AliasMeter {
    using AliasMeter::AliasMeter;
    WorldRenderer* world=nullptr;bool ownFog=false;
    IDirect3DPixelShader9 *local=nullptr,*combine=nullptr;
    unsigned localDraws=0,combines=0,clipped=0;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{
        const auto hr=AliasMeter::CreatePixelShader(code,out);if(SUCCEEDED(hr)){
            if(code==kLocalFogShader)local=*out;if(code==kLocalFogCombineShader)combine=*out;
        }return hr;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* vertices,UINT stride)override{
        const bool accumulating=bound&&bound==local,combining=bound&&bound==combine;
        if(accumulating||combining){
            const auto pair=WorldScreenEffectsTest::lightScratch(*world);
            IDirect3DSurface9* target=nullptr;screenOk(real->GetRenderTarget(0,&target));assert(target!=pair.second);
            IDirect3DTexture9* retired=nullptr;screenOk(pair.first->GetContainer(IID_IDirect3DTexture9,reinterpret_cast<void**>(&retired)));
            if(accumulating){
                assert(ownFog?target!=pair.first:target==pair.first);++localDraws;
                for(unsigned sampler=0;sampler<16;++sampler){IDirect3DBaseTexture9* texture=nullptr;screenOk(real->GetTexture(sampler,&texture));assert(texture!=retired);if(texture)texture->Release();}
                DWORD enabled=0;screenOk(real->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled));RECT rect{};screenOk(real->GetScissorRect(&rect));
                clipped+=enabled&&(rect.right-rect.left<long(width/2)||rect.bottom-rect.top<long(height/2));
            }else{
                IDirect3DBaseTexture9* texture=nullptr;screenOk(real->GetTexture(9,&texture));assert(texture);assert(ownFog?texture!=retired:texture==retired);texture->Release();++combines;
            }
            retired->Release();target->Release();
        }
        const auto hr=AliasMeter::DrawPrimitiveUP(type,count,vertices,stride);
        if(combining&&SUCCEEDED(hr)){IDirect3DSurface9* target=nullptr;screenOk(real->GetRenderTarget(0,&target));assert(NorthlightWorldDiagnostics::dump(real,target,directory,"fog-after-combine",frame));target->Release();}
        return hr;
    }
};
int main(int argc,char** argv){
    assert(argc==4);std::setvbuf(stdout,nullptr,_IONBF,0);logFile=stdout;
    const std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,256,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=256;pp.BackBufferHeight=160;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* d=nullptr;
    screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    FogAliasMeter meter(d,256,160);meter.directory=root;meter.ownFog=std::string(argv[3])=="dedicated";
    auto world=std::make_unique<WorldRenderer>(&meter);meter.world=world.get();unsigned allocated=0;
    for(unsigned stage=0;stage<3;++stage){
        const unsigned width=stage==1?193:256,height=stage==1?129:160;meter.width=width;meter.height=height;
        meter.failScreenAt=meter.screenCreates+3;assert(!WorldScreenEffectsTest::resources(*world,width,height));WorldScreenEffectsTest::screenRetry(*world);
        meter.failScreenAt=0;const auto before=meter.screenCreates;WorldScreenEffectsTest::setup(*world,width,height);allocated=meter.screenCreates-before;
        assert(allocated==(meter.ownFog?9u:8u));
        IDirect3DSurface9* out=nullptr;screenOk(d->CreateRenderTarget(width,height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&out,nullptr));
        const float rgb[]={.4f,.3f,.2f,1},ambient[]={0,0,0,1},z[]={1000.f/999.f-1000.f/(999.f*20)};
        auto* scene=screenTexture(d,width,height,rgb);auto* ao=screenTexture(d,width/2,height/2,ambient);auto* depth=screenTexture(d,width,height,z,1);
        for(unsigned frame=0;frame<36;++frame){
            WorldScreenEffectsTest::fogAliasFixtureFrame(*world,frame);meter.frame=stage*36+frame+1;
            if(frame==8)continue; // Old history must survive a frame without a world render.
            const auto owners=WorldScreenEffectsTest::removalScratch(*world);meter.scratch=owners.first;meter.history=owners.second;
            assert(renderChecked(*world,d,out,depth,width,height,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,meter.frame,frame%2?scene:nullptr));
            assert(NorthlightWorldDiagnostics::dump(d,out,root,"final",meter.frame));
        }
        for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);d->SetPixelShader(nullptr);d->SetRenderTarget(1,nullptr);
        WorldScreenEffectsTest::aliasReleased(*world);scene->Release();ao->Release();depth->Release();out->Release();
    }
    assert(meter.temporalDraws==105&&meter.localDraws>90&&meter.combines>30&&meter.clipped>20);
    std::printf("WORLD_FOG_ALIAS_PASS ownFog=%u frames=105 localDraws=%u combines=%u clipped=%u halfFp16Targets=%u partialOOM=3 resets=3 oddResize=1 HDR=1 fogTemporalFallback=1 historyNeverScratch=1 timings=not_measured\n",unsigned(meter.ownFog),meter.localDraws,meter.combines,meter.clipped,allocated);
    world.reset();d->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
}
