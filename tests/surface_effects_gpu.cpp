// Production bytecode on a separate hidden D3D9 device; no WoW module or account.
#define main northlight_unused_distance_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "surface_compiled_shaders.h"
#include <array>
constexpr unsigned W=160,H=128;
static Image floats(unsigned channels){return {W,H,channels==4?D3DFMT_A32B32G32R32F:channels==2?D3DFMT_G32R32F:D3DFMT_R32F,channels*4,std::vector<unsigned char>(W*H*channels*4)};}
static void put(Image& im,unsigned x,unsigned y,const float* value){std::memcpy(im.pixels.data()+(y*W+x)*im.bytes,value,im.bytes);}
static float projected(float distance){return 1000.f/(1000.f-.1f)*(1-.1f/distance);}
int main(int argc,char** argv){
    assert(argc==2);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightSurfaceOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *target=nullptr,*readback=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    IDirect3DTexture9* reflectionTarget=nullptr;IDirect3DSurface9* reflectionSurface=nullptr;
    ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&reflectionTarget,nullptr));ok(reflectionTarget->GetSurfaceLevel(0,&reflectionSurface));
    const DWORD* codes[]={kSurfaceDepthCopyShader,kWaterQualityShader,kWaterBalancedShader,kWaterPerformanceShader,kPrecipitationQualityShader,kPrecipitationPerformanceShader,kWaterPointLightsShader,kWaterSSRShader};IDirect3DPixelShader9* shaders[8]={};for(unsigned i=0;i<8;++i)ok(d->CreatePixelShader(codes[i],&shaders[i]));
    float c[24][4]={};c[22][0]=c[22][1]=c[22][2]=c[22][3]=1;c[23][0]=32;c[23][1]=128;c[23][2]=.3f;c[23][3]=4;
    c[20][1]=c[20][2]=1;c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.1f;c[0][3]=1000;c[1][0]=1.3f;c[1][1]=1.7f;c[1][2]=-1;c[1][3]=7;c[2][1]=1;
    c[4][1]=1;c[5][2]=1;c[6][0]=-1;c[7][2]=3;c[7][3]=1;c[8][0]=c[8][1]=c[8][2]=.1f;c[10][2]=1;
    const Vertex vertices[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    float waterHeight=3;bool blueBed=false;
    auto render=[&](unsigned entry,bool wet,bool foreground,bool flat,bool blue){
        auto color=floats(4),before=floats(1),mask=floats(2),final=floats(1);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float rgba[]={.08f,.14f,.18f,1},z=1000,m[]={0,0},fz=1000;
            if(y>H/2){const float slope=(2.f*y/H-1)/c[1][1];z=(waterHeight+2)/slope;float water=flat?10:waterHeight/slope;m[0]=water;m[1]=wet?.7f:0;fz=z;if(blueBed){rgba[0]=.9f;rgba[2]=.01f;}}
            else{z=fz=20;rgba[0]=blue?.01f:.9f;rgba[2]=blue?.9f:.01f;}
            if(foreground)fz=1;z=projected(z);fz=projected(fz);put(color,x,y,rgba);put(before,x,y,&z);put(mask,x,y,m);put(final,x,y,&fz);}
        // The static world/units appear only in the completed scene, after the pre-water snapshot.
        auto early=color;auto earlyDepth=before;
        for(unsigned y=0;y<=H/2;++y)for(unsigned x=0;x<W;++x){const float gray[]={.1f,.1f,.1f,1},sky=1;put(early,x,y,gray);put(earlyDepth,x,y,&sky);}
        IDirect3DTexture9* inputs[]={texture(d,early),texture(d,earlyDepth),texture(d,mask),texture(d,final),texture(d,color)};
        ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
        d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(shaders[entry]);d->SetPixelShaderConstantF(0,&c[0][0],24);
        for(unsigned i=0;i<4;++i){d->SetTexture(i,inputs[i]);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE);}
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})d->SetRenderState(state,0);
        d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x00143250,1,0));ok(d->BeginScene());
        if(entry>=1&&entry<=3&&c[22][3]>.5f){
            d->SetTexture(4,nullptr);ok(d->SetRenderTarget(0,reflectionSurface));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetPixelShader(shaders[7]);d->SetTexture(0,inputs[4]);
            ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));
            ok(d->SetRenderTarget(0,target));d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetPixelShader(shaders[entry]);d->SetTexture(0,inputs[0]);
            d->SetTexture(4,reflectionTarget);for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(4,state,D3DTEXF_POINT);
        }
        ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,readback));
        D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<std::array<float,4>> pixels(W*H);
        for(unsigned y=0;y<H;++y)std::memcpy(pixels.data()+y*W,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*16);readback->UnlockRect();for(unsigned i=0;i<5;++i)d->SetTexture(i,nullptr);for(auto* input:inputs)input->Release();
        for(auto& p:pixels)for(float v:p)assert(std::isfinite(v)&&v>=0&&v<4);return pixels;
    };
    unsigned cases=0;const std::array<float,4> clear={20.f/255,50.f/255,80.f/255,0};
    auto unchanged=[&](const auto& p){for(unsigned at=0;at<p.size();++at)for(unsigned i=0;i<4;++i)if(std::fabs(p[at][i]-clear[i])>=.00001f){std::fprintf(stderr,"Unchanged case=%u pixel=%u,%u channel=%u got=%.9g expected=%.9g\n",cases,at%W,at/W,i,p[at][i],clear[i]);std::abort();}++cases;};
    for(unsigned entry=1;entry<=3;++entry){unchanged(render(entry,false,false,false,false));unchanged(render(entry,true,true,false,false));unchanged(render(entry,true,false,true,false));
        auto red=render(entry,true,false,false,false),blue=render(entry,true,false,false,true);unsigned reflected=0,changed=0;
        for(unsigned y=H/2+3;y<H-2;++y)for(unsigned x=2;x<W-2;++x){auto i=y*W+x;reflected+=std::fabs(red[i][0]-blue[i][0])>.002f;changed+=std::fabs(red[i][0]-clear[0])>.002f;}
        assert(changed>100&&reflected>10);cases+=2;}
    // Native fog applies at the liquid surface, for every preset and camera handedness.
    for(unsigned entry=1;entry<=3;++entry){
        auto dry=render(entry,true,false,false,false);
        c[20][3]=1;c[21][0]=.23f;c[21][1]=.12f;c[21][2]=.37f;
        for(float sign:{-1.f,1.f}){
            c[1][2]=sign;c[6][0]=sign;
            for(float power:{1.f,2.f}){
                c[20][0]=-sign/40;c[20][1]=1;c[20][2]=power;auto fogged=render(entry,true,false,false,false);
                for(unsigned y=H/2+4;y<H-2;++y)for(unsigned x=2;x<W-2;++x){unsigned at=y*W+x;if(dry[at][3]<.69f)continue;
                    const float z=waterHeight*c[1][1]/(2.f*y/H-1),t=std::pow(std::max(0.f,1-z/40),power);
                    for(unsigned k=0;k<3;++k){const float expected=(dry[at][k]-clear[k]*.3f)*t+c[21][k]*(1-t)*.7f+clear[k]*.3f;
                        assert(std::fabs(fogged[at][k]-expected)<.0002f);}}
                ++cases;
            }
        }
        c[20][0]=0;c[20][1]=c[20][2]=1;c[20][3]=0;c[1][2]=-1;c[6][0]=-1;
    }
    // Smooth liquid stays covered at grazing incidence; SSR must not reflect the submerged bed.
    c[1][1]=16;waterHeight=c[7][2]=.3f;
    for(unsigned entry=1;entry<=3;++entry)for(float time:{0.f,3.7f,19.f})for(float origin:{-6000.f,0.f,6000.f}){
        c[7][0]=origin;c[7][1]=origin*.71f;c[1][3]=time;blueBed=false;auto a=render(entry,true,false,false,false);blueBed=true;auto b=render(entry,true,false,false,false);
        for(unsigned y=H/2+4;y<H/2+18;++y)for(unsigned x=4;x<W-4;++x){unsigned at=y*W+x;
            assert(a[at][3]>.69f);for(unsigned k=0;k<3;++k)assert(std::fabs(a[at][k]-b[at][k])<.03f);}
        cases+=2;
    }
    blueBed=false;c[7][0]=c[7][1]=0;c[1][1]=1.7f;waterHeight=c[7][2]=3;
    for(unsigned entry=4;entry<6;++entry){c[3][0]=0;unchanged(render(entry,false,false,false,false));c[3][0]=1;unchanged(render(entry,false,true,false,false));
        for(unsigned snow=0;snow<2;++snow){c[3][1]=float(snow);unsigned changed=0;for(unsigned phase=0;phase<8;++phase){c[1][3]=float(phase)*.31f;auto p=render(entry,false,false,false,false);for(auto& pixel:p)changed+=std::fabs(pixel[0]-clear[0])>.00001f;++cases;}assert(changed>0);}}
    c[19][0]=0;unchanged(render(6,true,false,false,false));
    c[19][0]=1;c[11][0]=20;c[11][2]=5;c[11][3]=50;c[15][0]=3;c[15][1]=2;c[15][2]=1;c[15][3]=.02f;
    unchanged(render(6,true,true,false,false));auto point=render(6,true,false,false,false);unsigned pointPixels=0;for(auto& p:point)pointPixels+=p[0]>clear[0]+.00001f;assert(pointPixels>0);++cases;
    c[20][1]=0;c[20][3]=1;unchanged(render(6,true,false,false,false));
    c[20][1]=1;c[20][3]=0;
    for(unsigned mode=0;mode<2;++mode){c[23][0]=mode?64:4;c[23][1]=mode?256:8;c[23][2]=mode?2.f:.01f;c[23][3]=mode?6:0;
        for(unsigned entry=1;entry<=3;++entry){render(entry,true,false,false,false);++cases;}}
    std::printf("PASS surface shaders: %u GPU cases; water/SSR, point glints, occlusion, unsupported planes, zero weather, rain and snow finite\n",cases);
    d->SetPixelShader(nullptr);for(auto* s:shaders)s->Release();reflectionSurface->Release();reflectionTarget->Release();readback->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
