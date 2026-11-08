// Hidden-device acceptance of the actual diagnostic copy/state helper; no game.
#include <windows.h>
#include <d3d9.h>
#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
static void logf(const char* text,...){va_list args;va_start(args,text);std::vprintf(text,args);va_end(args);std::puts("");std::fflush(stdout);}
static void checked(HRESULT hr,const char* operation,unsigned line){if(FAILED(hr)){std::fprintf(stderr,"line=%u operation=%s HRESULT=%08lx\n",line,operation,(unsigned long)hr);std::abort();}}
#define ok(operation) checked((operation),#operation,__LINE__)
#include "forwarders.h"
#include "world_lighting_capture.h"
template<class T>struct Ref{T* p=nullptr;~Ref(){if(p)p->Release();}Ref()=default;Ref(const Ref&)=delete;};
static std::vector<unsigned char> read(const std::string& path){std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
struct Meter:ForwardIDirect3DDevice9{
    const char* fault=nullptr;unsigned occurrence=1,seen=0;bool triggered=false;
    explicit Meter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    bool fail(const char* name){if(!fault||std::strcmp(name,fault))return false;if(++seen!=occurrence)return false;fault=nullptr;triggered=true;return true;}
    void arm(const char* name,unsigned at=1){fault=name;occurrence=at;seen=0;triggered=false;}
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE m,DWORD q,BOOL l,IDirect3DSurface9** p,HANDLE* x)override{if(fail("CreateRenderTarget")){*p=nullptr;return E_FAIL;}return real->CreateRenderTarget(w,h,f,m,q,l,p,x);}
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* c,IDirect3DPixelShader9** p)override{if(fail("CreatePixelShader")){*p=nullptr;return E_FAIL;}return real->CreatePixelShader(c,p);}
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE t,IDirect3DStateBlock9** p)override{if(fail("CreateStateBlock")){*p=nullptr;return E_FAIL;}return real->CreateStateBlock(t,p);}
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD n,IDirect3DSurface9** p)override{if(fail("GetRenderTarget")){*p=nullptr;return E_FAIL;}return real->GetRenderTarget(n,p);}
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* v)override{if(fail("GetViewport"))return E_FAIL;return real->GetViewport(v);}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT a,float* p,UINT n)override{if(fail("GetVertexShaderConstantF"))return E_FAIL;return real->GetVertexShaderConstantF(a,p,n);}
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT a,float* p,UINT n)override{if(fail("GetPixelShaderConstantF"))return E_FAIL;return real->GetPixelShaderConstantF(a,p,n);}
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* p)override{if(fail("SetDepthStencilSurface"))return E_FAIL;return real->SetDepthStencilSurface(p);}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* p)override{if(fail("SetRenderTarget"))return E_FAIL;return real->SetRenderTarget(n,p);}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* p)override{if(fail("SetVertexShader"))return E_FAIL;return real->SetVertexShader(p);}
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD f)override{if(fail("SetFVF"))return E_FAIL;return real->SetFVF(f);}
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT s,UINT n)override{if(fail("SetStreamSourceFreq"))return E_FAIL;return real->SetStreamSourceFreq(s,n);}
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD s,D3DTEXTURESTAGESTATETYPE k,DWORD v)override{if(fail("SetTextureStageState"))return E_FAIL;return real->SetTextureStageState(s,k,v);}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* p)override{if(fail("SetPixelShader"))return E_FAIL;return real->SetPixelShader(p);}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD s,IDirect3DBaseTexture9* p)override{if(fail("SetTexture"))return E_FAIL;return real->SetTexture(s,p);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD s,D3DSAMPLERSTATETYPE k,DWORD v)override{if(fail("SetSamplerState"))return E_FAIL;return real->SetSamplerState(s,k,v);}
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE k,DWORD v)override{if(fail("SetRenderState"))return E_FAIL;return real->SetRenderState(k,v);}
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* v)override{if(fail("SetViewport"))return E_FAIL;return real->SetViewport(v);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT s)override{if(fail("DrawPrimitiveUP"))return E_FAIL;return real->DrawPrimitiveUP(t,n,p,s);}
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT w,UINT h,D3DFORMAT f,D3DPOOL q,IDirect3DSurface9** p,HANDLE* x)override{if(fail("CreateOffscreenPlainSurface")){*p=nullptr;return E_FAIL;}return real->CreateOffscreenPlainSurface(w,h,f,q,p,x);}
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* a,IDirect3DSurface9* b)override{if(fail("GetRenderTargetData"))return E_FAIL;return real->GetRenderTargetData(a,b);}
};
static const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_DITHERENABLE,D3DRS_WRAP0,D3DRS_COLORWRITEENABLE,D3DRS_CULLMODE,D3DRS_FILLMODE,D3DRS_MULTISAMPLEMASK};
struct State{
    Ref<IDirect3DSurface9> rt[4],ds;Ref<IDirect3DBaseTexture9> texture[16];Ref<IDirect3DVertexBuffer9> stream;Ref<IDirect3DIndexBuffer9> indices;
    Ref<IDirect3DVertexShader9> vs;Ref<IDirect3DPixelShader9> ps;Ref<IDirect3DVertexDeclaration9> declaration;
    D3DVIEWPORT9 viewport{};RECT scissor{};DWORD fvf=0,sampler[16][13]{},render[std::size(states)]{},stage[2]{};UINT frequency=0,offset=0,stride=0;
    float vertex[256][4]{},pixel[224][4]{};
    explicit State(IDirect3DDevice9* d){
        for(unsigned i=0;i<4;++i)ok(d->GetRenderTarget(i,&rt[i].p));ok(d->GetDepthStencilSurface(&ds.p));
        for(unsigned s=0;s<16;++s){ok(d->GetTexture(s,&texture[s].p));for(unsigned k=1;k<=13;++k)ok(d->GetSamplerState(s,D3DSAMPLERSTATETYPE(k),&sampler[s][k-1]));}
        ok(d->GetVertexShader(&vs.p));ok(d->GetPixelShader(&ps.p));ok(d->GetVertexDeclaration(&declaration.p));ok(d->GetFVF(&fvf));
        ok(d->GetStreamSource(0,&stream.p,&offset,&stride));ok(d->GetStreamSourceFreq(0,&frequency));ok(d->GetIndices(&indices.p));
        ok(d->GetViewport(&viewport));ok(d->GetScissorRect(&scissor));
        for(unsigned i=0;i<std::size(states);++i)ok(d->GetRenderState(states[i],&render[i]));
        ok(d->GetTextureStageState(0,D3DTSS_TEXCOORDINDEX,&stage[0]));ok(d->GetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,&stage[1]));
        ok(d->GetVertexShaderConstantF(0,vertex[0],256));ok(d->GetPixelShaderConstantF(0,pixel[0],224));
    }
    void same(IDirect3DDevice9* d)const{
        State after(d);
        for(unsigned i=0;i<4;++i)assert(rt[i].p==after.rt[i].p);assert(ds.p==after.ds.p);
        for(unsigned s=0;s<16;++s)assert(texture[s].p==after.texture[s].p);
        assert(vs.p==after.vs.p&&ps.p==after.ps.p&&declaration.p==after.declaration.p&&fvf==after.fvf);
        assert(stream.p==after.stream.p&&indices.p==after.indices.p&&offset==after.offset&&stride==after.stride&&frequency==after.frequency);
        assert(!std::memcmp(&viewport,&after.viewport,sizeof viewport)&&!std::memcmp(&scissor,&after.scissor,sizeof scissor));
        assert(!std::memcmp(sampler,after.sampler,sizeof sampler)&&!std::memcmp(render,after.render,sizeof render)&&!std::memcmp(stage,after.stage,sizeof stage));
        assert(!std::memcmp(vertex,after.vertex,sizeof vertex)&&!std::memcmp(pixel,after.pixel,sizeof pixel));
    }
};
static std::string file(const std::string& folder,unsigned id,const char* name){return folder+"/capture-"+std::to_string(id)+"-"+name;}
int main(int argc,char** argv){
    assert(argc==4);const std::string folder=argv[2];const unsigned adapter=unsigned(std::stoul(argv[3]));constexpr unsigned W=37,H=23;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightLightingCapture";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);
    Ref<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);assert(api.p);Ref<IDirect3DDevice9> device;
    assert(adapter<api.p->GetAdapterCount());D3DADAPTER_IDENTIFIER9 identity{};ok(api.p->GetAdapterIdentifier(adapter,0,&identity));
    logf("LIGHT_CAPTURE_ADAPTER index=%u vendor=%04lx device=%04lx description=%s",adapter,(unsigned long)identity.VendorId,(unsigned long)identity.DeviceId,identity.Description);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ok(api.p->CreateDevice(adapter,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device.p));Meter meter(device.p);auto* d=&meter;
    D3DCAPS9 caps{};ok(d->GetDeviceCaps(&caps));assert(caps.MaxVertexShaderConst>=256&&caps.NumSimultaneousRTs>=4);
    Ref<IDirect3DSurface9> rt[4],ds;for(auto& target:rt)ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target.p,nullptr));
    ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&ds.p,nullptr));
    Ref<IDirect3DTexture9> neutral;ok(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&neutral.p,nullptr));D3DLOCKED_RECT lock{};ok(neutral.p->LockRect(0,&lock,nullptr,0));*(DWORD*)lock.pBits=0xff335577;ok(neutral.p->UnlockRect(0));
    Ref<IDirect3DVertexBuffer9> vb;Ref<IDirect3DIndexBuffer9> ib;ok(d->CreateVertexBuffer(512,0,0,D3DPOOL_MANAGED,&vb.p,nullptr));ok(d->CreateIndexBuffer(64,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib.p,nullptr));
    const DWORD shader[]={0xffff0300,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};Ref<IDirect3DPixelShader9> ps;ok(d->CreatePixelShader(shader,&ps.p));
    float vertex[256][4],pixel[224][4];for(unsigned i=0;i<256*4;++i)vertex[i/4][i%4]=(float(i)-512.25f)/2048;for(unsigned i=0;i<224*4;++i)pixel[i/4][i%4]=(float(i)-333.5f)/4096;
    auto setup=[&](IDirect3DTexture9* depth){
        ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=0;i<4;++i)ok(d->SetRenderTarget(i,rt[i].p));ok(d->SetDepthStencilSurface(ds.p));
        ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(ps.p));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
        ok(d->SetStreamSource(0,vb.p,12,24));ok(d->SetIndices(ib.p));ok(d->SetStreamSourceFreq(0,1));
        for(unsigned s=0;s<16;++s){ok(d->SetTexture(s,s==1?depth:neutral.p));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_MIRROR));ok(d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));ok(d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,TRUE));}
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_DITHERENABLE})ok(d->SetRenderState(state,TRUE));
        ok(d->SetRenderState(D3DRS_CLIPPLANEENABLE,1));ok(d->SetRenderState(D3DRS_WRAP0,D3DWRAP_U));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW));ok(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME));ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0x55555555));
        ok(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,1));ok(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2));
        D3DVIEWPORT9 vp={2,1,W-4,H-2,.125f,.94f};RECT scissor={3,2,long(W-3),long(H-2)};ok(d->SetViewport(&vp));ok(d->SetScissorRect(&scissor));
        ok(d->SetVertexShaderConstantF(0,vertex[0],256));ok(d->SetPixelShaderConstantF(0,pixel[0],224));
    };
    unsigned capture=0,positive=0,failures=0;
    for(D3DFORMAT format:{D3DFORMAT(MAKEFOURCC('D','F','2','4')),D3DFORMAT(MAKEFOURCC('I','N','T','Z'))}){
        const HRESULT support=api.p->CheckDeviceFormat(adapter,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_TEXTURE,format);
        if(FAILED(support)){logf("LIGHT_CAPTURE_UNSUPPORTED format=%08lx HRESULT=%08lx",(unsigned long)format,(unsigned long)support);continue;}
        Ref<IDirect3DTexture9> depth;Ref<IDirect3DSurface9> depthSurface;ok(d->CreateTexture(W,H,1,D3DUSAGE_DEPTHSTENCIL,format,D3DPOOL_DEFAULT,&depth.p,nullptr));ok(depth.p->GetSurfaceLevel(0,&depthSurface.p));
        for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetRenderTarget(0,rt[0].p));ok(d->SetDepthStencilSurface(depthSurface.p));
        // Alternating single-pixel 0/1 depths are exactly representable in both
        // physical formats. Any half-resolution reconstruction loses this input.
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){D3DRECT rect={long(x),long(y),long(x+1),long(y+1)};ok(d->Clear(1,&rect,D3DCLEAR_ZBUFFER,0,float((x+3*y)%2),0));}
        setup(depth.p);State before(d);const unsigned id=++capture;
        ok(d->BeginScene());{NorthlightLightingCapture::Capture output(d,folder,id,17,31,W,H);assert(output.active());output.inputs(0,1u<<1,1u<<1);output.inputs(0,1u<<1,1u<<1);output.inputs(1,1u<<1,1u<<1);output.inputs(2,1u);output.finish(true);}ok(d->EndScene());before.same(d);
        auto image=read(file(folder,id,"depth.fgr"));uint32_t header[5];assert(image.size()==20+W*H*4);std::memcpy(header,image.data(),20);assert(header[0]==0x31524746&&header[1]==W&&header[2]==H&&header[3]==D3DFMT_R32F&&header[4]==4);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float value;std::memcpy(&value,image.data()+20+(y*W+x)*4,4);assert(value==float((x+3*y)%2));}
        auto journal=read(file(folder,id,"lighting-inputs.jsonl"));const std::string text(journal.begin(),journal.end());assert(text.find("\"export_complete\":true")!=std::string::npos&&text.find("\"depth_reconstructed\":false")!=std::string::npos);
        auto neutralImage=read(file(folder,id,"lighting-input-g2-s0.fgr"));assert(neutralImage.size()==24);DWORD neutralPixel;std::memcpy(&neutralPixel,neutralImage.data()+20,4);assert(neutralPixel==0xff335577);
        ++positive;logf("LIGHT_CAPTURE_DEPTH format=%u dimensions=%ux%u exactPixels=%u stateRestored=1",unsigned(format),W,H,W*H);
        if(positive==1){
            for(const char* fault:{"CreateRenderTarget","CreatePixelShader","CreateStateBlock","GetRenderTarget","GetViewport","GetVertexShaderConstantF","GetPixelShaderConstantF","SetDepthStencilSurface","SetRenderTarget","SetVertexShader","SetFVF","SetStreamSourceFreq","SetTextureStageState","SetPixelShader","SetTexture","SetSamplerState","SetRenderState","SetViewport","DrawPrimitiveUP","CreateOffscreenPlainSurface","GetRenderTargetData"}){
                setup(depth.p);State initial(d);const unsigned failed=++capture;meter.arm(fault);ok(d->BeginScene());{NorthlightLightingCapture::Capture output(d,folder,failed,18,32,W,H);output.inputs(0,1u<<1);output.finish(true);}ok(d->EndScene());assert(meter.triggered);initial.same(d);
                auto incomplete=read(file(folder,failed,"lighting-inputs-incomplete.jsonl"));const std::string content(incomplete.begin(),incomplete.end());assert(content.find("\"export_complete\":false")!=std::string::npos&&content.find("80004005")!=std::string::npos);++failures;
            }
            setup(depth.p);const unsigned failed=++capture;meter.arm("SetViewport",2);ok(d->BeginScene());{NorthlightLightingCapture::Capture output(d,folder,failed,19,33,W,H);output.inputs(0,1u<<1);output.finish(true);}ok(d->EndScene());assert(meter.triggered);
            auto incomplete=read(file(folder,failed,"lighting-inputs-incomplete.jsonl"));assert(std::string(incomplete.begin(),incomplete.end()).find("\"export_complete\":false")!=std::string::npos);++failures;
        }
    }
    assert(positive>0&&failures==22);
    // Pass transport is a separate exact-bit contract, not inferred from a final
    // end-of-frame bank. This synthetic pass is never labelled a game replay.
    setup(nullptr);ok(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));ok(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
    Ref<IDirect3DCubeTexture9> cube;ok(d->CreateCubeTexture(16,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&cube.p,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));
    for(unsigned face=0;face<6;++face){Ref<IDirect3DSurface9> target;ok(cube.p->GetCubeMapSurface(D3DCUBEMAP_FACES(face),0,&target.p));ok(d->SetRenderTarget(0,target.p));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,face%2?0xffffffff:0xff000000,1,0));}
    setup(nullptr);State pointState(d);
    struct PointFixture {IDirect3DCubeTexture9* cube=nullptr;NorthlightLocalLights::Light selected{};bool ready=true,cacheValid=false;float selectionGain=1;struct {bool usable=true,complete=true;uint32_t updatedAt=123;uint64_t generation=17;NorthlightLocalLights::Light light{};} schedule;uint64_t cacheSource=42;unsigned staticOnlyFaces=3;std::string map="Azeroth";bool faceKnown[6]={true};uint64_t faceSerial[6]={17};struct {uint64_t words[2]={11,22},triangles=33;} faceContent[6];IDirect3DSurface9* cacheSurface[6]{};};
    std::array<PointFixture,128> points{};points[0].cube=cube.p;points[0].selected.sourceId=42;points[0].schedule.light.sourceId=42;
    const unsigned id=++capture;{NorthlightLightingCapture::Capture output(d,folder,id,20,34,W,H);float matrix[16]={};for(unsigned i=0;i<4;++i)matrix[i*5]=1;for(unsigned slot=0;slot<4;++slot)output.directional(slot,slot>=2,slot<2?'-':slot==2?'R':'U',matrix,cube.p,1,slot==3,91,17,23);output.inputs(0,1u);output.pass("FixtureTransport",0,1u,0,0,false,S_OK);output.pointSources(points,130,true,true);output.pointSources(points,130,true);output.pointPass(0,false,nullptr,S_OK);output.finish(true);}
    pointState.same(d);
    for(unsigned face=0;face<6;++face){auto image=read(file(folder,id,("point-0-face-"+std::to_string(face)+".fgr").c_str()));assert(image==read(file(folder,id,("native-point-0-face-"+std::to_string(face)+".fgr").c_str())));assert(image.size()==20+16*16*4);for(unsigned i=0;i<16*16;++i){float value;std::memcpy(&value,image.data()+20+i*4,4);assert(value==float(face%2));}}
    auto bank=read(file(folder,id,"lighting-pass-1-constants.f32"));assert(bank.size()==116*16&&!std::memcmp(bank.data(),pixel,bank.size()));
    auto journal=read(file(folder,id,"lighting-inputs.jsonl"));assert(std::string(journal.begin(),journal.end()).find("\"export_complete\":true")!=std::string::npos);
    const DWORD vertexCode[]={0xfffe0300,0x0200001f,0x80000000,0x900f0000,0x0200001f,0x80000000,0xe00f0000,0x02000001,0xe00f0000,0x90e40000,0x0000ffff};
    Ref<IDirect3DVertexShader9> nativeVS;ok(d->CreateVertexShader(vertexCode,&nativeVS.p));ok(d->SetVertexShader(nativeVS.p));
    const unsigned nativeId=++capture;State nativeState(d);const uint64_t sources[4]={42,55,0,0},emptySources[4]={};
    ok(d->BeginScene());{NorthlightLightingCapture::Capture output(d,folder,nativeId,21,35,W,H,"native-inputs");
        for(unsigned i=0;i<130;++i){const unsigned token=output.nativeBefore(35,i?42:0,0,i%2,true,nativeVS.p,ps.p,i?sources:emptySources);assert((token&0x7fffffffu)==(i<128?i+1:0));assert(bool(token&0x80000000u)==(i<8));const float position[3]={1,2,3},rgb[3]={.5f,.6f,.7f},attenuation[3]={1,0,.01f};for(unsigned slot=0;slot<4;++slot)output.nativeSlot(token,4,slot,slot!=3,position,rgb,attenuation,!i?"no-binding":slot==0?"matched-ready":slot==1?"not-ready":slot==2?"no-binding":"decode-rejected",i&&slot<2?42:0,i&&slot<2?0:-1);output.nativeAfter(token,i%2,S_OK,nullptr,i%3,i%5,i%7,i!=0);}
        output.finish(true);const float zero[3]={};output.nativeSlot(1,4,0,false,nullptr,nullptr,nullptr,"disabled");output.directional(0,false,'-',nullptr,nullptr,0,false,0,0,0);output.pointSources(points,130,true);assert(!output.nativeBefore(35,0,0,false,false,nullptr,nullptr,emptySources));}ok(d->EndScene());nativeState.same(d);
    assert(read(file(folder,nativeId,"native-draw-1-vs.bin"))==std::vector<unsigned char>(reinterpret_cast<const unsigned char*>(vertexCode),reinterpret_cast<const unsigned char*>(vertexCode)+sizeof vertexCode));
    auto nativeBank=read(file(folder,nativeId,"native-draw-128-vs-constants.f32"));assert(nativeBank.size()==sizeof vertex&&!std::memcmp(nativeBank.data(),vertex,sizeof vertex));
    for(unsigned i=1;i<=8;++i)assert(read(file(folder,nativeId,("native-draw-"+std::to_string(i)+"-before.fgr").c_str()))==read(file(folder,nativeId,("native-draw-"+std::to_string(i)+"-after.fgr").c_str())));
    logf("NATIVE_CAPTURE_PASS draws=128 imagePairs=8 exactShaders=1 exactConstants=1 stateUnchanged=1");
    logf("LIGHT_CAPTURE_PASS depthFormats=%u exactDepthPixels=%u apiFailureCases=%u constantRegisters=116 stateVS=256 statePS=224 timing=none",positive,positive*W*H,failures);
    // Release device-held references before the stack-owned reference guards.
    ok(d->SetDepthStencilSurface(nullptr));for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetStreamSource(0,nullptr,0,0));ok(d->SetIndices(nullptr));ok(d->SetPixelShader(nullptr));
    return 0;
}
