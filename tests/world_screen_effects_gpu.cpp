// Production screen effects must survive an unavailable geometry publication.
#undef NDEBUG
#define NORTHLIGHT_REPLAY_STAND 1
#include "../src/proxy/renderer.cpp"
#include <cassert>
#include <fstream>
struct WorldScreenEffectsTest {
    static void sceneOwnership(WorldRenderer& w,IDirect3DDevice9* d){
        auto scene=w.gpuScene;const auto originalLimit=w.captureSources().sharedSourceLimit();
        assert(scene->device==d&&scene->decodedSources&&scene->captureSources==w.captureSources().sharedSources());
        assert(&w.staticCasters==&scene->staticCasters&&&w.meshStore==&scene->meshStore&&&w.meshBuffers==&scene->meshBuffers&&&w.liveTerrainGPU==&scene->terrain&&&w.terrainBoundsCache==&scene->terrainSources);
        struct Views {IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;NorthlightReplayGPU::Placement at;~Views(){for(auto& p:vb)drop(p);drop(ib);}} material,shadow;
        auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->streams[0].stride=12;mesh->streams[0].bytes.resize(36);mesh->indices={0,1,2};mesh->vertexCount=3;mesh->primitiveCount=1;
        mesh->sourceIdentity=NorthlightSourceIdentity::make({0x7363656e65,1});const auto bytes=mesh->byteSize();
        auto admit=[](size_t){return true;};
        {
            NorthlightAutoBump materials(d,scene);
            assert(scene->normalMaps&&scene->displacement&&materials.captureSources().sharedSources()==scene->captureSources);
            assert(materials.captureSources().sharedSourceParticipants()==3&&materials.captureSources().sharedSourceLimit()==originalLimit+(48u<<20));
            scene->setFrame(20);assert(!scene->materials.bind(d,mesh,material.vb,material.ib,admit,material.at));
            scene->setFrame(21);assert(!scene->materials.bind(d,mesh,material.vb,material.ib,admit,material.at));assert(scene->materials.commitPending());
            assert(scene->materials.bind(d,mesh,material.vb,material.ib,admit,material.at)&&scene->shadows.bind(d,mesh,shadow.vb,shadow.ib,admit,shadow.at));
            assert(scene->native.bytes()==bytes&&material.vb[0]==shadow.vb[0]&&material.ib==shadow.ib);
            materials.reset();assert(!scene->materials.bytes()&&scene->shadows.bytes()==bytes&&scene->native.bytes()==bytes);
            auto reconstructed=std::make_shared<NorthlightDrawSnapshot::Mesh>(*mesh);mesh.reset();
            assert(scene->shadows.bind(d,reconstructed,shadow.vb,shadow.ib,admit,shadow.at)&&shadow.vb[0]==material.vb[0]);
        }
        assert(w.captureSources().sharedSourceLimit()==originalLimit&&w.captureSources().sharedSourceParticipants()==1);
        assert(scene->native.bytes()==bytes);scene->shadows.clear();assert(!scene->native.bytes()&&!scene->native.chargedBytes());
        bool rejected=false;try{NorthlightGpuScene::forDevice(reinterpret_cast<IDirect3DDevice9*>(uintptr_t(1)),scene);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
        std::puts("GPU_SCENE_OWNERSHIP_PASS actual_world_materials_displacement one_source_bank unchanged_caps one_upload consumer_reset reconstructed_source typed_views foreign_device_rejected");
    }
    static void setup(WorldRenderer& w,unsigned width,unsigned height){
        assert(w.resources(width,height,D3DFMT_A16B16G16R16F));
        assert(!w.color&&!w.colorSurface); // No consumer has requested full-size scratch.
        w.valid=true;w.frames=599;NorthlightDiagnostics::configure(true);w.lastRequest.map="Northrend";
        for(unsigned i=0;i<16;++i)w.context.view[i]=w.context.inverseView[i]=(i%5==0)?1.f:0.f;
        for(unsigned i=0;i<3;++i){w.context.camera[i]=0;w.context.direct[i]=.6f;w.context.ambient[i]=.25f;}
        w.context.lightDirection[0]=.4f;w.context.lightDirection[1]=.2f;w.context.lightDirection[2]=-.8944272f;
        w.projection[0]=w.projection[1]=w.projection[2]=1;
        w.sourceDirections[0]={.4f,.2f,-.8944272f};w.sourceDirections[1]={0,0,1};
        w.sourceColors[0]=w.sourceGlowColors[0]={.6f,.5f,.4f};w.sourceColors[1]=w.sourceGlowColors[1]={0,0,0};w.sourceWeights[0]=1;w.sourceWeights[1]=0;
        w.quality.equipmentLights=0;w.quality.horizonHaze=0;w.quality.fogClouds=0;
        w.effects.gi=true;w.effects.shadows=true;
        auto field=std::make_shared<NorthlightRegionalFog::Field>();field->originX=field->originY=-256;field->fogCells=field->airCells=4096;
        for(auto& t:field->texels){t.ground=-2;t.day=.015f;t.height=2.5f;}
        auto region=std::make_shared<WorldRenderer::FogRegion>();region->map=w.lastRequest.map;region->center={0,0,0};region->field=field;
        {std::lock_guard<std::mutex> lock(w.mutex);w.publishedFogRegion=std::move(region);}
        assert(!w.uploadedFogField&&!w.regionalFogTexture); // render() must upload without an active BVH
        assert(!w.ready()&&w.screenReady()&&!w.active&&!w.vertices&&!w.indices);
    }
    static void consumers(WorldRenderer& w){
        w.quality.nearShadowInterval=w.quality.farShadowInterval=1;
        w.captureDemand=false;w.actorCaptureDecided=true;w.lastRenderDebug=0;
        for(unsigned frame=0;frame<1000;++frame){
            w.captureMode=WorldRenderer::CaptureUndecided;w.actorCaptureDue=frame%20==0;
            assert(w.modelCaptureSkipped()==!w.actorCaptureDue);
        }
        w.actorCaptureDue=false;w.captureMode=WorldRenderer::CaptureUndecided;w.captureDemand=true;assert(!w.modelCaptureSkipped());
        w.captureMode=WorldRenderer::CaptureUndecided;w.lastRenderDebug=1;assert(!w.modelCaptureSkipped());w.lastRenderDebug=0;
        w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->bvh=std::make_shared<NorthlightGI::BVH>();
        w.captureMode=WorldRenderer::CaptureUndecided;assert(w.ready()&&!w.modelCaptureSkipped());w.active.reset();
        w.captureMode=WorldRenderer::CaptureUndecided;w.actorCaptureDecided=false;
        std::puts("WORLD_SCREEN_CONSUMERS_PASS frames=1000 GI=50 unusedShadowCaptures=0 demand=preserved diagnostic=preserved readyGeometry=preserved");
    }
    static void fog(WorldRenderer& w,bool on){w.effects.fog=on;w.fogHistoryValid=false;}
    static bool composed(const WorldRenderer& w){return w.composited&&!w.shadowsComposited&&!w.shadowFrameReady;}
    static void scratch(const WorldRenderer& w,bool expected){assert(bool(w.color)==expected&&bool(w.colorSurface)==expected);}
    static void direct(WorldRenderer& w,bool on){w.sourceColors[0]=on?WorldRenderer::V(.6f,.5f,.4f):WorldRenderer::V();w.sourceColors[1]={};}
    static void resetResources(WorldRenderer& w){w.releaseGPU(true);assert(w.publishedFogRegion&&!w.uploadedFogField&&!w.active);}
    static void diagnosticFrames(WorldRenderer& w,unsigned width,unsigned height){
        const auto directory=NorthlightWorldDiagnostics::directory(w.root);assert(!directory.empty());
        auto pixels=[&](const std::string& folder,const char* name,unsigned capture,bool zero){
            std::ifstream f(folder+"/capture-"+std::to_string(capture)+"-"+name+".fgr",std::ios::binary);assert(f);
            uint32_t header[5];f.read(reinterpret_cast<char*>(header),sizeof header);assert(header[0]==0x31524746&&header[1]&&header[2]);
            std::vector<unsigned char> bytes(size_t(header[1])*header[2]*header[4]);f.read(reinterpret_cast<char*>(bytes.data()),bytes.size());assert(f&&f.peek()==EOF);
            const bool empty=std::all_of(bytes.begin(),bytes.end(),[](auto byte){return byte==0;});assert(empty==zero);
        };
        w.captureProducts(directory,1,900,width,height,true,true);pixels(directory,"fog",1,false);
        const auto before=fogImage(w);
        // Populated targets are deliberately retained, but the next frame has
        // no producer. Export zeros without any device access or target clear.
        assert(NorthlightWorldDiagnostics::product(nullptr,w.fogSurface,directory,"ao-raw",2,false,width/2,height/2,D3DFMT_A16B16G16R16F,901));
        assert(NorthlightWorldDiagnostics::textureProduct(nullptr,nullptr,directory,"materials-normal-height",2,false,width,height,D3DFMT_A16B16G16R16F,901));
        pixels(directory,"ao-raw",2,true);pixels(directory,"materials-normal-height",2,true);assert(before==fogImage(w));
        w.captureProducts(directory,2,901,width,height,false);pixels(directory,"fog",2,true);pixels(directory,"normals",2,true);
        w.composited=false;assert(w.requestNormalGpuDiagnosticCapture());assert(w.captureSkippedDiagnostic(902,width,height));
        assert(w.diagnosticCaptureInLastRender()&&!w.gpuNormalDiagnosticRequested);pixels(w.gpuDiagnosticDirectory,"fog",1,true);pixels(w.gpuDiagnosticDirectory,"scene",1,true);
        NorthlightDiagnosticCapture::request(NorthlightDiagnosticCapture::AO);NorthlightDiagnosticCapture::request(NorthlightDiagnosticCapture::Materials);
        assert(NorthlightDiagnosticCapture::take()==(NorthlightDiagnosticCapture::AO|NorthlightDiagnosticCapture::Materials)&&!NorthlightDiagnosticCapture::take());
        std::puts("DIAGNOSTIC_FRAME_PASS staleTargetsRetained=1 skippedImagesZero=1 noDeviceAccessForEmpty=1 worldSkippedCapture=1 scopeQueue=1");
    }
    static void emptyDebugViews(WorldRenderer& world,IDirect3DSurface9* out,IDirect3DSurface9* resolved,IDirect3DSurface9* read,IDirect3DTexture9* depth,unsigned width,unsigned height){
        for(unsigned debug:{1u,2u,3u,1u,2u}){
            world.effects.shadows=world.effects.gi=debug<3?false:true;world.effects.fog=false;
            if(debug<3&&resolved==nullptr)world.effects.shadows=world.effects.gi=true; // also exercise missing geometry with enabled components
            assert(SUCCEEDED(world.d->ColorFill(out,nullptr,0xffffffff)));
            assert(world.render(out,depth,width,height,D3DFMT_A16B16G16R16F,1,1000,0,1,debug,nullptr,nullptr));
            if(resolved)assert(SUCCEEDED(world.d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE)));
            assert(SUCCEEDED(world.d->GetRenderTargetData(resolved?resolved:out,read)));D3DLOCKED_RECT lock{};assert(SUCCEEDED(read->LockRect(&lock,nullptr,D3DLOCK_READONLY)));
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width*8;++x)assert(static_cast<const unsigned char*>(lock.pBits)[y*lock.Pitch+x]==0);
            assert(SUCCEEDED(read->UnlockRect()));
        }
        std::puts("DIAGNOSTIC_VIEW_PASS disabledComponentsAndMissingGeometryBlack=1 noStaleImage=1");
    }
    static std::vector<unsigned char> fogImage(WorldRenderer& w){
        assert(w.uploadedFogField==w.publishedFogRegion->field&&!w.active&&!w.fogGeometryReady);
        D3DSURFACE_DESC desc{};assert(SUCCEEDED(w.fogSurface->GetDesc(&desc)));
        IDirect3DSurface9* read=nullptr;assert(SUCCEEDED(w.d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&read,nullptr)));
        assert(SUCCEEDED(w.d->GetRenderTargetData(w.fogSurface,read)));D3DLOCKED_RECT lock{};assert(SUCCEEDED(read->LockRect(&lock,nullptr,D3DLOCK_READONLY)));
        std::vector<unsigned char> image(size_t(desc.Width)*desc.Height*8);
        for(unsigned y=0;y<desc.Height;++y)std::memcpy(image.data()+size_t(y)*desc.Width*8,static_cast<const char*>(lock.pBits)+y*lock.Pitch,desc.Width*8);
        assert(SUCCEEDED(read->UnlockRect()));read->Release();return image;
    }
};
struct WorldScratchMeter:ForwardIDirect3DDevice9 {
    unsigned width,height,creates=0;
    WorldScratchMeter(IDirect3DDevice9* d,unsigned w,unsigned h):ForwardIDirect3DDevice9(d),width(w),height(h){}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** t,HANDLE* shared)override{
        if(w==width&&h==height&&(usage&D3DUSAGE_RENDERTARGET)&&format==D3DFMT_A16B16G16R16F)++creates;
        return real->CreateTexture(w,h,levels,usage,format,pool,t,shared);
    }
};
static void screenOk(HRESULT h){if(FAILED(h)){std::fprintf(stderr,"screen HRESULT=%08lx\n",(unsigned long)h);std::abort();}}
static float screenHalf(unsigned short h){unsigned e=h>>10&31,m=h&1023;float v=e==0?std::ldexp(float(m),-24):e==31?(m?NAN:INFINITY):std::ldexp(float(1024+m),int(e)-25);return h&32768?-v:v;}
static IDirect3DTexture9* screenTexture(IDirect3DDevice9* d,unsigned w,unsigned h,const float* value,unsigned channels=4){
    IDirect3DTexture9* t=nullptr;screenOk(d->CreateTexture(w,h,1,0,channels==4?D3DFMT_A32B32G32R32F:D3DFMT_R32F,D3DPOOL_MANAGED,&t,nullptr));D3DLOCKED_RECT l{};screenOk(t->LockRect(0,&l,nullptr,0));
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)std::memcpy(static_cast<char*>(l.pBits)+y*l.Pitch+x*channels*4,value,channels*4);screenOk(t->UnlockRect(0));return t;
}
int main(int argc,char** argv){
    assert(argc==3);constexpr unsigned W=256,H=160;const unsigned msaa=std::getenv("NORTHLIGHT_TEST_MSAA")?unsigned(std::atoi(std::getenv("NORTHLIGHT_TEST_MSAA"))):0;assert(msaa==0||msaa==2);
    std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightScreenEffectsOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *out=nullptr,*readback=nullptr,*resolved=nullptr;screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&out,nullptr));screenOk(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    if(msaa)screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
    // Capture stores positive-hemisphere normal detail relative to its reference.
    const float rgb[]={.4f,.4f,.4f,1},ambient[]={0,0,0,1},normal[]={.7f,.6f,20,1},z[]={1000.f/999.f-1000.f/(999.f*20)};
    auto* scene=screenTexture(d,W,H,rgb);auto* ao=screenTexture(d,W/2,H/2,ambient);auto* detail=screenTexture(d,W,H,normal);auto* depth=screenTexture(d,W,H,z,1);
    std::vector<float> images[4];
    {
        WorldScratchMeter meter(d,W,H);
        auto world=std::make_unique<WorldRenderer>(&meter);WorldScreenEffectsTest::sceneOwnership(*world,&meter);WorldScreenEffectsTest::setup(*world,W,H);WorldScreenEffectsTest::consumers(*world);
        for(unsigned mode=0;mode<4;++mode){
            WorldScreenEffectsTest::fog(*world,mode>=2);world->setMaterialDetail(mode&1?detail:nullptr,1,48,nullptr,8);
            screenOk(d->SetRenderTarget(0,out));screenOk(d->SetDepthStencilSurface(nullptr));screenOk(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            assert(world->render(out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,mode));assert(WorldScreenEffectsTest::composed(*world));
            WorldScreenEffectsTest::scratch(*world,bool(mode&1));assert(meter.creates==(mode+1)/2);
            if(msaa)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));screenOk(d->GetRenderTargetData(msaa?resolved:out,readback));D3DLOCKED_RECT l{};screenOk(readback->LockRect(&l,nullptr,D3DLOCK_READONLY));
            auto& image=images[mode];image.resize(W*H*3);
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned k=0;k<3;++k){unsigned short h;std::memcpy(&h,static_cast<char*>(l.pBits)+y*l.Pitch+x*8+k*2,2);float value=screenHalf(h);assert(std::isfinite(value)&&value>=0);image[(y*W+x)*3+k]=value;}
            screenOk(readback->UnlockRect());
            IDirect3DSurface9* restored=nullptr;screenOk(d->GetRenderTarget(0,&restored));assert(restored==out);restored->Release();DWORD blend=TRUE;screenOk(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend));assert(!blend);
            if(mode==1){
                // A camera turn may hide all captures without disabling materials.
                world->setMaterialDetail(nullptr,1,48,nullptr,8,true);
                assert(world->render(out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
                WorldScreenEffectsTest::scratch(*world,true);assert(meter.creates==1);
            }
        }
        // The unfurled legacy path still gets scratch; the next folded frame retires it.
        WorldScreenEffectsTest::fog(*world,false);world->setMaterialDetail(nullptr,0,48);
        for(bool folded:{false,true}){
            screenOk(d->SetRenderTarget(0,out));screenOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff666666,1,0));
            assert(world->render(out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,folded?scene:nullptr,ao));
            WorldScreenEffectsTest::scratch(*world,!folded);assert(meter.creates==2);
        }
        std::puts("WORLD_SCRATCH_PASS folded_off=0 materials_on=1 invisible_reused=1 legacy_on=1 retired_on_disable=1");
        std::vector<unsigned char> ambientOnly[2];
        for(unsigned direct=0;direct<2;++direct){
            if(direct)WorldScreenEffectsTest::resetResources(*world);
            WorldScreenEffectsTest::direct(*world,direct!=0);WorldScreenEffectsTest::fog(*world,true);
            screenOk(d->SetRenderTarget(0,out));
            assert(world->render(out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
            ambientOnly[direct]=WorldScreenEffectsTest::fogImage(*world);
        }
        assert(ambientOnly[0]==ambientOnly[1]); // missing shadow geometry disables direct shafts, never ambient extinction
        float maximumScatter=0,minimumTransmission=1;
        for(size_t pixel=0;pixel<ambientOnly[0].size()/8;++pixel)for(unsigned channel=0;channel<4;++channel){unsigned short bits;std::memcpy(&bits,ambientOnly[0].data()+pixel*8+channel*2,2);const float value=screenHalf(bits);assert(std::isfinite(value)&&value>=0);if(channel<3)maximumScatter=std::max(maximumScatter,value);else minimumTransmission=std::min(minimumTransmission,value);}
        assert(maximumScatter>.00001f&&minimumTransmission<.999f);
        std::printf("WORLD_AMBIENT_FOG_PASS noBvh=1 independentUpload=1 resetRestored=1 sunZero=1 moonZero=1 scatter=%g transmission=%g unshadowedDirectDisabled=1 msaa=%u\n",maximumScatter,minimumTransmission,msaa);
        WorldScreenEffectsTest::diagnosticFrames(*world,W,H);
        WorldScreenEffectsTest::emptyDebugViews(*world,out,resolved,readback,depth,W,H);
    }
    float bump=0,fog=0;for(unsigned i=0;i<W*H*3;++i){bump=std::max(bump,std::abs(images[1][i]-images[0][i]));fog=std::max(fog,std::abs(images[2][i]-images[0][i]));}
    std::printf("WORLD_SCREEN_GPU bumpDelta=%g fogDelta=%g noGeometry=1 noStaleShadows=1 stateRestored=1 msaa=%u\n",bump,fog,msaa);std::fflush(stdout);assert(bump>.002f&&fog>.002f);
    scene->Release();ao->Release();detail->Release();depth->Release();out->Release();if(resolved)resolved->Release();readback->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::puts("WORLD_SCREEN_GPU_PASS");
}
