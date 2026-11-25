// Production screen effects must survive an unavailable geometry publication.
#undef NDEBUG
#define NORTHLIGHT_REPLAY_STAND 1
#include "renderer.cpp"
#include <cassert>
#include <fstream>
#include "capture_file_reader.h"
struct WorldScreenEffectsTest {
    template<class W,class=void> struct HasFogScreen : std::false_type {};
    template<class W> struct HasFogScreen<W,std::void_t<decltype(std::declval<W&>().fogScreen)>> : std::true_type {};
    template<class W,class=void> struct HasComposite : std::false_type {};
    template<class W> struct HasComposite<W,std::void_t<decltype(std::declval<W&>().composite)>> : std::true_type {};
    template<class W,class=void> struct HasLightHistory : std::false_type {};
    template<class W> struct HasLightHistory<W,std::void_t<decltype(std::declval<W&>().history)>> : std::true_type {};
    template<class W,class=void> struct HasNormalsOwner : std::false_type {};
    template<class W> struct HasNormalsOwner<W,std::void_t<decltype(std::declval<W&>().worldNormals)>> : std::true_type {};
    template<class W> static bool normalTargetsAbsent(const W& w){
        if constexpr(HasNormalsOwner<W>::value)return !w.worldNormals.texture()&&!w.worldNormals.surface();
        else return !w.normalBuffer&&!w.normalSurface;
    }
    template<class W> static bool normalShadersAbsent(const W& w){
        if constexpr(HasNormalsOwner<W>::value)return !w.worldNormals.shader_&&!w.worldNormals.normalsOnly_&&!w.worldNormals.normalsOnlyRefused_;
        else return !w.normalsPS;
    }
    template<class W> static bool isNormalShader(const W& w,IDirect3DPixelShader9* shader){
        if constexpr(HasNormalsOwner<W>::value)return shader==w.worldNormals.shader_||shader==w.worldNormals.normalsOnly_;
        else return shader==w.normalsPS||shader==w.normalsNoDistancePS;
    }
    template<class W,class=void> struct HasScreenLighting : std::false_type {};
    template<class W> struct HasScreenLighting<W,std::void_t<decltype(std::declval<W&>().screenLighting)>> : std::true_type {};
    template<class W> static bool lightingTargetsAbsent(const W& w){
        if constexpr(HasScreenLighting<W>::value)return !w.screenLighting.raw().texture&&!w.screenLighting.raw().surface&&
            !w.screenLighting.baseline().texture&&!w.screenLighting.baseline().surface&&
            !w.screenLighting.prelit().texture&&!w.screenLighting.prelit().surface;
        else return !w.light&&!w.lightSurface&&!w.baselineLight&&!w.prelitDaylight;
    }
    template<class W> static bool isLightingShader(const W& w,IDirect3DPixelShader9* shader){
        if constexpr(HasScreenLighting<W>::value)
            return shader==w.screenLighting.directional_||shader==w.screenLighting.unshadowed_||shader==w.screenLighting.unshadowedGi_;
        else return shader==w.lightingPS||shader==w.lightingUnshadowedPS||shader==w.lightingUnshadowedGIPS;
    }
    template<class W> static bool& refuseUnshadowedGi(W& w){
        if constexpr(HasScreenLighting<W>::value)return w.screenLighting.unshadowedGiRefused_;
        else return w.lightingUnshadowedGIRefused;
    }
    template<class W> static auto& unshadowedGi(W& w){
        if constexpr(HasScreenLighting<W>::value)return w.screenLighting.unshadowedGi_;
        else return w.lightingUnshadowedGIPS;
    }
    template<class W> static auto& historyValid(W& w){
        if constexpr(HasLightHistory<W>::value)return w.history.valid_;
        else return w.temporalValid;
    }
    template<class W> static IDirect3DPixelShader9* historyShader(const W& w){
        if constexpr(HasLightHistory<W>::value)return w.history.lightingShader_;
        else return w.temporalPS;
    }
    template<class W> static IDirect3DPixelShader9* removalShader(const W& w){
        if constexpr(HasLightHistory<W>::value)return w.history.removalShader_;
        else return w.removalPS;
    }
    template<class W> static bool lightTargetsAbsent(const W& w){
        if constexpr(HasLightHistory<W>::value)
            return !w.history.pending().light.texture&&!w.history.produced().light.texture;
        else return !w.temporalLight[0]&&!w.temporalLight[1];
    }
    template<class W,class=void> struct HasActorShadows : std::false_type {};
    template<class W> struct HasActorShadows<W,std::void_t<decltype(std::declval<W&>().actorMaps)>> : std::true_type {};
    template<class W,class Visit> static void actorTextures(const W& w,const Visit& visit){
        if constexpr(HasActorShadows<W>::value)w.actorMaps.forEachTexture(visit);
        else{
            for(auto* t:w.actorShadow)visit(t);
            for(auto* t:w.actorTiles)visit(t);
            visit(w.actorTilesMid);
            visit(w.actorVisibility);
        }
    }
    template<class W> static bool actorDepthAbsent(const W& w,unsigned source){
        if constexpr(HasActorShadows<W>::value)return !w.actorMaps.depth(source).texture;
        else return !w.actorShadow[source];
    }
    template<class W,class=void> struct HasDirectionalTargets : std::false_type {};
    template<class W> struct HasDirectionalTargets<W,std::void_t<decltype(std::declval<W&>().directionalMaps)>> : std::true_type {};
    template<class W,class Texture,class Surface>
    static void directionalTargets(const W& w,const Texture& texture,const Surface& surface){
        if constexpr(HasDirectionalTargets<W>::value)w.directionalMaps.visit(texture,surface);
        else{
            for(auto* t:w.shadow)texture(t);
            for(auto* t:w.shadowCache)texture(t);
            texture(w.shadowScratch);
            surface(w.shadowDepth);surface(w.shadowCacheDepth);
        }
    }
    struct FogTarget {
        IDirect3DTexture9* texture;
        IDirect3DSurface9* surface;
    };
    struct FogTargets { FogTarget raw,scratch,history; };
    // Reference-header runs can still use the renderer from before the move.
    template<class W> static FogTargets fogTargets(const W& w){
        if constexpr(HasFogScreen<W>::value){
            const auto& screen=w.fogScreen;
            return {{screen.raw().texture,screen.raw().surface},
                    {screen.scratch().texture,screen.scratch().surface},
                    {screen.history().texture,screen.history().surface}};
        }else return {{w.fog,w.fogSurface},{w.fogBlurred,w.fogBlurredSurface},{w.fogHistory,w.fogHistorySurface}};
    }
    template<class W> static auto& fogHistoryValid(W& w){
        if constexpr(HasFogScreen<W>::value)return w.fogScreen.historyValid_;
        else return w.fogHistoryValid;
    }
    template<class W> static auto& fogTemporalShader(W& w){
        if constexpr(HasFogScreen<W>::value)return w.fogScreen.temporalShader_;
        else return w.fogTemporalPS;
    }
    static void menuContext(WorldRenderer& w,IDirect3DDevice9* d){
        w.pipelineActive=true;w.quality.gi=0;w.effects={false,true,true};
        w.nativeShadowOwned=w.nativeShadowAvailable=false;
        auto snapshot=std::make_unique<NorthlightStream::GameSnapshot>();auto& menu=snapshot->menu;
        menu.valid=1;menu.scene=0x20000;menu.model=0x30000;menu.data=0x40000;menu.created=17;std::strcpy(menu.name,"UI_Dwarf");
        for(unsigned i=0;i<4;++i)menu.view[i*5]=menu.inverseView[i*5]=1;menu.view[14]=-6;menu.inverseView[14]=6;
        auto* shader=reinterpret_cast<IDirect3DVertexShader9*>(1);
        w.captureShaders[shader].skinned=true;w.captureShaders[shader].projectionKind=2;w.nativeLightingShaders.insert(shader);
        const float p[16]={1.2f,0,0,0,0,1.7f,0,0,0,0,1000.f/999.f,-1000.f/999.f,0,0,1,0};
        const float light[12]={.6f,.5f,.4f,0,.1f,.2f,.3f,0,0,0,-1,0};
        assert(SUCCEEDED(d->SetVertexShaderConstantF(2,p,4))&&SUCCEEDED(d->SetVertexShaderConstantF(10,light,3)));
        NorthlightStream::ScopedPlayback playback(*snapshot);
        assert(w.menuContext(shader)&&w.isMenu()&&w.ready());
        assert(w.context.camera[2]==6&&w.context.ambient[0]==.1f&&w.context.direct[0]==.6f);
        assert(w.sourceDirections[0].z==1&&w.sourceDirections[0].x==0&&w.sourceDirections[0].y==0);
        assert(w.sourceColors[0].x==.6f&&w.sourceColors[0].y==.5f&&w.sourceColors[0].z==.4f&&w.sourceWeights[0]==1&&w.sourceWeights[1]==0);
        assert(w.skyEnvironment.skyValid&&w.celestialValid&&w.weather().kind==NorthlightWeather::Kind::Snow);
        assert(w.frameRequirements().geometry&&w.directionalShadowWork()&&!w.nativeShadowReplacementReady());
        NorthlightStreaming::Budget budget;assert(w.upload(budget)&&!w.vertices&&!w.indices);
        const auto first=w.active;char map[64];menu.identity(map);
        w.beginSceneDomain(map,w.context.camera,&menu);assert(w.active==first);
        menu.created++;menu.identity(map);w.beginSceneDomain(map,w.context.camera,&menu);
        assert(w.active!=first&&w.active->map==map&&!w.actorJobComplete);
        NorthlightWeather::State rain;rain.kind=NorthlightWeather::Kind::Rain;rain.intensity=rain.blend=1;w.setWeather(rain);
        assert(w.weather().kind==NorthlightWeather::Kind::Snow);
        w.beginSceneDomain("Northrend",w.context.camera,nullptr);
        assert(!w.isMenu()&&!w.active&&w.weather().kind==NorthlightWeather::Kind::Rain);
        w.captureShaders.erase(shader);w.nativeLightingShaders.erase(shader);
        std::puts("MENU_SCENE_GPU_PASS actual-context=1 isolated-weather=1 menu-switch-reset=1 world-return=1 replay-only-geometry=1");
    }
#ifdef NORTHLIGHT_TEMPORAL_MERGE_TEST
    template<class Meter> static void temporalMergeResources(WorldRenderer& w,Meter& meter){
        meter.refuseMerge=false;meter.failShaderAt=meter.shaderCreates+1;
        assert(!w.history.ensureMerged(w.d)&&!w.failed);
        const auto attempts=meter.shaderCreates;
        assert(!w.history.ensureMerged(w.d)&&meter.shaderCreates==attempts);
        w.releaseGPU(true);assert(!w.history.mergeTried_&&!w.history.mergedShader_);
        meter.failShaderAt=0;assert(w.history.ensureMerged(w.d));
        w.releaseGPU(true);assert(!w.history.mergeTried_&&!w.history.mergedShader_);
        w.setTemporalZeroWeightEnabled(true);
        meter.failShaderAt=meter.shaderCreates+1;
        // An optimized shader refusal keeps the old fused path, with no retry per frame.
        assert(w.history.ensureMerged(w.d)&&!w.history.zeroWeightActive_);
        const auto fallbackAttempts=meter.shaderCreates;
        assert(w.history.ensureMerged(w.d)&&meter.shaderCreates==fallbackAttempts);
        w.releaseGPU(true);assert(!w.history.zeroWeightActive_);
        meter.failShaderAt=0;assert(w.history.ensureMerged(w.d)&&w.history.zeroWeightActive_);
        w.releaseGPU(true);w.setTemporalZeroWeightEnabled(false);
    }
#endif
#ifdef NORTHLIGHT_SHARED_SHADOW_TEST
#ifndef NORTHLIGHT_LEGACY_DIRECTIONAL_VISIBILITY_FIXTURE
    static void visibilityAfterDeviceReset(WorldRenderer& w,D3DPRESENT_PARAMETERS& present){
        auto& visibility=w.directionalVisibility;
        const UINT width=present.BackBufferWidth,height=present.BackBufferHeight;
        assert(w.softShadowResources(width,height)&&visibility.prepareShared(w.d));
        w.reset(true);
        assert(!visibility.surface()&&!visibility.texture()&&!visibility.filter(false)&&!visibility.filter(true));
        assert(SUCCEEDED(w.d->Reset(&present)));
        assert(w.softShadowResources(width+32,height+16)&&visibility.prepareShared(w.d));
        D3DSURFACE_DESC desc{};
        assert(SUCCEEDED(visibility.surface()->GetDesc(&desc))&&desc.Width==(width+32)/2&&desc.Height==(height+16)/2);
        std::puts("WORLD_VISIBILITY_DEVICE_RESET_PASS both filter paths recreated and target uses new dimensions");
    }
#endif
    template<class Meter> static void sharedResources(WorldRenderer& w,Meter& meter){
#ifdef NORTHLIGHT_LEGACY_DIRECTIONAL_VISIBILITY_FIXTURE
        auto prepare=[&]{return w.sharedShadowResources();};
        auto empty=[&]{return !w.sharedDirectionalPS&&!w.lightingSharedPS;};
        auto triedShared=[&]{return w.sharedShadowTried;};
#else
        auto prepare=[&]{return w.directionalVisibility.prepareShared(w.d);};
        auto empty=[&]{return !w.directionalVisibility.filter(true)&&!w.directionalVisibility.lighting(true);};
        auto triedShared=[&]{return w.directionalVisibility.sharedTried_;};
#endif
        const unsigned saved=meter.shaderCreates;
        meter.shaderSlotLimit=512;assert(!prepare());assert(meter.shaderCreates==saved);
        w.releaseSoftShadows();meter.shaderSlotLimit=0;
        for(unsigned fail=1;fail<=2;++fail){
            meter.failShaderAt=meter.shaderCreates+fail;assert(!prepare());
            assert(empty());
            const unsigned tried=meter.shaderCreates;assert(!prepare()&&meter.shaderCreates==tried);
            w.releaseSoftShadows();
        }
        meter.failShaderAt=0;assert(prepare());const unsigned tries=meter.shaderCreates;
        assert(prepare()&&meter.shaderCreates==tries);
        w.releaseSoftShadows();assert(empty()&&!triedShared());
#ifndef NORTHLIGHT_LEGACY_DIRECTIONAL_VISIBILITY_FIXTURE
        auto& visibility=w.directionalVisibility;
        const unsigned savedTargets=meter.screenCreates;
        meter.failScreenAt=meter.screenCreates+1;
        assert(!w.softShadowResources(meter.width,meter.height)&&meter.screenCreates==meter.failScreenAt);
        assert(!visibility.texture()&&!visibility.surface()&&!visibility.filter(false)&&!visibility.lighting(false));
        const unsigned afterFailure=meter.shaderCreates;
        assert(!w.softShadowResources(meter.width,meter.height)&&meter.shaderCreates==afterFailure);
        meter.failScreenAt=0;
        w.releaseSoftShadows();
        assert(w.softShadowResources(meter.width,meter.height)&&visibility.texture()&&visibility.surface());
        w.releaseSoftShadows();assert(!visibility.texture()&&!visibility.surface());
        meter.screenCreates=savedTargets;
        std::puts("WORLD_VISIBILITY_TARGET_PASS allocationFailure=contained retries=bounded reset=restored");
#endif
        meter.shaderCreates=saved;
        std::puts("WORLD_SHARED_SHADOW_RESOURCES_PASS caps=guarded failures=contained retry=reset noRepeatedCreates=1");
    }
    static void sharedSetup(WorldRenderer& w,IDirect3DVertexBuffer9* vb,IDirect3DIndexBuffer9* ib,unsigned mode){
        giFallbackSetup(w,vb,ib,0,false);w.quality.gi=0;w.effects={false,true,false};
        w.quality.worldShadows=w.quality.actorShadows=w.quality.actorShadowDetail=1;w.quality.softShadows=mode==4?0:1;
        w.nativeShadowOwned=w.nativeShadowAvailable=true;w.captureMode=WorldRenderer::CaptureFresh;
        if(mode>=5){w.sourceColors[1]=w.sourceGlowColors[1]={.3f,.4f,.5f};w.sourceWeights[0]=w.sourceWeights[1]=.5f;}
    }
#endif
    static IDirect3DVolumeTexture9*& cloudNoiseTexture(WorldRenderer& w){
#ifdef NORTHLIGHT_REGIONAL_FOG_GPU_TEST
        return w.regionalFogGpu.noise_;
#else
        return w.cloudNoise;
#endif
    }
#ifdef NORTHLIGHT_FOG_VOLUME_TEST
    static void volumeFixture(WorldRenderer& w,unsigned frame){
        fixtureContext(w);w.effects={false,false,frame!=4};w.quality.localLights=w.quality.weather=0;
        w.skyEnvironment={};w.skyEnvironment.valid=true;auto& layer=w.skyEnvironment.layer[0];layer.flag[3]=1;layer.p[1]=.3f;
        layer.color[1][0]=.7f;layer.color[1][1]=.5f;layer.color[1][2]=.3f;
        if(frame>=18){
            auto& cloudNoise=cloudNoiseTexture(w);
            if(!cloudNoise){assert(SUCCEEDED(w.d->CreateVolumeTexture(4,4,4,1,0,D3DFMT_L8,D3DPOOL_MANAGED,&cloudNoise,nullptr)));D3DLOCKED_BOX box{};assert(SUCCEEDED(cloudNoise->LockBox(0,&box,nullptr,0)));for(unsigned z=0;z<4;++z)for(unsigned y=0;y<4;++y)std::memset(static_cast<char*>(box.pBits)+z*box.SlicePitch+y*box.RowPitch,128,4);assert(SUCCEEDED(cloudNoise->UnlockBox(0)));}
            layer.flag[2]=1;layer.p[20]=.1f;layer.p[22]=1;layer.p[11]=frame==21||frame==22?.1f:0.f;
        }
        if(frame==3||frame>=14){const float x=frame>=14?.5f:.25f;w.context.camera[0]=w.context.inverseView[12]=x;w.context.view[12]=-x;}
        if(frame==26){w.context.camera[0]=w.context.inverseView[12]=.75f;w.context.view[12]=-.75f;}
        w.memoryPressure=frame==6||frame==13||frame==21||frame==22;
        if(frame>=8&&frame<=13)w.fogVolume.reset();
    }
    static std::pair<unsigned,unsigned> volumeCounts(const WorldRenderer& w){return {w.fogVolume.updates,w.fogVolume.reuses};}
    static bool volumeValid(const WorldRenderer& w){return w.fogVolume.valid;}
    static void cloudFixture(WorldRenderer& w,bool enabled){
        volumeFixture(w,25);
        w.skyEnvironment={};
        w.quality.fogClouds=enabled;w.quality.fogCloudDensity=100;w.cloudLush=1;
        assert(w.fogCloudNoise().ready.load(std::memory_order_acquire));
    }
#endif
#ifdef NORTHLIGHT_FOG_DISTANCE_TEST
    static void fogOnlySetup(WorldRenderer& w,unsigned width,unsigned height,bool lighting){
        w.effects={false,false,true};w.quality.localLights=lighting;w.quality.localLightLimit=1;
        w.quality.autoBump=w.quality.weather=w.quality.contactAO=w.quality.nightBrightness=0;
        setup(w,width,height);w.effects={false,false,true};
        // Pure fog has no celestial surface correction to compare with lighting.
        w.sourceColors[0]=w.sourceGlowColors[0]={0,0,0};
        w.context.direct[0]=w.context.direct[1]=w.context.direct[2]=0;
        assert(distanceTargetsReady(w));
        if(!lighting)assert(normalTargetsAbsent(w)&&lightingTargetsAbsent(w)&&lightTargetsAbsent(w)&&!w.surfaceLightingEnabled());
    }
    static void fogOnlyFrame(WorldRenderer& w,unsigned frame,bool reference){
        w.context.camera[0]=w.context.inverseView[12]=float(frame)*.125f;
        w.quality.localLights=reference||frame==4||frame==5;
        if(reference&&(frame==4||frame==6))historyValid(w)=false;
    }
    template<class W> static bool distanceLighting(const W& w){
        if constexpr(HasLightHistory<W>::value)return w.history.lighting_;
        else return w.temporalLighting;
    }
#endif
    static std::pair<IDirect3DSurface9*,IDirect3DSurface9*> removalScratch(WorldRenderer& w){return {fogTargets(w).scratch.surface,fogTargets(w).history.surface};}
    template<class W> static std::pair<IDirect3DSurface9*,IDirect3DSurface9*> lightScratch(const W& w){
        if constexpr(HasLightHistory<W>::value)return {w.history.pending().light.surface,w.history.produced().light.surface};
        else return {w.temporalLightSurface[w.temporalIndex],w.temporalLightSurface[1-w.temporalIndex]};
    }
    static void fogAliasFixtureFrame(WorldRenderer& w,unsigned frame){
        aliasFixtureFrame(w,frame);w.quality.localLights=1;w.quality.localLightLimit=frame%12<10&&frame%12!=2?12:0;w.quality.pointShadows=0;
        w.quality.actorShadows=0;w.effects.gi=frame%8<6;w.localLightQpc=0;
        if(!w.active){w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map=w.lastRequest.map;}
        auto& lights=w.active->localLights;lights.resize(12);
        for(unsigned i=0;i<lights.size();++i){auto& light=lights[i];light.sourceId=i+1;light.kind=2;
            light.position[0]=float(int(i%4)-2)*5+(frame%12==5?100.f:float(frame%3));light.position[1]=float(int(i/4)-1)*4;light.position[2]=18+float(i%3)*5;
            light.diffuse[0]=1;light.diffuse[1]=.5f;light.diffuse[2]=.3f;light.attenuationStart=2;light.attenuationEnd=6;}
        if(frame==12)historyValid(w)=fogHistoryValid(w)=false;
        if(frame==18)w.context.camera[0]=w.context.inverseView[12]=42;
        if(frame==24)drop(fogTemporalShader(w));
    }
    static void aliasFixtureFrame(WorldRenderer& w,unsigned frame){
        fixtureContext(w);w.quality.weather=0;w.effects.fog=frame%12<9;
        w.sourceWeights[0]=frame%7==3?0.f:1.f;
        w.sourceColors[0]=w.sourceGlowColors[0]={.2f+.03f*(frame%5),.4f,.6f};
        w.context.camera[0]=w.context.inverseView[12]=float(frame%4)*.125f;
    }
    static void aliasReleased(WorldRenderer& w){w.active.reset();w.releaseGPU(true);assert(!fogTargets(w).scratch.texture&&!fogTargets(w).history.texture&&lightTargetsAbsent(w));}
    static void weatherBase(WorldRenderer& w,const float* c){
        w.setHdrBaseCompositeEnabled(std::getenv("NORTHLIGHT_BASE_BACKEND_VERIFIED")!=nullptr);
        w.pipelineActive=true;w.valid=true;w.traceContext=2;
        w.quality.effects=w.quality.weather=1;
        w.quality.gi=w.quality.worldShadows=w.quality.worldFog=w.quality.localLights=w.quality.contactAO=w.quality.autoBump=w.quality.equipmentLights=0;
        w.effects={false,false,false};w.materialDetail=w.materialOcclusion=nullptr;w.modelNormals=nullptr;
        w.lastRequest.map="offline-weather-base";
        w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map=w.lastRequest.map;w.active->bvh=std::make_shared<NorthlightGI::BVH>();
        std::memcpy(w.context.inverseView,c+12,64);std::memcpy(w.context.camera,c+60,12);
        std::memcpy(w.context.lightDirection,c+112,12);std::memcpy(w.context.direct,c+116,12);std::memcpy(w.context.ambient,c+72,12);
        for(unsigned i=0;i<16;++i)w.context.view[i]=i%5==0?1.f:0.f;
        for(unsigned r=0;r<3;++r){for(unsigned k=0;k<3;++k)w.context.view[4*r+k]=w.context.inverseView[4*k+r];
            w.context.view[12+r]=-(w.context.camera[0]*w.context.view[r]+w.context.camera[1]*w.context.view[4+r]+w.context.camera[2]*w.context.view[8+r]);}
        std::copy(c+4,c+7,w.projection);
        w.sourceDirections[0]={c[112],c[113],c[114]};w.sourceDirections[1]={-c[112],-c[113],-c[114]};
        w.sourceColors[0]=w.sourceGlowColors[0]={c[116],c[117],c[118]};w.sourceColors[1]=w.sourceGlowColors[1]={0,0,0};w.sourceWeights[0]=1;w.sourceWeights[1]=0;
        if(std::getenv("NORTHLIGHT_BASE_NO_DIRECTIONAL"))w.sourceWeights[0]=w.sourceWeights[1]=0;
    }
    template<class W> static IDirect3DPixelShader9* baseCompositor(const W& w){
        if constexpr(HasComposite<W>::value){
            return w.composite.hdrAOBasePS_?w.composite.hdrAOBasePS_:w.composite.hdrBasePS_;
        }else{
#ifdef NORTHLIGHT_AO_BASE_TEST
            if(w.hdrAOBasePS)return w.hdrAOBasePS;
#endif
            return w.hdrBasePS;
        }
    }
    template<class W> static bool distanceTargetsReady(const W& w){
        if constexpr(HasLightHistory<W>::value)
            return w.history.pending().distance.texture&&w.history.produced().distance.texture&&w.history.distanceShader_;
        else return w.temporalDepth[0]&&w.temporalDepth[1]&&w.distancePS;
    }
    template<class W> static IDirect3DTexture9* currentDistance(const W& w){
        if constexpr(HasLightHistory<W>::value)return w.history.produced().distance.texture;
        else return w.temporalDepth[1-w.temporalIndex];
    }
    static IDirect3DTexture9* rawFog(const WorldRenderer& w){return fogTargets(w).raw.texture;}
    template<class W> static void refuseBaseCompositor(W& w){
        if constexpr(HasComposite<W>::value){
            drop(w.composite.hdrBasePS_);w.composite.baseTried_=true;
            drop(w.composite.hdrAOBasePS_);w.composite.aoBaseTried_=true;
        }else{
            drop(w.hdrBasePS);w.hdrBaseTried=true;
#ifdef NORTHLIGHT_AO_BASE_TEST
            drop(w.hdrAOBasePS);w.hdrAOBaseTried=true;
#endif
        }
    }
    template<class W> static void configureAOBase(W& w,bool reference){
        w.composite.permitBase(true);w.composite.aoBaseTried_=reference;
    }
    template<class W> static bool isHdrWorldComposite(const W& w,IDirect3DPixelShader9* shader){
        if constexpr(HasComposite<W>::value)
            return shader&&(shader==w.composite.hdrFinalPS_||shader==w.composite.hdrBasePS_);
        else return shader&&(shader==w.hdrFinalPS||shader==w.hdrBasePS);
    }
    template<class W> static bool isWorldOrMaterialComposite(const W& w,IDirect3DPixelShader9* shader){
        const auto& c=w.composite;
        return shader&&(shader==c.finalPS_||shader==c.hdrFinalPS_||shader==c.hdrCombinedPS_||
                        shader==c.materialPS_||shader==c.hdrMaterialPS_);
    }
    static void inactiveGI(WorldRenderer& w){
        const auto quality=w.quality;const auto effects=w.effects;
        const bool expectPaused=!std::getenv("NORTHLIGHT_PIPELINE_BASELINE_RUN");
        for(unsigned mode=0;mode<4;++mode){
            w.quality=quality;w.quality.gi=mode!=3;w.quality.giDynamicProbes=1;w.quality.actorShadows=0;
            w.quality.giStrength=mode==1?0:60;w.effects={mode!=0,false,false};
            unsigned jobs=0,skipped=0;const auto start=std::chrono::steady_clock::now();
            for(unsigned frame=0;frame<100000;++frame){
                w.captureMode=WorldRenderer::CaptureUndecided;w.captureDemand=false;w.lastRenderDebug=0;
                w.actorCaptureDecided=false;w.lastActorCapture=0;w.actorJob.reset();
                skipped+=w.modelCaptureSkipped();jobs+=bool(w.actorJob);
                assert(w.actorCaptureWouldRun()==bool(w.actorJob));
            }
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            const bool paused=mode==3||(expectPaused&&mode<2);
            assert(jobs==(paused?0u:100000u)&&skipped==(paused?100000u:0u));
            std::printf("WORLD_GI_CPU mode=%u ms=%.6f frames=100000 jobs=%u skipped=%u\n",mode,ms,jobs,skipped);
        }
        w.quality=quality;w.effects=effects;w.captureMode=WorldRenderer::CaptureUndecided;
        w.actorCaptureDecided=false;w.actorJob.reset();
        std::puts("WORLD_GI_PAUSE_PASS hidden=1 zeroStrength=1 positivePreserved=1 qualityOff=1");
    }
    static void gi(WorldRenderer& w,bool enabled){w.effects.gi=enabled;}
    static void giFallbackSetup(WorldRenderer& w,IDirect3DVertexBuffer9* vb,IDirect3DIndexBuffer9* ib,unsigned mode,bool merged){
        fixtureContext(w);w.pipelineActive=true;w.lastRequest.map="OfflineGIStreaming";
        w.quality.effects=w.quality.gi=1;w.quality.giStrength=120;w.quality.giBrightness=80;
        w.quality.localLights=w.quality.weather=w.quality.contactAO=w.quality.autoBump=w.quality.equipmentLights=0;
        w.quality.pointShadows=w.quality.nightBrightness=w.quality.horizonHaze=w.quality.fogClouds=0;
        fakeScene(w,vb,ib,false);w.active->serial=0; // No solved probes, even with a resident mesh.
        w.effects={mode!=2,false,false};if(mode==3)w.quality.giStrength=0;
        if(mode==1)w.active.reset();
        w.nativeShadowOwned=w.nativeShadowAvailable=false;
        refuseUnshadowedGi(w)=!merged;w.frames=7;w.gpuDiagnosticCaptures=4;
    }
    template<class W> static void refuseUnshadowedLighting(W& w){
        if constexpr(HasScreenLighting<W>::value)w.screenLighting.unshadowedRefused_=true;
        else w.lightingUnshadowedRefused=true;
    }
    static void fogStreamingSetup(WorldRenderer& w,IDirect3DVertexBuffer9* vb,IDirect3DIndexBuffer9* ib,unsigned mode,unsigned medium){
        giFallbackSetup(w,vb,ib,mode==1?1:0,true);w.effects={false,true,true};
        w.nativeShadowOwned=w.nativeShadowAvailable=mode==2;
        if(mode==4){w.quality.localLights=1;w.quality.localLightLimit=1;} // Resident mesh consumer, empty light selection.
        if(mode==3){for(unsigned source=0;source<2;++source){w.sourceColors[source]=w.sourceGlowColors[source]={};w.sourceWeights[source]=0;}}
        auto field=std::make_shared<NorthlightRegionalFog::Field>();field->originX=field->originY=-256;field->fogCells=field->airCells=4096;
        for(auto& t:field->texels){t.ground=-2;t.day=.015f;t.height=2.5f;}
        auto region=std::make_shared<WorldRenderer::FogRegion>();region->map=w.lastRequest.map;region->field=field;w.publishedFogRegion=region;
        if(medium){
            w.skyEnvironment.valid=true;auto& l=w.skyEnvironment.layer[0];l.flag[3]=l.flag[0]=1;l.p[1]=.3f;l.p[9]=1;l.p[10]=.5f;
            for(unsigned c=0;c<3;++c){l.color[0][c]=.7f-.15f*c;l.color[1][c]=.4f-.1f*c;l.color[3][c]=.2f;}
            w.memoryPressure=medium==1;if(medium==1)w.fogVolume.reset();w.fogVolume.refused=medium==1; // Explicit unavailable-volume control.
        }
    }
#ifdef NORTHLIGHT_GI_MERGE_TEST
    static void giMergeControl(WorldRenderer& w,unsigned mode){drop(unshadowedGi(w));refuseUnshadowedGi(w)=mode==0;w.effects.shadows=false;w.effects.gi=mode!=3;historyValid(w)=false;}
    static bool giMerged(const WorldRenderer& w){return unshadowedGi(w)!=nullptr;}
#endif
    static void zeroGINight(WorldRenderer& w,unsigned brightness){
        w.quality.giStrength=0;w.quality.nightBrightness=brightness;
        w.celestialValid=true;w.celestial.dayFraction=0.;w.effects={true,false,false};historyValid(w)=fogHistoryValid(w)=false;
    }
    static void restoreGINight(WorldRenderer& w){w.quality.giStrength=60;w.quality.nightBrightness=50;w.celestialValid=false;w.effects={true,false,true};historyValid(w)=fogHistoryValid(w)=false;}
    static void localLightCPU(WorldRenderer& w){
        const auto quality=w.quality;const auto effects=w.effects;const auto active=w.active;
        w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map="offline-lamps";
        auto records=std::make_shared<NorthlightLightAttachments::Records>();records->resize(1);w.active->lightAttachments=records;
        auto& lights=w.active->localLights;lights.resize(10000);
        for(unsigned i=0;i<lights.size();++i){auto& l=lights[i];l.sourceId=i+1;l.position[0]=float(i%100)*.5f;l.position[1]=float(i/100)*.5f;l.position[2]=10;l.diffuse[0]=l.diffuse[1]=l.diffuse[2]=1;l.attenuationStart=2;l.attenuationEnd=15;l.kind=2;}
        w.quality.pointShadows=0;w.quality.equipmentLights=0;w.quality.fogClouds=0;w.effects={false,false,false};
        for(unsigned mode=0;mode<2;++mode)for(unsigned sample=0;sample<5;++sample){
            w.quality.localLightLimit=mode?48:0;w.lightAttachments.bind(records);
            for(unsigned i=0;i<200;++i)w.lightAttachments.positions[i+1]={float(i%20),float(i/20),10};
            WorldRenderer::AtmosphereFrame a{};unsigned selected=0;uint64_t digest=1469598103934665603ull;
            const auto start=std::chrono::steady_clock::now();
            for(unsigned frame=0;frame<200;++frame){
                w.localLightQpc=0;w.atmosphereWork(a,1,128,80,0);selected+=a.lights.count;
                for(unsigned k=0;k<a.lights.count;++k)for(float f:a.lights.position[k]){uint32_t bits;std::memcpy(&bits,&f,4);digest=(digest^bits)*1099511628211ull;}
            }
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            assert(mode?selected>0:selected==0);
            std::printf("WORLD_LOCAL_CPU mode=%u sample=%u ms=%.6f frames=200 lights=10000 selected=%u digest=%llu\n",mode,sample,ms,selected,(unsigned long long)digest);
        }
        w.quality=quality;w.effects=effects;w.active=active;w.lightAttachments.reset();w.attachedLocalLights.clear();w.localLightTracker.reset();w.localLightMap.clear();w.localLightQpc=0;
        std::puts("WORLD_LOCAL_PAUSE_PASS zeroLights=1 resume=1");
    }
    template<class W> static void retryHdrCombined(W& w){
        if constexpr(HasComposite<W>::value){drop(w.composite.hdrCombinedPS_);w.composite.combineTried_=false;}
        else{drop(w.hdrCombinedPS);w.hdrCombineTried=false;}
    }
    template<class W> static bool hdrCombined(const W& w){
        if constexpr(HasComposite<W>::value)return w.composite.hdrCombinedPS_!=nullptr;
        else return w.hdrCombinedPS!=nullptr;
    }
    static void fixtureContext(WorldRenderer& w){
        // A real frame reacquires the camera and lighting from a world draw after endFrame().
        w.valid=true;w.traceContext=2;
        for(unsigned i=0;i<16;++i)w.context.view[i]=w.context.inverseView[i]=(i%5==0)?1.f:0.f;
        for(unsigned i=0;i<3;++i){w.context.camera[i]=0;w.context.direct[i]=.6f;w.context.ambient[i]=.25f;}
        w.context.lightDirection[0]=.4f;w.context.lightDirection[1]=.2f;w.context.lightDirection[2]=-.8944272f;
        w.projection[0]=w.projection[1]=w.projection[2]=1;
        w.sourceDirections[0]={.4f,.2f,-.8944272f};w.sourceDirections[1]={0,0,1};
        w.rawSourceDirections[0]=w.sourceDirections[0];w.rawSourceDirections[1]=w.sourceDirections[1];
        w.sourceColors[0]=w.sourceGlowColors[0]={.6f,.5f,.4f};w.sourceColors[1]=w.sourceGlowColors[1]={0,0,0};w.sourceWeights[0]=1;w.sourceWeights[1]=0;
    }
#if NORTHLIGHT_TREE_MENU_TEST
    static void canopyToggles(WorldRenderer& w){
        fixtureContext(w);
        w.celestialValid=true;
        NorthlightSettings::Config next;next.quality=w.quality;next.celestial=w.celestialProfiles;
        next.quality.celestialEffects=next.quality.celestialStyle=1;
        for(unsigned body=0;body<2;++body){
            w.sourceWeights[body]=1;w.sourceWeights[1-body]=0;
            w.sourceDirections[body]={.8660254f,0,.5f};w.sourceColors[body]={.6f,.5f,.4f};
            w.framePalette.foliageTransmission[body]=.25f;
            for(unsigned mask=0;mask<4;++mask){
                const bool canopy=(mask&1)!=0,crown=(mask&2)!=0;
                next.quality.treeCanopy=canopy;next.quality.treeCrown=crown;
                w.applyLiveSettings(next);w.paletteFrameValid=true;
                for(float facing:{1.f,-1.f}){
                    w.context.inverseView[10]=facing;
                    float light[3][4]={};unsigned source=2;
                    const bool backlight=crown&&facing>0;
                    assert(w.treeCanopyLight(light,source)==(canopy||backlight));
                    assert(source==body&&(light[0][2]>0)==backlight);
                    assert((light[1][3]>0)==canopy&&(light[2][3]>0)==canopy);
                }
            }
        }
    }
#endif
    static void waterMaskProfile(WorldRenderer& w,bool weather,bool contact){
        fixtureContext(w);
        w.pipelineActive=true;w.effects={false,false,false};
        w.quality.localLights=w.quality.autoBump=w.quality.celestialEffects=0;
        w.quality.weather=weather;w.quality.contactAO=contact;
    }
    static void sceneOwnership(WorldRenderer& w,IDirect3DDevice9* d){
        auto scene=w.gpuScene;const auto originalLimit=w.captureSources().sharedSourceLimit();
        assert(scene->device==d&&scene->decodedSources&&scene->captureSources==w.captureSources().sharedSources());
        assert(&w.staticCasters==&scene->staticCasters&&&w.liveTerrainGPU==&scene->terrain&&&w.terrainBoundsCache==&scene->terrainSources);
#ifdef NORTHLIGHT_WORLD_RENDER_TARGET_OWNER_TEST
        assert(&w.meshStore==&scene->meshPages.store&&&w.meshBuffers==&scene->meshPages.buffers);
#else
        assert(&w.meshStore==&scene->meshStore&&&w.meshBuffers==&scene->meshBuffers);
#endif
        struct Views {IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;NorthlightReplayGPU::Placement at;~Views(){for(auto& p:vb)drop(p);drop(ib);}} material,shadow;
        auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->streams[0].stride=12;mesh->streams[0].bytes.resize(36);mesh->indices={0,1,2};mesh->vertexCount=3;mesh->primitiveCount=1;
        mesh->sourceIdentity=NorthlightSourceIdentity::make({0x7363656e65,1});const auto bytes=mesh->byteSize();
        auto admit=[](size_t){return true;};size_t residentBytes=0;
        {
            NorthlightAutoBump materials(d,scene);
            assert(scene->normalMaps&&scene->displacement&&materials.captureSources().sharedSources()==scene->captureSources);
            // Material, displacement and the shared 4 MiB skin-range source bank.
            assert(materials.captureSources().sharedSourceParticipants()==3&&materials.captureSources().sharedSourceLimit()==originalLimit+(48u<<20));
            scene->setFrame(20);assert(!scene->materials.bind(d,mesh,material.vb,material.ib,admit,material.at));
            scene->setFrame(21);assert(!scene->materials.bind(d,mesh,material.vb,material.ib,admit,material.at));assert(scene->materials.commitPending());
            assert(scene->materials.bind(d,mesh,material.vb,material.ib,admit,material.at)&&scene->shadows.bind(d,mesh,shadow.vb,shadow.ib,admit,shadow.at));
            D3DVERTEXBUFFER_DESC vd{};D3DINDEXBUFFER_DESC id{};assert(SUCCEEDED(material.vb[0]->GetDesc(&vd))&&SUCCEEDED(material.ib->GetDesc(&id)));
            assert(vd.Size==mesh->streams[0].bytes.size()&&id.Format==D3DFMT_INDEX16&&id.Size==mesh->indices.size()*2);
            residentBytes=size_t(vd.Size)+id.Size;assert(residentBytes==bytes-mesh->indices.size()*2&&scene->native.bytes()==residentBytes&&material.vb[0]==shadow.vb[0]&&material.ib==shadow.ib);
            materials.reset();assert(!scene->materials.bytes()&&scene->shadows.bytes()==residentBytes&&scene->native.bytes()==residentBytes);
            auto reconstructed=std::make_shared<NorthlightDrawSnapshot::Mesh>(*mesh);mesh.reset();
            assert(scene->shadows.bind(d,reconstructed,shadow.vb,shadow.ib,admit,shadow.at)&&shadow.vb[0]==material.vb[0]);
        }
        assert(w.captureSources().sharedSourceLimit()==originalLimit&&w.captureSources().sharedSourceParticipants()==1);
        assert(scene->native.bytes()==residentBytes);scene->shadows.clear();assert(!scene->native.bytes()&&!scene->native.chargedBytes());
        bool rejected=false;try{NorthlightGpuScene::forDevice(reinterpret_cast<IDirect3DDevice9*>(uintptr_t(1)),scene);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
        std::puts("GPU_SCENE_OWNERSHIP_PASS actual_world_materials_displacement one_source_bank unchanged_caps one_upload consumer_reset reconstructed_source typed_views foreign_device_rejected");
    }
    static void pageIndexImage(WorldRenderer& w,IDirect3DDevice9* d){
        struct Vertex {float x,y,z,rhw;DWORD color;};
        const Vertex v[]={{5,5,.5f,1,0xffff0000},{58,5,.5f,1,0xff00ff00},{58,58,.5f,1,0xff0000ff},{5,58,.5f,1,0xffffffff}};
        const uint16_t narrow[]={0,1,2,0,2,3};const uint32_t wide[]={0,1,2,0,2,3};
        NorthlightMeshPageKey::Key key;key.lo=123;key.hi=456;key.vertexBytes=sizeof(v);key.indexBytes=sizeof(narrow);
        assert(w.meshStore.beginStaging({key}));w.syncMeshBuffers();const auto id=w.meshStore.staging().uploadBuffer[0];assert(w.meshStore.markWriting(id));
        assert(w.acquireMeshPage(id,false,sizeof(narrow)));auto* ib16=w.meshBuffers[id].ib;D3DINDEXBUFFER_DESC desc{};assert(SUCCEEDED(ib16->GetDesc(&desc))&&desc.Format==D3DFMT_INDEX16);
        void* ptr=nullptr;assert(SUCCEEDED(ib16->Lock(0,sizeof(narrow),&ptr,0))&&ptr);std::memcpy(ptr,narrow,sizeof(narrow));assert(SUCCEEDED(ib16->Unlock()));
        IDirect3DIndexBuffer9* ib32=nullptr;IDirect3DVertexBuffer9* vb=nullptr;IDirect3DSurface9 *target=nullptr,*read=nullptr,*old=nullptr,*oldDepth=nullptr;IDirect3DStateBlock9* state=nullptr;
        assert(SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL,&state))&&SUCCEEDED(state->Capture()));assert(SUCCEEDED(d->GetRenderTarget(0,&old)));d->GetDepthStencilSurface(&oldDepth);
        assert(SUCCEEDED(d->CreateIndexBuffer(sizeof(wide),0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib32,nullptr)));assert(SUCCEEDED(ib32->Lock(0,sizeof(wide),&ptr,0))&&ptr);std::memcpy(ptr,wide,sizeof(wide));assert(SUCCEEDED(ib32->Unlock()));
        assert(SUCCEEDED(d->CreateVertexBuffer(sizeof(v),0,0,D3DPOOL_MANAGED,&vb,nullptr)));assert(SUCCEEDED(vb->Lock(0,sizeof(v),&ptr,0))&&ptr);std::memcpy(ptr,v,sizeof(v));assert(SUCCEEDED(vb->Unlock()));
        assert(SUCCEEDED(d->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr)));assert(SUCCEEDED(d->CreateOffscreenPlainSurface(64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr)));
        assert(SUCCEEDED(d->SetRenderTarget(0,target))&&SUCCEEDED(d->SetDepthStencilSurface(nullptr)));assert(SUCCEEDED(d->SetVertexShader(nullptr))&&SUCCEEDED(d->SetPixelShader(nullptr))&&SUCCEEDED(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE)));
        assert(SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_LIGHTING,FALSE)));
        assert(SUCCEEDED(d->SetTexture(0,nullptr))&&SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1))&&SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE)));assert(SUCCEEDED(d->SetStreamSource(0,vb,0,sizeof(Vertex))));
        std::vector<unsigned char> images[2];for(unsigned mode=0;mode<2;++mode){assert(SUCCEEDED(d->SetIndices(mode?ib16:ib32)));assert(SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0)));assert(SUCCEEDED(d->BeginScene()));assert(SUCCEEDED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2)));assert(SUCCEEDED(d->EndScene()));assert(SUCCEEDED(d->GetRenderTargetData(target,read)));D3DLOCKED_RECT locked{};assert(SUCCEEDED(read->LockRect(&locked,nullptr,D3DLOCK_READONLY)));images[mode].resize(64*64*4);for(unsigned y=0;y<64;++y)std::memcpy(images[mode].data()+y*64*4,static_cast<const char*>(locked.pBits)+y*locked.Pitch,64*4);assert(SUCCEEDED(read->UnlockRect()));}
        assert(images[0]==images[1]&&std::any_of(images[0].begin(),images[0].end(),[](unsigned char x){return x!=0;}));assert(SUCCEEDED(d->SetRenderTarget(0,old)));assert(SUCCEEDED(d->SetDepthStencilSurface(oldDepth)));assert(SUCCEEDED(state->Apply()));drop(oldDepth);drop(old);drop(target);drop(read);drop(vb);drop(ib32);drop(state);w.meshStore.cancel();w.releaseMeshPool();
        std::puts("WORLD_PAGE_INDEX16_IMAGE_PASS equal32=1 productionBuffer=1 activeUnchanged=1");
    }
    static void coldGpuAtAddressPressure(WorldRenderer& w,bool legacyBudget,bool inject,unsigned& indexCreates,unsigned& failIndexAt){
        using namespace NorthlightGeometryMemory;namespace P=NorthlightWorldMeshPages;using NorthlightGI::WorldVertex;
        auto pages=std::make_shared<P::Plan>();constexpr unsigned count=60,vertices=10000,indices=30000;
        for(unsigned n=0;n<count;++n){P::Page page;page.vertices.resize(vertices);page.indices.resize(indices);for(unsigned i=0;i<vertices;++i){page.vertices[i].position={float(i),float(n),0};page.vertices[i].normal={0,0,1};}for(unsigned i=0;i<indices;++i)page.indices[i]=uint16_t((i*7+n)%vertices);
            page.key=NorthlightMeshPageKey::keyOf(page.vertices.data(),page.vertices.size()*sizeof(WorldVertex),page.indices.data(),page.indices.size()*sizeof(uint16_t));pages->vertexBytes+=page.key.vertexBytes;pages->indexBytes+=page.key.indexBytes;pages->pages.push_back(std::move(page));
            P::Batch batch;batch.page=n;batch.start=0;batch.count=indices/3;batch.material=0;batch.minVertex=0;batch.vertexCount=vertices;pages->batches.push_back(batch);}
        auto plan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();plan->vertexCount=count*vertices;plan->triangleCount=count*indices/3;plan->vertexBytes=uint64_t(plan->vertexCount)*sizeof(WorldVertex);plan->indexBytes=uint64_t(plan->triangleCount)*3*sizeof(uint32_t);plan->textureBytes=4;plan->pagesSealed=true;
        auto payload=std::make_shared<NorthlightWorldMesh::UploadPayload>();payload->pages=pages;payload->materials.resize(1);payload->materials[0].bgra={255,255,255,255};payload->bytes=pages->cpuBytes()+4;plan->payload.set(payload);assert(!P::validateSealed(*plan));
        w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.active->meshPlan=plan;w.active->map="pressure-test";w.active->serial=0;assert(!w.geometryTargetsReady&&w.meshStore.table().empty());payload.reset();pages.reset();
        struct Ballast {std::vector<void*> blocks;~Ballast(){for(auto p:blocks)assert(VirtualFree(p,0,MEM_RELEASE));}} ballast;MEMORYSTATUSEX status{};status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));
        while(status.ullAvailVirtual>395*MiB+65536){uint64_t amount=std::min<uint64_t>(16*MiB,(status.ullAvailVirtual-395*MiB)&~uint64_t(65535));assert(amount);auto p=VirtualAlloc(nullptr,size_t(amount),MEM_RESERVE,PAGE_NOACCESS);assert(p);ballast.blocks.push_back(p);status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));}
        uint64_t minimum=status.ullAvailVirtual;unsigned frames=0;bool completed=false,sawFailure=false;
        if(inject)failIndexAt=indexCreates+3;
        for(;frames<1000&&!completed;++frames){NorthlightStreaming::Budget budget(legacyBudget?2u<<20:4u<<20,legacyBudget?.5:2.0);completed=w.upload(budget);status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));minimum=std::min<uint64_t>(minimum,status.ullAvailVirtual);assert(minimum>=ProcessReserve+16*MiB);
            if(w.meshRetry.waiting&&!w.pendingMesh){assert(inject&&!sawFailure&&!completed&&w.meshStore.table().empty()&&plan->payload.live());sawFailure=true;failIndexAt=0;w.meshRetry.failedAt-=1000;}}
        assert(sawFailure==inject&&completed&&w.meshStore.table().size()==count&&!plan->payload.live()&&!w.geometryTargetsReady);
        for(auto id:w.meshStore.table()){D3DINDEXBUFFER_DESC desc{};assert(SUCCEEDED(w.meshBuffers[id].ib->GetDesc(&desc))&&desc.Format==D3DFMT_INDEX16);}
        assert(w.geometryResources(256,160));status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));minimum=std::min<uint64_t>(minimum,status.ullAvailVirtual);assert(minimum>=ProcessReserve+16*MiB);
        NorthlightStreaming::Budget finalBudget;assert(w.upload(finalBudget));
        std::printf("WORLD_COLD_GPU_PRESSURE_PASS entryMiB=395 minimumMiB=%llu pages=%u frames=%u budget=%s injectedFailure=%u recovered=1 sceneTargetsAfterMesh=1 reserveIntact=1\n",(unsigned long long)(minimum>>20),count,frames,legacyBudget?"previous":"cold",unsigned(inject));
        w.active.reset();w.clearMesh();w.releaseMeshPool();w.releaseGeometryTargets();w.meshRetry.clear();
    }
    static void reuseAtAddressPressure(WorldRenderer& w){
        using namespace NorthlightGeometryMemory;
        using NorthlightMeshPageKey::Key;
        auto key=[](unsigned n){Key k;k.lo=n+1;k.hi=~uint64_t(n);k.vertexBytes=300000;k.indexBytes=100000;return k;};
        std::vector<Key> first,next;for(unsigned n=0;n<64;++n){first.push_back(key(n));next.push_back(key(n+1000));}
        auto create=[&](const std::vector<Key>& keys){
            assert(w.meshStore.beginStaging(keys));w.syncMeshBuffers();
            for(size_t i=0;i<w.meshStore.staging().upload.size();++i){const auto id=w.meshStore.staging().uploadBuffer[i];const auto& k=w.meshStore.staging().keys[w.meshStore.staging().upload[i]];
                assert(w.meshStore.markWriting(id));assert(w.acquireMeshPage(id,true,k.vertexBytes));assert(w.acquireMeshPage(id,false,k.indexBytes));}
            w.meshStore.commit();
        };
        create(first);create({key(999)});assert(w.meshStore.beginStaging(next));w.syncMeshBuffers();
        uint64_t vb=0,ib=0;auto has=[&](uint32_t id,bool vertex){return id<w.meshBuffers.size()&&(vertex?w.meshBuffers[id].vb!=nullptr:w.meshBuffers[id].ib!=nullptr);};
        assert(w.meshStore.stagingGrowth(has,vb,ib)&&vb==0&&ib==0);
        struct Ballast {std::vector<void*> blocks;~Ballast(){for(auto p:blocks)assert(VirtualFree(p,0,MEM_RELEASE));}} ballast;
        // Test aggregate admission independently of driver fragmentation and
        // delayed VA releases. Re-establish pressure against the actual two
        // budgets, keeping a contiguous witness until each admission sample.
        auto oldBudget=uploadBudget(64ull*300000,64ull*100000,MiB,MiB);oldBudget.largest=18*MiB;
        auto newBudget=uploadBudget(vb,ib,MiB,MiB);newBudget.largest=18*MiB;
        const uint64_t target=(oldBudget.available+newBudget.available)/2;
        bool exact=false;Sample sample;MEMORYSTATUSEX status{};
        for(unsigned attempt=0;attempt<8;++attempt){
            auto* witness=VirtualAlloc(nullptr,18*MiB,MEM_RESERVE,PAGE_NOACCESS);assert(witness);
            status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));
            const uint64_t heldTarget=target-18*MiB;
            while(status.ullAvailVirtual>heldTarget+65536){uint64_t bytes=std::min<uint64_t>(16*MiB,(status.ullAvailVirtual-heldTarget)&~uint64_t(65535));assert(bytes);auto p=VirtualAlloc(nullptr,size_t(bytes),MEM_RESERVE,PAGE_NOACCESS);assert(p);ballast.blocks.push_back(p);status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));}
            assert(VirtualFree(witness,0,MEM_RELEASE));sample=w.geometryAdmission(newBudget,&exact);
            if(sample.valid&&!admits(sample,oldBudget)&&admits(sample,newBudget))break;
            // A late driver allocation can cross the lower edge instead. Give
            // back one ballast block before establishing the same target again.
            if(sample.available<newBudget.available&&!ballast.blocks.empty()){assert(VirtualFree(ballast.blocks.back(),0,MEM_RELEASE));ballast.blocks.pop_back();}
        }
        if(!(sample.valid&&!admits(sample,oldBudget)&&admits(sample,newBudget)))std::fprintf(stderr,"PRESSURE_FIXTURE available=%llu largest=%llu oldAvailable=%llu newAvailable=%llu exact=%u valid=%u\n",(unsigned long long)sample.available,(unsigned long long)sample.largest,(unsigned long long)oldBudget.available,(unsigned long long)newBudget.available,unsigned(exact),unsigned(sample.valid));
        assert(sample.valid&&!admits(sample,oldBudget)&&admits(sample,newBudget));
        auto original=w.meshStore.table();uint64_t minimum=sample.available;
        for(size_t i=0;i<w.meshStore.staging().upload.size();++i){const auto id=w.meshStore.staging().uploadBuffer[i];const auto& k=w.meshStore.staging().keys[w.meshStore.staging().upload[i]];
            auto* v=w.meshBuffers[id].vb;auto* index=w.meshBuffers[id].ib;assert(v&&index&&w.meshStore.markWriting(id));
            assert(w.acquireMeshPage(id,true,k.vertexBytes)&&w.acquireMeshPage(id,false,k.indexBytes));assert(v==w.meshBuffers[id].vb&&index==w.meshBuffers[id].ib);
            void* bytes=nullptr;assert(SUCCEEDED(v->Lock(0,k.vertexBytes,&bytes,0))&&bytes);std::memset(bytes,int(i),k.vertexBytes);assert(SUCCEEDED(v->Unlock()));
            bytes=nullptr;assert(SUCCEEDED(index->Lock(0,k.indexBytes,&bytes,0))&&bytes);std::memset(bytes,int(i),k.indexBytes);assert(SUCCEEDED(index->Unlock()));v->PreLoad();index->PreLoad();
            status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));minimum=std::min<uint64_t>(minimum,status.ullAvailVirtual);assert(minimum>=ProcessReserve+16*MiB);
        }
        assert(w.meshStore.table()==original);w.meshStore.cancel();assert(w.meshStore.table()==original);
        std::printf("WORLD_REUSED_PAGES_PRESSURE_PASS availableMiB=%llu minimumMiB=%llu pages=64 newBytes=0 oldBudgetMiB=%llu newBudgetMiB=%llu activePreserved=1\n",(unsigned long long)(sample.available>>20),(unsigned long long)(minimum>>20),(unsigned long long)(oldBudget.available>>20),(unsigned long long)(newBudget.available>>20));
        w.releaseMeshPool();
    }
    static void setup(WorldRenderer& w,unsigned width,unsigned height){
        assert(w.resources(width,height,D3DFMT_A16B16G16R16F));
        scratch(w,false);
        fixtureContext(w);w.frames=599;NorthlightDiagnostics::configure(true);w.lastRequest.map="Northrend";
        w.quality.equipmentLights=0;w.quality.horizonHaze=0;w.quality.fogClouds=0;
        w.effects.gi=true;w.effects.shadows=true;
        auto field=std::make_shared<NorthlightRegionalFog::Field>();field->originX=field->originY=-256;field->fogCells=field->airCells=4096;
        for(auto& t:field->texels){t.ground=-2;t.day=.015f;t.height=2.5f;}
        auto region=std::make_shared<WorldRenderer::FogRegion>();region->map=w.lastRequest.map;region->center={0,0,0};region->field=field;
        {std::lock_guard<std::mutex> lock(w.mutex);w.publishedFogRegion=std::move(region);}
        assert(!fogField(w)&&!fogTexture(w)); // render() must upload without an active BVH
        assert(!w.ready()&&w.screenReady()&&!w.active&&!w.vertices&&!w.indices);
    }
    static bool resources(WorldRenderer& w,unsigned width,unsigned height){return w.resources(width,height,D3DFMT_A16B16G16R16F);}
    static void screenOnlyGeometry(WorldRenderer& w,IDirect3DDevice9* d,IDirect3DSurface9* out,IDirect3DSurface9* resolved,IDirect3DSurface9* read,
                                   IDirect3DTexture9* depth,IDirect3DTexture9* scene,IDirect3DTexture9* ao,unsigned width,unsigned height){
        namespace P=NorthlightWorldMeshPages;using NorthlightGI::WorldVertex;
        const auto quality=w.quality;const auto effects=w.effects;
        auto pages=std::make_shared<P::Plan>();P::Page page;page.vertices.resize(3);page.indices={0,1,2};
        page.vertices[0].position={0,0,5};page.vertices[1].position={1,0,5};page.vertices[2].position={0,1,5};
        for(auto& v:page.vertices)v.normal={0,0,1};
        page.key=NorthlightMeshPageKey::keyOf(page.vertices.data(),3*sizeof(WorldVertex),page.indices.data(),3*sizeof(uint16_t));
        pages->vertexBytes=page.key.vertexBytes;pages->indexBytes=page.key.indexBytes;pages->pages.push_back(std::move(page));
        P::Batch batch;batch.page=0;batch.count=1;batch.vertexCount=3;pages->batches.push_back(batch);
        auto plan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();plan->vertexCount=3;plan->triangleCount=1;
        plan->vertexBytes=3*sizeof(WorldVertex);plan->indexBytes=3*sizeof(uint32_t);plan->textureBytes=4;plan->pagesSealed=true;
        auto payload=std::make_shared<NorthlightWorldMesh::UploadPayload>();payload->pages=pages;payload->materials.resize(1);payload->materials[0].bgra={255,255,255,255};payload->bytes=pages->cpuBytes()+4;plan->payload.set(payload);assert(!P::validateSealed(*plan));
        auto cpuScene=std::make_shared<WorldRenderer::Snapshot>();cpuScene->bvh=std::make_shared<NorthlightGI::BVH>();cpuScene->meshPlan=plan;cpuScene->map="screen-only";
        w.active=cpuScene;w.lastRequest.map=cpuScene->map;fixtureContext(w);
        w.quality.gi=w.quality.worldShadows=w.quality.worldFog=w.quality.localLights=w.quality.contactAO=w.quality.autoBump=0;
        w.quality.weather=1;w.effects={false,false,false};w.materialDetail=nullptr;
        std::vector<unsigned char> reference;
        for(unsigned mode=0;mode<6;++mode){
            w.quality.contactAO=mode==1;w.quality.autoBump=mode==2;
            if(mode==3){w.quality.gi=1;w.quality.giStrength=0;w.effects.gi=true;}
            if(mode==4){w.quality.worldFog=1;w.effects.fog=true;}
            if(mode==5){w.quality.worldFog=0;w.effects.fog=false;w.quality.giStrength=100;}
            historyValid(w)=fogHistoryValid(w)=false;
            for(unsigned attempt=0;attempt<32;++attempt){
                historyValid(w)=fogHistoryValid(w)=false;
                assert(SUCCEEDED(d->SetRenderTarget(0,out))&&SUCCEEDED(d->SetDepthStencilSurface(nullptr))&&SUCCEEDED(d->BeginScene()));
                assert(w.render(out,depth,width,height,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));assert(SUCCEEDED(d->EndScene()));
                if(mode<5||!w.meshStore.table().empty())break;
            }
            if(mode<5)assert(w.meshStore.table().empty()&&!w.pendingMesh&&!w.geometryTargetsReady&&plan->payload.live());
            else assert(w.meshStore.table().size()==1&&w.geometryTargetsReady&&!plan->payload.live());
            assert(w.active==cpuScene); // CPU scene remains available to weather shelter.
            if(resolved)assert(SUCCEEDED(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE)));
            assert(SUCCEEDED(d->GetRenderTargetData(resolved?resolved:out,read)));D3DLOCKED_RECT lock{};assert(SUCCEEDED(read->LockRect(&lock,nullptr,D3DLOCK_READONLY)));
            std::vector<unsigned char> pixels(width*height*8);for(unsigned y=0;y<height;++y)std::memcpy(pixels.data()+y*width*8,static_cast<const char*>(lock.pBits)+y*lock.Pitch,width*8);assert(SUCCEEDED(read->UnlockRect()));
            if(!mode)reference=std::move(pixels);else if(mode<4){if(pixels!=reference)std::fprintf(stderr,"Screen-only equality failed in mode %u\n",mode);assert(pixels==reference);}
        }
        w.active.reset();w.clearMesh();w.releaseMeshPool();w.releaseGeometryTargets();w.quality=quality;w.effects=effects;historyValid(w)=fogHistoryValid(w)=false;
        std::puts("WORLD_SCREEN_GPU_DEMAND_PASS weatherAOmaterialsZeroGIFog=0Uploads cpuSceneRetained=1 reactivation=1 exactPixels=1");
    }
    static void assertScreenReleased(const WorldRenderer& w){
        assert(!w.width&&!w.height);
#ifdef NORTHLIGHT_SHADOW_PROGRAM_OWNER_TEST
        assert(!w.shadowPrograms.liveLayout());
#else
        assert(!w.liveTerrainDecl);
#endif
        assert(!w.composite.standardShader()&&lightingTargetsAbsent(w));
        assert(normalTargetsAbsent(w)&&normalShadersAbsent(w)&&lightTargetsAbsent(w));
#ifdef NORTHLIGHT_REGIONAL_FOG_GPU_TEST
        assert(!w.regionalFogGpu.base());
#else
        assert(!w.fogPS);
#endif
#ifdef NORTHLIGHT_LEGACY_LAMP_OWNER_FIXTURE
        assert(!w.localFogPS&&!w.localDirectPS);
#else
        assert(!w.localLighting.localFogPS&&!w.localLighting.localDirectPS);
#endif
#ifdef NORTHLIGHT_LEGACY_FOREVER_FOG_FIXTURE
        assert(!w.foreverFogPS);
#else
        assert(!w.foreverFog.available());
#endif
        assert(!fogTargets(w).raw.texture&&!fogTargets(w).scratch.texture&&!fogTargets(w).history.texture);
    }
    static void screenRetry(WorldRenderer& w){
        assert(w.screenResourceRetry&&!w.failed);
        assertScreenReleased(w);
        w.screenResourceRetryAt-=1000;
    }
