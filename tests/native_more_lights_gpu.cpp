#include <windows.h>
#include <d3d9.h>
#ifdef NORTHLIGHT_MORE_LIGHTS_BASELINE_HEADER
#include NORTHLIGHT_MORE_LIGHTS_BASELINE_HEADER
#else
#include "native_more_light_renderer.h"
#endif
#include "forwarders.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <cmath>
using Kind=NorthlightNativeMoreLightShader::Kind;
static void must(HRESULT hr){if(FAILED(hr)){std::printf("HRESULT=%08lx\n",(unsigned long)hr);std::abort();}}
struct Mirror {float vsFloat[256][4]{};bool vsFloatKnown[256]{};bool enabled=false;bool heldByThisThread()const{return enabled;}bool active()const{return enabled;}};
struct FaultDevice:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    unsigned creates=0,sets=0,failAt=0,failAfterAt=0,failFrom=0,shaderReads=0,constantReads=0,shaderWrites=0,constantWrites=0;
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** out)override{++shaderReads;return real->GetVertexShader(out);}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* values,UINT count)override{++constantReads;return real->GetVertexShaderConstantF(first,values,count);}
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out)override{++creates;return real->CreateVertexShader(code,out);}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* shader)override{++shaderWrites;if(++sets==failAt||(failFrom&&sets>=failFrom))return E_FAIL;const HRESULT hr=real->SetVertexShader(shader);return sets==failAfterAt?E_FAIL:hr;}
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT first,const float* values,UINT count)override{++constantWrites;if(++sets==failAt||(failFrom&&sets>=failFrom))return E_FAIL;const HRESULT hr=real->SetVertexShaderConstantF(first,values,count);return sets==failAfterAt?E_FAIL:hr;}
};
static unsigned word(std::ifstream& f){unsigned v=0;f.read(reinterpret_cast<char*>(&v),4);assert(f);return v;}
struct Vertex {float inputs[16][4]{};};
static std::vector<float> image(IDirect3DDevice9* d,IDirect3DSurface9* target){
    IDirect3DSurface9* copy=nullptr;must(d->CreateOffscreenPlainSurface(32,32,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&copy,nullptr));
    must(d->GetRenderTargetData(target,copy));D3DLOCKED_RECT lock{};must(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<float> result(32*32*4);for(unsigned y=0;y<32;++y)std::memcpy(result.data()+y*128,static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch,128*sizeof(float));
    must(copy->UnlockRect());copy->Release();return result;
}
static void constants(float (&values)[256][4],bool terrain,unsigned first,unsigned count){
    std::memset(values,0,sizeof values);
    const unsigned projection=terrain?4:2;for(unsigned k=0;k<4;++k)values[projection+k][k]=1;
    for(unsigned k=0;k<3;++k)values[terrain?k:31+k][k]=1;
    values[terrain?24:12][2]=-1;
    if(terrain)values[3][3]=1;
    else{std::fill_n(values[28],4,1.f);values[30][1]=values[30][2]=1;}
    const auto light=[&](unsigned logical,float* position,float* color,float* attenuation){
        position[0]=float(int(logical)-4)*.08f;position[1]=.1f;position[2]=2;position[3]=1;
        color[0]=.007f*(logical+1);color[1]=.005f*(logical+1);color[2]=.003f*(logical+1);attenuation[0]=1;};
    if(terrain){for(unsigned i=0;i<3;++i){values[30+3*i][0]=1;if(i<count)light(first+i,values[28+3*i],values[29+3*i],values[30+3*i]);}
        for(unsigned i=0;i<5;++i){values[238+3*i][0]=1;light(i+3,values[236+3*i],values[237+3*i],values[238+3*i]);}}
    else{for(unsigned i=0;i<4;++i){float p[4]{},rgb[4]{},att[4]{};values[25][i]=1;
            if(i<count){light(first+i,p,rgb,att);std::copy_n(rgb,4,values[17+i]);std::copy_n(p,4,values[21+i]);}}
        for(unsigned i=0;i<4;++i){float p[4]{},rgb[4]{},att[4]{};light(i+4,p,rgb,att);std::copy_n(rgb,4,values[236+i]);std::copy_n(p,4,values[240+i]);values[244][i]=1;}}
}
static float compare(const std::vector<float>& a,const std::vector<float>& b){assert(a.size()==b.size());float error=0;for(unsigned i=0;i<a.size();++i){assert(std::isfinite(a[i])&&std::isfinite(b[i]));error=std::max(error,std::fabs(a[i]-b[i]));}assert(error<.0004f);return error;}
#ifdef NORTHLIGHT_MORE_LIGHTS_BENCHMARK
#include "native_more_lights_benchmark.h"
#endif
static float numeric(IDirect3DDevice9* d,IDirect3DSurface9* target,const std::vector<DWORD>& source,const std::vector<std::uint32_t>& expanded,bool terrain){
    IDirect3DVertexShader9 *original=nullptr,*eight=nullptr;must(d->CreateVertexShader(source.data(),&original));must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(expanded.data()),&eight));
    std::vector<D3DVERTEXELEMENT9> declaration;Vertex vertices[4]{};const float xy[4][2]={{-.8f,-.8f},{-.8f,.8f},{.8f,-.8f},{.8f,.8f}};
    for(size_t at=1;at<source.size()-1;){unsigned op=source[at]&65535,n=op==65534?(source[at]>>16)&32767:(source[at]>>24)&15;
        if(op==31&&n==2&&NorthlightNativeMoreLightShader::type(source[at+2])==1){unsigned reg=source[at+2]&2047,usage=source[at+1]&31,index=(source[at+1]>>16)&15;assert(reg<16);
            declaration.push_back({0,WORD(reg*16),D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,BYTE(usage),BYTE(index)});
            for(unsigned v=0;v<4;++v){auto* p=vertices[v].inputs[reg];
                if(usage==D3DDECLUSAGE_POSITION){p[0]=xy[v][0];p[1]=xy[v][1];p[2]=.5f;p[3]=1;}
                else if(usage==D3DDECLUSAGE_NORMAL)p[2]=1;
                else if(usage==D3DDECLUSAGE_COLOR){p[0]=p[1]=p[2]=.5f;p[3]=.75f;}
                else if(usage==D3DDECLUSAGE_BLENDWEIGHT)p[0]=1;
                else if(usage==D3DDECLUSAGE_TEXCOORD){p[0]=(xy[v][0]+1)*.5f;p[1]=(xy[v][1]+1)*.5f;}
            }}at+=n+1;}
    declaration.push_back(D3DDECL_END());IDirect3DVertexDeclaration9* decl=nullptr;must(d->CreateVertexDeclaration(declaration.data(),&decl));must(d->SetVertexDeclaration(decl));
    const DWORD psCode[]={0xffff0300,0x0200001f,0x8000000a,0x900f0000,0x02000001,0x800f0800,0x90e40000,0xffff};
    IDirect3DPixelShader9* ps=nullptr;must(d->CreatePixelShader(psCode,&ps));must(d->SetPixelShader(ps));
    must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));must(d->SetRenderState(D3DRS_ZENABLE,FALSE));must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    must(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));must(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));must(d->SetRenderState(D3DRS_FOGENABLE,FALSE));must(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));
    D3DVIEWPORT9 vp{0,0,32,32,0,1};must(d->SetViewport(&vp));
    const auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));};
    const auto render=[&](IDirect3DVertexShader9* shader,unsigned first,unsigned count){float values[256][4];constants(values,terrain,first,count);must(d->SetVertexShaderConstantF(0,values[0],256));must(d->SetVertexShader(shader));
        must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));must(d->BeginScene());must(draw());must(d->EndScene());return image(d,target);};
    const unsigned base=terrain?3:4;const auto zero=render(original,0,0),a=render(original,0,base),b=render(original,base,base),all=render(eight,0,base);
    std::vector<float> expected(a.size());for(unsigned i=0;i<a.size();++i)expected[i]=a[i]+b[i]-zero[i];
    if(terrain){const auto tail=render(original,6,2);for(unsigned i=0;i<expected.size();++i)expected[i]+=tail[i]-zero[i];}
    float maximum=0;for(unsigned i=0;i<all.size();++i)if(i%4!=3)maximum=std::max(maximum,all[i]);assert(maximum>.001f);
    float error=compare(all,expected);
    FaultDevice faults(d);NorthlightNativeMoreLightRenderer renderer(&faults);renderer.registerShader(original,source.data(),source.size(),terrain);assert(renderer.kind(original)==(terrain?Kind::Terrain:Kind::Model));
    Mirror mirror;const auto managed=[&](unsigned limit,bool handoff=false){renderer.limits(terrain?8:limit,terrain?limit:8);float values[256][4];constants(values,terrain,0,base);must(d->SetVertexShaderConstantF(0,values[0],256));must(d->SetVertexShader(original));
        must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));must(d->BeginScene());must(renderer.draw(renderer.kind(original),&mirror,[&](bool t,float* bank){if(handoff)for(unsigned k=0;k<3;++k)bank[(t?4:0)+k]=0;},draw));must(d->EndScene());
        float restored[256][4];must(d->GetVertexShaderConstantF(0,restored[0],256));assert(!std::memcmp(values,restored,sizeof values));IDirect3DVertexShader9* bound=nullptr;must(d->GetVertexShader(&bound));assert(bound==original);bound->Release();return image(d,target);};
    error=std::max(error,compare(managed(8),all));error=std::max(error,compare(managed(base),a));
    const auto partial=render(original,base,2);for(unsigned i=0;i<expected.size();++i)expected[i]=a[i]+partial[i]-zero[i];error=std::max(error,compare(managed(base+2),expected));
    const auto firstExtra=render(original,base,1);for(unsigned i=0;i<expected.size();++i)expected[i]=all[i]-firstExtra[i]+zero[i];error=std::max(error,compare(managed(8,true),expected));
    const unsigned creations=faults.creates;managed(8);managed(base);assert(faults.creates==creations);
    faults.sets=0;faults.failAt=1;compare(managed(8),a);assert(!renderer.restoreFailed);faults.failAt=0;
    faults.sets=0;faults.failAt=2;compare(managed(base+2),a);assert(!renderer.restoreFailed);faults.failAt=0;
