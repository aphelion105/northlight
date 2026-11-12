// Hidden D3D9 device, production bytecode, homogeneous-medium analytic oracle.
#define main water_endpoint_unused_main
#include "water_fog_endpoint_gpu.cpp"
#undef main
#include <array>
#include "forever_atmosphere.h"

static Image rgba(float r,float g,float b,float a){
    Image v{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
    const float p[]={r,g,b,a};std::memcpy(v.pixels.data(),p,16);return v;
}
int main(int argc,char** argv){
    assert(argc==3);constexpr unsigned size=8;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightAtmosphericFogOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[2]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(size,size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    D3DVIEWPORT9 vp={0,0,size,size,0,1};ok(d->SetViewport(&vp));
    Vertex quad[]={{-.5f,-.5f,0,1,0,0},{size-.5f,-.5f,0,1,1,0},{-.5f,size-.5f,0,1,0,1},{size-.5f,size-.5f,0,1,1,1}};
    std::array<IDirect3DTexture9*,16> textures{};
    auto bind=[&](unsigned slot,const Image& im){ok(d->SetTexture(slot,nullptr));if(textures[slot])textures[slot]->Release();textures[slot]=texture(d,im);ok(d->SetTexture(slot,textures[slot]));};
    for(unsigned s=0;s<16;++s){
        bind(s,rgba(1,1,1,1));
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));
        ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
    }
    IDirect3DVolumeTexture9* volume=nullptr;ok(d->CreateVolumeTexture(1,1,1,1,0,D3DFMT_L8,D3DPOOL_MANAGED,&volume,nullptr));
    D3DLOCKED_BOX vb{};ok(volume->LockBox(0,&vb,nullptr,0));*static_cast<unsigned char*>(vb.pBits)=255;ok(volume->UnlockBox(0));
    unsigned cases=0;double maximum=0;
    for(const char* entry:{"ForeverFog","ForeverFogActor","ForeverFogTransition","ForeverFogActorTransition","WorldFog","WorldFogActor","WorldFogActorTiled","MoonFloorFog","MoonFloorFogActor","MoonFloorFogActorTiled"}){
        const auto code=read(std::string(argv[1])+"/"+entry+".bin");IDirect3DPixelShader9* shader=nullptr;
        ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shader));ok(d->SetPixelShader(shader));
        const bool floor=std::string(entry).find("MoonFloor")==0;
        const bool imported=std::string(entry).find("Forever")==0;
        ok(d->SetTexture(14,imported?static_cast<IDirect3DBaseTexture9*>(volume):textures[14]));
        for(float g:{0.f,.9f,-.5f})for(float cosine:{-1.f,0.f,1.f})for(float height:{-1.f,2.f,20.f,160.f})for(float density:{0.f,1.f,1.3f,1.8f})for(float distance:{2.f,40.f,100.f})for(bool blocked:{false,true})for(bool moon:{false,true}){
            if(!imported&&(g!=0||cosine!=1))continue;
            if(floor&&!moon)continue; // The host selects the floor pass only for moonlight.
            const float clipNear=.5f,clipFar=1000,top=144;
            float c[116][4]{};c[89][0]=c[89][1]=1;
            c[0][0]=c[0][1]=1.f/size;c[0][2]=clipNear;c[0][3]=clipFar;
            c[1][0]=c[1][1]=1e12f;c[1][2]=1;c[2][0]=1;c[2][2]=192;c[2][3]=1.f/1024;
            c[3][1]=c[4][2]=c[5][0]=1;c[6][3]=1;c[15][2]=height;
            c[14][2]=.5f;c[14][3]=1;c[16][0]=imported?cosine:1;c[16][1]=imported?std::sqrt(1-cosine*cosine):0;c[16][3]=moon?1.f:0.f;
            const float direct[]={.7f,.9f,.35f},ambient[]={.2f,.25f,.15f};std::memcpy(c[17],direct,12);std::memcpy(c[18],ambient,12);
            c[21][0]=1;c[21][1]=5.16f;c[21][2]=64;c[21][3]=128;c[22][0]=c[22][1]=c[22][2]=1;c[22][3]=.0035f*density;c[27][0]=1;
            c[31][0]=c[31][1]=-256;c[31][2]=1.f/512;c[32][2]=c[32][3]=top;
            c[33][0]=c[33][1]=size;c[33][2]=c[33][3]=size;c[58][0]=3.5f;c[58][1]=1e6f;c[59][3]=.0017f*density;
            bind(1,rgba(clipFar/(clipFar-clipNear)*(1-clipNear/distance),0,0,0));
            bind(3,rgba(blocked?0.f:1.f,0,0,0));bind(13,rgba(0,0,0,1.25f));
            ok(d->SetPixelShaderConstantF(0,c[0],116));
            if(imported){NorthlightForever::Frame f;for(unsigned layer=0;layer<3;++layer){auto& l=f.layer[layer];l.p[1]=(.52f/3)*density;l.flag[3]=1;l.flag[0]=1;l.p[9]=1;l.p[10]=g;
                for(unsigned k=0;k<3;++k){l.color[0][k]=.5f;l.color[1][k]=.25f;l.color[2][k]=.25f;}}
                float bank[38][4];NorthlightForever::constants(f,bank,128,1);ok(d->SetPixelShaderConstantF(116,bank[0],38));}

            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
            // World-plane intervals use midpoint near fading, independently enumerate
            // their covered path here; the homogeneous extinction then has a closed form.
            double path=0;for(unsigned i=0;i<49;++i){double a=i*128./48,b=std::min((i+1)*128./48,double(distance));if(a>=distance)break;if((a+b)*.5>3.5)path+=b-a;}
            const double vertical=std::clamp(1-double(height)/top,0.,1.);
            const double sigma=height>=0?.0052*density*vertical*vertical:0;
            const double transmission=std::exp(-sigma*path),phase=.03*.2775/std::pow(.0225,1.5)+.253;
            const double fade=std::clamp(double(height)/12,0.,1.);
            const double weight=blocked?0:(1-transmission)*(moon?fade*fade*(3-2*fade):1);
            const double cap=64/(64+direct[1]*5.16*phase*weight);
            D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){float value[4];std::memcpy(value,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,16);
                for(unsigned k=0;k<4;++k){const double legacyExpected=k==3?transmission:ambient[k]*.35*(1-transmission)+direct[k]*5.16*phase*weight*cap;
                    const double it=height>=0?std::exp(-.0052*density*distance):1;
                    const double il=std::pow((.5+.055)/1.055,2.4),ia=std::pow((.25+.055)/1.055,2.4);
                    const double expected=imported?(k==3?it:(ia+(blocked?0:il*(1-double(g)*g)/std::pow(1+double(g)*g-2*double(g)*cosine,1.5)))*(1-it)):legacyExpected;
                    double error=std::abs(value[k]-expected);maximum=std::max(maximum,error);
                    if(!std::isfinite(value[k])||error>4e-4*std::max(1.,std::abs(expected))){std::fprintf(stderr,"FAIL %s height=%g density=%g distance=%g blocked=%d moon=%d channel=%u actual=%g expected=%g\n",entry,height,density,distance,int(blocked),int(moon),k,value[k],expected);return 1;}
                }
            }
            ok(staging->UnlockRect());++cases;
        }
        ok(d->SetPixelShader(nullptr));shader->Release();
    }
    std::printf("PASS atmospheric fog GPU: %u cases, max error %.9g; ground sunlight, blocked rays, density, finite height, near fade, moon response\n",cases,maximum);
    for(unsigned s=0;s<16;++s){d->SetTexture(s,nullptr);textures[s]->Release();}
    volume->Release();
    staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
