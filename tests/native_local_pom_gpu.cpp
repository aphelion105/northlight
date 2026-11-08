// Real client VS/PS programs: single-source endpoints preserve native material,
// saturation, fog, alpha test, blend and MSAA. No WoW execution.
#include <windows.h>
#include <d3d9.h>
#include "native_local_light_renderer.h"
#include "forwarders.h"
#include "native_local_sampler_read_checks.h"
#include "upload_lock.h"
template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char*,...){}
#include "shared_pom.h"
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
struct FaultDevice:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    unsigned sets=0,failAt=0;bool failPrivatePS=false,failOriginalVS=false;IDirect3DVertexShader9* originalVS=nullptr;IDirect3DPixelShader9* originalPS=nullptr;
    bool fail(){return ++sets==failAt;}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* p)override{if(fail()||(failOriginalVS&&p==originalVS))return E_FAIL;return real->SetVertexShader(p);}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* p)override{if(fail())return E_FAIL;if(failPrivatePS&&p!=originalPS){failPrivatePS=false;return E_FAIL;}return real->SetPixelShader(p);}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT first,const float* p,UINT n)override{if(fail())return E_FAIL;return real->SetPixelShaderConstantF(first,p,n);}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture9* p)override{if(fail())return E_FAIL;return real->SetTexture(stage,p);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD stage,D3DSAMPLERSTATETYPE key,DWORD value)override{if(fail())return E_FAIL;return real->SetSamplerState(stage,key,value);}
};
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    assert(argc==7);constexpr unsigned Size=256;const bool wmo=std::strcmp(argv[5],"wmo")==0;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightNativeLocalTest";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,Size,Size,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=Size;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    NativeSamplerReadDevice samplerDevice(d);d=&samplerDevice;
    auto v=words(argv[2]),p=words(argv[3]);IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;must(d->CreateVertexShader(v.data(),&vs));must(d->CreatePixelShader(p.data(),&ps));
    unsigned slots[]={0,1};std::vector<uint32_t> negative;NorthlightNativeLocalPatch::VertexInfo vi;const bool two=NorthlightEquipmentLightProof::analyze(reinterpret_cast<const uint32_t*>(v.data()),v.size())>=2;assert(NorthlightNativeLocalPatch::vertex(reinterpret_cast<const uint32_t*>(v.data()),v.size(),slots,2,negative,vi)==two);
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,16,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,28,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,40,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,6},D3DDECL_END()};IDirect3DVertexDeclaration9* declaration=nullptr;must(d->CreateVertexDeclaration(elements,&declaration));
    struct Vertex {float x,y,z;DWORD index;float nx,ny,nz;DWORD color;float u,v;float primitive=0;};const Vertex plane[]={{-8,8,8,0,0,0,-1,0xb3ffffff,0,0},{8,8,8,0,0,0,-1,0xb3ffffff,1,0},{-8,-8,8,0,0,0,-1,0xb3ffffff,0,1},{8,-8,8,0,0,0,-1,0xb3ffffff,1,1}};
    IDirect3DTexture9* texture=nullptr;must(d->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));D3DLOCKED_RECT lock;must(texture->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=((x+y)%2?0xff000000:0x00000000)|0x00b39980;must(texture->UnlockRect(0));
    IDirect3DCubeTexture9* cube=nullptr;must(d->CreateCubeTexture(256,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&cube,nullptr));
    auto fillCube=[&](unsigned mode){for(unsigned face=0;face<6;++face){D3DLOCKED_RECT l;must(cube->LockRect(D3DCUBEMAP_FACES(face),0,&l,nullptr,0));for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)reinterpret_cast<float*>(static_cast<char*>(l.pBits)+y*l.Pitch)[x]=mode==0?1.f:mode==1?0.f:x<128?1.f:0.f;must(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}};
    NorthlightNativeLocalDraw light;light.cube=cube;light.position[2]=4;light.range=12;for(unsigned i=0;i<16;++i)light.inverseView[i]=i%5==0?1.f:0;
    IDirect3DSurface9* resolved=nullptr;must(d->CreateRenderTarget(Size,Size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
    const unsigned context=0;const float fog=1;
    must(d->SetRenderTarget(0,resolved));must(d->SetDepthStencilSurface(nullptr));D3DVIEWPORT9 viewport{0,0,Size,Size,0,1};must(d->SetViewport(&viewport));must(d->SetVertexDeclaration(declaration));
        const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={{D3DRS_ZENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FOGENABLE,FALSE},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_ALPHABLENDENABLE,false},{D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},{D3DRS_BLENDOP,D3DBLENDOP_ADD},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_ALPHATESTENABLE,false},{D3DRS_ALPHAFUNC,D3DCMP_GREATER},{D3DRS_ALPHAREF,128},{D3DRS_MULTISAMPLEANTIALIAS,TRUE}};for(auto state:states)must(d->SetRenderState(state.first,state.second));
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
        must(d->SetVertexShaderConstantF(0,constants[0],256));float pixelConstants[224][4]{};pixelConstants[2][0]=.05f;pixelConstants[2][1]=.1f;pixelConstants[2][2]=.2f;must(d->SetPixelShaderConstantF(0,pixelConstants[0],224));must(d->SetTexture(0,texture));
        for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(0,key,D3DTEXF_POINT));must(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));must(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));

    IDirect3DTexture9* height=nullptr;must(d->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&height,nullptr));
    must(height->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=0xff808080u+(x%2?0x00303030u:0);must(height->UnlockRect(0));
    IDirect3DTexture9* cached=nullptr;must(d->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&cached,nullptr));
    must(cached->LockRect(0,&lock,nullptr,0));const float cachedUV[]={.3f,.6f,7,1};std::memcpy(lock.pBits,cachedUV,16);must(cached->UnlockRect(0));
    std::vector<float> sharedMiss;
    for(unsigned variant=0;variant<3;++variant){const bool shared=variant!=0;
        std::vector<uint32_t> fw,pw,tmp;unsigned control=0;
        assert(!NorthlightWaterShaderPatch::patch(reinterpret_cast<const uint32_t*>(v.data()),v.size(),fw));
        if(shared){assert(NorthlightSharedPOM::vertex(reinterpret_cast<const DWORD*>(fw.data()),fw.size(),tmp));fw.swap(tmp);}
        assert(NorthlightParallaxPatch::pixel(reinterpret_cast<const uint32_t*>(p.data()),p.size(),false,pw,&control,shared));
        IDirect3DVertexShader9* pvs=nullptr;IDirect3DPixelShader9* pps=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(fw.data()),&pvs));must(d->CreatePixelShader(reinterpret_cast<const DWORD*>(pw.data()),&pps));
        NorthlightNativePOM pom{vs,pvs,ps,pps,control,shared?3u:2u,false,bool(shared),&NorthlightSharedPOM::vertex};
        {std::vector<uint32_t> nv,np,nf,nn;NorthlightNativeLocalPatch::VertexInfo vi;NorthlightNativeLocalPatch::PixelInfo pi;unsigned slot=0;
        const bool a=NorthlightNativeLocalPatch::vertex(reinterpret_cast<const uint32_t*>(v.data()),v.size(),&slot,1,nv,vi,0xc2u);
        const bool b=a&&NorthlightNativeLocalPatch::pixel(reinterpret_cast<const uint32_t*>(p.data()),p.size(),vi,reinterpret_cast<const uint32_t*>(kNativeLocalLighting1Shader),sizeof(kNativeLocalLighting1Shader)/4,np,pi);
        const char* c=a?NorthlightWaterShaderPatch::patch(nv.data(),nv.size(),nf):"native VS";unsigned pc=0;const bool e=b&&NorthlightParallaxPatch::pixel(np.data(),np.size(),false,nn,&pc,shared,nullptr,512);
        std::printf("COMPOSE_STAGE vs=%u ps=%u forward=%s pom=%u nativeWords=%zu pomWords=%zu\n",a,b,c?c:"ok",e,np.size(),nn.size());
        }