#ifdef NORTHLIGHT_LAZY_REMOVAL_TEST
    template<class Meter> static void removalDemand(WorldRenderer& w,Meter& meter){
        const unsigned width=meter.width,height=meter.height;
        const auto format=D3DFMT_A16B16G16R16F;
        const auto prepare=[&]{return w.resources(meter.width,meter.height,format,w.frameRequirements());};
        constexpr bool lazy=NORTHLIGHT_LAZY_REMOVAL_TEST;
        w.effects={false,false,false};
        w.quality.localLights=1;w.quality.localLightLimit=16;w.quality.shadowProvider=1;
        w.nativeShadowAvailable=true;
        assert(prepare());
        const unsigned cold=meter.screenCreates;
        assert(cold==(lazy?5:6));
        assert(bool(removalShader(w))==!lazy&&bool(fogTargets(w).scratch.texture)==!lazy);
        assert(!fogTargets(w).raw.texture&&!fogTargets(w).history.texture);
        if constexpr(lazy){
            // Fail the first scratch allocation when adding shadows.
            w.effects.shadows=true;
            meter.failScreenAt=cold+1;
            assert(!prepare()&&meter.screenCreates==cold+1);
            assert(!prepare()&&meter.screenCreates==cold+1);
            screenRetry(w);assert(!removalShader(w));
            meter.failScreenAt=0;
            const unsigned beforeRetry=meter.screenCreates;
            assert(prepare()&&meter.screenCreates==beforeRetry+6);
            const auto* shared=fogTargets(w).scratch.texture;
            assert(shared&&removalShader(w));

            const unsigned beforeFog=meter.screenCreates;
            w.effects={false,false,true};
            assert(prepare()&&meter.screenCreates==beforeFog+2);
            assert(fogTargets(w).scratch.texture==shared);
            w.effects={false,false,false};
            assert(prepare()&&meter.screenCreates==beforeFog+2);
            assert(fogTargets(w).scratch.texture==shared);

            meter.width-=2;meter.height-=2;
            const unsigned beforeResize=meter.screenCreates;
            assert(prepare()&&meter.screenCreates==beforeResize+5);
            assert(!removalShader(w)&&!fogTargets(w).scratch.texture);

            w.effects.fog=true;
            assert(prepare()&&meter.screenCreates==beforeResize+8);
            const auto* fogFirst=fogTargets(w).scratch.texture;
            assert(fogFirst&&!removalShader(w));
            w.effects.shadows=true;
            assert(prepare()&&meter.screenCreates==beforeResize+8);
            assert(fogTargets(w).scratch.texture==fogFirst&&removalShader(w));
            w.releaseGPU(true);assertScreenReleased(w);assert(!removalShader(w));
        }
        const auto bytes=uint64_t(width/2)*(height/2)*8;
        std::printf("WORLD_REMOVAL_DEMAND_PASS width=%u height=%u lightOnlyTargets=%u lightOnlyTargetBytes=%llu removalOnDemand=%u retryResizeAndReuse=%u\n",
            width,height,cold,static_cast<unsigned long long>(cold*bytes),unsigned(lazy),unsigned(lazy));
    }
