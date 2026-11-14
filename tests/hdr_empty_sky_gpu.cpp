// The actual Device admission/replay methods, on private GPU targets.
#define NORTHLIGHT_HDR_SKY_RESIDUAL_LIBRARY 1
#include "hdr_sky_residual_gpu.cpp"
#include "world_draw_domain.h"
#include "sky_composition_renderer.h"

struct ExtensionDevice:DeviceMeter {
    using DeviceMeter::DeviceMeter;
    unsigned stateBlocks=0;
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE t,IDirect3DStateBlock9** p)override{++stateBlocks;return real->CreateStateBlock(t,p);}
    struct RawScope {explicit RawScope(ExtensionDevice&) {}}; // This fixture has no mirror.
};
struct EmptySky {
    ExtensionDevice* ext;IDirect3DSurface9* target;
    bool hdrSkyValid=true,hdrSkyEmission=false,hdrSkyReplaying=false,hdrSkyRejected=false;
    bool auroraDrawProven=false;unsigned hdrSkyZeroSkips=0,auroraHdrDraws=0;
    float auroraDrawScale=0,auroraDrawIntensity=1;
    struct {float legacyExpansion=0;}hdrFrameSettings;
    struct Discs {
        IDirect3DSurface9* hdrSkyTarget=nullptr;bool hdrSkyWritten=false;
        #include "empty_sky_disc_methods.h"
    } discs;
    Discs* celestialDiscs=&discs;
    struct Aurora {
        template<class Draw>bool draw(ExtensionDevice*,bool,float,Draw draw,HRESULT& hr,float,float){hr=draw();return true;}
    } auroraRenderer;
    struct {bool active=true;}hdr;
    bool effectCoverageNative=true,hdrUiPhase=false,hdrAtUiBoundary=false,enabled=true,failed=false,applied=false;
    unsigned debugMode=0,worldDebug=0;bool drawUiHashVS=false,hdrRainDraw=false;
    float worldMinDepth=0,worldMaxDepth=.94f;
    DWORD drawRenderState(D3DRENDERSTATETYPE s){DWORD v=0;ok(ext->GetRenderState(s,&v));return v;}
    bool nameMaskFullViewport(const D3DVIEWPORT9&)const{return true;}
    bool worldDepthBound()const{return true;}
    bool hdrBackTarget()const{return true;}
    IDirect3DSurface9* hdrSkyTarget()const{return target;}
    template<class T>static void drop(T*& p){dropGraph(p);}
    #include "empty_sky_device_methods.h"
};