#ifndef NORTHLIGHT_NATIVE_DUPLICATE_BASELINE
    if(two&&std::getenv("NORTHLIGHT_TEST_PIXEL_REUSE"))nativeLocalPixelReuse(d,light,pvs,pps,&pom);
    if(two&&std::getenv("NORTHLIGHT_TEST_VERTEX_REUSE"))nativeLocalVertexReuse(d,light,pvs,pps,&pom);
#endif
    NorthlightNativeLocalLighting composed(d);samplerDevice.attach(composed);must(d->SetVertexShader(pvs));must(d->SetPixelShader(pps));
        float controls[12]={1,1,64.f/63.9f,-.1f*64.f/63.9f,.08f,64,16,0,1.f/Size,1.f/Size,7,0};
        must(d->SetPixelShaderConstantF(control,controls,pom.count));must(d->SetTexture(14,height));must(d->SetTexture(12,variant==2?cached:height));must(d->SetTexture(13,variant==2?cached:height));
        for(unsigned sampler:{12u,13u,14u}){must(d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT));must(d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT));must(d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE));}
        auto render=[&](bool patched,bool zero){float color[4];std::memcpy(color,constants[17],16);if(zero)color[0]=color[1]=color[2]=0;must(d->SetVertexShaderConstantF(17,color,1));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());
            auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));};must(patched?composed.draw(light,pvs,pps,draw,nullptr,&pom):draw());must(d->EndScene());return pixels(d,resolved,Size);};
        fillCube(0);const auto native=render(false,false),clear=render(true,false);
        std::printf("POM_ATTEMPT shared=%u corrected=%llu fallback=%llu\n",shared,(unsigned long long)composed.draws,(unsigned long long)composed.fallbacks);
        assert(composed.draws==1&&native==clear);if(variant==1)sharedMiss=native;if(variant==2)assert(native!=sharedMiss);
        if(!shared){controls[4]=0;must(d->SetPixelShaderConstantF(control,controls,pom.count));const auto flat=render(false,false);assert(flat!=native);controls[4]=.08f;must(d->SetPixelShaderConstantF(control,controls,pom.count));}
        fillCube(1);const auto zero=render(false,true),blocked=render(true,false);float removed=0,error=0;
        for(unsigned i=0;i<native.size();i+=4)removed=std::max(removed,native[i]-blocked[i]);
        for(unsigned y=80;y<176;++y)for(unsigned x=80;x<176;++x)for(unsigned k=0;k<4;++k){unsigned i=(y*Size+x)*4+k;error=std::max(error,std::fabs(zero[i]-blocked[i]));}
        assert(removed>.03f&&error<=.001f);
        IDirect3DVertexShader9* afterVS=nullptr;IDirect3DPixelShader9* afterPS=nullptr;float after[12]{};must(d->GetVertexShader(&afterVS));must(d->GetPixelShader(&afterPS));must(d->GetPixelShaderConstantF(control,after,pom.count));assert(afterVS==pvs&&afterPS==pps&&!std::memcmp(after,controls,pom.count*16));drop(afterVS);drop(afterPS);
        auto invalid=pom;invalid.vs=vs;unsigned calls=0;const auto before=composed.draws;must(composed.draw(light,pvs,pps,[&]{++calls;return S_OK;},nullptr,&invalid));assert(calls==1&&composed.draws==before);