#endif
    static bool uploadFog(WorldRenderer& w){return w.uploadRegionalFog(w.publishedFogRegion->field,w.lastRequest.map);}
#ifdef NORTHLIGHT_LEGACY_FOG_FIELD_FIXTURE
    static const std::shared_ptr<const NorthlightRegionalFog::Field>& fogField(const WorldRenderer& w){return w.uploadedFogField;}
    static IDirect3DTexture9* fogTexture(const WorldRenderer& w){return w.regionalFogTexture;}
    static auto& fogRetryPending(WorldRenderer& w){return w.fogUploadRetry;}
    static auto& fogRetryAt(WorldRenderer& w){return w.fogUploadRetryAt;}
    static auto& fogHeights(WorldRenderer& w){return w.fogGlobalHeights;}
#else
    static const std::shared_ptr<const NorthlightRegionalFog::Field>& fogField(const WorldRenderer& w){return w.regionalFog.field();}
    static IDirect3DTexture9* fogTexture(const WorldRenderer& w){return w.regionalFog.texture();}
    static auto& fogRetryPending(WorldRenderer& w){return w.regionalFog.retry_;}
    static auto& fogRetryAt(WorldRenderer& w){return w.regionalFog.retryAt_;}
    static auto& fogHeights(WorldRenderer& w){return w.regionalFog.globalHeights_;}
