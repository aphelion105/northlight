// Standalone hidden D3D9 device; exercises the production shader bytecode.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main northlight_composite_replay_unused
#include "composite_distance_gpu.cpp"
#undef main
#pragma clang diagnostic pop
#include "saved_state.h"

static Image rgba(float r,float g,float b,float a){Image v{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};const float f[]={r,g,b,a};std::memcpy(v.pixels.data(),f,16);return v;}
static Image red(float value){Image v{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(v.pixels.data(),&value,4);return v;}
int main(int argc,char** argv){
    assert(argc==4);constexpr unsigned size=8;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightContactFogOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[3]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[2]{};for(unsigned i=0;i<2;++i){const auto code=read(argv[i+1]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[i]));}
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(size,size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    D3DVIEWPORT9 vp={0,0,size,size,0,1};ok(d->SetViewport(&vp));
    Vertex quad[]={{-.5f,-.5f,0,1,0,0},{size-.5f,-.5f,0,1,1,0},{-.5f,size-.5f,0,1,0,1},{size-.5f,size-.5f,0,1,1,1}};
    for(unsigned s=0;s<16;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    // The exact outer production guard restores the two new constant slots.
    const float sentinel[]={.17f,.28f,.39f,.41f,.52f,.63f,.74f,.85f};ok(d->SetPixelShaderConstantF(5,sentinel,2));
    {SavedState saved(d);assert(saved.ok);float zero[8]{};ok(d->SetPixelShaderConstantF(5,zero,2));}
    float restored[8]{};ok(d->GetPixelShaderConstantF(5,restored,2));assert(!std::memcmp(sentinel,restored,sizeof sentinel));
    unsigned checked=0,failures=0;double maximum=0;
    for(unsigned mode=0;mode<2;++mode)for(float t:{0.f,.25f,.8f,1.f})for(float ao:{0.f,.35f,1.f})for(bool known:{false,true})for(bool darkFog:{false,true})for(bool relighting:{false,true}){
        if(mode&&relighting)continue;
        const float effective=known?t:1.f;
        const float fog[]={.25f,.35f,.45f},surface[]={.7f,.6f,.5f};float scene[3];
        for(unsigned k=0;k<3;++k)scene[k]=darkFog?.03f:effective*surface[k]+(1-effective)*fog[k];
        std::vector<IDirect3DTexture9*> textures;
        const auto bind=[&](unsigned slot,const Image& im){auto tex=texture(d,im);textures.push_back(tex);ok(d->SetTexture(slot,tex));};
        for(unsigned slot=0;slot<16;++slot)bind(slot,rgba(0,0,0,0));
        const float clipNear=.2f,clipFar=1000.f,z=25.f,depth=clipFar/(clipFar-clipNear)*(1-clipNear/z);
        bind(0,rgba(scene[0],scene[1],scene[2],1));bind(1,red(depth));
        float c[68][4]{};c[0][0]=c[0][1]=1.f/size;c[0][2]=clipNear;c[0][3]=clipFar;
        float fogParameters[]={0,t,1,known?1.f:0.f};
        if(mode==0){
            c[1][0]=c[1][1]=c[1][2]=1;c[2][0]=1;c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;
            c[33][0]=c[33][1]=size;c[33][2]=c[33][3]=size/2;
            std::memcpy(c[25],fogParameters,16);std::memcpy(c[26],fog,12);
            bind(8,relighting?rgba(-.2f,.1f,-.05f,1):rgba(0,0,0,1));bind(9,rgba(0,0,0,1));bind(10,rgba(0,0,0,ao));bind(12,rgba(1,1,1,1));bind(15,red(z));
        }else{
            c[1][0]=c[1][1]=1;c[2][1]=2;c[3][1]=c[3][2]=1;
            std::memcpy(c[5],fogParameters,16);std::memcpy(c[6],fog,12);bind(2,rgba(0,0,0,ao));
        }
        ok(d->SetPixelShaderConstantF(0,c[0],68));ok(d->SetPixelShader(shaders[mode]));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){float actual[4];std::memcpy(actual,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,16);
            for(unsigned k=0;k<4;++k){const float fogPart=k<3?std::min((1-effective)*fog[k],scene[k]):0;const float transported=k<3?(scene[k]-fogPart)*ao:0;float expected=k<3?transported+fogPart:1;
                if(k<3&&relighting){const float delta[]={-.2f,.1f,-.05f};expected=std::max(expected+std::min(transported,effective)*delta[k],expected-.45f*transported);}
                const double error=std::fabs(actual[k]-expected);maximum=std::max(maximum,error);
                if(!std::isfinite(actual[k])||error>2e-5){if(failures++<4)std::printf("FAIL mode=%u T=%.2f AO=%.2f known=%d dark=%d rgb=%u got=%.8f expected=%.8f\n",mode,t,ao,int(known),int(darkFog),k,actual[k],expected);}
            }
        }
        ok(staging->UnlockRect());for(unsigned slot=0;slot<16;++slot)ok(d->SetTexture(slot,nullptr));for(auto tex:textures)tex->Release();++checked;
    }
    std::printf("contact fog GPU cases=%u checkedPixels=%u maxError=%.9g failures=%u stateRestored=1\n",checked,checked*size*size,maximum,failures);
    d->SetPixelShader(nullptr);for(auto shader:shaders)shader->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
