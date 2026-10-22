// Compare installed and candidate shader bytecode on a hidden D3D9 device.
#define main unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
#include <functional>

static Image rgba(unsigned w,unsigned h,const std::function<std::array<float,4>(unsigned,unsigned)>& pixel){
    Image im{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(w*h*16)};
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto p=pixel(x,y);std::memcpy(im.pixels.data()+(y*w+x)*16,p.data(),16);}return im;
}
int main(int argc,char** argv){
    assert(argc==5);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightPerfAB";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[3]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*freq=nullptr,*disjoint=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    FILE* report=std::fopen(argv[4],"w");assert(report);std::fprintf(report,"[\n");unsigned cases=0;
    const char* names[]={"GTAOContactBloom","WorldFog","FogClouds","WorldComposite"};
    for(unsigned pass=1;pass<4;++pass)for(unsigned scenario=0;scenario<(pass==0?5u:4u);++scenario){
        const unsigned FW=3840,FH=2160,W=pass<3?FW/2:FW,H=pass<3?FH/2:FH;
        float c[80][4]{};c[0][0]=1.f/FW;c[0][1]=1.f/FH;c[0][2]=.2f;c[0][3]=1000;
        c[1][0]=1.3f;c[1][1]=1.9f;c[1][2]=1;c[2][0]=1;c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;
        c[3][0]=c[4][2]=c[5][1]=c[6][3]=1;c[6][2]=2;
        c[11][0]=c[12][1]=c[13][2]=1.f/384;c[14][2]=.5f;c[14][3]=1;
        c[15][0]=scenario&1?-14.3f:13.7f;c[15][1]=scenario&1?-7.2f:6.4f;c[15][2]=2;
        c[16][2]=1;c[17][0]=.8f;c[17][1]=.7f;c[17][2]=.6f;
        c[18][0]=.2f;c[18][1]=.23f;c[18][2]=.3f;c[18][3]=1;
        c[21][0]=1;c[21][1]=1;c[21][2]=.1f;c[21][3]=128;
        c[22][0]=c[22][1]=c[22][2]=1;c[22][3]=.002f;
        c[25][1]=c[25][2]=1;c[26][0]=.3f;c[26][1]=.4f;c[26][2]=.5f;c[26][3]=1;
        c[27][0]=1;c[31][0]=c[31][1]=-256;c[31][2]=1.f/512;c[31][3]=.7f;
        c[32][0]=.01f;c[32][1]=.004f;c[32][2]=30;c[32][3]=18;
        c[33][0]=FW;c[33][1]=FH;c[33][2]=FW/2;c[33][3]=FH/2;
        c[58][0]=scenario==0?0:scenario==1?3.5f:scenario==2?8:200;c[58][1]=1.f/12;
        c[59][1]=-10;c[59][3]=.0017f;c[60][1]=.37f;c[60][2]=.23f;c[60][3]=.13f;
        c[61][0]=.3125f;c[61][1]=.52f;c[61][2]=.27f;c[61][3]=.64f;
        c[62][0]=22;c[62][1]=-1.25f;c[62][2]=32;c[62][3]=.06f;
        c[63][0]=.875f;c[63][1]=1.f/64;c[63][2]=1.f/16;c[63][3]=1.625f;
        if(pass==0){std::memset(c[1],0,6*16);c[1][0]=1.3f;c[1][1]=1.9f;c[1][2]=.6f;c[2][1]=scenario==0?.1f:scenario==1?2:10;c[3][1]=c[3][2]=1;c[5][1]=c[5][2]=1;}
        if(pass==1||pass==2){c[31][0]=scenario==1?-8.f:scenario==2?1024.f:-256.f;c[31][1]=scenario==1?-8.f:scenario==2?1024.f:-256.f;}
        if(pass==3){c[30][0]=scenario==3;}

        auto solid=[&](float r,float g,float b,float a){return texture(d,rgba(1,1,[&](unsigned,unsigned){return std::array<float,4>{r,g,b,a};}));};
        IDirect3DTexture9* inputs[16]{};for(auto& t:inputs)t=solid(0,0,0,0);
        auto bind=[&](unsigned slot,Image im){inputs[slot]->Release();inputs[slot]=texture(d,im);};
        auto distanceAt=[&](unsigned x,unsigned y){return scenario==4?80.f:scenario==3?1000.f:20.f+170.f*float(x)/FW+8.f*std::sin(float(y)*.04f);};
        bind(0,rgba(FW,FH,[&](unsigned x,unsigned y){return std::array<float,4>{.2f+.2f*float(x)/FW,.3f+.1f*float(y)/FH,.4f,.7f};}));
        bind(1,rgba(FW,FH,[&](unsigned x,unsigned y){float z=distanceAt(x,y);float depth=1000.f/999.8f*(1-.2f/z);return std::array<float,4>{depth,depth,depth,depth};}));
        if(pass==0){bind(3,rgba(1,1,[](unsigned,unsigned){return std::array<float,4>{0,0,0,0};}));}
        if(pass==1||pass==2){
            bind(3,rgba(64,64,[](unsigned x,unsigned y){return std::array<float,4>{(x/5+y/7)%2?.3f:.7f,0,0,1};}));
            bind(13,rgba(64,64,[](unsigned x,unsigned y){return std::array<float,4>{-.2f,.008f,.004f,(x/5+y/7)%5?5.f:0.f};}));
        }
        if(pass==3){
            bind(8,rgba(FW/2,FH/2,[](unsigned x,unsigned y){return std::array<float,4>{-.1f+.05f*std::sin(x*.03f),.05f,.03f,(y/5)%2?.3f:1.f};}));
            bind(9,rgba(FW/2,FH/2,[](unsigned,unsigned){return std::array<float,4>{.05f,.04f,.03f,.8f};}));
            bind(10,rgba(FW/2,FH/2,[](unsigned,unsigned){return std::array<float,4>{.01f,.01f,.01f,.8f};}));
            bind(12,rgba(FW/2,FH/2,[](unsigned,unsigned){return std::array<float,4>{.6f,.6f,.6f,1};}));
            bind(15,rgba(FW/2,FH/2,[&](unsigned x,unsigned y){return std::array<float,4>{distanceAt(2*x,2*y),0,0,1};}));
        }

        for(unsigned s=0;s<16;++s){ok(d->SetTexture(s,inputs[s]));for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,s==0||s==4||s==10?D3DTEXF_LINEAR:D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
        IDirect3DVolumeTexture9* noise=nullptr;
        if(pass==2){
            ok(d->CreateVolumeTexture(32,32,32,1,0,D3DFMT_L8,D3DPOOL_MANAGED,&noise,nullptr));D3DLOCKED_BOX box{};ok(noise->LockBox(0,&box,nullptr,0));
            for(unsigned z=0;z<32;++z)for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)*((unsigned char*)box.pBits+z*box.SlicePitch+y*box.RowPitch+x)=static_cast<unsigned char>((x*37+y*17+z*53)%256);
            ok(noise->UnlockBox(0));ok(d->SetTexture(14,noise));for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(14,state,D3DTEXF_LINEAR));for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(d->SetSamplerState(14,state,D3DTADDRESS_WRAP));
        }
        IDirect3DPixelShader9* shaders[2]{};for(unsigned i=0;i<2;++i){auto b=read(std::string(argv[i+1])+"/"+names[pass]+".bin");ok(d->CreatePixelShader((const DWORD*)b.data(),&shaders[i]));}
        IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetPixelShaderConstantF(0,c[0],80));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
        const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(W)-.5f,-.5f,0,1,1,0},{-.5f,float(H)-.5f,0,1,0,1},{float(W)-.5f,float(H)-.5f,0,1,1,1}};
        auto draw=[&](unsigned n){ok(d->BeginScene());for(unsigned j=0;j<n;++j)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());};
        std::vector<float> pixels[2];std::vector<double> timings[2];
        for(unsigned block=0;block<18;++block){unsigned mode=(block/2)%2?1-block%2:block%2;ok(d->SetPixelShader(shaders[mode]));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x00112233,1,0));draw(5);wait(event);
            ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(32);ok(end->Issue(D3DISSUE_END));ok(freq->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
            UINT64 a=0,b=0,f=0;BOOL bad=FALSE;ok(start->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(freq->GetData(&f,sizeof f,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&b>=a&&f);if(block>=4)timings[mode].push_back(double(b-a)/f*1000/32);
            if(pixels[mode].empty()){ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));pixels[mode].resize(W*H*4);for(unsigned y=0;y<H;++y)std::memcpy(pixels[mode].data()+y*W*4,(char*)lock.pBits+y*lock.Pitch,W*16);ok(staging->UnlockRect());}
        }
        double maximum=0;unsigned failures=0;for(unsigned i=0;i<W*H*4;++i){double e=std::fabs(double(pixels[0][i])-pixels[1][i]);maximum=std::max(maximum,e);failures+=!std::isfinite(e)||e>1e-5;}
        double ms[2];for(unsigned i=0;i<2;++i){std::sort(timings[i].begin(),timings[i].end());ms[i]=timings[i][timings[i].size()/2];}
        std::fprintf(report,"%s{\"shader\":\"%s\",\"scenario\":%u,\"width\":%u,\"height\":%u,\"old_ms\":%.6f,\"new_ms\":%.6f,\"max_error\":%.9g,\"failures\":%u}",cases++?",\n":"",names[pass],scenario,W,H,ms[0],ms[1],maximum,failures);std::fflush(report);
        std::printf("%s case=%u old=%.4f new=%.4f maxError=%.9g failures=%u\n",names[pass],scenario,ms[0],ms[1],maximum,failures);std::fflush(stdout);assert(!failures);
        d->SetPixelShader(nullptr);for(auto s:shaders)s->Release();for(unsigned s=0;s<16;++s){d->SetTexture(s,nullptr);inputs[s]->Release();}if(noise)noise->Release();staging->Release();target->Release();
    }
    std::fprintf(report,"\n]\n");std::fclose(report);for(auto q:{event,start,end,freq,disjoint})q->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return 0;
}
