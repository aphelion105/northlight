// Image contracts for neutral relighting and full-resolution material silhouettes.
#include "lighting_gpu_support.h"
#include <functional>
using Pixel=std::array<float,4>;
using Bank=std::array<Pixel,120>;
struct Device {
    HWND window;HMODULE module;IDirect3D9* api;IDirect3DDevice9* d;
    std::vector<IDirect3DTexture9*> textures;
    Device(const char* backend){
        WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"RelightingColorEdges";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
        window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
        module=LoadLibraryA(backend);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);api=create(D3D_SDK_VERSION);assert(api);
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
        ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
        ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
        for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(s,FALSE));
        ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        for(unsigned s=0;s<16;++s){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));}
    }
    void image(unsigned slot,unsigned w,unsigned h,const std::function<Pixel(unsigned,unsigned)>& f){auto* t=pixels(d,w,h,f);textures.push_back(t);ok(d->SetTexture(slot,t));}
    void solid(unsigned slot,Pixel p){image(slot,1,1,[&](unsigned,unsigned){return p;});}
    std::vector<float> run(IDirect3DPixelShader9* ps,const Bank& c,unsigned w,unsigned h){
        IDirect3DSurface9 *target{},*staging{};ok(d->CreateRenderTarget(w,h,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
        ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->SetRenderTarget(0,target));
        D3DVIEWPORT9 vp{0,0,w,h,0,1};ok(d->SetViewport(&vp));ok(d->SetPixelShaderConstantF(0,c[0].data(),unsigned(c.size())));ok(d->SetPixelShader(ps));
        const Vertex q[]={{-.5f,-.5f,0,1,0,0},{float(w)-.5f,-.5f,0,1,1,0},{-.5f,float(h)-.5f,0,1,0,1},{float(w)-.5f,float(h)-.5f,0,1,1,1}};
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(w*h*4);
        for(unsigned y=0;y<h;++y)std::memcpy(out.data()+y*w*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,w*16);
        ok(staging->UnlockRect());staging->Release();target->Release();return out;
    }
    ~Device(){d->SetPixelShader(nullptr);for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);for(auto t:textures)t->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(L"RelightingColorEdges",GetModuleHandleW(nullptr));}
};
static float depth(float z){return 100.f/(100-.2f)*(1-.2f/z);}
static float linear(float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
static Bank constants(unsigned w,unsigned h){Bank c{};c[0]={1.f/w,1.f/h,.2f,100};c[1]={1,1,1,0};c[2][0]=1;c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[20][1]=c[20][2]=1;c[25]={0,1,1,0};c[30][2]=1;c[33]={float(w),float(h),float(w/2),float(h/2)};c[80]={1,1,1,1};return c;}
int main(int argc,char** argv){
    assert(argc==3);Device gpu(argv[1]);const std::string dir=argv[2];unsigned failures=0,cases=0;float hueError=0,edgeError=0;
    for(unsigned slot=0;slot<16;++slot)gpu.solid(slot,{0,0,0,0});
    gpu.solid(1,{depth(10),0,0,0});gpu.solid(9,{0,0,0,1});gpu.solid(10,{0,0,0,1});gpu.solid(15,{10,0,0,0});
    // Equal incident/removal RGB must change brightness, not chromaticity.
    // Include values below/above the albedo bound and saturated single channels.
    for(const char* name:{"WorldComposite","WorldCompositeHdr","WorldPrelitComposite","WorldPrelitCompositeHdr"}){
        auto* ps=shader(gpu.d,dir+"/"+name+".bin");bool hdr=std::string(name).find("Hdr")!=std::string::npos,prelit=std::string(name).find("Prelit")!=std::string::npos;
        for(Pixel native:{Pixel{.8f,.6f,.5f,1},Pixel{.12f,.08f,.04f,1},Pixel{.15f,.75f,.25f,1},Pixel{0,0,.8f,1},Pixel{0,0,0,1}})
        for(float base:{.2f,.7f})for(float correction:{-.1f,0.f,.1f}){
            Bank c=constants(8,8);c[18]={base,base,base,0};c[81][0]=prelit?1.f:0.f;Pixel source=native;if(hdr)for(unsigned k=0;k<3;++k)source[k]=linear(source[k]);
            gpu.solid(0,native);gpu.solid(2,hdr?source:Pixel{});gpu.solid(8,{correction,correction,correction,0});gpu.solid(14,{correction,correction,correction,0});gpu.solid(12,{base,base,base,1});
            const auto result=gpu.run(ps,c,8,8);float peak=std::max({source[0],source[1],source[2]});
            for(unsigned k=0;k<3;++k){const float expected=source[k]+source[k]/std::max(base,peak)*correction;const float error=std::fabs(result[k]-expected);hueError=std::max(hueError,error);failures+=error>3e-5f;}
            ++cases;
        }ps->Release();
    }
    // Resident GI attenuates baked ambient. A proven explicit room bank
    // retains its authored fill. Missing probes/GI mix preserve native light;
    // incoming lamp radiance is added after attenuation, with its own hue.
    for(const char* name:{"WorldPrelitComposite","WorldPrelitCompositeHdr"}){
        auto* ps=shader(gpu.d,dir+"/"+name+".bin");const bool hdr=std::string(name).find("Hdr")!=std::string::npos;
        for(float authored:{0.f,1.f})for(float ambient:{.25f,.5f})for(float resident:{0.f,1.f})for(float occlusion:{0.f,.5f,1.f})for(float lamp:{0.f,.08f}){
            Bank c=constants(8,8);c[81]={resident,authored,0,0};c[80][1]=0;c[18]={ambient,ambient,ambient,0};
            Pixel native{.4f,.3f,.2f,1},source=native;if(hdr)for(unsigned k=0;k<3;++k)source[k]=linear(source[k]);
            gpu.solid(0,native);gpu.solid(2,hdr?source:Pixel{});gpu.solid(8,{});
            gpu.solid(14,{0,0,0,occlusion});gpu.solid(12,{.5f,.5f,.5f,1});gpu.solid(3,{0,lamp,0,0});
            const auto result=gpu.run(ps,c,8,8);float kept=1-resident*occlusion*(1-authored)*(ambient/.5f);
            float expectedGreen=source[1]*kept;
            const float incoming=source[1]/std::max(authored?ambient:.5f,source[0])*lamp;
            expectedGreen+=hdr?incoming:incoming/(1+incoming/(1-expectedGreen));
            for(unsigned k=0;k<3;++k){const float expected=k==1?expectedGreen:source[k]*kept;
                if(std::fabs(result[k]-expected)>3e-5f){++failures;std::printf("ROOM_GI_FAIL hdr=%u authored=%.0f resident=%.0f occ=%.1f lamp=%.2f channel=%u got=%g expected=%g\n",unsigned(hdr),authored,resident,occlusion,lamp,k,result[k],expected);}}
            ++cases;
        }ps->Release();
    }
    gpu.solid(3,{});
    // The same foreground plane, translated against a different-depth facade.
    // Half-res normals switch every two pixels; full-res material coverage does not.
    for(const char* name:{"MaterialComposite","MaterialCompositeHdr"}){
        auto* ps=shader(gpu.d,dir+"/"+name+".bin");const bool hdr=std::string(name).find("Hdr")!=std::string::npos;
        for(unsigned w:{64u,65u}){const unsigned h=48;Bank c=constants(w,h);c[18]={.5f,.5f,.5f,0};c[73]={1,100,1,0};c[74]={-1,0,0,0};c[76]={.3f,.3f,.3f,0};
            gpu.solid(0,{.4f,.4f,.4f,1});gpu.solid(4,{0,0,0,1});gpu.solid(5,{.4f,.4f,.4f,1});gpu.solid(8,{0,0,0,1});gpu.solid(12,{.5f,.5f,.5f,1});
            for(unsigned phase=0;phase<4;++phase){
                auto front=[&](unsigned x,unsigned y){return x<12+y*2/3+phase;};
                gpu.image(1,w,h,[&](unsigned x,unsigned y){return Pixel{depth(front(x,y)?10.f:30.f),0,0,0};});
                gpu.image(9,w,h,[&](unsigned x,unsigned y){return front(x,y)?Pixel{1,11.f/14,-10,1}:Pixel{0,0,0,0};});
                gpu.image(14,w/2,h/2,[&](unsigned x,unsigned y){unsigned px=unsigned((x+.5f)*w/(w/2)),py=2*y+1;return front(px,py)?Pixel{0,0,-1,1}:Pixel{1,0,0,1};});
                const auto result=gpu.run(ps,c,w,h);const float expected=.4f+(hdr?linear(.4f):.4f)/.5f*.18f;
                for(unsigned y=3;y<h-3;++y)for(unsigned x=3;x<w-3;++x)if(front(x,y)){
                    float error=std::fabs(result[(y*w+x)*4]-expected);edgeError=std::max(edgeError,error);failures+=error>2e-4f;
                }++cases;
            }
            // A one-pixel feature absent from the half-resolution normal
            // buffer must keep native shading, never borrow the facade normal.
            unsigned thin=3;
            for(;;++thin){bool sampled=false;for(unsigned x=0;x<w/2;++x)sampled|=unsigned((x+.5f)*w/(w/2))==thin;if(!sampled)break;}
            gpu.image(1,w,h,[&](unsigned x,unsigned){return Pixel{depth(x==thin?10.f:30.f),0,0,0};});
            gpu.image(9,w,h,[&](unsigned x,unsigned){return x==thin?Pixel{1,11.f/14,-10,1}:Pixel{0,0,0,0};});
            gpu.solid(14,{1,0,0,1});
            const auto tiny=gpu.run(ps,c,w,h);
            for(unsigned y=3;y<h-3;++y){float error=std::fabs(tiny[(y*w+thin)*4]-.4f);edgeError=std::max(edgeError,error);failures+=error>2e-4f;}
            ++cases;
        }ps->Release();
    }
    std::printf("RELIGHTING_COLOR_EDGES cases=%u hueError=%.8g edgeError=%.8g failures=%u\n",cases,hueError,edgeError,failures);
    return failures?1:0;
}