#ifndef NORTHLIGHT_MORE_LIGHTS_BASELINE_HEADER
    // Unrelated shader registrations cannot evict a ready source variant.
    const DWORD unlit[]={0xfffe0300,0x0200001f,0x80000000,0x900f0000,0x0200001f,0x80000000,0xe00f0000,0x02000001,0xe00f0000,0x90e40000,0xffff};
    std::vector<IDirect3DVertexShader9*> unrelated;
    for(unsigned i=0;i<520;++i){IDirect3DVertexShader9* shader=nullptr;must(d->CreateVertexShader(unlit,&shader));unrelated.push_back(shader);renderer.registerShader(shader,unlit,std::size(unlit),false);assert(renderer.kind(shader)==Kind::None);}
    managed(8);assert(faults.creates==creations);for(auto* shader:unrelated)shader->Release();
    for(unsigned failure:{1u,2u}){faults.sets=0;faults.failAfterAt=failure;compare(managed(base+2),a);assert(!renderer.restoreFailed);faults.failAfterAt=0;}
    {float values[256][4];constants(values,terrain,0,base);renderer.limits(terrain?8:base+2,terrain?base+2:8);
     const auto setup=[&]{must(d->SetVertexShaderConstantF(0,values[0],256));must(d->SetVertexShader(original));};
     const auto restored=[&]{float current[256][4];must(d->GetVertexShaderConstantF(0,current[0],256));assert(!std::memcmp(values,current,sizeof values));IDirect3DVertexShader9* shader=nullptr;must(d->GetVertexShader(&shader));assert(shader==original);shader->Release();};
     setup();must(d->BeginScene());bool threw=false;try{renderer.draw(renderer.kind(original),&mirror,[](bool,float*){},[]()->HRESULT{throw std::bad_alloc();});}catch(const std::bad_alloc&){threw=true;}must(d->EndScene());assert(threw&&!renderer.restoreFailed);restored();
     setup();faults.sets=0;faults.failFrom=3;must(d->BeginScene());const HRESULT broken=renderer.draw(renderer.kind(original),&mirror,[](bool,float*){},draw);must(d->EndScene());assert(FAILED(broken)&&renderer.restoreFailed);
     unsigned submitted=0;assert(FAILED(renderer.draw(Kind::None,&mirror,[](bool,float*){},[&]{++submitted;return S_OK;}))&&!submitted);
     faults.failFrom=0;setup();renderer.deviceReset();assert(!renderer.restoreFailed);restored();}
    std::puts("MORE_LIGHTS_STATE_PASS unrelated520=warm-cache-preserved failedSetterAfterWrite=restored exception=restored persistentFailure=blocked reset=recovered");
