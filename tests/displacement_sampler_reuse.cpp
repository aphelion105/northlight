#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include "forwarders.h"
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char*,...){}
#include "quality_settings.h"
#include "auto_displacement_renderer.h"
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
struct SamplerMeter:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    bool recording=false,failTexture=false;int failKey=-1;unsigned gets=0,sets=0;
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD s,D3DSAMPLERSTATETYPE k,DWORD* out)override{if(recording)++gets;return real->GetSamplerState(s,k,out);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD s,D3DSAMPLERSTATETYPE k,DWORD value)override{if(recording)++sets;auto hr=real->SetSamplerState(s,k,value);if(recording&&int(k)==failKey){failKey=-1;return E_FAIL;}return hr;}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD s,IDirect3DBaseTexture9* value)override{auto hr=real->SetTexture(s,value);if(recording&&failTexture){failTexture=false;return E_FAIL;}return hr;}
};
struct DisplacementStateTestAccess {
    static void run(IDirect3DDevice9* real){
        constexpr DWORD stage=D3DVERTEXTEXTURESAMPLER0;const D3DSAMPLERSTATETYPE keys[]={D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_SRGBTEXTURE};
        const DWORD wanted[]={D3DTEXF_LINEAR,D3DTEXF_LINEAR,D3DTEXF_NONE,D3DTADDRESS_WRAP,D3DTADDRESS_CLAMP,FALSE},other[]={D3DTEXF_POINT,D3DTEXF_POINT,D3DTEXF_POINT,D3DTADDRESS_CLAMP,D3DTADDRESS_WRAP,TRUE};
        const DWORD code[]={0xfffe0300,0x0200001f,0x80000000,0x900f0000,0x0200001f,0x80000000,0xe00f0000,0x02000001,0xe00f0000,0x90e40000,0xffff};
        IDirect3DVertexShader9* original=nullptr;ok(real->CreateVertexShader(code,&original));ok(real->SetVertexShader(original));
        IDirect3DTexture9* oldTexture=nullptr;ok(real->CreateTexture(1,1,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&oldTexture,nullptr));ok(real->SetTexture(stage,oldTexture));
        unsigned cases=0;unsigned normalSetters=0;
        for(unsigned mask=0;mask<64;++mask)for(int failure=-2;failure<6;++failure){if(failure>=0&&!(mask&(1u<<failure)))continue;
            SamplerMeter d(real);NorthlightAutoDisplacement g(&d);NorthlightDisplacementStorage::Entry entry;
            entry.domain=1;entry.vertices=3;entry.primitives=1;entry.stride=12;entry.address=D3DTADDRESS_WRAP|(D3DTADDRESS_CLAMP<<8);
            ok(real->CreateTexture(1,1,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&entry.height,nullptr));ok(real->CreateVertexBuffer(36,0,0,D3DPOOL_MANAGED,&entry.vb,nullptr));ok(real->CreateIndexBuffer(6,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&entry.ib,nullptr));
            const D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};ok(real->CreateVertexDeclaration(decl,&entry.decl));
            auto& shader=g.shaders[original];shader.patched=original;original->AddRef();ok(real->CreateVertexShader(code,&shader.gpu));shader.gpuBinding={};shader.gpuBinding.constant=10;shader.gpuBinding.count=2;g.current=&entry;
            DWORD initial[6];for(unsigned i=0;i<6;++i){initial[i]=mask&(1u<<i)?other[i]:wanted[i];ok(real->SetSamplerState(stage,keys[i],initial[i]));}
            float saved[8]={1,2,3,4,5,6,7,8};ok(real->SetVertexShaderConstantF(10,saved,2));d.failTexture=failure==-2;d.failKey=failure>=0?int(keys[failure]):-1;d.recording=true;
            unsigned fallback=0,submitted=0;
            const auto hr=g.drawBound([&]{++fallback;return D3D_OK;},false,[&](unsigned,unsigned,IDirect3DVertexShader9*){++submitted;for(unsigned i=0;i<6;++i){DWORD value=0;ok(real->GetSamplerState(stage,keys[i],&value));assert(value==wanted[i]);}return D3D_OK;});ok(hr);d.recording=false;
            assert(d.gets==6&&fallback+submitted==1&&(failure==-1?submitted==1:fallback==1));
            for(unsigned i=0;i<6;++i){DWORD value=0;ok(real->GetSamplerState(stage,keys[i],&value));assert(value==initial[i]);}
            IDirect3DBaseTexture9* texture=nullptr;ok(real->GetTexture(stage,&texture));assert(texture==oldTexture);texture->Release();float after[8];ok(real->GetVertexShaderConstantF(10,after,2));assert(!std::memcmp(saved,after,sizeof saved));
            IDirect3DVertexShader9* vs=nullptr;ok(real->GetVertexShader(&vs));assert(vs==original);vs->Release();
            const unsigned attempted=failure==-2?0u:unsigned(__builtin_popcount(mask&(failure<0?63u:(1u<<(failure+1))-1)));
            assert(d.sets==2*attempted);if(failure==-1)normalSetters+=d.sets;++cases;
        }
        ok(real->SetTexture(stage,nullptr));oldTexture->Release();original->Release();
        assert(normalSetters==384);std::printf("DISPLACEMENT_SAMPLER_PASS cases=%u masks=64 normalSetters=%u referenceNormalSetters=768 failureAfterMutation=restored textureConstantsShader=restored\n",cases,normalSetters);
    }
};
int main(int argc,char** argv){assert(argc==2);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DisplacementSampler";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
    auto dll=LoadLibraryA(argv[1]);assert(dll);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(dll,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));DisplacementStateTestAccess::run(d);d->Release();api->Release();FreeLibrary(dll);DestroyWindow(hwnd);}
