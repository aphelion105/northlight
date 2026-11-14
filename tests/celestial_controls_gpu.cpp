// Real WorldRenderer dispatch, private targets, no game hooks or client memory.
#define NORTHLIGHT_FOG_VOLUME_TEST 1
#define main unused_world_screen_main
#include "world_screen_effects_gpu.cpp"
#undef main

struct ControlMeter:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    std::map<IDirect3DPixelShader9*,std::string> names;IDirect3DPixelShader9* bound=nullptr;
    std::map<std::string,unsigned> draws;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{
        const auto hr=real->CreatePixelShader(code,out);if(SUCCEEDED(hr)){
            const DWORD* codes[]={kWorldAOCompositeHdrShader,kWorldBaseCompositeHdrShader,kWorldFogShader,kWorldFogAmbientShader,kVolumeInjectShader,kVolumeInjectQuietShader,kVolumeInjectAmbientShader,kVolumeInjectAmbientQuietShader};
            const char* labels[]={"ao-base","base","fog-direct","fog-ambient","volume-direct","volume-direct","volume-ambient","volume-ambient"};
            for(unsigned i=0;i<8;++i)if(code==codes[i])names[*out]=labels[i];
        }return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* p)override{bound=p;return real->SetPixelShader(p);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT stride)override{
        auto found=names.find(bound);if(found!=names.end())++draws[found->second];
        if(std::getenv("NORTHLIGHT_CONTROLS_TIMING")&&found!=names.end()&&(found->second=="base"||found->second=="ao-base")){
            IDirect3DQuery9 *frequency=nullptr,*begin=nullptr,*end=nullptr,*disjoint=nullptr;
            assert(SUCCEEDED(real->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency))&&SUCCEEDED(real->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin))&&
                   SUCCEEDED(real->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end))&&SUCCEEDED(real->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint)));
            assert(SUCCEEDED(disjoint->Issue(D3DISSUE_BEGIN)));assert(SUCCEEDED(frequency->Issue(D3DISSUE_END)));assert(SUCCEEDED(begin->Issue(D3DISSUE_END)));
            for(unsigned i=0;i<64;++i)assert(SUCCEEDED(real->DrawPrimitiveUP(t,n,p,stride)));
            assert(SUCCEEDED(end->Issue(D3DISSUE_END)));assert(SUCCEEDED(disjoint->Issue(D3DISSUE_END)));
            auto read=[](IDirect3DQuery9* q,void* value,unsigned bytes){HRESULT hr=S_FALSE;for(unsigned i=0;i<10000&&hr==S_FALSE;++i){hr=q->GetData(value,bytes,D3DGETDATA_FLUSH);if(hr==S_FALSE)Sleep(1);}assert(hr==S_OK);};
            UINT64 hz=0,a=0,b=0;BOOL invalid=FALSE;read(frequency,&hz,sizeof hz);read(begin,&a,sizeof a);read(end,&b,sizeof b);read(disjoint,&invalid,sizeof invalid);assert(!invalid&&hz&&b>=a);
            std::printf("AO_BASE_GPU variant=%s draws=64 msPerDraw=%.6f contended=1\n",found->second.c_str(),double(b-a)*1000/double(hz)/64);
            frequency->Release();begin->Release();end->Release();disjoint->Release();return D3D_OK;
        }
        return real->DrawPrimitiveUP(t,n,p,stride);
    }
};
struct PipelineOffTest {
    static void setup(WorldRenderer& w,unsigned width,unsigned height,bool reference,unsigned sample){
        WorldScreenEffectsTest::setup(w,width,height);w.effects={false,false,false};
        w.quality.localLights=w.quality.weather=w.quality.autoBump=0;w.quality.contactAO=1;
        w.hdrBaseAllowed=true;w.hdrAOBaseTried=reference;
        w.celestialValid=true;w.celestial.dayFraction=sample&1?0.f:.5f;
        w.quality.nightBrightness=sample&1?100:0;
        w.legacyFogKnown=sample&2;w.legacyFog.parameters[0]=1;w.legacyFog.parameters[1]=10;w.legacyFog.parameters[2]=.02f;w.legacyFog.parameters[3]=1;
        w.legacyFog.color[0]=.1f;w.legacyFog.color[1]=.2f;w.legacyFog.color[2]=.3f;
    }
    static void fog(WorldRenderer& w,unsigned type,bool rays){
        w.effects={false,false,true};w.quality.godrays=rays;w.quality.fogClouds=0;
        w.sourceWeights[0]=w.sourceWeights[1]=.5f;w.sourceColors[1]=w.sourceGlowColors[1]={.3f,.4f,.5f};
        if(type){
            WorldScreenEffectsTest::volumeFixture(w,type==3?18:0);w.quality.godrays=rays;
            w.sourceWeights[0]=w.sourceWeights[1]=.5f;w.sourceColors[1]=w.sourceGlowColors[1]={.3f,.4f,.5f};
            auto& layer=w.skyEnvironment.layer[0];layer.flag[0]=1;layer.p[2]=1;layer.p[9]=12;layer.p[10]=.6f;
            layer.color[0][0]=layer.color[0][1]=layer.color[0][2]=1;
            if(type==1)w.fogVolume.refused=true; // Legacy raymarch fallback.
        }
    }
    static void liveToggle(WorldRenderer& w){
        w.fogHistoryValid=w.fogVolume.valid=true;
        NorthlightSettings::Config next;next.quality=w.quality;next.celestial=w.celestialProfiles;
        next.quality.godrays=!next.quality.godrays;w.applyLiveSettings(next);
        assert(!w.fogHistoryValid&&!w.fogVolume.valid);
    }
};

