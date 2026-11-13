// Same surface and lamps through the actual 8/16-light batching and compositor.
#define main unused_screen_effects_main
#include "world_screen_effects_gpu.cpp"
#undef main

struct LampConstantMeter:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    WorldRenderer* world=nullptr;IDirect3DPixelShader9* bound=nullptr;
    unsigned wide=0,narrow=0,composites=0;bool poison=false;
    float tuning[4]={};
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader)override{bound=shader;return real->SetPixelShader(shader);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT n,const void* vertices,UINT stride)override;
};
struct PipelineOffTest {
    static unsigned stage(const WorldRenderer& w,IDirect3DPixelShader9* p){
        if(!p)return 0;
        if(p==w.localDirect16PS)return 1;
        if(p==w.localDirectPS)return 2;
        if(p==w.finalPS||p==w.hdrFinalPS||p==w.hdrCombinedPS||p==w.materialPS||p==w.hdrMaterialPS)return 3;
        return 0;
    }
    static void setup(WorldRenderer& w,unsigned W,unsigned H,bool wide,IDirect3DTexture9* detail){
        WorldScreenEffectsTest::fixtureContext(w);w.pipelineActive=true;
        w.effects={false,false,false};w.quality.effects=w.quality.localLights=1;
        w.quality.localLightLimit=16;w.quality.shadowStrength=125;w.quality.shadowSoftness=50;w.quality.shadowContactRadius=100;
        w.quality.gi=w.quality.worldShadows=w.quality.worldFog=w.quality.contactAO=w.quality.autoBump=w.quality.weather=w.quality.equipmentLights=0;
        w.quality.pointShadows=w.quality.nightBrightness=w.quality.horizonHaze=w.quality.fogClouds=0;
        // A dark replacement of the native directional light reaches the removal
        // floor. The 16 selected lamps are visible but do not reach this surface.
        w.sourceColors[0]=w.sourceGlowColors[0]={0,0,0};
        for(unsigned c=0;c<3;++c){w.context.direct[c]=1;w.context.ambient[c]=.05f;}
        w.frames=7;w.gpuDiagnosticCaptures=4;w.lastRequest.map="OfflineLampConstants";
        w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map=w.lastRequest.map;
        auto& lights=w.active->localLights;lights.resize(16);
        for(unsigned i=0;i<lights.size();++i){auto& l=lights[i];l.sourceId=i+1;l.kind=2;l.position[2]=20;
            l.diffuse[0]=1;l.diffuse[1]=.47f;l.diffuse[2]=.16f;l.attenuationStart=1;l.attenuationEnd=40;}
        w.setMaterialDetail(detail,1,48,nullptr,8);
        assert(w.resources(W,H,D3DFMT_A16B16G16R16F,w.frameRequirements(detail!=nullptr)));
        // Warm resources retain the production fallback to two ordinary batches.
        if(!wide)drop(w.localDirect16PS);
    }
    static unsigned selected(const WorldRenderer& w){return w.localDirectCount;}
};
HRESULT LampConstantMeter::DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT n,const void* vertices,UINT stride){
    const auto pass=world?PipelineOffTest::stage(*world,bound):0;
    wide+=pass==1;narrow+=pass==2;
    if(pass==3){
        ++composites;if(!poison||composites==1)screenOk(real->GetPixelShaderConstantF(115,tuning,1));
        if(poison){const float lamp[]={1,.47f,.16f,1.f/39};screenOk(real->SetPixelShaderConstantF(115,lamp,1));}
    }
    return real->DrawPrimitiveUP(type,n,vertices,stride);
}
static std::vector<float> readImage(IDirect3DDevice9* d,IDirect3DSurface9* out,IDirect3DSurface9* read,unsigned W,unsigned H){
    screenOk(d->GetRenderTargetData(out,read));D3DLOCKED_RECT lock{};screenOk(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<float> image;image.reserve(W*H*3);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned c=0;c<3;++c){unsigned short bits;
        std::memcpy(&bits,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8+c*2,2);const float v=screenHalf(bits);assert(std::isfinite(v));image.push_back(v);}
    screenOk(read->UnlockRect());return image;
}
int main(int argc,char** argv){
    assert(argc==3);constexpr unsigned W=128,H=80;std::setvbuf(stdout,nullptr,_IONBF,0);
    std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightLampConstants";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *out=nullptr,*read=nullptr;screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&out,nullptr));screenOk(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&read,nullptr));
    const float rgb[]={.6f,.4f,.3f,1},z[]={1000.f/999.f-1000.f/(999.f*100)},normal[]={.5f,.5f,100,1};
    auto* scene=screenTexture(d,W,H,rgb);auto* depth=screenTexture(d,W,H,z,1);
    auto* detail=screenTexture(d,W,H,normal);
    unsigned failures=0;
    for(unsigned scenario=0;scenario<5;++scenario){
        const bool hdr=scenario==1||scenario>=3,material=scenario>=2,combined=scenario==3;
        std::vector<float> images[3];
        for(unsigned mode=0;mode<3;++mode){
            LampConstantMeter meter(d);WorldRenderer world(&meter);meter.world=&world;meter.poison=mode==2;
            PipelineOffTest::setup(world,W,H,mode!=0,material?detail:nullptr);
            assert(renderChecked(world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,nullptr,7,hdr?scene:nullptr,combined));
            assert(PipelineOffTest::selected(world)==16&&meter.composites==(material&&!combined?2u:1u));
            assert(mode==0?meter.narrow==2&&meter.wide==0:meter.wide==1&&meter.narrow==0);
            const float expected[]={1.25f,.5f,1,1};bool valid=true;for(unsigned c=0;c<4;++c)valid&=std::fabs(meter.tuning[c]-expected[c])<1e-6f;
            failures+=!valid;
            std::printf("LAMP_CONSTANTS scenario=%u mode=%u wide=%u narrow=%u c115=%.6f,%.6f,%.6f,%.6f valid=%u\n",scenario,mode,meter.wide,meter.narrow,meter.tuning[0],meter.tuning[1],meter.tuning[2],meter.tuning[3],unsigned(valid));
            images[mode]=readImage(d,out,read,W,H);
        }
        unsigned changed=0,control=0;float maximum=0;
        for(size_t i=0;i<images[0].size();++i){const float delta=std::fabs(images[0][i]-images[1][i]);maximum=std::max(maximum,delta);changed+=delta>1e-3f;control+=std::fabs(images[0][i]-images[2][i])>.01f;}
        std::printf("LAMP_IMAGE scenario=%u hdr=%u material=%u combined=%u changed=%u maxDifference=%.6f negativeControl=%u\n",scenario,unsigned(hdr),unsigned(material),unsigned(combined),changed,maximum,control);
        failures+=changed!=0;failures+=control==0;
    }
    for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);
    scene->Release();depth->Release();detail->Release();out->Release();read->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::printf("LAMP_CONSTANTS_GPU failures=%u actualWorldRenderer=1\n",failures);return failures?1:0;
}