#endif
#ifdef NORTHLIGHT_MORE_LIGHTS_BENCHMARK
    benchmarkMoreLights(d,target,source,original,eight,terrain,vertices,draw);
#endif
    original->Release();eight->Release();ps->Release();decl->Release();return error;
}
int main(int argc,char** argv){
    assert(argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);std::ifstream input(std::filesystem::u8path(argv[1]),std::ios::binary);assert(input);
    HMODULE dll=LoadLibraryW(std::filesystem::u8path(argv[2]).c_str());assert(dll);using Create=IDirect3D9*(WINAPI*)(UINT);auto create=reinterpret_cast<Create>(GetProcAddress(dll,"Direct3DCreate9"));assert(create);
    IDirect3D9* api=create(D3D_SDK_VERSION);assert(api);D3DADAPTER_IDENTIFIER9 adapter{};must(api->GetAdapterIdentifier(0,0,&adapter));
    std::printf("MORE_LIGHTS_ADAPTER vendor=%04lx device=%04lx name=%s driver=%s\n",(unsigned long)adapter.VendorId,(unsigned long)adapter.DeviceId,adapter.Description,adapter.Driver);
    HWND window=CreateWindowExW(0,L"STATIC",L"More lights offscreen",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    D3DPRESENT_PARAMETERS p{};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.BackBufferWidth=p.BackBufferHeight=32;p.BackBufferFormat=D3DFMT_A8R8G8B8;p.hDeviceWindow=window;
    IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&p,&d));
    IDirect3DSurface9* target=nullptr;must(d->CreateRenderTarget(32,32,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    unsigned accepted=0,existing=0,models=0,terrains=0,numericCases=0;bool done[2]{};float error=0;std::uint64_t digest=14695981039346656037ull;
    const unsigned variants=word(input);for(unsigned i=0;i<variants;++i){const bool terrain=word(input)!=0,numericCandidate=word(input)!=0;const unsigned count=word(input);assert(count>=2&&count<=4096);
        std::vector<DWORD> source(count);input.read(reinterpret_cast<char*>(source.data()),count*4);assert(input);std::vector<std::uint32_t> output;Kind kind;bool already=false;
        if(!NorthlightNativeMoreLightShader::transform(reinterpret_cast<const std::uint32_t*>(source.data()),source.size(),terrain,output,kind,already))continue;
        IDirect3DVertexShader9* shader=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(output.data()),&shader));shader->Release();++accepted;existing+=already;if(terrain)++terrains;else ++models;
        for(auto w:output)for(unsigned byte=0;byte<4;++byte)digest=(digest^((w>>(8*byte))&255))*1099511628211ull;
        std::vector<std::uint32_t> twice;Kind secondKind;bool secondAlready=false;assert(NorthlightNativeMoreLightShader::transform(output.data(),output.size(),terrain,twice,secondKind,secondAlready)&&secondAlready&&twice==output);
        if(numericCandidate&&!already&&!done[terrain]&&source[0]==0xfffe0300u){std::printf("numeric terrain=%u variant=%u\n",unsigned(terrain),i);error=std::max(error,numeric(d,target,source,output,terrain));done[terrain]=true;++numericCases;}
    }
    assert(accepted&&models&&terrains&&numericCases==2);target->Release();d->Release();api->Release();DestroyWindow(window);FreeLibrary(dll);
    std::printf("MORE_LIGHTS_PROGRAM_DIGEST %016llx\n",(unsigned long long)digest);
    std::printf("NATIVE_MORE_LIGHTS_GPU_PASS variants=%u accepted=%u models=%u terrain=%u existing=%u numeric=%u maxError=%.9g native4plus4=1 terrain3plus5=1 liveLimits=1 ownershipRemoval=1 constantsRestored=1 shaderRestored=1 warmShaderCreation=0 injectedSetFailures=2\n",variants,accepted,models,terrains,existing,numericCases,error);
}
