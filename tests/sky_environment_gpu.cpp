#define main unused_water_endpoint_main
#include "water_fog_endpoint_gpu.cpp"
#undef main
#include "sky-background-production.h"

static unsigned productionDepthCases(IDirect3D9* api,IDirect3DDevice9* d){
    constexpr unsigned n=8;unsigned cases=0;
    for(auto format:{D3DFMT_A8R8G8B8,D3DFMT_A16B16G16R16F})for(auto samples:{D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_2_SAMPLES}){
        if(FAILED(api->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,format,TRUE,samples,nullptr))||
           FAILED(api->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,D3DFMT_D24S8,TRUE,samples,nullptr)))continue;
        IDirect3DSurface9 *target=nullptr,*depth=nullptr,*copy=nullptr,*staging=nullptr;
        IDirect3DTexture9 *scene=nullptr,*layers=nullptr;
        ok(d->SetDepthStencilSurface(nullptr));
        ok(d->CreateRenderTarget(n,n,format,samples,0,FALSE,&target,nullptr));
        ok(d->CreateDepthStencilSurface(n,n,D3DFMT_D24S8,samples,0,TRUE,&depth,nullptr));
        ok(d->CreateTexture(n,n,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&scene,nullptr));ok(scene->GetSurfaceLevel(0,&copy));
        ok(d->CreateOffscreenPlainSurface(n,n,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        Image layer{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
        const float values[]={.3f,.3f,.3f,1};std::memcpy(layer.pixels.data(),values,16);layers=texture(d,layer);
        SkyBackgroundProduction renderer{d};
        const unsigned stride=format==D3DFMT_A8R8G8B8?4:8;
        auto pixels=[&]{
            ok(d->StretchRect(target,nullptr,copy,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(copy,staging));
            D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> result(n*n*stride);
            for(unsigned y=0;y<n;++y)std::memcpy(result.data()+y*n*stride,static_cast<char*>(lock.pBits)+y*lock.Pitch,n*stride);
            ok(staging->UnlockRect());return result;
        };
        // Geometry may precede the sky boundary. Include the native world,
        // horizon and sky depth intervals, plus independent texture colours.
        for(float z:{.5f,.94f,.9985f,.9999f}){
            ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));
            D3DVIEWPORT9 vp={0,0,n,n,0,.94f};ok(d->SetViewport(&vp));
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,D3DCOLOR_XRGB(80,110,140),1,0));
            D3DRECT left={0,0,n/2,n},detail={0,0,n/4,n/2};
            ok(d->Clear(1,&left,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,D3DCOLOR_XRGB(40,70,100),z,0));
            ok(d->Clear(1,&detail,D3DCLEAR_TARGET,D3DCOLOR_XRGB(70,20,130),z,0));
            const auto before=pixels();
            ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS));
            float projection[]={1,1,1},view[16]{},colors[8][3];for(unsigned i=0;i<4;++i)view[i*5]=1;
            for(auto& color:colors)for(float& c:color)c=.8f;
            ok(d->BeginScene());assert(renderer.renderSkyBackground(target,scene,nullptr,layers,n,n,projection,view,colors));ok(d->EndScene());
            const auto after=pixels();
            for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
                const auto offset=(y*n+x)*stride;
                if(x<n/2){if(std::memcmp(before.data()+offset,after.data()+offset,stride)){
                    std::fprintf(stderr,"Sky overwrote geometry: format=%u msaa=%u depth=%g pixel=%u,%u\n",unsigned(format),unsigned(samples),z,x,y);std::abort();}}
                else assert(std::memcmp(before.data()+offset,after.data()+offset,stride));
            }
            IDirect3DSurface9* restored=nullptr;ok(d->GetDepthStencilSurface(&restored));assert(restored==depth);restored->Release();
            DWORD value=0;ok(d->GetRenderState(D3DRS_ZWRITEENABLE,&value));assert(value);ok(d->GetRenderState(D3DRS_ZFUNC,&value));assert(value==D3DCMP_LESS);
            D3DVIEWPORT9 restoredVP{};ok(d->GetViewport(&restoredVP));assert(!std::memcmp(&vp,&restoredVP,sizeof vp));
            ++cases;
        }
        // No depth surface means no proof of clear sky: leave the native image.
        const auto before=pixels();ok(d->SetDepthStencilSurface(nullptr));
        float p[]={1,1,1},v[16]{},c[8][3]{};
        ok(d->BeginScene());assert(!renderer.renderSkyBackground(target,scene,nullptr,layers,n,n,p,v,c));ok(d->EndScene());
        assert(before==pixels());
        if(renderer.skyBackgroundPS)renderer.skyBackgroundPS->Release();
        layers->Release();staging->Release();copy->Release();scene->Release();depth->Release();target->Release();
    }
    assert(cases>=8);return cases;
}