#ifndef NORTHLIGHT_NATIVE_DUPLICATE_BASELINE
        if(std::getenv("NORTHLIGHT_TEST_SHARING_FAULTS"))nativeLocalCreationFallback(d,light,pvs,pps,two,&pom);
#endif
        FaultDevice faulty(d);faulty.originalVS=pvs;faulty.originalPS=pps;NorthlightNativeLocalLighting checked(&faulty);
        must(checked.draw(light,pvs,pps,[]{return S_OK;},nullptr,&pom));const auto setters=faulty.sets;float savedBank[224][4];must(d->GetPixelShaderConstantF(0,savedBank[0],224));
        for(unsigned fail=1;fail<=setters;++fail){faulty.sets=0;faulty.failAt=fail;unsigned submitted=0;must(checked.draw(light,pvs,pps,[&]{++submitted;return S_OK;},nullptr,&pom));assert(submitted==1);faulty.failAt=0;
            float bank[224][4];must(d->GetPixelShaderConstantF(0,bank[0],224));must(d->GetVertexShader(&afterVS));must(d->GetPixelShader(&afterPS));assert(afterVS==pvs&&afterPS==pps&&!std::memcmp(bank,savedBank,sizeof bank));drop(afterVS);drop(afterPS);}
        if(std::getenv("NORTHLIGHT_TEST_SAMPLER_BULK_MODE"))samplerDevice.verify(light,pvs,pps,two?2:1,&pom);
    composed.reset();++light.rendererFrame;fillCube(0);assert(render(true,false)==native);
        std::printf("NATIVE_POM_GPU_PASS variant=%u fullyLit=bitexact shadowError=%g removed=%g state=restored mismatchedPair=unchanged reset=exact faultSetters=%u\n",variant,error,removed,setters);
        if(!shared){auto large=p;for(unsigned i=0;i<160;++i)large.insert(large.end()-1,{0x02000001u,0x800f001eu,0x90e40000u});
            std::vector<uint32_t> material;unsigned base=0;assert(NorthlightParallaxPatch::pixel(reinterpret_cast<const uint32_t*>(large.data()),large.size(),false,material,&base));
            IDirect3DPixelShader9 *source=nullptr,*bound=nullptr;must(d->CreatePixelShader(large.data(),&source));must(d->CreatePixelShader(reinterpret_cast<const DWORD*>(material.data()),&bound));must(d->SetPixelShader(bound));
            auto unsupported=pom;unsupported.originalPS=source;unsupported.ps=bound;unsupported.sourceControl=base;must(d->SetPixelShaderConstantF(base,controls,2));
            NorthlightNativeLocalLighting capped(d);unsigned submissions=0;for(unsigned retry=0;retry<2;++retry)must(capped.draw(light,pvs,bound,[&]{++submissions;return S_OK;},nullptr,&unsupported));
            assert(submissions==2&&!capped.draws&&capped.fallbacks==2&&capped.coldPairs==1);must(d->GetPixelShader(&afterPS));assert(afterPS==bound);drop(afterPS);
            std::puts("NATIVE_POM_LIMIT_PASS oversized=unchanged materialPreserved=1 negativeCache=1");must(d->SetPixelShader(pps));drop(source);drop(bound);
        }
        must(d->SetVertexShader(vs));must(d->SetPixelShader(ps));drop(pvs);drop(pps);
    }
    for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);cached->Release();height->Release();cube->Release();texture->Release();declaration->Release();vs->Release();ps->Release();resolved->Release();d->Release();api->Release();FreeLibrary(library);DestroyWindow(hwnd);
}
