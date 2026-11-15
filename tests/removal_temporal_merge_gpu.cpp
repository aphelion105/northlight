// Compare two-pass/fused graphs or frozen/current fused shaders over the same history.
#define main unused_removal_distance_main
#include "removal_distance_gpu.cpp"
#undef main

struct Target {
    IDirect3DTexture9* texture=nullptr;IDirect3DSurface9* surface=nullptr;
    Target(IDirect3DDevice9* d,unsigned w,unsigned h,D3DFORMAT format){ok(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&texture,nullptr));ok(texture->GetSurfaceLevel(0,&surface));}
    ~Target(){surface->Release();texture->Release();}
};
int main(int argc,char** argv){
    assert(argc==6||argc==7);const bool referenceFused=argc==7;std::setvbuf(stdout,nullptr,_IONBF,0);
    const std::string prefix=argv[1],baseline=std::string(argv[2])+"/";
    const bool control=std::strcmp(argv[5],"control")==0;
    auto bank=constantBank(read(prefix+"constants.f32"));auto& c=bank.values;
    auto scene=image(prefix+"scene.fgr"),depth=image(prefix+"depth.fgr"),light=image(prefix+"light.fgr");
    const unsigned W=light.width,H=light.height;const bool timing=W>512;
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[4]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    {
        IDirect3DPixelShader9* shaders[5]={};
        const std::string files[]={baseline+"RemovalSmooth.bin",baseline+"TemporalLight.bin",argv[3],baseline+"WorldDistance.bin",argc==7?argv[6]:argv[3]};
        for(unsigned i=0;i<5;++i){auto code=read(files[i]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[i]));}
        auto changedLight=light;
        for(size_t i=3;i<changedLight.pixels.size()/2;i+=4){std::uint16_t alpha=0x3800;std::memcpy(changedLight.pixels.data()+i*2,&alpha,2);}
        IDirect3DTexture9* textures[]={texture(d,scene),texture(d,depth),texture(d,light),texture(d,image(prefix+"baseline.fgr")),texture(d,image(prefix+"normals.fgr")),texture(d,image(prefix+"ao.fgr")),texture(d,changedLight)};
        const unsigned slots[]={0,1,8,12,14,10};
        for(unsigned i=0;i<6;++i)ok(d->SetTexture(slots[i],textures[i]));
        for(unsigned s=0;s<16;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,s==0||s==10?D3DTEXF_LINEAR:D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
        ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetVertexShader(nullptr));ok(d->SetDepthStencilSurface(nullptr));
        ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        ok(d->SetPixelShaderConstantF(0,c[0],bank.count));const D3DVIEWPORT9 viewport={0,0,W,H,0,1};ok(d->SetViewport(&viewport));
        const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
        auto draw=[&](IDirect3DPixelShader9* ps,Target& target){ok(d->SetRenderTarget(0,target.surface));ok(d->SetViewport(&viewport));ok(d->SetPixelShader(ps));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));};
        Target smooth(d,W,H,D3DFMT_A16B16G16R16F),distance(d,W,H,D3DFMT_R32F);
        Target a0(d,W,H,D3DFMT_A16B16G16R16F),a1(d,W,H,D3DFMT_A16B16G16R16F),b0(d,W,H,D3DFMT_A16B16G16R16F),b1(d,W,H,D3DFMT_A16B16G16R16F);
        Target* history[2][2]={{&a0,&a1},{&b0,&b1}};
        ok(d->BeginScene());draw(shaders[3],distance);
        for(auto& variant:history)for(auto* target:variant){ok(d->SetRenderTarget(0,target->surface));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));}ok(d->EndScene());
        IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        auto pixels=[&](Target& target){ok(d->GetRenderTargetData(target.surface,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<std::uint16_t> result(size_t(W)*H*4);for(unsigned y=0;y<H;++y)std::memcpy(result.data()+size_t(y)*W*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*8);ok(staging->UnlockRect());return result;};
        auto render=[&](unsigned variant,unsigned frame,unsigned count){
            const bool merged=referenceFused||(variant&&!control);const unsigned current=frame%2,previous=1-current;
            ok(d->BeginScene());
            for(unsigned n=0;n<count;++n){
                ok(d->SetTexture(9,nullptr));ok(d->SetTexture(13,nullptr));ok(d->SetTexture(14,nullptr));ok(d->SetTexture(15,nullptr));
                // A common destination is unbound before its previous result can be sampled.
                ok(d->SetRenderTarget(0,history[variant][current]->surface));
                ok(d->SetTexture(14,textures[4]));ok(d->SetTexture(15,distance.texture));
                if(merged){
                    ok(d->SetTexture(9,history[variant][previous]->texture));ok(d->SetTexture(13,distance.texture));
                    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(9,state,D3DTEXF_LINEAR));
                    draw(shaders[referenceFused&&(!variant||control)?4:2],*history[variant][current]);
                }else{
                    draw(shaders[0],smooth);
                    ok(d->SetRenderTarget(0,history[variant][current]->surface));ok(d->SetTexture(9,smooth.texture));
                    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(9,state,D3DTEXF_POINT));
                    ok(d->SetTexture(14,history[variant][previous]->texture));
                    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(14,state,D3DTEXF_LINEAR));
                    draw(shaders[1],*history[variant][current]);
                    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(14,state,D3DTEXF_POINT));
                }
            }ok(d->EndScene());
        };
        unsigned changed=0,alpha=0,outside=0,nonfinite=0;double maximum=0;
        for(unsigned frame=0;frame<(timing?4u:48u);++frame){
            float temporal[]={frame==0||frame==24?0.f:.85f,c[57][1],c[57][2],c[57][3]};ok(d->SetPixelShaderConstantF(57,temporal,1));
            float previous[16];std::memcpy(previous,c[53],sizeof previous);if(frame%12>=6)previous[12]+=.03f;
            ok(d->SetPixelShaderConstantF(53,previous,4));ok(d->SetTexture(8,textures[frame%10<5?2:6]));
            render(0,frame,1);auto before=pixels(*history[0][frame%2]);render(1,frame,1);auto after=pixels(*history[1][frame%2]);
            for(size_t i=0;i<before.size();++i){const float a=half(before[i]),b=half(after[i]);const double delta=std::fabs(a-b);changed+=before[i]!=after[i];nonfinite+=!std::isfinite(delta);maximum=std::max(maximum,delta);if(i%4==3)alpha+=before[i]!=after[i];else outside+=delta>.002+.001*std::fabs(a);}
        }
        std::printf("{\"event\":\"image\",\"changed\":%u,\"alpha\":%u,\"outside\":%u,\"nonfinite\":%u,\"max_abs\":%.9g}\n",changed,alpha,outside,nonfinite,maximum);
        assert(!alpha&&!outside&&!nonfinite);
        if(referenceFused)assert(!changed); // Same arithmetic: zero-weight read elision must be byte-exact.
        if(timing){
            const char* batchText=std::getenv("NORTHLIGHT_TIMING_BATCH");const unsigned batch=batchText?unsigned(std::atoi(batchText)):32;assert(batch>=32&&batch<=256);
            IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
            ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
            for(unsigned block=0;block<64;++block){unsigned variant=block%4==1||block%4==2;render(variant,0,std::max(4u,batch/4));wait(event);
                ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));render(variant,0,batch);ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
                UINT64 a=0,b=0,hz=0;BOOL bad=FALSE;ok(start->GetData(&a,8,0));ok(end->GetData(&b,8,0));ok(frequency->GetData(&hz,8,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(hz&&b>=a&&!bad);
                std::printf("{\"event\":\"timing\",\"block\":%u,\"variant\":%u,\"ms\":%.9f}\n",block,variant,double(b-a)*1000/double(hz)/batch);
            }
            for(auto* q:{event,start,end,frequency,disjoint})q->Release();
        }
        for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));ok(d->SetPixelShader(nullptr));staging->Release();for(auto* t:textures)t->Release();for(auto* s:shaders)s->Release();
    }
    d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);puts("REMOVAL_TEMPORAL_MERGE_PASS");
}
