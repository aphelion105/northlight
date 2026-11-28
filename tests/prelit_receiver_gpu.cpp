// Real shader registration followed by the production SDR/HDR compositor.
#define NORTHLIGHT_PROBE_ATLAS_OWNER_TEST 1
#define NORTHLIGHT_REGIONAL_FOG_GPU_TEST 1
#define NORTHLIGHT_SHADOW_PROGRAM_OWNER_TEST 1
#define NORTHLIGHT_WORLD_RENDER_TARGET_OWNER_TEST 1
#define main unused_screen_effects_main
#include "world_screen_effects_gpu.cpp"
#undef main
#include "lighting_pair_timing.h"

struct PipelineOffTest {
    static void unmarkRunPixels(Device& device,unsigned width,unsigned height){
        auto* d=device.ext;
        IDirect3DSurface9 *target=nullptr,*depth=nullptr,*readback=nullptr;
        screenOk(d->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
        screenOk(d->CreateDepthStencilSurface(width,height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
        screenOk(d->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));
        {
            SavedState saved(d,&device.stateBlocks);
            assert(saved.ok);
            screenOk(d->SetDepthStencilSurface(nullptr));
            screenOk(d->SetRenderTarget(0,target));screenOk(d->SetDepthStencilSurface(depth));
            screenOk(d->SetVertexShader(nullptr));screenOk(d->SetPixelShader(nullptr));
            screenOk(d->SetTexture(0,nullptr));screenOk(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
            screenOk(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
            screenOk(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
            screenOk(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
            screenOk(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
            screenOk(d->SetRenderState(D3DRS_ZENABLE,FALSE));
            screenOk(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
            screenOk(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            constexpr D3DCOLOR green=0xff00ff00;
            const auto strip=[&](unsigned first,unsigned last){
                const float x0=float(first)-.5f,x1=float(last)-.5f,y0=-.5f,y1=float(height)-.5f;
                struct Vertex {float x,y,z,w;DWORD color;};
                const Vertex vertices[]={{x0,y0,0,1,green},{x1,y0,0,1,green},{x0,y1,0,1,green},{x1,y1,0,1,green}};
                return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));
            };
            assert(device.setExclusiveOwner(true));
            {
                MirrorGuard held(device.mirrorState.gate);
                for(bool reuse:{false,true}){
                    device.unmarkRunReuse=reuse;
                    screenOk(d->SetRenderState(D3DRS_STENCILENABLE,FALSE));
                    screenOk(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_STENCIL,0xff000000,1,NorthlightNameStencil::kBit));
                    screenOk(d->SetRenderState(D3DRS_COLORWRITEENABLE,0));
                    const auto renderUnmark=[&](unsigned start,unsigned end){
                        device.maskKind=2;
                        screenOk(device.nameMaskDraw(false,false,[&]{return strip(start,end);}));
                    };
                    screenOk(d->BeginScene());
                    renderUnmark(0,width/4);renderUnmark(width/4,width/2);
                    DWORD stencil=TRUE;screenOk(d->GetRenderState(D3DRS_STENCILENABLE,&stencil));assert(!stencil);
                    screenOk(strip(width/2,width));
                    assert(!device.mirrorState.deferredUnmarkCount);
                    screenOk(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
                    screenOk(d->SetRenderState(D3DRS_STENCILENABLE,TRUE));
                    screenOk(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL));
                    screenOk(d->SetRenderState(D3DRS_STENCILREF,NorthlightNameStencil::kBit));
                    screenOk(d->SetRenderState(D3DRS_STENCILMASK,NorthlightNameStencil::kBit));
                    screenOk(d->SetRenderState(D3DRS_STENCILWRITEMASK,0));
                    screenOk(strip(0,width));
                    screenOk(d->EndScene());
                    screenOk(d->GetRenderTargetData(target,readback));
                    D3DLOCKED_RECT lock{};screenOk(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
                    const auto* row=reinterpret_cast<const DWORD*>(static_cast<const char*>(lock.pBits)+height/2*lock.Pitch);
                    assert((row[width/8]&0xffffff)==0&&(row[3*width/8]&0xffffff)==0&&(row[3*width/4]&0xffffff)==0xff00);
                    screenOk(readback->UnlockRect());
                }
                assert(device.mirrorState.unmarkRunsReused==1&&!device.mirrorState.unmarkRestoreFailures);
                device.unmarkRunReuse=false;
            }
            assert(device.setExclusiveOwner(false));
        }
        readback->Release();depth->Release();target->Release();
        std::puts("UNMARK_RUN_GPU_PASS consecutive_draw_pixels ordinary_draw_stencil restored");
    }
    static void layeredRoom(Device& device,const std::vector<DWORD>& words,unsigned width,unsigned height,
                            bool model=false,bool skinned=true,bool preserveRigidRoomLight=true){
        IDirect3DVertexShader9* shader=nullptr;IDirect3DSurface9* depth=nullptr;
        screenOk(device.CreateVertexShader(words.data(),&shader));
        screenOk(device.CreateDepthStencilSurface(width,height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
        screenOk(device.SetDepthStencilSurface(depth));screenOk(device.SetVertexShader(shader));
        drop(device.worldDepth);device.worldDepth=device.mirrorResources.unwrap(depth);device.worldDepth->AddRef();
        screenOk(device.worldDepth->GetDesc(&device.worldDepthDesc));device.worldDepthDescKnown=true;
        device.width=width;device.height=height;device.worldMinDepth=0;device.worldMaxDepth=.94f;
        device.terrain=device.projectionValid=true;device.applied=false;device.menuScene=false;
        device.nameMask.cleared=true;device.aoActorOn=false;
        auto& world=*device.world;lighting(world,true);world.legacyFogKnown=true;
        const auto previousPreserve=world.quality.giPreserveRigidRoomLight;
        world.quality.giPreserveRigidRoomLight=preserveRigidRoomLight;
        const float room[]={.43921572f,.41960788f,.47450984f,0,0,0,0,0};
        const float playerRoom[]={.50588214f,.53333312f,.65882325f,0,.28235301f,.29803929f,.36862752f,0};
        const float innRoom[]={.659f,.604f,.612f,0,.376f,.345f,.349f,0};
        constexpr unsigned sharedTint=NorthlightUnlitReceiver::RoomSharedTint;
        assert(NorthlightUnlitReceiver::roomAmbientLevel(innRoom)==sharedTint);
        const float tinted[]={0,1,0,0,1,0,1,0};
        assert(NorthlightUnlitReceiver::roomAmbientLevel(tinted)==0);
        const float forestAmbient[]={.03922f,.20392f,.34510f};
        const float forestDirect[]={.33725f,.44706f,.53333f};
        const float scaledDirect[]={.16863f,.22353f,.26667f,0,.03922f,.20392f,.34510f,0};
        const float scaledAmbient[]={.33725f,.44706f,.53333f,0,.04403f,.22896f,.38746f,0};
        assert(!NorthlightUnlitReceiver::lightingOverride(scaledDirect,forestAmbient,forestDirect,true));
        assert(!NorthlightUnlitReceiver::lightingOverride(scaledAmbient,forestAmbient,forestDirect,true));
        assert(NorthlightUnlitReceiver::lightingOverride(scaledDirect,forestDirect,forestAmbient));
        const float rugAmbient[]={.23529f,.28627f,.33333f};
        const float rugDirect[]={.15686f,.22353f,.29020f};
        const float rugRoom[]={.68235f,.58039f,.50980f,0,.37647f,.31765f,.28235f,0};
        assert(NorthlightUnlitReceiver::lightingOverride(rugRoom,rugAmbient,rugDirect,true));
        float outdoor[8]{};
        std::copy_n(model?world.context.direct:world.context.ambient,3,outdoor);
        std::copy_n(model?world.context.ambient:world.context.direct,3,outdoor+4);
        bool worldBlendPair=!model;
        if(model){
            const auto& receiver=device.classifyVs(device.mirrorResources.unwrap(shader));
            assert(receiver.family==3&&receiver.skinned==skinned);
            // A rigid doodad program passes the WMO coordinate proof and so
            // shares the WMO INVSRCALPHA/ONE layer admission.
            assert(receiver.wmo==!skinned);worldBlendPair=receiver.wmo;
        }
        const bool probeRoom=model&&(skinned||!preserveRigidRoomLight);
        D3DVIEWPORT9 vp{0,0,width,height,0,.94f};screenOk(device.SetViewport(&vp));
        screenOk(device.SetRenderState(D3DRS_ZENABLE,TRUE));screenOk(device.SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
        screenOk(device.SetRenderState(D3DRS_STENCILENABLE,FALSE));
        // Captured WMO pairs use SRCALPHA/ZERO, then INVSRCALPHA/ONE.
        // The authored room bank can be on either layer. Exercise the real
        // draw admission and scoped stencil states, not a prefilled mask.
        for(unsigned variant=0;variant<7;++variant){
            screenOk(device.SetVertexShaderConstantF(10,variant==6?outdoor:model?playerRoom:room,2));
            screenOk(device.SetRenderState(D3DRS_ZWRITEENABLE,variant==3?FALSE:TRUE));
            screenOk(device.SetRenderState(D3DRS_ALPHABLENDENABLE,model&&variant!=1?FALSE:TRUE));
            screenOk(device.SetRenderState(D3DRS_SRCBLEND,variant==0?D3DBLEND_SRCALPHA:variant==4?D3DBLEND_SRCCOLOR:D3DBLEND_INVSRCALPHA));
            screenOk(device.SetRenderState(D3DRS_DESTBLEND,variant==0?D3DBLEND_ZERO:D3DBLEND_ONE));
            screenOk(device.SetRenderState(D3DRS_BLENDOP,variant==2?D3DBLENDOP_REVSUBTRACT:D3DBLENDOP_ADD));
            screenOk(device.SetRenderState(D3DRS_COLORWRITEENABLE,variant==5?0:15));
            device.prepareDrawImpl([](IDirect3DVertexShader9*){},1);
            std::printf("ROOM_LAYER_ADMISSION variant=%u mask=%d\n",variant,device.maskKind);
            const bool accepted=model?(variant==0||variant==2||variant==4||(variant==1&&worldBlendPair)):variant<2;
            if(accepted)assert(device.maskKind==(probeRoom?5+sharedTint:5));
            else assert(device.maskKind<4);
            DWORD previous=0;screenOk(device.GetRenderState(D3DRS_STENCILENABLE,&previous));
            bool called=false;screenOk(device.nameMaskDraw(false,false,[&]{
                called=true;DWORD on=0,ref=0;screenOk(device.ext->GetRenderState(D3DRS_STENCILENABLE,&on));
                screenOk(device.ext->GetRenderState(D3DRS_STENCILREF,&ref));
                if(accepted)assert(on&&ref==(probeRoom?NorthlightUnlitReceiver::RoomRef[sharedTint]:NorthlightUnlitReceiver::AuthoredBit));return S_OK;
            }));assert(called);
            DWORD restored=0;screenOk(device.GetRenderState(D3DRS_STENCILENABLE,&restored));assert(restored==previous);
        }
        screenOk(device.SetVertexShader(nullptr));screenOk(device.SetDepthStencilSurface(nullptr));
        drop(device.worldDepth);depth->Release();shader->Release();
        world.quality.giPreserveRigidRoomLight=previousPreserve;
        if(model&&!skinned)std::printf("RIGID_M2_ROOM_ADMISSION_PASS preserve=%d probeMask=%d\n",preserveRigidRoomLight,probeRoom);
        else std::puts(model?"TOWER_PLAYER_ROOM_ADMISSION_PASS posedM2=1 probeMask=1 outdoorUnchanged=1":
                             "ROOM_LAYER_ADMISSION_PASS capturedBlendPair=1 scopedStates=1 negativeControls=5");
    }
    static __attribute__((noinline)) bool receiver(WorldRenderer& world,IDirect3DVertexShader9* shader,bool model){return world.authoredReceiver(shader,model);}
    static bool prelit(Device& device,const std::vector<DWORD>& words){
        IDirect3DVertexShader9* shader=nullptr;
        screenOk(device.CreateVertexShader(words.data(),&shader));
        const auto entry=device.classifyVs(device.mirrorResources.unwrap(shader)).entry;
        shader->Release();return (entry&Device::kPrelitTag)!=0;
    }
    static void overrideRegistration(Device& device,const std::vector<DWORD>& words,bool model=false){
        IDirect3DVertexShader9* shader=nullptr;screenOk(device.CreateVertexShader(words.data(),&shader));
        auto* original=device.mirrorResources.unwrap(shader);auto& world=*device.world;
        lighting(world,true);
        assert((device.classifyVs(original).family==3)==model);
        float room[]={.439216f,.419608f,.47451f,0,0,0,0,0};
        if(model){const float captured[]={.9999994f,.8666662f,.4784311f,0,.3764707f,.3254903f,.1803922f,0};std::copy_n(captured,8,room);}
        screenOk(device.SetVertexShaderConstantF(10,room,2));
        assert(world.authoredReceiver(original,model));
        unsigned roomLevel=4;
        assert(world.authoredReceiver(original,model,&roomLevel)&&roomLevel==NorthlightUnlitReceiver::roomAmbientLevel(room));
        float outdoor[8]{};std::copy_n(model?world.context.direct:world.context.ambient,3,outdoor);std::copy_n(model?world.context.ambient:world.context.direct,3,outdoor+4);
        screenOk(device.SetVertexShaderConstantF(10,outdoor,2));assert(!world.authoredReceiver(original,model));
        // A new context can change the classification with the same native bank.
        const float previous=world.context.ambient[0];
        world.context.ambient[0]+=.25f;++world.materialLightRevision;
        assert(world.authoredReceiver(original,model));
        world.context.ambient[0]=previous;++world.materialLightRevision;
        assert(!world.authoredReceiver(original,model));
        if(std::getenv("NORTHLIGHT_LIGHTING_BENCH"))for(bool indoor:{false,true}){
            screenOk(device.SetVertexShaderConstantF(10,indoor?room:outdoor,2));volatile unsigned hits=0;
            const auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<1000000;++i)hits+=receiver(world,original,model);
            const double perThousand=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/1000;
            assert(hits==(indoor?1000000u:0u));
            std::printf("AUTHORED_CLASSIFY_COST model=%u indoor=%u draws=1000000 cpuMsPerThousand=%.6f\n",unsigned(model),unsigned(indoor),perThousand);
        }
        shader->Release();
    }
    static void lighting(WorldRenderer& world,bool occluded){
        WorldScreenEffectsTest::fixtureContext(world);world.pipelineActive=true;
        world.effects={false,false,false};world.quality.localLights=1;world.quality.localLightLimit=1;
        world.quality.gi=world.quality.weather=world.quality.contactAO=world.quality.autoBump=0;
        world.quality.equipmentLights=world.quality.nightBrightness=world.quality.horizonHaze=world.quality.fogClouds=0;
        world.quality.shadowStrength=125;world.frames=7;world.gpuDiagnosticCaptures=4;
        world.context.lightDirection[0]=world.context.lightDirection[1]=0;world.context.lightDirection[2]=-1;
        world.sourceDirections[0]=world.rawSourceDirections[0]={0,0,-1};
        const float direct[]={.12f,.45f,.8f};
        for(unsigned c=0;c<3;++c){world.context.direct[c]=direct[c];world.context.ambient[c]=.2f;}
        // The correction under complete directional occlusion is -nativeDirect.
        // Feed those same endpoints without depending on synthetic caster geometry.
        world.sourceColors[0]=world.sourceGlowColors[0]=occluded?NorthlightGI::Vec3{}:NorthlightGI::Vec3{.12f,.45f,.8f};
    }
};
static std::vector<DWORD> readShader(const std::string& path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f);
    const auto size=f.tellg();assert(size>0&&size%4==0);std::vector<DWORD> words(size_t(size)/4);
    f.seekg(0);f.read(reinterpret_cast<char*>(words.data()),size);assert(f);return words;
}
int main(int argc,char** argv){
    assert(argc==4);const bool benchmark=std::getenv("NORTHLIGHT_LIGHTING_BENCH")!=nullptr;
    const unsigned W=benchmark?2560:96,H=benchmark?1440:64;std::setvbuf(stdout,nullptr,_IONBF,0);
    std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    auto window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    auto original=readShader(std::string(argv[3])+"/prelit.bin"),edited=original,lit=readShader(std::string(argv[3])+"/lit.bin");
    edited.insert(edited.end()-1,{0x0001fffeu,0x12345678u}); // Comment: exact native output, new identity.
    const auto identity=[](const auto& w){return NorthlightShaderTags::fnv1a(w.data(),w.size()*4);};
    assert(NorthlightShaderTags::inWmoSignatures(identity(original))&&!NorthlightShaderTags::inWmoSignatures(identity(edited)));
    assert(!NorthlightUnlitReceiver::prelitShader(reinterpret_cast<const uint32_t*>(edited.data()),edited.size(),false)); // Old admission loses the mask.
    bool marked[2];
    {
        NorthlightHdr::Presentation presentation;presentation.logical=pp;presentation.settings.requested=false;
        d->AddRef();auto* device=new Device(d,api,presentation);
        marked[0]=PipelineOffTest::prelit(*device,original);marked[1]=PipelineOffTest::prelit(*device,edited);
        assert(marked[0]&&marked[1]&&!PipelineOffTest::prelit(*device,lit));
        PipelineOffTest::overrideRegistration(*device,lit);
        auto editedLit=lit;editedLit.insert(editedLit.end()-1,{0x0001fffeu,0x87654321u});
        assert(!NorthlightShaderTags::inWmoSignatures(identity(editedLit)));
        PipelineOffTest::overrideRegistration(*device,editedLit);
        PipelineOffTest::layeredRoom(*device,editedLit,W,H);
        PipelineOffTest::layeredRoom(*device,readShader(std::string(argv[3])+"/tower-player.bin"),W,H,true);
        PipelineOffTest::overrideRegistration(*device,readShader(std::string(argv[3])+"/model.bin"),true);
        PipelineOffTest::overrideRegistration(*device,readShader(std::string(argv[3])+"/rigid-model.bin"),true);
        PipelineOffTest::layeredRoom(*device,readShader(std::string(argv[3])+"/rigid-model.bin"),W,H,true,false);
        PipelineOffTest::layeredRoom(*device,readShader(std::string(argv[3])+"/rigid-model.bin"),W,H,true,false,false);
        if(std::ifstream(std::string(argv[3])+"/captured-model.bin").good()){
            PipelineOffTest::overrideRegistration(*device,readShader(std::string(argv[3])+"/captured-model.bin"),true);
            std::puts("CAPTURED_M2_ROOM_REGISTRATION_PASS");
        }
        if(std::ifstream(std::string(argv[3])+"/foreign.bin").good()){
            const auto foreign=readShader(std::string(argv[3])+"/foreign.bin");
            assert(!NorthlightShaderTags::inWmoSignatures(identity(foreign)));
            PipelineOffTest::overrideRegistration(*device,foreign);
            PipelineOffTest::layeredRoom(*device,foreign,W,H);
            std::puts("CAPTURED_FOREIGN_WMO_REGISTRATION_PASS");
        }
        if(!benchmark)PipelineOffTest::unmarkRunPixels(*device,W,H);
        device->Release();
    }
    IDirect3DSurface9 *target=nullptr,*read=nullptr,*mask=nullptr;
    screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    screenOk(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&read,nullptr));
    screenOk(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&mask,nullptr));
    const float rgb[]={.4f,.4f,.4f,1},linear[]={.132868f,.132868f,.132868f,1},z[]={1000.f/999.f-1000.f/(999.f*20)};
    auto* scene=screenTexture(d,W,H,rgb);auto* hdrBase=screenTexture(d,W,H,linear);auto* depth=screenTexture(d,W,H,z,1);
    constexpr unsigned sharedTintVariant=6;
    constexpr unsigned variants=sharedTintVariant+1;
    unsigned controls=0;
    for(bool hdr:{false,true}){
        float values[variants][2][3]{};
        for(unsigned variant=0;variant<variants;++variant)for(unsigned shadow=0;shadow<2;++shadow){
            WorldRenderer world(d);PipelineOffTest::lighting(world,shadow!=0);
            screenOk(d->SetRenderTarget(0,target));screenOk(d->SetDepthStencilSurface(mask));
            D3DVIEWPORT9 viewport{0,0,W,H,0,1};screenOk(d->SetViewport(&viewport));screenOk(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
            const unsigned level=variant==sharedTintVariant?NorthlightUnlitReceiver::RoomSharedTint:variant>=3?variant-3:0;
            screenOk(d->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1,variant>=3?NorthlightUnlitReceiver::RoomRef[level]:variant<2&&marked[variant]?NorthlightUnlitReceiver::PrelitBit:0));
            screenOk(d->SetDepthStencilSurface(nullptr));if(variant>=3)world.setAuthoredMask(mask,1u<<level);else world.setPrelitMask(mask);
            assert(renderChecked(world,d,target,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,nullptr,7,hdr?hdrBase:nullptr));
            screenOk(d->GetRenderTargetData(target,read));D3DLOCKED_RECT lock{};screenOk(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned c=0;c<3;++c){unsigned short bits;std::memcpy(&bits,static_cast<char*>(lock.pBits)+(H/2)*lock.Pitch+(W/2)*8+c*2,2);values[variant][shadow][c]=screenHalf(bits);}
            screenOk(read->UnlockRect());
            std::printf("PRELIT_PIXEL hdr=%u variant=%u shadow=%u rgb=%.6f,%.6f,%.6f\n",unsigned(hdr),variant,shadow,values[variant][shadow][0],values[variant][shadow][1],values[variant][shadow][2]);
        }
        const float expected=hdr?linear[0]:rgb[0];
        for(unsigned variant=0;variant<2;++variant)for(unsigned shadow=0;shadow<2;++shadow)for(unsigned c=0;c<3;++c){
            assert(values[variant][shadow][c]==values[0][0][c]); // Same compositor dither as the unoccluded reference.
            assert(std::fabs(values[variant][shadow][c]-expected)<1.f/255);
        }
        for(unsigned variant=3;variant<variants;++variant)for(unsigned shadow=0;shadow<2;++shadow)for(unsigned c=0;c<3;++c)assert(values[variant][shadow][c]==values[0][0][c]);
        const auto* bad=values[2][1];assert(bad[0]>bad[2]+expected*.05f&&bad[2]<expected*.7f);++controls;
        std::printf("PRELIT_COLOR hdr=%u preserved=%.6f oldMaskLoss=%.6f,%.6f,%.6f\n",unsigned(hdr),values[1][1][0],bad[0],bad[1],bad[2]);
    }
    if(benchmark){
        const float flat[]={0,0,1,0};auto* detail=screenTexture(d,W,H,flat);
        screenOk(d->SetDepthStencilSurface(mask));screenOk(d->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1,NorthlightUnlitReceiver::AuthoredBit));screenOk(d->SetDepthStencilSurface(nullptr));
        for(bool material:{false,true})for(bool hdr:{false,true}){
            std::unique_ptr<WorldRenderer> worlds[2];for(auto& w:worlds){w=std::make_unique<WorldRenderer>(d);PipelineOffTest::lighting(*w,true);
                if(material)w->setMaterialDetail(detail,1,48);}
            const bool control=std::getenv("NORTHLIGHT_LIGHTING_CONTROL")!=nullptr;
            char name[80];std::snprintf(name,sizeof name,"pipeline-material%u-hdr%u-%s",unsigned(material),unsigned(hdr),control?"AA":"AB");
            lightingPairTiming(d,name,8,[&](unsigned){screenOk(d->SetRenderTarget(0,target));screenOk(d->SetDepthStencilSurface(nullptr));},
                [&](unsigned mode){auto& w=*worlds[mode];w.setAuthoredMask(mode&&!control?mask:nullptr);
                    return w.render(target,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,nullptr,7,hdr?hdrBase:nullptr)?S_OK:E_FAIL;});
        }
        detail->Release();
    }
    for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);
    scene->Release();hdrBase->Release();depth->Release();mask->Release();target->Release();read->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
    std::printf("PRELIT_RECEIVER_GPU_PASS actualRegistration=1 unchangedNativeShader=1 SDR=1 HDR=1 outdoorRejected=1 negativeControls=%u\n",controls);
}