static Image rgba(float r,float g,float b,float a){
    Image v{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
    const float p[]={r,g,b,a};std::memcpy(v.pixels.data(),p,16);return v;
}
int main(int argc,char** argv){
    assert(argc==3);constexpr unsigned size=8;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightSkyEnvironment";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[2]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));
    auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(size,size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));
    const auto code=read(argv[1]);IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shader));ok(d->SetPixelShader(shader));
    D3DVIEWPORT9 vp={0,0,size,size,0,1};ok(d->SetViewport(&vp));
    Vertex quad[]={{-.5f,-.5f,0,1,0,0},{size-.5f,-.5f,0,1,1,0},{-.5f,size-.5f,0,1,0,1},{size-.5f,size-.5f,0,1,1,1}};
    auto decode=[](double x){return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);};
    unsigned cases=0;
    for(bool depthTest:{false,true})for(float elevation:{0.f,1.f})for(bool hdr:{false,true})for(float transmission:{0.f,.5f,1.f})for(float depth:{.5f,1.f})for(float sky:{.1f,.8f}){
        // Native background .3 plus foreground .2. Replacing the background
        // must preserve the foreground, an independent HDR residual and geometry.
        const float native=.2f+.3f*transmission;
        IDirect3DTexture9* inputs[]={texture(d,rgba(depth,0,0,0)),texture(d,rgba(native,native,native,1)),texture(d,rgba(.3f*transmission,.3f*transmission,.3f*transmission,transmission))};
        unsigned i=0;for(unsigned slot:{0u,1u,6u}){ok(d->SetTexture(slot,inputs[i++]));for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,state,D3DTEXF_POINT));ok(d->SetSamplerState(slot,D3DSAMP_SRGBTEXTURE,FALSE));}
        float c[57][4]{};c[0][0]=c[0][1]=1.f/size;c[1][0]=c[1][1]=1e12f;c[1][2]=1;c[2][0]=c[3][1]=c[4][2]=1;c[4][0]=1-elevation;c[4][2]=elevation;c[10][0]=hdr?2:0;c[10][3]=depthTest?1:0;
        for(unsigned row=51;row<57;++row)c[row][0]=c[row][1]=c[row][2]=sky+.01f*(row-51);
        ok(d->SetPixelShaderConstantF(0,c[0],57));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,hdr));
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
        const double gradient=sky+(elevation==0?.04:0);
        const double expected=depthTest&&depth<1?1:hdr?1+decode(.2+gradient*transmission)-decode(native):.2+gradient*transmission;
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){float rgb[3];std::memcpy(rgb,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,12);for(float value:rgb)assert(std::isfinite(value)&&std::abs(value-expected)<2e-5);}
        ok(staging->UnlockRect());i=0;for(unsigned slot:{0u,1u,6u}){d->SetTexture(slot,nullptr);inputs[i++]->Release();}++cases;
    }
    std::printf("PASS sky environment GPU: %u cases, foreground transmission, signed HDR residual, opaque geometry, SDR\n",cases);
    const unsigned geometryCases=productionDepthCases(api,d);
    std::printf("PASS production sky boundary: %u geometry-before-sky cases, exact colours, SDR/HDR, supported MSAA, no-depth fallback and state restoration\n",geometryCases);
    shader->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
