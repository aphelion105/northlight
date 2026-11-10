// Exercise the actual shared compositor; no AO-specific compositor or game.
#define main unused_screen_effects_main
#include "world_screen_effects_gpu.cpp"
#undef main
#include <chrono>

struct LayerMeter:ForwardIDirect3DDevice9 {
    WorldRenderer* observed=nullptr;IDirect3DPixelShader9* bound=nullptr;
    unsigned passes[5]={};
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader)override{bound=shader;return real->SetPixelShader(shader);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT n,const void* vertices,UINT stride)override;
};
struct PipelineOffTest {
    static unsigned stage(const WorldRenderer& w,IDirect3DPixelShader9* p){
        if(p==w.normalsPS||p==w.normalsNoDistancePS)return 0;
        if(p==w.lightingPS||p==w.lightingUnshadowedPS||p==w.lightingUnshadowedGIPS)return 1;
        if(p==w.removalPS)return 2;if(p==w.temporalPS)return 3;
        return 4;
    }
    static void policy(Device& d){
        auto& w=*d.world;w.quality.effects=w.quality.contactAO=1;
        w.quality.gi=w.quality.worldShadows=w.quality.worldFog=w.quality.localLights=w.quality.autoBump=w.quality.weather=0;
        for(unsigned bit=0;bit<7;++bit){
            w.effects={bit==1,bit==2,bit==3};w.quality.localLights=bit==4;w.quality.localLightLimit=8;w.quality.autoBump=bit==5;w.quality.weather=bit==6;
            d.latchDrawGates();assert(d.modelNormalsNeeded==(bit>0&&bit<6));
            if(!d.modelNormalsNeeded){NorthlightAutoDisplacement::Args args{};
                // An invalid sentinel must never be inspected or passed to capture.
                for(unsigned i=0;i<1024;++i)d.captureModelNormals(reinterpret_cast<IDirect3DVertexShader9*>(1),[]{assert(false);return E_FAIL;},false,args);
            }
        }
        w.effects={false,false,false};w.quality.autoBump=w.quality.localLights=w.quality.weather=0;
        d.worldDebug=1;d.latchDrawGates();assert(d.modelNormalsNeeded);d.worldDebug=0;d.latchDrawGates();assert(!d.modelNormalsNeeded);
        w.gpuNormalDiagnosticRequested=true;d.latchDrawGates();assert(d.modelNormalsNeeded);w.gpuNormalDiagnosticRequested=false;d.latchDrawGates();assert(!d.modelNormalsNeeded);
        std::puts("OPTIONAL_MODEL_POLICY_PASS aoOnlyNoCapture=1 consumersResume=1 weatherIndependent=1 debugRetained=1");
    }
    static void configure(WorldRenderer& w,unsigned mode){
        WorldScreenEffectsTest::fixtureContext(w);w.pipelineActive=true;
        w.quality.gi=w.quality.worldShadows=w.quality.worldFog=w.quality.localLights=w.quality.autoBump=w.quality.weather=w.quality.equipmentLights=0;
        w.quality.contactAO=1;w.quality.nightBrightness=w.quality.horizonHaze=w.quality.fogClouds=0;
        w.quality.localLightLimit=8;w.quality.localLights=mode==4;w.quality.autoBump=mode==5;w.quality.weather=mode==6;
        w.effects={mode==1,mode==2,mode==3};w.lastRequest.map="OfflineOptionalLayers";
        w.frames=7;w.temporalValid=false;w.gpuDiagnosticCaptures=4;
    }
    static void render(WorldRenderer& w,IDirect3DDevice9* d,IDirect3DSurface9* out,IDirect3DTexture9* depth,IDirect3DTexture9* scene,IDirect3DTexture9* ao,IDirect3DTexture9* water,unsigned W,unsigned H,bool hdr){
        assert(SUCCEEDED(d->BeginScene()));
        assert(w.render(out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,water,scene,ao,7,hdr?scene:nullptr));
        assert(SUCCEEDED(d->EndScene())&&w.composited&&!w.failed);
    }
    static void checks(WorldRenderer& w,LayerMeter& meter,IDirect3DSurface9* out,IDirect3DSurface9* read,IDirect3DTexture9* depth,IDirect3DTexture9* scene,IDirect3DTexture9* ao,IDirect3DTexture9* water,unsigned W,unsigned H){
        for(bool hdr:{false,true})for(unsigned mode:{0u,1u,0u,2u,0u,3u,0u,4u,0u,5u,0u,6u,0u}){
            configure(w,mode);std::fill(std::begin(meter.passes),std::end(meter.passes),0);
            render(w,meter.real,out,depth,scene,ao,nullptr,W,H,hdr);
#ifndef NORTHLIGHT_OPTIONAL_LAYER_REFERENCE
            if(mode==0||mode==5||mode==6){assert(!meter.passes[0]&&!meter.passes[1]&&!meter.passes[2]&&!meter.passes[3]&&meter.passes[4]==1&&!w.temporalValid);}
            else assert(meter.passes[0]&&meter.passes[1]&&meter.passes[3]);
#endif
        }
#ifndef NORTHLIGHT_OPTIONAL_LAYER_REFERENCE
        for(bool hdr:{false,true})for(bool liquid:{false,true}){
            configure(w,0);render(w,meter.real,out,depth,scene,ao,liquid?water:nullptr,W,H,hdr);
            assert(SUCCEEDED(meter.real->GetRenderTargetData(out,read)));D3DLOCKED_RECT lock{};assert(SUCCEEDED(read->LockRect(&lock,nullptr,D3DLOCK_READONLY)));
            const float color[]={.5f,.3f,.2f};
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned c=0;c<3;++c){unsigned short bits;std::memcpy(&bits,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8+c*2,2);
                const float v=screenHalf(bits),expected=color[c]*(liquid?1.f:.6f);assert(std::isfinite(v)&&std::fabs(v-expected)<.006f);}
            assert(SUCCEEDED(read->UnlockRect()));
        }
#endif
        std::puts("OPTIONAL_SHARED_COMPOSITOR_PASS modes=7 hdr=2 switches=26 oneComposite=1 aoAndWater=1");
    }
};
HRESULT LayerMeter::DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT n,const void* vertices,UINT stride){if(observed)++passes[PipelineOffTest::stage(*observed,bound)];return real->DrawPrimitiveUP(type,n,vertices,stride);}
static void await(IDirect3DQuery9* q,void* p,DWORD n){const DWORD start=GetTickCount();HRESULT h;while((h=q->GetData(p,n,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-start<30000);Sleep(0);}assert(h==S_OK);}
int main(int argc,char** argv){
    assert(argc==6);const unsigned W=unsigned(std::atoi(argv[3])),H=unsigned(std::atoi(argv[4]));const bool bench=std::atoi(argv[5])!=0;
    std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightOptionalLayers";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    LayerMeter meter(d);NorthlightHdr::Presentation presentation;presentation.logical=pp;presentation.settings.requested=false;d->AddRef();
    auto* device=new Device(&meter,api,presentation);PipelineOffTest::policy(*device);device->Release();
    IDirect3DSurface9 *out=nullptr,*read=nullptr;screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&out,nullptr));screenOk(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&read,nullptr));
    const float color[]={.5f,.3f,.2f,1},rawDepth[]={.75f},ambient[]={0,0,0,.6f},liquid[]={2,1,0,0};
    auto scene=screenTexture(d,W,H,color),depth=screenTexture(d,W,H,rawDepth,1),ao=screenTexture(d,W/2,H/2,ambient),water=screenTexture(d,W,H,liquid);
    {
        WorldRenderer world(&meter);meter.observed=&world;
        PipelineOffTest::checks(world,meter,out,read,depth,scene,ao,water,W,H);
        if(bench){IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
            screenOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));screenOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));screenOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));screenOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
            for(unsigned block=0;block<12;++block){PipelineOffTest::configure(world,0);meter.observed=nullptr;
                screenOk(disjoint->Issue(D3DISSUE_BEGIN));screenOk(frequency->Issue(D3DISSUE_END));screenOk(start->Issue(D3DISSUE_END));const auto begin=std::chrono::steady_clock::now();
                for(unsigned i=0;i<32;++i)PipelineOffTest::render(world,d,out,depth,scene,ao,nullptr,W,H,false);
                const double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/32;
                screenOk(end->Issue(D3DISSUE_END));screenOk(disjoint->Issue(D3DISSUE_END));UINT64 a=0,b=0,f=0;BOOL bad=FALSE;await(end,&b,sizeof b);await(start,&a,sizeof a);await(frequency,&f,sizeof f);await(disjoint,&bad,sizeof bad);assert(!bad&&f&&b>=a);
                std::printf("OPTIONAL_LAYER_TIMING warm=%u width=%u height=%u cpuMs=%.9f gpuMs=%.9f frames=32\n",unsigned(block<4),W,H,cpu,double(b-a)*1000/f/32);
            }
            start->Release();end->Release();frequency->Release();disjoint->Release();
        }
        meter.observed=nullptr;
    }
    for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);scene->Release();depth->Release();ao->Release();water->Release();out->Release();read->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::puts("OPTIONAL_LAYERS_GPU_PASS actualWorldRenderer=1 actualDevicePolicy=1");
}
