// Compare standalone point corrections with a consecutive same-input batch.
#define NOMINMAX
#include "renderer.cpp"
#include <cassert>
#include <chrono>
static void batchOk(HRESULT hr){assert(SUCCEEDED(hr));}
struct PointBatchMeter:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    unsigned samplers=0,textures=0,draws=0,fault=0;
    bool tracing=false,failSoftCreate=false;
    uint64_t sequence=0;
    unsigned commands=0;
    std::unordered_map<const void*,unsigned> resources;
    void beginTrace(std::initializer_list<const void*> bindings){
        tracing=true;sequence=14695981039346656037ull;commands=0;resources.clear();
        unsigned id=0;for(auto* resource:bindings){assert(resource);resources.emplace(resource,++id);}
    }
    unsigned binding(const void* resource)const{return !tracing||!resource?0:resources.at(resource);}
    void words(std::initializer_list<unsigned> fields){
        if(tracing){++commands;for(unsigned value:fields)sequence=(sequence^value)*1099511628211ull;}
    }
    static uint64_t bytes(uint64_t hash,const void* data,size_t size){
        auto* p=static_cast<const unsigned char*>(data);
        for(size_t i=0;i<size;++i)hash=(hash^p[i])*1099511628211ull;
        return hash;
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{
        if(failSoftCreate&&code==kLocalLightingPcssShader){failSoftCreate=false;*out=nullptr;return E_OUTOFMEMORY;}
        return real->CreatePixelShader(code,out);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* s)override{words({1,binding(s)});return real->SetPixelShader(s);}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT first,const float* p,UINT n)override{words({2,first,n});if(tracing)sequence=bytes(sequence,p,n*16);
        if(fault==1&&first==12){fault=0;return E_FAIL;}return real->SetPixelShaderConstantF(first,p,n);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD i,D3DSAMPLERSTATETYPE s,DWORD v)override{words({3,i,unsigned(s),v});++samplers;if(fault==3&&i==5){fault=0;return E_FAIL;}return real->SetSamplerState(i,s,v);}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD i,IDirect3DBaseTexture9* t)override{words({4,i,binding(t)});++textures;if(fault==2&&i==5){fault=0;return E_FAIL;}return real->SetTexture(i,t);}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD i,IDirect3DSurface9* t)override{words({5,i,binding(t)});return real->SetRenderTarget(i,t);}
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE s,DWORD v)override{words({6,unsigned(s),v});return real->SetRenderState(s,v);}
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* v)override{words({7,v->X,v->Y,v->Width,v->Height});return real->SetViewport(v);}
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* r)override{words({8,unsigned(r->left),unsigned(r->top),unsigned(r->right),unsigned(r->bottom)});return real->SetScissorRect(r);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT s)override{words({9,unsigned(t),n,s});++draws;return real->DrawPrimitiveUP(t,n,p,s);}
};
static IDirect3DTexture9* pointTexture(IDirect3DDevice9* d,unsigned w,unsigned h,unsigned channels,const std::vector<float>& data){
    IDirect3DTexture9* t=nullptr;batchOk(d->CreateTexture(w,h,1,0,channels==1?D3DFMT_R32F:D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&t,nullptr));D3DLOCKED_RECT lock{};batchOk(t->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<h;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,data.data()+size_t(y)*w*channels,w*channels*4);batchOk(t->UnlockRect(0));return t;
}
static uint64_t pointQuery(IDirect3DQuery9* q){uint64_t v=0;HRESULT hr;while((hr=q->GetData(&v,sizeof v,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();batchOk(hr);return v;}
struct WorldScreenEffectsTest {
    static void run(PointBatchMeter& meter,bool timing){
        auto* d=&meter;unsigned cases=0;const unsigned W=timing?1920:130,H=timing?1080:98;
        WorldRenderer w(d);w.valid=true;assert(w.resources(W,H,D3DFMT_A16B16G16R16F));
        w.projection[0]=w.projection[1]=w.projection[2]=1;for(unsigned i=0;i<4;++i)w.context.inverseView[i*5]=1;
        std::vector<float> normal(size_t(W/2)*(H/2)*4);for(size_t i=0;i<normal.size();i+=4){normal[i+2]=-1;normal[i+3]=1;}
        drop(w.worldNormals.texture_);w.worldNormals.texture_=pointTexture(d,W/2,H/2,4,normal);
        auto* depth=pointTexture(d,W,H,1,std::vector<float>(size_t(W)*H,(64.f/(64-.1f))*(1-.1f/5)));
        auto* water=pointTexture(d,1,1,4,{9,1,0,0});auto* detail=pointTexture(d,1,1,4,{.5f,.5f,1,1});
        w.pointIndex=0;w.pointSlot().selected.attenuationEnd=64;assert(w.pointResources()&&w.pointLighting.ensureSoft(d,[] {}));
        for(unsigned face=0;face<6;++face){batchOk(d->SetRenderTarget(0,w.pointSlot().faces[face]));batchOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));}
        batchOk(d->SetRenderTarget(0,w.pointSlot().faces[D3DCUBEMAP_FACE_POSITIVE_Z]));const D3DRECT half={0,0,128,256};batchOk(d->Clear(1,&half,D3DCLEAR_TARGET,D3DCOLOR_COLORVALUE(.97f,.97f,.97f,1),1,0));
        for(unsigned i=0;i<64;++i){auto& slot=w.pointTargets.slots()[i];if(i){slot.cube=w.pointTargets.slots()[0].cube;slot.cube->AddRef();}slot.ready=true;slot.selected.attenuationEnd=64;slot.selected.position[0]=float(int(i%3)-1)*.2f;slot.selected.diffuse[0]=.1f+float(i%3)*.1f;slot.selected.diffuse[1]=.2f;slot.selected.diffuse[2]=.3f;slot.selectionGain=float(i%3)*.5f;}
        const auto seed=[&]{batchOk(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)batchOk(d->SetRenderTarget(i,nullptr));batchOk(d->SetRenderTarget(0,w.screenLighting.baseline().surface));batchOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));batchOk(d->SetRenderTarget(0,w.screenLighting.raw().surface));batchOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x3f777777,1,0));batchOk(d->SetVertexShader(nullptr));batchOk(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SRGBWRITEENABLE})batchOk(d->SetRenderState(s,FALSE));batchOk(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));batchOk(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));batchOk(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));};
        const auto draw=[&](unsigned count,bool shared,bool masked,bool failures=false){bool prepared=false;for(w.pointIndex=0;w.pointIndex<count;++w.pointIndex){const bool ok=w.renderPointLighting(depth,masked?water:nullptr,W,H,.1f,64,0,1,1,.2f,.8f,1,nullptr,shared?&prepared:nullptr);if(!ok)assert(failures&&!prepared);}};
        IDirect3DSurface9* stage=nullptr;batchOk(d->CreateOffscreenPlainSurface(W/2,H/2,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&stage,nullptr));
        const auto pixels=[&]{batchOk(d->GetRenderTargetData(w.screenLighting.raw().surface,stage));D3DLOCKED_RECT lock{};batchOk(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> out(size_t(W/2)*(H/2)*8);for(unsigned y=0;y<H/2;++y)std::memcpy(out.data()+size_t(y)*(W/2)*8,static_cast<char*>(lock.pBits)+y*lock.Pitch,(W/2)*8);batchOk(stage->UnlockRect());return out;};
        const auto beginTrace=[&]{meter.beginTrace({depth,water,w.worldNormals.texture_,w.screenLighting.baseline().texture,
            detail,w.pointTargets.slots()[0].cube,w.pointLighting.hardShader(),w.pointLighting.softShader()});};
        const auto receipt=[&](const std::vector<unsigned char>& image){
            std::printf("POINT_OWNER case=%u commands=%u order=%016llx pixels=%016llx\n",cases,meter.commands,
                (unsigned long long)meter.sequence,(unsigned long long)PointBatchMeter::bytes(14695981039346656037ull,image.data(),image.size()));
        };
        for(bool soft:{false,true})for(unsigned count:{1u,4u,16u,64u}){
            w.quality.softShadows=soft;
            if(!timing){for(bool material:{false,true})for(bool masked:{false,true}){w.materialDetail=material?detail:nullptr;w.materialBumpGain=material?.7f:0;
                std::vector<unsigned char> reference;unsigned oldSamplers=0;
                for(bool shared:{false,true}){seed();meter.samplers=meter.textures=meter.draws=0;batchOk(d->BeginScene());
                    beginTrace();draw(count,shared,masked);meter.tracing=false;batchOk(d->EndScene());
                    const auto image=pixels();if(!shared){reference=image;oldSamplers=meter.samplers;}else{assert(image==reference);assert(meter.samplers*count==oldSamplers);}
                    assert(meter.draws==count);receipt(image);++cases;}
            }}else{
                IDirect3DQuery9 *start=nullptr,*end=nullptr,*hz=nullptr;batchOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));batchOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));batchOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&hz));batchOk(hz->Issue(D3DISSUE_END));const auto frequency=pointQuery(hz);
                for(unsigned block=0;block<16;++block){const bool shared=(block/2)%2?1-block%2:block%2;double cpu=0,gpu=0;unsigned sets=0;
                    for(unsigned frame=0;frame<16;++frame){seed();batchOk(d->BeginScene());meter.samplers=0;batchOk(start->Issue(D3DISSUE_END));const auto begin=std::chrono::steady_clock::now();draw(count,shared,false);cpu+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();sets+=meter.samplers;batchOk(end->Issue(D3DISSUE_END));batchOk(d->EndScene());gpu+=double(pointQuery(end)-pointQuery(start))*1000/double(frequency);}
                    std::printf("POINT_BATCH_TIMING soft=%u count=%u shared=%u warm=%u cpuMs=%.9f gpuMs=%.9f samplers=%u\n",unsigned(soft),count,unsigned(shared),unsigned(block<4),cpu/16,gpu/16,sets/16);std::fflush(stdout);
                }drop(start);drop(end);drop(hz);
            }
        }
        if(!timing)for(unsigned fault:{1u,2u,3u}){std::vector<unsigned char> reference;
            for(bool shared:{false,true}){for(auto& slot:w.pointTargets.slots())slot.ready=true;seed();meter.fault=fault;meter.draws=0;batchOk(d->BeginScene());
                beginTrace();draw(4,shared,false,true);meter.tracing=false;batchOk(d->EndScene());
                assert(!meter.fault&&meter.draws==3);const auto image=pixels();if(!shared)reference=image;else assert(image==reference);
                receipt(image);++cases;}
        }
        if(!timing){
            w.pointTargets.slots()[0].ready=true;
            w.quality.softShadows=false;seed();batchOk(d->BeginScene());draw(1,false,false);batchOk(d->EndScene());const auto hard=pixels();
            w.pointLighting.reset(false);batchOk(w.pointLighting.createHard(d));meter.failSoftCreate=true;
            w.quality.softShadows=true;seed();batchOk(d->BeginScene());draw(1,false,false);batchOk(d->EndScene());
            assert(!meter.failSoftCreate&&!w.pointLighting.softShader()&&pixels()==hard);
            seed();batchOk(d->BeginScene());draw(1,false,false);batchOk(d->EndScene());assert(!w.pointLighting.softShader()&&pixels()==hard);
            w.pointLighting.reset(true);assert(w.pointLighting.ensureSoft(d,[] {}));
            std::puts("POINT_OWNER_SOFT_ALLOC_PASS fallback=hard retryAfterReset=1");
        }
        w.materialDetail=nullptr;for(unsigned i=0;i<16;++i)batchOk(d->SetTexture(i,nullptr));drop(stage);drop(depth);drop(water);drop(detail);w.pointIndex=0;
        std::printf("POINT_BATCH_PASS cases=%u timing=%u\n",cases,unsigned(timing));
    }
};
int main(int argc,char** argv){assert(argc==2||argc==3);logFile=nullptr;HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9* raw=nullptr;batchOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));{PointBatchMeter meter(raw);WorldScreenEffectsTest::run(meter,argc==3);}raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);}