#endif
    static void fogRetry(WorldRenderer& w){
        assert(fogRetryPending(w)&&!w.failed&&!fogTexture(w)&&!fogField(w)&&w.screenReady());
        fogRetryAt(w)-=1000;
    }
    static void fogUploaded(WorldRenderer& w){
        auto* texture=fogTexture(w);
        assert(!fogRetryPending(w)&&fogField(w)==w.publishedFogRegion->field&&texture);
        D3DLOCKED_RECT lock{};
        assert(SUCCEEDED(texture->LockRect(0,&lock,nullptr,D3DLOCK_READONLY)));
        constexpr unsigned n=NorthlightRegionalFog::N;
        for(unsigned y=0;y<n;++y)
            assert(!std::memcmp(static_cast<const char*>(lock.pBits)+y*lock.Pitch,fogField(w)->texels.data()+y*n,n*sizeof(NorthlightRegionalFog::Texel)));
        assert(SUCCEEDED(texture->UnlockRect(0)));
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
    static void fog(WorldRenderer& w,bool on){w.effects.fog=on;fogHistoryValid(w)=false;}
    static void fogAltitude(WorldRenderer& w,float z,bool bounds){
        w.context.camera[2]=w.context.inverseView[14]=z;
        fogHeights(w)=bounds?NorthlightRegionalFog::globalHeightBounds(*fogField(w)):std::array<float,3>{};
    }
    static void neutralFogHistory(const WorldRenderer& w){assert(fogHistoryValid(w));}
    static std::shared_ptr<const NorthlightRegionalFog::Field> replaceFogField(WorldRenderer& w,std::shared_ptr<const NorthlightRegionalFog::Field> field){
        std::lock_guard<std::mutex> lock(w.mutex);auto old=w.publishedFogRegion->field;
        auto region=std::make_shared<WorldRenderer::FogRegion>(*w.publishedFogRegion);region->field=std::move(field);w.publishedFogRegion=std::move(region);return old;
    }
    static void cloudPressure(WorldRenderer& w){
        auto& noise=w.fogCloudNoise();noise.data=NorthlightFogClouds::generate();noise.quantiles=NorthlightFogClouds::quantiles(noise.data.data());noise.ready.store(true,std::memory_order_release);
        w.memoryPressure=true;assert(w.ensureCloudNoise()&&cloudNoiseTexture(w));w.memoryPressure=false;
        std::puts("WORLD_CLOUD_PRESSURE_PASS demandPressure=1 fullNoiseTexture=1");
    }
    static void ambientVariant(WorldRenderer& w){
#ifdef NORTHLIGHT_REGIONAL_FOG_GPU_TEST
        auto* ambient=w.fogVolumeShader(true,true);assert(ambient&&ambient==w.regionalFogGpu.ambient_);
        assert(w.fogVolumeShader(false,false)==w.regionalFogGpu.base());
        struct FogShaderFaults:ForwardIDirect3DDevice9 {
            using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
            const DWORD* fault=kWorldFogAmbientShader;
            unsigned attempts=0;
            HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{
                if(code!=fault)return real->CreatePixelShader(code,out);
                ++attempts;
                assert(SUCCEEDED(real->CreatePixelShader(code,out))&&*out);
                return E_OUTOFMEMORY;
            }
        } fault(w.d);
        NorthlightRegionalFogGpu fog;
        const auto pixel=[&](const DWORD* code,IDirect3DPixelShader9*& shader,const char*){
            return shader||SUCCEEDED(fault.CreatePixelShader(code,&shader));
        };
        assert(fog.createBase(pixel)&&fog.base());
        assert(fog.volumeShader(&fault,true,false)==fog.base()&&!fog.ambient_);
        assert(fog.volumeShader(&fault,true,false)==fog.base()&&fault.attempts==1);
        fault.fault=kFogCloudsAmbientShader;
        assert(!fog.ensureAmbientClouds(&fault)&&!fog.ambientClouds());
        assert(!fog.ensureAmbientClouds(&fault)&&fault.attempts==2);
        fault.fault=kFogCloudsActorShader;
        fog.createOptional(&fault);
        assert(fog.clouds()&&!fog.actorVariants()&&!fog.actor()&&!fog.actorClouds());
        fog.reset();
        assert(!fog.base()&&!fog.clouds());
        fault.fault=nullptr;
        assert(fog.createBase(pixel)&&fog.ensureAmbientClouds(&fault));
        assert(fog.volumeShader(&fault,true,false)!=fog.base());
        std::puts("WORLD_REGIONAL_FOG_SHADERS_PASS oom=ambient,clouds actorPair=atomic retryAfterReset=1");
#else
        auto* ambient=w.fogVolumeShader(true,true);assert(ambient&&ambient==w.fogAmbientPS);
        assert(w.fogVolumeShader(false,false)==w.fogPS);
        drop(w.fogAmbientPS);w.fogAmbientRefused=true;
        assert(w.fogVolumeShader(true,false)==w.fogPS&&!w.fogAmbientPS);
        w.fogAmbientRefused=false;assert(w.fogVolumeShader(true,false)==w.fogAmbientPS&&w.fogAmbientPS);
#endif
        std::puts("WORLD_FOG_AMBIENT_VARIANT_PASS zeroSource=1 residentReuse=1 optionalFallback=1");
#ifdef NORTHLIGHT_LEGACY_LAMP_OWNER_FIXTURE
        assert(w.localDirectShader(2)==w.localDirectPS&&!w.localDirectSinglePS);
        auto* single=w.localDirectShader(1);assert(single&&single!=w.localDirectPS&&w.localDirectShader(1)==single);
        drop(w.localDirectSinglePS);w.localDirectSingleRefused=true;
        assert(w.localDirectShader(1)==w.localDirectPS&&!w.localDirectSinglePS);
        w.localDirectSingleRefused=false;assert(w.localDirectShader(1)==w.localDirectSinglePS&&w.localDirectSinglePS);
#else
        assert(w.localLighting.localDirectShader(w.d,2)==w.localLighting.localDirectPS&&!w.localLighting.localDirectSinglePS);
        auto* single=w.localLighting.localDirectShader(w.d,1);assert(single&&single!=w.localLighting.localDirectPS&&w.localLighting.localDirectShader(w.d,1)==single);
        drop(w.localLighting.localDirectSinglePS);w.localLighting.localDirectSingleRefused=true;
        assert(w.localLighting.localDirectShader(w.d,1)==w.localLighting.localDirectPS&&!w.localLighting.localDirectSinglePS);
        w.localLighting.localDirectSingleRefused=false;assert(w.localLighting.localDirectShader(w.d,1)==w.localLighting.localDirectSinglePS&&w.localLighting.localDirectSinglePS);
#endif
        std::puts("WORLD_LOCAL_SINGLE_VARIANT_PASS exactCount=1 residentReuse=1 optionalFallback=1");
    }
    template<class F> static void probeTextures(const WorldRenderer& w,const F& visit){
#ifdef NORTHLIGHT_PROBE_ATLAS_OWNER_TEST
        w.probeAtlas.visitTextures(visit);
#else
        for(auto* texture:w.probe)visit(texture);
        visit(w.probePrev);
        visit(w.probeLocal);
#endif
    }
    static IDirect3DTexture9* probeChannel(const WorldRenderer& w,unsigned i){
#ifdef NORTHLIGHT_PROBE_ATLAS_OWNER_TEST
        return w.probeAtlas.channel(i);
#else
        return w.probe[i];
#endif
    }
    static IDirect3DTexture9* probeLocalVisibility(const WorldRenderer& w){
#ifdef NORTHLIGHT_PROBE_ATLAS_OWNER_TEST
        return w.probeAtlas.localVisibility();
#else
        return w.probeLocal;
#endif
    }
    static bool probeVisibilityChanged(const WorldRenderer& w,const std::vector<NorthlightGI::ProbeAtlasEntry>& atlas){
#ifdef NORTHLIGHT_PROBE_ATLAS_OWNER_TEST
        return w.probeAtlas.localMirror_.changed(atlas);
#else
        return w.probeLocalMirror.changed(atlas);
#endif
    }
#ifdef NORTHLIGHT_PROBE_ATLAS_OWNER_TEST
    static void probeAtlasReset(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& present){
        struct ProbeDevice:ForwardIDirect3DDevice9 {
            using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
            unsigned attempts=0,fault=0;
            HRESULT refused=E_OUTOFMEMORY;
            HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,
                                                     IDirect3DTexture9** out,HANDLE* shared)override{
                assert(pool==D3DPOOL_MANAGED&&levels==1&&usage==0);
                const HRESULT result=real->CreateTexture(w,h,levels,usage,format,pool,out,shared);
                return ++attempts==fault&&SUCCEEDED(result)?refused:result;
            }
        } calls(device);
        WorldRenderer w(&calls);
        auto& atlas=w.probeAtlas;
        const unsigned n=NorthlightGI::probeLayout().atlas,layers=NorthlightGI::ProbeCascadeCount;
        const auto local=NorthlightGI::LocalProbeVisibility::atlas(n,layers);
        const uint64_t coeff=uint64_t(n)*n*n*layers*16;
        const uint64_t sizes[]={coeff,coeff,coeff,6*coeff,coeff,uint64_t(local.width)*local.height*8,3*coeff};
        auto log=[](const char*,auto...){};
        HRESULT error=S_OK;
        auto check=[&](HRESULT hr,const char*){error=hr;return SUCCEEDED(hr);};
        auto always=[](uint64_t){return true;};
        for(unsigned i=0;i<7;++i){
            if(i==2){
                calls.fault=calls.attempts+1;
                assert(!atlas.create(&calls,always,check,log)&&error==E_OUTOFMEMORY&&!atlas.channel(i));
                assert(atlas.channel(0)&&atlas.channel(1));
                calls.fault=0;
            }
            unsigned granted=0;
            const unsigned before=calls.attempts;
            bool ready=atlas.create(&calls,[&](uint64_t bytes){if(granted++)return false;assert(bytes==sizes[i]);return true;},check,log);
            assert(ready==(i==6)&&calls.attempts==before+1);
        }
        std::vector<IDirect3DTexture9*> retained;
        atlas.visitTextures([&](auto* texture){assert(texture);retained.push_back(texture);});
        w.reset(true);
        assert(SUCCEEDED(device->Reset(&present)));
        atlas.validateRetained();
        const unsigned before=calls.attempts;
        assert(atlas.create(&calls,always,check,log)&&calls.attempts==before);
        unsigned index=0;
        atlas.visitTextures([&](auto* texture){assert(texture==retained[index++]);});

        drop(atlas.local_);
        assert(SUCCEEDED(device->CreateTexture(16,16,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&atlas.local_,nullptr)));
        atlas.validateRetained();
        assert(!atlas.localVisibility()&&atlas.channel(0)==retained[0]&&atlas.previous()==retained[retained.size()-2]);
        assert(atlas.create(&calls,always,check,log)&&calls.attempts==before+1);

        atlas.releaseTextures();
        calls.fault=calls.attempts+7;
        calls.refused=D3DERR_NOTAVAILABLE;
        assert(atlas.create(&calls,always,check,log)&&calls.attempts==calls.fault);
        assert(!atlas.previous()&&atlas.localVisibility()&&atlas.channel(0));
        std::puts("WORLD_PROBE_ATLAS_OWNER_PASS staged=7 partialOom=1 retainedReset=7 incompatibleRebuilt=1 optionalFallback=1");
    }
#endif
#ifdef NORTHLIGHT_REPLAY_PROBE_PASS_TEST
    static void replayProbePass(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& present,unsigned msaa){
        enum Command { UnbindDepth,BindTarget,BindDepth,Viewport,ClearTarget,Issue };
        struct ProbeDevice:ForwardIDirect3DDevice9 {
            using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
            std::vector<Command> commands;
            unsigned depths=0;
            bool failDepth=false,failSave=false,failClear=false;
            HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE aa,DWORD q,
                    BOOL discard,IDirect3DSurface9** out,HANDLE* shared)override{
                assert(w==1024&&h==1024&&f==D3DFMT_D24S8&&aa==D3DMULTISAMPLE_NONE&&q==0&&discard);
                ++depths;
                const auto hr=real->CreateDepthStencilSurface(w,h,f,aa,q,discard,out,shared);
                return failDepth&&SUCCEEDED(hr)?E_OUTOFMEMORY:hr;
            }
            HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type,IDirect3DStateBlock9** out)override{
                if(failSave){*out=nullptr;return E_FAIL;}
                return real->CreateStateBlock(type,out);
            }
            HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* surface)override{
                commands.push_back(surface?BindDepth:UnbindDepth);return real->SetDepthStencilSurface(surface);
            }
            HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD index,IDirect3DSurface9* surface)override{
                if(index==0)commands.push_back(BindTarget);
                return real->SetRenderTarget(index,surface);
            }
            HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* viewport)override{
                commands.push_back(Viewport);return real->SetViewport(viewport);
            }
            HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* rect,DWORD flags,D3DCOLOR color,float z,DWORD stencil)override{
                assert(n==0&&!rect&&flags==(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER)&&color==0xffffffff&&z==1&&stencil==0);
                commands.push_back(ClearTarget);
                const auto hr=real->Clear(n,rect,flags,color,z,stencil);
                return failClear&&SUCCEEDED(hr)?E_FAIL:hr;
            }
        } calls(device);
        struct Clock {static int64_t now(){static int64_t t=0;return ++t;}};
        auto world=std::make_unique<WorldRenderer>(&calls);
        auto exercise=[&](bool failures){
            SavedState original(device);assert(original.ok);
            IDirect3DSurface9 *color=nullptr,*depth=nullptr;
            const auto aa=D3DMULTISAMPLE_TYPE(msaa);
            assert(SUCCEEDED(device->CreateRenderTarget(80,64,D3DFMT_X8R8G8B8,aa,0,FALSE,&color,nullptr)));
            assert(SUCCEEDED(device->CreateDepthStencilSurface(80,64,D3DFMT_D24S8,aa,0,TRUE,&depth,nullptr)));
            const D3DVIEWPORT9 viewport={3,4,72,54,.125f,.875f};
            assert(SUCCEEDED(device->SetDepthStencilSurface(nullptr))&&SUCCEEDED(device->SetRenderTarget(0,color))&&
                   SUCCEEDED(device->SetDepthStencilSurface(depth))&&SUCCEEDED(device->SetViewport(&viewport))&&
                   SUCCEEDED(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE)));
            unsigned issues=0,errors=0;const char* errorStage=nullptr;
            const auto run=[&]{
                calls.commands.clear();
                const auto timing=world->replayProbePass.run<Clock>(&calls,world->stateBlocks,[&]{
                    calls.commands.push_back(Issue);++issues;
                    assert(SUCCEEDED(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE)));
                },[&](const char* stage,HRESULT){++errors;errorStage=stage;});
                IDirect3DSurface9 *rt=nullptr,*ds=nullptr;D3DVIEWPORT9 vp{};DWORD blend=0;
                assert(SUCCEEDED(device->GetRenderTarget(0,&rt))&&rt==color);
                assert(SUCCEEDED(device->GetDepthStencilSurface(&ds))&&ds==depth);
                assert(SUCCEEDED(device->GetViewport(&vp))&&!std::memcmp(&vp,&viewport,sizeof vp));
                assert(SUCCEEDED(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))&&blend==TRUE);
                rt->Release();ds->Release();
                return timing;
            };
            if(failures){
                calls.failDepth=true;
                assert(!run().issued&&issues==0&&errors==1&&std::strcmp(errorStage,"probe targets")==0&&calls.commands.empty());
                calls.failDepth=false;calls.failSave=true;
                assert(!run().issued&&issues==0&&errors==2&&std::strcmp(errorStage,"probe state save")==0&&calls.commands.empty());
                calls.failSave=false;calls.failClear=true;
                assert(!run().issued&&issues==0&&errors==3&&std::strcmp(errorStage,"probe target binding")==0);
                calls.failClear=false;
            }
            const auto timing=run();
            const std::vector<Command> expected={UnbindDepth,BindTarget,BindDepth,Viewport,ClearTarget,Issue};
            assert(calls.commands.size()>expected.size()&&std::equal(expected.begin(),expected.end(),calls.commands.begin()));
            assert(timing.issued&&timing.setup==1&&timing.issue==1&&timing.restore==1&&issues==1&&errors==(failures?3u:0u));
            color->Release();depth->Release();
        };
        exercise(true);
        assert(calls.depths==2);
        world->reset(true);
        assert(SUCCEEDED(device->Reset(&present)));
        exercise(false);
        assert(calls.depths==3);
        std::printf("WORLD_REPLAY_PROBE_PASS ordered=1 partialOom=1 stateRefusal=1 clearRefusal=1 restored=1 reset=1 msaa=%u\n",msaa);
    }
#endif
#ifdef NORTHLIGHT_SCREEN_FALLBACKS_TEST
    static void screenFallbackResources(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& present){
        struct FallbackDevice:ForwardIDirect3DDevice9 {
            using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
            unsigned attempts=0,failAt=0,failThrough=0;
            HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,
                    D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared)override{
                assert(levels==1&&!usage&&pool==D3DPOOL_MANAGED&&!shared);
                assert((w==1&&h==1&&format==D3DFMT_A8R8G8B8)||
                       (w==NorthlightDitherNoise::N&&h==w&&format==D3DFMT_A32B32G32R32F));
                const auto hr=real->CreateTexture(w,h,levels,usage,format,pool,out,shared);
                ++attempts;
                return attempts>=failAt&&attempts<=failThrough&&SUCCEEDED(hr)?E_OUTOFMEMORY:hr;
            }
        } calls(device);
        auto world=std::make_unique<WorldRenderer>(&calls);
        auto& textures=world->screenFallbacks;
        unsigned refusals=0;const char* last=nullptr;
        auto check=[&](HRESULT result,const char* action){
            if(SUCCEEDED(result))return true;
            assert(result==E_OUTOFMEMORY);++refusals;last=action;return false;
        };
        calls.failAt=calls.failThrough=1;
        assert(!textures.create(&calls,check)&&calls.attempts==1&&!textures.ao()&&!textures.white());
        assert(refusals==1&&std::strcmp(last,"neutral AO texture")==0);
        calls.failAt=calls.failThrough=3;
        assert(!textures.create(&calls,check)&&calls.attempts==3&&textures.ao()&&!textures.white());
        assert(refusals==2&&std::strcmp(last,"neutral shadow texture")==0);
        calls.failAt=5;calls.failThrough=6;
        assert(textures.create(&calls,check)&&calls.attempts==6&&textures.white()&&!textures.zero()&&!textures.dither());
        assert(refusals==2);
        calls.failAt=calls.failThrough=0;
        assert(textures.create(&calls,check)&&calls.attempts==8);
        assert(textures.create(&calls,check)&&calls.attempts==8);
        const auto verifyPixels=[&]{
            for(auto texture:{textures.ao(),textures.white(),textures.zero()}){
                assert(texture);D3DLOCKED_RECT lock{};
                assert(SUCCEEDED(texture->LockRect(0,&lock,nullptr,D3DLOCK_READONLY)));
                const DWORD color=texture==textures.ao()?0xff000000u:texture==textures.white()?0xffffffffu:0u;
                assert(*static_cast<const DWORD*>(lock.pBits)==color);
                assert(SUCCEEDED(texture->UnlockRect(0)));
            }
            auto* dither=textures.dither();assert(dither);
            D3DLOCKED_RECT lock{};assert(SUCCEEDED(dither->LockRect(0,&lock,nullptr,D3DLOCK_READONLY)));
            constexpr unsigned n=NorthlightDitherNoise::N;
            const auto expected=NorthlightDitherNoise::offsets();
            for(unsigned y=0;y<n;++y){
                const auto* row=reinterpret_cast<const float*>(static_cast<const char*>(lock.pBits)+size_t(y)*lock.Pitch);
                for(unsigned x=0;x<n;++x){
                    const float value=expected[y*n+x];
                    assert(row[4*x]==value&&row[4*x+1]==value&&row[4*x+2]==value&&row[4*x+3]==0);
                }
            }
            assert(SUCCEEDED(dither->UnlockRect(0)));
        };
        verifyPixels();
        auto* white=textures.white();
        textures.releaseScreen();
        assert(!textures.ao()&&textures.white()==white&&!textures.zero()&&!textures.dither());
        assert(textures.create(&calls,check)&&calls.attempts==11&&textures.white()==white);
        world->reset(true);
        assert(!textures.ao()&&!textures.white()&&!textures.zero()&&!textures.dither());
        assert(SUCCEEDED(device->Reset(&present)));
        assert(textures.create(&calls,check)&&calls.attempts==15);
        verifyPixels();
        std::puts("WORLD_SCREEN_FALLBACKS_PASS order=exact partialOom=4 optionalFallback=1 reuse=1 texels=exact reset=1");
    }
