// Experimental common cascade. Geometry is supplied as authored depth images:
// timings charge GPU union/reductions/resolve, but give fine world raster for free.
// This optimistic comparison cannot establish a complete renderer speedup.
#define main northlight_unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
#include <limits>
static void logf(const char* format,...){va_list args;va_start(args,format);std::vfprintf(stderr,format,args);va_end(args);}
#include "gpu_frame_timer.h"

struct Target {
    IDirect3DTexture9* texture=nullptr;IDirect3DSurface9* surface=nullptr;
    unsigned size,channels;
    Target(IDirect3DDevice9* d,unsigned n,unsigned c):size(n),channels(c){
        ok(d->CreateTexture(n,n,1,D3DUSAGE_RENDERTARGET,c==1?D3DFMT_R32F:D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&texture,nullptr));
        ok(texture->GetSurfaceLevel(0,&surface));
    }
    ~Target(){surface->Release();texture->Release();}
    void check(IDirect3DDevice9* d,const Image& expected){
        assert(expected.width==size&&expected.height==size&&expected.bytes==channels*4);
        IDirect3DSurface9* copy=nullptr;ok(d->CreateOffscreenPlainSurface(size,size,channels==1?D3DFMT_R32F:D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&copy,nullptr));
        ok(d->GetRenderTargetData(surface,copy));D3DLOCKED_RECT lock{};ok(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<size;++y)assert(std::memcmp(static_cast<char*>(lock.pBits)+y*lock.Pitch,expected.pixels.data()+size_t(y)*size*channels*4,size*channels*4)==0);
        ok(copy->UnlockRect());copy->Release();
    }
};

static void pass(IDirect3DDevice9* d,Target& target,IDirect3DPixelShader9* shader){
    ok(d->SetRenderTarget(0,target.surface));D3DVIEWPORT9 vp={0,0,target.size,target.size,0,1};ok(d->SetViewport(&vp));ok(d->SetPixelShader(shader));
    const float n=float(target.size);const Vertex q[]={{-.5f,-.5f,0,1,0,0},{n-.5f,-.5f,0,1,1,0},{-.5f,n-.5f,0,1,0,1},{n-.5f,n-.5f,0,1,1,1}};
    ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));
}

