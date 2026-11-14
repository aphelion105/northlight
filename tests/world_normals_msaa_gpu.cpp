#define NORTHLIGHT_WORLD_NORMAL_MSAA_TEST
#define main modelNormalOriginalMain
#include "model_normals_gpu.cpp"
#undef main
#include "world_compiled_shaders.h"
#include "screen_quad.h"

static void freeTarget(Target& t){t.read->Release();t.s->Release();t.t->Release();}
static std::vector<unsigned char> readFloat(IDirect3DTexture9* texture,unsigned w,unsigned h){
    IDirect3DSurface9 *source=nullptr,*readback=nullptr;ok(texture->GetSurfaceLevel(0,&source));ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    ok(d->GetRenderTargetData(source,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> bytes(size_t(w)*h*4);
    for(unsigned y=0;y<h;++y)std::memcpy(bytes.data()+size_t(y)*w*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,w*4);
    ok(readback->UnlockRect());readback->Release();source->Release();return bytes;
}
static bool compare(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b,bool floating,unsigned test){
    assert(a.size()==b.size());unsigned changed=0,confidence=0,over=0;double maximum=0;
    const unsigned stride=floating?4:2;
    for(size_t i=0;i<a.size()/stride;++i){float x,y;if(floating){std::memcpy(&x,a.data()+i*4,4);std::memcpy(&y,b.data()+i*4,4);}else{x=half(reinterpret_cast<const uint16_t*>(a.data())[i]);y=half(reinterpret_cast<const uint16_t*>(b.data())[i]);}
        assert(std::isfinite(x)&&std::isfinite(y));const double error=std::fabs(double(x)-y);maximum=std::max(maximum,error);changed+=x!=y;
        if(!floating&&i%4==3&&x!=y)++confidence;
        if(error>(floating?std::max(.00002,double(std::fabs(x))*2e-6):.001)){if(over<12)std::printf("OVER case=%u component=%zu expected=%.9g actual=%.9g\n",test,i,x,y);++over;}
    }
    if(changed)std::printf("DIFFERENCE case=%u distance=%u changed=%u confidence=%u over=%u max=%.9g\n",test,unsigned(floating),changed,confidence,over,maximum);
    return !over&&!confidence;
}
int main(int argc,char** argv){
    assert(argc==2||argc==3);const bool timing=argc==3;std::setvbuf(stdout,nullptr,_IONBF,0);
    auto window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    D3DADAPTER_IDENTIFIER9 adapter{};ok(api->GetAdapterIdentifier(0,0,&adapter));std::printf("ADAPTER %s\n",adapter.Description);
    IDirect3DPixelShader9 *constant=nullptr,*world=nullptr;const DWORD code[]={0xffff0300u,0x02000001u,NorthlightWaterShaderPatch::dst(8,0),NorthlightWaterShaderPatch::src(2,0),65535u};
    ok(d->CreatePixelShader(code,&constant));ok(d->CreatePixelShader(kWorldNormalsShader,&world));
    unsigned cases=0;bool equal=true;
    for(unsigned sampleCount:{2u,4u,8u})for(auto dimensions:{std::pair<unsigned,unsigned>{64,64},{65,47},{256,128}}){
        if(timing&&(sampleCount!=2||dimensions.first!=64))continue;
        const unsigned w=timing?3840:dimensions.first,h=timing?2160:dimensions.second;
        NorthlightModelNormalMsaa fusion;D3DSURFACE_DESC desc{};desc.Width=w;desc.Height=h;desc.MultiSampleType=D3DMULTISAMPLE_TYPE(sampleCount);desc.Format=D3DFMT_A8R8G8B8;assert(fusion.ensure(d,desc));
        auto normal=target(w/2,h/2,D3DFMT_A16B16G16R16F,8),distance=target(w/2,h/2,D3DFMT_R32F,4);
        IDirect3DTexture9 *scene=nullptr,*depth=nullptr;
        ok(d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&scene,nullptr));ok(d->CreateTexture(w,h,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&depth,nullptr));
        float bank[80][4]{};bank[0][0]=1.f/w;bank[0][1]=1.f/h;bank[0][2]=.1f;bank[0][3]=150;
        bank[1][0]=1.2f;bank[1][1]=1.7f;bank[1][2]=1;bank[2][0]=1;
        for(unsigned i=0;i<3;++i)bank[3+i][i]=1;
        bank[33][0]=float(w);bank[33][1]=float(h);bank[33][2]=float(w/2);bank[33][3]=float(h/2);
        for(unsigned pattern=0;pattern<(timing?1u:6u);++pattern)for(unsigned models=0;models<2;++models){
            D3DLOCKED_RECT a{},b{};ok(scene->LockRect(0,&a,nullptr,0));ok(depth->LockRect(0,&b,nullptr,0));
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                float z=2;
                if(pattern==1)z=2+float(x)*.01f+float(y)*.015f;
                if(pattern==2)z=x<w/2?2:5;
                if(pattern==3)z=(x/7+y/11)%3?2:150;
                if(pattern==4)z=(x/3+y/5)%2?2.01f:2.f;
                if(pattern==5)z=.2f+float((x*17+y*13)%301)*.2f;
                reinterpret_cast<float*>(static_cast<char*>(b.pBits)+y*b.Pitch)[x]=(150.f-.1f*150.f/z)/(150.f-.1f);
                reinterpret_cast<uint32_t*>(static_cast<char*>(a.pBits)+y*a.Pitch)[x]=0xff000000u|((x*37%256)<<16)|((y*19%256)<<8)|((x+y)*11%256);
            }
            ok(scene->UnlockRect(0));ok(depth->UnlockRect(0));
            for(unsigned i=0;i<16;++i){ok(d->SetTexture(i,nullptr));for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(i,key,D3DTEXF_POINT));ok(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE));}
            ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetRenderTarget(0,fusion.surface()));
            for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(key,FALSE));
            ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));
            ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetPixelShader(constant));
            D3DVIEWPORT9 vp{0,0,w,h,0,1};ok(d->SetViewport(&vp));ok(d->BeginScene());
            const float first[]={0,0,-1,2},other[]={1,0,0,5};
            ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,1));ok(d->SetPixelShaderConstantF(0,first,1));ok(NorthlightScreen::drawQuad(d,w,h));
            ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,~1u));ok(d->SetPixelShaderConstantF(0,other,1));ok(NorthlightScreen::drawQuad(d,w,h));ok(d->EndScene());ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,~0u));
            bank[79][0]=float(models);float constants[8][4];std::memcpy(constants,bank,6*16);std::memcpy(constants[6],bank[33],16);std::memcpy(constants[7],bank[79],16);constants[7][1]=1;
            auto reference=[&]{
                assert(fusion.resolve());ok(d->SetRenderTarget(0,normal.s));ok(d->SetRenderTarget(1,distance.s));
                D3DVIEWPORT9 vp{0,0,w/2,h/2,0,1};ok(d->SetViewport(&vp));ok(d->SetPixelShader(world));ok(d->SetPixelShaderConstantF(0,&bank[0][0],80));
                ok(d->SetTexture(0,scene));for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(0,key,D3DTEXF_LINEAR));ok(d->SetTexture(1,depth));ok(d->SetTexture(13,fusion.texture()));ok(d->BeginScene());ok(NorthlightScreen::drawQuad(d,w/2,h/2));ok(d->EndScene());
            };
            reference();const auto expectedNormal=pixels(normal),expectedDistance=pixels(distance);
            assert(fusion.composeWorld(scene,depth,&constants[0][0]));
            equal &= compare(expectedNormal,capturedPixels(fusion.worldTexture(),w/2,h/2),false,cases);
            equal &= compare(expectedDistance,readFloat(fusion.worldDistanceTexture(),w/2,h/2),true,cases);++cases;
            IDirect3DSurface9* bound=nullptr;ok(d->GetRenderTarget(0,&bound));assert(bound==normal.s);bound->Release();
            if(timing){
                IDirect3DQuery9 *q0=nullptr,*q1=nullptr,*hz=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&q0));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&q1));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&hz));
                auto get=[](IDirect3DQuery9* q){UINT64 v=0;HRESULT hr;while((hr=q->GetData(&v,sizeof v,D3DGETDATA_FLUSH))==S_FALSE)Sleep(0);ok(hr);return v;};
                LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);std::vector<double> gpu[2],cpu[2];
                for(unsigned block=0;block<4;++block)for(bool fused:{false,true,true,false})for(unsigned round=0;round<8;++round){
                    ok(hz->Issue(D3DISSUE_END));ok(q0->Issue(D3DISSUE_END));LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
                    if(fused)assert(fusion.composeWorld(scene,depth,&constants[0][0]));else reference();
                    QueryPerformanceCounter(&b);ok(q1->Issue(D3DISSUE_END));auto end=get(q1),start=get(q0),rate=get(hz);
                    if(round>=4){gpu[fused].push_back(double(end-start)*1000/rate);cpu[fused].push_back(double(b.QuadPart-a.QuadPart)*1000/frequency.QuadPart);}
                }
                auto median=[](std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];};
                std::printf("WORLD_NORMAL_MSAA_TIMING size=%ux%u models=%u gpuSeparate=%.6f gpuFused=%.6f cpuSeparate=%.6f cpuFused=%.6f scope=resolve-plus-WorldNormals-only\n",w,h,models,median(gpu[0]),median(gpu[1]),median(cpu[0]),median(cpu[1]));q0->Release();q1->Release();hz->Release();
            }
        }
        for(unsigned i:{0u,1u,13u})ok(d->SetTexture(i,nullptr));ok(d->SetRenderTarget(1,nullptr));
        scene->Release();depth->Release();freeTarget(normal);freeTarget(distance);
    }
    constant->Release();world->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
    std::printf("WORLD_NORMAL_MSAA_GPU_%s cases=%u modelCoverage=0/1 depthPatterns=6 nativeState=exact\n",equal?"PASS":"FAIL",cases);return equal?0:1;
}
