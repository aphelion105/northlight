// Reuse the existing production-shader graph; only the sky storage differs.
#define NORTHLIGHT_HDR_PIPELINE_LIBRARY
#include "hdr_pipeline_gpu.cpp"
struct SkyRouting {
    NorthlightHdr::Frame& hdrFrame;IDirect3DSurface9* hdrPresentBack;
    struct {struct {D3DMULTISAMPLE_TYPE MultiSampleType=D3DMULTISAMPLE_NONE;} logical;} hdr;
#include "hdr_sky_storage_methods.inc"
};
int main(int argc,char** argv){
    assert(argc==2);HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    HMODULE lib=LoadLibraryA(argv[1]);assert(lib);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(lib,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));DeviceMeter d(raw);
    const Scenario scenarios[]={
        {"sky-partial",128,96,0,4,4,true,true,false,false,true,true,false},
        {"sky-mask",128,96,0,0,4,true,true,false,true,true,true,false},
        {"sky-expansion",128,96,0,0,4,true,true,true,false,true,true,false},
        {"sky-odd",257,193,0,4,4,true,true,false,false,true,true,false},
        {"sky-msaa2",128,96,2,4,4,true,true,false,false,true,true,false},
        {"sky-msaa4",128,96,4,4,4,true,true,false,false,true,true,false},
        {"no-sky",128,96,0,4,4,true,false,false,false,true,true,false},
    };
    unsigned cases=0;
    for(const auto& scenario:scenarios){Graph g(&d,scenario,"");
        g.frame(2,true,false);const auto expected=g.pixels();g.frame(2,true,true);assert(g.pixels()==expected);
        // The previous post/world image is deliberately unrelated. A partial
        // native sky draw must still start from this frame's complete SDR base.
        ok(d.ColorFill(g.hdr.surface(),nullptr,0xffff00ff));g.frame(2,true,true);assert(g.pixels()==expected);
        g.frame(2,true,false);d.calls={};g.frame(2,true,false);const auto before=d.calls;
        d.calls={};g.frame(2,true,true);const auto after=d.calls;
        assert(!before.allocations&&!after.allocations&&before.draws==after.draws);
        assert(before.resolves==after.resolves+(scenario.sky&&!scenario.samples?1:0));
        SkyRouting routing{g.hdr,g.output.surface};assert(routing.hdrSkyTarget()==g.hdr.surface());routing.hdr.logical.MultiSampleType=D3DMULTISAMPLE_2_SAMPLES;assert(routing.hdrSkyTarget()==g.output.surface);
        std::printf("HDR_SKY_STORAGE_CASE name=%s samples=%u exact=1 stalePostRejected=1 resolves=%u/%u draws=%u allocations=0\n",scenario.name,scenario.samples,before.resolves,after.resolves,after.draws);++cases;
    }
    // Frame resources resize/reset independently of the additional swapchain.
    {NorthlightHdr::Frame frame;IDirect3DSurface9* back=nullptr;ok(raw->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));SkyRouting routing{frame,back};
     for(const auto dims:{std::pair<unsigned,unsigned>{128,96},{257,193},{128,96}}){ok(raw->BeginScene());ok(frame.ensure(raw,dims.first,dims.second));ok(raw->EndScene());D3DSURFACE_DESC desc{};ok(routing.hdrSkyTarget()->GetDesc(&desc));assert(desc.Width==dims.first&&desc.Height==dims.second&&desc.Format==D3DFMT_A16B16G16R16F&&desc.MultiSampleType==D3DMULTISAMPLE_NONE);}
     frame.reset();assert(!routing.hdrSkyTarget());ok(raw->BeginScene());ok(frame.ensure(raw,128,96));ok(raw->EndScene());assert(routing.hdrSkyTarget()==frame.surface());back->Release();}
    {Scenario scenario={"sky-copy-4k",3840,2160,0,0,4,true,true,false,false,true,true,false};Graph g(&d,scenario,"");
     for(unsigned block=0;block<8;++block){const bool alias=block%4==1||block%4==2;const auto t=timeGraph(raw,[&]{g.frame(2,false,alias);},16);std::printf("HDR_SKY_STORAGE_TIMING block=%u alias=%u gpuMs=%.6f submitMs=%.6f\n",block,unsigned(alias),t.gpu,t.cpu);}
    }
    raw->Release();api->Release();FreeLibrary(lib);DestroyWindow(window);std::printf("HDR_SKY_STORAGE_GPU_PASS cases=%u pixelBits=exact partialSkyPoison=1 resizeReset=1 msaaFallback=1 nativeSkyWorldMaterialExposureBloomUi=1\n",cases);
}
