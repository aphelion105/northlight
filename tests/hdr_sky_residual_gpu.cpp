// Independent colour oracle for complete native sky + linear emitter excess.
#define NORTHLIGHT_HDR_PIPELINE_LIBRARY
#include "hdr_pipeline_gpu.cpp"
#include "hdr_sky_residual.h"

static std::vector<float> pixels(IDirect3DDevice9* d,IDirect3DSurface9* surface){
    D3DSURFACE_DESC desc{};ok(surface->GetDesc(&desc));IDirect3DSurface9* readback=nullptr;
    ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    ok(d->GetRenderTargetData(surface,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<float> result(desc.Width*desc.Height*4);
    for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x)for(unsigned c=0;c<4;++c){
        const auto* row=static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch;
        result[(y*desc.Width+x)*4+c]=desc.Format==D3DFMT_A16B16G16R16F?half(reinterpret_cast<const std::uint16_t*>(row)[x*4+c]):row[x*4+(c<3?2-c:3)]/255.f;
    }
    ok(readback->UnlockRect());readback->Release();return result;
}
static float decode(float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
static void replayCapture(DeviceMeter& d,const char* input,const char* output){
    const auto captured=image(input);assert(captured.bytes==4);
    auto* native=texture(&d,captured);const float clearDepth=1;
    Image z{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(z.pixels.data(),&clearDepth,4);auto* depth=texture(&d,z);
    NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;settings.legacyExpansion=0;
    ok(d.BeginScene());ok(frame.ensure(&d,captured.width,captured.height));frame.beginSkyResidual();
    ok(d.ColorFill(frame.surface(),nullptr,0xff000000));
    ok(frame.prepareLegacy(&d,native,depth,frame.surface(),captured.width,captured.height,0,.94f,settings));ok(d.EndScene());
    const auto actual=pixels(&d,frame.surface());float error=0;
    for(unsigned p=0;p<captured.width*captured.height;++p)for(unsigned c=0;c<3;++c){
        const float expected=decode(captured.pixels[p*4+2-c]/255.f);error=std::max(error,std::fabs(actual[p*4+c]-expected));
    }assert(error<.001f);
    IDirect3DSurface9* readback=nullptr;ok(d.CreateOffscreenPlainSurface(captured.width,captured.height,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    ok(d.GetRenderTargetData(frame.surface(),readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::ofstream file(output,std::ios::binary);const std::uint32_t header[]={0x31524746,captured.width,captured.height,D3DFMT_A16B16G16R16F,8};file.write(reinterpret_cast<const char*>(header),sizeof header);
    for(unsigned y=0;y<captured.height;++y)file.write(static_cast<const char*>(lock.pBits)+y*lock.Pitch,captured.width*8);
    ok(readback->UnlockRect());readback->Release();native->Release();depth->Release();
    std::printf("HDR_SKY_CAPTURE_PASS pixels=%u decodeError=%.7f additionalEmission=0 neutralGrade=1\n",captured.width*captured.height,error);
}
static void check(DeviceMeter& d,unsigned samples,bool inlineBase){
    const unsigned w=96,h=64;NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;
    settings.legacyExpansion=0;Surface native,resolved,excess;
    native.make(&d,w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_TYPE(samples));
    resolved.make(&d,w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,true);
    if(samples)excess.make(&d,w,h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(samples));
    Image z{w,h,D3DFMT_R32F,4,std::vector<unsigned char>(w*h*4)};
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const float depth=x<w/2?1.f:x<3*w/4?1.f-1.f/16777216.f:.9985f;std::memcpy(z.pixels.data()+(y*w+x)*4,&depth,4);
    }
    auto* depth=texture(&d,z);IDirect3DPixelShader9* constant=nullptr;
    ok(d.CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&constant));
    for(unsigned iteration=0;iteration<3;++iteration){
        ok(d.BeginScene());ok(frame.ensure(&d,w,h));frame.beginSkyResidual();
        auto* target=samples?excess.surface:frame.surface();
        const float base[]={.14f,.35f,.46f,1};
        auto bind=[&](IDirect3DSurface9* s,const float* color){ok(NorthlightHdr::quadState(&d,s,w,h));ok(d.SetPixelShader(constant));ok(d.SetPixelShaderConstantF(0,color,1));};
        bind(native.surface,base);ok(NorthlightHdr::drawQuad(&d,w,h));
        const float emission[]={iteration?.25f:0,iteration?2.f:0,iteration?.75f:0,1};
        bind(target,emission);ok(NorthlightHdr::drawQuad(&d,w,h));
        // Native additive ribbons, translucent cloud, multiplicative tint,
        // screen glow, and a small opaque foreground. The CPU verifies the
        // same destination factors independently, including partial coverage.
        const struct Layer{float rgba[4];DWORD src,dst;bool opaque;} layers[]={
            {{.03f,.2f,.05f,.7f},D3DBLEND_SRCALPHA,D3DBLEND_ONE,false},
            {{.6f,.65f,.7f,.25f},D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,false},
            {{.9f,.8f,.7f,1},D3DBLEND_DESTCOLOR,D3DBLEND_ZERO,false},
            {{.1f,.2f,.3f,1},D3DBLEND_INVDESTCOLOR,D3DBLEND_ONE,false},
            {{.3f,.4f,.5f,1},D3DBLEND_ONE,D3DBLEND_ZERO,true}};
        for(const auto& layer:layers)for(bool residual:{false,true}){
            bind(residual?target:native.surface,layer.rgba);
            ok(d.SetRenderState(D3DRS_ALPHABLENDENABLE,!layer.opaque));ok(d.SetRenderState(D3DRS_SRCBLEND,layer.src));ok(d.SetRenderState(D3DRS_DESTBLEND,layer.dst));
            ok(d.SetRenderState(D3DRS_COLORWRITEENABLE,7));
            RECT rect{0,0,int(w/4),int(h/2)};ok(d.SetScissorRect(&rect));ok(d.SetRenderState(D3DRS_SCISSORTESTENABLE,layer.opaque));
            auto draw=[&]{return NorthlightHdr::drawQuad(&d,w,h);};
            if(residual)ok(NorthlightHdr::attenuateSky(&d,draw));else ok(draw());
            DWORD actual=0;ok(d.GetRenderState(D3DRS_SRCBLEND,&actual));assert(actual==layer.src);ok(d.GetRenderState(D3DRS_DESTBLEND,&actual));assert(actual==layer.dst);
        }
        ok(d.EndScene());ok(d.StretchRect(native.surface,nullptr,resolved.surface,nullptr,D3DTEXF_NONE));
        const auto source=pixels(&d,resolved.surface);
        ok(d.BeginScene());ok(frame.prepareLegacy(&d,resolved.texture,depth,target,w,h,0,.94f,settings,nullptr,!inlineBase));ok(d.EndScene());
        auto before=pixels(&d,frame.surface());
        // A failed inline compositor retries against the same sky: no second
        // source addition and no re-resolve of the old MSAA residual target.
        ok(d.BeginScene());ok(frame.prepareLegacy(&d,resolved.texture,depth,frame.surface(),w,h,0,.94f,settings));ok(d.EndScene());
        const auto actual=pixels(&d,frame.surface());float error=0;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<3;++c){
            const unsigned at=(y*w+x)*4+c;const bool sky=x<w/2,opaque=x<w/4&&y<h/2;
            const float factor=.75f*layers[2].rgba[c]*(1-layers[3].rgba[c]);
            const float expected=decode(source[at])+(sky&&!opaque?emission[c]*factor:0);
            error=std::max(error,std::fabs(actual[at]-expected));assert(std::fabs(actual[at]-expected)<.003f);
            if(sky)assert(actual[at]==before[at]);
        }
        std::printf("HDR_SKY_RESIDUAL_CASE samples=%u inline=%u iteration=%u error=%.7f huePreserved=1 headroom=1 noDoubleAdd=1\n",samples,unsigned(inlineBase),iteration,error);
    }
    frame.reset();assert(!frame.surface());constant->Release();depth->Release();
}
int main(int argc,char** argv){
    assert(argc==2||argc==4);HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    HMODULE library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    {DeviceMeter d(raw);for(unsigned samples:{0u,2u,4u})for(bool inlineBase:{false,true})check(d,samples,inlineBase);if(argc==4)replayCapture(d,argv[2],argv[3]);}
    raw->Release();api->Release();FreeLibrary(library);DestroyWindow(window);std::puts("HDR_SKY_RESIDUAL_GPU_PASS cases=18");
}