static void runEmptySky(ExtensionDevice& d,unsigned samples,bool timing){
    const unsigned w=timing?3840:96,h=timing?2160:64;
    Surface native,excess,resolved;
    native.make(&d,w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_TYPE(samples));
    excess.make(&d,w,h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(samples));
    resolved.make(&d,w,h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE);
    IDirect3DPixelShader9* constant=nullptr;ok(d.CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&constant));
    const float color[]={.6f,.4f,.2f,.25f};
    auto bind=[&]{ok(NorthlightHdr::quadState(&d,native.surface,w,h));ok(d.SetPixelShader(constant));ok(d.SetPixelShaderConstantF(0,color,1));
        ok(d.SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d.SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d.SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
        const D3DVIEWPORT9 vp={0,0,w,h,0,.94f};ok(d.SetViewport(&vp));};
    EmptySky state{&d,excess.surface};state.discs.setHdrSkyTarget(excess.surface);
    // Empty, disc-owned and aurora-owned headroom. The reference forces the
    // previous conservative assumption that the target always contains emission.
    for(unsigned source=0;source<3;++source){std::vector<float> reference;
        for(unsigned variant=0;variant<2;++variant){
            state.hdrSkyEmission=variant==0||source==2;state.discs.hdrSkyWritten=source==1;
            ok(d.ColorFill(excess.surface,nullptr,source?0xff805020:0xff000000));bind();
            ok(d.SetRenderState(D3DRS_ZENABLE,TRUE));ok(d.SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
            assert(state.hdrWorldAlphaDraw(false)==(variant==0||source!=0));ok(d.SetRenderState(D3DRS_ZENABLE,FALSE));
            d.calls={};ok(d.BeginScene());
            for(unsigned layer=0;layer<8;++layer)state.hdrReplaySky(true,false,[&]{return NorthlightHdr::drawQuad(&d,w,h);});
            ok(d.EndScene());assert(d.calls.draws==((variant==0||source)?8u:0u));
            if(variant&&source==0)assert(d.calls.targets==0); // No empty render-pass interruptions either.
            IDirect3DSurface9* restored=nullptr;ok(d.GetRenderTarget(0,&restored));assert(restored==native.surface);restored->Release();
            ok(d.StretchRect(excess.surface,nullptr,resolved.surface,nullptr,D3DTEXF_NONE));auto result=pixels(&d,resolved.surface);
            if(!variant)reference=result;else assert(reference==result);
        }
    }
    // A new owned emitter wakes a zero target; invalidation and target reset
    // revoke the proof, even when the COM pointer is reused next frame.
    state.hdrSkyEmission=false;state.discs.setHdrSkyTarget(excess.surface);assert(!state.hdrSkyHasEmission());
    state.discs.hdrSkyWritten=true;assert(state.hdrSkyHasEmission());state.discs.setHdrSkyTarget(excess.surface);assert(!state.hdrSkyHasEmission());
    state.auroraDrawProven=true;bind();d.calls={};ok(d.BeginScene());state.hdrReplaySky(true,false,[&]{return NorthlightHdr::drawQuad(&d,w,h);});ok(d.EndScene());
    assert(state.hdrSkyHasEmission()&&state.auroraHdrDraws==1&&d.calls.draws==1);state.auroraDrawProven=false;
    state.hdrSkyValid=false;assert(!state.hdrSkyHasEmission());state.hdrSkyValid=true;
    if(timing){
        for(unsigned block=0;block<8;++block){const bool optimized=((block+1)/2)%2;
            state.hdrSkyEmission=!optimized;state.discs.setHdrSkyTarget(excess.surface);
            ok(d.ColorFill(excess.surface,nullptr,0xff000000));bind();
            auto submit=[&]{ok(d.BeginScene());NorthlightHdr::drawQuad(&d,w,h);
                for(unsigned i=0;i<8;++i)state.hdrReplaySky(true,false,[&]{return NorthlightHdr::drawQuad(&d,w,h);});ok(d.EndScene());};
            const auto t=timeGraph(&d,submit,12);
            std::printf("EMPTY_SKY_TIMING optimized=%u msaa=%u gpuMs=%.6f cpuMs=%.6f\n",unsigned(optimized),samples,t.gpu,t.cpu);
        }
    }
    // Additive native foreground is also identity for the sky-layer mask.
    NorthlightSkyComposition::Capture layers;layers.begin(0);bind();
    ok(d.BeginScene());assert(layers.prepare(&d));ok(d.SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));
    unsigned submitted=0;const auto blocks=d.stateBlocks;assert(layers.draw(&d,NorthlightSkyComposition::Role::Foreground,[&]{++submitted;return NorthlightHdr::drawQuad(&d,w,h);}));
    assert(submitted==0&&d.stateBlocks==blocks);ok(d.EndScene());layers.reset();
    constant->Release();std::printf("EMPTY_SKY_GPU_PASS msaa=%u exact=1 emptyDraws=0 emptyTargets=0 positivePreserved=1 emitterWakes=1 reset=1 additiveMaskDraws=0\n",samples);
}
int main(int argc,char** argv){
    assert(argc==2||argc==3);HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    {ExtensionDevice d(raw);for(unsigned samples:{0u,2u,4u})runEmptySky(d,samples,argc==3);}
    raw->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
}
