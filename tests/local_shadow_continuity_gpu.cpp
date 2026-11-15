// Subtexel receiver motion through analytical cube occlusion boundaries.
#define main unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
using V=std::array<float,3>;
static V plus(V a,V b,float scale=1){for(unsigned k=0;k<3;++k)a[k]+=b[k]*scale;return a;}
static float dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static const V forward[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
static const V right[]={{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}};
static const V up[]={{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}};
int main(int argc,char** argv){
    assert(argc==3);const std::string folder=std::string(argv[2])+"/";constexpr unsigned W=2048;
    auto window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[4]{};const char* names[]={"current-hard","current-soft","previous-hard","previous-soft"};
    for(unsigned i=0;i<4;++i){auto b=read(folder+names[i]+".bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(b.data()),&shaders[i]));}
    IDirect3DSurface9 *target=nullptr,*stage=nullptr;ok(d->CreateRenderTarget(W,1,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,1,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&stage,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 viewport{0,0,W,1,0,1};ok(d->SetViewport(&viewport));
    ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE})ok(d->SetRenderState(s,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    for(auto s:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(1,s,D3DTEXF_POINT));ok(d->SetSamplerState(1,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,.5f,0,1,0,1},{W-.5f,.5f,0,1,1,1}};
    unsigned cases=0,negativeCases=0;float worstStep=0,worstError=0,oldStep=0;
    for(unsigned resolution:{128u,256u,512u}){
        IDirect3DCubeTexture9* cube=nullptr;ok(d->CreateCubeTexture(resolution,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&cube,nullptr));
        for(unsigned face=0;face<6;++face)for(unsigned axis=0;axis<2;++axis)for(unsigned location=0;location<3;++location)for(float blocker:{9.9f,3.f}){
            // Centre, face edge and three-face corner. The occluder boundary
            // passes through the receiver path, independently of cube layout.
            const V direction=plus(plus(forward[face],axis?up[face]:right[face],location?1.f:0.f),axis?right[face]:up[face],location==2?1.f:0.f);
            const V along=axis?up[face]:right[face];
            const V cut=plus(along,forward[face],location?-1.f:0.f);
            for(unsigned f=0;f<6;++f){D3DLOCKED_RECT lock{};ok(cube->LockRect(D3DCUBEMAP_FACES(f),0,&lock,nullptr,0));
                for(unsigned y=0;y<resolution;++y)for(unsigned x=0;x<resolution;++x){
                    const V ray=plus(plus(forward[f],right[f],2*(x+.5f)/resolution-1),up[f],1-2*(y+.5f)/resolution);
                    const float a=64.f/63.9f;const float z=dot(cut,ray)<0?a*(1-.1f/blocker):1.f;
                    std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&z,4);
                }ok(cube->UnlockRect(D3DCUBEMAP_FACES(f),0));}
            ok(d->SetTexture(1,cube));float c[84][4]{};c[9][0]=.1f;c[9][1]=64;c[9][2]=1.f/resolution;c[9][3]=1;
            std::memcpy(c[80],direction.data(),12);std::memcpy(c[81],along.data(),12);c[82][0]=8.f/resolution;c[82][1]=10;
            for(unsigned k=0;k<3;++k)c[83][k]=-forward[face][k];ok(d->SetPixelShaderConstantF(0,c[0],84));
            for(unsigned variant=0;variant<4;++variant){
                ok(d->SetPixelShader(shaders[variant]));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));
                float maxStep=0,error=0,previous=0,low=1,high=0;
                std::vector<float> values(W);
                for(unsigned x=0;x<W;++x){float value;std::memcpy(&value,static_cast<const char*>(lock.pBits)+x*16,4);assert(std::isfinite(value)&&value>=-.0001f&&value<=1.0001f);
                    if(x)maxStep=std::max(maxStep,std::fabs(value-previous));previous=value;low=std::min(low,value);high=std::max(high,value);values[x]=value;
                    if(location==0&&(variant%2==0||blocker>9)){
                        const float expected=std::clamp(.5f+((x+.5f)/W-.5f)*4,0.f,1.f);error=std::max(error,std::fabs(value-expected));
                    }
                }ok(stage->UnlockRect());
                if(variant<2){
                    if(maxStep>.04f||error>.002f){std::printf("FAIL size=%u face=%u axis=%u location=%u blocker=%g variant=%u step=%g error=%g range=%g..%g\n",resolution,face,axis,location,blocker,variant,maxStep,error,low,high);std::fflush(stdout);}
                    assert(maxStep<.04f&&error<.002f);worstStep=std::max(worstStep,maxStep);worstError=std::max(worstError,error);++cases;
                }else {oldStep=std::max(oldStep,maxStep);if(maxStep>.2f)++negativeCases;}
                if(resolution==256&&face==4&&axis==0&&location==0&&blocker>9){std::ofstream file(folder+names[variant]+".f32",std::ios::binary);file.write(reinterpret_cast<const char*>(values.data()),values.size()*4);}
            }
        }
        ok(d->SetTexture(1,nullptr));cube->Release();
    }
    assert(negativeCases>=72&&oldStep>=.25f);
    std::printf("LOCAL_SHADOW_CONTINUITY_PASS cases=%u oldFailures=%u maxStep=%.7f oldStep=%.7f maxAnalyticalError=%.7f\n",cases,negativeCases,worstStep,oldStep,worstError);
    for(auto* shader:shaders)shader->Release();stage->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
