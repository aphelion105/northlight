#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include <cassert>
struct WorldScreenEffectsTest {
    static void setup(WorldRenderer& w){
        w.valid=true;w.lastRequest.map="Northrend";w.frames=599;
        for(unsigned i=0;i<16;++i)w.context.view[i]=w.context.inverseView[i]=(i%5==0)?1.f:0.f;
        for(unsigned k=0;k<3;++k){w.context.camera[k]=0;w.context.ambient[k]=.25f;w.context.direct[k]=.6f;}
        w.context.lightDirection[0]=.4f;w.context.lightDirection[1]=.2f;w.context.lightDirection[2]=-.8944272f;
        w.projection[0]=w.projection[1]=w.projection[2]=1;
        w.sourceDirections[0]={.4f,.2f,-.8944272f};w.sourceDirections[1]={0,0,1};w.sourceWeights[0]=1;w.sourceWeights[1]=0;
        w.quality.equipmentLights=w.quality.horizonHaze=w.quality.fogClouds=0;w.quality.pointShadows=1;
        w.effects.gi=w.effects.shadows=true;
        auto field=std::make_shared<NorthlightRegionalFog::Field>();field->originX=field->originY=-256;field->airCells=field->fogCells=4096;
        for(auto& cell:field->texels){cell.ground=-2;cell.day=.015f;cell.height=2.5f;}
        auto publication=std::make_shared<WorldRenderer::FogRegion>();publication->map="Northrend";publication->field=field;w.publishedFogRegion=publication;
        assert(!w.active&&!w.ready()&&w.screenReady());
    }
    static void source(WorldRenderer& w,bool on){w.sourceColors[0]=w.sourceGlowColors[0]=on?WorldRenderer::V(.6f,.5f,.4f):WorldRenderer::V();w.sourceColors[1]=w.sourceGlowColors[1]={};w.effects.fog=true;w.fogHistoryValid=false;}
    static std::vector<unsigned char> fog(WorldRenderer& w,bool history=false){
        assert(w.composited&&!w.fogGeometryReady&&!w.shadowFrameReady&&!w.pointReady&&!w.shadowsComposited&&!w.active);
        assert(w.uploadedFogField==w.publishedFogRegion->field);
        auto* surface=history?w.fogHistorySurface:w.fogSurface;if(history)assert(w.fogHistoryValid);D3DSURFACE_DESC desc{};assert(SUCCEEDED(surface->GetDesc(&desc)));IDirect3DSurface9* read=nullptr;
        assert(SUCCEEDED(w.d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&read,nullptr)));
        assert(SUCCEEDED(w.d->GetRenderTargetData(surface,read)));D3DLOCKED_RECT lock{};assert(SUCCEEDED(read->LockRect(&lock,nullptr,D3DLOCK_READONLY)));
        std::vector<unsigned char> bytes(size_t(desc.Width)*desc.Height*8);for(unsigned y=0;y<desc.Height;++y)std::memcpy(bytes.data()+size_t(y)*desc.Width*8,static_cast<const char*>(lock.pBits)+y*lock.Pitch,desc.Width*8);
        assert(SUCCEEDED(read->UnlockRect()));read->Release();return bytes;
    }
    static void altitude(WorldRenderer& w,float z,bool bounds){
        w.context.camera[2]=w.context.inverseView[14]=z;
        w.fogGlobalHeights=bounds?NorthlightRegionalFog::globalHeightBounds(*w.uploadedFogField):std::array<float,3>{};
    }
    static auto field(WorldRenderer& w,std::shared_ptr<const NorthlightRegionalFog::Field> field){
        auto old=w.publishedFogRegion->field;auto region=std::make_shared<WorldRenderer::FogRegion>(*w.publishedFogRegion);region->field=field;w.publishedFogRegion=region;return old;
    }
    static void reset(WorldRenderer& w){w.releaseGPU(true);assert(w.publishedFogRegion&&!w.uploadedFogField);}
};
static float half(unsigned short h){unsigned e=h>>10&31,m=h&1023;float v=e==0?std::ldexp(float(m),-24):e==31?(m?NAN:INFINITY):std::ldexp(float(1024+m),int(e)-25);return h&32768?-v:v;}
static IDirect3DTexture9* texture(IDirect3DDevice9* d,unsigned width,unsigned height,const float* value,unsigned channels){
    IDirect3DTexture9* result=nullptr;assert(SUCCEEDED(d->CreateTexture(width,height,1,0,channels==1?D3DFMT_R32F:D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&result,nullptr)));
    D3DLOCKED_RECT lock{};assert(SUCCEEDED(result->LockRect(0,&lock,nullptr,0)));for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*channels*4,value,channels*4);
    assert(SUCCEEDED(result->UnlockRect(0)));return result;
}
int main(int argc,char** argv){
    assert(argc==4);constexpr unsigned Width=256,Height=160;const unsigned samples=unsigned(std::atoi(argv[3]));
    const std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightFogRecovery";assert(RegisterClassW(&wc));
    HWND window=CreateWindowW(wc.lpszClassName,L"Private fog recovery",WS_OVERLAPPEDWINDOW,0,0,Width,Height,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);IDirect3D9* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=Width;pp.BackBufferHeight=Height;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    IDirect3DDevice9* device=nullptr;assert(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&device)));
    IDirect3DSurface9* target=nullptr;assert(SUCCEEDED(device->CreateRenderTarget(Width,Height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr)));
    const float color[4]={.35f,.3f,.25f,1},ambient[4]={0,0,0,1},depthValue[1]={.9f};auto* scene=texture(device,Width,Height,color,4);auto* ao=texture(device,Width/2,Height/2,ambient,4);auto* depth=texture(device,Width,Height,depthValue,1);
    {
        WorldRenderer world(device);WorldScreenEffectsTest::setup(world);std::vector<unsigned char> images[2];
        for(unsigned direct=0;direct<2;++direct){
            if(direct)WorldScreenEffectsTest::reset(world);WorldScreenEffectsTest::source(world,direct!=0);
            assert(SUCCEEDED(device->SetRenderTarget(0,target)));assert(SUCCEEDED(device->SetDepthStencilSurface(nullptr)));assert(SUCCEEDED(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE)));
            assert(SUCCEEDED(device->BeginScene()));assert(world.render(target,depth,Width,Height,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));assert(SUCCEEDED(device->EndScene()));images[direct]=WorldScreenEffectsTest::fog(world);
            IDirect3DSurface9* restored=nullptr;assert(SUCCEEDED(device->GetRenderTarget(0,&restored))&&restored==target);restored->Release();DWORD blend=TRUE;assert(SUCCEEDED(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))&&!blend);
        }
        assert(images[0]==images[1]);float scatter=0,transmission=1;
        for(size_t i=0;i<images[0].size()/2;++i){unsigned short bits;std::memcpy(&bits,images[0].data()+i*2,2);const float value=half(bits);assert(std::isfinite(value)&&value>=0);if(i%4==3)transmission=std::min(transmission,value);else scatter=std::max(scatter,value);}
        assert(scatter>.00001f&&transmission<.999f);
        auto render=[&]{assert(SUCCEEDED(device->BeginScene()));assert(world.render(target,depth,Width,Height,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));assert(SUCCEEDED(device->EndScene()));};
        for(float altitude:{300.f,-300.f}){
            std::vector<unsigned char> empty[2],history[2],reentry[2];
            for(unsigned clipped=0;clipped<2;++clipped){
                WorldScreenEffectsTest::source(world,true);WorldScreenEffectsTest::altitude(world,0,true);render();
                WorldScreenEffectsTest::altitude(world,altitude,clipped!=0);render();render();
                empty[clipped]=WorldScreenEffectsTest::fog(world);history[clipped]=WorldScreenEffectsTest::fog(world,true);
                WorldScreenEffectsTest::altitude(world,altitude+1,false);render();
                WorldScreenEffectsTest::altitude(world,0,true);render();reentry[clipped]=WorldScreenEffectsTest::fog(world,true);
            }
            assert(empty[0]==empty[1]&&history[0]==history[1]&&reentry[0]==reentry[1]);
            for(size_t i=0;i<empty[1].size()/2;++i){unsigned short bits;std::memcpy(&bits,empty[1].data()+i*2,2);assert(bits==(i%4==3?0x3c00:0));}
        }
        std::vector<unsigned char> neutral[2],reentry[2];
        for(unsigned clipped=0;clipped<2;++clipped){
            auto empty=std::make_shared<NorthlightRegionalFog::Field>();empty->fogCells=clipped?0:1;
            auto old=WorldScreenEffectsTest::field(world,empty);render();neutral[clipped]=WorldScreenEffectsTest::fog(world,true);
            WorldScreenEffectsTest::field(world,old);render();reentry[clipped]=WorldScreenEffectsTest::fog(world,true);
        }
        assert(neutral[0]==neutral[1]&&reentry[0]==reentry[1]);
        std::puts("FOG_EMPTY_SUPPORT_PASS aboveBelow=1 unknownBounds=1 emptyField=1 historyExact=1 reentryExact=1");
        std::printf("FOG_RECOVERY_GPU_PASS msaa=%u noBVH=1 pointShadowsRequested=1 geometryConsumersInactive=1 independentUpload=1 resetRestored=1 unshadowedDirectDisabled=1 stateRestored=1 scatter=%g transmission=%g\n",samples,scatter,transmission);
    }
    scene->Release();ao->Release();depth->Release();target->Release();device->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
