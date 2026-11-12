// Production fog/depth shaders and fullscreen coverage against independent references.
#define main unused_water_endpoint_main
#include "water_fog_endpoint_gpu.cpp"
#undef main
#include "screen_quad.h"
#include "forever_atmosphere.h"

static Image solid(float r,float g=0,float b=0,float a=1){
    Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
    const float v[]={r,g,b,a};std::memcpy(im.pixels.data(),v,16);return im;
}
int main(int argc,char** argv){
    assert(argc==4);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightFogWorkOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[3]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};
    pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    IDirect3DTexture9* textures[16]{};
    auto bind=[&](unsigned slot,const Image& im){ok(d->SetTexture(slot,nullptr));if(textures[slot])textures[slot]->Release();textures[slot]=texture(d,im);ok(d->SetTexture(slot,textures[slot]));};
    for(unsigned s=0;s<16;++s){bind(s,solid(0));for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    auto shader=[&](const char* folder,const std::string& name){auto code=read(std::string(folder)+"/"+name+".bin");IDirect3DPixelShader9* ps=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&ps));return ps;};
    auto quad=[&](unsigned w,unsigned h){const Vertex v[]={{-.5f,-.5f,0,1,0,0},{w-.5f,-.5f,0,1,1,0},{-.5f,h-.5f,0,1,0,1},{w-.5f,h-.5f,0,1,1,1}};ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Vertex)));};
    unsigned cases=0;float maximum=0;
    auto readback=[&](IDirect3DSurface9* target,unsigned w,unsigned h,D3DFORMAT format,unsigned bytes){
        IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(w,h,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<unsigned char> data(size_t(w)*h*bytes);for(unsigned y=0;y<h;++y)std::memcpy(data.data()+size_t(y)*w*bytes,static_cast<char*>(lock.pBits)+y*lock.Pitch,w*bytes);ok(staging->UnlockRect());staging->Release();return data;
    };
    auto compare=[&](const std::vector<unsigned char>& a,const std::vector<unsigned char>& b){assert(a.size()==b.size());for(size_t i=0;i<a.size();i+=4){float x,y;std::memcpy(&x,a.data()+i,4);std::memcpy(&y,b.data()+i,4);assert(std::isfinite(x)&&std::isfinite(y));maximum=std::max(maximum,std::abs(x-y));assert(std::abs(x-y)<=2e-5f*std::max(1.f,std::abs(x)));}++cases;};
    auto* distance=shader(argv[1],"WorldDistance");auto* temporal=shader(argv[1],"TemporalLight");auto* coverage=shader(argv[2],"Coverage");
    for(auto size:{std::pair<unsigned,unsigned>{137,79},{256,160},{1,1}}){
        const unsigned fw=size.first,fh=size.second,w=std::max(1u,fw/2),h=std::max(1u,fh/2);
        Image depth{fw,fh,D3DFMT_R32F,4,std::vector<unsigned char>(size_t(fw)*fh*4)};
        for(float partition:{1.f,.94f})for(float minZ:{0.f,.05f}){
            for(unsigned y=0;y<fh;++y)for(unsigned x=0;x<fw;++x){float z=minZ+(partition-minZ)*float((x*17+y*13)%101)/100;std::memcpy(depth.pixels.data()+(size_t(y)*fw+x)*4,&z,4);}bind(1,depth);
            float c[68][4]{};c[0][0]=1.f/fw;c[0][1]=1.f/fh;c[0][2]=.5f;c[0][3]=1000;c[1][0]=c[1][1]=c[1][2]=1;c[1][3]=minZ;c[2][0]=1/(partition-minZ);c[33][0]=float(fw);c[33][1]=float(fh);c[33][2]=float(w);c[33][3]=float(h);ok(d->SetPixelShaderConstantF(0,c[0],68));
            IDirect3DSurface9 *dry=nullptr,*light=nullptr,*standalone=nullptr;
            ok(d->CreateRenderTarget(w,h,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&dry,nullptr));ok(d->CreateRenderTarget(w,h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&light,nullptr));ok(d->CreateRenderTarget(w,h,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&standalone,nullptr));
            ok(d->SetRenderTarget(0,light));ok(d->SetRenderTarget(1,dry));ok(d->SetPixelShader(temporal));ok(d->BeginScene());quad(w,h);ok(d->EndScene());ok(d->SetRenderTarget(1,nullptr));
            ok(d->SetRenderTarget(0,standalone));ok(d->SetPixelShader(distance));ok(d->BeginScene());ok(NorthlightScreen::drawQuad(d,w,h));ok(d->EndScene());
            assert(readback(dry,w,h,D3DFMT_R32F,4)==readback(standalone,w,h,D3DFMT_R32F,4));++cases;dry->Release();light->Release();standalone->Release();
        }
        for(unsigned samples:{0u,2u,4u,8u}){
            DWORD levels=0;assert(SUCCEEDED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_TYPE(samples),&levels)));
            IDirect3DSurface9 *target=nullptr,*resolved=nullptr;ok(d->CreateRenderTarget(fw,fh,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));ok(d->CreateRenderTarget(fw,fh,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
            std::vector<unsigned char> images[2];ok(d->SetPixelShader(coverage));
            for(unsigned mode=0;mode<2;++mode){ok(d->SetRenderTarget(0,target));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());if(mode)ok(NorthlightScreen::drawQuad(d,fw,fh));else quad(fw,fh);ok(d->EndScene());if(samples)ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));images[mode]=readback(samples?resolved:target,fw,fh,D3DFMT_A8R8G8B8,4);}
            for(size_t i=0;i<images[0].size();++i){assert(std::abs(int(images[0][i])-int(images[1][i]))<=1);if(i%4==3)assert(images[1][i]==255);}++cases;target->Release();resolved->Release();
        }
    }
    constexpr unsigned w=16,h=12;IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(w,h,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->SetRenderTarget(0,target));
    IDirect3DVolumeTexture9* volume=nullptr;ok(d->CreateVolumeTexture(4,4,4,1,0,D3DFMT_L8,D3DPOOL_MANAGED,&volume,nullptr));D3DLOCKED_BOX box{};ok(volume->LockBox(0,&box,nullptr,0));for(unsigned z=0;z<4;++z)for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)*(static_cast<unsigned char*>(box.pBits)+z*box.SlicePitch+y*box.RowPitch+x)=static_cast<unsigned char>(31+(x*57+y*19+z*33)%224);ok(volume->UnlockBox(0));ok(d->SetTexture(14,volume));
    for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(d->SetSamplerState(14,state,D3DTADDRESS_WRAP));
    for(bool transition:{false,true})for(bool actor:{false,true}){
        const std::string suffix=transition?"Transition":"",name=actor?"ForeverFogActor":"ForeverFog";
        auto* reference=shader(argv[2],name+"Reference"+suffix);auto* general=shader(argv[1],name+suffix);auto* quiet=shader(argv[1],name+"NoNoise"+suffix);
        for(unsigned noisy=0;noisy<2;++noisy)for(unsigned shape=0;shape<9;++shape){
            float c[116][4]{};c[0][0]=1.f/w;c[0][1]=1.f/h;c[0][2]=.5f;c[0][3]=1000;c[1][0]=c[1][1]=2;c[1][2]=c[2][0]=1;c[2][2]=192;c[2][3]=1.f/1024;
            c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[15][2]=20;c[14][2]=.5f;c[14][3]=1;c[16][2]=1;c[21][0]=1;c[27][0]=1;c[24][3]=31;
            c[31][0]=c[31][1]=-256;c[31][2]=1.f/512;c[33][0]=c[33][2]=float(w);c[33][1]=c[33][3]=float(h);
            c[71][2]=.5f;c[71][3]=1;c[72][0]=actor?1.f:0.f;c[89][0]=c[89][1]=.4f;
            bind(1,solid(1000.f/999.5f*(1-.5f/100)));bind(3,solid(.7f));bind(4,solid(.4f));bind(13,solid(0,0,0,1.25f));ok(d->SetPixelShaderConstantF(0,c[0],116));
            NorthlightForever::Frame f;f.valid=true;
            for(unsigned layer=0;layer<3;++layer){auto& l=f.layer[layer];l.flag[3]=shape==8||((shape==6)&&layer==0)?0.f:1.f;l.flag[0]=1;l.flag[2]=noisy?1.f:0.f;l.p[0]=shape==0?0.f:shape==7?150.f:float(shape*12+layer*4);l.p[1]=1+float(layer)*.25f;l.p[9]=1;l.p[10]=.3f;l.p[20]=l.p[21]=.1f;l.p[22]=.6f;l.p[23]=.4f;
                for(unsigned k=0;k<3;++k){l.color[0][k]=.6f;l.color[1][k]=.25f;l.color[2][k]=.1f;l.color[3][k]=.2f;l.p[11+k]=.01f*float(k+1);}}
            float bank[38][4];NorthlightForever::constants(f,bank,128,1);ok(d->SetPixelShaderConstantF(116,bank[0],38));
            std::vector<unsigned char> images[2];for(unsigned mode=0;mode<2;++mode){ok(d->SetPixelShader(mode?(noisy?general:quiet):reference));ok(d->BeginScene());ok(NorthlightScreen::drawQuad(d,w,h));ok(d->EndScene());images[mode]=readback(target,w,h,D3DFMT_A32B32G32R32F,16);}compare(images[0],images[1]);
        }
        reference->Release();general->Release();quiet->Release();
    }
    std::printf("LUXVELUM_GPU_PASS cases=%u distanceBitExact=1 coverageMSAA=0,2,4,8 fogMaxError=%g actor=1 transitions=1\n",cases,maximum);
    if(std::getenv("NORTHLIGHT_OPTIMIZATION_BENCHMARK")){
        constexpr unsigned bw=960,bh=540,draws=32;
        IDirect3DSurface9* measured=nullptr;ok(d->CreateRenderTarget(bw,bh,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&measured,nullptr));ok(d->SetRenderTarget(0,measured));
        IDirect3DQuery9 *begin=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
        ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
        auto collect=[](IDirect3DQuery9* query,void* value,unsigned bytes){HRESULT hr;const DWORD started=GetTickCount();while((hr=query->GetData(value,bytes,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-started<10000);SwitchToThread();}ok(hr);};
        auto measure=[&](IDirect3DPixelShader9* ps){
            ok(d->SetPixelShader(ps));ok(d->BeginScene());for(unsigned i=0;i<4;++i)ok(NorthlightScreen::drawQuad(d,bw,bh));
            ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));ok(begin->Issue(D3DISSUE_END));for(unsigned i=0;i<draws;++i)ok(NorthlightScreen::drawQuad(d,bw,bh));ok(end->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));ok(d->EndScene());
            UINT64 a=0,b=0,hz=0;BOOL invalid=TRUE;collect(end,&b,sizeof b);collect(begin,&a,sizeof a);collect(frequency,&hz,sizeof hz);collect(disjoint,&invalid,sizeof invalid);assert(!invalid&&hz&&b>=a);return 1000.*double(b-a)/double(hz)/draws;
        };
        auto* old=shader(argv[2],"ForeverFogReference");auto* noisyShader=shader(argv[1],"ForeverFog");auto* quiet=shader(argv[1],"ForeverFogNoNoise");
        for(unsigned noisy=0;noisy<2;++noisy)for(float start:{0.f,64.f}){
            float c[116][4]{};c[0][0]=1.f/bw;c[0][1]=1.f/bh;c[0][2]=.5f;c[0][3]=1000;c[1][0]=c[1][1]=2;c[1][2]=c[2][0]=1;c[2][2]=192;c[2][3]=1.f/1024;c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[15][2]=20;c[14][2]=.5f;c[14][3]=1;c[16][2]=1;c[21][0]=c[27][0]=1;c[31][0]=c[31][1]=-256;c[31][2]=1.f/512;c[33][0]=c[33][2]=bw;c[33][1]=c[33][3]=bh;ok(d->SetPixelShaderConstantF(0,c[0],116));
            NorthlightForever::Frame f;for(auto& l:f.layer){l.flag[3]=1;l.flag[2]=float(noisy);l.p[0]=start;l.p[1]=1;l.p[9]=1;l.p[20]=l.p[21]=.1f;l.p[22]=.6f;l.p[23]=.4f;for(unsigned k=0;k<3;++k){l.color[0][k]=.6f;l.color[1][k]=.25f;l.color[3][k]=.2f;}}
            float bank[38][4];NorthlightForever::constants(f,bank,128,1);ok(d->SetPixelShaderConstantF(116,bank[0],38));std::vector<double> times[2];
            for(unsigned block=0;block<8;++block)for(unsigned mode:{0u,1u,1u,0u})times[mode].push_back(measure(mode?(noisy?noisyShader:quiet):old));
            for(auto& values:times)std::sort(values.begin(),values.end());
            std::printf("LUXVELUM_FOG_GPU_MS noise=%u start=%.0f width=%u height=%u before=%.6f after=%.6f\n",noisy,start,bw,bh,(times[0][7]+times[0][8])*.5,(times[1][7]+times[1][8])*.5);
        }
        old->Release();noisyShader->Release();quiet->Release();begin->Release();end->Release();frequency->Release();disjoint->Release();measured->Release();
    }
    d->SetPixelShader(nullptr);for(unsigned s=0;s<16;++s){d->SetTexture(s,nullptr);textures[s]->Release();}distance->Release();temporal->Release();coverage->Release();volume->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
