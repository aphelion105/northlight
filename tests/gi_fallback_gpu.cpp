// Production WorldGI and shadow-baseline contracts on a hidden D3D9 device.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include <array>

static Image solid(float r,float g=0,float b=0,float a=0){
    Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
    const float p[]={r,g,b,a};std::memcpy(im.pixels.data(),p,16);return im;
}
static Image metadata(float coverage,bool spatial=false){
    Image im{16,4,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16*4*16)};
    for(unsigned z=0;z<4;++z)for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){
        const bool valid=x<2&&y<2&&z<2&&coverage>0&&(!spatial||x==0);
        const float p[]={float(x),float(y),float(z),valid?1-.45f*coverage:-1};
        std::memcpy(im.pixels.data()+((z*4+x)+y*16)*16,p,16);
    }
    return im;
}
int main(int argc,char** argv){
    assert(argc>=3);const unsigned W=argc>=4?1280:4,H=argc>=4?720:4;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightGiFallback";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    const auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,4,4,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    const auto module=LoadLibraryA(argv[2]);assert(module);const auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=4;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    D3DADAPTER_IDENTIFIER9 adapter{};ok(api->GetAdapterIdentifier(0,0,&adapter));std::printf("GPU %s\n",adapter.Description);
    IDirect3DPixelShader9 *gi=nullptr,*oldGi=nullptr,*lighting[2]{};
    auto shader=[&](const std::string& path,IDirect3DPixelShader9** output){auto bytes=read(path);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()),output));};
    shader(std::string(argv[1])+"/WorldGI.bin",&gi);
    shader(std::string(argv[1])+"/WorldLighting.bin",&lighting[0]);shader(std::string(argv[1])+"/WorldLightingSoft.bin",&lighting[1]);
    if(argc>=4)shader(std::string(argv[3])+"/WorldGI.bin",&oldGi);
    for(unsigned s=0;s<16;++s){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(k,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));
    IDirect3DSurface9 *target=nullptr,*baseline=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&baseline,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->SetRenderTarget(0,target));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(W)-.5f,-.5f,0,1,1,0},{-.5f,float(H)-.5f,0,1,0,1},{float(W)-.5f,float(H)-.5f,0,1,1,1}};
    std::array<IDirect3DTexture9*,16> owned{};
    auto bind=[&](unsigned slot,Image im){ok(d->SetTexture(slot,nullptr));if(owned[slot])owned[slot]->Release();owned[slot]=texture(d,im);ok(d->SetTexture(slot,owned[slot]));};
    auto draw=[&](IDirect3DPixelShader9* ps,unsigned n=1){ok(d->SetPixelShader(ps));ok(d->BeginScene());for(unsigned i=0;i<n;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());};
    auto sample=[&](IDirect3DSurface9* surface){ok(d->GetRenderTargetData(surface,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::array<float,4> value{};for(unsigned k=0;k<4;++k){uint16_t v;std::memcpy(&v,static_cast<char*>(lock.pBits)+lock.Pitch+8+2*k,2);value[k]=half(v);}ok(staging->UnlockRect());return value;};
    float c[79][4]{};c[0][0]=1.f/(W*2);c[0][1]=1.f/(H*2);c[0][2]=.2f;c[0][3]=1000;c[1][0]=c[1][1]=c[1][2]=c[2][0]=1;
    c[6][0]=c[6][1]=4;c[6][2]=3.75f;c[6][3]=1;c[18][0]=.2f;c[18][1]=.3f;c[18][2]=.4f;c[19][3]=8;c[20][0]=4;c[20][1]=.5f;c[20][2]=.85f;c[20][3]=1;c[24][3]=1;
    c[33][0]=float(W*2);c[33][1]=float(H*2);c[33][2]=float(W);c[33][3]=float(H);
    const float depth=1000.f/(1000-.2f)*(1-.2f/10);
    bind(1,solid(depth));bind(7,solid(100,10000,1,-1000));bind(11,solid(0));
    unsigned cases=0;float worst=0;
    for(float coverage:{0.f,.25f,.5f,1.f})for(float visibility:{-1.f,0.f,.15f,.5f,1.f})for(float gain:{0.f,.5f,1.f})for(float irradiance:{0.f,.1f,.7f})for(float confidence:{0.f,1.f}){
        c[20][1]=gain;ok(d->SetPixelShaderConstantF(0,c[0],79));bind(10,metadata(coverage));bind(12,solid(0,0,0,visibility));bind(14,solid(0,0,1,confidence));
        for(unsigned s:{4u,5u,6u})bind(s,solid(irradiance/.28209479f));
        draw(gi);const auto actual=sample(target);
        for(unsigned k=0;k<3;++k){const float raw=((irradiance-c[18][k])*coverage-c[18][k]*(1-std::fabs(visibility))*(1-coverage))*gain;
            const float expected=std::min(raw,0.f)+std::max(raw,0.f)*confidence;worst=std::max(worst,std::fabs(actual[k]-expected));assert(std::isfinite(actual[k])&&std::fabs(actual[k]-expected)<.001f);}
        assert(actual[3]==0);if(oldGi&&(coverage==1||std::fabs(visibility)==1||gain==0)){draw(oldGi);assert(sample(target)==actual);}++cases;
    }
    // Spatial fade at a missing neighboring probe, independent of born-time fade.
    c[20][1]=.5f;bind(10,metadata(1,true));bind(12,solid(0,0,0,.15f));bind(14,solid(0,0,1,1));for(unsigned s:{4u,5u,6u})bind(s,solid(.1f/.28209479f));
    float previous=0;for(unsigned i=0;i<=32;++i){c[6][0]=float(i)*.25f;ok(d->SetPixelShaderConstantF(0,c[0],79));draw(gi);const float actual=sample(target)[0],coverage=1-float(i)/32;
        const float expected=((.1f-.2f)*coverage-.2f*.85f*(1-coverage))*.5f;assert(std::fabs(actual-expected)<.0002f);if(i)assert(actual<=previous&&previous-actual<.002f);previous=actual;++cases;}
    // Sky and water cannot acquire the missing-probe correction.
    bind(10,metadata(0));bind(1,solid(1));draw(gi);assert(sample(target)==(std::array<float,4>{}));bind(1,solid(depth));c[30][0]=1;ok(d->SetPixelShaderConstantF(0,c[0],79));bind(11,solid(5,1));draw(gi);assert(sample(target)==(std::array<float,4>{}));c[30][0]=0;bind(11,solid(0));
    // Producer contract: distinguish unknown coverage from fully occluded moon.
    c[7][0]=c[8][1]=1.f/48;c[10][2]=.5f;c[11][0]=c[12][1]=1.f/192;c[14][2]=.5f;c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;c[35][0]=1.f/1280;
    c[16][2]=c[28][2]=1;c[27][1]=1;c[73][2]=1;c[6][0]=4;
    bind(2,solid(0));bind(3,solid(0));bind(12,solid(1,1,1,1));bind(15,solid(0));
    ok(d->SetRenderTarget(1,baseline));
    for(auto ps:lighting)for(float moon:{0.f,1.f})for(float outside:{0.f,1.f})for(float active:{0.f,1.f}){
        c[16][3]=moon;c[6][0]=outside?1000:4;for(unsigned k=0;k<3;++k)c[17][k]=active*.6f;
        ok(d->SetPixelShaderConstantF(0,c[0],79));draw(ps);const auto value=sample(baseline);const float expected=outside?-1:active?(moon?0:.15f):1;assert(std::fabs(value[3]-expected)<.0003f);++cases;
    }
    ok(d->SetRenderTarget(1,nullptr));
    std::printf("GI_FALLBACK_PASS cases=%u maxError=%.9g fullCoverageExact=%u skyWaterNeutral=1 unknownCoverageNeutral=1\n",cases,worst,unsigned(oldGi!=nullptr));
    if(oldGi){
        // Alternating GPU-only blocks at the capture's half resolution.
        c[6][0]=c[6][1]=4;c[20][1]=.5f;c[30][0]=0;ok(d->SetPixelShaderConstantF(0,c[0],79));bind(12,solid(0,0,0,.15f));bind(14,solid(0,0,1,1));
        IDirect3DQuery9 *event=nullptr,*begin=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
        ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
        for(float coverage:{0.f,1.f}){bind(10,metadata(coverage));std::vector<double> timings[2];
            for(unsigned block=0;block<20;++block){const unsigned mode=(block/2)%2?1-block%2:block%2;auto* ps=mode?gi:oldGi;draw(ps,8);wait(event);ok(disjoint->Issue(D3DISSUE_BEGIN));ok(begin->Issue(D3DISSUE_END));draw(ps,32);ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
                UINT64 a=0,b=0,f=0;BOOL bad=FALSE;assert(begin->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&frequency->GetData(&f,sizeof f,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&f&&b>=a);if(block>=4)timings[mode].push_back(double(b-a)*1000/double(f)/32);}
            double med[2];for(unsigned i=0;i<2;++i){std::sort(timings[i].begin(),timings[i].end());med[i]=(timings[i][3]+timings[i][4])*.5;}
            std::printf("GI_FALLBACK_TIMING resident=%.0f width=%u height=%u oldMs=%.6f newMs=%.6f deltaMs=%.6f\n",coverage,W,H,med[0],med[1],med[1]-med[0]);
        }
        for(auto* q:{event,begin,end,frequency,disjoint})q->Release();oldGi->Release();
    }
    for(unsigned s=0;s<16;++s){d->SetTexture(s,nullptr);if(owned[s])owned[s]->Release();}
    for(auto* ps:lighting)ps->Release();gi->Release();staging->Release();baseline->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
