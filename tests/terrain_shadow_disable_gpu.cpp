#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "patch_terrain_shadow.h"
#include "water_shader_patch.h"
#include "auto_parallax_patch.h"
using Word=std::uint32_t;
static void check(HRESULT hr,int line){if(FAILED(hr)){std::printf("line=%d HRESULT=%08lx\n",line,(unsigned long)hr);std::abort();}}
#define ok(call) check(call,__LINE__)
static std::vector<Word> read(const std::string& path){std::ifstream in(path,std::ios::binary);assert(in);std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});assert(bytes.size()%4==0);std::vector<Word> code(bytes.size()/4);std::memcpy(code.data(),bytes.data(),bytes.size());return code;}
static Word reg(unsigned type,unsigned index,unsigned mask=15){return 0x80000000u|((type&7)<<28)|((type&24)<<8)|index|(mask<<16);}
static Word source(unsigned type,unsigned index,unsigned swizzle=0xe4){return (reg(type,index)&~0xf0000u)|(swizzle<<16);}
int main(int argc,char** argv){
    assert(argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);const std::string dir=argv[2];
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightTerrainShadowOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    // Validate every successful stock terrain transform with the real driver.
    std::ifstream list(dir+"programs.txt");assert(list);unsigned created=0;std::string name;
    while(std::getline(list,name)){if(name.empty())continue;auto code=read(dir+name);std::vector<Word> patched;assert(NorthlightTerrainShadow::patch(code.data(),code.size(),patched));for(bool dual:{false,true})for(bool shared:{false,true}){
        std::vector<Word> before,after;const bool supported=NorthlightParallaxPatch::pixel(code.data(),code.size(),dual,before,nullptr,shared);
        if(supported)assert(NorthlightParallaxPatch::pixel(patched.data(),patched.size(),dual,after,nullptr,shared));
    }
    IDirect3DPixelShader9* ps=nullptr;const auto hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&ps);if(FAILED(hr)){IDirect3DPixelShader9* original=nullptr;const auto base=d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&original);std::printf("program=%s original=%08lx patched=%08lx\n",name.c_str(),(unsigned long)base,(unsigned long)hr);if(original)original->Release();}ok(hr);ps->Release();++created;}
    assert(created==392);
    const DWORD vertexCode[]={0xfffe0300u,
        0x0200001fu,0x80000000u,reg(1,0),0x0200001fu,0x80000005u,reg(1,1,3),
        0x0200001fu,0x80000000u,reg(6,0),0x0200001fu,0x8000000au,reg(6,1),0x0200001fu,0x8001000au,reg(6,2),
        0x0200001fu,0x80000005u,reg(6,3,3),0x0200001fu,0x80010005u,reg(6,4,3),0x0200001fu,0x8000000bu,reg(6,5,1),
        0x02000001u,reg(6,0),source(1,0),0x02000001u,reg(6,1),source(2,10),0x02000001u,reg(6,2),source(2,11),
        0x02000001u,reg(6,3,3),source(1,1),0x02000001u,reg(6,4,3),source(1,1),0x02000001u,reg(6,5,1),source(2,12,0),65535u};
    IDirect3DVertexShader9* vs=nullptr;ok(d->CreateVertexShader(vertexCode,&vs));
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};IDirect3DVertexDeclaration9* decl=nullptr;ok(d->CreateVertexDeclaration(elements,&decl));
    ok(d->SetVertexShader(vs));ok(d->SetVertexDeclaration(decl));
    const float lighting[][4]={{.65f,.75f,.85f,1},{.10f,.05f,.02f,1},{.67f,0,0,0}};ok(d->SetVertexShaderConstantF(10,lighting[0],3));
    const float weights[]={.2f,.3f,.4f,0},fog[]={.08f,.12f,.18f,0};ok(d->SetPixelShaderConstantF(0,weights,1));ok(d->SetPixelShaderConstantF(2,fog,1));
    constexpr unsigned W=64,H=64;IDirect3DTexture9* textures[3]={};
    for(unsigned t=0;t<3;++t){ok(d->CreateTexture(W,H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&textures[t],nullptr));D3DLOCKED_RECT lock{};ok(textures[t]->LockRect(0,&lock,nullptr,0));
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const unsigned a=t==2?255:t==1?(x*13+y*7)%256:70+(x*3+y)%100;
            const DWORD rgb=t==0?((60+x*2)<<16)|((80+y*2)<<8)|(40+(x+y)%150):(((x*3+y)%256)<<16)|(((y*3+x)%256)<<8)|((x*5+y*7)%256);
            reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=rgb|(a<<24);}
        ok(textures[t]->UnlockRect(0));}
    for(unsigned slot=0;slot<2;++slot){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,state,D3DTEXF_POINT));ok(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(slot,state,D3DTADDRESS_CLAMP));}
    ok(d->SetTexture(0,textures[0]));ok(d->SetDepthStencilSurface(nullptr));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    struct Vertex {float x,y,z,w,u,v;};const Vertex quad[]={{-1,1,.5f,1,0,0},{1,1,.5f,1,1,0},{-1,-1,.5f,1,0,1},{1,-1,.5f,1,1,1}};
    unsigned cases=0;
    for(auto format:{D3DFMT_A8R8G8B8,D3DFMT_A16B16G16R16F}){
        IDirect3DTexture9* output=nullptr;IDirect3DSurface9 *surface=nullptr,*system=nullptr;ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&output,nullptr));ok(output->GetSurfaceLevel(0,&surface));ok(d->CreateOffscreenPlainSurface(W,H,format,D3DPOOL_SYSTEMMEM,&system,nullptr));ok(d->SetRenderTarget(0,surface));D3DVIEWPORT9 viewport{0,0,W,H,0,1};ok(d->SetViewport(&viewport));
        auto image=[&](IDirect3DPixelShader9* ps,unsigned texture){ok(d->SetPixelShader(ps));ok(d->SetTexture(1,textures[texture]));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff123456,1,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(surface,system));D3DLOCKED_RECT lock{};ok(system->LockRect(&lock,nullptr,D3DLOCK_READONLY));const unsigned bytes=format==D3DFMT_A8R8G8B8?4:8;std::vector<char> pixels(size_t(W)*H*bytes);for(unsigned y=0;y<H;++y)std::memcpy(pixels.data()+size_t(y)*W*bytes,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*bytes);ok(system->UnlockRect());return pixels;};
        for(const auto* file:{"terrain0-ps2.bin","terrain0-ps3.bin"}){
            auto code=read(dir+file);std::vector<Word> patched;assert(NorthlightTerrainShadow::patch(code.data(),code.size(),patched));IDirect3DPixelShader9 *original=nullptr,*suppressed=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&original));ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&suppressed));
            const auto reference=image(original,2),withShadow=image(original,1),result=image(suppressed,1);assert(result==reference&&withShadow!=reference);
            const unsigned stride=format==D3DFMT_A8R8G8B8?4:8,alpha=stride==4?3:6;for(size_t p=0;p<result.size();p+=stride)assert(!std::memcmp(result.data()+p+alpha,withShadow.data()+p+alpha,stride==4?1:2));
            original->Release();suppressed->Release();++cases;
        }
        system->Release();surface->Release();output->Release();
    }
    std::printf("TERRAIN_SHADOW_DISABLE_GPU_PASS programs=%u cases=%u SDR_HDR=1 shadow_removed=1 color_fog_specular_layers=exact alpha=exact\n",created,cases);
    return 0;
}