int main(int argc,char** argv){
    assert(argc==3);const unsigned W=std::getenv("NORTHLIGHT_CONTROLS_TIMING")?3840:256,H=std::getenv("NORTHLIGHT_CONTROLS_TIMING")?2160:160;
    const unsigned msaa=std::getenv("NORTHLIGHT_TEST_MSAA")?unsigned(std::atoi(std::getenv("NORTHLIGHT_TEST_MSAA"))):0;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightControlsOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *out=nullptr,*readback=nullptr,*resolved=nullptr;screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&out,nullptr));screenOk(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    if(msaa)screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
    const float rgb[]={.4f,.3f,.2f,1},ambient[]={0,0,0,1},z[]={.99f};
    auto* scene=screenTexture(d,W,H,rgb);auto* ao=screenTexture(d,W/2,H/2,ambient);auto* depth=screenTexture(d,W,H,z,1);
    auto vary=[&](IDirect3DTexture9* t,unsigned channels){D3DSURFACE_DESC desc{};screenOk(t->GetLevelDesc(0,&desc));D3DLOCKED_RECT l{};screenOk(t->LockRect(0,&l,nullptr,0));
        for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x){auto* p=reinterpret_cast<float*>(static_cast<char*>(l.pBits)+y*l.Pitch)+x*channels;
            if(channels==1)p[0]=x<desc.Width/3?.8f:x<desc.Width*2/3?.99f:1.f;
            else if(t==ao)p[3]=float((x*7+y*11)%17)/16;
            else{p[0]=float(x%31)/30;p[1]=float(y%23)/22;p[2]=float((x+y)%13)/12;p[3]=float(x%7)/6;}}
        screenOk(t->UnlockRect(0));};vary(scene,4);vary(ao,4);vary(depth,1);
    auto pixels=[&]{if(resolved)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));screenOk(d->GetRenderTargetData(resolved?resolved:out,readback));D3DLOCKED_RECT l{};screenOk(readback->LockRect(&l,nullptr,D3DLOCK_READONLY));std::vector<unsigned short> p(W*H*4);
        for(unsigned y=0;y<H;++y)std::memcpy(p.data()+y*W*4,static_cast<char*>(l.pBits)+y*l.Pitch,W*8);screenOk(readback->UnlockRect());return p;};
    unsigned differing=0;float peak=0;
    for(unsigned sample=0;sample<4;++sample){std::vector<unsigned short> reference;
        for(unsigned variant=0;variant<2;++variant){ControlMeter meter(d);auto w=std::make_unique<WorldRenderer>(&meter);PipelineOffTest::setup(*w,W,H,variant==0,sample);
            assert(renderChecked(*w,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,sample,scene));
            auto result=pixels();assert(meter.draws[variant?"ao-base":"base"]==1&&meter.draws[variant?"base":"ao-base"]==0);
            if(!variant)reference=result;else for(size_t i=0;i<result.size();++i){const float a=screenHalf(reference[i]),b=screenHalf(result[i]);assert(std::isfinite(a)&&std::isfinite(b));
                const float delta=std::abs(a-b);peak=std::max(peak,delta);differing+=result[i]!=reference[i];assert(std::abs(int(result[i])-int(reference[i]))<=1&&delta<=.001f);}
        }
    }
    std::printf("AO_BASE_PARITY_PASS cases=4 msaa=%u differing=%u maxError=%g\n",msaa,differing,peak);
    for(unsigned type=0;type<4;++type){std::vector<unsigned char> reference;
        for(unsigned rays=0;rays<2;++rays){ControlMeter meter(d);auto w=std::make_unique<WorldRenderer>(&meter);PipelineOffTest::setup(*w,W,H,false,0);PipelineOffTest::fog(*w,type,rays);
            assert(renderChecked(*w,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,0,scene));
            auto result=WorldScreenEffectsTest::fogImage(*w,false,true);
            if(!rays)reference=result;else{assert(reference.size()==result.size());float lightDelta=0;
                for(size_t i=0;i<result.size();i+=2){unsigned short a,b;std::memcpy(&a,reference.data()+i,2);std::memcpy(&b,result.data()+i,2);assert(std::isfinite(screenHalf(a))&&std::isfinite(screenHalf(b)));
                    if(i%8==6)assert(a==b);else lightDelta=std::max(lightDelta,screenHalf(b)-screenHalf(a));}std::printf("FOG_DELTA type=%u delta=%g bytes=%zu\n",type,lightDelta,result.size());assert(lightDelta>.0001f);}
            if(!rays){assert(meter.draws["fog-direct"]==0&&meter.draws["volume-direct"]==0);if(type>=2)assert(meter.draws["volume-ambient"]==(type==3?2u:1u));}
            PipelineOffTest::liveToggle(*w);
        }
    }
    std::printf("GODRAYS_PASS mediumAlpha=exact regional=1 importedFallback=1 atlas=1 noise=1 disabledDirectionalDraws=0 liveHistoryReset=1 msaa=%u\n",msaa);
    scene->Release();ao->Release();depth->Release();out->Release();if(resolved)resolved->Release();readback->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