#endif
    static void noSceneTargets(const WorldRenderer& w){
        assert(!w.geometryTargetsReady);
        directionalTargets(w,[](auto* t){assert(!t);},[](auto* s){assert(!s);});
        probeTextures(w,[](auto* texture){assert(!texture);});actorTextures(w,[](auto* t){assert(!t);});
    }
    static uint64_t sceneBytes(const WorldRenderer& w){
        using namespace NorthlightResourceMemory;Inventory i;
        directionalTargets(w,[&](auto* t){i.texture(t,ShadowMaps);},[&](auto* s){i.surface(s,ShadowMaps);});
        actorTextures(w,[&](auto* t){i.texture(t,ShadowMaps);});
        probeTextures(w,[&](auto* texture){i.texture(texture,ProbeTextures);});
        assert(i.complete());return i.groups[ShadowMaps]+i.groups[ProbeTextures];
    }
    static uint64_t probeBytes(const WorldRenderer& w){NorthlightResourceMemory::Inventory i;probeTextures(w,[&](auto* texture){i.texture(texture,NorthlightResourceMemory::ProbeTextures);});assert(i.complete());return i.groups[NorthlightResourceMemory::ProbeTextures];}
    static void fakeScene(WorldRenderer& w,IDirect3DVertexBuffer9* vb,IDirect3DIndexBuffer9* ib,bool shadows=true){
        w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map=w.lastRequest.map;w.active->bvh=std::make_shared<NorthlightGI::BVH>();
        w.active->serial=1;w.active->atlas.resize(NorthlightGI::probeLayout().atlasSize());auto& entry=w.active->atlas[0];entry.occupied=true;entry.probe.valid=true;entry.probe.sh[0]={.3f,.4f,.5f};
        auto visibility=std::make_shared<NorthlightGI::ProbeLocalVisibility>();
        for(auto& p:visibility->texels)p={0x3800,0,0,0x3c00};entry.probe.localVisibility=visibility;
        auto coarse=std::make_shared<std::vector<NorthlightGI::ProbeAtlasEntry>>(w.active->atlas);
        (*coarse)[0].probe.sh[0]={.7f,.8f,.9f};w.active->coarseAtlas=coarse;
        w.uploaded=w.active->bvh;w.uploadedMap=w.active->map;w.vertices=vb;w.indices=ib;w.effects.shadows=shadows;w.effects.gi=false;
    }
    static void retryScene(WorldRenderer& w){w.geometryTargetsRetryAt-=1000;}
    static bool sceneDrawn(const WorldRenderer& w){return w.fogGeometryReady;}
    static IDirect3DTexture9* screenFog(const WorldRenderer& w){return fogTargets(w).raw.texture;}
    static void pressure(WorldRenderer& w,bool on){w.memoryPressure=on;}
    static void expireFakeScene(WorldRenderer& w){w.active.reset();w.expiredMeshPending=true;}
    static std::vector<unsigned char> probeImage(WorldRenderer& w){
        assert(w.uploadedSerial==1&&probeChannel(w,0));D3DSURFACE_DESC desc{};assert(SUCCEEDED(probeChannel(w,0)->GetLevelDesc(0,&desc)));D3DLOCKED_RECT lock{};
        assert(SUCCEEDED(probeChannel(w,0)->LockRect(0,&lock,nullptr,D3DLOCK_READONLY)));std::vector<unsigned char> image(size_t(desc.Width)*desc.Height*16);
        for(unsigned y=0;y<desc.Height;++y)std::memcpy(image.data()+size_t(y)*desc.Width*16,static_cast<const char*>(lock.pBits)+size_t(y)*lock.Pitch,size_t(desc.Width)*16);
        assert(SUCCEEDED(probeChannel(w,0)->UnlockRect(0)));
        const unsigned n=NorthlightGI::probeLayout().atlas;
        assert(desc.Height==2*n);float fineValue,coarseValue;
        std::memcpy(&fineValue,image.data(),4);std::memcpy(&coarseValue,image.data()+size_t(n)*desc.Width*16,4);
        assert(fineValue==.3f&&coarseValue==.7f);
        auto combined=w.active->atlas;combined.insert(combined.end(),w.active->coarseAtlas->begin(),w.active->coarseAtlas->end());
        assert(probeLocalVisibility(w)&&!probeVisibilityChanged(w,combined));
        assert(SUCCEEDED(probeLocalVisibility(w)->LockRect(0,&lock,nullptr,D3DLOCK_READONLY)));
        for(unsigned y=0;y<9;++y)assert(!std::memcmp(static_cast<const char*>(lock.pBits)+size_t(y)*lock.Pitch,w.active->atlas[0].probe.localVisibility->texels.data()+y*9,9*8));
        const auto layout=NorthlightGI::LocalProbeVisibility::atlas(n,2);const size_t index=size_t(n)*n*n;
        const unsigned cx=unsigned(index%layout.columns)*9,cy=unsigned(index/layout.columns)*9;
        for(unsigned y=0;y<9;++y)assert(!std::memcmp(static_cast<const char*>(lock.pBits)+size_t(cy+y)*lock.Pitch+cx*8,(*w.active->coarseAtlas)[0].probe.localVisibility->texels.data()+y*9,9*8));
        std::array<uint16_t,4> empty;std::memcpy(empty.data(),static_cast<const char*>(lock.pBits)+9*8,8);assert(empty[3]==0xbc00);
        assert(SUCCEEDED(probeLocalVisibility(w)->UnlockRect(0)));return image;
    }
    static void probeRetired(const WorldRenderer& w){assert(!w.uploadedSerial);}
    static void removeFakeScene(WorldRenderer& w){
        w.vertices=nullptr;w.indices=nullptr;w.uploaded.reset();w.uploadedMap.clear();w.active.reset();w.releaseGeometryTargets();w.effects.shadows=w.effects.gi=true;
    }
    static bool composed(const WorldRenderer& w){return w.composited&&!w.shadowsComposited&&!w.shadowFrameReady;}
    template<class W> static void scratch(const W& w,bool expected){
        if constexpr(HasComposite<W>::value)
            assert(bool(w.composite.scratchTexture())==expected&&bool(w.composite.scratchSurface())==expected);
        else assert(bool(w.color)==expected&&bool(w.colorSurface)==expected);
    }
    template<class W> static void discardScratch(W& w){
        w.stateBlocks.clear();
        if constexpr(HasComposite<W>::value)w.composite.releaseScratch();
        else{drop(w.colorSurface);drop(w.color);}
    }
    static void direct(WorldRenderer& w,bool on){w.sourceColors[0]=on?WorldRenderer::V(.6f,.5f,.4f):WorldRenderer::V();w.sourceColors[1]={};}
    static void resetResources(WorldRenderer& w){
        w.releaseGPU(true);
        assertScreenReleased(w);
        assert(w.publishedFogRegion&&!fogField(w)&&!w.active);
    }
    static void diagnosticFrames(WorldRenderer& w,unsigned width,unsigned height){
        const auto directory=NorthlightWorldDiagnostics::directory(w.root);assert(!directory.empty());
        auto pixels=[&](const std::string& folder,const char* name,unsigned capture,bool zero){
            const auto bytes=captureFileBytes(folder+"/capture-"+std::to_string(capture)+"-"+name+".fgr");
            uint32_t header[5];assert(bytes.size()>=sizeof header);std::memcpy(header,bytes.data(),sizeof header);
            assert(header[0]==0x31524746&&header[1]&&header[2]&&bytes.size()==sizeof header+size_t(header[1])*header[2]*header[4]);
            const bool empty=std::all_of(bytes.begin()+sizeof header,bytes.end(),[](auto byte){return byte==0;});assert(empty==zero);
        };
        w.captureProducts(directory,1,900,width,height,true,true);pixels(directory,"fog",1,false);
        const auto before=fogImage(w);
        // Populated targets are deliberately retained, but the next frame has
        // no producer. Export zeros without any device access or target clear.
        assert(NorthlightWorldDiagnostics::product(nullptr,fogTargets(w).raw.surface,directory,"ao-raw",2,false,width/2,height/2,D3DFMT_A16B16G16R16F,901));
        assert(NorthlightWorldDiagnostics::textureProduct(nullptr,nullptr,directory,"materials-normal-height",2,false,width,height,D3DFMT_A16B16G16R16F,901));
        pixels(directory,"ao-raw",2,true);pixels(directory,"materials-normal-height",2,true);assert(before==fogImage(w));
        w.captureProducts(directory,2,901,width,height,false);pixels(directory,"fog",2,true);pixels(directory,"normals",2,true);
        w.composited=false;assert(w.requestNormalGpuDiagnosticCapture());assert(w.captureSkippedDiagnostic(902,width,height));
        assert(w.diagnosticCaptureInLastRender()&&!w.gpuNormalDiagnosticRequested);pixels(w.gpuDiagnosticDirectory,"fog",1,true);pixels(w.gpuDiagnosticDirectory,"scene",1,true);
        for(unsigned capture=2;capture<=6;++capture){
            w.gpuDiagnosticFrame=false;
            assert(w.requestNormalGpuDiagnosticCapture()&&!w.requestNormalGpuDiagnosticCapture());
            assert(w.captureSkippedDiagnostic(901+capture,width,height));
            assert(w.gpuDiagnosticCaptures==capture&&!w.gpuNormalDiagnosticRequested);
            pixels(w.gpuDiagnosticDirectory,"scene",capture,true);
        }
        NorthlightDiagnosticCapture::request(NorthlightDiagnosticCapture::AO);NorthlightDiagnosticCapture::request(NorthlightDiagnosticCapture::Materials);
        assert(NorthlightDiagnosticCapture::take()==(NorthlightDiagnosticCapture::AO|NorthlightDiagnosticCapture::Materials)&&!NorthlightDiagnosticCapture::take());
        std::puts("DIAGNOSTIC_FRAME_PASS staleTargetsRetained=1 skippedImagesZero=1 noDeviceAccessForEmpty=1 worldSkippedCapture=1 scopeQueue=1");
    }
    static void emptyDebugViews(WorldRenderer& world,IDirect3DSurface9* out,IDirect3DSurface9* resolved,IDirect3DSurface9* read,IDirect3DTexture9* depth,unsigned width,unsigned height){
        for(unsigned debug:{1u,2u,3u,1u,2u}){
            world.effects.shadows=world.effects.gi=debug<3?false:true;world.effects.fog=false;
            if(debug<3&&resolved==nullptr)world.effects.shadows=world.effects.gi=true; // also exercise missing geometry with enabled components
            assert(SUCCEEDED(world.d->ColorFill(out,nullptr,0xffffffff)));
            assert(SUCCEEDED(world.d->BeginScene()));const bool rendered=world.render(out,depth,width,height,D3DFMT_A16B16G16R16F,1,1000,0,1,debug,nullptr,nullptr);assert(SUCCEEDED(world.d->EndScene()));assert(rendered);
            if(resolved)assert(SUCCEEDED(world.d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE)));
            assert(SUCCEEDED(world.d->GetRenderTargetData(resolved?resolved:out,read)));D3DLOCKED_RECT lock{};assert(SUCCEEDED(read->LockRect(&lock,nullptr,D3DLOCK_READONLY)));
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width*8;++x)assert(static_cast<const unsigned char*>(lock.pBits)[y*lock.Pitch+x]==0);
            assert(SUCCEEDED(read->UnlockRect()));
        }
        std::puts("DIAGNOSTIC_VIEW_PASS disabledComponentsAndMissingGeometryBlack=1 noStaleImage=1");
    }
    static void localFogMotionFrame(WorldRenderer& w,unsigned frame,bool lamps){
        fixtureContext(w);w.effects={false,false,true};
        w.quality.localLights=1;w.quality.localLightLimit=8;w.quality.pointShadows=w.quality.equipmentLights=0;
        w.quality.weather=w.quality.autoBump=w.quality.contactAO=0;w.localLightQpc=0;
        w.context.camera[0]=w.context.inverseView[12]=float(frame)*.125f;
        w.context.view[12]=-w.context.camera[0];
        if(!w.active){w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map=w.lastRequest.map;}
        auto& lights=w.active->localLights;lights.resize(lamps&&frame%16<14?5:0);
        for(unsigned i=0;i<lights.size();++i){auto& l=lights[i];l.sourceId=i+1;l.kind=2;
            l.position[0]=float(int(i)-2)*2+std::sin(float(frame)*.2f);l.position[1]=0;l.position[2]=12+float(i);
            l.diffuse[0]=1;l.diffuse[1]=.3f;l.diffuse[2]=.1f;l.attenuationStart=1;l.attenuationEnd=9;}
        if(frame==24)drop(fogTemporalShader(w)); // Optional temporal-shader fallback.
    }
    static std::vector<unsigned char> fogImage(WorldRenderer& w,bool history=false,bool final=false,bool geometry=false){
        assert(fogField(w)==w.publishedFogRegion->field&&w.fogGeometryReady==geometry);
        auto* surface=history?fogTargets(w).history.surface:final&&w.diagnosticFogResolved?fogTargets(w).scratch.surface:fogTargets(w).raw.surface;
        D3DSURFACE_DESC desc{};assert(SUCCEEDED(surface->GetDesc(&desc)));
        IDirect3DSurface9* read=nullptr;assert(SUCCEEDED(w.d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&read,nullptr)));
        assert(SUCCEEDED(w.d->GetRenderTargetData(surface,read)));D3DLOCKED_RECT lock{};assert(SUCCEEDED(read->LockRect(&lock,nullptr,D3DLOCK_READONLY)));
        std::vector<unsigned char> image(size_t(desc.Width)*desc.Height*8);
        for(unsigned y=0;y<desc.Height;++y)std::memcpy(image.data()+size_t(y)*desc.Width*8,static_cast<const char*>(lock.pBits)+y*lock.Pitch,desc.Width*8);
        assert(SUCCEEDED(read->UnlockRect()));read->Release();return image;
    }
};
struct WorldScratchMeter:ForwardIDirect3DDevice9 {
#ifdef NORTHLIGHT_SHARED_SHADOW_TEST
    IDirect3DPixelShader9 *sharedShadowShader=nullptr,*sharedLightingShader=nullptr,*actorFilterShader=nullptr,*actorUniformShader=nullptr;
    bool sharedShadowBound=false,sharedLightingBound=false,actorFilterBound=false,refuseSharedShadow=false;
    unsigned sharedShadowDraws=0,sharedLightingDraws=0,actorFilterDraws=0,failSharedShadowAt=0,sharedShadowAttempts=0;
#endif
#ifdef NORTHLIGHT_GI_MERGE_TEST
    IDirect3DPixelShader9* giShader=nullptr;bool refuseGI=false,giBound=false;unsigned giCreates=0,giDraws=0;
#endif
    unsigned shaderSlotLimit=0,combinedDraws=0;bool refuseCombined=false,combinedBound=false,refuseVolume=false,failCombinedDraw=false;
    const DWORD* refusedFogShader=nullptr;
    IDirect3DPixelShader9* volumePrefix=nullptr;bool volumePrefixBound=false,failVolumeDraw=false;
    IDirect3DPixelShader9* combinedShader=nullptr;
    IDirect3DPixelShader9 *fogMarch=nullptr,*fogAmbient=nullptr,*fogTemporal=nullptr,*fogBlur=nullptr;
    bool fogBound=false;unsigned fogDraws=0,clears=0;
    IDirect3DPixelShader9 *cloudShader=nullptr,*cloudAmbient=nullptr;
    bool cloudBound=false;unsigned cloudDraws=0;
    unsigned width,height,creates=0,sceneCreates=0,failSceneAt=0,screenCreates=0,failScreenAt=0,shaderCreates=0,failShaderAt=0,fogCreates=0,failFogAt=0;float fogWeight=-1;
    unsigned indexCreates=0,failIndexAt=0;
    bool recordFog=false,recordFrameSetup=false;
    std::uint64_t fogCommands=0;
    unsigned fogCommandCount=0;
    std::unordered_map<const void*,unsigned> fogResources;
    void beginFogTrace(){recordFog=true;fogCommands=14695981039346656037ull;fogCommandCount=0;fogResources.clear();}
    unsigned fogResource(const void* p){return p?fogResources.try_emplace(p,unsigned(fogResources.size()+1)).first->second:0;}
    void fogCommand(unsigned op,unsigned a=0,unsigned b=0,unsigned c=0){
        if(!recordFog)return;
        for(unsigned word:{op,a,b,c})fogCommands=(fogCommands^word)*1099511628211ull;
        ++fogCommandCount;
    }
    WorldScratchMeter(IDirect3DDevice9* d,unsigned w,unsigned h):ForwardIDirect3DDevice9(d),width(w),height(h){}
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps)override{const auto hr=real->GetDeviceCaps(caps);if(SUCCEEDED(hr)&&shaderSlotLimit)caps->MaxPixelShader30InstructionSlots=shaderSlotLimit;return hr;}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader)override{
        if(recordFog)fogCommand(1,fogResource(shader));
        cloudBound=shader&&(shader==cloudShader||shader==cloudAmbient);
#ifdef NORTHLIGHT_SHARED_SHADOW_TEST
        sharedShadowBound=shader&&shader==sharedShadowShader;sharedLightingBound=shader&&shader==sharedLightingShader;
        actorFilterBound=shader&&(shader==actorFilterShader||shader==actorUniformShader);
#endif
        volumePrefixBound=shader&&shader==volumePrefix;
#ifdef NORTHLIGHT_GI_MERGE_TEST
        giBound=shader&&shader==giShader;
#endif
        combinedBound=shader&&shader==combinedShader;fogBound=shader&&(shader==fogMarch||shader==fogAmbient||shader==fogTemporal||shader==fogBlur);return real->SetPixelShader(shader);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT n,const void* vertices,UINT stride)override{
        fogCommand(2,type,n,stride);
#ifdef NORTHLIGHT_SHARED_SHADOW_TEST
        if(sharedShadowBound&&++sharedShadowAttempts==failSharedShadowAt)return D3DERR_INVALIDCALL;
        sharedShadowDraws+=sharedShadowBound;sharedLightingDraws+=sharedLightingBound;actorFilterDraws+=actorFilterBound;
#endif
        if(failVolumeDraw&&volumePrefixBound){failVolumeDraw=false;return D3DERR_INVALIDCALL;}
        if(failCombinedDraw&&combinedBound){failCombinedDraw=false;return D3DERR_INVALIDCALL;}
        cloudDraws+=cloudBound;
#ifdef NORTHLIGHT_GI_MERGE_TEST
        giDraws+=giBound;
#endif
        combinedDraws+=combinedBound;fogDraws+=fogBound;return real->DrawPrimitiveUP(type,n,vertices,stride);}
    HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* rectangles,DWORD flags,D3DCOLOR color,float z,DWORD stencil)override{++clears;return real->Clear(n,rectangles,flags,color,z,stencil);}
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DIndexBuffer9** out,HANDLE* shared)override{
        if(++indexCreates==failIndexAt){if(out)*out=nullptr;return E_OUTOFMEMORY;}return real->CreateIndexBuffer(length,usage,format,pool,out,shared);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** t,HANDLE* shared)override{
        if(recordFrameSetup)fogCommand(8,w,h,format);
        if(w==width&&h==height&&(usage&D3DUSAGE_RENDERTARGET)&&format==D3DFMT_A16B16G16R16F)++creates;
        if(w==width/2&&h==height/2&&(usage&D3DUSAGE_RENDERTARGET)&&format==D3DFMT_A16B16G16R16F&&++screenCreates==failScreenAt)return E_OUTOFMEMORY;
        if(w==NorthlightRegionalFog::N&&h==NorthlightRegionalFog::N&&format==D3DFMT_A32B32G32R32F&&++fogCreates==failFogAt)return E_OUTOFMEMORY;
        if(w==1024&&h==1024&&format==D3DFMT_R32F&&(usage&D3DUSAGE_RENDERTARGET)&&++sceneCreates==failSceneAt)return E_OUTOFMEMORY;
        return real->CreateTexture(w,h,levels,usage,format,pool,t,shared);
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** shader)override{
#ifdef NORTHLIGHT_SHARED_SHADOW_TEST
        if(code==kUnifiedDirectionalShadowShader&&refuseSharedShadow){*shader=nullptr;return D3DERR_NOTAVAILABLE;}
#endif
#ifdef NORTHLIGHT_FOG_VOLUME_TEST
        if(refuseVolume&&(code==kVolumeInjectQuietShader||code==kVolumeInjectAmbientQuietShader)){if(shader)*shader=nullptr;return D3DERR_NOTAVAILABLE;}
#endif
#ifdef NORTHLIGHT_GI_MERGE_TEST
        if(code==kWorldLightingUnshadowedGIShader){++giCreates;if(refuseGI){*shader=nullptr;return D3DERR_NOTAVAILABLE;}}
#endif
        if(++shaderCreates==failShaderAt)return E_OUTOFMEMORY;
        if(code==refusedFogShader){*shader=nullptr;return D3DERR_NOTAVAILABLE;}
        if(code==kWorldMaterialCompositeHdrShader&&refuseCombined){*shader=nullptr;return D3DERR_NOTAVAILABLE;}
        const auto hr=real->CreatePixelShader(code,shader);if(SUCCEEDED(hr)){
#ifdef NORTHLIGHT_SHARED_SHADOW_TEST
            if(code==kUnifiedDirectionalShadowShader)sharedShadowShader=*shader;
            if(code==kWorldLightingSharedShader)sharedLightingShader=*shader;
            if(code==kActorShadowShader)actorFilterShader=*shader;
            if(code==kActorShadowUniformShader)actorUniformShader=*shader;
#endif
#ifdef NORTHLIGHT_FOG_VOLUME_TEST
            if(code==kVolumePrefixShader)volumePrefix=*shader;
#endif
#ifdef NORTHLIGHT_GI_MERGE_TEST
            if(code==kWorldLightingUnshadowedGIShader)giShader=*shader;
#endif
            if(code==kWorldMaterialCompositeHdrShader)combinedShader=*shader;
            if(code==kWorldFogShader)fogMarch=*shader;if(code==kWorldFogAmbientShader)fogAmbient=*shader;
            if(code==kFogTemporalShader)fogTemporal=*shader;if(code==kFogBlurShader)fogBlur=*shader;
            if(code==kFogCloudsShader)cloudShader=*shader;if(code==kFogCloudsAmbientShader)cloudAmbient=*shader;
        }return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT start,const float* values,UINT count)override{
        fogCommand(3,start,count);
        if(start==64&&count==1)fogWeight=values[1];return real->SetPixelShaderConstantF(start,values,count);
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture9* texture)override{
        if(recordFog)fogCommand(4,stage,fogResource(texture));return real->SetTexture(stage,texture);
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD stage,D3DSAMPLERSTATETYPE type,DWORD value)override{
        fogCommand(5,stage,type,value);return real->SetSamplerState(stage,type,value);
    }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE type,DWORD value)override{
        fogCommand(6,type,value);return real->SetRenderState(type,value);
    }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD index,IDirect3DSurface9* target)override{
        if(recordFog)fogCommand(7,index,fogResource(target));return real->SetRenderTarget(index,target);
    }
};
static void screenOk(HRESULT h){if(FAILED(h)){std::fprintf(stderr,"screen HRESULT=%08lx\n",(unsigned long)h);std::abort();}}
#ifndef NORTHLIGHT_LEGACY_FOREVER_FOG_FIXTURE
template<size_t N>
static void assertFogShader(IDirect3DPixelShader9* shader,const DWORD (&code)[N]){
    assert(shader);
    UINT bytes=0;screenOk(shader->GetFunction(nullptr,&bytes));
    assert(bytes==sizeof code);
    std::vector<DWORD> actual(N);screenOk(shader->GetFunction(actual.data(),&bytes));
    assert(!std::memcmp(actual.data(),code,sizeof code));
}
static void verifyForeverFogShaders(IDirect3DDevice9* d,unsigned width,unsigned height){
    NorthlightWorldForeverFogShaders shaders;
    WorldScratchMeter meter(d,width,height);
    meter.refusedFogShader=kForeverFogShader;
    assert(!shaders.createBase(&meter)&&!shaders.available());
    shaders.createRaymarchVariants(&meter);assert(meter.shaderCreates==1);

    meter.refusedFogShader=nullptr;meter.shaderSlotLimit=1;
    assert(shaders.createBase(&meter));
    shaders.createRaymarchVariants(&meter);assert(meter.shaderCreates==3);
    auto selected=shaders.select(true,true,true,true);
    assert(!selected.actor&&!selected.quiet&&!selected.tiled);
    assertFogShader(selected.shader,kForeverFogShader);
    selected=shaders.select(true,true,false,false);
    assert(!selected.actor&&selected.quiet&&!selected.tiled);
    assertFogShader(selected.shader,kForeverFogNoNoiseShader);
    shaders.createRaymarchVariants(&meter);assert(meter.shaderCreates==3);

    shaders.reset();assert(!shaders.available()&&!shaders.select(true,true,true,false).shader);
    meter.shaderSlotLimit=1024;meter.refusedFogShader=kForeverFogActorTiledNoNoiseShader;
    assert(shaders.createBase(&meter));
    shaders.createRaymarchVariants(&meter);
    selected=shaders.select(true,true,false,false);
    assert(selected.actor&&selected.quiet&&!selected.tiled);
    assertFogShader(selected.shader,kForeverFogActorNoNoiseShader);
    selected=shaders.select(true,true,false,true);
    assert(selected.actor&&!selected.quiet&&selected.tiled);
    assertFogShader(selected.shader,kForeverFogActorTiledShader);
    selected=shaders.select(true,true,true,false);
    assert(selected.actor&&selected.quiet&&!selected.tiled);
    assertFogShader(selected.shader,kForeverFogActorNoNoiseTransitionShader);

    shaders.reset();meter.refusedFogShader=nullptr;
    assert(shaders.createBase(&meter));
    shaders.createRaymarchVariants(&meter);
    selected=shaders.select(true,true,false,false);
    assert(selected.actor&&selected.quiet&&selected.tiled);
    assertFogShader(selected.shader,kForeverFogActorTiledNoNoiseShader);
    std::puts("WORLD_FOREVER_FOG_SHADERS_PASS bytecode=exact lowCaps=base optionalRefusal=fallback reset=recreated");
}
#endif
static bool renderChecked(WorldRenderer& world,IDirect3DDevice9* device,IDirect3DSurface9* out,IDirect3DTexture9* depth,UINT w,UINT h,D3DFORMAT format,float nearZ,float farZ,float minZ,float maxZ,int debug,NorthlightGpuProfile* profile,IDirect3DTexture9* water,IDirect3DTexture9* scene=nullptr,IDirect3DTexture9* ao=nullptr,unsigned frame=0,IDirect3DTexture9* hdrBase=nullptr,bool hdrCombineAllowed=true,bool hdrSkyBase=false,const SavedState* enclosingState=nullptr){
    screenOk(device->BeginScene());
#ifdef NORTHLIGHT_WORLD_STATE_SCOPE_TEST
    const bool result=world.render(out,depth,w,h,format,nearZ,farZ,minZ,maxZ,debug,profile,water,scene,ao,frame,hdrBase,false,hdrCombineAllowed,hdrSkyBase,false,enclosingState);
#else
    const bool result=world.render(out,depth,w,h,format,nearZ,farZ,minZ,maxZ,debug,profile,water,scene,ao,frame,hdrBase,false,hdrCombineAllowed,hdrSkyBase);
#endif
    screenOk(device->EndScene());
    if(!result)std::fprintf(stderr,"WORLD_SCREEN_RENDER_FAILED reason=%s screenReady=%u geometryReady=%u debug=%d frame=%u\n",world.lastSkipReason(),unsigned(world.screenReady()),unsigned(world.ready()),debug,frame);
    return result;
}
static float screenHalf(unsigned short h){unsigned e=h>>10&31,m=h&1023;float v=e==0?std::ldexp(float(m),-24):e==31?(m?NAN:INFINITY):std::ldexp(float(1024+m),int(e)-25);return h&32768?-v:v;}
static IDirect3DTexture9* screenTexture(IDirect3DDevice9* d,unsigned w,unsigned h,const float* value,unsigned channels=4){
    IDirect3DTexture9* t=nullptr;screenOk(d->CreateTexture(w,h,1,0,channels==4?D3DFMT_A32B32G32R32F:D3DFMT_R32F,D3DPOOL_MANAGED,&t,nullptr));D3DLOCKED_RECT l{};screenOk(t->LockRect(0,&l,nullptr,0));
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)std::memcpy(static_cast<char*>(l.pBits)+y*l.Pitch+x*channels*4,value,channels*4);screenOk(t->UnlockRect(0));return t;
}
#ifdef NORTHLIGHT_INSPECTION_VIEW_GPU_TEST
struct PipelineOffTest {
    static void inspectionViews(IDirect3D9* api,D3DPRESENT_PARAMETERS pp,unsigned msaa){
        pp.BackBufferWidth=640;pp.BackBufferHeight=360;
        pp.MultiSampleType=D3DMULTISAMPLE_TYPE(msaa);
        IDirect3DDevice9* backend=nullptr;
        screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,pp.hDeviceWindow,
            D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&backend));
        NorthlightHdr::Presentation presentation;presentation.logical=pp;presentation.settings.requested=false;
        backend->AddRef();
        auto* proxy=new Device(backend,api,presentation);
        auto& renderer=*proxy;
        const UINT width=pp.BackBufferWidth,height=pp.BackBufferHeight;
        const auto format=pp.BackBufferFormat;
        IDirect3DSurface9 *back=nullptr,*resolved=nullptr,*read=nullptr;
        screenOk(backend->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));
        if(pp.MultiSampleType!=D3DMULTISAMPLE_NONE)
            screenOk(backend->CreateRenderTarget(width,height,format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
        screenOk(backend->CreateOffscreenPlainSurface(width,height,format,D3DPOOL_SYSTEMMEM,&read,nullptr));
        const float flat=.8f;
        auto* depth=screenTexture(backend,width,height,&flat,1);
        D3DLOCKED_RECT lock{};
        screenOk(depth->LockRect(0,&lock,nullptr,0));
        for(UINT y=0;y<height;++y){
            auto* row=reinterpret_cast<float*>(static_cast<char*>(lock.pBits)+y*lock.Pitch);
            for(UINT x=width/2;x<width;++x)row[x]=y<height/2?.78f:1.f;
        }
        screenOk(depth->UnlockRect(0));
        for(const int mode:{Device::NormalsView,Device::SsaoView}){
            renderer.clearFrame();
            WorldScreenEffectsTest::fixtureContext(*renderer.world);
            renderer.world->setPipelineEnabled(true);
            renderer.enabled=renderer.projectionValid=true;
            renderer.worldDebug=mode;
            renderer.nearZ=renderer.scaleX=renderer.scaleY=1.f;
            renderer.farZ=1000.f;renderer.worldMinDepth=0.f;renderer.worldMaxDepth=1.f;
            renderer.contactAO=false;renderer.selectAO(3,2);
            screenOk(backend->SetRenderTarget(0,back));
            screenOk(backend->SetDepthStencilSurface(nullptr));
            assert(renderer.resources(width,height,format));
            depth->AddRef();drop(renderer.depthTex);renderer.depthTex=depth;
            renderer.captured=true;
            screenOk(backend->ColorFill(back,nullptr,0xff204060));
            screenOk(backend->BeginScene());
            renderer.renderEffects();renderer.spellReplay();
            screenOk(backend->EndScene());
            assert(renderer.applied&&renderer.diagnosticWorld&&!renderer.failed);
            if(resolved)screenOk(backend->StretchRect(back,nullptr,resolved,nullptr,D3DTEXF_NONE));
            screenOk(backend->GetRenderTargetData(resolved?resolved:back,read));
            screenOk(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            const auto pixel=[&](UINT x,UINT y){
                return static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch+x*4;
            };
            const auto* surface=pixel(width/4,height/4);
            const auto* sky=pixel(width*3/4,height*3/4);
            if(mode==Device::NormalsView){
                assert(surface[2]>=127&&surface[2]<=128&&surface[1]>=127&&surface[1]<=128&&surface[0]<=1);
                assert(!sky[0]&&!sky[1]&&!sky[2]);
            }else{
                assert(renderer.diagnosticAoRaw&&renderer.diagnosticAo&&!renderer.contactAO&&renderer.aoMethod==3);
                assert(surface[0]>=250&&sky[0]==255&&sky[1]==255&&sky[2]==255);
                bool darkContact=false;
                for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){
                    const auto* color=pixel(x,y);
                    assert(color[0]==color[1]&&color[1]==color[2]);
                    darkContact|=color[0]<245;
                }
                assert(darkContact);
            }
            screenOk(read->UnlockRect());
        }
        proxy->Release();depth->Release();read->Release();drop(resolved);back->Release();backend->Release();
        std::puts("WORLD_INSPECTION_GPU_PASS normals=1 ssao=1 separate=1 configuredAoOff=1");
    }
};
#endif
#ifdef NORTHLIGHT_WORLD_SCREEN_INPUT_TEST
static void screenInputCommands(IDirect3DDevice9* d,IDirect3DSurface9* color){
    enum Command {Depth,Texture,Copy,Vertex,Fvf,Frequency,State,Stage,Rows,Sampler,Target,Viewport,DiagnosticClear};
    using Call=std::array<std::uintptr_t,4>;
    const auto pointer=[](auto* p){return reinterpret_cast<std::uintptr_t>(p);};
    struct Recorder {
        std::vector<Call> calls;
        bool failCopy=false;
        size_t failAt=0;
        HRESULT record(Call call){calls.push_back(call);return calls.size()==failAt?E_FAIL:D3D_OK;}
        HRESULT SetDepthStencilSurface(IDirect3DSurface9* p){assert(!p);return record({Depth});}
        HRESULT SetTexture(DWORD slot,IDirect3DBaseTexture9* p){return record({Texture,slot,reinterpret_cast<std::uintptr_t>(p)});}
        HRESULT StretchRect(IDirect3DSurface9* src,const RECT* sr,IDirect3DSurface9* dst,const RECT* dr,D3DTEXTUREFILTERTYPE filter){
            assert(!sr&&!dr&&filter==D3DTEXF_NONE);
            record({Copy,reinterpret_cast<std::uintptr_t>(src),reinterpret_cast<std::uintptr_t>(dst)});
            return failCopy?D3DERR_INVALIDCALL:D3D_OK;
        }
        HRESULT SetVertexShader(IDirect3DVertexShader9* p){assert(!p);return record({Vertex});}
        HRESULT SetFVF(DWORD fvf){return record({Fvf,fvf});}
        HRESULT SetStreamSourceFreq(UINT stream,UINT rate){return record({Frequency,stream,rate});}
        HRESULT SetRenderState(D3DRENDERSTATETYPE key,DWORD value){return record({State,key,value});}
        HRESULT SetTextureStageState(DWORD slot,D3DTEXTURESTAGESTATETYPE key,DWORD value){return record({Stage,slot,key,value});}
        HRESULT SetPixelShaderConstantF(UINT first,const float* data,UINT count){
            if(first==19){
                assert(count==1&&data[0]==0&&data[1]==0&&data[2]==0&&data[3]==12.5f);
                return record({Rows,first,count});
            }
            return record({Rows,first,count,reinterpret_cast<std::uintptr_t>(data)});
        }
        HRESULT SetSamplerState(DWORD slot,D3DSAMPLERSTATETYPE key,DWORD value){return record({Sampler,slot,key,value});}
        HRESULT SetRenderTarget(DWORD slot,IDirect3DSurface9* surface){
            return record({Target,slot,reinterpret_cast<std::uintptr_t>(surface)});
        }
        HRESULT SetViewport(const D3DVIEWPORT9* viewport){
            assert(!viewport->X&&!viewport->Y&&viewport->MinZ==0&&viewport->MaxZ==1);
            return record({Viewport,viewport->Width,viewport->Height});
        }
        HRESULT Clear(DWORD count,const D3DRECT* rectangles,DWORD flags,D3DCOLOR color,float depth,DWORD stencil){
            assert(!count&&!rectangles&&flags==D3DCLEAR_TARGET&&!color&&depth==1&&!stencil);
            return record({DiagnosticClear});
        }
    } device;
    NorthlightWorldComposite composite;
    D3DSURFACE_DESC desc{};screenOk(color->GetDesc(&desc));
    bool refuse=true;
#ifdef NORTHLIGHT_WORLD_RENDER_TARGET_OWNER_TEST
    const auto allocate=[&](HRESULT hr,const char*){return !refuse&&SUCCEEDED(hr);};
    const auto scratch=[&]{return composite.prepareScratch(d,desc.Width,desc.Height,desc.Format,true,false,allocate,[]{});};
#else
    const auto allocate=[&](UINT w,UINT h,D3DFORMAT format,IDirect3DTexture9** texture,IDirect3DSurface9** surface){
        return !refuse&&SUCCEEDED(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,texture,nullptr))&&
            SUCCEEDED((*texture)->GetSurfaceLevel(0,surface));
    };
    const auto scratch=[&]{return composite.prepareScratch(desc.Width,desc.Height,desc.Format,true,false,allocate,[]{});};
