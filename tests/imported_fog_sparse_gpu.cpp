// Complete imported fog, including the unchanged 48 intervals and animated noise.
#define main unused_water_endpoint_main
#include "water_fog_endpoint_gpu.cpp"
#undef main
#include "fog_volume.h"
#include "forever_atmosphere.h"
static void wait(IDirect3DQuery9* event){ok(event->Issue(D3DISSUE_END));while(event->GetData(nullptr,0,D3DGETDATA_FLUSH)==S_FALSE)SwitchToThread();}
#include "fog_actor_tiles_fixture.h"

int main(int argc,char** argv){
    assert(argc==2||argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightSparseFog";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    for(unsigned s=0;s<16;++s){NorthlightFogVolume::sampler(d,s);ok(d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT));}
    const bool timing=std::getenv("NORTHLIGHT_SPARSE_FOG_BENCHMARK");const unsigned W=timing?1920:128,H=timing?1080:72;
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->SetRenderTarget(0,target));
    auto scalar=[&](float v){Image im{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(im.pixels.data(),&v,4);return texture(d,im);};
    auto* depth=scalar(1000.f/999.5f*(1-.5f/200));auto* white=scalar(1);
    IDirect3DVolumeTexture9* noise=nullptr;ok(d->CreateVolumeTexture(16,16,16,1,0,D3DFMT_L8,D3DPOOL_MANAGED,&noise,nullptr));D3DLOCKED_BOX box{};ok(noise->LockBox(0,&box,nullptr,0));for(unsigned z=0;z<16;++z)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)*(static_cast<unsigned char*>(box.pBits)+z*box.SlicePitch+y*box.RowPitch+x)=static_cast<unsigned char>(128+60*std::sin(x*.392699f)*std::cos(y*.392699f)*std::sin(z*.392699f));ok(noise->UnlockBox(0));
    IDirect3DPixelShader9* ps[2][2]{};const DWORD* code[2][2]={{kForeverFogActorNoNoiseShader,kForeverFogActorShader},{kForeverFogActorTiledNoNoiseShader,kForeverFogActorTiledShader}};
    for(unsigned v=0;v<2;++v)for(unsigned n=0;n<2;++n){std::vector<unsigned char> bytes;if(!v&&argc==3)bytes=read(std::string(argv[2])+(n?"/ForeverFogActor.bin":"/ForeverFogActorNoNoise.bin"));ok(d->CreatePixelShader(bytes.empty()?code[v][n]:reinterpret_cast<const DWORD*>(bytes.data()),&ps[v][n]));}
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*freq=nullptr,*disjoint=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    auto wait=[&](){ok(event->Issue(D3DISSUE_END));while(event->GetData(nullptr,0,D3DGETDATA_FLUSH)==S_FALSE)SwitchToThread();};
    unsigned cases=0;double openEnergy=0;
    for(unsigned pattern=0;pattern<4;++pattern){
        Image actor{1024,1024,D3DFMT_R32F,4,std::vector<unsigned char>(1024*1024*4)};
        for(unsigned y=0;y<1024;++y)for(unsigned x=0;x<1024;++x){const bool caster=pattern==1?((x>470&&x<490&&y>460&&y<540)||(x==519&&y>400&&y<600)):pattern==2?((x/47+y/53)%7==0):pattern==3;const float z=caster?.35f:1.f;std::memcpy(actor.pixels.data()+4*(y*1024+x),&z,4);}
        auto* actorMap=texture(d,actor);auto* tiles=fogActorTilesFixture(d,actor,true);
        Image field{64,64,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(64*64*16)};for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){const float v[]={float((x/8)%2)*8,0,0,(x/13+y/11)%4?1.25f:0.f};std::memcpy(field.pixels.data()+16*(y*64+x),v,16);}auto* regional=texture(d,field);
        for(unsigned noisy=0;noisy<2;++noisy)for(float range:{128.f,1000.f,4000.f}){
            float c[154][4]{};c[0][0]=.5f/W;c[0][1]=.5f/H;c[0][2]=.5f;c[0][3]=5000;c[1][0]=c[1][1]=2;c[1][2]=c[2][0]=1;c[2][2]=192;c[2][3]=1.f/1024;
            c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[6][2]=c[15][2]=20;c[16][2]=1;c[21][0]=c[27][0]=1;c[24][3]=31;c[24][0]=.0001f;
            c[31][0]=c[31][1]=-128;c[31][2]=1.f/256;c[14][2]=c[71][2]=.5f;c[68][0]=c[69][1]=.05f;c[70][2]=.001f;c[72][0]=1;c[66][0]=c[66][1]=-1;c[66][2]=c[66][3]=1;
            NorthlightForever::Frame frame;for(unsigned i=0;i<3;++i){auto& l=frame.layer[i];l.flag[3]=l.flag[0]=1;l.flag[2]=float(noisy);l.p[1]=.3f;l.p[9]=1;l.p[10]=.5f;l.p[20]=.1f;l.p[21]=.03f;l.p[22]=.6f;l.p[23]=.4f;l.p[11]=.2f;l.p[6]=10;l.p[7]=30;for(unsigned k=0;k<3;++k){l.color[0][k]=.7f-.15f*k;l.color[1][k]=.4f-.1f*k;l.color[3][k]=.2f;}}
            NorthlightForever::constants(frame,c+116,range,1);
            const float surface=timing?200.f:range*.9f,rawDepth=5000.f/4999.5f*(1-.5f/surface);D3DLOCKED_RECT depthLock{};ok(depth->LockRect(0,&depthLock,nullptr,0));std::memcpy(depthLock.pBits,&rawDepth,4);ok(depth->UnlockRect(0));
            auto draw=[&](unsigned v,unsigned count){ok(d->SetPixelShader(ps[v][noisy]));ok(d->SetPixelShaderConstantF(0,c[0],154));ok(d->SetTexture(1,depth));ok(d->SetTexture(3,white));ok(d->SetTexture(4,actorMap));ok(d->SetTexture(5,tiles));ok(d->SetTexture(13,regional));ok(d->SetTexture(14,noise));NorthlightFogVolume::sampler(d,14);for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(d->SetSamplerState(14,state,D3DTADDRESS_WRAP));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));ok(d->BeginScene());for(unsigned i=0;i<count;++i)ok(NorthlightScreen::drawQuad(d,W,H));ok(d->EndScene());};
            std::vector<float> images[2];for(unsigned v=0;v<2;++v){draw(v,1);ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));images[v].resize(size_t(W)*H*4);for(unsigned y=0;y<H;++y)std::memcpy(images[v].data()+size_t(y)*W*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*16);ok(staging->UnlockRect());}
            double energy=0;for(size_t i=0;i<images[0].size();++i)if(i%4!=3)energy+=images[0][i];assert(energy>0);
            if(!noisy&&range==1000){if(pattern==0)openEnergy=energy;if(pattern==3)assert(energy<openEnergy*.99);}
            double squared=0;float peak=0;for(size_t i=0;i<images[0].size();++i){assert(std::isfinite(images[0][i])&&std::isfinite(images[1][i]));const float error=std::abs(images[0][i]-images[1][i]);if(i%4==3)assert(error==0);peak=std::max(peak,error);squared+=error*error;}assert(peak<.00015f);
            double ms[2]{};if(timing&&range==1000){std::vector<double> times[2];for(unsigned block=0;block<16;++block){const unsigned v=(block/2)%2?1-block%2:block%2;draw(v,1);wait();ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(v,4);ok(end->Issue(D3DISSUE_END));ok(freq->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait();UINT64 a=0,b=0,hz=0;BOOL invalid=TRUE;ok(start->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(freq->GetData(&hz,sizeof hz,0));ok(disjoint->GetData(&invalid,sizeof invalid,0));assert(!invalid&&hz&&b>=a);if(block>=4)times[v].push_back(1000.*double(b-a)/hz/4);}for(unsigned v=0;v<2;++v){std::sort(times[v].begin(),times[v].end());ms[v]=(times[v][2]+times[v][3])*.5;}}
            std::printf("SPARSE_FOG pattern=%u noise=%u range=%g max=%g rms=%g oldMs=%.6f newMs=%.6f\n",pattern,noisy,range,peak,std::sqrt(squared/images[0].size()),ms[0],ms[1]);++cases;
        }
        for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);actorMap->Release();tiles->Release();regional->Release();
    }
    for(auto& v:ps)for(auto* p:v)p->Release();for(auto* q:{event,start,end,freq,disjoint})q->Release();depth->Release();white->Release();noise->Release();target->Release();staging->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);std::printf("IMPORTED_FOG_SPARSE_PASS cases=%u\n",cases);
}
