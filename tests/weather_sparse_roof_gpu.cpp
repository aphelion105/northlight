// Compare against the frozen shipping shader, including its ceiling/depth rules.
#define NORTHLIGHT_SURFACE_HELPERS_ONLY
#include "surface_effects_gpu.cpp"
#undef NORTHLIGHT_SURFACE_HELPERS_ONLY

static std::vector<unsigned char> readTarget(IDirect3DDevice9* d,IDirect3DSurface9* target,
        IDirect3DSurface9* resolved,IDirect3DSurface9* stage,unsigned w,unsigned h,unsigned bytes){
    ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved,stage));
    D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<unsigned char> result(size_t(w)*h*bytes);
    for(unsigned y=0;y<h;++y)std::memcpy(result.data()+size_t(y)*w*bytes,static_cast<char*>(lock.pBits)+y*lock.Pitch,w*bytes);
    ok(stage->UnlockRect());return result;
}
int main(int argc,char** argv){
    assert(argc==3);const char* allow=std::getenv("NORTHLIGHT_TEST_ALLOW_RUNNING_GAME");if(!allow||std::strcmp(allow,"1"))requireNoGame();
    HWND window=CreateWindowExW(0,L"STATIC",L"Northlight weather oracle",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};
    pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    {
    NorthlightSurfaceRenderer surface(d);IDirect3DPixelShader9* shaders[2][4]{};
    const char* names[]={"RainQuality","SnowQuality","RainPerformance","SnowPerformance"};
    for(unsigned entry=0;entry<4;++entry){
        auto code=read(std::string(argv[2])+"/"+names[entry]+".bin");
        ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[0][entry]));
        auto candidate=read(std::string(argv[2])+"/candidate-"+names[entry]+".bin");
        ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(candidate.data()),&shaders[1][entry]));
    }
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,0));
    ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));
    ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));
    ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));ok(d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE));
    for(unsigned s:{3u,5u}){
        for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,state,D3DTADDRESS_CLAMP));
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));
        ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));
    }
    NorthlightWeatherArtTestAccess::bindSnow(surface);
    unsigned cases=0,nonempty=0;
    for(unsigned timing=0;timing<2;++timing)for(unsigned samples:{0u,4u})for(unsigned hdr:{0u,1u}){
        if(timing&&(samples||!hdr))continue;
        const unsigned w=timing?3840:321,h=timing?2160:181,bytes=hdr?8:4;
        const auto format=hdr?D3DFMT_A16B16G16R16F:D3DFMT_A8R8G8B8;
        IDirect3DSurface9 *target=nullptr,*resolved=nullptr,*stage=nullptr;
        ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));
        ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
        ok(d->CreateOffscreenPlainSurface(w,h,format,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->SetRenderTarget(0,target));
        D3DVIEWPORT9 vp={0,0,w,h,0,1};ok(d->SetViewport(&vp));
        const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{w-.5f,-.5f,0,1,1,0},{-.5f,h-.5f,0,1,0,1},{w-.5f,h-.5f,0,1,1,1}};
        auto draw=[&](unsigned mode,unsigned entry,unsigned repeats){ok(d->SetPixelShader(shaders[mode][entry]));ok(d->BeginScene());for(unsigned i=0;i<repeats;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());};
        for(unsigned scene=0;scene<5;++scene){
            Image depth{w,h,D3DFMT_R32F,4,std::vector<unsigned char>(size_t(w)*h*4)};
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                // Open sky; a roof; a doorway with foreground; shallow layer
                // transitions; then roof-field edges at a moved camera.
                const float z=scene==2&&x<w/2?1.5f:scene==3?2.f+27.f*x/w:1000.f;
                const float raw=.06f+.88f*(1000.f/999.9f*(1-.1f/z));
                std::memcpy(depth.pixels.data()+(size_t(y)*w+x)*4,&raw,4);
            }
            auto* input=texture(d,depth);ok(d->SetTexture(3,input));
            Image roof{128,128,D3DFMT_R32F,4,std::vector<unsigned char>(128*128*4)};
            for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){const float z=scene==1?100.f:scene==2&&y<64?12.f:-100.f;std::memcpy(roof.pixels.data()+(y*128+x)*4,&z,4);}
            auto* ceiling=texture(d,roof);ok(d->SetTexture(5,ceiling));
            const float roofMap[4]={-48,-48,1.f/96.f,1};ok(d->SetPixelShaderConstantF(12,roofMap,1));
            for(unsigned entry=0;entry<(timing?2u:4u);++entry)for(unsigned phase=0;phase<(timing?1u:3u);++phase)for(unsigned size=0;size<(timing?1u:3u);++size){
                float c[10][4]{};c[0][0]=1.f/w;c[0][1]=1.f/h;c[0][2]=.1f;c[0][3]=1000;
                c[1][0]=1.3f;c[1][1]=1.7f;c[1][2]=-1;c[1][3]=phase==2?123.4f:.31f*phase;
                c[2][0]=.06f;c[2][1]=1.f/.88f;c[3][0]=phase==1?.35f:1.f;c[3][1]=float(entry%2);
                c[3][2]=timing?0:size==0?-.75f:size==1?0.f:2.f;
                c[4][1]=1;c[5][2]=1;c[6][0]=-1;c[7][0]=scene==4?49.f:phase*.73f;c[7][1]=-phase*.19f;c[7][2]=3;c[7][3]=1;c[8][0]=.1f;c[9][1]=.7f;
                ok(d->SetPixelShaderConstantF(0,c[0],10));
                if(!timing){
                    ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x7f143250,1,0));auto clear=readTarget(d,target,resolved,stage,w,h,bytes);
                    draw(0,entry,1);auto original=readTarget(d,target,resolved,stage,w,h,bytes);
                    ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x7f143250,1,0));draw(1,entry,1);auto result=readTarget(d,target,resolved,stage,w,h,bytes);
                    if(result!=original){std::fprintf(stderr,"MISMATCH scene=%u entry=%u phase=%u size=%u hdr=%u samples=%u\n",scene,entry,phase,size,hdr,samples);std::abort();}
                    if(scene==1)assert(result==clear);nonempty+=result!=clear;++cases;
                }else for(unsigned block=0;block<12;++block)for(unsigned order=0;order<2;++order){
                    const unsigned mode=(block+order)&1;IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
                    ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
                    ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x7f143250,1,0));ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(mode,entry,8);
                    ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
                    UINT64 a=0,b=0,f=0;BOOL bad=FALSE;assert(start->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&frequency->GetData(&f,sizeof f,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&b>=a&&f);
                    if(block>=4)std::printf("TIMING scene=%u entry=%u block=%u mode=%u ms=%.8f\n",scene,entry,block,mode,double(b-a)*1000./double(f)/8.);
                    for(auto* q:{event,start,end,frequency,disjoint})q->Release();
                }
            }
            ok(d->SetTexture(3,nullptr));ok(d->SetTexture(5,nullptr));drop(input);drop(ceiling);
        }
        drop(stage);drop(resolved);drop(target);
    }
    assert(cases==720&&nonempty>100);std::printf("SPARSE_ROOF_PASS cases=%u nonempty=%u exact=1\n",cases,nonempty);
    for(auto& mode:shaders)for(auto*& shader:mode)drop(shader);
    }
    drop(d);drop(api);FreeLibrary(module);DestroyWindow(window);
}