#endif
    assert(!scratch()&&!composite.scratchTexture()&&!composite.scratchSurface());
    refuse=false;assert(scratch());
    const std::vector<Call> folded={{Depth},{Texture,0},{Vertex},{Fvf,D3DFVF_XYZRHW|D3DFVF_TEX1},{Frequency,0,1},
        {State,D3DRS_ZENABLE,FALSE},{State,D3DRS_ZWRITEENABLE,FALSE},{Stage,0,D3DTSS_TEXCOORDINDEX,0},
        {Stage,0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE},{State,D3DRS_WRAP0,0}};
    unsigned copies=0;
    const auto check=[&](HRESULT hr,const char* label){assert(!std::strcmp(label,"world color copy"));++copies;return SUCCEEDED(hr);};
    assert(composite.prepareScreenInput(&device,color,true,check)&&device.calls==folded&&!copies);
    for(unsigned round=0;round<2;++round){
        auto legacy=folded;
        legacy.insert(legacy.begin()+2,{Copy,pointer(color),pointer(composite.scratchSurface())});
        device.failCopy=true;device.calls.clear();
        assert(!composite.prepareScreenInput(&device,color,false,check)&&device.calls==std::vector<Call>(legacy.begin(),legacy.begin()+3));
        device.failCopy=false;device.calls.clear();
        assert(composite.prepareScreenInput(&device,color,false,check)&&device.calls==legacy);
        assert(composite.prepareScreenInput(d,color,false,check));
        composite.reset();
        assert(!composite.scratchTexture()&&!composite.scratchSurface());
        if(round==0)assert(scratch());
    }
    assert(copies==6);
    auto* zero=reinterpret_cast<IDirect3DTexture9*>(1);
    for(std::uintptr_t id:{2,3}){
        device.calls.clear();
        NorthlightWorldScreenLighting::bindDistanceOnly(&device,zero,reinterpret_cast<IDirect3DTexture9*>(id),12.5f);
        assert((device.calls==std::vector<Call>{{Texture,12,1},{Rows,19,1},{Texture,10,id},
            {Sampler,10,D3DSAMP_MINFILTER,D3DTEXF_LINEAR},{Sampler,10,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR}}));
    }
#ifdef NORTHLIGHT_WORLD_CELESTIAL_SOURCE_TEST
    float rows[7][16]={};
    const NorthlightWorldScreenLighting::Source source{
        reinterpret_cast<IDirect3DTexture9*>(2),reinterpret_cast<IDirect3DTexture9*>(3),
        rows[0],rows[1],rows[2],rows[3],rows[4],rows[5]
    };
    for(bool fog:{false,true}){
        device.calls.clear();
        NorthlightWorldScreenLighting::bindSource(&device,source,[&]() -> const float* {
            if(!fog)return nullptr;
            device.SetTexture(4,reinterpret_cast<IDirect3DTexture9*>(4));
            return rows[6];
        });
        std::vector<Call> expected={{Rows,2,1,pointer(rows[0])},{Texture,2,2},{Texture,3,3},
            {Rows,7,4,pointer(rows[1])},{Rows,11,4,pointer(rows[2])},
            {Rows,16,1,pointer(rows[3])},{Rows,17,1,pointer(rows[4])}};
        if(fog)expected.insert(expected.end(),{{Texture,4,4},{Rows,21,1,pointer(rows[6])}});
        expected.push_back({Rows,27,1,pointer(rows[5])});
        assert(device.calls==expected);
    }
    std::puts("WORLD_CELESTIAL_SOURCE_PASS sceneAndFog=sameMaps actorInputsBeforeWeights=1 constants=exact");
#endif
    std::puts("WORLD_SCREEN_INPUT_PASS folded=0copies copyFailure=stop scratchOom=retry scratchReset=recreated distanceBindings=exact");
#ifdef NORTHLIGHT_WORLD_SHARED_FRAME_TEST
    float frameRows[68][4]={};
    const float tuning[4]={.25f,.5f,.75f,1.f};
    IDirect3DTexture9* textures[14]={};
    for(UINT slot=0;slot<14;++slot)if(slot!=8&&slot!=9&&slot!=12)
        textures[slot]=reinterpret_cast<IDirect3DTexture9*>(std::uintptr_t(slot+1));
    std::vector<Call> bank={{Rows,0,68,pointer(frameRows[0])},{Rows,115,1,pointer(tuning)}};
    for(UINT slot=0;slot<14;++slot){
        const DWORD filter=slot==0||slot==9?D3DTEXF_LINEAR:D3DTEXF_POINT;
        bank.insert(bank.end(),{{Texture,slot,pointer(textures[slot])},
            {Sampler,slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{Sampler,slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},
            {Sampler,slot,D3DSAMP_MINFILTER,filter},{Sampler,slot,D3DSAMP_MAGFILTER,filter},
            {Sampler,slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE},{Sampler,slot,D3DSAMP_SRGBTEXTURE,FALSE}});
    }
    device.calls.clear();
    NorthlightWorldScreenLighting::bindFrameBank(&device,frameRows,tuning,textures);
    assert(device.calls==bank);
    const std::vector<Call> diagnostic={{Depth},{Target,1},{Target,2},{Target,3},{Target,0,pointer(color)},
        {Viewport,desc.Width,desc.Height},{State,D3DRS_SCISSORTESTENABLE,FALSE},{DiagnosticClear}};
    for(size_t stop=0;stop<=diagnostic.size();++stop){
        device.calls.clear();device.failAt=stop;
        assert(composite.clearDiagnostic(&device,color,desc.Width,desc.Height)==!stop);
        assert(device.calls==std::vector<Call>(diagnostic.begin(),diagnostic.begin()+(stop?stop:diagnostic.size())));
    }
    std::puts("WORLD_SHARED_FRAME_PASS bankBindings=exact diagnosticClear=exact failurePrefixes=exact");
#endif
#ifdef NORTHLIGHT_WORLD_FRAME_SETUP_TEST
    WorldScratchMeter meter(d,desc.Width,desc.Height),expected(d,desc.Width,desc.Height);
    NorthlightWorldScreenLighting lighting;
    NorthlightWorldScreenLighting::FrameInput frame{desc.Width,desc.Height,false,true,17.f,true,true};
    meter.recordFrameSetup=true;
    const auto verifyFrame=[&](bool allocate,bool ready){
        meter.beginFogTrace();expected.beginFogTrace();
        expected.fogCommand(6,D3DRS_SRCBLEND,D3DBLEND_ONE);
        expected.fogCommand(6,D3DRS_DESTBLEND,D3DBLEND_ONE);
        expected.fogCommand(6,D3DRS_BLENDOP,D3DBLENDOP_ADD);
        if(allocate)expected.fogCommand(8,desc.Width/2,desc.Height/2,D3DFMT_A16B16G16R16F);
        expected.fogCommand(3,78,1);
        assert(lighting.prepareFrame(&meter,frame,[]{return true;})==ready);
        assert(meter.fogCommands==expected.fogCommands&&meter.fogCommandCount==expected.fogCommandCount);
        const float receiver[]={1,17,1,1};
        float rows[4]={};
        screenOk(d->GetPixelShaderConstantF(78,rows,1));assert(!std::memcmp(receiver,rows,sizeof rows));
        assert(bool(lighting.prelit().surface)==ready);
    };
    verifyFrame(false,false);
    frame.prelit=true;
    meter.failScreenAt=1;
    verifyFrame(true,false);
    verifyFrame(false,false);
    lighting.releaseAuxiliaryTargets();
    verifyFrame(true,true);
    verifyFrame(false,true);
    lighting.releaseAuxiliaryTargets();
    assert(!lighting.prelit().surface&&!lighting.prelit().texture);
    std::puts("WORLD_FRAME_SETUP_PASS blendBeforeAllocation=1 optionalOom=1 retry=1 receiverConstants=exact");
#endif
}
#endif
#if !defined(NORTHLIGHT_LEGACY_LAMP_OWNER_FIXTURE) && !defined(NORTHLIGHT_LEGACY_LOCAL_SURFACE_FIXTURE)
static void localSurfaceCommands(){
    enum Command {Texture,Sampler,State,Target,Clear,Constants,Direct,Flash,Point};
    using Call=std::array<std::uintptr_t,4>;
    struct Device {
        std::vector<Call> calls;
        int fault=-1;
        HRESULT record(Call call){calls.push_back(call);return int(call[0])==fault?E_FAIL:S_OK;}
        HRESULT SetTexture(DWORD slot,IDirect3DBaseTexture9* texture){
            return record({Texture,slot,reinterpret_cast<std::uintptr_t>(texture)});
        }
        HRESULT SetSamplerState(DWORD slot,D3DSAMPLERSTATETYPE state,DWORD value){return record({Sampler,slot,state,value});}
        HRESULT SetRenderState(D3DRENDERSTATETYPE state,DWORD value){return record({State,state,value});}
        HRESULT SetRenderTarget(DWORD slot,IDirect3DSurface9* target){
            return record({Target,slot,reinterpret_cast<std::uintptr_t>(target)});
        }
        HRESULT Clear(DWORD count,const D3DRECT* rect,DWORD flags,DWORD color,float z,DWORD stencil){
            assert(!count&&!rect&&flags==D3DCLEAR_TARGET&&!color&&z==1&&!stencil);
            return record({Command::Clear});
        }
        HRESULT SetPixelShaderConstantF(UINT first,const float* rows,UINT count){
            assert(rows[0]==float(first));
            return record({Constants,first,count});
        }
    } device;
    float constants[68][4]={};constants[7][0]=7.f;
    const auto texture=[](unsigned id){return reinterpret_cast<IDirect3DTexture9*>(static_cast<std::uintptr_t>(id));};
    IDirect3DTexture9* frame[6]={texture(1),texture(2),texture(3),texture(4),texture(5),texture(6)};
    const auto target=reinterpret_cast<IDirect3DSurface9*>(17);
    NorthlightWorldLocalLighting::SurfaceInput input{target,texture(9),texture(14),frame,constants,true,true,true,false};
    NorthlightWorldLocalLighting pass;
    const NorthlightLocalLightSelection::Selection lights{};
    const char* failure=nullptr;
    const auto draw=[&]{
        return pass.drawSurface(&device,input,lights,nullptr,nullptr,nullptr,nullptr,
            [&](HRESULT hr,const char* operation){if(FAILED(hr))failure=operation;return SUCCEEDED(hr);},
            [&]{return SUCCEEDED(device.record({Direct}));},
            [&]{device.record({Flash});},[&]{device.record({Point});});
    };
    std::vector<Call> expected{{Texture,8},{State,D3DRS_SCISSORTESTENABLE,FALSE},{Target,0,17},{Command::Clear},
        {State,D3DRS_SRCBLEND,D3DBLEND_ONE},{State,D3DRS_DESTBLEND,D3DBLEND_ONE},{State,D3DRS_BLENDOP,D3DBLENDOP_ADD},
        {State,D3DRS_ALPHABLENDENABLE,TRUE},{State,D3DRS_COLORWRITEENABLE,7},{Constants,7,21},
        {Sampler,9,D3DSAMP_MINFILTER,D3DTEXF_POINT},{Sampler,9,D3DSAMP_MAGFILTER,D3DTEXF_POINT},
        {Texture,9,9},{Texture,14,14},{Sampler,14,D3DSAMP_MINFILTER,D3DTEXF_POINT},
        {Sampler,14,D3DSAMP_MAGFILTER,D3DTEXF_POINT},{Direct},{State,D3DRS_COLORWRITEENABLE,15},{Flash},{Point},{Constants,0,53}};
    for(unsigned sampler=0;sampler<6;++sampler){
        const auto filter=sampler==0?D3DTEXF_LINEAR:D3DTEXF_POINT;
        expected.insert(expected.end(),{{Texture,sampler,sampler+1},{Sampler,sampler,D3DSAMP_MINFILTER,filter},
            {Sampler,sampler,D3DSAMP_MAGFILTER,filter}});
    }
    expected.insert(expected.end(),{{State,D3DRS_COLORWRITEENABLE,15},{State,D3DRS_ALPHABLENDENABLE,FALSE}});
    assert(draw()&&device.calls==expected&&!failure);
    for(const auto& fault:std::array<std::pair<Command,unsigned>,3>{{{Target,3},{Command::Clear,4},{Direct,17}}}){
        device.calls.clear();device.fault=fault.first;failure=nullptr;
        assert(!draw()&&device.calls==std::vector<Call>(expected.begin(),expected.begin()+fault.second));
        if(fault.first==Target)assert(failure&&std::strcmp(failure,"current local light target")==0);
        else if(fault.first==Command::Clear)assert(failure&&std::strcmp(failure,"current local light clear")==0);
    }
    device.calls.clear();device.fault=-1;
    input.accumulate=input.bindMaterial=input.positional=false;
    assert(draw()&&(device.calls==std::vector<Call>{{Texture,8},{Constants,7,21},
        {Sampler,9,D3DSAMP_MINFILTER,D3DTEXF_POINT},{Sampler,9,D3DSAMP_MAGFILTER,D3DTEXF_POINT},
        {Direct},{State,D3DRS_COLORWRITEENABLE,15},{State,D3DRS_ALPHABLENDENABLE,FALSE}}));
    std::puts("WORLD_LOCAL_SURFACE_PASS target clear and draw order, constants and samplers restored, target/clear/draw failures stop");
}
#endif
#ifdef NORTHLIGHT_WORLD_RENDER_TARGET_OWNER_TEST
#ifdef NORTHLIGHT_WORLD_MATERIAL_UPLOAD_TEST
static void materialUploadOwner(IDirect3DDevice9* d){
    struct Device:ForwardIDirect3DDevice9 {
        using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
        unsigned creates=0,failAt=0,updates=0;
        IUnknown* partial=nullptr;
        bool failUpload=false,failShader=false;
        HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,
                                                 IDirect3DTexture9** out,HANDLE* shared)override{
            ++creates;
            const HRESULT hr=real->CreateTexture(w,h,levels,usage,format,pool,out,shared);
            if(FAILED(hr)||creates!=failAt)return hr;
            partial=*out;partial->AddRef();return E_OUTOFMEMORY;
        }
        HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* source,IDirect3DBaseTexture9* target)override{
            ++updates;
            return failUpload?E_FAIL:real->UpdateTexture(source,target);
        }
        HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{
            const HRESULT hr=real->CreatePixelShader(code,out);
            if(FAILED(hr)||!failShader)return hr;
            partial=*out;partial->AddRef();return E_OUTOFMEMORY;
        }
        void released(){assert(partial&&partial->Release()==0);partial=nullptr;}
    } device(d);
    NorthlightWorldMeshPublication::MaterialUpload upload;
    NorthlightWorldMesh::MaterialUpload material;
    material.width=8;material.height=3;material.alphaOnly=true;
    for(unsigned i=0;i<24;++i)material.bgra.push_back(uint8_t(31+7*i));
    const auto pixels=[&](size_t offset,size_t n,void* target){
        assert(offset<=material.bgra.size()&&n<=material.bgra.size()-offset);
        std::memcpy(target,material.bgra.data()+offset,n);return true;
    };
    for(const auto format:{NorthlightShadowAlphaTexture::Format{D3DFMT_A8,1,true},
                          NorthlightShadowAlphaTexture::Format{D3DFMT_A8R8G8B8,4,true}}){
        screenOk(upload.begin(&device,material,format.value,false).hr);
        const size_t row=material.width*format.stride;
        assert(!upload.copy(material,format,row-1,pixels).bytes&&!upload.rowsWritten());
        assert(FAILED(upload.copy(material,format,row,[](size_t,size_t,void*){return false;}).hr)&&!upload.rowsWritten());
        assert(upload.copy(material,format,row,pixels).bytes==row&&upload.rowsWritten()==1);
        assert(upload.copy(material,format,2*row,pixels).bytes==2*row&&upload.rowsWritten()==3);
        screenOk(upload.finish(&device).hr);
        auto* texture=upload.take();
        D3DLOCKED_RECT read{};screenOk(texture->LockRect(0,&read,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<3;++y)for(unsigned x=0;x<8;++x){
            const auto* data=static_cast<const uint8_t*>(read.pBits)+y*read.Pitch;
            assert(data[x*format.stride+format.stride-1]==material.bgra[y*8+x]);
            if(format.stride==4)assert(data[x*4]==255&&data[x*4+1]==255&&data[x*4+2]==255);
        }
        screenOk(texture->UnlockRect(0));drop(texture);
    }
    const NorthlightShadowAlphaTexture::Format alpha{D3DFMT_A8,1,true};
    device.failAt=device.creates+2;
    const auto failed=upload.begin(&device,material,alpha.value,true);
    assert(FAILED(failed.hr)&&!std::strcmp(failed.stage,"material staging")&&!upload.texture());device.released();
    screenOk(upload.begin(&device,material,alpha.value,true).hr);
    assert(upload.copy(material,alpha,24,pixels).bytes==24);
    device.failUpload=true;
    assert(FAILED(upload.finish(&device).hr)&&device.updates==1);
    device.failUpload=false;
    screenOk(upload.finish(&device).hr);
    auto* texture=upload.take();assert(texture&&device.updates==2);drop(texture);
    IDirect3DPixelShader9* shader=nullptr;
    device.failShader=true;
    assert(FAILED(NorthlightScreen::createPixelShader(&device,kWorldNormalsShader,shader))&&!shader);device.released();
    device.failShader=false;
    screenOk(NorthlightScreen::createPixelShader(&device,kWorldNormalsShader,shader));assert(shader);drop(shader);
    std::puts("WORLD_MATERIAL_UPLOAD_GPU_PASS A8=exact BGRA=exact budget=rows copyRollback=1 partialOOM=1 defaultUpdateRetry=1 shaderRollback=1");
}
#endif

static void allocationOwners(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS& pp){
    struct Faults:ForwardIDirect3DDevice9 {
        using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
        bool pixelFailure=false,indexFailure=false;
        unsigned textureCalls=0,indexCalls=0;
        IUnknown* partial=nullptr;
        HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,
                                                 IDirect3DTexture9** out,HANDLE* shared) override{
            ++textureCalls;
            const auto hr=real->CreateTexture(w,h,levels,usage,format,pool,out,shared);
            if(SUCCEEDED(hr)&&pixelFailure){partial=*out;partial->AddRef();return E_OUTOFMEMORY;}
            return hr;
        }
        HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT size,DWORD usage,D3DFORMAT format,D3DPOOL pool,
                                                     IDirect3DIndexBuffer9** out,HANDLE* shared) override{
            ++indexCalls;
            const auto hr=real->CreateIndexBuffer(size,usage,format,pool,out,shared);
            if(SUCCEEDED(hr)&&indexFailure){partial=*out;partial->AddRef();return E_OUTOFMEMORY;}
            return hr;
        }
        void released(){assert(partial&&partial->Release()==0);partial=nullptr;}
    } faults(d);
    IDirect3DTexture9* texture=nullptr;IDirect3DSurface9* surface=nullptr;
    bool stopOnSurface=false;const char* rejected=nullptr;
    const auto check=[&](HRESULT hr,const char* name){
        if(FAILED(hr)||(stopOnSurface&&std::strcmp(name,"surface")==0)){rejected=name;return false;}
        return true;
    };
    const auto target=[&]{
        return NorthlightWorldRenderTarget::ensure(&faults,32,16,D3DFMT_A8R8G8B8,texture,surface,check,"texture","surface");
    };
    faults.pixelFailure=true;
    assert(!target()&&!texture&&!surface&&!std::strcmp(rejected,"texture"));faults.released();
    faults.pixelFailure=false;stopOnSurface=true;
    assert(!target()&&!texture&&!surface&&!std::strcmp(rejected,"surface"));
    stopOnSurface=false;
    assert(target()&&texture&&surface&&target()&&faults.textureCalls==3);
    D3DSURFACE_DESC desc{};screenOk(surface->GetDesc(&desc));assert(desc.Width==32&&desc.Height==16);

    NorthlightWorldMeshPool pages;
    NorthlightMeshPageKey::Key activeKey{},pendingKey{};
    activeKey.lo=1;activeKey.vertexBytes=96;activeKey.indexBytes=12;
    pendingKey=activeKey;pendingKey.lo=2;
    const auto pressureReplay=[] {return false;};
    assert(pages.store.beginStaging({activeKey}));pages.sync();
    const auto active=pages.store.staging().uploadBuffer[0];
    assert(pages.store.markWriting(active));
    assert(pages.acquire(&faults,active,true,96,D3DPOOL_MANAGED,pressureReplay));
    assert(pages.acquire(&faults,active,false,12,D3DPOOL_MANAGED,pressureReplay));
    pages.store.commit();
    auto* resident=pages.buffers[active].ib;
    assert(!pages.acquire(&faults,active,false,14,D3DPOOL_DEFAULT,pressureReplay)&&faults.indexCalls==1);
    assert(pages.store.beginStaging({pendingKey}));pages.sync();
    const auto pending=pages.store.staging().uploadBuffer[0];
    assert(pending!=active&&pages.store.markWriting(pending));
    assert(pages.acquire(&faults,pending,true,96,D3DPOOL_DEFAULT,pressureReplay));
    faults.indexFailure=true;
    assert(!pages.acquire(&faults,pending,false,12,D3DPOOL_DEFAULT,pressureReplay));faults.released();
    assert(!pages.buffers[pending].ib&&!pages.buffers[pending].ibFresh&&!pages.store.entries()[pending].ibCapacity);
    assert(pages.store.bufferOf(0)==active&&pages.buffers[active].ib==resident);
    faults.indexFailure=false;
    assert(pages.acquire(&faults,pending,false,12,D3DPOOL_DEFAULT,pressureReplay)&&pages.buffers[pending].ibFresh);
    pages.store.cancel();
    assert(pages.trim(UINT64_MAX)>=8192);
    drop(surface);drop(texture);
    screenOk(d->Reset(&pp));
    assert(pages.store.bufferOf(0)==active&&pages.buffers[active].ib==resident&&target());
    D3DINDEXBUFFER_DESC indexDesc{};screenOk(resident->GetDesc(&indexDesc));
    assert(indexDesc.Format==D3DFMT_INDEX16&&indexDesc.Pool==D3DPOOL_MANAGED);
    drop(surface);drop(texture);
    std::puts("WORLD_ALLOCATION_OWNERS_GPU_PASS partialTextureOOM=1 surfaceRollback=1 partialIndexOOM=1 activePagePreserved=1 retry=1 reset=1");
}
#endif

