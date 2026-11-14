// Compare the production two-filter route with one shared directional resolve.
#define main northlight_unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
static void logf(const char* format,...){va_list args;va_start(args,format);std::vfprintf(stderr,format,args);va_end(args);}
#include "gpu_frame_timer.h"
#include <chrono>

struct ShadowTarget {
    IDirect3DTexture9* texture=nullptr;
    IDirect3DSurface9* surface=nullptr;
    ShadowTarget(IDirect3DDevice9* d,unsigned w,unsigned h,D3DFORMAT format){
        ok(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&texture,nullptr));
        ok(texture->GetSurfaceLevel(0,&surface));
    }
    ~ShadowTarget(){surface->Release();texture->Release();}
};
int main(int argc,char** argv){
    assert(argc==3);const std::string folder=argv[2];
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightSharedDirectional";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    D3DCAPS9 caps{};ok(d->GetDeviceCaps(&caps));std::printf("CAPS slots=%lu\n",(unsigned long)caps.MaxPixelShader30InstructionSlots);
    IDirect3DPixelShader9* ps[4]={};unsigned n=0;
    for(const auto* name:{"ActorShadow","DirectionalShadow","CombineReference","UnifiedDirectionalShadow"}){
        const auto bytes=read(folder+name+".bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()),&ps[n++]));
    }
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    for(unsigned s=0;s<16;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    IDirect3DQuery9* event=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));
    unsigned cases=0;double worst=0;
    for(unsigned shape=0;shape<12;++shape)for(unsigned phase=0;phase<4;++phase){
        const bool benchmark=phase==0&&(shape==0||shape==1||shape==2||shape==4||shape==7);
        const unsigned W=benchmark?1920:257,H=benchmark?1080:145,N=1024;
        const auto make=[](unsigned w,unsigned h,unsigned channels){return Image{w,h,channels==1?unsigned(D3DFMT_R32F):unsigned(D3DFMT_A32B32G32R32F),channels*4,std::vector<unsigned char>(size_t(w)*h*channels*4)};};
        auto depth=make(W,H,1),normal=make(W,H,4),water=make(W,H,4),nearMap=make(N,N,1),farMap=make(N,N,1),actor=make(N,N,1),tiles=make(N/16,N/16,1);
        auto set=[](Image& im,unsigned x,unsigned y,float v,unsigned channel=0){std::memcpy(im.pixels.data()+(size_t(y)*im.width+x)*im.bytes+channel*4,&v,4);};
        auto get=[](const Image& im,unsigned x,unsigned y){float v;std::memcpy(&v,im.pixels.data()+(size_t(y)*im.width+x)*im.bytes,4);return v;};
        for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){
            const unsigned shift=phase*7;
            float a=1,b=1,c=1;
            if(shape==1)a=b=c=.52f;
            else if(shape>=2){
                const float edge=(x+shift<N/2?.54f:.72f);
                a=shape==2?1:shape==3?edge:(x+shift)%31<3?.54f:1;
                b=shape==3?1:shape==4?edge:((x+y*3+shift)%37<4?.55f:1);
                c=(y+shift)%23<3?.57f:1;
                if(shape==7){a=.597f+.000006f*float(x);b=.598f+.000003f*float(y);c=.59f;}
            }
            set(actor,x,y,a);set(nearMap,x,y,b);set(farMap,x,y,c);
        }
        for(unsigned y=0;y<N/16;++y)for(unsigned x=0;x<N/16;++x){float v=1;for(unsigned dy=0;dy<16;++dy)for(unsigned dx=0;dx<16;++dx)v=std::min(v,get(actor,x*16+dx,y*16+dy));set(tiles,x,y,v);}
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            set(depth,x,y,shape==8?1.f:(1000.f-.2f*1000.f/20.f)/(1000.f-.2f));
            set(normal,x,y,shape==6?.6f:0.f);set(normal,x,y,shape==6?-.3f:0.f,1);set(normal,x,y,shape==6?.74161985f:1.f,2);set(normal,x,y,shape==5?0.f:1.f,3);
            set(water,x,y,5.f);set(water,x,y,shape==9?1.f:0.f,1);
        }
        const std::pair<unsigned,Image*> inputs[]={{1,&depth},{2,&nearMap},{3,&farMap},{10,&tiles},{11,&water},{12,&actor},{14,&normal}};
        std::vector<IDirect3DTexture9*> textures;for(auto input:inputs){auto* t=texture(d,*input.second);textures.push_back(t);ok(d->SetTexture(input.first,t));}
        float c[96][4]={};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.2f;c[0][3]=1000;c[1][0]=c[1][1]=c[1][2]=1;
        c[2][0]=1;c[2][1]=48;c[2][2]=288;c[2][3]=1.f/N;
        for(unsigned k=0;k<4;++k)c[3+k][k]=1;
        c[7][0]=c[8][1]=1.f/48;c[9][2]=1.f/200;c[10][2]=.5f;c[10][3]=1;
        c[11][0]=c[12][1]=1.f/288;c[13][2]=1.f/200;c[14][2]=.5f;c[14][3]=1;
        c[16][2]=1;c[24][0]=.00025f;c[30][0]=shape==9?1.f:0.f;c[33][0]=float(W);c[33][1]=float(H);c[33][2]=float(W);c[33][3]=float(H);c[35][0]=1.f/200;
        c[68][0]=c[69][1]=1.f/12;c[70][2]=1.f/200;c[71][2]=.5f;c[71][3]=1;c[71][0]=float(phase)*.2f;
        c[72][0]=shape==10?0:1;c[72][1]=1.f/N;c[72][2]=12;
        c[10][0]=shape==11?.85f:float(phase)*.1f;
        ok(d->SetPixelShaderConstantF(0,c[0],96));
        ShadowTarget actorOut(d,W,H,D3DFMT_A8R8G8B8),actorPrecise(d,W,H,D3DFMT_A16B16G16R16F),worldOut(d,W,H,D3DFMT_A16B16G16R16F),result(d,W,H,D3DFMT_A16B16G16R16F);
        D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
        const auto draw=[&](IDirect3DSurface9* target,IDirect3DPixelShader9* shader){ok(d->SetRenderTarget(0,target));ok(d->SetPixelShader(shader));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));};
        const auto render=[&](bool unified,bool combine,bool precise=false){
            ok(d->SetTexture(8,nullptr));ok(d->SetTexture(9,nullptr));
            if(unified)draw(result.surface,ps[3]);else{
                draw(precise?actorPrecise.surface:actorOut.surface,ps[0]);draw(worldOut.surface,ps[1]);
                if(combine){ok(d->SetRenderTarget(0,result.surface));ok(d->SetTexture(8,precise?actorPrecise.texture:actorOut.texture));ok(d->SetTexture(9,worldOut.texture));draw(result.surface,ps[2]);}
            }
        };
        IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        std::vector<uint16_t> pixels[3];
        for(unsigned mode=0;mode<3;++mode){ok(d->BeginScene());render(mode==2,true,mode==1);ok(d->EndScene());wait(event);ok(d->GetRenderTargetData(result.surface,staging));D3DLOCKED_RECT locked{};ok(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY));pixels[mode].resize(size_t(W)*H*4);
            for(unsigned y=0;y<H;++y)std::memcpy(pixels[mode].data()+size_t(y)*W*4,static_cast<char*>(locked.pBits)+size_t(y)*locked.Pitch,W*8);ok(staging->UnlockRect());}
        unsigned changed=0;double maxError=0,mean=0,oldError=0;
        for(size_t i=0;i<pixels[0].size();i+=4){const float a=half(pixels[1][i]),b=half(pixels[2][i]);assert(std::isfinite(a)&&std::isfinite(b));const double error=std::fabs(a-b);changed+=a!=b;maxError=std::max(maxError,error);mean+=error;oldError=std::max(oldError,double(std::fabs(half(pixels[0][i])-b)));}
        mean/=double(W)*H;worst=std::max(worst,maxError);
        std::printf("IMAGE shape=%u phase=%u changed=%u maxError=%.9f meanError=%.9f old8BitError=%.9f\n",shape,phase,changed,maxError,mean,oldError);std::fflush(stdout);
        // The high-precision reference retains both original filter algorithms.
        // The production reference differs by at most its removed UNORM8 step.
        assert(maxError<=1./2048&&mean<.00002&&oldError<=1./255+1./2048);++cases;
        if(benchmark){NorthlightGpuFrameTimer timer(d);constexpr unsigned iterations=256;
            for(unsigned block=0;block<16;++block){const bool unified=block%4==1||block%4==2;wait(event);ok(d->BeginScene());const bool timed=timer.begin(block);const auto started=std::chrono::steady_clock::now();
                for(unsigned iteration=0;iteration<iterations;++iteration)render(unified,false);
                const double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()/iterations;timer.end();ok(d->EndScene());wait(event);double gpu=-1;if(timed)timer.poll(gpu);
                std::printf("TIMING shape=%u block=%u warm=%u unified=%u iterations=%u cpuMs=%.6f gpuMs=%.6f\n",shape,block,unsigned(block<4),unsigned(unified),iterations,cpu,gpu<0?gpu:gpu/iterations);std::fflush(stdout);
            }
        }
        staging->Release();for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));for(auto* t:textures)t->Release();
    }
    std::printf("SHARED_DIRECTIONAL_PASS cases=%u worst=%.9f\n",cases,worst);
    for(auto* shader:ps)shader->Release();event->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
