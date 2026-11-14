// The production atlas against independent Beer-Lambert integration.
#define main unused_water_endpoint_main
#include "water_fog_endpoint_gpu.cpp"
#undef main
#include "fog_volume.h"
#include "world_compiled_shaders.h"
#include "shadow_transition_compiled_shaders.h"
#include "forever_atmosphere.h"

// A regional field stores the TOP surface of a whole WMO column. A roof at
// z=64 must not remove the authored global medium around a street at z=8.
static void globalMediumOracle(IDirect3DDevice9* d){
    const unsigned w=128,h=64,ow=w/2,oh=h/2;
    NorthlightFogVolume volume;assert(volume.resources(d,w,h));
    const DWORD* code[]={kForeverFogShader,kForeverFogNoNoiseShader,kForeverFogActorShader,kForeverFogActorNoNoiseShader,kForeverFogTransitionShader,kForeverFogNoNoiseTransitionShader,kForeverFogActorTransitionShader,kForeverFogActorNoNoiseTransitionShader};
    IDirect3DPixelShader9* fallback[8]={};for(unsigned i=0;i<8;++i)ok(d->CreatePixelShader(code[i],&fallback[i]));
    IDirect3DSurface9 *output=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(ow,oh,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&output,nullptr));
    ok(d->CreateOffscreenPlainSurface(ow,oh,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    auto scalar=[&](float v){Image im{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(im.pixels.data(),&v,4);return texture(d,im);};
    auto* depth=scalar(1000.f/999.5f*(1-.5f/100));IDirect3DTexture9* shadow[]={scalar(.6f),scalar(.4f)};
    unsigned cases=0;float maximum=0;
    for(unsigned variant=0;variant<8;++variant)for(unsigned mode=0;mode<2;++mode)for(unsigned profile=0;profile<3;++profile){
        std::vector<float> baseline;
        for(unsigned fieldCase=0;fieldCase<(profile==0?5u:2u);++fieldCase){
            Image im{64,64,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(64*64*16)};
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
                const float v[]={fieldCase==1||(fieldCase==2&&x>=32)?64.f:0.f,0,0,fieldCase==3?0.f:1.25f};
                std::memcpy(im.pixels.data()+(y*64+x)*16,v,16);
            }
            auto* field=texture(d,im);std::vector<float> lit;
            for(unsigned blocked=0;blocked<2;++blocked){
                float c[157][4]{};c[0][0]=1.f/w;c[0][1]=1.f/h;c[0][2]=.5f;c[0][3]=1000;
                c[1][0]=2;c[1][1]=100;c[1][2]=c[2][0]=1;c[2][2]=192;c[2][3]=1.f/1024;
                c[3][0]=c[4][2]=c[5][1]=c[6][3]=1;c[6][2]=c[15][2]=8;
                c[16][1]=blocked?-1.f:1.f;c[21][0]=c[27][0]=1;
                c[31][0]=c[31][1]=fieldCase==4?10000.f:-256.f;c[31][2]=1.f/512;
                c[33][0]=w;c[33][1]=h;c[33][2]=ow;c[33][3]=oh;
                c[14][2]=c[71][2]=.5f;c[70][2]=.001f;c[72][0]=(variant&2)?1.f:0.f;
                c[66][0]=c[66][1]=-1;c[66][2]=c[66][3]=1;
                c[89][1]=.4f;c[94][0]=c[95][1]=c[96][2]=c[97][3]=1;c[101][0]=c[102][1]=c[103][2]=c[104][3]=1;
                NorthlightForever::Frame frame;auto& layer=frame.layer[0];layer.flag[3]=layer.flag[0]=1;
                layer.p[1]=1;layer.p[9]=2;layer.p[10]=.4f;
                if(profile){layer.p[6]=2;layer.p[7]=10;layer.flag[1]=profile==2?1.f:0.f;}
                for(unsigned k=0;k<3;++k){layer.color[0][k]=.8f;layer.color[1][k]=layer.color[3][k]=.3f;layer.color[2][k]=.2f;}
                float bank[38][4];NorthlightForever::constants(frame,bank,100,1);
                ok(d->SetPixelShaderConstantF(0,c[0],116));ok(d->SetPixelShaderConstantF(116,bank[0],38));volume.grid(d,100);
                ok(d->SetTexture(1,depth));ok(d->SetTexture(3,shadow[blocked]));ok(d->SetTexture(4,shadow[0]));
                ok(d->SetTexture(7,shadow[blocked]));ok(d->SetTexture(9,shadow[0]));ok(d->SetTexture(13,field));
                ok(d->BeginScene());
                if(mode){assert(volume.target(d,volume.raw()));assert(volume.injection(d,volume.inject[variant],100,(variant&1)==0,true));assert(volume.integrate(d,100,c[1],false));assert(volume.composite(d,output,ow,oh));}
                else{ok(d->SetTexture(0,nullptr));ok(d->SetRenderTarget(0,output));ok(d->SetPixelShader(fallback[variant]));D3DVIEWPORT9 vp={0,0,ow,oh,0,1};ok(d->SetViewport(&vp));ok(NorthlightScreen::drawQuad(d,ow,oh));}
                ok(d->EndScene());ok(d->GetRenderTargetData(output,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
                std::vector<float> values(size_t(ow)*oh*4);for(unsigned y=0;y<oh;++y)std::memcpy(values.data()+size_t(y)*ow*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,ow*16);ok(staging->UnlockRect());
                double litEnergy=0,energy=0;
                for(unsigned y=0;y<oh;++y)for(unsigned x=0;x<ow;++x){
                    const size_t i=(size_t(y)*ow+x)*4;
                    float rx=(2.f*x/w*2-1)/2,rz=(1-2.f*y/h*2)/100,rayZ=rz/std::sqrt(1+rx*rx+rz*rz);
                    double optical=0;for(unsigned j=0;j<512;++j){float t=100.f*(j+.5f)/512;optical+=NorthlightForever::sigma(frame,t,8+rayZ*t,fieldCase==1?64.f:0.f)*(100./512);}
                    const float expected=fieldCase==3?1.f:float(std::exp(-optical)),error=std::abs(values[i+3]-expected);maximum=std::max(maximum,error);
                    if(error>.006f){std::fprintf(stderr,"MEDIUM_FAIL variant=%u atlas=%u profile=%u field=%u blocked=%u xy=%u,%u T=%g expected=%g\n",variant,mode,profile,fieldCase,blocked,x,y,values[i+3],expected);std::abort();}
                    for(unsigned k=0;k<4;++k)assert(std::isfinite(values[i+k]));
                    // Light changes may alter RGB, never extinction. An absolute
                    // profile must also ignore a raised/split roof envelope.
                    if(blocked){assert(values[i+3]==lit[i+3]);energy+=values[i];litEnergy+=lit[i];}
                    if((fieldCase==1||fieldCase==2)&&profile!=2&&values[i+3]!=baseline[i+3]){
                        std::fprintf(stderr,"MEDIUM_FIELD variant=%u atlas=%u profile=%u field=%u blocked=%u xy=%u,%u T=%.9g baseline=%.9g\n",variant,mode,profile,fieldCase,blocked,x,y,values[i+3],baseline[i+3]);std::abort();
                    }
                }
                if(!blocked)lit=values;else if(fieldCase!=3)assert(energy>0&&energy<litEnergy*.9);
                if(!fieldCase&&!blocked)baseline=values;
                ++cases;
            }
            d->SetTexture(13,nullptr);field->Release();
        }
    }
    for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);volume.reset();for(auto* p:fallback)p->Release();for(auto* p:shadow)p->Release();depth->Release();output->Release();staging->Release();
    std::printf("FOG_GLOBAL_MEDIUM_PASS cases=%u maxTransmittanceError=%g\n",cases,maximum);
}

int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    assert(argc==2);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightVolumeOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    for(unsigned s=0;s<16;++s){NorthlightFogVolume::sampler(d,s);ok(d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT));}
    globalMediumOracle(d);
    float maximum=0;unsigned cases=0;
    for(auto dimensions:{std::pair<unsigned,unsigned>{64,48},{137,79},{2,2}}){
        const unsigned w=dimensions.first,h=dimensions.second,ow=w/2,oh=h/2;
        NorthlightFogVolume volume;assert(volume.resources(d,w,h));
        IDirect3DSurface9 *output=nullptr,*staging=nullptr;
        ok(d->CreateRenderTarget(ow,oh,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&output,nullptr));ok(d->CreateOffscreenPlainSurface(ow,oh,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        IDirect3DTexture9* depth=nullptr;IDirect3DTexture9* field=nullptr;IDirect3DTexture9* water=nullptr;
        Image fieldImage{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};float fv[]={-1000,0,0,1.25f};std::memcpy(fieldImage.pixels.data(),fv,16);field=texture(d,fieldImage);ok(d->SetTexture(13,field));
        for(float sign:{1.f,-1.f})for(bool wet:{false,true})for(float sigma:{0.f,.0001f,.003f,.1f})for(float z:{.5f,3.f,31.f,100.f,1000.f})for(unsigned frame=0;frame<3;++frame){
            float c[157][4]{};c[0][0]=1.f/w;c[0][1]=1.f/h;c[0][2]=.5f;c[0][3]=1000;c[1][0]=c[1][1]=2;c[1][2]=c[2][0]=1;
            c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[53][0]=c[54][1]=c[55][2]=c[56][3]=1;
            c[15][0]=c[6][0]=float(frame)*.125f;c[56][0]=-float(frame- (frame?1:0))*.125f;
            c[31][0]=c[31][1]=-256;c[31][2]=1.f/512;c[33][0]=float(w);c[33][1]=float(h);c[33][2]=float(ow);c[33][3]=float(oh);c[27][0]=1;
            c[116][0]=sigma;c[116][3]=-1000;c[117][3]=100;c[131][0]=.4f;c[131][1]=.2f;c[131][2]=.1f;
            c[1][2]=sign;c[30][0]=wet?1.f:0.f;
            ok(d->SetPixelShaderConstantF(0,c[0],157));volume.grid(d,100);
            Image depthImage{w,h,D3DFMT_R32F,4,std::vector<unsigned char>(size_t(w)*h*4)};
            const float dv=1000.f/999.5f*(1-.5f/z);for(size_t i=0;i<depthImage.pixels.size();i+=4)std::memcpy(depthImage.pixels.data()+i,&dv,4);
            ok(d->SetTexture(1,nullptr));if(depth)depth->Release();depth=texture(d,depthImage);ok(d->SetTexture(1,depth));
            ok(d->SetTexture(11,nullptr));if(water)water->Release();Image wi{1,1,D3DFMT_G32R32F,8,std::vector<unsigned char>(8)};const float endpoint[]={z*.5f,1};std::memcpy(wi.pixels.data(),endpoint,8);water=texture(d,wi);ok(d->SetTexture(11,water));
            assert(volume.target(d,volume.raw()));ok(d->SetPixelShader(volume.inject[1]));ok(d->BeginScene());ok(volume.draw(d));
            assert(volume.integrate(d,100,c[1],frame>0));assert(volume.composite(d,output,ow,oh));ok(d->EndScene());
            ok(d->GetRenderTargetData(output,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned y=0;y<oh;++y)for(unsigned x=0;x<ow;++x){
                float value[4];std::memcpy(value,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,16);
                // depthUV selects a full-resolution texel and viewPosition subtracts its half texel.
                float u=(std::min(unsigned((float(x)+.5f)*w/ow),w-1))/float(w),v=(std::min(unsigned((float(y)+.5f)*h/oh),h-1))/float(h);
                float distance=std::min(100.f,(wet&&z>1?z*.5f:z)*std::sqrt(1+(u-.5f)*(u-.5f)+(v-.5f)*(v-.5f)));
                float T=std::exp(-sigma*distance),expected[]={.4f*(1-T),.2f*(1-T),.1f*(1-T),T};
                for(unsigned k=0;k<4;++k){assert(std::isfinite(value[k]));maximum=std::max(maximum,std::abs(value[k]-expected[k]));if(std::abs(value[k]-expected[k])>.006f){std::fprintf(stderr,"FAIL %ux%u sigma=%g z=%g frame=%u xy=%u,%u channel=%u got=%g expected=%g\n",w,h,sigma,z,frame,x,y,k,value[k],expected[k]);std::abort();}}
            }
            ok(staging->UnlockRect());++cases;
        }
        for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);volume.reset();depth->Release();field->Release();water->Release();output->Release();staging->Release();
    }
    // Independent trilinear/history oracle: a known affine coefficient volume,
    // camera translation and rejected cells outside the previous frustum.
    {
        NorthlightFogVolume volume;assert(volume.resources(d,128,96));const unsigned cw=volume.cellWidth,ch=volume.cellHeight,aw=cw*8,ah=ch*8;
        IDirect3DTexture9* input[2]={};
        auto coefficient=[](float u,float v,float s){return .1f+.2f*u+.15f*v+.002f*s;};
        for(unsigned mode=0;mode<2;++mode){Image im{aw,ah,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(size_t(aw)*ah*16)};
            for(unsigned y=0;y<ah;++y)for(unsigned x=0;x<aw;++x){const float value=coefficient((x%cw+.5f)/cw,(y%ch+.5f)/ch,float(x/cw+8*(y/ch)))+.05f*mode;float v[]={value,value*.5f,value*.25f,.003f};std::memcpy(im.pixels.data()+(size_t(y)*aw+x)*16,v,16);}input[mode]=texture(d,im);}
        IDirect3DSurface9 *target=nullptr,*readback=nullptr;ok(d->CreateRenderTarget(aw,ah,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(aw,ah,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
        for(float shift:{0.f,.125f,4.f}){
            float c[157][4]{};c[1][0]=c[1][1]=2;c[1][2]=1;c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[15][0]=shift;
            c[53][0]=c[54][1]=c[55][2]=c[56][3]=1;ok(d->SetPixelShaderConstantF(0,c[0],157));volume.grid(d,100);
            const float pass[]={100,0,.85f,0},previous[]={2,2,1,100};ok(d->SetPixelShaderConstantF(155,pass,1));ok(d->SetPixelShaderConstantF(156,previous,1));
            ok(d->SetRenderTarget(0,target));NorthlightFogVolume::sampler(d,0);NorthlightFogVolume::sampler(d,2);ok(d->SetTexture(0,input[0]));ok(d->SetTexture(2,input[1]));ok(d->SetPixelShader(volume.temporal));ok(d->BeginScene());ok(volume.draw(d));ok(d->EndScene());
            ok(d->GetRenderTargetData(target,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            unsigned blended=0,rejected=0;
            for(unsigned s=0;s<64;++s)for(unsigned y=0;y<ch;++y)for(unsigned x=0;x<cw;++x){
                const float u=(x+.5f)/cw,v=(y+.5f)/ch,t=100.f*std::pow((s+.5f)/64,3);
                float rx=u-.5f,ry=.5f-v,rz=1,len=std::sqrt(rx*rx+ry*ry+1);rx=rx/len*t+shift;ry=ry/len*t;rz=rz/len*t;
                float ou=rx/rz+.5f,ov=.5f-ry/rz,os=std::cbrt(std::sqrt(rx*rx+ry*ry+rz*rz)/100)*64-.5f;
                const float current=coefficient(u,v,float(s));float expected=current;
                if(ou>=0&&ou<=1&&ov>=0&&ov<=1&&os>=-.001f&&os<=63.001f){
                    os=std::clamp(os,0.f,63.f);
                    float lo=current,hi=current;
                    for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}){
                        const float nu=std::clamp(u+(axis==0?sign/cw:0.f),.5f/cw,1-.5f/cw),nv=std::clamp(v+(axis==1?sign/ch:0.f),.5f/ch,1-.5f/ch),ns=std::clamp(float(s)+(axis==2?sign:0.f),0.f,63.f),value=coefficient(nu,nv,ns);lo=std::min(lo,value);hi=std::max(hi,value);}
                    float history=coefficient(std::clamp(ou,.5f/cw,1-.5f/cw),std::clamp(ov,.5f/ch,1-.5f/ch),os)+.05f;
                    expected=current*.15f+std::clamp(history,lo,hi)*.85f;++blended;
                }else ++rejected;
                float actual;std::memcpy(&actual,static_cast<char*>(lock.pBits)+(s/8*ch+y)*lock.Pitch+(s%8*cw+x)*16,4);
                if(!std::isfinite(actual)||std::abs(actual-expected)>=2e-5f){std::fprintf(stderr,"TEMPORAL shift=%g cell=%u,%u,%u previous=%g,%g,%g got=%g expected=%g\n",shift,x,y,s,ou,ov,os,actual,expected);std::abort();}
            }
            assert(blended>0);if(shift>0)assert(rejected>0);ok(readback->UnlockRect());++cases;
        }
        d->SetTexture(0,nullptr);d->SetTexture(2,nullptr);for(auto* p:input)p->Release();target->Release();readback->Release();volume.reset();
    }
    // Compare complete production paths at a fixed view, including animated
    // 3D noise and nonuniform height/distance layers. Optional timings use GPU
    // timestamps, never CPU submission duration.
    {
        const bool benchmark=std::getenv("NORTHLIGHT_FOG_VOLUME_BENCHMARK")!=nullptr;
        const unsigned w=benchmark?1920:640,h=benchmark?1080:360,ow=w/2,oh=h/2;
        NorthlightFogVolume volume;assert(volume.resources(d,w,h));
        IDirect3DPixelShader9* old[8]={};const DWORD* oldCode[]={kForeverFogShader,kForeverFogNoNoiseShader,kForeverFogActorShader,kForeverFogActorNoNoiseShader,kForeverFogTransitionShader,kForeverFogNoNoiseTransitionShader,kForeverFogActorTransitionShader,kForeverFogActorNoNoiseTransitionShader};for(unsigned i=0;i<8;++i)ok(d->CreatePixelShader(oldCode[i],&old[i]));
        IDirect3DSurface9 *output=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(ow,oh,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&output,nullptr));ok(d->CreateOffscreenPlainSurface(ow,oh,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        auto download=[&](){ok(d->GetRenderTargetData(output,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> result(size_t(ow)*oh*4);for(unsigned y=0;y<oh;++y)std::memcpy(result.data()+size_t(y)*ow*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,ow*16);ok(staging->UnlockRect());return result;};
        Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
        const float fieldValue[]={0,0,0,1.25f};std::memcpy(im.pixels.data(),fieldValue,16);auto* field=texture(d,im);
        Image di{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};float depth=1000.f/999.5f*(1-.5f/100);std::memcpy(di.pixels.data(),&depth,4);auto* depthTexture=texture(d,di);
        const float litDepth=.6f,blockedDepth=.4f;std::memcpy(di.pixels.data(),&litDepth,4);auto* litShadow=texture(d,di);std::memcpy(di.pixels.data(),&blockedDepth,4);auto* blockedShadow=texture(d,di);
        IDirect3DVolumeTexture9* noise=nullptr;ok(d->CreateVolumeTexture(16,16,16,1,0,D3DFMT_L8,D3DPOOL_MANAGED,&noise,nullptr));D3DLOCKED_BOX box{};ok(noise->LockBox(0,&box,nullptr,0));for(unsigned z=0;z<16;++z)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)*(static_cast<unsigned char*>(box.pBits)+z*box.SlicePitch+y*box.RowPitch+x)=static_cast<unsigned char>(128+60*std::sin(x*.392699f)*std::cos(y*.392699f)*std::sin(z*.392699f));ok(noise->UnlockBox(0));
        IDirect3DQuery9 *begin=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
        if(benchmark){ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));}
        auto collect=[](IDirect3DQuery9* q,void* value,unsigned bytes){HRESULT hr;DWORD start=GetTickCount();while((hr=q->GetData(value,bytes,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-start<10000);SwitchToThread();}ok(hr);};
        for(unsigned shadows=0;shadows<4;++shadows)for(unsigned noisy=0;noisy<2;++noisy)for(float range:{128.f,1000.f,4000.f})for(unsigned shape=0;shape<3;++shape){
            const bool actor=shadows&1,transition=shadows&2;const unsigned variant=(transition?4:0)+(actor?2:0)+(noisy?0:1);
            float c[157][4]{};c[0][0]=1.f/w;c[0][1]=1.f/h;c[0][2]=.5f;c[0][3]=1000;c[1][0]=c[1][1]=2;c[1][2]=c[2][0]=1;c[2][2]=192;c[2][3]=1.f/1024;
            c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[6][2]=c[15][2]=20;c[16][2]=1;c[21][0]=c[27][0]=1;c[24][3]=31;
            c[31][0]=c[31][1]=-256;c[31][2]=1.f/512;c[33][0]=float(w);c[33][1]=float(h);c[33][2]=float(ow);c[33][3]=float(oh);
            c[14][2]=c[71][2]=.5f;c[70][2]=.001f;c[72][0]=actor?1.f:0.f;c[66][0]=c[66][1]=-1;c[66][2]=c[66][3]=1;
            c[89][1]=.4f;c[94][0]=c[95][1]=c[96][2]=c[97][3]=1;c[101][0]=c[102][1]=c[103][2]=c[104][3]=1;
            NorthlightForever::Frame frame;for(unsigned i=0;i<3;++i){auto& l=frame.layer[i];l.flag[3]=1;l.flag[2]=float(noisy);l.p[1]=.3f;l.p[9]=1;l.p[10]=.5f;l.p[20]=.1f;l.p[21]=.03f;l.p[22]=.6f;l.p[23]=.4f;l.p[11]=.2f;
                l.flag[0]=shadows?1.f:0.f;
                if(shape==1){l.p[6]=10;l.p[7]=30;}if(shape==2){l.p[0]=float(i*15);l.p[6]=20;l.p[7]=40;l.p[18]=.5f;l.p[19]=.5f;}
                for(unsigned k=0;k<3;++k){l.color[0][k]=.7f-.15f*k;l.color[1][k]=.4f-.1f*k;l.color[3][k]=.2f;}}
            float bank[38][4];NorthlightForever::constants(frame,bank,range,1);
            auto constants=[&](){ok(d->SetPixelShaderConstantF(0,c[0],116));ok(d->SetPixelShaderConstantF(116,bank[0],38));volume.grid(d,range);ok(d->SetTexture(1,depthTexture));ok(d->SetTexture(3,litShadow));ok(d->SetTexture(4,blockedShadow));ok(d->SetTexture(7,blockedShadow));ok(d->SetTexture(9,litShadow));ok(d->SetTexture(13,field));ok(d->SetTexture(14,noise));NorthlightFogVolume::sampler(d,14);for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(d->SetSamplerState(14,state,D3DTADDRESS_WRAP));};
            auto draw=[&](unsigned mode){constants();if(mode==0){ok(d->SetTexture(0,nullptr));ok(d->SetRenderTarget(0,output));ok(d->SetPixelShader(old[variant]));D3DVIEWPORT9 vp={0,0,ow,oh,0,1};ok(d->SetViewport(&vp));ok(NorthlightScreen::drawQuad(d,ow,oh));}
                else{if(mode==1){assert(volume.target(d,volume.raw()));assert(volume.injection(d,volume.inject[variant],range,noisy,true));assert(volume.integrate(d,range,c[1],false));}assert(volume.composite(d,output,ow,oh));}};
            std::vector<float> images[2];for(unsigned mode=0;mode<2;++mode){ok(d->BeginScene());draw(mode);ok(d->EndScene());images[mode]=download();}
            double sum=0;float peak=0;for(size_t i=0;i<images[0].size();++i){assert(std::isfinite(images[0][i])&&std::isfinite(images[1][i]));float error=std::abs(images[0][i]-images[1][i]);sum+=error*error;peak=std::max(peak,error);}
            const double rms=std::sqrt(sum/images[0].size());std::printf("FOG_VOLUME_REFERENCE shadows=%u noise=%u range=%g shape=%u rms=%g max=%g\n",shadows,noisy,range,shape,rms,peak);assert(rms<.008&&peak<.04f);++cases;
            if(benchmark&&shape==1&&range==1000&&(shadows==0||shadows==3)){std::vector<double> times[3];
                for(unsigned block=0;block<6;++block)for(unsigned mode:{0u,1u,2u,2u,1u,0u}){
                    ok(d->BeginScene());draw(mode);ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));ok(begin->Issue(D3DISSUE_END));for(unsigned n=0;n<4;++n)draw(mode);ok(end->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));ok(d->EndScene());
                    UINT64 a=0,b=0,hz=0;BOOL invalid=TRUE;collect(end,&b,sizeof b);collect(begin,&a,sizeof a);collect(frequency,&hz,sizeof hz);collect(disjoint,&invalid,sizeof invalid);assert(!invalid&&hz&&b>=a);times[mode].push_back(1000.*double(b-a)/double(hz)/4);}
                for(auto& values:times)std::sort(values.begin(),values.end());
                std::printf("FOG_VOLUME_GPU_MS shadows=%u noise=%u width=%u height=%u raymarch=%g volume=%g cached=%g\n",shadows,noisy,w,h,(times[0][5]+times[0][6])*.5,(times[1][5]+times[1][6])*.5,(times[2][5]+times[2][6])*.5);
            }
        }
        for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);volume.reset();for(auto* p:old)p->Release();field->Release();depthTexture->Release();litShadow->Release();blockedShadow->Release();noise->Release();output->Release();staging->Release();if(begin){begin->Release();end->Release();frequency->Release();disjoint->Release();}
    }
    std::printf("FOG_VOLUME_GPU_PASS cases=%u maxError=%g\n",cases,maximum);
    d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