#ifdef NORTHLIGHT_SOLAR_MATERIAL_TEST
static void solarMaterialRoute(IDirect3DDevice9* d){
    struct ShaderFault:ForwardIDirect3DDevice9 {
        using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
        unsigned calls=0;
        HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD*,IDirect3DPixelShader9** out) override {
            ++calls;*out=nullptr;return D3DERR_NOTAVAILABLE;
        }
    } rejected(d);
    NorthlightWorldComposite composite;
    unsigned warnings=0;
    const auto warn=[&](const char*){++warnings;};
    const auto pixel=[&](const DWORD* code,IDirect3DPixelShader9*& shader,const char*){
        return SUCCEEDED(d->CreatePixelShader(code,&shader));
    };
    assert(composite.createMaterial(pixel));
    assert(!composite.prepareSolar(&rejected,true,warn));
    assert(!composite.prepareSolar(&rejected,true,warn)&&warnings==1&&rejected.calls==1);
    assert(!composite.selectMaterial(true,true,true,false).solar);
    composite.reset();
    assert(composite.createMaterial(pixel));
    for(bool hdr:{false,true}){
        assert(composite.prepareSolar(d,hdr,warn));
        const auto route=composite.selectMaterial(true,hdr,true,false,true);
        assert(route.full&&route.solar&&!route.combined);
    }
    composite.reset();
    std::puts("WORLD_SOLAR_MATERIAL_PASS sdr=1 hdr=1 fallback=1 retryAfterReset=1");
}
#endif

int main(int argc,char** argv){
    assert(argc==3);constexpr unsigned W=256,H=160;const unsigned msaa=std::getenv("NORTHLIGHT_TEST_MSAA")?unsigned(std::atoi(std::getenv("NORTHLIGHT_TEST_MSAA"))):0;assert(msaa==0||msaa==2);
    std::setvbuf(stdout,nullptr,_IONBF,0);logFile=stdout;
    std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightScreenEffectsOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
#ifdef NORTHLIGHT_SOLAR_MATERIAL_TEST
    solarMaterialRoute(d);
#endif
#ifdef NORTHLIGHT_WORLD_RENDER_TARGET_OWNER_TEST
#ifdef NORTHLIGHT_WORLD_MATERIAL_UPLOAD_TEST
    materialUploadOwner(d);
#endif
    allocationOwners(d,pp);
#endif
#ifdef NORTHLIGHT_REPLAY_PROBE_PASS_TEST
    WorldScreenEffectsTest::replayProbePass(d,pp,msaa);
#endif
#ifdef NORTHLIGHT_SCREEN_FALLBACKS_TEST
    WorldScreenEffectsTest::screenFallbackResources(d,pp);
#endif
#ifdef NORTHLIGHT_PROBE_ATLAS_OWNER_TEST
    WorldScreenEffectsTest::probeAtlasReset(d,pp);
#endif
#if defined(NORTHLIGHT_SHARED_SHADOW_TEST) && !defined(NORTHLIGHT_LEGACY_DIRECTIONAL_VISIBILITY_FIXTURE)
    {
        auto world=std::make_unique<WorldRenderer>(d);
        WorldScreenEffectsTest::visibilityAfterDeviceReset(*world,pp);
    }
#endif
#ifdef NORTHLIGHT_LAZY_REMOVAL_TEST
    {
        WorldScratchMeter meter(d,3840,2160);
        auto world=std::make_unique<WorldRenderer>(&meter);
        WorldScreenEffectsTest::removalDemand(*world,meter);
    }
#endif
#ifndef NORTHLIGHT_LEGACY_FOREVER_FOG_FIXTURE
    verifyForeverFogShaders(d,W,H);
#endif
#if !defined(NORTHLIGHT_LEGACY_LAMP_OWNER_FIXTURE) && !defined(NORTHLIGHT_LEGACY_LOCAL_SURFACE_FIXTURE)
    localSurfaceCommands();
#endif
    IDirect3DSurface9 *out=nullptr,*readback=nullptr,*resolved=nullptr;screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&out,nullptr));screenOk(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    if(msaa)screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
#ifdef NORTHLIGHT_WORLD_SCREEN_INPUT_TEST
    {
        SavedState state(d);
        assert(state.ok);
        screenInputCommands(d,out);
        assert(state.restore());
    }
#endif
#ifdef NORTHLIGHT_INSPECTION_VIEW_GPU_TEST
    PipelineOffTest::inspectionViews(api,pp,msaa);
#endif
    // Capture stores positive-hemisphere normal detail relative to its reference.
    const float rgb[]={.4f,.4f,.4f,1},ambient[]={0,0,0,1},normal[]={.7f,.6f,20,1},z[]={1000.f/999.f-1000.f/(999.f*20)};
    auto* scene=screenTexture(d,W,H,rgb);auto* ao=screenTexture(d,W/2,H/2,ambient);auto* detail=screenTexture(d,W,H,normal);auto* depth=screenTexture(d,W,H,z,1);
    {auto menu=std::make_unique<WorldRenderer>(d);WorldScreenEffectsTest::menuContext(*menu,d);}
#ifdef NORTHLIGHT_SHARED_SHADOW_TEST
    {
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
        screenOk(d->CreateVertexBuffer(3*sizeof(NorthlightGI::WorldVertex),0,0,D3DPOOL_MANAGED,&vb,nullptr));
        screenOk(d->CreateIndexBuffer(6,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr));
        // Compare a partially drawn shared resolve with a resolve refused at creation.
        std::vector<unsigned char> separateImage;
        constexpr size_t rowBytes=W*8; // RGBA16F
        for(unsigned mode=0;mode<8;++mode){
            WorldScratchMeter meter(d,W,H);
            meter.shaderSlotLimit=mode==1?512:0;
            meter.refuseSharedShadow=mode==2||mode==6;
            meter.failSharedShadowAt=mode==3?1:mode==7?2:0;
            auto world=std::make_unique<WorldRenderer>(&meter);WorldScreenEffectsTest::sharedSetup(*world,vb,ib,mode);
            const bool rendered=renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao);
            std::printf("WORLD_SHARED_SHADOW mode=%u rendered=%u shared=%u lighting=%u separate=%u attempts=%u\n",mode,unsigned(rendered),meter.sharedShadowDraws,meter.sharedLightingDraws,meter.actorFilterDraws,meter.sharedShadowAttempts);std::fflush(stdout);
            assert(rendered);
            const unsigned expected=mode==0?1:mode==5?2:0;
            assert(meter.sharedShadowDraws==(mode==7?1:expected)&&meter.sharedLightingDraws==expected);
            assert(meter.actorFilterDraws==(expected?0:mode>=6?2:1));
            if(mode==3)assert(meter.sharedShadowAttempts==1);
            if(mode>=6){
                assert(meter.sharedShadowAttempts==(mode==7?2:0));
                if(msaa)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));
                screenOk(d->GetRenderTargetData(msaa?resolved:out,readback));
                D3DLOCKED_RECT lock{};screenOk(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
                if(mode==6)separateImage.resize(rowBytes*H);
                for(unsigned y=0;y<H;++y){
                    const auto* row=static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch;
                    auto* expectedRow=separateImage.data()+y*rowBytes;
                    if(mode==6)std::memcpy(expectedRow,row,rowBytes);
                    else assert(std::memcmp(expectedRow,row,rowBytes)==0);
                }
                screenOk(readback->UnlockRect());
            }
            WorldScreenEffectsTest::removeFakeScene(*world);
        }
        vb->Release();ib->Release();
        std::puts("WORLD_SHARED_SHADOW_PASS onePass=1 twoSources=1 unsupported=legacy createFailure=legacy drawFailure=actorRestored hardShadows=legacy stateRestored=1 partialFailurePixels=exact");
    }
#endif
    std::vector<float> images[4];
    {
        WorldScratchMeter meter(d,W,H);
        auto world=std::make_unique<WorldRenderer>(&meter);
#ifdef NORTHLIGHT_SHARED_SHADOW_TEST
        WorldScreenEffectsTest::sharedResources(*world,meter);
#endif
        WorldScreenEffectsTest::inactiveGI(*world);
        WorldScreenEffectsTest::fixtureContext(*world);WorldScreenEffectsTest::localLightCPU(*world);
        meter.failShaderAt=3;assert(!WorldScreenEffectsTest::resources(*world,W,H));
        const auto shaderAttempts=meter.shaderCreates;assert(!WorldScreenEffectsTest::resources(*world,W,H)&&meter.shaderCreates==shaderAttempts);WorldScreenEffectsTest::screenRetry(*world);
        meter.failShaderAt=0;meter.failScreenAt=3;assert(!WorldScreenEffectsTest::resources(*world,W,H));
        const auto targetAttempts=meter.screenCreates;assert(!WorldScreenEffectsTest::resources(*world,W,H)&&meter.screenCreates==targetAttempts);WorldScreenEffectsTest::screenRetry(*world);
        const unsigned beforeRetry=meter.screenCreates;assert(WorldScreenEffectsTest::resources(*world,W,H));assert(meter.screenCreates>beforeRetry);
        std::puts("WORLD_SCREEN_RESOURCE_RETRY_PASS shaderOom=1 targetOom=1 partialReleased=1 recovered=1");
        WorldScreenEffectsTest::screenOnlyGeometry(*world,d,out,resolved,readback,depth,scene,ao,W,H);
        WorldScreenEffectsTest::sceneOwnership(*world,&meter);WorldScreenEffectsTest::reuseAtAddressPressure(*world);WorldScreenEffectsTest::pageIndexImage(*world,d);WorldScreenEffectsTest::coldGpuAtAddressPressure(*world,true,false,meter.indexCreates,meter.failIndexAt);WorldScreenEffectsTest::coldGpuAtAddressPressure(*world,false,false,meter.indexCreates,meter.failIndexAt);WorldScreenEffectsTest::coldGpuAtAddressPressure(*world,false,true,meter.indexCreates,meter.failIndexAt);meter.sceneCreates=0;WorldScreenEffectsTest::setup(*world,W,H);
        meter.fogCreates=0;meter.failFogAt=1;assert(!WorldScreenEffectsTest::uploadFog(*world));
        const auto fogAttempts=meter.fogCreates;assert(!WorldScreenEffectsTest::uploadFog(*world)&&meter.fogCreates==fogAttempts);WorldScreenEffectsTest::fogRetry(*world);
        meter.failFogAt=0;assert(WorldScreenEffectsTest::uploadFog(*world));WorldScreenEffectsTest::fogUploaded(*world);
        std::puts("WORLD_FOG_UPLOAD_RETRY_PASS allocationOom=1 throttled=1 recoveredFieldExact=1");
        WorldScreenEffectsTest::consumers(*world);WorldScreenEffectsTest::cloudPressure(*world);
        WorldScreenEffectsTest::noSceneTargets(*world);assert(WorldScreenEffectsTest::sceneBytes(*world)==0&&meter.sceneCreates==0);
        for(unsigned mode=0;mode<4;++mode){
            WorldScreenEffectsTest::fog(*world,mode>=2);world->setMaterialDetail(mode&1?detail:nullptr,1,48,nullptr,8);
            screenOk(d->SetRenderTarget(0,out));screenOk(d->SetDepthStencilSurface(nullptr));screenOk(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,mode));assert(WorldScreenEffectsTest::composed(*world));
            WorldScreenEffectsTest::scratch(*world,bool(mode&1));assert(meter.creates==(mode+1)/2);
            if(msaa)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));screenOk(d->GetRenderTargetData(msaa?resolved:out,readback));D3DLOCKED_RECT l{};screenOk(readback->LockRect(&l,nullptr,D3DLOCK_READONLY));
            auto& image=images[mode];image.resize(W*H*3);
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned k=0;k<3;++k){unsigned short h;std::memcpy(&h,static_cast<char*>(l.pBits)+y*l.Pitch+x*8+k*2,2);float value=screenHalf(h);assert(std::isfinite(value)&&value>=0);image[(y*W+x)*3+k]=value;}
            screenOk(readback->UnlockRect());
            IDirect3DSurface9* restored=nullptr;screenOk(d->GetRenderTarget(0,&restored));assert(restored==out);restored->Release();DWORD blend=TRUE;screenOk(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend));assert(!blend);
            if(mode==1){
                // A camera turn may hide all captures without disabling materials.
                world->setMaterialDetail(nullptr,1,48,nullptr,8,true);
                assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
                WorldScreenEffectsTest::scratch(*world,true);assert(meter.creates==1);
            }
        }

        WorldScreenEffectsTest::fog(*world,true);
        for(unsigned frame=0;frame<2;++frame){
            meter.fogWeight=-1;assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
            assert(meter.fogWeight==(frame?.85f:0.f));
        }
        std::puts("WORLD_FOG_HISTORY_PASS noGeometry=1 firstWeight=0 nextWeight=0.85");
        auto composedImage=[&](){
            if(msaa)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));screenOk(d->GetRenderTargetData(msaa?resolved:out,readback));
            D3DLOCKED_RECT lock{};screenOk(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> image(W*H*8);
            for(unsigned y=0;y<H;++y)std::memcpy(image.data()+y*W*8,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*8);screenOk(readback->UnlockRect());return image;
        };
        {
            const float wet[]={2.f,1.f,0.f,0.f},occluded[]={0.f,0.f,0.f,.5f};
            auto* liquid=screenTexture(d,W,H,wet);auto* contact=screenTexture(d,W/2,H/2,occluded);
            for(bool weather:{false,true})for(bool hdr:{false,true})for(bool contactOn:{false,true}){
                std::vector<unsigned char> result[2];
                for(unsigned withMask=0;withMask<2;++withMask){
                    WorldRenderer sample(d);
                    WorldScreenEffectsTest::waterMaskProfile(sample,weather,contactOn);
                    assert(renderChecked(sample,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,
                                         withMask?liquid:nullptr,scene,contactOn?contact:ao,1,hdr?scene:nullptr));
                    result[withMask]=composedImage();
                }
                assert((result[0]!=result[1])==contactOn);
            }
            contact->Release();liquid->Release();
            std::puts("WORLD_WATER_MASK_DEMAND_PASS hdr=2 weather=2 contact=2");
        }
        for(float altitude:{300.f,-300.f}){
            std::vector<unsigned char> empty[2],history[2],reentry[2],emptyColor[2],reentryColor[2];
            for(unsigned clipped=0;clipped<2;++clipped){
                WorldScreenEffectsTest::fixtureContext(*world);WorldScreenEffectsTest::fog(*world,true);
                WorldScreenEffectsTest::fogAltitude(*world,0,true);
                assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
                WorldScreenEffectsTest::fogAltitude(*world,altitude,clipped!=0);meter.fogDraws=0;
                for(unsigned frame=0;frame<2;++frame)assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
                assert(meter.fogDraws==(clipped?0:8));WorldScreenEffectsTest::neutralFogHistory(*world);
                empty[clipped]=WorldScreenEffectsTest::fogImage(*world);history[clipped]=WorldScreenEffectsTest::fogImage(*world,true);emptyColor[clipped]=composedImage();
                // Losing the optional bounds must resume accumulation from the
                // neutral history, with the same weight as an unclipped frame.
                WorldScreenEffectsTest::fogAltitude(*world,altitude+1,false);meter.fogWeight=-1;
                assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));assert(meter.fogWeight==.85f);
                WorldScreenEffectsTest::fixtureContext(*world);WorldScreenEffectsTest::fogAltitude(*world,0,true);
                assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
                reentry[clipped]=WorldScreenEffectsTest::fogImage(*world,true);reentryColor[clipped]=composedImage();
            }
            assert(empty[0]==empty[1]&&history[0]==history[1]&&reentry[0]==reentry[1]&&emptyColor[0]==emptyColor[1]&&reentryColor[0]==reentryColor[1]);
            for(size_t i=0;i<empty[1].size()/2;++i){unsigned short bits;std::memcpy(&bits,empty[1].data()+i*2,2);assert(bits==(i%4==3?0x3c00:0));}
        }
        std::puts("WORLD_EMPTY_FOG_PASS aboveBelow=1 draws=0 referenceDraws=4 exactNeutralHistory=1 resumedWeight=0.85 reentryExact=1 finalColorExact=1");
        {
            std::vector<unsigned char> history[2],reentry[2],emptyColor[2],reentryColor[2];
            for(unsigned clipped=0;clipped<2;++clipped){
                auto empty=std::make_shared<NorthlightRegionalFog::Field>();empty->fogCells=clipped?0:1;
                auto old=WorldScreenEffectsTest::replaceFogField(*world,empty);meter.fogDraws=0;
                assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
                assert(meter.fogDraws==(clipped?0:4));WorldScreenEffectsTest::neutralFogHistory(*world);history[clipped]=WorldScreenEffectsTest::fogImage(*world,true);emptyColor[clipped]=composedImage();
                for(unsigned repeat=0;repeat<2;++repeat){
                    const auto clears=meter.clears;
                    assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
                    if(clipped)assert(meter.clears==clears); // Already neutral raw fog/history need no clear.
                    assert(WorldScreenEffectsTest::fogImage(*world,true)==history[clipped]);
                }
                emptyColor[clipped]=composedImage(); // Compare both variants after the same light-history cadence.
                WorldScreenEffectsTest::replaceFogField(*world,old);meter.fogWeight=-1;
                assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));assert(meter.fogWeight==.85f);
                reentry[clipped]=WorldScreenEffectsTest::fogImage(*world,true);reentryColor[clipped]=composedImage();
            }
            assert(history[0]==history[1]&&reentry[0]==reentry[1]&&emptyColor[0]==emptyColor[1]&&reentryColor[0]==reentryColor[1]);
        }
        std::puts("WORLD_EMPTY_FIELD_PASS draws=0 referenceDraws=4 historyExact=1 reentryExact=1 finalColorExact=1");
        WorldScreenEffectsTest::ambientVariant(*world);
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
        screenOk(d->CreateVertexBuffer(32,0,0,D3DPOOL_MANAGED,&vb,nullptr));screenOk(d->CreateIndexBuffer(12,0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib,nullptr));
        // Mesh expiry must not switch the analytic GI fallback off. There are no
        // resident probe keys in either image; disabling GI is the negative control.
        for(bool hdr:{false,true})for(bool merged:{false,true}){
            std::vector<unsigned char> images[4];
            for(unsigned mode=0;mode<4;++mode){
                WorldRenderer fallback(d);WorldScreenEffectsTest::giFallbackSetup(fallback,vb,ib,mode,merged);
                assert(renderChecked(fallback,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,7,hdr?scene:nullptr));
                assert(WorldScreenEffectsTest::sceneDrawn(fallback)==(mode==0));
                images[mode]=composedImage();
                WorldScreenEffectsTest::removeFakeScene(fallback); // vb/ib belong to this fixture.
            }
            float delta=0,control=0;for(size_t i=0;i<images[0].size();i+=2){
                unsigned short a,b,c;std::memcpy(&a,&images[0][i],2);std::memcpy(&b,&images[1][i],2);std::memcpy(&c,&images[2][i],2);
                assert(std::isfinite(screenHalf(a))&&std::isfinite(screenHalf(b)));
                delta=std::max(delta,std::abs(screenHalf(a)-screenHalf(b)));control=std::max(control,std::abs(screenHalf(a)-screenHalf(c)));
            }
            std::printf("WORLD_GI_STREAMING_FALLBACK hdr=%u merged=%u maxDifference=%g negativeControl=%g msaa=%u\n",unsigned(hdr),unsigned(merged),delta,control,msaa);
            assert(delta==0&&control>.001f&&images[2]==images[3]);
            if(!merged){
                WorldRenderer neutralMap(d);
                WorldScreenEffectsTest::giFallbackSetup(neutralMap,vb,ib,0,false);
                WorldScreenEffectsTest::refuseUnshadowedLighting(neutralMap);
                assert(renderChecked(neutralMap,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,7,hdr?scene:nullptr));
                assert(composedImage()==images[0]);
                WorldScreenEffectsTest::removeFakeScene(neutralMap);
                std::printf("WORLD_UNSHADOWED_FALLBACK_PASS hdr=%u msaa=%u finalColor=exact\n",unsigned(hdr),msaa);
            }
        }
        for(unsigned medium=0;medium<3;++medium){
            std::vector<unsigned char> images[5];
            for(unsigned mode=0;mode<5;++mode){
                WorldRenderer fallback(d);WorldScreenEffectsTest::fogStreamingSetup(fallback,vb,ib,mode,medium);
                assert(renderChecked(fallback,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
                assert(WorldScreenEffectsTest::sceneDrawn(fallback)==((mode==2||mode==4)));
                images[mode]=WorldScreenEffectsTest::fogImage(fallback,false,false,(mode==2||mode==4));
                WorldScreenEffectsTest::removeFakeScene(fallback);
            }
            assert(images[0]==images[4]);
            float delta=0,control=0;for(size_t i=0;i<images[0].size();i+=2){
                unsigned short a,b,c;std::memcpy(&a,&images[0][i],2);std::memcpy(&b,&images[1][i],2);std::memcpy(&c,&images[3][i],2);
                assert(std::isfinite(screenHalf(a))&&std::isfinite(screenHalf(b)));
                delta=std::max(delta,std::abs(screenHalf(a)-screenHalf(b)));control=std::max(control,std::abs(screenHalf(a)-screenHalf(c)));
            }
            std::printf("WORLD_FOG_STREAMING_PROVIDER medium=%u maxDifference=%g sourceOffControl=%g msaa=%u\n",medium,delta,control,msaa);
            assert(delta==0&&control>.00001f);
            // Owned maps may occlude radiance. Neither visibility nor source
            // strength may change where fog exists or its optical thickness.
            for(unsigned mode=1;mode<4;++mode)for(size_t pixel=0;pixel<images[0].size()/8;++pixel)
                assert(!std::memcmp(images[0].data()+pixel*8+6,images[mode].data()+pixel*8+6,2));
        }
        auto* screenFog=WorldScreenEffectsTest::screenFog(*world);WorldScreenEffectsTest::fakeScene(*world,vb,ib);meter.failSceneAt=3;
        meter.fogWeight=-1;
        assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
        assert(!WorldScreenEffectsTest::sceneDrawn(*world)&&meter.fogWeight==.85f&&WorldScreenEffectsTest::screenFog(*world)==screenFog);WorldScreenEffectsTest::noSceneTargets(*world);assert(world->screenReady());
        WorldScreenEffectsTest::retryScene(*world);meter.failSceneAt=0;
        meter.fogWeight=-1;
        assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
        auto sceneBytes=WorldScreenEffectsTest::sceneBytes(*world);assert(sceneBytes>0&&world->ready()&&WorldScreenEffectsTest::sceneDrawn(*world)&&meter.fogWeight==0&&WorldScreenEffectsTest::screenFog(*world)==screenFog);
        const auto creates=meter.sceneCreates;assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));assert(meter.sceneCreates==creates);
        // A dormant GI consumer keeps its CPU atlas; upload on reactivation.
        WorldScreenEffectsTest::gi(*world,true);
        assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
        sceneBytes=WorldScreenEffectsTest::sceneBytes(*world);
        const auto probeBefore=WorldScreenEffectsTest::probeImage(*world);
        std::vector<unsigned short> nightImages[2];
        for(unsigned night=0;night<2;++night){
            WorldScreenEffectsTest::zeroGINight(*world,night?50:0);
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
            if(msaa)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));screenOk(d->GetRenderTargetData(msaa?resolved:out,readback));
            D3DLOCKED_RECT lock{};screenOk(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));nightImages[night].resize(W*H*4);
            for(unsigned y=0;y<H;++y)std::memcpy(nightImages[night].data()+y*W*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*8);
            screenOk(readback->UnlockRect());
        }
        float nightDelta=0;uint64_t nightDigest=1469598103934665603ull;
        for(unsigned i=0;i<nightImages[1].size();++i){const float value=screenHalf(nightImages[1][i]);assert(std::isfinite(value));nightDelta=std::max(nightDelta,std::abs(value-screenHalf(nightImages[0][i])));nightDigest=(nightDigest^nightImages[1][i])*1099511628211ull;}
        assert(nightDelta>.001f);std::printf("WORLD_ZERO_GI_NIGHT_PASS delta=%g digest=%llu msaa=%u\n",nightDelta,(unsigned long long)nightDigest,msaa);
        WorldScreenEffectsTest::restoreGINight(*world);
        const auto probeBytes=WorldScreenEffectsTest::probeBytes(*world);assert(probeBytes>0&&probeBytes<sceneBytes);
        WorldScreenEffectsTest::pressure(*world,true);world->endFrame();assert(WorldScreenEffectsTest::sceneBytes(*world)==sceneBytes); // Active BVH still owns these targets.
        WorldScreenEffectsTest::expireFakeScene(*world);world->endFrame();WorldScreenEffectsTest::noSceneTargets(*world);WorldScreenEffectsTest::probeRetired(*world);
        assert(WorldScreenEffectsTest::sceneBytes(*world)==0&&WorldScreenEffectsTest::screenFog(*world)==screenFog);
        WorldScreenEffectsTest::fixtureContext(*world);
        assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
        WorldScreenEffectsTest::pressure(*world,false);WorldScreenEffectsTest::fakeScene(*world,vb,ib,false);
        WorldScreenEffectsTest::gi(*world,true);
        assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
        assert(WorldScreenEffectsTest::sceneBytes(*world)==probeBytes&&WorldScreenEffectsTest::screenFog(*world)==screenFog&&WorldScreenEffectsTest::probeImage(*world)==probeBefore);