int main(int argc,char** argv){
    assert(argc==3);const std::string folder=argv[2];
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightCommonCascade";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* ps[9]={};unsigned n=0;
    for(const auto* name:{"UnifiedDirectionalShadow","CommonReference","CommonDirectional","CommonUnion","CommonBlocks","CommonTiles","ActorTiles","CommonKernel","CommonKernelReference"}){const auto bytes=read(folder+name+".bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()),&ps[n++]));}
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    for(unsigned s=0;s<16;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    IDirect3DQuery9* event=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));unsigned cases=0;double worst=0;
    for(unsigned shape=0;shape<10;++shape)for(unsigned phase=0;phase<3;++phase){
        const bool benchmark=phase==0&&(shape==0||shape==1||shape==2||shape==3||shape==4||shape==8);
        const unsigned W=benchmark?960:257,H=benchmark?540:145,N=1024;
        const auto make=[](unsigned w,unsigned h,unsigned channels){return Image{w,h,channels==1?unsigned(D3DFMT_R32F):unsigned(D3DFMT_A32B32G32R32F),channels*4,std::vector<unsigned char>(size_t(w)*h*channels*4)};};
        auto depth=make(W,H,1),normal=make(W,H,4),water=make(W,H,4),nearMap=make(N,N,1),farMap=make(N,N,1),actor=make(N,N,1),fine=make(N,N,1),fineWorld=make(N,N,1);
        auto actorTiles=make(64,64,1),tiles=make(64,64,4),blocks=make(256,256,4);
        auto set=[](Image& im,unsigned x,unsigned y,float v,unsigned channel=0){std::memcpy(im.pixels.data()+(size_t(y)*im.width+x)*im.bytes+channel*4,&v,4);};
        auto get=[](const Image& im,unsigned x,unsigned y){float v;std::memcpy(&v,im.pixels.data()+(size_t(y)*im.width+x)*im.bytes,4);return v;};
        const auto staticZ=[&](float x,float y){
            x+=phase*.035f;
            if(shape==0||shape==8||shape==9)return 1.f;
            if(shape==1)return .54f;
            if(shape==2)return x<0?.57f:1.f;
            if(shape==3)return std::fmod(std::fabs(x+.3f),2.f)<.06f?.57f:1.f;
            if(shape==4)return x+y*.3f<0?.56f:1.f;
            if(shape==5)return .6f+.0001f*x;
            if(shape==6)return std::fabs(x)<.03f?.599f:1.f;
            return std::fmod(std::fabs(x+y*.3f),.7f)<.09f?.58f:1.f;
        };
        const auto actorZ=[&](float x,float y){
            if(shape!=4&&shape!=8)return 1.f;
            if(std::fabs(x)>8||std::fabs(y)>8)return 1.f;
            return std::fmod(std::fabs(x+.7f+phase*.1f),1.7f)<.11f?(shape==8&&phase==2?0.f:.59f):1.f;
        };
        for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){
            const float nx=float(x)/N*2-1,ny=1-float(y)/N*2;
            set(nearMap,x,y,staticZ(nx*48,ny*48));set(farMap,x,y,staticZ(nx*288,ny*288));
            const float a=actorZ(nx*12,ny*12),w=staticZ(nx*12,ny*12);set(actor,x,y,a);set(fineWorld,x,y,w);set(fine,x,y,a<w?-std::max(a,1e-30f):w);
        }
        for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){float lo=1,hi=0;unsigned counts=0,moments=0;for(unsigned dy=0;dy<4;++dy)for(unsigned dx=0;dx<4;++dx){const float z=std::fabs(get(fine,x*4+dx,y*4+dy));lo=std::min(lo,z);if(z<1){hi=std::max(hi,z);counts+=1u<<(3*dy);moments+=dx<<(3*dy);}}set(blocks,x,y,lo);set(blocks,x,y,hi,1);set(blocks,x,y,float(counts),2);set(blocks,x,y,float(moments),3);}
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
            float alo=1;for(unsigned dy=0;dy<16;++dy)for(unsigned dx=0;dx<16;++dx)alo=std::min(alo,get(actor,x*16+dx,y*16+dy));set(actorTiles,x,y,alo);
            float lo=1,hi=0;for(int dy=-4;dy<8;++dy)for(int dx=-4;dx<8;++dx){const unsigned tx=std::clamp(int(x*4)+dx,0,255),ty=std::clamp(int(y*4)+dy,0,255);lo=std::min(lo,get(blocks,tx,ty));float z,count;std::memcpy(&z,blocks.pixels.data()+(size_t(ty)*256+tx)*16+4,4);std::memcpy(&count,blocks.pixels.data()+(size_t(ty)*256+tx)*16+8,4);hi=std::max(hi,count==2340?z:1.f);}set(tiles,x,y,lo);set(tiles,x,y,hi,1);
        }
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            set(depth,x,y,shape==9?1.f:(1000.f-.2f*1000.f/20.f)/(1000.f-.2f));
            set(normal,x,y,0);set(normal,x,y,0,1);set(normal,x,y,1,2);set(normal,x,y,1,3);set(water,x,y,5.f);
        }
        const std::pair<unsigned,Image*> inputs[]={{1,&depth},{2,&nearMap},{3,&farMap},{11,&water},{14,&normal}};
        std::vector<IDirect3DTexture9*> textures;for(auto input:inputs){auto* t=texture(d,*input.second);textures.push_back(t);ok(d->SetTexture(input.first,t));}
        auto* actorTexture=texture(d,actor);auto* worldTexture=texture(d,fineWorld);
        Target fineTarget(d,1024,1),blockTarget(d,256,4),tileTarget(d,64,4),actorMid(d,256,1),actorTileTarget(d,64,1);
        float c[96][4]={};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.2f;c[0][3]=1000;c[1][0]=c[1][1]=c[1][2]=1;
        c[2][0]=1;c[2][1]=48;c[2][2]=288;c[2][3]=1.f/N;
        for(unsigned k=0;k<4;++k)c[3+k][k]=1;
        c[7][0]=c[8][1]=1.f/48;c[9][2]=1.f/200;c[10][2]=.5f;c[10][3]=1;
        c[11][0]=c[12][1]=1.f/288;c[13][2]=1.f/200;c[14][2]=.5f;c[14][3]=1;
        c[16][2]=c[74][2]=1;c[24][0]=.00025f;c[33][0]=float(W);c[33][1]=float(H);c[33][2]=float(W);c[33][3]=float(H);c[35][0]=1.f/200;
        c[68][0]=c[69][1]=1.f/12;c[70][2]=1.f/200;c[71][2]=.5f;c[71][3]=1;
        c[72][0]=1;c[72][1]=1.f/N;c[72][2]=12;c[72][3]=shape==8?.1f:4.25f*96.f/1024;
        ok(d->SetPixelShaderConstantF(0,c[0],96));
        const auto prepare=[&](bool common,bool unite=true){
            // Unbind all possible preceding targets before assigning a new RT.
            for(unsigned s:{10u,12u,13u})ok(d->SetTexture(s,nullptr));
            if(common){
                if(unite){ok(d->SetTexture(2,worldTexture));ok(d->SetTexture(12,actorTexture));pass(d,fineTarget,ps[3]);}
                ok(d->SetRenderTarget(0,blockTarget.surface));ok(d->SetTexture(12,fineTarget.texture));pass(d,blockTarget,ps[4]);
                ok(d->SetRenderTarget(0,tileTarget.surface));ok(d->SetTexture(13,blockTarget.texture));pass(d,tileTarget,ps[5]);
            }else{
                ok(d->SetTexture(12,actorTexture));ok(d->SetPixelShaderConstantF(72,c[72],1));pass(d,actorMid,ps[6]);
                const float info[]={1,1.f/256,12,0};ok(d->SetPixelShaderConstantF(72,info,1));
                ok(d->SetRenderTarget(0,actorTileTarget.surface));ok(d->SetTexture(12,actorMid.texture));pass(d,actorTileTarget,ps[6]);
            }
            ok(d->SetTexture(2,textures[1]));ok(d->SetPixelShaderConstantF(72,c[72],1));
        };
        ok(d->BeginScene());prepare(false);prepare(true);ok(d->EndScene());wait(event);
        fineTarget.check(d,fine);blockTarget.check(d,blocks);tileTarget.check(d,tiles);actorTileTarget.check(d,actorTiles);
        // An independent double-precision CPU oracle integrates every fine
        // texel, including clamped border samples. Neither shader is its oracle.
        auto queries=make(8,8,4);std::vector<double> oracle(64),oracleLow(64),oracleHigh(64);
        const float gx=phase==0?0.f:phase==1?.02f:-.017f,gy=phase==0?0.f:phase==1?-.013f:.019f;
        for(unsigned j=0;j<64;++j){
            const float px=j<8?float(j)-2.3f:j<16?1025.7f-float(j-8):480.13f+float((j*137)%71);
            const float py=j<16?507.31f:float((j*113)%1030)-3.17f;
            const float reach=j%4==0?1.1f:j%4==1?4.25f:j%4==2?8.5f:17.f,receiver=.58f+(j%3)*.01f;
            set(queries,j%8,j/8,px);set(queries,j%8,j/8,py,1);set(queries,j%8,j/8,reach,2);set(queries,j%8,j/8,receiver,3);
            double sum=0,weight=0,low=0,high=0;
            for(int y=int(std::floor(py-reach))+1;y<=int(std::floor(py+reach));++y)
            for(int x=int(std::floor(px-reach))+1;x<=int(std::floor(px+reach));++x){
                const double w=std::max(0.,double(reach)-std::fabs(x-double(px)))*std::max(0.,double(reach)-std::fabs(y-double(py)));
                const double expected=double(receiver)+double(gx)*(x-double(px))/1024+double(gy)*(y-double(py))/1024;
                const double depth=std::fabs(get(fine,unsigned(std::clamp(x,0,1023)),unsigned(std::clamp(y,0,1023))));
                sum+=(expected<=depth?w:0);weight+=w;
                // A float comparison exactly on the receiver plane is not
                // uniquely determined by a double oracle. Bound ONLY those
                // taps within the accumulated FP32 coordinate/dot rounding.
                const double roundoff=8*std::numeric_limits<float>::epsilon()*(1+std::fabs(gx)+std::fabs(gy));
                low+=(expected+roundoff<=depth?w:0);high+=(expected-roundoff<=depth?w:0);
            }
            oracle[j]=sum/weight;oracleLow[j]=low/weight;oracleHigh[j]=high/weight;
        }
        auto* queryTexture=texture(d,queries);Target kernel(d,8,1);double kernelWorst=0,intervalWidth=0;
        unsigned ambiguous=0;for(unsigned j=0;j<64;++j){intervalWidth=std::max(intervalWidth,oracleHigh[j]-oracleLow[j]);ambiguous+=oracleHigh[j]!=oracleLow[j];}
        for(unsigned mode=7;mode<9;++mode){
            const float gradient[]={gx,gy,0,0};ok(d->SetPixelShaderConstantF(81,gradient,1));
            ok(d->SetTexture(0,queryTexture));ok(d->SetTexture(12,fineTarget.texture));ok(d->SetTexture(13,blockTarget.texture));
            ok(d->BeginScene());pass(d,kernel,ps[mode]);ok(d->EndScene());wait(event);
            IDirect3DSurface9* readback=nullptr;ok(d->CreateOffscreenPlainSurface(8,8,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
            ok(d->GetRenderTargetData(kernel.surface,readback));D3DLOCKED_RECT data{};ok(readback->LockRect(&data,nullptr,D3DLOCK_READONLY));
            for(unsigned j=0;j<64;++j){float value;std::memcpy(&value,static_cast<char*>(data.pBits)+(j/8)*data.Pitch+(j%8)*4,4);
                assert(std::isfinite(value)&&value>=-2e-5f&&value<=1.00002f);
                const double error=std::max({0.,oracleLow[j]-value,value-oracleHigh[j]});kernelWorst=std::max(kernelWorst,error);
                if(error>2e-5){std::fprintf(stderr,"KERNEL_FAIL shape=%u phase=%u mode=%u query=%u gpu=%.9f cpu=%.9f error=%.9f\n",shape,phase,mode,j,value,oracle[j],error);std::abort();}}
            ok(readback->UnlockRect());readback->Release();
        }
        ok(d->SetTexture(0,nullptr));queryTexture->Release();
        std::printf("KERNEL shape=%u phase=%u comparisons=128 maxIntervalError=%.9f ambiguousQueries=%u maxIntervalWidth=%.9f\n",shape,phase,kernelWorst,ambiguous,intervalWidth);
        IDirect3DTexture9* result=nullptr;IDirect3DSurface9 *surface=nullptr,*staging=nullptr;
        ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&result,nullptr));ok(result->GetSurfaceLevel(0,&surface));ok(d->SetRenderTarget(0,surface));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
        const auto bind=[&](unsigned mode){ok(d->SetRenderTarget(0,surface));ok(d->SetViewport(&vp));ok(d->SetTexture(12,mode?fineTarget.texture:actorTexture));ok(d->SetTexture(10,mode?tileTarget.texture:actorTileTarget.texture));ok(d->SetTexture(13,mode?blockTarget.texture:nullptr));ok(d->SetPixelShader(ps[mode]));};
        const auto draw=[&](){ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));};
        std::vector<uint16_t> pixels[3];
        for(unsigned mode=0;mode<3;++mode){ok(d->BeginScene());bind(mode);draw();ok(d->EndScene());wait(event);ok(d->GetRenderTargetData(surface,staging));D3DLOCKED_RECT locked{};ok(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY));pixels[mode].resize(size_t(W)*H*4);
            for(unsigned y=0;y<H;++y)std::memcpy(pixels[mode].data()+size_t(y)*W*4,static_cast<char*>(locked.pBits)+size_t(y)*locked.Pitch,W*8);ok(staging->UnlockRect());}
        double maxError=0,mean=0,oldDifference=0;unsigned changed=0;
        for(size_t i=0;i<pixels[0].size();i+=4){const float a=half(pixels[1][i]),b=half(pixels[2][i]),old=half(pixels[0][i]);assert(std::isfinite(a)&&std::isfinite(b)&&std::isfinite(old));const double e=std::fabs(a-b);changed+=a!=b;maxError=std::max(maxError,e);mean+=e;oldDifference+=std::fabs(old-b);}
        mean/=double(W)*H;oldDifference/=double(W)*H;worst=std::max(worst,maxError);
        std::printf("IMAGE shape=%u phase=%u changed=%u maxError=%.9f meanError=%.9f baselineDifference=%.9f\n",shape,phase,changed,maxError,mean,oldDifference);std::fflush(stdout);
        // Summing exact block moments changes floating-point accumulation order.
        // Bound the final FP16 difference to two steps, and its average separately.
        assert(maxError<=1./1024&&mean<.00002);if(shape==0||shape==1||shape==8||shape==9)assert(oldDifference<.00002);++cases;
        if(benchmark){NorthlightGpuFrameTimer timer(d);constexpr unsigned iterations=256;
            for(unsigned stage=0;stage<3;++stage)for(unsigned block=0;block<16;++block){const unsigned mode=block%4==1||block%4==2?2:0;wait(event);ok(d->BeginScene());bind(mode);const bool timed=timer.begin(block);const auto start=std::chrono::steady_clock::now();
                for(unsigned i=0;i<iterations;++i){if(stage){prepare(mode==2,stage==1);bind(mode);}draw();}
                const double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/iterations;timer.end();ok(d->EndScene());wait(event);double gpu=-1;if(timed)timer.poll(gpu);
                std::printf("TIMING shape=%u stage=%u block=%u warm=%u common=%u iterations=%u cpuMs=%.6f gpuMs=%.6f\n",shape,stage,block,unsigned(block<4),unsigned(mode==2),iterations,cpu,gpu<0?gpu:gpu/iterations);std::fflush(stdout);}
        }
        staging->Release();surface->Release();result->Release();for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));for(auto* t:textures)t->Release();actorTexture->Release();worldTexture->Release();
    }
    std::printf("COMMON_CASCADE_PASS cases=%u worst=%.9f\n",cases,worst);for(auto* shader:ps)shader->Release();event->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
