// Real client VS/PS programs: single-source endpoints preserve native material,
// saturation, fog, alpha test, blend and MSAA. No WoW execution.
#include <windows.h>
#include <d3d9.h>
#include "native_local_light_renderer.h"
#include "native_direct_ownership.h"
#include "forwarders.h"
#include "native_local_sampler_read_checks.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <chrono>
static int failAllocation=-1;
void* operator new(std::size_t n){if(failAllocation==0){failAllocation=-1;throw std::bad_alloc();}if(failAllocation>0)--failAllocation;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static void must(HRESULT hr){if(FAILED(hr)){std::printf("HRESULT %08lx\n",(unsigned long)hr);std::abort();}}
static std::vector<DWORD> words(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f&&f.tellg()%4==0);std::vector<DWORD> out(size_t(f.tellg())/4);f.seekg(0);f.read(reinterpret_cast<char*>(out.data()),out.size()*4);assert(f);return out;}
#include "native_local_sharing_checks.h"
#include "native_local_vertex_reuse_checks.h"
#include "native_local_pixel_reuse_checks.h"
static std::vector<float> pixels(IDirect3DDevice9* d,IDirect3DSurface9* rt,unsigned size){IDirect3DSurface9* copy=nullptr;must(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&copy,nullptr));must(d->GetRenderTargetData(rt,copy));D3DLOCKED_RECT lock;must(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(size*size*4);for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size*4;++x)out[y*size*4+x]=NorthlightActorDeformation::half(reinterpret_cast<const uint16_t*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)[x]);must(copy->UnlockRect());copy->Release();nativeLocalRecord(out);return out;}
static void save(const std::string& path,const std::vector<float>& image,unsigned size){std::ofstream f(path,std::ios::binary);unsigned h[]={0x31524746,size,size,116,16};f.write(reinterpret_cast<char*>(h),20);f.write(reinterpret_cast<const char*>(image.data()),image.size()*4);assert(f);}
#include "native_local_light_final.inc"
struct FaultDevice:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    unsigned sets=0,failAt=0;bool failPrivatePS=false,failOriginalVS=false;IDirect3DVertexShader9* originalVS=nullptr;IDirect3DPixelShader9* originalPS=nullptr;
    bool fail(){return ++sets==failAt;}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* p)override{if(fail()||(failOriginalVS&&p==originalVS))return E_FAIL;return real->SetVertexShader(p);}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* p)override{if(fail())return E_FAIL;if(failPrivatePS&&p!=originalPS){failPrivatePS=false;return E_FAIL;}return real->SetPixelShader(p);}
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT first,const float* p,UINT n)override{if(fail())return E_FAIL;return real->SetVertexShaderConstantF(first,p,n);}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT first,const float* p,UINT n)override{if(fail())return E_FAIL;return real->SetPixelShaderConstantF(first,p,n);}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture9* p)override{if(fail())return E_FAIL;return real->SetTexture(stage,p);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD stage,D3DSAMPLERSTATETYPE key,DWORD value)override{if(fail())return E_FAIL;return real->SetSamplerState(stage,key,value);}
};
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const bool singleOnly=std::getenv("NORTHLIGHT_TEST_EXPECT_SINGLE_ONLY")!=nullptr;
    assert(argc==7);constexpr unsigned Size=256;const bool wmo=std::strcmp(argv[5],"wmo")==0;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightNativeLocalTest";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,Size,Size,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=Size;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    NativeSamplerReadDevice samplerDevice(d);d=&samplerDevice;
    auto v=words(argv[2]),p=words(argv[3]);IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;must(d->CreateVertexShader(v.data(),&vs));must(d->CreatePixelShader(p.data(),&ps));
    unsigned slots[]={0,1};std::vector<uint32_t> negative;NorthlightNativeLocalPatch::VertexInfo vi;const bool two=NorthlightEquipmentLightProof::analyze(reinterpret_cast<const uint32_t*>(v.data()),v.size())>=2;assert(NorthlightNativeLocalPatch::vertex(reinterpret_cast<const uint32_t*>(v.data()),v.size(),slots,2,negative,vi)==two);
    {std::vector<uint32_t> pixel2,displaced2;NorthlightNativeLocalPatch::PixelInfo info2;NorthlightDisplacementPatch::Binding binding2;
     const bool pix=two&&NorthlightNativeLocalPatch::pixel(reinterpret_cast<const uint32_t*>(p.data()),p.size(),vi,reinterpret_cast<const uint32_t*>(kNativeLocalLighting2Shader),sizeof(kNativeLocalLighting2Shader)/4,pixel2,info2);
     const bool disp=two&&NorthlightDisplacementPatch::patch(negative.data(),negative.size(),displaced2,binding2,true,true);
     std::printf("NATIVE_DUAL_COMPILE vertex=%u pixel=%u displaced=%u pixelAdded=%u vertexWords=%zu\n",unsigned(two),unsigned(pix),unsigned(disp),info2.addedSlots,negative.size());}
    {unsigned slots3[]={0,1,2};std::vector<uint32_t> rejected;NorthlightNativeLocalPatch::VertexInfo info;assert(!NorthlightNativeLocalPatch::vertex(reinterpret_cast<const uint32_t*>(v.data()),v.size(),slots3,3,rejected,info));}
    {using namespace NorthlightWaterShaderPatch;auto integerNative=p;integerNative.insert(integerNative.begin()+1,{0x05000030u,dst(7,0),4,0,1,0});std::vector<uint32_t> rejected;NorthlightNativeLocalPatch::PixelInfo info;
     assert(!NorthlightNativeLocalPatch::pixel(reinterpret_cast<const uint32_t*>(integerNative.data()),integerNative.size(),vi,reinterpret_cast<const uint32_t*>(kNativeLocalLighting2Shader),sizeof(kNativeLocalLighting2Shader)/4,rejected,info));
     std::vector<uint32_t> undefined(std::begin(kNativeLocalLighting2Shader),std::end(kNativeLocalLighting2Shader));bool found=false;for(size_t at=1;at+1<undefined.size();){const unsigned op=undefined[at]&65535,n=op==65534?(undefined[at]>>16)&32767:(undefined[at]>>24)&15;if(op==48){undefined[at+1]=replaceReg(undefined[at+1],7,15);found=true;break;}at+=n+1;}assert(found);
     assert(!NorthlightNativeLocalPatch::pixel(reinterpret_cast<const uint32_t*>(p.data()),p.size(),vi,undefined.data(),undefined.size(),rejected,info));}
    int integerBank[16][4];for(unsigned i=0;i<16;++i)for(unsigned k=0;k<4;++k)integerBank[i][k]=int(100+i*4+k);must(d->SetPixelShaderConstantI(0,integerBank[0],16));
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,16,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,28,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,40,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,7},{0,52,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,6},{0,60,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDWEIGHT,0},D3DDECL_END()};IDirect3DVertexDeclaration9* declaration=nullptr;must(d->CreateVertexDeclaration(elements,&declaration));
    struct Vertex {float x,y,z;DWORD index;float nx,ny,nz;DWORD color;float u,v;float dx,dy,dz,hu,hv,weights[4];};Vertex plane[]={{-8,8,8,0,0,0,-1,0xb3ffffff,0,0},{8,8,8,0,0,0,-1,0xb3ffffff,1,0},{-8,-8,8,0,0,0,-1,0xb3ffffff,0,1},{8,-8,8,0,0,0,-1,0xb3ffffff,1,1}};
    for(auto& vertex:plane){vertex.dz=1.5f;vertex.hu=vertex.u;vertex.hv=vertex.v;vertex.weights[0]=1;}
    const bool gpu=std::getenv("NORTHLIGHT_TEST_DISPLACEMENT_CPU")==nullptr;
    std::vector<uint32_t> displaced;NorthlightDisplacementPatch::Binding binding;
    assert(NorthlightDisplacementPatch::patch(reinterpret_cast<const uint32_t*>(v.data()),v.size(),displaced,binding,true,gpu));
    IDirect3DVertexShader9* boundVS=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(displaced.data()),&boundVS));
    NorthlightNativeDisplacement descriptor{vs,boundVS,binding.constant,binding.count,binding.sampler,gpu};
    IDirect3DTexture9* height=nullptr;must(d->CreateTexture(8,8,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&height,nullptr));D3DLOCKED_RECT heightLock;must(height->LockRect(0,&heightLock,nullptr,0));
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)reinterpret_cast<float*>(static_cast<char*>(heightLock.pBits)+y*heightLock.Pitch)[x]=.65f+.03f*x;must(height->UnlockRect(0));
    const unsigned heightSampler=D3DVERTEXTEXTURESAMPLER0+binding.sampler;must(d->SetTexture(heightSampler,height));for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(heightSampler,key,D3DTEXF_POINT));
    IDirect3DTexture9* texture=nullptr;must(d->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));D3DLOCKED_RECT lock;must(texture->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=((x+y)%2?0xff000000:0x00000000)|0x00b39980;must(texture->UnlockRect(0));
    IDirect3DCubeTexture9* cube=nullptr;must(d->CreateCubeTexture(256,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&cube,nullptr));
    auto fillCube=[&](unsigned mode){for(unsigned face=0;face<6;++face){D3DLOCKED_RECT l;must(cube->LockRect(D3DCUBEMAP_FACES(face),0,&l,nullptr,0));for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)reinterpret_cast<float*>(static_cast<char*>(l.pBits)+y*l.Pitch)[x]=mode==0?1.f:mode==1?0.f:x<128?1.f:0.f;must(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}};
    NorthlightNativeLocalDraw light;light.cube=cube;light.position[2]=4;light.range=12;for(unsigned i=0;i<16;++i)light.inverseView[i]=i%5==0?1.f:0;

#ifndef NORTHLIGHT_NATIVE_DUPLICATE_BASELINE
    if(two&&std::getenv("NORTHLIGHT_TEST_PIXEL_REUSE"))nativeLocalPixelReuse(d,light,boundVS,ps,nullptr,&descriptor);
    if(two&&std::getenv("NORTHLIGHT_TEST_VERTEX_REUSE"))nativeLocalVertexReuse(d,light,boundVS,ps,nullptr,&descriptor);
#endif
    NorthlightNativeLocalLighting renderer(d);samplerDevice.attach(renderer);unsigned cases=0;float worst=0;double coldMs=0;std::vector<float> native,blocked,edge;std::vector<std::vector<float>> fixture;IDirect3DSurface9* resolved=nullptr;must(d->CreateRenderTarget(Size,Size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
    for(unsigned ms:{0u,2u}){IDirect3DSurface9* target=nullptr;if(ms){if(FAILED(d->CreateRenderTarget(Size,Size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(ms),0,FALSE,&target,nullptr)))continue;}else{target=resolved;target->AddRef();}
    for(unsigned mode=0;mode<3;++mode)for(float fog:{1.f,.35f})for(unsigned context:{0u,1u})for(float strength:{0.f,.15f,1.5f}){
        must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));D3DVIEWPORT9 viewport{0,0,Size,Size,0,1};must(d->SetViewport(&viewport));must(d->SetVertexDeclaration(declaration));must(d->SetVertexShader(boundVS));must(d->SetPixelShader(ps));
        const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={{D3DRS_ZENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FOGENABLE,FALSE},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_ALPHABLENDENABLE,mode==2},{D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},{D3DRS_BLENDOP,D3DBLENDOP_ADD},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_ALPHATESTENABLE,mode==1},{D3DRS_ALPHAFUNC,D3DCMP_GREATER},{D3DRS_ALPHAREF,128},{D3DRS_MULTISAMPLEANTIALIAS,TRUE}};for(auto state:states)must(d->SetRenderState(state.first,state.second));
        float constants[256][4]{};constants[2][0]=constants[3][1]=1;constants[4][2]=64.f/63.9f;constants[4][3]=-.1f*constants[4][2];constants[5][2]=1;constants[6][0]=constants[7][1]=1;
        for(unsigned i=0;i<16;++i)light.inverseView[i]=i%5==0?1.f:0;light.position[0]=light.position[1]=0;light.position[2]=4;
        if(context){const float angle=.3f,pitch=.2f,s=std::sin(angle),c=std::cos(angle),sp=std::sin(pitch),cp=std::cos(pitch);
            const float view[]={c,s,0,0,-s*cp,c*cp,sp,0,s*sp,-c*sp,cp,0,-8574.044922f,997.463379f,103.869629f,1};std::memcpy(light.inverseView,view,64);
            for(unsigned k=0;k<3;++k)light.position[k]=view[12+k]+4*view[8+k];constants[2][0]=1.13f;constants[3][1]=1.27f;constants[2][2]=.07f;constants[3][2]=-.05f;
        }
        for(unsigned k=0;k<3;++k){constants[10][k]=.2f;constants[11][k]=.1f;constants[28][k]=1;}constants[12][2]=1;constants[28][3]=.7f;
        constants[17][0]=12;constants[17][1]=.4f;constants[17][2]=.1f;constants[18][0]=.15f;constants[18][1]=.8f;constants[18][2]=.2f;
        constants[21][2]=4;constants[22][0]=-2;constants[22][1]=2;constants[22][2]=5;
        constants[25][0]=constants[25][1]=1;constants[26][0]=constants[26][1]=.2f;constants[27][0]=constants[27][1]=.01f;constants[30][1]=fog;constants[30][2]=1;constants[31][0]=constants[32][1]=constants[33][2]=1;
        NorthlightLocalLights::Light authored{};authored.kind=2;authored.flags=2;authored.sourceId=3183403080900234831ull;std::copy_n(light.position,3,authored.position);std::copy_n(constants[17],3,authored.diffuse);
        const float denominator[]={constants[25][0],constants[26][0],constants[27][0]};authored=NorthlightLocalLights::nativeTransport(authored,denominator);
        struct OwnBinding {const NorthlightNativeLights::Point* native;const NorthlightLocalLights::Light* authored;};
        const OwnBinding ownBinding{nullptr,&authored};NorthlightNativeDirect::BankMemo ownershipMemo;unsigned ownershipHits=0;
        NorthlightNativeLights::Snapshot incomplete;assert(!NorthlightNativeDirect::source(authored,incomplete));
        constants[binding.constant][0]=strength;if(gpu)constants[binding.constant+1][0]=.5f;must(d->SetVertexShaderConstantF(0,constants[0],256));float pixelConstants[224][4]{};pixelConstants[2][0]=.05f;pixelConstants[2][1]=.1f;pixelConstants[2][2]=.2f;must(d->SetPixelShaderConstantF(0,pixelConstants[0],224));must(d->SetTexture(0,texture));
        for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(0,key,D3DTEXF_POINT));must(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));must(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        auto render=[&](bool patched,bool zero){float color[4];std::memcpy(color,constants[17],16);if(zero)color[0]=color[1]=color[2]=0;must(d->SetVertexShaderConstantF(17,color,1));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());auto submit=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));};const bool cold=patched&&!renderer.coldPairs;const auto start=std::chrono::steady_clock::now();
            float bank[44];must(d->GetVertexShaderConstantF(17,bank,11));const unsigned count=two?2:1;bool ownershipHit=false;
            const auto& ownership=ownershipMemo.resolve(bank,count,light.inverseView,light.inverseView+12,&ownBinding,1,ownershipHit);ownershipHits+=ownershipHit;
            const bool owns=ownership.bindings[0]==0;
            must(patched&&owns?renderer.draw(light,boundVS,ps,submit,nullptr,nullptr,&descriptor):submit());if(cold)coldMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();must(d->EndScene());if(ms)must(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));return pixels(d,resolved,Size);};
        fillCube(0);native=render(false,false);const auto clear=render(true,false);assert(renderer.draws>0&&renderer.coldPairs==1);
        assert(native==clear);assert(ownershipHits>0);
        if(!strength){must(d->SetVertexShader(vs));assert(render(false,false)==native);must(d->SetVertexShader(boundVS));}
        float afterBank[256][4];must(d->GetVertexShaderConstantF(0,afterBank[0],256));assert(!std::memcmp(afterBank,constants,sizeof constants)); // Includes nonidentity projection clipping and all MSAA edge pixels.
        fillCube(1);const auto without=render(false,true);blocked=render(true,false);
        const auto owned=authored;authored.diffuse[1]+=.02f;ownershipMemo.reset();assert(render(true,false)==native);authored=owned;ownershipMemo.reset();authored.attenuationEnd+=1;ownershipMemo.reset();assert(render(true,false)==native);authored=owned;ownershipMemo.reset();authored.position[0]+=.02f;ownershipMemo.reset();assert(render(true,false)==native);authored=owned;ownershipMemo.reset();
        light.gain=0;const auto gain0=render(true,false);assert(gain0==native);light.gain=.5f;const auto fade=render(true,false);light.gain=1;
        float removed=0;for(unsigned i=0;i<native.size();i+=4)removed=std::max(removed,native[i]-without[i]);assert(removed>.03f); // Never accept an empty/cutout/transparent fixture.
        for(unsigned y=80;y<176;++y)for(unsigned x=80;x<176;++x)for(unsigned channel=0;channel<4;++channel){unsigned i=(y*Size+x)*4+channel;assert(fade[i]>=std::min(native[i],blocked[i])-.001f&&fade[i]<=std::max(native[i],blocked[i])+.001f);const float error=std::max(std::fabs(native[i]-clear[i]),std::fabs(without[i]-blocked[i]));worst=std::max(worst,error);if(error>.001f){std::printf("ENDPOINT_FAIL ms=%u mode=%u fog=%g x=%u y=%u c=%u native=%g clear=%g without=%g blocked=%g\n",ms,mode,fog,x,y,channel,native[i],clear[i],without[i],blocked[i]);std::abort();}}
        fillCube(2);edge=render(true,false);for(unsigned y=80;y<176;++y)for(unsigned x=80;x<176;++x)for(unsigned k=0;k<4;++k){unsigned i=(y*Size+x)*4+k;assert(edge[i]>=std::min(native[i],without[i])-.001f&&edge[i]<=std::max(native[i],without[i])+.001f);}
        if(!ms&&!mode&&fog==1&&!context){fillCube(1);const auto zero=render(true,true);assert(zero==without);fixture={native,clear,blocked,edge,without,zero,gain0,fade};save(std::string(argv[4])+"/opaque-native.fgr",native,Size);save(std::string(argv[4])+"/opaque-blocked.fgr",blocked,Size);save(std::string(argv[4])+"/opaque-edge.fgr",edge,Size);}
        if(two){
            NorthlightNativeLocalLighting pair(d);NorthlightNativeLocalDraw other=light;other.slot=1;
            for(unsigned k=0;k<3;++k)other.position[k]=light.inverseView[12+k]-2*light.inverseView[k]+2*light.inverseView[4+k]+5*light.inverseView[8+k];
            fillCube(1);
            must(pair.draw(light,boundVS,ps,[]{return S_OK;},&other,nullptr,&descriptor));assert(pair.draws==1&&!pair.multiDraws&&pair.coldPairs==1);++light.rendererFrame;
            for(unsigned mask=0;mask<(singleOnly?2u:4u);++mask){std::vector<float> images[2];for(unsigned patched=0;patched<2;++patched){
                float colors[2][4];std::memcpy(colors,constants[17],sizeof colors);if(!patched)for(unsigned s=0;s<2;++s)if(mask&(1u<<s))colors[s][0]=colors[s][1]=colors[s][2]=0;
                must(d->SetVertexShaderConstantF(17,colors[0],2));light.gain=(mask&1)?1.f:0.f;other.gain=(mask&2)?1.f:0.f;
                must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());auto submitPair=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));};
                must(patched?pair.draw(light,boundVS,ps,submitPair,&other,nullptr,&descriptor):submitPair());must(d->EndScene());if(ms)must(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));images[patched]=pixels(d,resolved,Size);
            }
            if(mask)assert(singleOnly?(!pair.multiDraws&&pair.coldPairs==2):pair.multiDraws>0);if(!mask)assert(images[0]==images[1]);for(unsigned y=80;y<176;++y)for(unsigned x=80;x<176;++x)for(unsigned k=0;k<4;++k){unsigned i=(y*Size+x)*4+k;assert(std::fabs(images[0][i]-images[1][i])<=.001f);}}
            // Identical physical sources, opposite native slot order after a camera update.
            std::vector<float> permutations[2];fillCube(2);
            for(unsigned permutation=0;permutation<2;++permutation){float bank[11][4];std::memcpy(bank,constants[17],sizeof bank);
                if(permutation){for(unsigned k=0;k<4;++k){std::swap(bank[0][k],bank[1][k]);std::swap(bank[4][k],bank[5][k]);}for(unsigned k=8;k<11;++k)std::swap(bank[k][0],bank[k][1]);}
                must(d->SetVertexShaderConstantF(17,bank[0],11));auto first=permutation?other:light,second=permutation?light:other;first.slot=0;second.slot=1;first.gain=permutation?.35f:1.f;second.gain=permutation?1.f:.35f;
                must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());must(pair.draw(first,boundVS,ps,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));},&second,nullptr,&descriptor));must(d->EndScene());if(ms)must(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));permutations[permutation]=pixels(d,resolved,Size);
            }
            if(!singleOnly){float maximum=0;for(size_t i=0;i<permutations[0].size();++i)maximum=std::max(maximum,std::fabs(permutations[0][i]-permutations[1][i]));if(maximum>.002f){std::printf("PERMUTATION_FAIL error=%g\n",maximum);std::abort();}}
            if(!ms&&!mode&&fog==1&&!context&&strength==1.5f){save(std::string(argv[4])+"/permutation0.fgr",permutations[0],Size);save(std::string(argv[4])+"/permutation1.fgr",permutations[1],Size);}
            must(d->SetVertexShaderConstantF(17,constants[17],11));
            light.gain=1;must(d->SetVertexShaderConstantF(17,constants[17],2));
        }
        ++cases;
    }
    target->Release();}
    assert(cases>=6&&renderer.coldPairs==1);save(std::string(argv[4])+"/native.fgr",native,Size);save(std::string(argv[4])+"/blocked.fgr",blocked,Size);save(std::string(argv[4])+"/edge.fgr",edge,Size);
    must(d->SetRenderTarget(0,resolved));must(d->SetVertexShader(boundVS));must(d->SetPixelShader(ps));
    FaultDevice fault(d);fault.originalVS=boundVS;fault.originalPS=ps;NorthlightNativeLocalLighting checked(&fault);
    float savedVS[256][4],savedPS[224][4];must(d->GetVertexShaderConstantF(0,savedVS[0],256));must(d->GetPixelShaderConstantF(0,savedPS[0],224));
    auto checkState=[&]{int integers[16][4];must(d->GetPixelShaderConstantI(0,integers[0],16));assert(!std::memcmp(integers,integerBank,sizeof integers));float vv[256][4],pp[224][4];must(d->GetVertexShaderConstantF(0,vv[0],256));must(d->GetPixelShaderConstantF(0,pp[0],224));assert(!std::memcmp(vv,savedVS,sizeof vv)&&!std::memcmp(pp,savedPS,sizeof pp));IDirect3DVertexShader9* vcheck=nullptr;IDirect3DPixelShader9* pcheck=nullptr;IDirect3DBaseTexture9* tcheck=nullptr;must(d->GetVertexShader(&vcheck));must(d->GetPixelShader(&pcheck));must(d->GetTexture(heightSampler,&tcheck));assert(vcheck==boundVS&&pcheck==ps&&tcheck==height);vcheck->Release();pcheck->Release();tcheck->Release();};
    auto submit=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));};
    must(d->BeginScene());must(checked.draw(light,boundVS,ps,submit,nullptr,nullptr,&descriptor));must(d->EndScene());assert(checked.draws==1);checkState();const unsigned setters=fault.sets;
    for(unsigned fail=1;fail<=setters;++fail){fault.sets=0;fault.failAt=fail;unsigned submits=0;must(d->BeginScene());must(checked.draw(light,boundVS,ps,[&]{++submits;return submit();},nullptr,nullptr,&descriptor));must(d->EndScene());assert(submits==1);fault.failAt=0;checkState();}
    auto invalid=descriptor;invalid.originalVS=nullptr;unsigned fallback=0;must(checked.draw(light,boundVS,ps,[&]{++fallback;return S_OK;},nullptr,nullptr,&invalid));assert(fallback==1);checkState();
    checked.reset();const auto draws=checked.draws;must(checked.draw(light,boundVS,ps,[]{return S_OK;},nullptr,nullptr,&descriptor));assert(checked.draws==draws);++light.rendererFrame;must(checked.draw(light,boundVS,ps,[]{return S_OK;},nullptr,nullptr,&descriptor));assert(checked.draws==draws+1);checkState();
    std::printf("NATIVE_DISPLACEMENT_STATE_PASS setterFailures=%u fullVertexPixelBanks=exact heightTexture=preserved privatePairMismatch=fallback resetColdBudget=preserved gpu=%u\n",setters,unsigned(gpu));
    std::printf("NATIVE_DISPLACEMENT_PASS cases=%u endpointMaxError=%g fullyLit=bitexact materialGeometry=preserved physicalSlotPermutation=stable threeSourcePatch=rejected\n",cases,worst);
#ifndef NORTHLIGHT_NATIVE_DUPLICATE_BASELINE
    if(std::getenv("NORTHLIGHT_TEST_SHARING_FAULTS"))nativeLocalCreationFallback(d,light,boundVS,ps,two,nullptr,&descriptor);
#endif
    if(std::getenv("NORTHLIGHT_TEST_SAMPLER_BULK_MODE"))samplerDevice.verify(light,boundVS,ps,two?2:1,nullptr,&descriptor);
    return 0;
}