#ifdef NORTHLIGHT_GI_MERGE_TEST
        for(bool resident:{true,false}){
        if(!resident)WorldScreenEffectsTest::removeFakeScene(*world);
        for(unsigned mode=0;mode<5;++mode){
            WorldScreenEffectsTest::giMergeControl(*world,mode);meter.refuseGI=mode==1;meter.giShader=nullptr;meter.giBound=false;
            const auto creates=meter.giCreates,draws=meter.giDraws;
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
            const bool available=mode==2||mode==4,merged=available&&!resident;
            assert(WorldScreenEffectsTest::giMerged(*world)==available&&meter.giDraws-draws==unsigned(merged));
            assert(meter.giCreates-creates==unsigned(mode==1||available));
        }
        }
        meter.refuseGI=false;meter.giShader=nullptr;meter.giBound=false;
        std::printf("WORLD_GI_MERGE_SELECTOR_PASS blocked=1 creationFailure=1 merged=1 residentSeparate=1 giOff=1 resume=1 msaa=%u\n",msaa);
#endif
        WorldScreenEffectsTest::removeFakeScene(*world);vb->Release();ib->Release();
        std::printf("WORLD_SCENE_LAZY_PASS noBvhBytes=0 partialFailureKeptScreen=1 retryWithoutReset=1 pressuredExpiryReclaimed=1 probesRestored=1 sceneBytes=%llu\n",(unsigned long long)sceneBytes);
        // The unfurled legacy path still gets scratch; the next folded frame retires it.
        WorldScreenEffectsTest::fog(*world,false);world->setMaterialDetail(nullptr,0,48);
        for(bool folded:{false,true}){
            screenOk(d->SetRenderTarget(0,out));screenOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff666666,1,0));
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,folded?scene:nullptr,ao));
            WorldScreenEffectsTest::scratch(*world,!folded);assert(meter.creates==2);
        }
        std::puts("WORLD_SCRATCH_PASS folded_off=0 materials_on=1 invisible_reused=1 legacy_on=1 retired_on_disable=1");
        std::vector<unsigned char> ambientOnly[2];
        for(unsigned direct=0;direct<2;++direct){
            if(direct)WorldScreenEffectsTest::resetResources(*world);
            WorldScreenEffectsTest::direct(*world,direct!=0);WorldScreenEffectsTest::fog(*world,true);
            screenOk(d->SetRenderTarget(0,out));
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
            ambientOnly[direct]=WorldScreenEffectsTest::fogImage(*world);
        }
        // Missing shadow maps keep neutral visibility; they cannot extinguish
        // the source or move/remove the medium itself.
        assert(ambientOnly[0]!=ambientOnly[1]);
        for(size_t pixel=0;pixel<ambientOnly[0].size()/8;++pixel)
            assert(!std::memcmp(ambientOnly[0].data()+pixel*8+6,ambientOnly[1].data()+pixel*8+6,2));
        float maximumScatter=0,minimumTransmission=1;
        for(size_t pixel=0;pixel<ambientOnly[0].size()/8;++pixel)for(unsigned channel=0;channel<4;++channel){unsigned short bits;std::memcpy(&bits,ambientOnly[0].data()+pixel*8+channel*2,2);const float value=screenHalf(bits);assert(std::isfinite(value)&&value>=0);if(channel<3)maximumScatter=std::max(maximumScatter,value);else minimumTransmission=std::min(minimumTransmission,value);}
        assert(maximumScatter>.00001f&&minimumTransmission<.999f);
        std::printf("WORLD_AMBIENT_FOG_PASS noBvh=1 independentUpload=1 resetRestored=1 sunZero=1 moonZero=1 scatter=%g transmission=%g unshadowedDirectRetained=1 extinctionIndependent=1 msaa=%u\n",maximumScatter,minimumTransmission,msaa);
        WorldScreenEffectsTest::diagnosticFrames(*world,W,H);
        WorldScreenEffectsTest::emptyDebugViews(*world,out,resolved,readback,depth,W,H);
        {
            auto world=std::make_unique<WorldRenderer>(&meter);WorldScreenEffectsTest::setup(*world,W,H);
        // Four adjacent classes share native stencil: names, self-lit M2,
        // prelit WMO (AO/local lights/fog), and ordinary lit geometry.
        IDirect3DSurface9* receiverDepth=nullptr;
        screenOk(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&receiverDepth,nullptr));
        float unlitPixels[4][3]={},prelitPixels[4][3]={};
        for(unsigned mode=0;mode<4;++mode){
            WorldScreenEffectsTest::fixtureContext(*world);WorldScreenEffectsTest::fog(*world,mode>=2);
            world->setMaterialDetail(mode&1?detail:nullptr,1,48,nullptr,8);
            // Establish the same material configuration before measuring mask cost.
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
            screenOk(d->SetRenderTarget(0,out));screenOk(d->SetDepthStencilSurface(receiverDepth));
            D3DVIEWPORT9 viewport{0,0,W,H,0,1};screenOk(d->SetViewport(&viewport));
            screenOk(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
            screenOk(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_STENCIL,0xff00ff00,1,0));
            D3DRECT name{0,0,LONG(W/4),H},unlit{LONG(W/4),0,LONG(W/2),H},prelit{LONG(W/2),0,LONG(W*3/4),H};
            screenOk(d->Clear(1,&name,D3DCLEAR_STENCIL,0,1,NorthlightNameStencil::kBit));
            screenOk(d->Clear(1,&unlit,D3DCLEAR_STENCIL,0,1,NorthlightUnlitReceiver::Bit));
            screenOk(d->Clear(1,&prelit,D3DCLEAR_STENCIL,0,1,NorthlightUnlitReceiver::PrelitBit));
            screenOk(d->SetDepthStencilSurface(nullptr));
            world->setNameMask(receiverDepth);world->setUnlitMask(receiverDepth);world->setPrelitMask(receiverDepth);
            const unsigned targets=meter.creates;
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
            assert(meter.creates==targets); // no additional full-size texture
            if(msaa)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));
            screenOk(d->GetRenderTargetData(msaa?resolved:out,readback));D3DLOCKED_RECT lock{};screenOk(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned channel=0;channel<3;++channel){
                unsigned short nameValue,unlitValue,prelitValue;
                const auto* row=static_cast<char*>(lock.pBits)+(H/2)*lock.Pitch;
                std::memcpy(&nameValue,row+(W/8)*8+channel*2,2);std::memcpy(&unlitValue,row+(W*3/8)*8+channel*2,2);
                std::memcpy(&prelitValue,row+(W*5/8)*8+channel*2,2);prelitPixels[mode][channel]=screenHalf(prelitValue);
                assert(std::abs(screenHalf(nameValue)-(channel==1?1.f:0.f))<.002f);
                unlitPixels[mode][channel]=screenHalf(unlitValue);
                if(mode<2)assert(std::abs(unlitPixels[mode][channel]-.4f)<.003f);
            }
            screenOk(readback->UnlockRect());
        }
        float fogDelta=0;
        for(unsigned k=0;k<3;++k){assert(std::abs(unlitPixels[0][k]-unlitPixels[1][k])<.001f);assert(std::abs(unlitPixels[2][k]-unlitPixels[3][k])<.001f);fogDelta=std::max(fogDelta,std::abs(unlitPixels[2][k]-unlitPixels[0][k]));}
        assert(fogDelta>.002f);
        for(unsigned k=0;k<3;++k){assert(std::abs(prelitPixels[0][k]-prelitPixels[1][k])<.001f);assert(std::abs(prelitPixels[2][k]-prelitPixels[3][k])<.001f);assert(prelitPixels[0][k]>0&&prelitPixels[0][k]<=unlitPixels[0][k]+.003f);}
        // Exercise the actual optional HDR selector, not only its bytecode:
        // low advertised caps, failed shader creation, combined draw and a
        // diagnostic capture retaining the original two-pass input ownership.
        const float linear[]={.132868f,.132868f,.132868f,1};auto* hdrBase=screenTexture(d,W,H,linear);
        const float skyOnly[]={.8f,.1f,.6f,1};auto* skyBase=screenTexture(d,W,H,skyOnly);
        const float expanded[]={2.f,1.3f,.8f,1};auto* expandedBase=screenTexture(d,W,H,expanded);
        std::vector<unsigned short> hdrReference;WorldScreenEffectsTest::fog(*world,false);
        world->setMaterialDetail(detail,1,48,nullptr,8);
        for(unsigned mode=0;mode<8;++mode){
            WorldScreenEffectsTest::fixtureContext(*world);
            if(mode<3){WorldScreenEffectsTest::retryHdrCombined(*world);meter.combinedShader=nullptr;}
            meter.shaderSlotLimit=mode==0?512:0;meter.refuseCombined=mode==1;
            if(mode==3)assert(world->requestNormalGpuDiagnosticCapture());
            screenOk(d->ColorFill(out,nullptr,0xff00ff00));world->setNameMask(receiverDepth);world->setUnlitMask(receiverDepth);world->setPrelitMask(receiverDepth);
            const auto combinedBefore=meter.combinedDraws;
            WorldScreenEffectsTest::discardScratch(*world);
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,100+mode,mode>=6?expandedBase:mode==5?skyBase:hdrBase,mode!=4&&mode!=6,mode==5));
            WorldScreenEffectsTest::scratch(*world,mode!=2&&mode!=5&&mode!=7);
            assert(WorldScreenEffectsTest::hdrCombined(*world)==(mode>=2));
            assert(meter.combinedDraws-combinedBefore==(mode==2||mode==5||mode==7?1u:0u));
            if(msaa)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));screenOk(d->GetRenderTargetData(msaa?resolved:out,readback));
            D3DLOCKED_RECT lock{};screenOk(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            std::vector<unsigned short> image(W*H*4);for(unsigned y=0;y<H;++y)std::memcpy(image.data()+y*W*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*8);screenOk(readback->UnlockRect());
            if(!mode||mode==6)hdrReference=image;
            for(unsigned i=0;i<image.size();++i){assert(std::isfinite(screenHalf(image[i])));assert(std::abs(screenHalf(image[i])-screenHalf(hdrReference[i]))<.004f);if(i%4==3)assert(image[i]==hdrReference[i]);}
        }
        // A failed draw restores native state and disables WorldRenderer until
        // its existing explicit recovery path is invoked.
        WorldScreenEffectsTest::fixtureContext(*world);meter.failCombinedDraw=true;
        IDirect3DSurface9* beforeTarget=nullptr;IDirect3DPixelShader9* beforeShader=nullptr;
        screenOk(d->GetRenderTarget(0,&beforeTarget));screenOk(d->GetPixelShader(&beforeShader));
#ifdef NORTHLIGHT_WORLD_STATE_SCOPE_TEST
        DWORD originalFogEnable=0;
        screenOk(d->GetRenderState(D3DRS_FOGENABLE,&originalFogEnable));
        screenOk(d->SetRenderState(D3DRS_FOGENABLE,TRUE));
        {
            SavedState expired(&meter);
            assert(expired.restore());
            assert(!renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,108,expandedBase,true,false,&expired));
        }
        DWORD fogEnable=0;
        screenOk(d->GetRenderState(D3DRS_FOGENABLE,&fogEnable));assert(fogEnable==TRUE);
#else
        assert(!renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,108,expandedBase));
#endif
        IDirect3DSurface9* afterTarget=nullptr;IDirect3DPixelShader9* afterShader=nullptr;
        screenOk(d->GetRenderTarget(0,&afterTarget));screenOk(d->GetPixelShader(&afterShader));assert(afterTarget==beforeTarget&&afterShader==beforeShader);
        beforeTarget->Release();afterTarget->Release();if(beforeShader)beforeShader->Release();if(afterShader)afterShader->Release();
        assert(!meter.failCombinedDraw&&!world->screenReady());world->recover();WorldScreenEffectsTest::fixtureContext(*world);
        world->setMaterialDetail(detail,1,48,nullptr,8);
#ifdef NORTHLIGHT_WORLD_STATE_SCOPE_TEST
        {
            SavedState enclosing(&meter);
            assert(enclosing.ok);
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,109,expandedBase,true,false,&enclosing));
            screenOk(d->GetRenderState(D3DRS_FOGENABLE,&fogEnable));assert(fogEnable==FALSE);
        }
        screenOk(d->GetRenderState(D3DRS_FOGENABLE,&fogEnable));assert(fogEnable==TRUE);
        screenOk(d->SetRenderState(D3DRS_FOGENABLE,originalFogEnable));
        std::printf("WORLD_STATE_SCOPE_PASS borrowedRestores=1 expiredDrawFailureRestores=1 msaa=%u\n",msaa);
#else
        assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,109,expandedBase));
#endif
        meter.shaderSlotLimit=0;meter.refuseCombined=false;expandedBase->Release();skyBase->Release();hdrBase->Release();receiverDepth->Release();
        std::printf("HDR_COMBINED_WORLD_GPU_PASS capFallback=1 creationFallback=1 actualCombinedDraw=1 skyBaseGeometry=1 diagnosticFallback=1 policyFallback=1 expandedHdrCompared=1 drawFailureRestored=1 namesUnlitPrelitPreserved=1 msaa=%u\n",msaa);
        std::printf("UNLIT_RECEIVER_GPU_PASS nativeRadiance=1 atmosphereRetained=1 materialInvariant=1 namesRetained=1 extraFullSizeTextures=0 msaa=%u\n",msaa);
        std::printf("PRELIT_WMO_PIPELINE_PASS SDR_HDR_materialInvariant=1 namesUnlitIsolated=1 extraFullSizeTextures=0 msaa=%u\n",msaa);
        }

    }

#ifdef NORTHLIGHT_FOG_DISTANCE_TEST
    {
        WorldScratchMeter meter(d,W,H);
        auto fogOnly=std::make_unique<WorldRenderer>(&meter),reference=std::make_unique<WorldRenderer>(&meter);
        WorldScreenEffectsTest::fogOnlySetup(*fogOnly,W,H,false);WorldScreenEffectsTest::fogOnlySetup(*reference,W,H,true);
        for(unsigned frame=0;frame<8;++frame){
            std::vector<unsigned char> actual[2],composite[2];unsigned mode=0;
            for(auto* world:{reference.get(),fogOnly.get()}){
                WorldScreenEffectsTest::fogOnlyFrame(*world,frame,world==reference.get());
                assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,frame));
                actual[mode]=WorldScreenEffectsTest::fogImage(*world,true);
                if(msaa)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));screenOk(d->GetRenderTargetData(msaa?resolved:out,readback));
                D3DLOCKED_RECT lock{};screenOk(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));composite[mode].resize(W*H*8);
                for(unsigned y=0;y<H;++y)std::memcpy(composite[mode].data()+y*W*8,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*8);screenOk(readback->UnlockRect());++mode;
                if(world==fogOnly.get())assert(WorldScreenEffectsTest::distanceLighting(*world)==(frame==4||frame==5));
            }
            if(actual[0]!=actual[1]||composite[0]!=composite[1])std::fprintf(stderr,"FOG_DISTANCE_DIFF frame=%u fogEqual=%u compositeEqual=%u\n",frame,unsigned(actual[0]==actual[1]),unsigned(composite[0]==composite[1]));
            assert(actual[0]==actual[1]);assert(composite[0]==composite[1]);
        }
        std::puts("WORLD_FOG_DISTANCE_PASS coldWithoutLightingTargets=1 historyImagesExact=1 lightingSwitches=1 stateRestored=1");
    }
#endif
#ifdef NORTHLIGHT_FOG_VOLUME_TEST
    {
        WorldScratchMeter meter(d,W,H);auto world=std::make_unique<WorldRenderer>(&meter);
        WorldScreenEffectsTest::setup(*world,W,H);
        std::vector<unsigned char> pressureReference;
        for(unsigned frame=0;frame<27;++frame){
            WorldScreenEffectsTest::volumeFixture(*world,frame);
            meter.refuseVolume=frame==8;meter.shaderSlotLimit=frame==9?160:frame==10?512:0;
            meter.failScreenAt=frame==12?meter.screenCreates+2:0;
            auto before=WorldScreenEffectsTest::volumeCounts(*world);
            const float sentinel[12]={31,32,33,34,35,36,37,38,39,40,41,42};screenOk(d->SetPixelShaderConstantF(154,sentinel,3));
            meter.beginFogTrace();
            if(frame==26){meter.failVolumeDraw=true;assert(!renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,frame));assert(!meter.failVolumeDraw&&!WorldScreenEffectsTest::volumeValid(*world));}
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,frame));
            meter.recordFog=false;
            std::printf("FOG_ATMOSPHERE_TRACE frame=%u commands=%u hash=%016llx\n",frame,meter.fogCommandCount,(unsigned long long)meter.fogCommands);
            float restored[12]={};screenOk(d->GetPixelShaderConstantF(154,restored,3));
            if(std::memcmp(sentinel,restored,sizeof sentinel)){std::fprintf(stderr,"FOG_VOLUME_STATE_DIFF frame=%u",frame);for(float value:restored)std::fprintf(stderr," %.9g",value);std::fputc('\n',stderr);}
            assert(!std::memcmp(sentinel,restored,sizeof sentinel));
            auto after=WorldScreenEffectsTest::volumeCounts(*world);
            if(frame==1||frame==2||frame==16||frame==17||frame==20||frame==25||frame==6||frame==7)assert(after.first==before.first&&after.second==before.second+1);
            else if(frame==4||frame==8||frame==9||frame==12)assert(!WorldScreenEffectsTest::volumeValid(*world));
            else assert(WorldScreenEffectsTest::volumeValid(*world)&&after.first==before.first+1);
            if(frame!=4){auto image=WorldScreenEffectsTest::fogImage(*world,false,true);bool scattering=false;
                if(frame!=21&&frame!=22){std::uint64_t hash=14695981039346656037ull;
                    for(auto byte:image)hash=(hash^byte)*1099511628211ull;
                    std::printf("FOG_ATMOSPHERE_IMAGE frame=%u hash=%016llx\n",frame,(unsigned long long)hash);}
                if(frame==5)pressureReference=image;
                if(frame==6)assert(image==pressureReference);
                for(size_t i=0;i<image.size();i+=2){unsigned short bits;std::memcpy(&bits,image.data()+i,2);const float value=screenHalf(bits);assert(std::isfinite(value)&&value>=0);if(i%8!=6)scattering|=value>.001f;else assert(value<=1);}
                assert(scattering);}
        }
        std::printf("WORLD_FOG_VOLUME_PASS frames=27 cached=8 cameraReprojection=1 settledCache=1 staticNoiseCache=1 windUpdates=1 offOn=1 pressureRetained=1 creationFallback=1 capsFallback=1 quietAtlasAt512Slots=1 partialOomFallback=1 failedPassRecovery=1 stateRestored=1 msaa=%u\n",msaa);
    }
    {
        WorldScratchMeter meter(d,W,H);auto world=std::make_unique<WorldRenderer>(&meter);
        WorldScreenEffectsTest::setup(*world,W,H);
        for(bool clouds:{false,true}){
            WorldScreenEffectsTest::cloudFixture(*world,clouds);
            const unsigned before=meter.cloudDraws;
            meter.beginFogTrace();
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,0));
            meter.recordFog=false;
            assert(meter.cloudDraws-before==unsigned(clouds));
            std::printf("FOG_CLOUD_COMMANDS enabled=%u commands=%u hash=%016llx\n",unsigned(clouds),meter.fogCommandCount,(unsigned long long)meter.fogCommands);
        }
    }
#endif
    {
        WorldScratchMeter meter(d,W,H);
        auto lit=std::make_unique<WorldRenderer>(&meter),unlit=std::make_unique<WorldRenderer>(&meter);
        WorldScreenEffectsTest::setup(*lit,W,H);WorldScreenEffectsTest::setup(*unlit,W,H);
        const float initialDepth=1;auto* motionDepth=screenTexture(d,W,H,&initialDepth,1);
        D3DLOCKED_RECT depthLock{};screenOk(motionDepth->LockRect(0,&depthLock,nullptr,0));
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const float distance=x<W/3?8.f:x<2*W/3?32.f:1000.f;
            const float value=1000.f/999.f*(1-1/distance);std::memcpy(static_cast<char*>(depthLock.pBits)+y*depthLock.Pitch+x*4,&value,4);}
        screenOk(motionDepth->UnlockRect(0));
        unsigned illuminated=0,retired=0;
        for(unsigned frame=0;frame<32;++frame){
            std::vector<unsigned char> history[2],current[2];unsigned mode=0;
            for(auto* world:{lit.get(),unlit.get()}){
                WorldScreenEffectsTest::localFogMotionFrame(*world,frame,world==lit.get());
                assert(renderChecked(*world,d,out,motionDepth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,frame));
                history[mode]=WorldScreenEffectsTest::fogImage(*world,true);
                current[mode++]=WorldScreenEffectsTest::fogImage(*world,false,true);
            }
            assert(history[0]==history[1]); // Lamp radiance must never enter atmospheric history.
            if(frame%16>=14){assert(current[0]==current[1]);++retired;}
            else{
                float peak=0;for(size_t i=0;i<current[0].size();i+=2){unsigned short a,b;std::memcpy(&a,current[0].data()+i,2);std::memcpy(&b,current[1].data()+i,2);
                    if(i%8==6)assert(a==b);else peak=std::max(peak,screenHalf(a)-screenHalf(b));}
                assert(peak>.001f);++illuminated;
            }
        }
        motionDepth->Release();
        std::printf("LOCAL_FOG_MOTION_PASS frames=32 illuminated=%u retired=%u historyClean=1 alphaUnchanged=1 temporalFallback=1 msaa=%u\n",illuminated,retired,msaa);
    }
    float bump=0,fog=0;for(unsigned i=0;i<W*H*3;++i){bump=std::max(bump,std::abs(images[1][i]-images[0][i]));fog=std::max(fog,std::abs(images[2][i]-images[0][i]));}
    std::printf("WORLD_SCREEN_GPU bumpDelta=%g fogDelta=%g noGeometry=1 noStaleShadows=1 stateRestored=1 msaa=%u\n",bump,fog,msaa);std::fflush(stdout);assert(bump>.002f&&fog>.002f);
#if NORTHLIGHT_TREE_MENU_TEST
    {WorldRenderer world(d);WorldScreenEffectsTest::canopyToggles(world);}
    std::puts("CANOPY_TOGGLES_PASS sun=1 moon=1 front=1 back=1 combinations=4 live=1");
#endif
    scene->Release();ao->Release();detail->Release();depth->Release();out->Release();if(resolved)resolved->Release();readback->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::puts("WORLD_SCREEN_GPU_PASS");
}
