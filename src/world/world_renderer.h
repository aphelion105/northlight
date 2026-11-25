#include "effect_coverage.h"
#include "screen_quad.h"
#pragma once
#include "capture_work_audit.h"
#include "adt_draw_bounds.h"
#include <cstdlib>
#include "utf8_path.h"
#include "material_profile.h"
#include "settings_schema.h"
#include "surface_frame.h"
#include "weather_shelter.h"
#include "stream_hooks.h"
#include "weather_memory.h"
#include "world_screen_fallbacks.h"
#include "name_stencil.h"
#include "world_context.h"
#include "effect_switches.h"
#include "frame_requirements.h"
#include "frame_parity_clock.h"
#include "world_camera.h"
#include "wmo_context.h"
#include "world_environment.h"
#include "menu_environment.h"
#include "underwater_fade.h"
#include "projection.h"
#include "celestial_context.h"
#include "celestial_sources.h"
#include "celestial_glow.h"
#include "apparent_horizon.h"
#include "twilight_fill.h"
#include "aurora_illumination.h"
#include "night_floor.h"
#include "regional_fog.h"
#include "regional_fog_cache.h"
#include "world_fog_field.h"
#include "world_moon_floor_fog.h"
#include "regional_shadow_range.h"
#include "celestial_profiles.h"
#if NORTHLIGHT_DEV_TUNING
#include "zone_tuning.h"
#endif
#include "world_gi.h"
#include "scene_source_cache.h"
#include "world_local_lights.h"
#include "model_light_attachments.h"
#include "local_light_selection.h"
#include "equipment_lights.h"
#include "native_light_bridge.h"
#include "particle_lights.h"
#include "particle_uploads.h"
#include "equipment_light_proof.h"
#include "native_direct_ownership.h"
#include "native_point_submit.h"
#include "native_local_light_renderer.h"
#include "local_light_scissor.h"
#include "local_light_batches.h"
#include "local_light_trace.h"
#include "point_light_shadow.h"
#include "world_point_targets.h"
#include "world_point_lighting.h"
#include "point_pipeline_warmup.h"
#include "point_replay_casters.h"
#include "point_replay_indices.h"
#include "point_transmission_filter.h"
#include "point_shadow_face_pass.h"
#include "weather_lens_heat.h"
#include "fog_ambient.h"
#include "local_light_compiled_shaders.h"
#include "replay_bounds.h"
#include "pose_bounds_handoff.h"
#include "replay_bounds_schedule.h"
#include "replay_bounds_metadata.h"
#include "replay_bounds_job.h"
#include <atomic>
#include <optional>
#include "world_math.h"
#include "shadow_transition.h"
#include "shadow_transition_math.h"
#include "shadow_transition_compiled_shaders.h"
#include "world_shadow_transition.h"
#include "terrain_shadow_programs.h"
#include "world_mesh_plan.h"
#include "world_streaming.h"
#include "world_mesh_pages.h"
#include "point_mesh_candidates.h"
#include "world_mesh_page_gpu.h"
#include "resource_memory_profile.h"
#include "memory_profile_snapshot.h"
#include "streaming_budget.h"
#include "streaming_phase_profile.h"
#include "cpu_retirement.h"
#include "deferred_gpu_release.h"
#include "geometry_memory.h"
#include "address_space_snapshot.h"
#include "terrain_reach_fallback.h"
#include "mesh_pool_trim.h"
#include "mesh_buffer_store.h"
#include "memory_admission_probe.h"
#include <chrono>
#include "world_probe_cache.h"
#include "world_probe_progress.h"
#include "probe_cascade.h"
#include "world_probe_atlas.h"
#include "celestial_time_warp.h"
#include "cascade_anchor.h"
#include "actor_shadow_map.h" // 0.3.209 (task 29)
#include "actor_side_hold.h" // the inset hysteresis of the actor map
#include "actor_side_tracker.h" // crowd/self shimmer diagnostics: actor map vs near cascade per caster
#include "shadow_pivot.h"
#include "flight_shadow.h"
#include "world_dynamic_probes.h"
#include "shadow_bounds.h"
#include "shadow_refresh_budget.h"
#include "directional_shadow_batches.h"
#include "directional_cache_mesh.h"
#include "static_cache_slices.h"
#include "terrain_shadow_candidates.h"
#include "legacy_fog.h"
#include "spell_fog.h"
#include "spell_fog_proof.h"
#include "spell_fog_march.h"
#include "horizon_haze.h"
#include "weather_state.h"
#include "weather_effects.h"
#include "forever_atmosphere.h"
#include "expansion_atmosphere.h"
#include "fog_clouds.h"
#include "gpu_budget.h" // 0.3.200 (gpu budget)
#include "job_system.h" // 0.3.200 (jobs)
#include "sun_hue.h"
#include "replay_constant_ranges.h"
#include "replay_pose_groups.h"
#include "replay_capture_constants.h"
#include "replay_shadow_policy.h"
#include "shader_constant_usage.h"
#include "capture_phase_profile.h"
#include "world_compiled_shaders.h"
#include "fog_volume.h"
#include "world_fog_screen.h"
#include "world_regional_fog_gpu.h"
#include "world_atmosphere_commands.h"
#include "world_forever_fog_shaders.h"
#include "material_pixel_audit.h"
#include "patch_shadow_shader.h"
#include "model_normal_slice.h"
#include "shadow_alpha.h"
#include "shadow_alpha_draw.h"
#include "shadow_alpha_replay.h"
#include "terrain_capture_bounds.h"
#include "draw_snapshot.h"
#include "point_rigid_pose_rows.h"
#include "point_opaque_face_cache.h"
#include "point_mixed_face_cache.h"
#include "shadow_palette_pass.h"
#include "gpu_scene.h"
#include "world_replay_buffers.h"
#include "live_terrain_indices.h"
#include "dynamic_ring.h"
#include "lock_meter.h"
#include "replay_copies.h"
#include "vertex_declaration_cache.h"
#include "shadow_replay_submit.h"
#include "render_thread_probe.h"
#include "actor_deformation.h"
#include "sampled_vertex_cache.h"
#include "prepare_worker.h"
#include "shadow_fate.h"
#include "actor_texture_read.h"
#include "actor_scene_job.h"
#include "worker_actor_memo.h"
#include <set>
#include <unordered_set>
#include <map>
#include <tuple>
#include "world_shader_signatures.h"
#include "detail_doodad_shadow.h"
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <stdexcept>
#include "world_diagnostics.h"
#include "world_lighting_capture.h"
#include "shadow_terrain.h"
#include "live_terrain_gpu.h"
#include "static_shadow_request.h"
#include "static_shadow_gpu.h"
#include "static_plan_prebuild.h"
#include "static_shadow_dedup.h"
#include "static_m2_dedup.h"
#include "static_shadow_coverage.h"
#include "celestial_terrain_pass.h"
#include "quality_settings.h"
#include "diagnostics_switch.h"
#include "memory_switches.h"
#include "gi_solve_pool.h"
#include "near_reserve.h"
#include "capture_admission.h"
#include "effects_buckets.h"
#include <cstdarg>
#include "rigid_memory.h"

#include "world_composite.h"
#include "world_light_history.h"
#include "world_normals_pass.h"
#include "world_screen_lighting.h"
#include "world_local_lighting.h"
#include "world_actor_shadows.h"
#include "world_flash_shadows.h"
#include "world_directional_visibility.h"
#include "world_directional_targets.h"
#include "world_shadow_programs.h"
#include "world_replay_probe_pass.h"
#include "directional_gpu_pass.h"

// Included after SavedState. Worker never accesses D3D or game memory.
#include "world_publication.h"
#include "world_mesh_stage.h"
class WorldRenderer : private NorthlightWorldPublication::State {
    NorthlightWorldComposite composite;
    friend struct WorldScreenEffectsTest;
    friend struct PipelineOffTest;
    friend struct TerrainCaptureDemandTest;
#if defined(NORTHLIGHT_WORLD_UPLOAD_TEST)
    friend struct WorldColdUploadTest;
#endif
    friend struct GrassShadowTest;
    friend struct PointTerrainCoverageTest;
    friend struct FlashShadowTest;
    std::shared_ptr<const NorthlightAuroraSource::Snapshot> auroraSource;
    std::string auroraSourceMap;
    std::shared_ptr<NorthlightGpuScene::Scene> gpuScene;
    IDirect3DDevice9* d;
    NorthlightShadowAlpha::ReplayAlpha shadowReplayAlpha{d};
    NorthlightMaterialPixelAudit materialPixelAudit{d};
    std::string root;
    std::shared_ptr<NorthlightGI::SceneSourceCache> sharedSceneSources;
    std::unique_ptr<StaticShadow::Streamer> staticStream;
    StaticShadow::GpuCache& staticCasters=gpuScene->staticCasters;
    std::shared_ptr<const StaticShadow::Snapshot> staticScene;
    StaticShadowDedup::Matcher staticMatcher;
    StaticM2Dedup::Memory staticM2Memory;unsigned long long staticM2Frame=0; /* rigid M2 doodads the static cache draws: see prepareStaticM2 */
    unsigned staticM2Skipped=0,staticM2Verified=0,staticM2Rejected=0;
    V staticFramePivot;
    bool staticPivotReady=false,staticAllowLoads=true;
    DWORD staticMemoryTick=0,staticLogTick=0,staticRetryTick=0;
    uint64_t staticFrame=0,staticAdmissionFrame=UINT64_MAX,staticReservedBytes=0;
    NorthlightGeometryMemory::Sample staticMemorySample;
    bool staticMemoryExact=true;
    NorthlightMemoryAdmission::Probe admissionProbe;
    unsigned staticDrawFailures=0,staticDedupSkipped=0,staticDedupCursor=0,staticDedupAttempts=0,staticDedupMatches=0;
    NorthlightGeometryMemory::StallWatch generationStall; /* 0.3.156, guarded by mutex: builder marks, render thread reports */
    static constexpr uint32_t GenerationStallLogMs=10000;
    DWORD actorRequestTick=0;
    std::atomic<unsigned> geometryBuildEstimateMs{1000}; /* 0.3.169: builder's smoothed build time, read by the render thread's lead */
    bool settingsRequestDirty=false,pipelineActive=true;
public:
    void applyLiveSettings(const NorthlightSettings::Config& next){
        // Frame cleanup has joined D3D-free jobs. Worker solver keys stay immutable;
        // GI enable and bounce count travel in each Request.
        auto profiles=next.celestial;
#if NORTHLIGHT_DEV_TUNING
        celestialShipped=profiles;profiles=NorthlightZoneTuning::applyCelestial(celestialShipped,*devSnapshot()); /* the saved table is the new shipped base; the developer overrides stay on top */
#endif
        const auto effective=NorthlightSettings::liveQualityUpdate(quality,next.quality);
        const bool giRequest=NorthlightSettings::giRequestChanged(quality,effective,celestialProfiles,profiles);
        const bool actorResources=quality.actorShadows!=effective.actorShadows||quality.actorShadowDetail!=effective.actorShadowDetail;
        const bool softChanged=quality.softShadows!=effective.softShadows;
        const bool shadowTuningChanged=quality.shadowStrength!=effective.shadowStrength||quality.shadowSoftness!=effective.shadowSoftness||quality.shadowContactRadius!=effective.shadowContactRadius;
        const bool nearIntervalChanged=quality.nearShadowInterval!=effective.nearShadowInterval;
        const bool celestialChanged=quality.celestialStyle!=effective.celestialStyle||quality.celestialEffects!=effective.celestialEffects;
        if(quality.godrays!=effective.godrays){fogScreen.invalidateHistory();fogVolume.valid=false;}
        if(celestialChanged){celestialLightMotion.reset();fogScreen.invalidateHistory();history.invalidate();}
        const bool directions=celestialChanged||quality.shadowDirectionSteps!=effective.shadowDirectionSteps||quality.shadowProvider!=effective.shadowProvider;
        const bool giToggle=quality.gi!=effective.gi||quality.worldShadows!=effective.worldShadows||quality.worldFog!=effective.worldFog;
        if(quality.disableOriginalTerrainShadows!=effective.disableOriginalTerrainShadows)history.invalidate();
        for(const auto& k:NorthlightQuality::Keys)if(NorthlightSettings::liveQuality(k.name))k.set(quality,k.get(effective));
        quality.preset=effective.preset;settingsRequestDirty=settingsRequestDirty||giRequest;
        if(giToggle)setEffects(requestedEffects); /* component hotkeys remain independent of the saved menu state */
        minSkinnedShadowTriangles=quality.minSkinnedTriangles;captureBudgetMiB=quality.captureBudgetMiB;actorShadowBudgetMiB=quality.actorShadowBudgetMiB;setNearReserve();
        replaySnapshots.configureBudget(size_t(captureBudgetMiB)*1048576,size_t(captureBudgetMiB)*524288);
        shadowFateDiagnostics=NorthlightQuality::shadowFate(quality);
        NorthlightDiagnostics::configure(quality.diagnostics!=0);NorthlightWeatherMemory::configure(quality.weatherMemory!=0);NorthlightMemorySwitches::configure(quality.memMap!=0);
        NorthlightRenderThreadProbe::configure(NorthlightQuality::renderProfile(quality),NorthlightQuality::replayProbe(quality));
        logf("PROFILE effective diagnostics=%u renderProfile=%u replayProbe=%u period=%u cpuGpuAdditive=0",unsigned(NorthlightDiagnostics::enabled()),unsigned(NorthlightRenderThreadProbe::profiling()),unsigned(NorthlightRenderThreadProbe::probing()),NorthlightRenderThreadProbe::profiling()?NorthlightRenderThreadProbe::ProfilePeriod:NorthlightRenderThreadProbe::SamplePeriod);
        NorthlightWorldContext::SelfReadStats::timed.store(NorthlightQuality::renderProfile(quality),std::memory_order_relaxed);
        if(actorResources)actorSettingsChanged();
        if(directions){invalidateShadowCache(true);fogScreen.invalidateHistory();history.invalidate();shadowsComposited=shadowFrameReady=false;}
        if(shadowTuningChanged)history.invalidate();
        if(softChanged){history.invalidate();if(!quality.softShadows)releaseSoftShadows();}
        if(nearIntervalChanged){history.invalidate();releaseNearEnvironment();}
        staticPrebuild.steps=float(quality.shadowDirectionSteps);
        celestialProfiles=std::move(profiles);paletteFrameValid=false;
    }
    // Read-only access to the COMMITTED terrain pages. Never start uploads,
    // loads, shadow updates or mesh publication from the native sky draw hook.
    uint64_t celestialTerrainGeneration()const{
        char map[64]={};float camera[3];
        if(failed||meshStore.table().empty()||batches.empty()||!NorthlightWorldContext::readMapAndCamera(map,camera)||uploadedMap!=map)return 0;
        return meshGeneration+1;
    }
    // 0.3.175 (S3): the renderer's effects buckets (RenderProfile sample frames; marks are no-ops otherwise).
    void setEffectsBuckets(NorthlightEffectsBuckets::Frame* frame){effectsBuckets=frame;}
    bool drawCelestialTerrain(unsigned body,const float* matrix){
        if(!celestialTerrainGeneration())return false;
        const int64_t started=NorthlightRenderThreadProbe::profiling()?QpcClock::now():0;
        NorthlightCelestialTerrain::RunStats runs;
        const bool listed=terrainBatchListGeneration==meshGeneration&&terrainBatchListSize==batches.size();
        if(!NorthlightCelestialTerrain::submitMask(d,batches,terrainBatchList,listed,NorthlightWorldMeshPages::PageIndexLimit,matrix,
            [&](UINT page,UINT& bound){Batch key;key.page=page;return bindMeshPage(key,bound);},runs))return false;
        auto& m=celestialMask;++m.redraws[body&1];m.accepted=runs.accepted;m.runs=runs.runs;m.triangles=runs.triangles;m.candidates=listed?terrainBatchList.size():batches.size();m.listed=listed;
        if(started&&captureFrequency.QuadPart>0){m.ms=double(QpcClock::now()-started)*1000.0/double(captureFrequency.QuadPart);m.peakMs=std::max(m.peakMs,m.ms);}
        const DWORD now=GetTickCount();
        if(NorthlightDiagnostics::enabled()&&(!m.lastLog||now-m.lastLog>=10000)){m.lastLog=now;
            logf("CELESTIAL terrain mask map=\"%s\" generation=%llu draws=%u triangles=%llu candidates=%zu accepted=%u runs=%u listed=%d redraws=%u,%u reuses=%u,%u ms=%.3f peakMs=%.3f",
                uploadedMap.c_str(),(unsigned long long)meshGeneration,m.runs,(unsigned long long)m.triangles,m.candidates,m.accepted,m.runs,int(m.listed),
                m.redraws[0],m.redraws[1],m.reuses[0],m.reuses[1],m.ms,m.peakMs);
            m.redraws[0]=m.redraws[1]=m.reuses[0]=m.reuses[1]=0;m.peakMs=0;}
        return true;
    }
    void noteCelestialTerrainReuse(unsigned body){++celestialMask.reuses[body&1];}
    unsigned workerFault()const noexcept {return workerFaultCode.load();}
    static const char* workerFaultMessage(unsigned code)noexcept {
        return code==1?"GI worker allocation failed":code==2?"GI worker exception":code==3?"GI worker unknown exception":"";
    }
private:
    bool menuDomain=false;std::string menuIdentity;NorthlightMenu::Snapshot menuSnapshot;NorthlightNativeLights::Snapshot menuLights;
    std::weak_ptr<Snapshot> observedPublication;
    std::string loggedCacheMessage; // last logged "WORLD cache" message: uncached maps republish the same error per GI request, log it once
    std::weak_ptr<NorthlightGI::BVH> uploaded; // GPU commit must not retain the CPU scene.
    std::weak_ptr<const NorthlightWorldMesh::WorldMeshUploadPlan> uploadedMeshPlan;
    std::vector<float> uploadedAlphaCutoffs;
    uint64_t uploadedSerial=0;
    std::string uploadedProbeMap;
    std::shared_ptr<const StaticShadow::OwnerSet> uploadedStaticOwners;
    uint64_t staticOwnerGeneration=UINT64_MAX;
    uint64_t meshGeneration=0; // committed mesh identity; content signatures decide shadow reuse
    NorthlightEffectsBuckets::Frame* effectsBuckets=nullptr;
    void bucket(NorthlightEffectsBuckets::Bucket b){if(effectsBuckets)effectsBuckets->mark(b);}
    unsigned probeBlendPublishes=0,probeBlendSlots=0; // 0.3.197: per LOCAL log interval
    bool valid=false,failed=false,reportedContext=false;
    std::uint64_t materialLightRevision=0;
    struct AuthoredLightingMemo {
        std::uint64_t revision=~std::uint64_t{0};
        float bank[8]{};
        bool authored=false;
        unsigned roomLevel=0;
    } authoredLightingMemo[2];
    unsigned traceContext=0; /* 0.3.200 (frame trace): this frame's context path: 0 none, 1 terrain+global light, 2 terrain native light (camera disagreed), 3 WMO */
    unsigned contextRejects=0,frames=0,slowReports=0;
    DWORD diagnosticTick=0;
    DWORD resourceMemoryProfileAt=0;
    unsigned resourceMemoryProfileFrames=0;
    // 0.3.169 coverage hold and geometry lead (render thread). coverMax: largest eye-to-active
    // distance since the last WORLD camera line. A hold episode is active beyond 96 units.
    NorthlightWorldStreaming::Motion geometryMotion;float geometryLead=0,geometryAnchorOffset=0,coverMax=0,holdMax=0;
    bool coverageHold=false;DWORD holdStart=0;unsigned holdEpisodes=0;
    const char* skipReason="";
    bool gpuDiagnosticArmed=true,gpuNormalDiagnosticRequested=false,gpuDiagnosticFrame=false;unsigned gpuDiagnosticCaptures=0;
    bool diagnosticFogResolved=false,diagnosticLocalLighting=false;
    std::string gpuDiagnosticDirectory;
    std::unique_ptr<NorthlightLightingCapture::Capture> nativeDiagnosticCapture;
    std::string nativeDiagnosticMap;
    unsigned nativeDiagnosticWmoImages=0,nativeDiagnosticTerrainDraws=0;
    NorthlightWorldFogField regionalFog;
    NorthlightWorldScreenFallbacks screenFallbacks;
    IDirect3DTexture9* effectCoverage=nullptr; // Frame-local native overlay coverage, borrowed from the proxy.
    bool effectEmission=false;
    IDirect3DTexture9* rainMask=nullptr; // 0.3.202 (rain): the proxy's rain streak alpha mask (non-owning, set per frame, cleared after the composite)
    bool fogGeometryReady=false;
    NorthlightWorldContext::TerrainContext context;
    NorthlightCelestial::Context celestial,celestialLight;
    unsigned celestialOrbitReports=0;
    NorthlightCelestialOrbit::LightMotion celestialLightMotion;
    NorthlightApparentHorizon::Smooth apparentHorizon; // the camera's far edge for the discs (celestialContext), one frame behind
    std::string celestialLightMap;V celestialLightCamera;
    bool continuousCelestialShadows=false;
    NorthlightCelestialProfiles::Table celestialProfiles;
    NorthlightForever::Table foreverAtmosphere;NorthlightForever::Transition foreverTransition;NorthlightForever::Frame skyEnvironment;
    NorthlightRegionalShadow::Table shadowRanges;
    NorthlightCelestialProfiles::Transition paletteTransition;
    NorthlightCelestialProfiles::Profile framePalette;
    bool paletteFrameValid=false;std::string paletteFrameMap;DWORD paletteLogAt=0;
#if NORTHLIGHT_DEV_TUNING
    // Developer-only zone tuning (scripts/dev_tuning/zone_tuner.py), never compiled into a release build. Two files in the game root:
    // northlight-tuning.ini (polled, render thread) and northlight-tuning-state.json (written). celestialProfiles holds the EFFECTIVE table
    // (shipped + overrides); celestialShipped keeps the pristine one. The worker reads terrain_radius / ground fog scales from devOverrides.
    NorthlightCelestialProfiles::Table celestialShipped;
    std::shared_ptr<const NorthlightZoneTuning::Overrides> devOverrides=std::make_shared<const NorthlightZoneTuning::Overrides>(); // guarded by devMutex
    std::mutex devMutex;
    std::atomic<float> devTerrainReach{0};
    std::shared_ptr<const NorthlightZoneTuning::Overrides> devBuilt; // guarded by devMutex: the overrides snapshot of the last published build
    std::atomic<unsigned> devBuilds{0};                              // published builds so far
    std::string devError;unsigned devRevision=0;
    DWORD devPollAt=0,devStateAt=0,devChangeAt=0,devRequestedAt=0;bool devStampSeen=false;uint64_t devStamp[2]={};
    unsigned devSeenRevision=~0u,devBuildsAtRequest=0; // render thread: the revision last seen (for the quiet timer), builds counted at the last request
    std::shared_ptr<const NorthlightZoneTuning::Overrides> devSnapshot(){std::lock_guard<std::mutex> lock(devMutex);return devOverrides;}
    std::shared_ptr<const NorthlightZoneTuning::Overrides> devBuiltSnapshot(){std::lock_guard<std::mutex> lock(devMutex);return devBuilt;}
    static std::wstring devPath(const wchar_t* name){return std::wstring(rootPath)+name;}
    void devReload(){
        std::string text,error;const bool present=readSmallFile(devPath(L"northlight-tuning.ini"),text);
        NorthlightZoneTuning::Overrides next;std::istringstream in(text);
        if(present&&!NorthlightZoneTuning::parse(in,next,error)){devError=error;logf("TUNING overrides revision=%u keys=%u error=%s",devRevision,devSnapshot()->count(),error.c_str());return;}
        devError.clear();
        next.revision=devRevision+1;
        const auto published=std::make_shared<const NorthlightZoneTuning::Overrides>(std::move(next));
        celestialProfiles=NorthlightZoneTuning::applyCelestial(celestialShipped,*published);
        {std::lock_guard<std::mutex> lock(devMutex);devOverrides=published;}
        logf("TUNING overrides revision=%u keys=%u error=%s",++devRevision,published->count(),"");
    }
    void devWriteState(const char* map,const float* camera,uint32_t zone){
        const auto ov=devSnapshot();NorthlightZoneTuning::State s;
        s.build="dev-tuning";s.tick=GetTickCount();s.inWorld=true;s.map=map;s.zone=zone;std::memcpy(s.camera,camera,sizeof s.camera);
        s.night=NorthlightRegionalFog::nightFactor(celestialValid?celestial.dayFraction:-1.f);
        s.enabled=ov->enabled;s.loaded=ov->enabled?ov->count():0;s.groundFogActive=NorthlightZoneTuning::groundFogActive(zone);s.error=devError;s.revision=devRevision;{const auto built=devBuiltSnapshot();s.builtRevision=built?built->revision:0;}
        s.base=NorthlightZoneTuning::baseValues(celestialShipped,shadowRanges,s.map,zone);
        s.effective=NorthlightZoneTuning::effectiveValues(s.base,celestialProfiles,*ov,s.map,zone);
        const float reach=devTerrainReach.load();s.terrainReach=reach>0?reach:s.effective.terrainRadius;
        const std::string json=NorthlightZoneTuning::stateJson(s);
        const std::wstring path=devPath(L"northlight-tuning-state.json"),tmp=path+L".tmp";
        HANDLE f=CreateFileW(tmp.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return;
        DWORD wrote=0;const bool ok=WriteFile(f,json.data(),DWORD(json.size()),&wrote,nullptr)&&wrote==json.size();CloseHandle(f);
        if(!ok||!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING))DeleteFileW(tmp.c_str());
    }
    // Render thread, once per frame (from celestialPalette): poll the overrides (250 ms), ask the worker to rebuild after a terrain /
    // ground fog change (the existing geometryRebuild request, at most every 750 ms), write the state (500 ms).
    void devPoll(const char* map,const float* camera,uint32_t zone){
        const DWORD now=GetTickCount();
        if(!devPollAt||DWORD(now-devPollAt)>=250){
            devPollAt=now?now:1;WIN32_FILE_ATTRIBUTE_DATA a{};uint64_t stamp[2]={};
            if(GetFileAttributesExW(devPath(L"northlight-tuning.ini").c_str(),GetFileExInfoStandard,&a)){
                stamp[0]=(uint64_t(a.ftLastWriteTime.dwHighDateTime)<<32)|a.ftLastWriteTime.dwLowDateTime;stamp[1]=(uint64_t(a.nFileSizeHigh)<<32)|a.nFileSizeLow;}
            if(!devStampSeen||stamp[0]!=devStamp[0]||stamp[1]!=devStamp[1]){devStampSeen=true;devStamp[0]=stamp[0];devStamp[1]=stamp[1];devReload();}
        }
        // Only real override changes ask for a rebuild: the last published build used a snapshot that is not worker-equivalent to the
        // current one. Debounced (the snapshot unchanged for 2 s); one request outstanding, the next waits for a build to be published (or 5 s).
        const auto current=devSnapshot();const auto built=devBuiltSnapshot();
        if(current->revision!=devSeenRevision){devSeenRevision=current->revision;devChangeAt=now;}
        const unsigned builds=devBuilds.load(std::memory_order_relaxed);
        if(built&&!current->sameWorker(*built)&&DWORD(now-devChangeAt)>=2000&&(builds!=devBuildsAtRequest||DWORD(now-devRequestedAt)>=5000)){
            devBuildsAtRequest=builds;devRequestedAt=now?now:1;
            {std::lock_guard<std::mutex> lock(mutex);geometryRebuild=true;pending=true;}
            wake.notify_one();logf("TUNING worker rebuild requested (terrain_radius / ground fog scale changed)");
        }
        if(!devStateAt||DWORD(now-devStateAt)>=500){devStateAt=now?now:1;devWriteState(map,camera,zone);}
    }
#endif
    struct PaletteRegion {std::string map;NorthlightRegionalFog::Region region;int tx=0,ty=0;};
    std::shared_ptr<const PaletteRegion> publishedPaletteRegion; // guarded by mutex
    struct FogRegion {std::string map;V center;std::shared_ptr<const NorthlightRegionalFog::Field> field;uint64_t requestId=0;};
    std::shared_ptr<const FogRegion> publishedFogRegion; // guarded by mutex; no BVH/upload ownership
    bool celestialValid=false,shadowFrameReady=false;float authoredFill=1;
    // 0.3.200: the last good celestial read (see updateWorldContext), held across single rejected reads.
    static constexpr DWORD CelestialHoldMs=500;NorthlightCelestial::Context celestialHeld;std::string celestialHeldMap;DWORD celestialHeldAt=0;bool celestialHeldOk=false;unsigned long celestialHolds=0;
    NorthlightEffectSwitches::Settings effects,requestedEffects; /* requestedEffects: the hotkey state before the GI setting masks it */
    bool nativeShadowOwned=true,nativeShadowAvailable=true; // Raw backend stands have no competing native producer.
    V sourceDirections[2],sourceColors[2],sourceGlowColors[2]; // sourceGlowColors: the same sources for the fog volume pass (glow_strength / glow hue instead of light_strength)
    float sourceWeights[2]={1,0};
    NorthlightUnderwater::Fade underwaterFade; // the camera in liquid: the sun and moon fade out (updateWorldContext)
    DWORD underwaterLogAt=0;bool underwaterLogged=false;
    float sourceMatrices[2][2][16]={};
    // Far/NearShadowInterval>1 only: the map and matrix of the last complete far/near cascade render per source.
    NorthlightQuality::ShadowMapReuse farShadow[2],nearShadow[2];unsigned shadowPasses=0;
    // Model capture skip (quality_settings.h skipModelCapture), decided at the frame's first model draw.
    // captureDemand: a skipped frame had to keep a map/cube past its schedule; the next frame captures.
    enum CaptureMode:unsigned char {CaptureUndecided,CaptureFresh,CaptureSkipped};CaptureMode captureMode=CaptureUndecided;
    bool captureDemand=false,lastCaptureSkipped=false;unsigned lastCapturePhaseReads=0;int lastRenderDebug=0;size_t pointReplayCount=0;
    unsigned nearReuses=0,farReuses=0,captureSkippedFrames=0,captureDeferrals=0; /* 600-frame log window */
    DWORD animationEpoch=GetTickCount();
    NorthlightLegacyFog::Constants legacyFog;
    NorthlightLegacyFog::Hold legacyFogHold; /* uploaded fog (c25/c26): readable fog passes through, an unreadable frame holds the last value 2 s per map, then fades w; horizon haze and logs keep the raw legacyFog */
    bool legacyFogKnown=false; /* per frame: the last readOriginalFog decode() succeeded (a known fog-off result counts) */
    unsigned traceFog=0; /* per frame (FRAMETRACE fog=): 0 unknown, 1 read in the context draw, 2 composite used the held (or post-hold fading) value, fog-off included */
    NorthlightHorizonHaze::State horizonHazeState; /* game fog end/colour, smoothed per map */
    /* glow hue: the game's light slots (band 9 native glare, band 10 sunHalo), held for 2 s on one map without a proven read. */
    std::uint32_t lightSlots[NorthlightSunHue::Slots]={};bool lightSlotsValid=false;std::string lightSlotsMap;DWORD lightSlotsAt=0;
    NorthlightSunHue::GlowHue glowHueFrame,celestialGlowHueFrame;
    NorthlightWorldEnvironment worldEnvironment;
    NorthlightWmoContext::LightingHold globalLightHold; /* 0.3.207: sky block reads caught between the colour copy and the glare darkening (wmo_context.h) */
    NorthlightHorizonHaze::Constants hazeFrame; /* last composite haze constants, for the celestial veil */
    NorthlightSpellFog::Hold spellFogHold;DWORD spellFogTick=0;bool spellFogResetLogged=false; /* SpellFog: the smoothed per-frame estimate (spell_fog.h) */
    float skyTransmittanceFrame=1; /* CPU estimate of the fog transmittance toward the sun, for the veil */
    struct FogShader { unsigned major=0;int colorRegister=-1;bool verified=false,detailDoodad=false;NorthlightShadowAlpha::Info alpha; };
    std::unordered_map<IDirect3DPixelShader9*,FogShader> fogShaders;
    unsigned fogReports=0;
    float projection[3]={1,1,1};
    NorthlightWorldProbeAtlas probeAtlas;
    NorthlightWorldNormalsPass worldNormals;
    NorthlightWorldScreenLighting screenLighting;
    NorthlightWorldLightHistory history;
    bool temporalGeometryReady=false;
    bool traceHistory=false;DWORD traceHistoryTick=0,traceGeometryTick=0;unsigned traceHistoryCount=0,traceGeometryCount=0;
    // Static shadow cache: per (source,cascade) an R32F map with a 128-texel
    // margin holding static batches only, re-rendered when the snapped light
    // origin travels beyond the margin or the geometry/direction key changes.
    // Per frame only live terrain and replays are drawn (scratch) and united.
    static constexpr UINT ShadowCacheMargin=NorthlightWorldDirectionalTargets::CacheMargin;
    static constexpr UINT ShadowCacheSize=NorthlightWorldDirectionalTargets::CacheSize;
    NorthlightWorldDirectionalTargets directionalMaps;
    NorthlightWorldActorShadows actorMaps;
    // Actor maps and near replays share the current pose; the near environment alone may be retained.
    float actorMatrices[2][16]={};bool actorValid[2]={};unsigned actorCasterCount[2]={};float actorFogBounds[2][4]={{-1,-1,1,1},{-1,-1,1,1}};
    bool nearActorSkip[2]={}; /* final near map omits the current fine actor map's casters */
    NorthlightActorSide::Tracker actorSide[2]; /* diagnostics: which map each skinned caster went to, per source */
    NorthlightActorSideHold::Hold actorHold[2]; /* the casters held in the actor map (hysteresis), per source; about 48 KB each */
    unsigned replayNearSize=0; /* diagnostics: the bounds job's near list of the last frame */
    unsigned long replayNearStops=0; /* diagnostics: near walks that ended on their budget share */
    V actorCenter[2];bool actorCenterValid[2]={}; /* leashed centre per source (actor_shadow_map.h leash) */unsigned long actorNearSkipped=0,actorDrawn=0;std::vector<uint32_t> actorReplays; /* the near loop's skipped replays: exactly what the actor map draws */DWORD actorShadowLogAt[2]={}; /* per source: the moon's line is not starved by the sun's timer */
    NorthlightWorldShadowPrograms shadowPrograms;
    std::shared_ptr<const NorthlightLocalShadowSignature::Records> uploadedLocalShadowRecords;
    struct ShadowCacheKey {
        bool valid=false,localContentKnown=false;NorthlightWorldMath::ShadowFrame frame;V direction;
        uint64_t serial=0,chunkHash=0,staticSignature=0;
        NorthlightLocalShadowSignature::Digest localContent;
        NorthlightLocalShadowSignature::Memo localMemo;
        StaticShadow::GpuCache::ContentRecord staticContent;
    };
    ShadowCacheKey shadowCacheKey[4];
#include "world_shadow_transition.inl"
    DWORD shadowCacheLogAt=0,shadowPhaseLogAt=0;unsigned shadowCacheRenders=0,shadowCacheReuses=0,culledReplayDraws=0;
    unsigned long long replaySlotTested[4]={},replaySlotBounded[4]={},replaySlotCulled[4]={},replaySlotM2Skipped[4]={}; /* per source*2+cascade, 600-frame window */
#include "world_directional_cache_plan.inl"
    void invalidateShadowCache(bool retainPointStatic=false){
        resetShadowTransitions();staticCasters.settle();dropStaticDirtyJobs();
        for(auto& k:shadowCacheKey){k.valid=false;k.staticContent.valid=false;}for(auto& c:staticSlices)c.reset();
        for(auto& p:pointTargets.slots()){
            if(!retainPointStatic)p.cacheValid=false;
            p.ready=false;p.schedule.invalidate();p.faceCycle.reset();p.contentMemo.valid=false;
        }
        if(!retainPointStatic)clearPointKeeps();
        for(int s=0;s<2;++s){farShadow[s].invalidate();nearShadow[s].invalidate();actorValid[s]=false;nearActorSkip[s]=false;actorCenterValid[s]=false;}
    }
    uint64_t liveChunkHash()const{uint64_t h=1469598103934665603ull;for(const auto& c:liveTerrainChunks){h^=uint64_t(uint32_t(c.first))*0x9E3779B97F4A7C15ull+uint64_t(uint32_t(c.second));h*=1099511628211ull;}return h;}
    NorthlightWorldForeverFogShaders foreverFog;
    // 0.3.199 (fog clouds): the moving fog banks' own half-resolution pass (FogClouds), its noise volume and the wind clock. The volume's bytes are
    // generated once per process on a worker thread (first active frame, never DllMain) and shared by every device; the holder is leaked on
    // purpose and the worker detached, so neither DLL unload nor process exit waits on a thread under the loader lock.
    struct FogCloudNoise{
        std::vector<uint8_t> data;NorthlightFogClouds::Quantiles quantiles{};std::atomic<bool> ready{false},started{false},retryPending{false};std::atomic<DWORD> retryAt{0};
        void request(){
            if(ready.load(std::memory_order_acquire)||(retryPending.load()&&DWORD(GetTickCount()-retryAt.load())<1000))return;
            if(started.exchange(true))return;
            const auto retry=[this]{retryAt.store(GetTickCount());retryPending.store(true);started.store(false);};
            try{std::thread([this]{
                const auto t0=std::chrono::steady_clock::now();
                try{data=NorthlightFogClouds::generate();quantiles=NorthlightFogClouds::quantiles(data.data());}catch(...){retryAt.store(GetTickCount());retryPending.store(true);started.store(false);return;}
                const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
                ready.store(true,std::memory_order_release);logf("WORLD fog clouds noise ms=%.1f",ms);
            }).detach();}catch(...){retry();}
        }
    };
    static FogCloudNoise& fogCloudNoise(){static FogCloudNoise* noise=new FogCloudNoise;return *noise;}
    NorthlightRegionalFogGpu regionalFogGpu;
    NorthlightWorldLocalLighting localLighting;
    IDirect3DPixelShader9* fogVolumeShader(bool ambientOnly,bool actor,bool tiled=false){
        return regionalFogGpu.volumeShader(d,ambientOnly,actor,tiled?actorMaps.fogTiled():nullptr);
    }
    // 0.3.199 (fog temporal): last frame's resolved half-resolution fog (swapped with fogBlurred after the pass, so one extra target is enough), valid only after a frame that ran the pass.
    NorthlightFogScreen fogScreen;
    NorthlightFogVolume fogVolume;
    uint64_t fogVolumeKey=0,fogShadowRevision=0;
    std::shared_ptr<const NorthlightRegionalFog::Field> fogVolumeField;
    bool fogVolumeResources(unsigned w,unsigned h){
        if(fogVolume.texture[0])return true;
        if(fogVolume.refused)return false;
        // These DEFAULT render targets have no requested CPU shadow copy.
        // Use the same VA admission as other post targets; a CPU-cache pressure
        // episode must not discard the current volume and select a slower grid.
        if(memoryPressure){const auto budget=NorthlightGeometryMemory::defaultTargetBudget();
            if(!NorthlightGeometryMemory::admits(geometryAdmission(budget),budget))return false;}
        return fogVolume.resources(d,w,h);
    }
    // 0.3.200 (gpu budget): the level the proxy's controller chose (0 = full, always with GpuBudgetMs=0).
    unsigned gpuBudgetLevel=0;
    unsigned cpuBudgetLevel=0; /* task 45: the CPU budget controller's level (ActorShadowCpuMs): 0 = ActorShadowRadius as set */
    NorthlightFogClouds::Wind cloudWind;int64_t cloudQpc=0;float cloudDenseZone=0,cloudLush=1; /* smoothed dense-zone profile at the camera (Duskwood 1), see denseZoneDamp; smoothed lush zone at the camera (forest/grass 1), see derive */
    bool ensureCloudNoise(){
        if(regionalFogGpu.noise())return true;
        const auto& noise=fogCloudNoise();
        const auto* data=noise.ready.load(std::memory_order_acquire)?noise.data.data():nullptr;
        return regionalFogGpu.ensureNoise(d,data,NorthlightDiagnostics::enabled(),[&](bool report){
            if(!memoryPressure)return true;
            // Budget for the larger fallback format and three driver/managed copies.
            constexpr unsigned n=NorthlightFogClouds::N;
            const auto budget=NorthlightGeometryMemory::dynamicBudget(uint64_t(n)*n*n*4);
            bool exact=true;
            const auto memory=geometryAdmission(budget,&exact);
            if(NorthlightGeometryMemory::admits(memory,budget))return true;
            if(report)logGeometryMemory("cloud-noise-deferred",memory,0,exact,&budget);
            return false;
        },[](const char* format,auto... args){logf(format,args...);});
    }
    unsigned localDirectCount=0;float localDirectNearest=0;
    // 0.3.197: soft cap, incumbent bias and fades for the local lights (task 13). The clock is the selection's own: its gap and reset policy mirror useHistory.
    NorthlightEquipmentLights::Tracker equipmentLights;std::unordered_map<IDirect3DVertexShader9*,unsigned> equipmentShaders,nativePointShaders;
    std::unordered_set<IDirect3DVertexShader9*> nativeLightingShaders;
    std::unordered_map<IDirect3DVertexShader9*,bool> nativePointOwners;
    std::unordered_map<IDirect3DPixelShader9*,bool> nativePointPixelOwners;
    std::unique_ptr<NorthlightNativeLocalLighting> nativeDirectRenderer;
    void* nativeSamplerContext=nullptr;bool(*nativeSamplerReader)(void*,DWORD,unsigned,DWORD*)=nullptr;
    NorthlightNativeLights::Snapshot nativeDirectScene;unsigned nativeDirectFrame=~0u;
    NorthlightNativePointSubmit nativePointSubmit;
    bool nativeExtraRequested=false,nativeExtraOwners=false;
    struct NativeDirectBinding {
        const NorthlightNativeLights::Point* native=nullptr;const NorthlightLocalLights::Light* authored=nullptr;NorthlightNativeLocalDraw light;
        NorthlightNativeLights::Point observed{};bool drawWitness=false;
        const NorthlightLocalLights::Light* selected=nullptr;IDirect3DCubeTexture9* const* cube=nullptr;
        float selectedPosition[3]={};
        const bool *ready=nullptr,*usable=nullptr;const float* gain=nullptr;const uint32_t* updated=nullptr;const std::string* map=nullptr;uint64_t sourceId=0;float ownerGain=0;
    };
    std::array<NativeDirectBinding,NorthlightPointShadow::MaxLights+NorthlightLocalLightSelection::Slots> nativeDirectBindings{};unsigned nativeDirectBindingCount=0;
    NorthlightNativeDirect::BankMemo nativeDirectBankMemo;unsigned nativeDirectBankHits=0,nativeDirectBankMisses=0;
    std::string nativeDirectMap;float nativeDirectView[16]{};uint64_t nativeDirectStamp=0,nativeCubeRevision=0,nativeDirectCubeRevision=~uint64_t(0);unsigned nativeDirectSceneCount=0;const void *nativeDirectSnapshot=nullptr,*nativeDirectWorld=nullptr;
    unsigned nativeDirectMatched=0,nativeDirectMultiple=0,nativeDirectMissingCube=0,nativeDirectSlots=0,nativeDirectTraces=0;
    unsigned nativeObservedOwnedSlots=0;
    std::unordered_set<IDirect3DVertexShader9*> nativeTerrainPointShaders;
    std::array<unsigned,7> nativeDirectMissReasons{};
    std::array<unsigned,5> nativeDirectSlotCounts{},nativeDirectMatchCounts{};unsigned nativeDirectPreparedBypass=0,nativeDirectPrivateBypass=0;
    std::shared_ptr<const NorthlightNativeFixtures::Catalog> nativeFixtures;
    NorthlightLightAttachments::Tracker lightAttachments;
    std::vector<NorthlightLocalLights::Light> attachedLocalLights;
    NorthlightParticleLights::Frame particleLights;std::string particleMap;
    std::string equipmentMap;NorthlightEquipmentLights::CaptureBudget equipmentCapture;
    NorthlightLocalLightTrace::InputHash equipmentBankAudit,equipmentDrawAudit;
    NorthlightLocalLightSelection::Tracker localLightTracker;std::string localLightMap;V localLightCamera;int64_t localLightQpc=0;double localSelectUsSum=0,localSelectUsMax=0;unsigned localSelectFrames=0;
    unsigned parityFrame=0;
    DWORD visualTime()const{
        return NorthlightFrameParityClock::enabled()?NorthlightFrameParityClock::milliseconds(parityFrame):GetTickCount();
    }
    float visualAnimationSeconds(DWORD processNow)const{
        const DWORD elapsed=NorthlightFrameParityClock::enabled()
            ?NorthlightFrameParityClock::milliseconds(parityFrame):DWORD(processNow-animationEpoch);
        return float(elapsed)*.001f;
    }
    NorthlightLocalLightScissor::View localScissorView;
    unsigned localOffscreenSum=0,localArranged=0;double localArrangeBefore=0,localArrangeAfter=0;
    unsigned localSourceTraces=0;NorthlightLocalLightTrace::Frame sourceTrace;
    void reportLocalSources(const NorthlightLocalLightTrace::Frame& trace,unsigned capture,unsigned rendererFrame,float directGain,float sunlitCut,float fogGain,UINT w,UINT h,float nearZ,float farZ,float minZ,float maxZ,bool waterReady,int debug){
        if(!trace.enabled)return;
        logf("LOCAL_SOURCE frame=%u frameDomain=world monotonicNs=%llu capture=%u rows=%u omitted=%u nativeTracked=%u nativePublished=%u dynamicPublished=%u nativeSuppressed=%u nativeDuplicateCounter=%u viewKnown=%u directGain=%.6f sunlitCut=%.6f fogGain=%.6f combine=%s upstreamParticleClusters=unmeasured pixelContribution=unmeasured rendererFrame=%u particleProbes=%u particleDraws=%u particleAccepted=%u particlePoints=%u particlePublished=%u selected=%u fullSize=%u,%u depthRange=%.9g,%.9g sceneDepthViewport=%.9g,%.9g camera=%.6f,%.6f,%.6f projection=%.9g,%.9g,%.9g fogEnabled=%u fogField=%u waterReady=%u debug=%d",
            frames,(unsigned long long)NorthlightStream::nowNs(),capture,trace.count,trace.omitted,equipmentLights.count,equipmentLights.outputCount,particleLights.outputCount,equipmentLights.suppressedCount,equipmentLights.duplicates,unsigned(localScissorView.valid),directGain,sunlitCut,fogGain,NorthlightLocalLightTrace::name(trace.combine),rendererFrame,particleLights.probes,particleLights.draws,particleLights.accepted,particleLights.points,particleLights.immediateCount,localDirectCount,w,h,nearZ,farZ,minZ,maxZ,context.camera[0],context.camera[1],context.camera[2],projection[0],projection[1],projection[2],unsigned(effects.fog),unsigned(bool(regionalFog.field())),unsigned(waterReady),debug);
        for(unsigned i=0;i<trace.count;++i){const auto& r=trace.rows[i];
            logf("LOCAL_SOURCE draw frame=%u sourceId=%llu publishedXYZ=%.6f,%.6f,%.6f publishedRGB=%.6f,%.6f,%.6f range=%.6f,%.6f selected=%u slot=%u selectedXYZR=%.6f,%.6f,%.6f,%.6f selectedRGB=%.6f,%.6f,%.6f sigma=%.9g direct=%s directHRESULT=%08x directRect=%ld,%ld,%ld,%ld fog=%s fogHRESULT=%08x fogRect=%ld,%ld,%ld,%ld",
                frames,(unsigned long long)r.published.sourceId,r.published.position[0],r.published.position[1],r.published.position[2],r.published.diffuse[0],r.published.diffuse[1],r.published.diffuse[2],r.published.attenuationStart,r.published.attenuationEnd,unsigned(r.slot!=~0u),r.slot,
                r.position[0],r.position[1],r.position[2],r.position[3],r.color[0],r.color[1],r.color[2],r.fog[0],
                NorthlightLocalLightTrace::name(r.direct),unsigned(r.directResult),r.directRect.left,r.directRect.top,r.directRect.right,r.directRect.bottom,
                NorthlightLocalLightTrace::name(r.volume),unsigned(r.fogResult),r.fogRect.left,r.fogRect.top,r.fogRect.right,r.fogRect.bottom);
            for(bool volume:{false,true}){const auto& state=volume?r.fogState:r.directState;
                logf("LOCAL_SOURCE state frame=%u sourceId=%llu pass=%s reason=%s known=%u viewport=%u,%u,%u,%u viewportDepth=%.9g,%.9g scissorEnabled=%u scissor=%ld,%ld,%ld,%ld",
                    frames,(unsigned long long)r.published.sourceId,volume?"fog":"direct",NorthlightLocalLightTrace::reason(r,volume),state.known,state.viewport[0],state.viewport[1],state.viewport[2],state.viewport[3],state.minZ,state.maxZ,state.scissorEnabled,state.scissor.left,state.scissor.top,state.scissor.right,state.scissor.bottom);}
        }
    }
    IDirect3DVertexBuffer9* vertices=nullptr; // non-owning aliases of page 0 of the active table; also the "mesh present" flags
    IDirect3DIndexBuffer9* indices=nullptr;
    // 0.3.216 (issue #66): ONE store of page buffers (mesh_buffer_store.h) instead of two full slots. meshStore does all the
    // bookkeeping (content keys, active table, claims, idle state); meshBuffers[id] holds the D3D resources of store entry
    // `id` (capacities live in the entry). The active table is drawn; a staging build reuses buffers whose key matches a new
    // page, uploads only the misses into idle/new buffers and switches the table atomically at commit. An ACTIVE buffer is
    // never created, written or released (acquireMeshPage/perform check meshStore.writable(id), trims take idle ones only).
    using MeshBuffers=NorthlightWorldMeshPool::Buffers;
    NorthlightMeshBufferStore::Store& meshStore=gpuScene->meshPages.store;
    std::vector<MeshBuffers>& meshBuffers=gpuScene->meshPages.buffers;
    NorthlightMeshPoolTrim::State& meshPoolTrim=gpuScene->meshPages.trimState;
    void syncMeshBuffers(){gpuScene->meshPages.sync();}
    // 0.3.216 (issue #66): keys of the pages the ACTIVE table holds, published for the geometry worker, which frees the CPU data of
    // every page of a new plan whose key is in it (the GPU has it already). Keys only; guarded by `mutex`. Replaced at each mesh
    // commit, cleared with the pool and whenever an elided page missed at staging (the next plan then elides nothing).
    std::shared_ptr<const NorthlightMeshPageKey::KeySet> residentPages;std::string residentMap;unsigned elidedFallbacks=0;
    void publishResidentPages(const std::string& map){
        std::shared_ptr<const NorthlightMeshPageKey::KeySet> keys;
        if(!map.empty()&&!meshStore.table().empty()){try{keys=std::make_shared<const NorthlightMeshPageKey::KeySet>(meshStore.activeKeys([&](uint32_t id){return id<meshBuffers.size()&&meshBuffers[id].vb&&meshBuffers[id].ib;}));}catch(...){keys.reset();}}
        std::lock_guard<std::mutex> lock(mutex);residentPages=std::move(keys);residentMap=residentPages?map:std::string();
    }
    DWORD memoryPressureLogAt=0,memoryPressureUntil=0;
    static UINT roundBuffer(uint64_t bytes,uint64_t stepBytes){return UINT(NorthlightGeometryMemory::roundUp(bytes,stepBytes));}
    // Growth admission for any large GPU buffer. A refused growth is remembered
    // for one second so pressure does not add an address-space walk per frame.
    bool admitsGrowth(const char* stage,uint64_t capacityBytes,const NorthlightGeometryMemory::Sample* cachedSample=nullptr,bool pressureReplay=false){
        DWORD now=GetTickCount();
        if(memoryPressureUntil&&now<memoryPressureUntil)return false;
        const auto budget=NorthlightGeometryMemory::replayBudget(NorthlightGeometryMemory::dynamicBudget(capacityBytes),pressureReplay&&pressureReplayAllocation());
        bool exact=true;
        auto memory=cachedSample?*cachedSample:geometryAdmission(budget,&exact);
        if(NorthlightGeometryMemory::admits(memory,budget))return true;
        memoryPressureUntil=now+1000;NorthlightMemMap::deferredRequest.store(true,std::memory_order_relaxed); /* task 42: MEMMAP memory-deferred request */
        if(!memoryPressureLogAt||now-memoryPressureLogAt>=60000){memoryPressureLogAt=now;logGeometryMemory(stage,memory,0,exact&&!cachedSample,&budget);}
        return false;
    }
    bool acquireMeshPage(uint32_t id,bool vertex,UINT bytes,D3DPOOL pool=D3DPOOL_MANAGED){
        return gpuScene->meshPages.acquire(d,id,vertex,bytes,pool,[&]{return pressureReplayAllocation();});
    }
    void releaseMeshPool(){gpuScene->meshPages.clear();publishResidentPages(std::string());vertices=nullptr;indices=nullptr;}
    // Frees idle buffers (returns bytes): those not referenced by the active table and not claimed by a staging.
    // Never while a staged upload is pending; the active table (and vertices/indices, which alias its page 0) stays.
    // Order (mesh_buffer_store.h trimIdle): invalid-key buffers first (partial content from a cancelled staging), then
    // the valid ones idle for the longest. maxBytes: stop once this many bytes were released (always at least one
    // buffer when any is idle); unbounded for the memory guard.
    size_t trimMeshPools(uint64_t maxBytes=UINT64_MAX){
        return pendingMesh?0:gpuScene->meshPages.trim(maxBytes);
    }
    // After a staged commit (pendingMesh already cleared): releases idle capacity the next build is unlikely to need,
    // with hysteresis (mesh_pool_trim.h): target = windowMax(mesh) + windowMax(uploaded this commit). Runs once per
    // commit, O(buffers); the active table's buffers never leave.
    void trimMeshPoolAfterCommit(uint64_t stagedCapacity){
        if(pendingMesh||meshStore.table().empty())return;
        const uint64_t pool=meshStore.poolBytes(),mesh=meshStore.activeBytes();
        const uint64_t request=meshPoolTrim.onCommit(pool,mesh,stagedCapacity); // at most MaxReleaseBytes per commit
        if(!request)return;
        const size_t freed=trimMeshPools(request);
        if(freed)logf("WORLD mesh pool trimmed freedMiB=%.1f poolMiB=%.1f meshMiB=%.1f",double(freed)/1048576,double(pool-freed)/1048576,double(mesh)/1048576);
    }
    std::vector<IDirect3DTexture9*> materials;
    NorthlightStreaming::DeferredRelease<IDirect3DTexture9> retiredMaterials;
    NorthlightShadowAlphaTexture::Format shadowTextureFormat;
    size_t uploadedTextureBytes=0;
    using MaterialPixels=std::vector<NorthlightWorldMesh::MaterialUpload>;
    std::shared_ptr<MaterialPixels> uploadedMaterialPixels;
    static constexpr size_t MaterialReuseBytes=4u<<20;
    size_t materialReuseBytes=0;
    IDirect3DTexture9* reusableMaterial(const NorthlightWorldMesh::MaterialUpload& material,size_t materialIndex)const{
        // MANAGED cutouts survive Reset. Exact plan/index identity avoids both
        // a BGRA duplicate and replay-time texture staging allocations.
        if(resetReplayScoped&&active&&active->bvh==resetReplaySource.lock()&&active->meshPlan==resetReplayPlan.lock()&&materialIndex<materials.size()&&materials[materialIndex]){
            auto payload=active->meshPlan->payload.share();D3DSURFACE_DESC desc{};
            auto* texture=materials[materialIndex];
            if(payload&&materialIndex<payload->materials.size()&& &payload->materials[materialIndex]==&material&&texture->GetLevelCount()==1&&
               SUCCEEDED(texture->GetLevelDesc(0,&desc))&&desc.Pool==D3DPOOL_MANAGED&&desc.Format==shadowTextureFormat.value&&desc.Width==material.width&&desc.Height==material.height)return texture;
        }
        if(!uploadedMaterialPixels)return nullptr;
        for(size_t i=0;i<uploadedMaterialPixels->size()&&i<materials.size();++i){
            const auto& old=(*uploadedMaterialPixels)[i];
            if(!material.spooled&&!old.pixels().empty()&&old.alphaOnly==material.alphaOnly&&old.textureIdentity==material.textureIdentity&&
               old.width==material.width&&old.height==material.height&&old.pixels()==material.pixels())return materials[i];
        }
        return nullptr;
    }
    using Batch=NorthlightWorldMeshPages::Batch;
    bool bindMeshPage(const Batch& batch,UINT& boundPage){
        if(boundPage==batch.page)return true;
        const uint32_t id=meshStore.bufferOf(batch.page);
        if(id>=meshBuffers.size())return false; // also NorthlightMeshBufferStore::None
        auto& page=meshBuffers[id];
        const bool compact=page.vbPool==D3DPOOL_DEFAULT;
        if(!shadowPrograms.bindMesh(d,page.vb,page.ib,compact,compact?sizeof(StaticShadow::Vertex):sizeof(NorthlightGI::WorldVertex)))return false;
        boundPage=batch.page;return true;
    }
    std::vector<Batch> batches,pointSubBatches;
    std::vector<NorthlightWorldMeshPages::PointRange> pointSubRanges;
    std::shared_ptr<const NorthlightWorldMeshPages::PointTree> pointTree;
    size_t pointMeshBatchCount()const{return batches.size()+pointSubBatches.size();}
    const Batch& pointMeshBatch(size_t i)const{return i<batches.size()?batches[i]:pointSubBatches[i-batches.size()];}
    // 0.3.175 (S1a): indices of this mesh generation's terrain batches, in batch order; rebuilt only
    // at the mesh commit (rebuildTerrainLists), valid while terrainBatchListGeneration==meshGeneration.
    // 0.3.175 (S2): shadowTerrainList, the terrain batches outside this generation's fixed chunks
    // (the directional terrain selection then tests only the live chunks), for terrainListFixed.
    std::vector<uint32_t> pointStaticList;
    NorthlightDirectionalCacheMesh directionalCacheMesh;
    std::vector<uint32_t> terrainBatchList,shadowTerrainList;uint64_t terrainBatchListGeneration=UINT64_MAX;size_t terrainBatchListSize=0;const void* terrainListFixed=nullptr;
    void rebuildTerrainLists(){
        directionalCacheMesh.invalidate();
        terrainBatchListGeneration=UINT64_MAX;terrainBatchList.clear();shadowTerrainList.clear();pointStaticList.clear();
        if(batches.size()>UINT32_MAX)return;
        const auto& fixed=fixedTerrainChunks();
        try{
            for(size_t i=0;i<batches.size();++i){
                const auto& batch=batches[i];
                if(!batch.terrain){pointStaticList.push_back(uint32_t(i));continue;}
                terrainBatchList.push_back(uint32_t(i));
                if(!fixed.count({batch.chunkX,batch.chunkY}))shadowTerrainList.push_back(uint32_t(i));
            }
        }catch(...){terrainBatchList.clear();shadowTerrainList.clear();pointStaticList.clear();return;}
        terrainBatchListSize=batches.size();terrainListFixed=&fixed;terrainBatchListGeneration=meshGeneration;
        directionalCacheMesh.publish(batches,meshGeneration,fixed,fixedTerrainBits);
    }
    // CELESTIAL terrain mask line (rate-limited): the last redraw, and per body (sun, moon) redraws/reuses since the line.
    struct CelestialMaskStats {size_t candidates=0;unsigned accepted=0,runs=0,redraws[2]={},reuses[2]={};uint64_t triangles=0;bool listed=false;double ms=0,peakMs=0;DWORD lastLog=0;} celestialMask;
    NorthlightTerrainCandidates::Scratch<> directionalTerrainScratch; // bounded capacity only; eligibility is render-local
    std::shared_ptr<const std::set<std::pair<int,int>>> fixedTerrain; /* immutable, shared with the worker-prepared commit */
    const std::set<std::pair<int,int>>& fixedTerrainChunks()const{static const std::set<std::pair<int,int>> none;return fixedTerrain?*fixedTerrain:none;}
    NorthlightShadowTerrain::FixedChunkBits fixedTerrainBits; /* 0.3.176 (U1c): beside fixedTerrain, filled at the commit */
    std::string uploadedMap;
    NorthlightLiveTerrainIndices liveTerrainIndices;
    NorthlightDynamicRing::FrameFence frameFence; /* 0.3.192 (DXVK3): ONE EVENT query per frame for every upload ring, issued in endFrame() */
    NorthlightDynamicRing::FrameFence& fence(){frameFence.device=d;return frameFence;}
    using PendingMesh=NorthlightWorldMeshPublication::Pending<PreparedCommit>;
    std::unique_ptr<PendingMesh> pendingMesh;
    NorthlightWorldStreaming::Retry meshRetry;
    std::weak_ptr<NorthlightGI::BVH> payloadRebuildAsked;DWORD payloadRebuildTick=0;unsigned payloadRebuilds=0;
    unsigned streamingReports=0,preparedOwnerCommits=0;
    std::atomic<unsigned> generationDeferrals{0}; /* worker: canAdmit()==false events */
    /* 0.3.138: worker waits for the CPU reaper before a generation deferral. */
    std::atomic<unsigned> generationWaits{0},generationWaitPeakUs{0};
    size_t ownerChangesPeak=0;unsigned replayCreatedPeak=0;uint64_t replayTimeDeferred=0,replayGrowths=0,replayFallbacks=0,replayBudgetOverrides=0; /* overrides: meshes created past the time budget to avoid bulk growth */std::vector<size_t> replayTimeDeferredIndices; /* growth/fallback: cumulative, all frames */
    double streamingCpuPeakMs=0;unsigned streamingBudgetOverruns=0;
    NorthlightStreaming::PhaseProfile streamingPhases;
    NorthlightStreaming::RetirementBacklog retirementBacklog; /* render thread only */
    NorthlightVertexDeclarations::Cache declarationCache;
    NorthlightReplayBounds::Cache replayBoundsCache;
    NorthlightReplayMetadata::Cache<NorthlightReplayBounds::Prepared,IDirect3DVertexShader9,IDirect3DVertexDeclaration9> replayBoundsMetadata;
    std::unordered_set<IDirect3DVertexShader9*> terrainShaders;
    // Live terrain has float positions and zero UVs; its shadow shaders never read a normal.
    // Reuse the existing lossless 20-byte caster format in the same bounded arena.
    NorthlightLiveTerrainGPU::Cache<NorthlightTerrainCapture::MeshSnapshot,StaticShadow::Vertex>& liveTerrainGPU=gpuScene->terrain;
    size_t liveTerrainIndexCount=0,liveDirectionalIndexCount=0; /* 0.3.176 (U1a): the live IB holds the point lists, then the directional */
    uint64_t liveTerrainGeneration=0,liveTerrainRevision=0;
    // Ordered immutable captures preserve exact point-light batch offsets.
    // Vertex residency is per capture; the compact active index stream keeps
    // the directional pass at one draw even when residency is fragmented.
    using TerrainSnapshot = std::shared_ptr<const NorthlightTerrainCapture::MeshSnapshot>;
    std::vector<TerrainSnapshot> frameTerrain, uploadedTerrain;
    size_t frameTerrainVertices=0,frameTerrainIndices=0,terrainUploadBytes=0;
    bool terrainUploadReused=false;
    NorthlightStateBlockPool stateBlocks;
    unsigned terrainAttempts=0,terrainSnapshots=0,terrainFailures=0;
    NorthlightTerrainCapture::FrameCache& terrainBoundsCache=gpuScene->terrainSources;
    NorthlightTerrainCandidates::ChunkSet liveTerrainChunks;
    UINT width=0,height=0,vertexCount=0;
    struct Replay {
        IDirect3DVertexShader9* shader=nullptr,*originalShader=nullptr;
        NorthlightReplayBounds::Bounds pointBounds,materialViewBounds;
        NorthlightReplayBounds::WorkInfo boundsWork; // scheduling hint; cleared every frame, never an identity proof
        std::shared_ptr<const NorthlightReplayBounds::Prepared> boundsPrepared;
        IDirect3DVertexDeclaration9* decl=nullptr;
        IDirect3DVertexBuffer9* stream[4]={};
        UINT offset[4]={},stride[4]={};
        IDirect3DIndexBuffer9* index=nullptr;
        IDirect3DBaseTexture9* texture=nullptr;
        using FloatBank=std::array<float,1024>;
        static constexpr FloatBank emptyConstants{};
        std::shared_ptr<FloatBank> constantBank;
        BOOL boolStorage[16]={};int intStorage[64]={};
        const float* constants=emptyConstants.data();const BOOL* bools=boolStorage;const int* ints=intStorage;
        NorthlightConstantEpoch::Stamp constantStamp;
        Replay()=default;Replay(const Replay&)=delete;Replay& operator=(const Replay&)=delete;
        float* writableConstants() noexcept{
            if(constantBank.use_count()!=1)try{
                auto next=std::make_shared<FloatBank>();
                if(constantBank)std::memcpy(next->data(),constantBank->data(),sizeof(FloatBank));
                constantBank=std::move(next);
            }catch(const std::bad_alloc&){return nullptr;}
            constantStamp={};constants=constantBank->data();return constantBank->data();
        }
        void shareConstants(const Replay& previous) noexcept{
            constantBank=previous.constantBank;constants=previous.constants;
        }
        void dropConstantBank() noexcept{constantBank.reset();constants=emptyConstants.data();constantStamp={};}
        void resetConstantCapture() noexcept{
            if(constantBank.use_count()!=1)dropConstantBank();
            bools=boolStorage;ints=intStorage;constantStamp={};
        }
        float cutoff=-1;unsigned alphaMode=0;
        NorthlightShaderConstants::Usage constantUsage;
        unsigned constantGroup=0; // exact consecutive banks, rebuilt from this frame's captures
        unsigned char directionalPaletteEnd=0;
        unsigned projectionKind=2;DWORD addressU=D3DTADDRESS_WRAP,addressV=D3DTADDRESS_WRAP;
        NorthlightDrawSnapshot::Mesh snapshot; // owned copy: DYNAMIC/UP draws and misses the cache could not store
        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> shared; // immutable snapshot: tracked generations or legacy static cache
        const NorthlightDrawSnapshot::Mesh& mesh()const{return shared?*shared:snapshot;}
        bool gpuCached=false,shadowSkinned=false,shadowSelected=true,shadowSmall=false;int fateSlot=-1;unsigned char fateClass=0;float fateDistance=0; /* shadow fate diagnostics */
        bool bulkSingle=false; /* 0.3.211: uploadReplay's layout verdict (single-stream bulk placement) for the absolute pass */
        bool selfShadow=false; /* a draw of the selection's self body (set by the selection, reset at capture): the side tracker's self, ring 0 of the bounds job's near list */bool boneKnown=false;float bone=NAN; /* 0.3.176 (S2): this frame's selection tested its rigid bone (reset at capture) */
        float captureRoot[3]={};bool captureRootValid=false; /* 0.3.211 (task 31): the palette root computed once at capture (capture admission); passive for the selection */
        float anchor[3]={};bool anchorValid=false; /* an injected rigid prop (rigidMemoryInject): the origin of its remembered bone, in world space; it has no palette root */
        // The per-instance position known without bounds: the palette root, or the rigid prop's anchor. Null: neither.
        const float* position()const{return captureRootValid?captureRoot:anchorValid?anchor:nullptr;}
        // 0.3.177 (r83): filled at capture for selected skinned draws, read by prepareRecord (the stable
        // selection's prepare, possibly on the prepare worker): the program (null: none) and a copy of
        // the declaration (the declaration cache may reuse its slot within the frame). 0.3.179 (T1): the
        // program is the capture metadata's, not a handle; a program retired while a frame may point at it
        // lives in retiredPrograms until that frame is recycled (0.3.183: after a watchdog, pinned in
        // prepareQuarantinedPrograms until the worker has settled).
        const NorthlightActorDeformation::Program* program=nullptr;
        std::uint64_t prepareStamp=0; /* 0.3.183: the prepare epoch at capture (preparePublish); 0: pooled, rejected or injected */
        // 0.3.179 (T2): the frame's declaration copy (prepareDecls); null: the per-record copy below (the
        // arena was full) or not declared.
        const NorthlightActorPrepare::DeclCopy* declCopy=nullptr;
        std::vector<D3DVERTEXELEMENT9> elements;bool declared=false;
        const D3DVERTEXELEMENT9* declarationElements()const{return declCopy?declCopy->elements:elements.data();}
        UINT declarationCount()const{return declCopy?declCopy->count:UINT(elements.size());}
        size_t retainedBytes()const{return snapshot.capacityBytes()+elements.capacity()*sizeof(D3DVERTEXELEMENT9)+(constantBank?sizeof(FloatBank):0);}
        uint32_t staticProofMask=0,staticM2Mask=0;uint64_t staticProofRevision=0;std::string staticProofModel;const std::string* staticM2Model=nullptr; /* staticM2Model: the M2 proof's model key, owned by its StaticM2Dedup entry (valid for the frame) */ /* staticM2Mask: slots whose cache holds this rigid M2 (bounds in staticProofLow/High) */
        V staticProofLow,staticProofHigh;
        D3DPRIMITIVETYPE type;INT base;UINT min,vertices,start,count;bool indexed;
        void releaseResources(bool retainSnapshot=false){if(!retainSnapshot)dropConstantBank();resetConstantCapture();program=nullptr;declCopy=nullptr;prepareStamp=0;declared=false;elements.clear();drop(shader);drop(originalShader);pointBounds={};boundsWork={};boundsPrepared.reset();drop(decl);drop(index);drop(texture);for(auto& s:stream)drop(s);shared.reset();if(!retainSnapshot)snapshot=NorthlightDrawSnapshot::Mesh{};}
        ~Replay(){releaseResources();}
    };
    std::vector<std::unique_ptr<Replay>> replays,freeReplays,heldShadowReplays;
    std::vector<uint32_t> directionalReplayCandidates,directionalPaletteOrder;
    NorthlightPointShadow::ReplayIndexBatches shadowReplayIndices;
    /* Sampled-frame instancing audit: geometry identity of each drawn replay in one pass. */
    struct ReplayMeshKey {const void* index;const void* stream;const void* decl;UINT offset,start,count;INT base;unsigned group;
        bool operator<(const ReplayMeshKey& o)const{return std::tie(index,stream,decl,offset,start,count,base,group)<std::tie(o.index,o.stream,o.decl,o.offset,o.start,o.count,o.base,o.group);}
        bool sameMesh(const ReplayMeshKey& o)const{return index==o.index&&stream==o.stream&&decl==o.decl&&offset==o.offset&&start==o.start&&count==o.count&&base==o.base;}};
    std::vector<ReplayMeshKey> replayMeshKeys;
    std::vector<NorthlightReplayShadowPolicy::Candidate> shadowCandidates;
    unsigned minSkinnedShadowTriangles=0,captureBudgetMiB=32,actorShadowBudgetMiB=0;bool shadowFateDiagnostics=false;
    NorthlightActorDeformation::RigidBoneCache smallShadowBones{256}; // Render-thread owner; preparation has its own cache.
    NorthlightQuality::Settings quality; /* northlight-quality.ini; Settings{} is the full-quality default */
    std::unique_ptr<NorthlightWeatherShelter::Worker> weatherShelter;
    bool weatherShelterUnavailable=false;
    bool shadowSelectionDone=false;
    unsigned replayTriangleBins[6]={},skinnedTriangleBins[6]={},acceptedTriangleBins[6]={},acceptedSkinnedTriangleBins[6]={};
    unsigned smallShadowEarly=0,smallShadowGI=0;
    size_t pooledSnapshotBytes=0; // Snapshot, overflow declarations and retained float banks.
    NorthlightDrawSnapshot::Frame replaySnapshots;
    // Snapshot lookup stays on Frame's Prefetch default. Profiling must not
    // rotate production algorithms or run key microbenchmarks on this thread.
    // The next pooled Replay's lines that the capture touches after acquireReplay() (Prefetch mode).
    void prefetchReplay(const Replay& r){
        using NorthlightDrawSnapshot::prefetch;
        prefetch(&r.shader);prefetch(&r.originalShader);prefetch(&r.pointBounds);prefetch(&r.snapshot);prefetch(&r.shared);
        prefetch(&r.shadowSkinned);prefetch(&r.fateDistance);prefetch(&r.projectionKind);
    }
    std::unique_ptr<NorthlightActorPrepare::Caches> prepareCaches=std::make_unique<NorthlightActorPrepare::Caches>(); /* sampled vertex inputs and rigid bones (0.3.177: one owner at a time) */
    // 0.3.177 (r83): the prepare worker's state (world_shadow_experiment.inl; the worker itself is declared
    // after retiredPrograms). After a watchdog the abandoned worker keeps its caches and (0.3.183) only the
    // abandoned frame's replays (quarantined, never recycled), its arena and the programs pinned at the
    // abandon, all until it settles.
    std::unique_ptr<NorthlightActorPrepare::Caches> prepareAbandonedCaches;
    // 0.3.179 (T2): this frame's declaration copies (null: allocation failed, per-record copies), and the
    // arena an abandoned worker's frame may still read (0.3.183: moved there at the abandon, at most one).
    std::unique_ptr<NorthlightActorPrepare::DeclArena> prepareDecls=std::make_unique<NorthlightActorPrepare::DeclArena>();
    std::vector<std::unique_ptr<NorthlightActorPrepare::DeclArena>> prepareQuarantinedDecls;
    std::vector<std::unique_ptr<Replay>> prepareQuarantine;
    std::vector<std::shared_ptr<const NorthlightActorDeformation::Program>> prepareQuarantinedPrograms; /* 0.3.183: every program pinned at the abandon */
    NorthlightShadowFate::Tracker shadowFate;unsigned otherBlendRejected=0,otherFadeRejected=0,otherBudgetRejected=0,otherProjectionRejected=0;
    NorthlightActorShadowSelection::History actorShadowHistory;
    std::vector<NorthlightActorShadowSelection::Draw> actorShadowDraws;
    NorthlightActorShadowSelection::Scratch actorShadowScratch;size_t actorShadowToggles=0;unsigned actorShadowFrames=0;
    float actorShadowOrigin[3]={};bool actorShadowOriginValid=false,actorShadowOver=false;unsigned actorShadowTransitions=0,actorShadowCapBinds=0;double actorShadowPrepareMs=0,actorShadowChooseMs=0; /* last frame's shadow pivot ranks the actor quota */
    size_t actorShadowRadiusToggles=0,actorShadowRadiusFlicker=0,actorShadowRadiusRekeyed=0,actorShadowRadiusRekeyFlips=0,actorShadowAttachToFree=0,actorShadowFreeToAttach=0,actorShadowAttachToFreeBeyond=0,actorShadowFreeToAttachBeyond=0; /* ActorShadowRadius per log window: inside<->outside changes, changes within RadiusDwell of the last one, identities kept across a key change */
    NorthlightGpuScene::NativeClient& replayGpuCache=gpuScene->shadows;
    // 0.3.177 (r83): shared handles: a replay captured with a program keeps it for the frame even if
    // registerShader() erases the entry (the prepare worker reads it without the map).
    std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const NorthlightActorDeformation::Program>> actorPrograms;
    std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const NorthlightActorDeformation::Program>> actorUVPrograms;
    std::shared_ptr<NorthlightActorGeometry::ActorJob> actorJob=std::make_shared<NorthlightActorGeometry::ActorJob>();
    std::shared_ptr<const NorthlightActorGeometry::ActorJob> actorJobComplete;
    uint64_t actorJobSerial_=0;DWORD lastActorCapture=0;bool actorCaptureDecided=false,actorCaptureDue=false;std::string actorSceneMap_;
    unsigned actorVerticesEvaluated=0,actorDraws=0,actorSkippedAlpha=0,snapshotRejects=0;

    LONGLONG terrainCaptureTicks=0,replayCaptureTicks=0;
    LARGE_INTEGER captureFrequency={};
    unsigned terrainCaptureCalls=0,terrainUPCalls=0,replayCaptureCalls=0,unknownCaptureCalls=0,captureSourceHandoffs=0;
    NorthlightAdtBounds::Coverage nativeAdtCoverage;
#ifdef NORTHLIGHT_REPLAY_STAND
    unsigned captureProjectionGets=0,captureProjectionLoans=0;
    unsigned terrainCameraGets=0,terrainCameraBorrows=0;
    unsigned capturePacketReads=0,capturePacketLoans=0,capturePacketReadRegisters=0,capturePacketLoanRegisters=0;
    unsigned captureWmoProjectionBorrows=0,captureWmoPacketBorrows=0,captureWmoPacketRegisters=0;
#endif
    NorthlightDetailShadow::Budget detailBudget;
    unsigned detailCandidates=0,detailMaskRejected=0,detailRangeRejected=0,detailBudgetRejected=0,detailBlendRejected=0;
    unsigned rejSmall=0,rejGate=0,rejCut=0,rejBudget=0,rejOther=0; /* task 43: per frame; WORLD CPU capture line */
    std::uint64_t previousCacheHits=0;
    size_t capturedConstantBytes=0,capturedConstantCalls=0;
    // 0.3.180 (C1): the device mirror's constant clock, read in place under the draw's gate.
    NorthlightReplayCaptureConstants::ClockSource<DeviceMirror> constantEpochSource;
    // 0.3.180 (C0): constantEpochStats keeps the self-check (verify()) and pose counters; the epoch and
    // block counters of RenderProfile sample frames go to constantEpochProfile. Both are cleared per frame.
    NorthlightReplayCaptureConstants::Stats constantEpochStats,constantEpochProfile;
    NorthlightReplayCaptureConstants::SelfCheck constantSelfCheckState{0,&constantEpochStats};
    unsigned capturedSM1Draws=0,capturedRelativeDraws=0;
    unsigned skinnedCandidates=0,skinnedBlendRejected=0,skinnedFadeRejected=0,skinnedProjectionRejected=0,skinnedBudgetRejected=0,skinnedSnapshotRejected=0,skinnedAccepted=0;
    size_t captureRejectedBytes=0,acceptedSkinnedBytes=0,acceptedOtherBytes=0;
    bool captureSampled=false;
    void* captureRasterContext=nullptr;bool(*captureRasterReader)(void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*)=nullptr;
    bool captureShortfall=false; /* this capture frame lost a draw to the capture limits (bytes or count) */
    // 0.3.140 cadence of the constant-epoch self-check (replay_capture_constants.h
    // verify(): repairs a pose on mismatch): the renderer's frame%120==0, independent
    // of Diagnostics and RenderProfile. The diagnostic `sample` is a separate, gated flag.
    bool constantSelfCheck=false;
    enum CapturePhase {CaptureState,CaptureProjection,CaptureSnapshot,CaptureConstants,CaptureMaterial,CaptureFinalize,CaptureActor,CapturePhaseCount};
    NorthlightCapturePhases::Stats<CapturePhaseCount> capturePhases;
    NorthlightCapturePhases::Stats<3> actorPhases;
    NorthlightCapturePhases::Stats<2> replayUploadPhases;
    unsigned shaderAlphaCaptured=0,shaderAlphaRejected=0;
    unsigned capturePhaseSerial=0,capturePhaseDraws=0,capturePhaseAccepted=0,alphaStateQueriesSkipped=0;
    unsigned actorPhaseCandidates=0,actorTextureReads=0,replayUploadCalls=0,replayUploadFallbacks=0;
    void clearCaptureDiagnostics(){
        capturePhases.clear();actorPhases.clear();replayUploadPhases.clear();constantEpochStats={};constantEpochProfile={};constantSelfCheckState.serial=0;
        capturePhaseDraws=capturePhaseAccepted=alphaStateQueriesSkipped=shaderAlphaCaptured=shaderAlphaRejected=0;
        actorPhaseCandidates=actorTextureReads=replayUploadCalls=replayUploadFallbacks=0;
    }
    std::unique_ptr<Replay> acquireReplay(){
        if(freeReplays.empty())return std::make_unique<Replay>();
        auto p=std::move(freeReplays.back());freeReplays.pop_back();pooledSnapshotBytes-=p->retainedBytes();return p;
    }
    void recycleReplay(Replay* raw){
        if(prepareHeld(*raw)){try{prepareQuarantine.emplace_back(raw);}catch(...){} return;} /* 0.3.177: an abandoned worker may read it; on failure it leaks (0.3.183: its frame's only) */
        std::unique_ptr<Replay> p(raw);
        const size_t limit=replayPoolLimit(),records=freeReplays.size()+1;
        // Count metadata once and charge retained capacity separately.
        if(records>limit/sizeof(Replay)||pooledSnapshotBytes>limit-records*sizeof(Replay))return;
        const size_t available=limit-records*sizeof(Replay)-pooledSnapshotBytes;
        p->releaseResources(true);
        if(p->retainedBytes()>available)p->snapshot=NorthlightDrawSnapshot::Mesh{};
        if(p->retainedBytes()>available)p->dropConstantBank();
        const size_t capacity=p->retainedBytes();if(capacity>available)return;
        pooledSnapshotBytes+=capacity;
        try{freeReplays.push_back(std::move(p));}catch(...){pooledSnapshotBytes-=capacity;}
    }
    struct ReplayRecycle {WorldRenderer* owner;void operator()(Replay* replay)const{if(replay)owner->recycleReplay(replay);}};

    void logReplayConstantStorage(unsigned rendererFrame)const{
        NorthlightMemoryFootprint::Counter memory;
        size_t packets=0;
        for(const auto* list:{&replays,&heldShadowReplays})for(const auto& p:*list)if(p){
            ++packets;
            memory.allocation(NorthlightMemoryFootprint::Metadata,p->constantBank.get(),sizeof(Replay::FloatBank));
        }
        logf("MODEL replay banks rendererFrame=%u packets=%zu floatBanks=%zu floatBytes=%llu inlineFloatBytes=%zu complete=%u",
            rendererFrame,packets,memory.owners(),(unsigned long long)memory.total(),packets*sizeof(Replay::FloatBank),unsigned(memory.complete()));
    }
    // Immutable for a registered shader. Re-registration replaces the complete
    // record before the pointer can be used for a different shader object.
    struct CaptureShader {
        IDirect3DVertexShader9* replacement=nullptr;
        bool color0W=false,detailDoodad=false;unsigned projectionKind=0;
        NorthlightShaderConstants::Usage usage;
        NorthlightPointShadow::RigidPoseRows pointRigidRows;
        bool skinned=false,sm1=false,fog=false,wmo=false;
        std::shared_ptr<const NorthlightActorDeformation::Program> program; /* 0.3.179 (T1): actorPrograms' object (null: none) */
    };
    std::unordered_map<IDirect3DVertexShader9*,CaptureShader> captureShaders;
    // 0.3.196 (task 12): one-entry lookup cache for repeated draws of the same shader. terrainShaders/captureShaders change only in
    // registerShader (a freed address can be re-registered), which bumps worldShaderGen first, so a stale entry never matches.
    // The cached CaptureShader pointer is a map node (stable across rehash) and is dropped with the generation on any erase.
    std::uint32_t worldShaderGen=0;
    struct ShaderLookup{IDirect3DVertexShader9* shader=nullptr;std::uint32_t gen=~0u;bool terrain=false;const CaptureShader* capture=nullptr;};
    ShaderLookup lastShader;unsigned long long shaderLookupHits=0,shaderLookupMisses=0;
    const ShaderLookup& lookupShader(IDirect3DVertexShader9* shader){
        if(lastShader.shader==shader&&lastShader.gen==worldShaderGen){++shaderLookupHits;return lastShader;}
        ++shaderLookupMisses;auto it=captureShaders.find(shader);
        lastShader.shader=shader;lastShader.gen=worldShaderGen;lastShader.terrain=terrainShaders.count(shader)!=0;lastShader.capture=it==captureShaders.end()?nullptr:&it->second;return lastShader;
    }
    // 0.3.179 (T1): programs erased by registerShader while replays may still point at them; released after
    // endFrame's recycle (0.3.183: an abandoned worker reads the copies pinned in prepareQuarantinedPrograms).
    std::vector<std::shared_ptr<const NorthlightActorDeformation::Program>> retiredPrograms;
    void retireProgram(std::shared_ptr<const NorthlightActorDeformation::Program>&& program){
        if(!program)return;
        try{retiredPrograms.push_back(std::move(program));}catch(...){new std::shared_ptr<const NorthlightActorDeformation::Program>(std::move(program));} /* no memory: leak it, never free it early */
    }
    // 0.3.177 (r83): the prepare worker. Declared after everything its records point at (replays, caches,
    // declaration arenas, quarantine, since 0.3.179 actorPrograms, captureShaders and retiredPrograms, and
    // since 0.3.183 prepareQuarantinedPrograms),
    // so it is destroyed first; ~WorldRenderer also joins it explicitly before anything else.
    NorthlightActorPrepare::Worker<Replay> prepareWorker;
    NorthlightTerrainShadowPrograms terrainShadowPrograms;
    // True only after a render() that drew at least one shadow source and
    // ran the lighting composite; terrain draws of the next frame consult it.
    bool shadowsComposited=false;
    // Shadow cascades are centred on the camera's orbit pivot (the player),
    // not the eye: orbiting the camera then leaves every cascade texel, the
    // near/far hand-over and the cached static maps exactly where they were,
    // so foliage shadows and light shafts do not change with the view. The
    // pivot distance is recovered from the intersection of successive centre
    // rays while the camera rotates; walking keeps the last distance.
    V pivotEye,pivotForward;bool pivotValid=false;float pivotDistance=12.f;unsigned pivotUpdates=0;
    // 0.3.190 (shadow_pivot.h): the distance follows a zoom/collision snap and the captured self. pivotSelfCaptured:
    // the latest selection (render thread, selectShadowReplays) captured the self (radiusSelf==1), at frame pivotSelfFrame.
    NorthlightShadowPivot::State pivotCorrection;bool pivotSelfCaptured=false;unsigned pivotSelfFrame=0;
    // 0.3.159: the cascade frames use (pivot.x, pivot.y, lowest pivot z of the last second), so a
    // jump leaves every pivot-relative shadow band where it was (cascade_anchor.h).
    NorthlightCascadeAnchor::Anchor cascadeAnchor;std::string cascadeAnchorMap;
    void resetShadowPivot(){
        pivotValid=false;pivotDistance=12.f;pivotCorrection.reset();pivotSelfCaptured=false;cascadeAnchor.reset();
    }
    V shadowPivot(){
        const V eye=vec(context.camera);
        V forward=V(context.inverseView[8],context.inverseView[9],context.inverseView[10])*projection[2];
        const float length=std::sqrt(NorthlightGI::dot(forward,forward));if(!(length>1e-6f))return eye;forward=forward*(1.f/length);
        if(!pivotValid){pivotEye=eye;pivotForward=forward;pivotValid=true;}
        else{
            const float b=NorthlightGI::dot(pivotForward,forward);
            // Two rays only 2 degrees apart locate their crossing to within
            // gap/sin(2 deg), i.e. tens of units, and the estimate swung
            // between 12 and 26 u while orbiting. Wait for a 12 degree turn
            // (error under 3 u for a .6 u miss), then filter slowly.
            if(b<.978f){
                const V w0=pivotEye-eye;const float d=NorthlightGI::dot(pivotForward,w0),e=NorthlightGI::dot(forward,w0),denom=1-b*b;
                const float sRef=(b*e-d)/denom,t=(e-b*d)/denom;
                const V onRef=pivotEye+pivotForward*sRef,onNow=eye+forward*t,gap=onRef-onNow;
                if(t>1&&t<80&&NorthlightGI::dot(gap,gap)<.36f){
                    const float step=std::max(-2.f,std::min(2.f,(t-pivotDistance)*.25f));
                    pivotDistance+=step;++pivotUpdates;
                }
                pivotEye=eye;pivotForward=forward;
            }else{
                // Walking moves the pivot point itself; an orbit with the right
                // distance does not. Resetting on eye motion alone discarded the
                // reference every 2 u, before a 12 degree turn could accumulate.
                const V refPivot=pivotEye+pivotForward*pivotDistance,nowPivot=eye+forward*pivotDistance,moved=nowPivot-refPivot;
                if(NorthlightGI::dot(moved,moved)>16){pivotEye=eye;pivotForward=forward;}
            }
        }
        // 0.3.190: zoom, collision snap and the captured self move the distance along the ray (ShadowPivotCorrection=0: untouched).
        const bool selfFresh=pivotSelfCaptured&&frames-pivotSelfFrame<=4&&actorShadowHistory.selfHold()>0;
        pivotDistance=NorthlightShadowPivot::correct(quality.shadowPivotCorrection!=0,pivotCorrection,&eye.x,&forward.x,pivotDistance,selfFresh?actorShadowHistory.selfAt():nullptr);
        return eye+forward*pivotDistance;
    }
    std::unordered_map<IDirect3DVertexShader9*,WmoShaderSignature> wmoShaders;
    unsigned cameraChecks=0,wmoContexts=0,wmoRejects=0;
    static V vec(const float* p){return V(p[0],p[1],p[2]);}
    static bool different(V a,V b,float e){return NorthlightGI::dot(a-b,a-b)>e*e;}
    static V quantize(V p,float step){return V(std::floor(p.x/step)*step,std::floor(p.y/step)*step,std::floor(p.z/step)*step);}
#include "world_point_rendering.inl"
    NorthlightWorldFlashShadows flashShadows;
#include "world_flash_rendering.inl"
#include "world_soft_shadows.inl"
#include "world_near_environment.inl"
    static NorthlightGeometryMemory::Sample geometryMemory() {
        MEMORYSTATUSEX info={};info.dwLength=sizeof info;
        NorthlightGeometryMemory::Sample sample;
        if(!GlobalMemoryStatusEx(&info))return sample;
        sample.available=info.ullAvailVirtual;uint64_t walkedFree=0;
        uintptr_t address=0;MEMORY_BASIC_INFORMATION region={};
        while(VirtualQuery(reinterpret_cast<const void*>(address),&region,sizeof region)==sizeof region){
            if(region.State==MEM_FREE){sample.largest=std::max(sample.largest,uint64_t(region.RegionSize));walkedFree+=uint64_t(region.RegionSize);}
            const uint64_t next=uint64_t(reinterpret_cast<uintptr_t>(region.BaseAddress))+uint64_t(region.RegionSize);
            if(next<=address||next>std::numeric_limits<uintptr_t>::max())break;
            address=uintptr_t(next);
        }
        sample.available=NorthlightGeometryMemory::noteWalk(sample.available,walkedFree);
        sample.valid=sample.largest>0;return sample;
    }
    static NorthlightGeometryMemory::Sample geometryAdmissionFor(NorthlightMemoryAdmission::Probe& probe,NorthlightGeometryMemory::Budget budget,bool* exact=nullptr){
        // Remember only candidate addresses. Every admission gets current
        // aggregate availability and a fresh query of the candidate free block.
        // A stale/small witness falls back to the complete address-space walk.
        const auto result=probe.sample(budget,
            [](uint64_t& available){MEMORYSTATUSEX info={};info.dwLength=sizeof info;
                if(!GlobalMemoryStatusEx(&info))return false;available=NorthlightGeometryMemory::corrected(info.ullAvailVirtual);return true;},
            [](uint64_t address,NorthlightMemoryAdmission::Region& output){MEMORY_BASIC_INFORMATION region={};
                if(VirtualQuery(reinterpret_cast<const void*>(uintptr_t(address)),&region,sizeof region)!=sizeof region)return false;
                output.base=uint64_t(reinterpret_cast<uintptr_t>(region.BaseAddress));output.size=uint64_t(region.RegionSize);output.free=region.State==MEM_FREE;return true;},
            uint64_t(std::numeric_limits<uintptr_t>::max()));
        if(exact)*exact=result.fullScan;
        return result.memory;
    }
    // Issue #40: true when world-cache/<map>/ holds at least one .fg3 tile. The cache
    // covers only the continents; instance maps have no folder at all. Only a missing folder or
    // tile means "no cache": any other failure (access, lock, I/O) answers true, so the normal
    // build runs and reports it instead of the map's geometry being off until the map changes.
    bool worldCacheHasTiles(const std::string& map)const{
        if(map.empty())return false;
        WIN32_FIND_DATAW data;const HANDLE h=FindFirstFileW(std::filesystem::u8path(root+"world-cache/"+map+"/*.fg3").c_str(),&data);
        if(h==INVALID_HANDLE_VALUE){const DWORD error=GetLastError();return error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND;}
        FindClose(h);return true;}
    NorthlightGeometryMemory::Sample geometryAdmission(NorthlightGeometryMemory::Budget budget,bool* exact=nullptr){
        return geometryAdmissionFor(admissionProbe,budget,exact);
    }
    // largestFreeMiB is ">=N" unless exact (a witnessed lower bound is not a measurement); refused= names the
    // limit of `budget` that failed (available/largest/...) or none, need= its requirement.
    static void logGeometryMemory(const char* stage,NorthlightGeometryMemory::Sample sample,size_t generations=0,bool exactLargest=true,const NorthlightGeometryMemory::Budget* budget=nullptr){
        char largest[32];NorthlightGeometryMemory::formatLargestMiB(largest,sizeof largest,sample.largest,exactLargest);
        const auto why=budget?NorthlightGeometryMemory::refusal(sample,*budget):NorthlightGeometryMemory::Refusal{};
        logf("GEOMETRY MEMORY stage=%s availableVirtualMiB=%llu largestFreeMiB=%s generations=%zu valid=%u largestFreeExact=%u refused=%s need=%llu",
            stage,(unsigned long long)(sample.available>>20),largest,generations,unsigned(sample.valid),unsigned(exactLargest),why.limit,(unsigned long long)(why.need>>20));
    }
    static void logCpuFootprint(const char* scope,const char* stage,uint64_t id,const NorthlightMemoryFootprint::Counter& memory){
        using namespace NorthlightMemoryFootprint;
        const auto mib=[&](Kind kind){return double(memory.bytes(kind))/1048576;};
        logf("WORLD MEMORY scope=%s stage=%s buildId=%llu knownCapacityMiB=%.3f verticesMiB=%.3f trianglesMiB=%.3f accelerationMiB=%.3f texelsMiB=%.3f uploadMiB=%.3f probesMiB=%.3f fogMiB=%.3f metadataMiB=%.3f identities=%zu complete=%u basis=retained-capacity scopeTotalsNotAdditive=1 allocatorDriverAndUnreachableOwnersExcluded=1",
             scope,stage,(unsigned long long)id,double(memory.total())/1048576,mib(Vertices),mib(Triangles),mib(Acceleration),mib(Texels),mib(Upload),mib(Probes),mib(Fog),mib(Metadata),memory.owners(),unsigned(memory.complete()));
        NorthlightMemoryProfile::store().cpu(std::strcmp(scope,"builder-reachable")==0,stage,id,memory);
    }
    bool admitStaticAllocation(size_t managedBytes){
        // Preserve one aggregate transaction per static frame and its existing
        // reservation debits. A witnessed block is only a lower bound: a later
        // larger resource must re-query rather than treat that bound as the max.
        const bool first=staticAdmissionFrame!=staticFrame;
        if(first){staticAdmissionFrame=staticFrame;staticReservedBytes=0;}
        NorthlightGeometryMemory::Budget budget;
        budget.available=NorthlightGeometryMemory::ProcessReserve+64*NorthlightGeometryMemory::MiB+managedBytes+staticReservedBytes;
        budget.largest=managedBytes+NorthlightGeometryMemory::ChunkMargin;budget.valid=true;
        if(first)staticMemorySample=geometryAdmission(budget,&staticMemoryExact);
        else if(!staticMemoryExact&&staticMemorySample.valid&&staticMemorySample.available>=budget.available&&staticMemorySample.largest<budget.largest){
            auto current=geometryAdmission(budget,&staticMemoryExact);
            current.available=std::min(current.available,staticMemorySample.available);
            staticMemorySample=current;
        }
        if(!NorthlightGeometryMemory::admits(staticMemorySample,budget))return false;
        staticReservedBytes+=managedBytes;return true;
    }
    NorthlightFlightShadow::Coverage farShadowCoverage;
    NorthlightFlightShadow::Coverage flightCoverage(V pivot,const std::string& map)const{
        const auto plan=uploadedMap==map?uploadedMeshPlan.lock():nullptr;
        return NorthlightFlightShadow::coverage(pivot,plan?plan->shadowGround:std::numeric_limits<float>::quiet_NaN(),
            plan?plan->shadowTerrainReach:0,farShadowCoverage.radius);
    }
    float farMapRadius(unsigned source)const{
        const auto& map=shadowTransition[source*2+1];return map.workingValid?map.working.texel*512.f:farShadowCoverage.radius;
    }
    void requestStaticCasters(const char* map){
        if(!staticStream||!directionalShadowWork())return;
        if(staticRecoveryPaused&&!worldMeshDrawable())return;
        staticFramePivot=shadowPivot();staticPivotReady=true;
        const DWORD now=GetTickCount();
        if(!staticMemoryTick||DWORD(now-staticMemoryTick)>=1000){
            staticMemoryTick=now;
            const auto budget=NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB);
            staticAllowLoads=NorthlightGeometryMemory::admits(geometryAdmission(budget),budget);
        }
        const bool lit[2]={NorthlightGI::dot(sourceColors[0],sourceColors[0])>1e-10f,
                           NorthlightGI::dot(sourceColors[1],sourceColors[1])>1e-10f};
        farShadowCoverage=flightCoverage(staticFramePivot,map);
        try {auto request=StaticShadow::makeRequest(map,staticFramePivot,sourceDirections,lit,
                celestialValid,celestialValid?celestial.dayFraction:0.,staticAllowLoads,NorthlightQuality::foreverCelestial(quality));
             if(NorthlightFrameParityClock::fixedStaticShadowStream())request.diagnosticTick=visualTime();
             request.groundOffset=staticFramePivot.z-farShadowCoverage.center.z;request.farRadius=farShadowCoverage.radius;staticStream->request(request);
             if(staticRecoveryPaused){staticStream->setPaused(false);staticRecoveryPaused=false;logf("STATIC SHADOW recovery paused=0 reason=geometry-ready");}}
        catch(...){if(staticDrawFailures++<4)logf("STATIC SHADOW request deferred: allocation failure; existing shadows retained");}
    }
    void updateStaticCasters(bool allowUploads=true,NorthlightStreaming::Budget* budget=nullptr){
        staticCasters.settle(); /* 0.3.152: no plan job across the static cache update */
        ++staticFrame;
        if(!staticStream)return;
        const DWORD now=GetTickCount();
        const bool fixed=NorthlightFrameParityClock::fixedStaticShadowStream();
        if(fixed&&!staticStream->waitIdle(5000)){
            logf("STATIC_PARITY_FAILURE rendererFrame=%u stage=cpu-timeout",parityFrame);
            return;
        }
        auto next=staticStream->snapshot();
        // Never carry another world's casters while its replacement loads.
        if(staticScene&&staticScene->map!=lastRequest.map){staticCasters.reset();staticMatcher.clear();staticM2Memory.clear();staticScene.reset();staticOwnerGeneration=UINT64_MAX;}
        if(next&&next->map==lastRequest.map)staticScene=std::move(next);
        if(staticOwnerGeneration!=meshGeneration){
            auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Owners);
            try {staticCasters.setCoveredOwners(uploadedStaticOwners?std::shared_ptr<const StaticShadow::OwnerMap>(uploadedStaticOwners,&uploadedStaticOwners->owners):nullptr);staticOwnerGeneration=meshGeneration;
                 ownerChangesPeak=std::max(ownerChangesPeak,staticCasters.lastOwnerChanges());}
            catch(...){staticCasters.setCoveredPlacements({});staticOwnerGeneration=UINT64_MAX;}
        }
        if(staticScene&&(!staticRetryTick||DWORD(now-staticRetryTick)>=1000)){
            auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::StaticCache);
            try {
                // Both halves consume the same byte allowance independently of
                // how much of the shared render-thread deadline other passes spent.
                std::optional<NorthlightStreaming::Budget> fixedBudget;
                if(fixed)fixedBudget.emplace(2u<<20,std::numeric_limits<double>::infinity());
                if(!staticCasters.update(d,staticScene,staticFrame,allowUploads,fixed?&*fixedBudget:budget,fixed)){
                    staticRetryTick=now;
                    if(fixed)logf("STATIC_PARITY_FAILURE rendererFrame=%u stage=gpu-upload",parityFrame);
                }else staticRetryTick=0;
                streamingPhases.record(NorthlightStreaming::PhaseProfile::StaticPublish,staticCasters.stats().publishFrameMs);
            }catch(...){staticRetryTick=now;if(fixed)logf("STATIC_PARITY_FAILURE rendererFrame=%u stage=gpu-exception",parityFrame);if(staticDrawFailures++<4)logf("STATIC SHADOW upload deferred: allocation failure; base shadows retained");}
        }
        if(fixed&&staticScene){const auto& g=staticCasters.stats();
            logf("STATIC_PARITY rendererFrame=%u upload=%u placements=%u cpuModels=%u ready=%u pending=%u bytes=%llu content=%016llx",
                 parityFrame,unsigned(allowUploads),staticScene->stats.placements,staticScene->stats.models,g.readyModels,g.pendingModels,
                 (unsigned long long)g.frameBytes,(unsigned long long)staticCasters.readyContentHash());}
        if(staticScene&&NorthlightDiagnostics::enabled()&&(!staticLogTick||DWORD(now-staticLogTick)>=2000)){
            staticLogTick=now;const auto& c=staticScene->stats;const auto& g=staticCasters.stats();
            logf("STATIC INSTANCE STREAM totalLocks=%llu totalDiscards=%llu totalBytes=%llu",
                 (unsigned long long)g.instanceLocks,(unsigned long long)g.instanceDiscards,(unsigned long long)g.instanceBytes);
            logf("STATIC SHADOW PLANS builds=%llu hits=%llu invalidations=%llu placementTests=%llu batchTests=%llu modelBuilds=%llu modelReuses=%llu modelFallbacks=%llu",
                 (unsigned long long)g.planBuilds,(unsigned long long)g.planHits,(unsigned long long)g.planInvalidations,
                 (unsigned long long)g.placementTests,(unsigned long long)g.batchTests,
                 (unsigned long long)g.modelPlanBuilds,(unsigned long long)g.modelPlanReuses,(unsigned long long)g.modelPlanFallbacks);
            logf("STATIC SHARED SOURCE sharedModels=%llu compactDecodes=%llu",(unsigned long long)c.sharedModelSources,(unsigned long long)c.compactDecodes);
            logf("STATIC SHADOW PLAN COST planMs=%.3f peakMs=%.3f coveredMemoFills=%llu evictions=%llu fastLookups=%u lru=%u selfChecks=%llu selfCheckMismatches=%llu prebuilds=%u prebuildHits=%u prebuildMisses=%u prebuildFailures=%u prebuildUnstable=%u prebuildDiscards=%u planDiscards=%llu reuseMs=%.3f rebuildMs=%.3f walkMs=%.3f asyncKicks=%llu asyncBuilds=%llu stolen=%llu asyncFailures=%llu waitMs=%.3f workerMs=%.3f asyncDirtyRects=%llu kickMs=%.3f",
                 g.planMs,g.planPeakMs,(unsigned long long)g.coveredMemoFills,(unsigned long long)g.planEvictions,unsigned(StaticShadow::GpuCache::FastPlanLookups),unsigned(StaticShadow::GpuCache::LruPlanSlots),
                 (unsigned long long)g.planSelfChecks,(unsigned long long)g.planSelfCheckMismatches,staticPrebuild.stats.built,staticPrebuild.stats.hits,staticPrebuild.stats.misses,staticPrebuild.stats.failures,staticPrebuild.stats.unstable,staticPrebuild.stats.discarded,(unsigned long long)g.planDiscards,
                 g.planReuseMs,g.planRebuildMs,g.planWalkMs,(unsigned long long)g.asyncKicks,(unsigned long long)g.asyncBuilds,(unsigned long long)g.asyncStolen,(unsigned long long)g.asyncFailures,g.asyncWaitMs,g.asyncWorkerMs,staticDirtyJobsUsed,staticKickMs);
            logf("STATIC SHADOW PUBLISH publications=%llu publishMs=%.3f peakMs=%.3f exactDiffs=%llu diffFallbacks=%llu ownerTests=%llu idleReleases=%llu exact=%u grouped=%u idle=%u",
                 (unsigned long long)g.publications,g.publishMs,g.publishPeakMs,(unsigned long long)g.publishDiffs,(unsigned long long)g.publishDiffFallbacks,(unsigned long long)g.ownerTests,(unsigned long long)g.planIdleReleases,
                 unsigned(StaticShadow::GpuCache::ExactPublishDiff),unsigned(StaticShadow::GpuCache::GroupedOwnerInvalidation),unsigned(StaticShadow::GpuCache::IdlePlanRelease));
            logf("STATIC SHADOW map=\"%s\" generation=%llu placements=%u cpuModels=%u readyGPU=%u pendingCPU=%u pendingGPU=%u metadataReads=%llu modelReads=%llu reused=%llu cpuMiB=%.2f cpuPeakMiB=%.2f transientPeakMiB=%.2f gpuMiB=%.2f gpuPeakMiB=%.2f uploadBytes=%llu totalUploadBytes=%llu uploadMs=%.3f readMs=%.3f packMs=%.3f maxJobMs=%.3f latencyMs=%.3f instancing=%u draws=%llu instances=%llu cpuFailures=%llu gpuFailures=%llu dedupSkipped=%u complete=%u dedupAttempts=%u dedupMatches=%u localOwners=%zu metadataMiB=%.2f selectionMs=%.3f m2Skipped=%u m2Verified=%u m2Rejected=%u",
                 staticScene->map.c_str(),(unsigned long long)staticScene->mapGeneration,c.placements,c.models,g.readyModels,c.pendingModels,g.pendingModels,
                 (unsigned long long)c.metadataReads,(unsigned long long)c.modelReads,(unsigned long long)c.modelReuses,
                 double(c.cpuBytes)/1048576,double(c.cpuPeak)/1048576,double(c.transientPeak)/1048576,double(g.residentBytes)/1048576,double(g.peakBytes)/1048576,
                 (unsigned long long)g.frameBytes,(unsigned long long)g.uploadedBytes,g.uploadMs,c.readMs,c.packMs,c.maxJobMs,c.latencyMs,unsigned(g.instancing),
                 (unsigned long long)g.drawCalls,(unsigned long long)g.instances,(unsigned long long)c.failures,(unsigned long long)g.failures,staticDedupSkipped,unsigned(c.complete),staticDedupAttempts,staticDedupMatches,(uploadedStaticOwners?uploadedStaticOwners->placements:size_t(0)),double(c.metadataBytes)/1048576,c.selectionMs,staticM2Skipped,staticM2Verified,staticM2Rejected);
            staticDedupSkipped=staticDedupAttempts=staticDedupMatches=staticM2Skipped=staticM2Verified=staticM2Rejected=0;
        }
    }
#include "world_build_pipeline.inl"
    bool buildingScreenResources=false,screenResourceOom=false,screenResourceRetry=false,screenResourceReported=false;DWORD screenResourceRetryAt=0,screenResourceReportAt=0;
    bool check(HRESULT h,const char* s){if(SUCCEEDED(h))return true;if(buildingScreenResources&&(h==E_OUTOFMEMORY||h==D3DERR_OUTOFVIDEOMEMORY)){screenResourceOom=true;return false;}if(!failed)logf("WORLD DISABLED: %s HRESULT=%08lx",s,(unsigned long)h);failed=true;return false;}
    void clearMaterials(){for(auto& m:materials)drop(m);materials.clear();uploadedTextureBytes=0;uploadedMaterialPixels.reset();materialReuseBytes=0;}
    void clearMesh(bool keepMaterials=false){staticCasters.settle();dropStaticDirtyJobs();vertices=nullptr;indices=nullptr;if(!keepMaterials)clearMaterials();directionalCacheMesh.invalidate();batches.clear();pointSubBatches.clear();pointSubRanges.clear();pointTree.reset();pointMeshBounds.release();pointStaticList.clear();terrainBatchList.clear();shadowTerrainList.clear();terrainBatchListGeneration=UINT64_MAX;uploadedAlphaCutoffs.clear();uploadedLocalShadowRecords.reset();uploaded.reset();uploadedMeshPlan.reset();}
    void prepareActorShaders(const D3DCAPS9& caps){
        if(!actorMaps.createShaders(d,caps,quality.actorShadows&&quality.actorShadowDetail,regionalFogGpu.actorVariants()))
            logf("WORLD actor shadow map disabled: shader creation failed");
    }
#include "world_screen_resources.inl"
#include "world_geometry_resources.inl"
    /* 0.3.138: charge only what this snapshot would actually free. Most
       retirements share the BVH/plan with their successor; charging the full
       ~200 MB each time kept the reaper "full" for small items. Accounting
       only: a racing reference drop can undercount, never change content. */
    static size_t snapshotRetireBytes(const Snapshot& s){
        size_t bytes=sizeof(Snapshot)+s.atlas.capacity()*sizeof(NorthlightGI::ProbeAtlasEntry);
        for(const auto& e:s.atlas)if(e.probe.localVisibility&&e.probe.localVisibility.use_count()==1)bytes+=sizeof(NorthlightGI::ProbeLocalVisibility);
        if(s.coarseAtlas&&s.coarseAtlas.use_count()==1){bytes+=s.coarseAtlas->capacity()*sizeof(NorthlightGI::ProbeAtlasEntry);for(const auto& e:*s.coarseAtlas)if(e.probe.localVisibility&&e.probe.localVisibility.use_count()==1)bytes+=sizeof(NorthlightGI::ProbeLocalVisibility);}
        if(s.bvh&&s.bvh.use_count()==1)bytes+=s.bvh->retainedBytes();
        if(s.meshPlan&&s.meshPlan.use_count()==1)bytes+=size_t(s.meshPlan->cpuBytes());
        return bytes;
    }
    static std::vector<StaticShadow::Placement> ownerPlacements(const NorthlightWorldMesh::WorldMeshUploadPlan& plan){
        std::vector<StaticShadow::Placement> owners;owners.reserve(plan.completePlacements.size());
        for(const auto& owner:plan.completePlacements){StaticShadow::Placement p;
            p.uid=owner.uid;p.category=owner.category;p.modelKey=owner.modelKey;
            std::copy(owner.matrix,owner.matrix+9,p.matrix);p.translation=owner.translation;p.low=owner.low;p.high=owner.high;
            owners.push_back(std::move(p));
        }
        return owners;
    }
    void retirePendingCpu(){
        if(!pendingMesh)return;
        // No D3D owner leaves this thread. The packet may be the last CPU
        // geometry owner after a commit/cancel, so release it on the reaper.
        auto& pending=*pendingMesh;
        if(pending.plan&&!pending.textures.empty())retiredMaterials.enqueue(pending.textures,size_t(pending.plan->textureBytes));
        // 0.3.214 (task 42): the payload is the largest CPU object. Its plan reference is gone after a commit or an
        // orphaned plan, so this reference is the last one: free it on the reaper. Otherwise (failed attempt that
        // retries the same plan) only drop this reference.
        if(pending.payload){if(pending.plan&&!pending.plan->payload.live())retirementBacklog.retireOrFree(NorthlightStreaming::cpuRetirement(),pending.payload,size_t(pending.payload->bytes));else pending.payload.reset();}
        if(pending.plan)retirementBacklog.retireOrFree(NorthlightStreaming::cpuRetirement(),pending.plan,pending.plan->cpuBytes());
        if(pending.bvh)retirementBacklog.retireOrFree(NorthlightStreaming::cpuRetirement(),pending.bvh,pending.bvh->retainedBytes());
        // Queue pressure uses synchronous reclamation, preserving the memory
        // reserve; it never retains an unbounded chain of old generations.
    }
    // 0.3.156: a staged upload can only commit into the snapshot it was taken from.
    // Once that snapshot is no longer active it never commits, yet it keeps a geometry
    // generation alive; with an out-of-range published build that fills Generations<BVH,2>.
    template<class Pending,class Active> static bool pendingOrphaned(const Pending* pending,const Active* active){return pending&&(!active||pending->map!=active->map||pending->bvh!=active->bvh);}
    // D3D thread only: the draw hooks (updateWorldContext) and render()/upload(). Not reached
    // while effects are off (F10): recovery then happens on the first frame after re-enabling.
    void releaseOrphanedPending(const char* site){
        if(!pendingOrphaned(pendingMesh.get(),active.get()))return;
        logf("WORLD pending mesh released orphan=%s site=%s map=\"%s\"",!active?"inactive":pendingMesh->map!=active->map?"map":"replaced",site,pendingMesh->map.c_str());
        if(active&&pendingMesh->plan&&pendingMesh->plan!=active->meshPlan)pendingMesh->plan->payload.discard(); /* 0.3.214 (task 42): a superseded plan is never uploaded */
        meshStore.cancel(); /* 0.3.216: claimed buffers return to idle; the written ones already have an invalid key */
        retirePendingCpu();pendingMesh.reset();meshRetry.clear();
    }
    bool expiredMeshPending=false,expiredMeshMapChanged=false;
    // Release only at endFrame, after replay consumers are finished. A hard-expired
    // region cannot draw again; its active page table must not defeat trimIdle().
    // On the same map without memory demand or pressure the pages stay: the next build
    // elides the ones it shares (residentPages) instead of uploading them all again, and
    // its commit trims the rest. A demand or pressure that comes later still releases them.
    void releaseExpiredMesh(){
        if(!expiredMeshPending)return;
        if(active&&active->bvh){expiredMeshPending=expiredMeshMapChanged=false;return;} // a replacement was adopted
        if(!expiredMeshMapChanged&&!memoryPressure&&!geometryMemoryDemand.load(std::memory_order_relaxed))return;
        expiredMeshPending=expiredMeshMapChanged=false;
        const uint64_t bytes=meshStore.poolBytes()+uploadedTextureBytes;
        clearMesh();releaseMeshPool();uploadedMap.clear();
        uploadedStaticOwners.reset();staticOwnerGeneration=UINT64_MAX;
        fixedTerrain.reset();fixedTerrainBits.reset();uploadedTerrain.clear();
        liveTerrainIndexCount=liveDirectionalIndexCount=0;liveTerrainGeneration=0;
        invalidateShadowCache();fogScreen.invalidateHistory();history.invalidate();
        workerMemoryTrim.store(true,std::memory_order_relaxed);
        logf("WORLD expired mesh released managedMiB=%.2f",double(bytes)/1048576);
    }
    // 0.3.214 (task 42): the active generation's payload was freed after its commit and the GPU copy is gone
    // (device Reset, resize, recover). Ask the builder for a fresh generation once per bvh (again after 15 s
    // if it never arrived); until it is adopted the world simply has no shadow mesh.
    void requestPayloadRebuild(){
        const auto& w=payloadRebuildAsked;const DWORD now=GetTickCount();
        if(!w.owner_before(active->bvh)&&!active->bvh.owner_before(w)&&DWORD(now-payloadRebuildTick)<15000u)return;
        payloadRebuildAsked=active->bvh;payloadRebuildTick=now;++payloadRebuilds;
        deferLogf("WORLD mesh upload payload was released after commit; GPU mesh lost, rebuilding upload from retained local scene map=\"%s\" count=%u",active->map.c_str(),payloadRebuilds);
        enqueuePayloadRebuild();
    }
    bool streamingFailure(HRESULT hr,const char* stage,const char* detail=""){
        const auto* plan=pendingMesh?pendingMesh->plan.get():(active?active->meshPlan.get():nullptr);
        if(streamingReports++<12)logf("WORLD streaming retry: stage=%s hr=%08lx detail=%s vertexBytes=%llu indexBytes=%llu vertices=%u triangles=%u sourceMatch=%d oldMesh=%d retryMs=1000",stage,(unsigned long)hr,detail,
            (unsigned long long)(plan?plan->vertexBytes:0),(unsigned long long)(plan?plan->indexBytes:0),plan?plan->vertexCount:0,plan?plan->triangleCount:0,
            plan&&plan->pagesSealed,vertices&&indices&&active&&uploadedMap==active->map);
        meshStore.cancel(); /* 0.3.216: as in releaseOrphanedPending; idempotent without an open staging */
        retirePendingCpu();pendingMesh.reset();meshRetry.fail(GetTickCount());
        return vertices&&indices&&active&&uploadedMap==active->map;
    }
    NorthlightWorldMoonFloorFog moonFloorFog;
    bool moonFloorShadowCurrent=false;
    void releaseMoonFloor(){moonFloorShadowCurrent=false;moonFloorFog.reset();}
    void moonFloorReuseProof(int slot,NorthlightQuality::CascadeAction action,const char* reason,bool staticChanged,bool sliceBand){
        if(slot==3)moonFloorShadowCurrent=action!=NorthlightQuality::CascadeAction::Render&&!reason&&!staticChanged&&!sliceBand&&!staticSlices[slot].active&&shadowCacheKey[slot].valid;
    }
    bool moonFloorSourceCurrent(int source,bool geometryReady,bool direct,const std::string& map)const{
        return source==1&&direct&&geometryReady&&directionalShadowWork()&&nativeShadowOwned&&NorthlightGI::dot(sourceColors[1],sourceColors[1])>1e-10f&&directionalMaps.current(3).texture&&
            shadowCacheKey[3].valid&&shadowTransition[3].workingValid&&moonFloorShadowCurrent&&regionalFog.field()&&regionalFog.map()==map&&uploadedMap==map&&
            active&&uploaded.lock()==active->bvh&&NorthlightWorldStreaming::applicable(active->map,active->center,map,vec(context.camera));
    }
    HRESULT drawFogVolume(unsigned w,unsigned h,IDirect3DPixelShader9* baseline,bool floorEligible,bool actor,bool tiled,bool fading,IDirect3DTexture9* scene,const float* bounds){
        const NorthlightWorldMoonFloorFog::Input input{baseline,scene,bounds,regionalFog.field(),regionalFog.map(),floorEligible,actor,tiled,fading};
        return moonFloorFog.draw(d,input,w,h,[&](const auto& budget){
            return NorthlightGeometryMemory::admits(geometryAdmission(budget),budget);
        });
    }
    bool uploadRegionalFog(const std::shared_ptr<const NorthlightRegionalFog::Field>& field,const std::string& map){
        return regionalFog.upload(d,field,map,[]{return GetTickCount();},
            [&](HRESULT hr,const char* stage){return check(hr,stage);},
            [&](HRESULT hr,const char* stage){logf("WORLD regional fog deferred: %s HRESULT=%08lx retryMs=1000",stage,(unsigned long)hr);},
            [&]{moonFloorFog.invalidateField();});
    }
    bool frameStaging=false,frameProbeUpload=false; /* 0.3.176 (D3): this frame staged world mesh pages / uploaded the probe atlas */
#if defined(NORTHLIGHT_WORLD_UPLOAD_TEST)
    std::function<void(const char*,unsigned)> uploadTrace;
#endif
    void traceUpload(const char* stage,unsigned channel=0){
#if defined(NORTHLIGHT_WORLD_UPLOAD_TEST)
        if(uploadTrace)uploadTrace(stage,channel);
#else
        (void)stage;(void)channel;
#endif
    }
    NorthlightDynamicRing::FrameFence resetReplayFence;IDirect3DQuery9* resetReplayQuery=nullptr;bool resetReplayPending=false;DWORD resetReplayStarted=0;
    unsigned resetReplayWaitFrames=0,resetReplayFailures=0;
    std::weak_ptr<const NorthlightGI::BVH> resetReplaySource;
    std::weak_ptr<const NorthlightWorldMesh::WorldMeshUploadPlan> resetReplayPlan;bool resetReplayScoped=false;
    bool pressureReplayAllocation(){
        if(!resetReplayScoped)return false;
        if(active&&active->bvh==resetReplaySource.lock()&&active->meshPlan==resetReplayPlan.lock())return true;
        resetReplaySource.reset();resetReplayPlan.reset();resetReplayScoped=false;
        if(!vertices)clearMaterials();
        drop(resetReplayQuery);resetReplayFence.drop();resetReplayPending=false;return false;
    }

    bool pressureReplayReady(){
        if(!resetReplayPending)return true;
        const auto now=GetTickCount();++resetReplayWaitFrames;
        const auto retry=[&]{drop(resetReplayQuery);++resetReplayFailures;meshRetry.fail(now);if(NorthlightDiagnostics::enabled()&&resetReplayFailures<=3)logf("WORLD pressure reset replay fence retry delayMs=1000 failures=%u",resetReplayFailures);return false;};
        if(!resetReplayQuery){
            traceUpload("reset-fence-before-create");auto& queries=resetReplayFence;queries.device=d;resetReplayQuery=queries.issue();traceUpload("reset-fence-after-create");
            if(!resetReplayQuery){queries.drop();return retry();}
            if(!queries.endFrame())return retry();traceUpload("reset-fence-after-issue");
            resetReplayStarted=now;return false;
        }
        traceUpload("reset-fence-before-read");const auto result=resetReplayQuery->GetData(nullptr,0,0);traceUpload("reset-fence-after-read");
        if(result==S_FALSE){if(DWORD(now-resetReplayStarted)<5000)return false;return retry();}
        if(result!=S_OK)return retry();
        drop(resetReplayQuery);resetReplayPending=false;return true;
    }
#include "world_mesh_publication.inl"
    // 0.3.176 (D1): this frame's terrain change (sample frames), per-triangle tests, and (RenderProfile
    // sample frames) the arena / index build / lock+copy times; -1: not measured.
    const char* terrainChange="none";size_t terrainTriangleTests=0;double terrainPartMs[3]={-1,-1,-1};
    bool uploadLiveTerrain(){
        terrainUploadBytes=0;terrainUploadReused=false;terrainTriangleTests=0;for(auto& ms:terrainPartMs)ms=-1;
        liveTerrainGPU.beginFrame();
        // Only private opaque depth passes consume this list. Keep their index
        // order stable when native draw order changes, including point bounds.
        if(frameTerrain!=uploadedTerrain)std::sort(frameTerrain.begin(),frameTerrain.end());
        pointLiveBatches.clear();UINT pointStart=0;
        for(const auto& snapshot:frameTerrain){pointRecordTerrain(pointStart,UINT(snapshot->indices.size()/3),snapshot->bounds);pointStart+=UINT(snapshot->indices.size());}
        const bool timedParts=profileSampled();auto partStart=timedParts?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        auto part=[&](int k){if(!timedParts)return;const auto now=std::chrono::steady_clock::now();terrainPartMs[k]=std::chrono::duration<double,std::milli>(now-partStart).count();partStart=now;};
        const bool reuse=liveTerrainGeneration==meshGeneration&&frameTerrain==uploadedTerrain && (frameTerrain.empty()||(liveTerrainGPU.vertices()&&liveTerrainIndices.buffer()));
        if(captureSampled)terrainChange=reuse?"none":frameTerrain==uploadedTerrain?"identity":"membership";
        // 0.3.192 (DXVK3): a reuse frame draws the live IB slice again without a place(): re-tag its span with this frame's
        // fence so it stays pending (no NOOVERWRITE overwrite of a slice still drawn) until this frame's draws are submitted.
        // The newest span is the current lists (place() appends, the older spans are earlier lists); the current index base is kept.
        if(reuse){terrainUploadReused=true;if(liveTerrainIndexCount||liveDirectionalIndexCount)liveTerrainIndices.touch(fence());return true;}
        // Point cubes may retain terrain-only faces across refreshes. Advance
        // before any mutation, including a deferred/failed upload or empty set.
        ++liveTerrainRevision;
        NorthlightGeometryMemory::Sample arenaMemory;bool arenaSampled=false;
        std::optional<NorthlightStreaming::PhaseProfile::Scope> arenaPhase;arenaPhase.emplace(streamingPhases.peaks[NorthlightStreaming::PhaseProfile::TerrainArena]);
        const HRESULT terrainResult=liveTerrainGPU.update(d,frameTerrain,
            [&](size_t bytes){
                if(bytes>8*NorthlightGeometryMemory::MiB){
                    if(memoryPressureUntil&&GetTickCount()<memoryPressureUntil)return false;
                    arenaMemory=geometryAdmission(NorthlightGeometryMemory::dynamicBudget(bytes));arenaSampled=true;
                    // Refusing optional retention capacity must not suppress
                    // the original 8 MiB active-set allocation for one second.
                    return NorthlightGeometryMemory::admits(arenaMemory,NorthlightGeometryMemory::dynamicBudget(bytes));
                }
                return admitsGrowth("live-terrain-arena",bytes,arenaSampled?&arenaMemory:nullptr);
            },
            [](const NorthlightTerrainCapture::Position& p){return StaticShadow::Vertex{p.x,p.y,p.z,0,0};});
        arenaPhase.reset();part(0);
        if(terrainResult==S_FALSE){
            // Preserve the existing memory-pressure fallback: the complete
            // cached terrain covers this frame; do not advertise absent live chunks.
            liveTerrainIndexCount=liveDirectionalIndexCount=0;liveTerrainIndices.clearBase();liveTerrainChunks.clear();uploadedTerrain.clear();return true;
        }
        if(!check(terrainResult,"live terrain arena upload"))return false;
        std::optional<NorthlightStreaming::PhaseProfile::Scope> indexPhase;indexPhase.emplace(streamingPhases.peaks[NorthlightStreaming::PhaseProfile::TerrainIndices]);
        // 0.3.176 (U1a/U1c): the totals first, then the lists straight into the locked IB (the bytes and
        // order of the 0.3.175 concatenation); fixed-chunk membership from the bitmap beside the set.
        const auto& fixed=fixedTerrainChunks();const bool bits=fixedTerrainBits.source()==&fixed;
        const auto containsFixed=[&](std::pair<int,int> c){return bits?fixedTerrainBits.contains(c):fixed.count(c)!=0;};
        if(!liveTerrainGPU.prepare(frameTerrain,meshGeneration,[&](const auto& snapshot,uint32_t offset,std::vector<uint32_t>& output){
            NorthlightShadowTerrain::appendLiveDirectionalBy(snapshot,containsFixed,offset,output,captureSampled?&terrainTriangleTests:nullptr);
        },liveTerrainIndexCount,liveDirectionalIndexCount,[&](const auto& snapshot){
            // The original filter copies every index when none of these chunks is fixed.
            // Unknown/empty metadata and mixed captures retain the original filtering path.
            return !snapshot.bounds.chunks.empty()&&std::none_of(snapshot.bounds.chunks.begin(),snapshot.bounds.chunks.end(),[&](const auto& c){
                return c.x<0||c.y<0||c.x>=1024||c.y>=1024||containsFixed({c.x,c.y});});
        }))return check(E_FAIL,"live terrain arena membership");
        indexPhase.reset();part(1);
        if(!liveTerrainIndexCount){uploadedTerrain=frameTerrain;liveTerrainGeneration=meshGeneration;return true;}
        auto indexUpload=streamingPhases.measure(NorthlightStreaming::PhaseProfile::TerrainIndexUpload);
        const UINT ib=UINT((liveTerrainIndexCount+liveDirectionalIndexCount)*sizeof(uint32_t));
        const auto uploaded=liveTerrainIndices.upload(d,ib,fence(),
            [&](UINT capacity){return admitsGrowth("live-terrain-index-growth",capacity);},
            [&](uint32_t* data){liveTerrainGPU.write(data);},
            [&](HRESULT result,const char* action){return check(result,action);});
        if(uploaded==NorthlightLiveTerrainIndices::Upload::Deferred){
            liveTerrainIndexCount=liveDirectionalIndexCount=0;liveTerrainIndices.clearBase();liveTerrainChunks.clear();uploadedTerrain.clear();return true;
        }
        if(uploaded==NorthlightLiveTerrainIndices::Upload::Failed)return false;
        part(2);
        uploadedTerrain=frameTerrain;liveTerrainGeneration=meshGeneration;terrainUploadBytes=liveTerrainGPU.uploadedBytes+ib;
        return true;
    }
    // Win32 wide-path read (non-ASCII game folders); absent file -> false.
    static bool readSmallFile(const std::wstring& path,std::string& out){
        HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE)return false;
        char buffer[4096];DWORD got=0;bool ok=true;out.clear();
        while((ok=ReadFile(f,buffer,sizeof buffer,&got,nullptr)!=FALSE)&&got&&out.size()<262144)out.append(buffer,got);
        CloseHandle(f);return ok;
    }
    void loadQuality(){
        std::string own,old;std::vector<std::string> problems;
        const bool hasOwn=readSmallFile(std::wstring(rootPath)+L"northlight-quality.ini",own),hasOld=readSmallFile(std::wstring(rootPath)+L"shadow-experiment.ini",old);
        try{std::istringstream a(NorthlightQuality::narrow(own)),b(NorthlightQuality::narrow(old));quality=NorthlightQuality::load(hasOwn?&a:nullptr,hasOld?&b:nullptr,problems);}
        catch(...){quality=NorthlightQuality::Settings{};problems.assign(1,"allocation failure; full-quality defaults");}
        if(NorthlightQuality::bigEndianUtf16(own)||NorthlightQuality::bigEndianUtf16(old))problems.push_back("UTF-16 big-endian file not supported; save as UTF-8 or ANSI");
        logf("QUALITY %s file=%s legacyShadowExperiment=%s problems=%zu",NorthlightQuality::describe(quality).c_str(),hasOwn?"northlight-quality.ini":"absent",hasOld?"read":"absent",problems.size());
        for(size_t i=0;i<problems.size()&&i<32;++i)logf("QUALITY warning %s",problems[i].c_str());
        // 0.3.158: ActorShadows=0 forces the replay-derived keys off before anything reads them.
        const std::string forced=NorthlightQuality::forcedOff(quality);quality=NorthlightQuality::effective(quality);
        logf("QUALITY ActorShadows=%u: %s; forced off: %s",quality.actorShadows,quality.actorShadows?"actor and static shadows":"static shadows only",forced.c_str());
        logf("QUALITY ActorShadowDetail=%u: %s",quality.actorShadowDetail,NorthlightQuality::actorShadowMap(quality,true)?"actor shadow map and soft filter":"actor shadows in the near cascade"); /* 0.3.209 (task 29) */
        NorthlightDiagnostics::configure(quality.diagnostics!=0);
        NorthlightWeatherMemory::configure(quality.weatherMemory!=0); /* WeatherMemory=0: no weather reads, also not in the stream snapshot */
        NorthlightMemorySwitches::configure(quality.memMap!=0); /* task 42: MemMap (also read earlier by the process-level MEMMAP points) */
        NorthlightRenderThreadProbe::configure(NorthlightQuality::renderProfile(quality),NorthlightQuality::replayProbe(quality));
#ifdef NORTHLIGHT_REPLAY_STAND
        logf("STAND_PROFILE fixedWork=%u",unsigned(NorthlightFrameParityClock::fixedProfileWork()));
#endif
        logf("PROFILE effective diagnostics=%u renderProfile=%u replayProbe=%u period=%u cpuGpuAdditive=0",unsigned(NorthlightDiagnostics::enabled()),unsigned(NorthlightRenderThreadProbe::profiling()),unsigned(NorthlightRenderThreadProbe::probing()),NorthlightRenderThreadProbe::profiling()?NorthlightRenderThreadProbe::ProfilePeriod:NorthlightRenderThreadProbe::SamplePeriod);
        NorthlightWorldContext::SelfReadStats::timed.store(NorthlightQuality::renderProfile(quality),std::memory_order_relaxed); /* 0.3.160 */
        // 0.3.153 GIDistance: the probe layout is fixed by the first renderer of the process.
        static const bool layoutConfigured=NorthlightGI::configureProbeLayout(NorthlightGI::probeLayoutFor(NorthlightQuality::giProbeGrid(quality)));
        const auto& layout=NorthlightGI::probeLayout();
        logf("QUALITY GI distance=%u effective=%u grid=%ux%ux%u atlas=%u^3 cacheProbes=%zu configured=%u probeAhead=%u",quality.giDistance,layout.n*4-4,layout.n,layout.n,layout.nz,layout.atlas,layout.atlasSize(),unsigned(layoutConfigured),quality.giProbeAhead);
    }
public:
    // Buffer identity for the snapshot cache: a private-data token created on
    // first sight and owned by the COM object, so pointer reuse cannot alias.
    static std::uint64_t bufferIdentity(void* buffer,bool indexBuffer){
        return NorthlightTrackedBuffers::identity(buffer,indexBuffer);
    }
    void setConstantEpochSource(NorthlightReplayCaptureConstants::ClockSource<DeviceMirror> source){
        // A different provider can have the same numeric serials.
        for(auto& replay:replays)replay->constantStamp={};
        constantEpochSource=source;
    }
    void setNativeSamplerStateSource(void* context,bool(*reader)(void*,DWORD,unsigned,DWORD*)){
        nativeSamplerContext=context;nativeSamplerReader=context==d?reader:nullptr;
        if(nativeDirectRenderer)nativeDirectRenderer->setSamplerStateSource(nativeSamplerContext,nativeSamplerReader);
    }
    NorthlightDrawSnapshot::Frame& captureSources(){return replaySnapshots;}
    void setParityFrame(unsigned frame){parityFrame=frame;}
#ifdef NORTHLIGHT_REPLAY_STAND
    NorthlightCaptureWork::Work captureWork()const{
        NorthlightCaptureWork::Work work;
        work.addSnapshots(replaySnapshots);
        work.packets=replays.size();
        work.geometryReuse=captureSourceHandoffs;
        work.bankReuse=captureProjectionLoans;
        work.floatGets=4*(captureProjectionGets+terrainCameraGets);
        work.floatHandoff=4*(captureProjectionLoans+terrainCameraBorrows);
        work.rangeReads=capturePacketReadRegisters;
        work.rangeHandoff=capturePacketLoanRegisters;
        return work;
    }
    void reportReplayCopyMemory(unsigned rendererFrame)const{
        const auto memory=geometryMemory();
        logf("REPLAY COPY memory frame=%u valid=%u availableBytes=%llu largestFreeBytes=%llu",
            rendererFrame,unsigned(memory.valid),(unsigned long long)memory.available,(unsigned long long)memory.largest);
    }
#endif
    HRESULT warmPointPipelinesAtDeviceInit(){
        if(!pipelineActive||!effects.shadows||!quality.localLights||!quality.pointShadows||!quality.localLightLimit||!quality.pointShadowLimit)return S_OK;
        const auto start=std::chrono::steady_clock::now();bool kicked=false;HRESULT restored=S_OK;const HRESULT hr=prewarmPointPipelines(kicked,restored);
        if(NorthlightDiagnostics::enabled())logf("POINT pipeline prewarm scope=device-init kickCalled=%u hr=%08lx cpuMs=%.4f completion=unobserved",unsigned(kicked),(unsigned long)hr,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        if(FAILED(restored))logf("POINT pipeline prewarm state/scene restoration failed hr=%08lx; device initialization rejected",(unsigned long)restored);
        return restored; // Optional prewarm failures are harmless; unknown device state is not.
    }
    explicit WorldRenderer(IDirect3DDevice9* device,std::shared_ptr<NorthlightGpuScene::Scene> scene={}):gpuScene(NorthlightGpuScene::forDevice(device,std::move(scene))),d(device),stateBlocks(device){
        if(!gpuScene->attach(replaySnapshots))throw std::runtime_error("Cannot join world capture sources");
        if(!gpuScene->decodedSources)gpuScene->decodedSources=std::make_shared<NorthlightGI::SceneSourceCache>(8u*1024u*1024u);sharedSceneSources=gpuScene->decodedSources;terrainBoundsCache.setIdentityProvider(&bufferIdentity);terrainBoundsCache.setVersionProvider(&NorthlightTrackedBuffers::version);terrainBoundsCache.setMetadataProvider(&NorthlightTrackedBuffers::captureMetadata);terrainBoundsCache.setDeclarationCache(&declarationCache);replaySnapshots.setDeclarationProvider(&declarationCache,[](void* c,IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& e,UINT& n){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(decl,e,n);});replaySnapshots.setLayoutProvider(&declarationCache,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(decl,extent);});replaySnapshots.setIdentityProvider(&bufferIdentity);replaySnapshots.setVersionProvider(&NorthlightTrackedBuffers::version);replaySnapshots.setMetadataProvider(&NorthlightTrackedBuffers::captureMetadata);QueryPerformanceFrequency(&captureFrequency);char path[MAX_PATH*3];WideCharToMultiByte(CP_UTF8,0,rootPath,-1,path,sizeof path,nullptr,nullptr);root=path;std::string paletteError;
        loadQuality();pipelineActive=NorthlightQuality::pipelineWork(quality);request.enabled=pipelineActive;setEffects(requestedEffects);
        nativeFixtures=NorthlightNativeLights::loadFixtures(root+"world-cache/lights/native-fixtures.bin");
        staticPrebuild.steps=float(quality.shadowDirectionSteps); /* prebuild targets the same direction lattice */
        minSkinnedShadowTriangles=quality.minSkinnedTriangles;captureBudgetMiB=quality.captureBudgetMiB;actorShadowBudgetMiB=quality.actorShadowBudgetMiB;shadowFateDiagnostics=NorthlightQuality::shadowFate(quality);
        replaySnapshots.configureBudget(size_t(captureBudgetMiB)*1048576,size_t(captureBudgetMiB)*524288);setNearReserve();
        logf("SHADOW experiment minSkinnedTriangles=%u captureBudgetMiB=%u actorShadowBudgetMiB=%u actorShadowRadius=%u distance=sampled-current-vertex unknown=first GI=preserved-at-capture-budget",minSkinnedShadowTriangles,captureBudgetMiB,actorShadowBudgetMiB,quality.actorShadowRadius);
        (void)NorthlightStreaming::cpuRetirement();
        const bool foreverLoaded=foreverAtmosphere.load(root+"forever-atmosphere.bin");
        logf("FOREVER atmosphere loaded=%d lights=%zu keys=%zu outlines=%zu",foreverLoaded,foreverAtmosphere.lights.size(),foreverAtmosphere.keys.size(),foreverAtmosphere.zones.size());
        bool paletteLoaded=NorthlightCelestialProfiles::load(root+"celestial-profiles.ini",celestialProfiles,paletteError);
        logf("CELESTIAL profiles loaded=%d zones=%zu %s",paletteLoaded,celestialProfiles.zones.size(),paletteError.c_str());
#if NORTHLIGHT_DEV_TUNING
        celestialShipped=celestialProfiles;
        logf("TUNING %s: %snorthlight-tuning.ini is polled, never ship this DLL",NorthlightZoneTuning::Marker,root.c_str());
#endif
        std::string rangeError;bool rangesLoaded=NorthlightRegionalShadow::load(root+"shadow-range-profiles.ini",shadowRanges,rangeError);
        logf("SHADOW regional terrain loaded=%d zones=%zu %s",rangesLoaded,shadowRanges.zones.size(),rangeError.c_str());
        // Reuse a source already decoded by the region builder. A miss goes
        // straight to the bounded compact decoder, without another full decode.
        staticStream=std::make_unique<StaticShadow::Streamer>(root+"world-cache",StaticShadow::Limits{},
            [sources=sharedSceneSources](const std::string& path,std::string& error,const NorthlightGI::AllocationAdmission&){return sources->find(path,error);});
        staticCasters.setAdmission([this](size_t bytes){return admitStaticAllocation(bytes);});
        worker=std::thread([this]{work();});}
    ~WorldRenderer(){prepareWorker.join(); /* 0.3.179: first: an abandoned worker may still be inside a record */
        stopWorker();std::atomic_store(&gpuScene->workerPreparation,std::shared_ptr<void>{});releaseGPU();for(auto& p:captureShaders)drop(p.second.replacement);}
    void actorSettingsChanged(){
        // Actor controls change dynamic caster coverage, not the shared scene,
        // static maps, GI, terrain/replay buffers or world/fog shader programs.
        actorMaps.settingsChanged();
        fogScreen.invalidateHistory();history.invalidate();actorShadowHistory.clear();actorShadowOriginValid=false;
        shadowFrameReady=pointReady=false;captureDemand=true;
        for(unsigned source=0;source<2;++source){
            nearShadow[source].invalidate();farShadow[source].invalidate();
            actorValid[source]=nearActorSkip[source]=actorCenterValid[source]=false;actorHold[source].invalidate();
        }
        for(auto& slot:pointTargets.slots()){slot.ready=false;slot.schedule.invalidate();slot.faceCycle.reset();}
        flashShadows.invalidate();
    }
    // 0.3.214 (task 42): keepMesh (device Reset, resolution change) keeps what describes the UPLOADED mesh: the MANAGED
    // page pool + material textures (they survive Reset in D3D9 and DXVK), the vertices/indices aliases, batches, terrain
    // lists, fixedTerrain(+bits), uploadedStaticOwners, uploaded/uploadedMap/meshGeneration and a staged pendingMesh (MANAGED
    // too; its cursor continues). Everything else (DEFAULT targets, dynamic rings, queries, state blocks, caches derived from
    // them) is released as before. recover() and the destructor release the mesh as well; the CPU payload is gone by then, so
    // upload() asks the builder for a fresh generation.
    void releaseGPU(bool keepMesh=false){
        releaseFogVolume();
        screenGroups=geometryGroups=0;
        shadowReplayAlpha.reset();
        releaseMoonFloor();
        if(nativeDiagnosticCapture){
            nativeDiagnosticCapture->finish(false);
            nativeDiagnosticCapture.reset();
        }
        releaseShadowTransitions();releaseNearEnvironment();
        releaseSoftShadows();
        composite.reset();
        drop(resetReplayQuery);resetReplayFence.drop();
        if(!keepMesh){
            resetReplaySource.reset();resetReplayPlan.reset();
            resetReplayScoped=false;resetReplayPending=false;
        }
        screenResourceRetry=false;
        NorthlightMemoryProfile::store().invalidateGpu();resourceMemoryProfileAt=0;
        materialPixelAudit.reset();replayBoundsAbandon();releaseReplayProbe();

        // Drain workers before invalidating the replay inputs they can read.
        staticCasters.settle();
        rigidMemoryClear();
        admission.reset();admissionOn=false;gatedBodies.clear();
        prepareQuiesce();
        prepareCaches->sampled.clear();prepareCaches->bones.clear();prepareCachesStale=false;
        if(replays.empty()&&heldShadowReplays.empty())prepareFrameRelease(); /* 0.3.179: no replay left to point at them */
        actorShadowHistory.clear();actorShadowOriginValid=false;
        replayBoundsMetadata.clear();
        if(!keepMesh)replayBoundsCache.clear(); /* CPU envelopes validate immutable source, program, declaration and current pose; GPU reset must not make every caster unbounded. */
        declarationCache.clear();
        if(!keepMesh)uploadedStaticOwners.reset();
        staticOwnerGeneration=UINT64_MAX;staticCasters.reset();
        staticMatcher.clear();staticM2Memory.clear();staticScene.reset();staticRetryTick=0;
        stateBlocks.clear();

        uploadedTerrain.clear();
        liveTerrainIndexCount=liveDirectionalIndexCount=0;
        if(!keepMesh){fixedTerrain.reset();fixedTerrainBits.reset();}
        liveTerrainGeneration=0;
        regionalFog.reset();
        releaseScreenFallbacks();
        releasePointGPU(keepMesh);
        flashShadows.reset();
        probeAtlas.resetHistory();
        screenLighting.releaseAuxiliaryTargets();
        releaseReplayGPU();
        releaseLiveTerrainBuffers();
        if(!keepMesh){pendingMesh.reset();clearMesh();}
        retiredMaterials.clear();
        if(!keepMesh){releaseMeshPool();uploadedMap.clear();}

        releaseShadowMapsOnReset();
        if(!keepMesh||!keepPressureProbes)probeAtlas.releaseTextures();
        releaseScreenColorTargets();
        foreverFog.reset();
        screenLighting.releaseShaders();
        regionalFogGpu.reset();
        localLighting.reset();
        worldNormals.reset();
        history.reset();
        shadowPrograms.releaseCasters();
        geometryTargetsReady=false;geometryTargetsRetry=false;
        width=height=0;
        uploadedSerial=0;
    }
    // Explicit enable/retry only, called after the wrapper's clearFrame(). This
    // never calls endFrame(), so packet capture and cleanup run exactly once.
    void recover(){meshRetry.clear();if(!failed)return;releaseGPU(true);failed=false;valid=false;streamingReports=0;logf("WORLD explicit recovery requested");}
    void releaseStateCache(bool retainPointStatic=false){stateBlocks.clear();invalidateShadowCache(retainPointStatic&&!failed);}
    void setEffects(const NorthlightEffectSwitches::Settings& requested){
        requestedEffects=requested;auto next=requested;next.gi=pipelineActive&&NorthlightQuality::giPass(quality,next.gi);
        next.shadows=pipelineActive&&quality.worldShadows&&next.shadows;next.fog=pipelineActive&&quality.worldFog&&next.fog;
        if(effects==next)return;
        // Called at the frame boundary; never reuse lighting from a different
        // effect combination. The shared geometry and GI cache stay warm.
        history.invalidate();
        if(effects.shadows!=next.shadows){
            shadowsComposited=false;shadowFrameReady=false;pointReady=false;
            // A switch changes dynamic caster coverage, not static light/mesh
            // content. Revalidate its face keys and redraw every dynamic face.
            invalidateShadowCache(true);if(!next.shadows)releaseShadowTransitions();
        }
        settingsRequestDirty=settingsRequestDirty||effects.fog!=next.fog||effects.gi!=next.gi;
        effects=next;
    }
    bool effectsEnabled()const{return NorthlightQuality::pipelineWork(quality);}
    NorthlightFrameRequirements::Plan frameRequirements(bool materials=false,bool diagnostic=false)const{return NorthlightFrameRequirements::make(quality,effects,materials,diagnostic,menuDomain||nativeShadowAvailable);}
    bool wantsWaterMask(bool materials,bool diagnostic)const{
        // Capture starts before native shadow-provider availability is known for this frame.
        return NorthlightFrameRequirements::make(quality,effects,materials,diagnostic).waterMask;
    }
    bool northlightShadowsRequested()const{return effects.shadows&&quality.shadowProvider;}
    bool directionalShadowWork()const{return northlightShadowsRequested()&&(menuDomain||nativeShadowAvailable);}
    bool nativeShadowReplacementReady()const{
        if(menuDomain||!directionalShadowWork()||!ready()||!fogGeometryReady||!shadowFrameReady||!composited)return false;
        bool activeSource=false;
        for(unsigned source=0;source<2;++source){
            if(NorthlightGI::dot(sourceColors[source],sourceColors[source])<=1e-10f)continue;
            activeSource=true;
            for(unsigned cascade=0;cascade<2;++cascade){const unsigned slot=source*2+cascade;const char action=cascadeActions[slot];
                if(!directionalMaps.current(slot).texture||(action!='R'&&action!='U'&&!(action=='D'&&shadowTransition[slot].workingValid)))return false;}
        }
        // Under open sky, keep the client lighting while both celestial sources are dark.
        const bool openSky=celestialValid&&celestial.nativePlanets&&celestial.skyboxCover==0.f;
        return activeSource||!openSky;
    }
    void setNativeShadowOwnership(bool owned,bool available=true){
        if(nativeShadowOwned==owned&&nativeShadowAvailable==available)return;
        const bool changed=nativeShadowAvailable!=available;nativeShadowOwned=owned;nativeShadowAvailable=available;
        fogScreen.invalidateHistory();history.invalidate();shadowsComposited=false;
        if(changed){shadowFrameReady=false;invalidateShadowCache(true);}
    }
    bool surfaceLightingEnabled(bool materials=true)const{return frameRequirements(materials&&quality.autoBump).lighting;}
    void setPipelineEnabled(bool value){
        if(pipelineActive==value)return;pipelineActive=value;setEffects(requestedEffects);settingsRequestDirty=true;
        setWorkerEnabled(value);
    }
    void releaseDefaultMesh(){
        // A second Reset may interrupt replay after targets were recreated but
        // before any page committed. Keep compatible probes and re-fence the
        // same committed source even when its active GPU table is empty.
        const bool continuing=pressureReplayAllocation();
        if(continuing){keepPressureProbes=true;resetReplayPending=true;}
        const auto hasDefault=[&](uint32_t id){return id<meshBuffers.size()&&((meshBuffers[id].vb&&meshBuffers[id].vbPool==D3DPOOL_DEFAULT)||(meshBuffers[id].ib&&meshBuffers[id].ibPool==D3DPOOL_DEFAULT));};
        bool anyDefault=false;for(uint32_t id=0;id<meshBuffers.size();++id)anyDefault|=hasDefault(id);
        if(!anyDefault&&!continuing)return;
        const bool activeLost=std::any_of(meshStore.table().begin(),meshStore.table().end(),hasDefault);
        // Cancel claims before releasing DEFAULT resources. An active MANAGED
        // generation survives idle DEFAULT cleanup; active DEFAULT pages replay
        // from a retained pressure payload, or rebuild on the worker after Reset.
        meshStore.cancel();retirePendingCpu();pendingMesh.reset();
        if(activeLost){
            resetReplayScoped=active&&active->bvh==uploaded.lock()&&active->meshPlan==uploadedMeshPlan.lock()&&active->meshPlan->payload.live();
            if(resetReplayScoped){resetReplaySource=active->bvh;resetReplayPlan=active->meshPlan;}else{resetReplaySource.reset();resetReplayPlan.reset();}
            resetReplayPending=resetReplayScoped;clearMesh();releaseMeshPool();uploadedMap.clear();
        }
        else trimMeshPools();
    }
    // MANAGED probe atlases survive Reset even after resident upload pages
    // were discarded, or a second Reset interrupts their reconstruction.
    // Reuse storage, invalidate history/serial and rewrite current probe data.
    // The new mesh plan still uses the normal 256 MiB admission reserve.
    void reset(bool preserveCaptureSources=false){keepPressureProbes=true;auroraSource.reset();auroraSourceMap.clear();drop(resetReplayQuery);resetReplayFence.drop();releaseDefaultMesh();weatherShelter.reset();weatherShelterUnavailable=false;lightAttachments.reset();smallShadowBones.clear();equipmentLights.reset();equipmentMap.clear();particleLights.reset();particleMap.clear();shadowsComposited=false;resetShadowPivot();endFrame();replaySnapshots.clearIndexCache(nullptr,nullptr,preserveCaptureSources);actorJobComplete.reset();actorJobSerial_=0;actorSceneMap_.clear();lastActorCapture=0;terrainBoundsCache.clearPersistent();previousCacheHits=0;freeReplays.clear();pooledSnapshotBytes=0;valid=false;failed=false;releaseGPU(true); /* task 42: the committed MANAGED mesh survives any Reset, failed or not */}
    // 0.3.176 (U0/S0): the sample-frame lines of the selection and upload spans (and the RIGID event
    // lines) are formatted where they are today and written here, from endFrame, so no log write (a
    // vfprintf and fflush under the log lock) is inside a bucketed span. The text is the same.
    std::vector<std::string> deferredLines;
    __attribute__((format(printf,2,3))) void deferLogf(const char* format,...){
        char buffer[2048];va_list a;va_start(a,format);va_list b;va_copy(b,a);const int n=std::vsnprintf(buffer,sizeof buffer,format,a);va_end(a);
        if(n>=0)try{
            if(size_t(n)<sizeof buffer)deferredLines.emplace_back(buffer,size_t(n));
            else{std::string line(size_t(n),'\0');std::vsnprintf(&line[0],size_t(n)+1,format,b);deferredLines.push_back(std::move(line));}
        }catch(...){}
        va_end(b);}
    void flushDeferredLogs(){for(const auto& line:deferredLines)logf("%s",line.c_str());deferredLines.clear();}
    // 0.3.192 (DXVK3): the per-interval lock volume (lock_meter.h); runs on the Diagnostics 2000 ms tick of endFrame.
    DWORD lockMeterTick=0;
    void logLockMeter(){
        const auto m=NorthlightLockMeter::takeInterval();const double frames=m.frames?double(m.frames):1.0,kib=1024.0;
        logf("LOCK METER frames=%u discardKiB/frame avg=%.0f max=%.0f stagingKiB/frame avg=%.0f max=%.0f over10MiB=%u | replayVB=%.0f replayIB=%.0f liveIB=%.0f arena=%.0f instances=%.0f game=%.0f | ring wraps=%u fenceReuse=%u discards=%u shrinks=%u | readback/frame locks=%.1f KiB=%.0f dynamic=%u/%.0fKiB defaultStatic=%u/%.0fKiB other=%u/%.0fKiB per frame flag=0x%x processVertices=%u | copies/frame served=%.1f/%.0fKiB fallbackLocks=%.1f fills=%u/%.0fKiB evictions=%u invalidations=%u refused=%u resident=%.0fKiB/%ubuffers cap=%.0fKiB large=%.0fKiB/%u/%u",
             unsigned(m.frames),m.discardSum/frames/kib,double(m.discardMax)/kib,m.stagingSum/frames/kib,double(m.stagingMax)/kib,unsigned(m.over10MiB),
             m.site[NorthlightLockMeter::ReplayVB]/frames/kib,m.site[NorthlightLockMeter::ReplayIB]/frames/kib,m.site[NorthlightLockMeter::LiveIB]/frames/kib,
             m.site[NorthlightLockMeter::Arena]/frames/kib,m.site[NorthlightLockMeter::Instances]/frames/kib,m.site[NorthlightLockMeter::Game]/frames/kib,
             unsigned(m.ringWraps),unsigned(m.ringFenceReuse),unsigned(m.ringDiscards),unsigned(m.ringShrinks),
             m.readLocks/frames,m.readBytes/frames/kib,unsigned(m.readClass[NorthlightLockMeter::ReadDynamic]),m.readClassBytes[NorthlightLockMeter::ReadDynamic]/frames/kib,unsigned(m.readClass[NorthlightLockMeter::ReadDefaultStatic]),m.readClassBytes[NorthlightLockMeter::ReadDefaultStatic]/frames/kib,unsigned(m.readClass[NorthlightLockMeter::ReadOther]),m.readClassBytes[NorthlightLockMeter::ReadOther]/frames/kib,
             unsigned(NorthlightUpload::readBackLock()),unsigned(m.processVertices),
             m.copyServed/frames,m.copyServedBytes/frames/kib,m.copyFallback/frames,unsigned(m.copyFills),m.copyFillBytes/kib,unsigned(m.copyEvictions),unsigned(m.copyInvalidations),unsigned(m.copyRefused),m.copyResidentBytes/kib,unsigned(m.copyResidentBuffers),m.copyCapBytes/kib,m.copyLargeBytes/kib,unsigned(m.copyLargeGrants),unsigned(m.copyLargeDrops));
        const auto r=NorthlightReplayCopies::takeRefusals();
        logf("REPLAY COPIES refused budget=%llu tooLarge=%llu backoff=%llu unsafe=%llu locked=%llu pressure=%llu allocation=%llu largestBufferKiB=%.0f largeCapMiB=%.0f maxLargeMiB=%.0f",(unsigned long long)r.budget,(unsigned long long)r.tooLarge,(unsigned long long)r.backoff,(unsigned long long)r.unsafe,(unsigned long long)r.locked,(unsigned long long)r.pressure,(unsigned long long)r.allocation,double(r.largestBuffer)/kib,double(NorthlightReplayCopies::kLargeCapBytes)/1048576,double(NorthlightReplayCopies::kLargeCopyBytes)/1048576);
    }
    void endFrame(bool retainPool=true,unsigned rendererFrame=0){
        gpuDiagnosticFrame=false;
        equipmentBankAudit={};equipmentDrawAudit={};
        if(equipmentLightsOn()){equipmentLights.nextFrame();equipmentCapture.nextFrame();particleLights.nextFrame();NorthlightParticleUploads::cache().nextFrame();}
        flushDeferredLogs(); /* 0.3.176 (U0/S0): after every bucketed span of the frame */
        if(profileSampled())logPrepareProfile(rendererFrame);
        if(profileSampled()){unsigned typed=0,eligible=0;for(const auto& p:replays){typed+=p->boundsWork.paletteSpan!=0;eligible+=pointPaletteSpan(*p)!=0;}
            logf("POINT palette rendererFrame=%u draws=%u packets=%u registers=%u rebasedPackets=%u savedRegisters=%u instanceBytes=%u typedSources=%u eligibleSources=%u disabled=%u retryFrames=%llu",rendererFrame,pointWork.paletteDraws,pointWork.palettePackets,pointWork.paletteRegisters,pointWork.paletteRebased,pointWork.paletteSavedRegisters,shadowPalette.buffer()?NorthlightShadowPalettePass::InstanceBytes:0,typed,eligible,unsigned(shadowPalette.unsupported()),(unsigned long long)shadowPalette.retryIn(frames));}
        if(profileSampled())logf("POINT workload rendererFrame=%u selected=%u staticDraws=%u terrainDraws=%u liveDraws=%u replayDraws=%u replayMerged=%u unionDraws=%u casterTriangles=%llu clippedGapTriangles=%llu staticRebuiltFaces=%u staticRestores=%u boundsTotal=%zu boundsValid=%u boundsUnbounded=%zu unboundedRigidFacePackets=%u unboundedSkinnedFacePackets=%u whiteLightingSkipped=%u boundsRuns=%u crossGroupMerged=%u duplicateInstances=%u pairedStaticFaces=%u crowdOperations=%u boundsOperations=%zu boundsOperationLimit=%zu",rendererFrame,pointWork.selected,pointWork.statics,pointWork.terrain,pointWork.live,pointWork.replay,pointWork.replayMerged,pointWork.unions,(unsigned long long)pointWork.triangles,(unsigned long long)pointWork.bridged,pointWork.rebuilt,pointWork.restored,replays.size(),pointBoundsValid,replays.size()-std::min(replays.size(),size_t(pointBoundsValid)),pointWork.unboundedRigid,pointWork.unboundedSkinned,pointWork.whiteLightingSkipped,replayBoundsRuns,pointWork.crossGroupMerged,pointWork.duplicateInstances,pointWork.pairedFaces,unsigned(replayCrowdBoundsOperations()),pointBoundOperations,replayBoundsCache.operationLimit());
        if(profileSampled())logf("NATIVE_DIRECT workload rendererFrame=%u nativeSlots=%u attempts=%u otherMatchedKept=%u unavailableCube=%u submittedTotal=%llu dualSubmittedTotal=%llu tripleSubmittedTotal=%llu fallbackTotal=%llu coldPairs=%u deferredPairs=%u rendererDirect=unchanged",rendererFrame,nativeDirectSlots,nativeDirectMatched,nativeDirectMultiple,nativeDirectMissingCube,(unsigned long long)(nativeDirectRenderer?nativeDirectRenderer->draws:0),(unsigned long long)(nativeDirectRenderer?nativeDirectRenderer->multiDraws:0),(unsigned long long)(nativeDirectRenderer?nativeDirectRenderer->tripleDraws:0),(unsigned long long)(nativeDirectRenderer?nativeDirectRenderer->fallbacks:0),nativeDirectRenderer?nativeDirectRenderer->coldPairs:0,nativeDirectRenderer?nativeDirectRenderer->deferredPairs:0);
        if(profileSampled())logf("NATIVE_DIRECT coverage rendererFrame=%u nativeCounts=%u,%u,%u,%u,%u matchedCounts=%u,%u,%u,%u,%u unclassifiedPreparedBypass=%u unclassifiedPrivateBypass=%u bankMemoHits=%u bankMemoMisses=%u scope=decoded-native-pairs counts=draws-by-slot-count",
            rendererFrame,nativeDirectSlotCounts[0],nativeDirectSlotCounts[1],nativeDirectSlotCounts[2],nativeDirectSlotCounts[3],nativeDirectSlotCounts[4],nativeDirectMatchCounts[0],nativeDirectMatchCounts[1],nativeDirectMatchCounts[2],nativeDirectMatchCounts[3],nativeDirectMatchCounts[4],nativeDirectPreparedBypass,nativeDirectPrivateBypass,nativeDirectBankHits,nativeDirectBankMisses);
        if(profileSampled())logf("NATIVE_DIRECT misses rendererFrame=%u decodeIgnored=%u noBinding=%u ambiguous=%u cubeNotReady=%u invalidGain=%u changedCube=%u changedSource=%u",rendererFrame,nativeDirectMissReasons[0],nativeDirectMissReasons[1],nativeDirectMissReasons[2],nativeDirectMissReasons[3],nativeDirectMissReasons[4],nativeDirectMissReasons[5],nativeDirectMissReasons[6]);
        const auto& pointSubmitWork=nativePointSubmit.work();
        if(profileSampled())logf("NATIVE_POINT_OWNER rendererFrame=%u draws=%u slots=%u observedMatchedSlots=%u terrainDraws=%u nativeSceneValid=%u nativeRGB=selected-source-handoff source=scene-or-proven-draw-bank alphaPosition=proven rendererDirect=single-owner",rendererFrame,pointSubmitWork.draws,pointSubmitWork.slots,nativeObservedOwnedSlots,pointSubmitWork.terrainDraws,nativeDirectScene.valid);
        if(profileSampled()&&NorthlightDiagnostics::enabled()&&nativeDirectRenderer){const auto& r=*nativeDirectRenderer;
            logf("NATIVE_DIRECT recipes rendererFrame=%u requested=%llu,%llu,%llu coldAttempts=%llu,%llu,%llu deferred=%llu,%llu,%llu coldRejected=%llu,%llu,%llu successful=%llu,%llu,%llu legacyFilter=%llu,%llu,%llu samplerBulkHits=%llu samplerBulkFallbacks=%llu samplerGettersSkipped=%llu whiteCubeSkips=%llu whiteCubeBypasses=%llu zeroGainSkips=%llu zeroGainBypasses=%llu counts=cumulative-by-requested-source-count",
                rendererFrame,(unsigned long long)r.requestsByCount[0],(unsigned long long)r.requestsByCount[1],(unsigned long long)r.requestsByCount[2],(unsigned long long)r.coldAttemptsByCount[0],(unsigned long long)r.coldAttemptsByCount[1],(unsigned long long)r.coldAttemptsByCount[2],(unsigned long long)r.deferredByCount[0],(unsigned long long)r.deferredByCount[1],(unsigned long long)r.deferredByCount[2],(unsigned long long)r.coldRejectedByCount[0],(unsigned long long)r.coldRejectedByCount[1],(unsigned long long)r.coldRejectedByCount[2],(unsigned long long)r.successfulByCount[0],(unsigned long long)r.successfulByCount[1],(unsigned long long)r.successfulByCount[2],(unsigned long long)r.legacyByCount[0],(unsigned long long)r.legacyByCount[1],(unsigned long long)r.legacyByCount[2], (unsigned long long)r.samplerBulkHits,(unsigned long long)r.samplerBulkFallbacks,(unsigned long long)(r.samplerBulkHits*7),(unsigned long long)r.whiteCubeSkips,(unsigned long long)r.whiteCubeBypasses,(unsigned long long)r.zeroGainSkips,(unsigned long long)r.zeroGainBypasses);}
        nativeDirectMissReasons={};nativeDirectSlotCounts={};nativeDirectMatchCounts={};nativeDirectPreparedBypass=nativeDirectPrivateBypass=nativeDirectBankHits=nativeDirectBankMisses=0;
        nativeDirectSlots=nativeDirectMatched=nativeDirectMultiple=nativeDirectMissingCube=nativeObservedOwnedSlots=0;
        nativePointSubmit.endFrame();
        composite.clearMasks();effectCoverage=nullptr;effectEmission=false;
        frameFence.endFrame(); /* 0.3.192 (DXVK3): the one Issue of the frame's fence, after every replay/terrain draw, before any early return; reset() calls endFrame() too */
        NorthlightLockMeter::endFrame();
        if(NorthlightReplayCopies::enabled.load(std::memory_order_relaxed))NorthlightReplayCopies::advanceFrame(); /* 0.3.192 (CS): the CPU copies' thrash/fill time base, once per frame on the replay thread */
        if(NorthlightDiagnostics::enabled()){const DWORD tick=GetTickCount();
            if(!lockMeterTick){lockMeterTick=tick?tick:1;NorthlightLockMeter::takeInterval();}
            else if(DWORD(tick-lockMeterTick)>=2000){lockMeterTick=tick;logLockMeter();}}
        prepareQuiesce();prepareEndFrame(); /* 0.3.177: before the replays are recycled */
        replayBoundsAbandon(); /* render() joined it; packets are recycled below */
        paletteFrameValid=false;staticPivotReady=false;rigidMemory.clearDrawn(); /* 0.3.173: drawn marks are per capture frame */
        if(!valid||failed||workerFault())stateBlocks.clear();
        /* 0.3.149 RenderProfile lines: after every measured span of the frame */
        if(profileSampled())logRenderProfile();else{replayProfileUsed.clear();replayGiPacked.clear();}
#ifdef NORTHLIGHT_REPLAY_STAND
        if(profileSampled())logf("WORLD shadow packet bank rendererFrame=%u reads=%u loans=%u readRegisters=%u loanRegisters=%u",
            rendererFrame,capturePacketReads,capturePacketLoans,capturePacketReadRegisters,capturePacketLoanRegisters);
        if(profileSampled()||NorthlightCaptureWork::selected(rendererFrame))
            logf("WORLD WMO mirror rendererFrame=%u projectionBorrows=%u packetBorrows=%u packetRegisters=%u",
                 rendererFrame,captureWmoProjectionBorrows,captureWmoPacketBorrows,captureWmoPacketRegisters);
#endif
        frameStaging=frameProbeUpload=false;
        logReplayProbeWindow();
        nativeAdtCoverage.flush(&logf);
        for(int slot=0;slot<4;++slot){lastCascadeActions[slot]=cascadeActions[slot];cascadeActions[slot]='-';}
        if(captureSampled&&captureFrequency.QuadPart>0){double ms=1000.0/double(captureFrequency.QuadPart);
            logf("WORLD CPU capture terrain=%.3fms replay=%.3fms terrainCalls=%u terrainUP=%u replayCandidates=%u unknownCalls=%u acceptedReplay=%zu captureSkipped=%u rejSmall=%u rejGate=%u rejCut=%u rejBudget=%u rendererFrame=%u",terrainCaptureTicks*ms,replayCaptureTicks*ms,terrainCaptureCalls,terrainUPCalls,replayCaptureCalls,unknownCaptureCalls,replays.size(),unsigned(captureMode==CaptureSkipped),rejSmall,rejGate,rejCut,rejBudget,rendererFrame);
        }
        if(captureSampled){logReplayConstantStorage(rendererFrame);logf("WORLD capture cache hits=%llu frameHits=%llu entries=%zu bytes=%llu constantsReadBytes=%zu constantReadCalls=%zu sm1Draws=%u relativeDraws=%u",(unsigned long long)terrainBoundsCache.persistentHits(),(unsigned long long)(terrainBoundsCache.persistentHits()-previousCacheHits),terrainBoundsCache.persistentEntries(),(unsigned long long)terrainBoundsCache.persistentBytes(),capturedConstantBytes,capturedConstantCalls,capturedSM1Draws,capturedRelativeDraws);
            logf("BUFFER content noChangeWrites=%llu noChangeMiB=%.3f skippedWrites=%llu skippedMiB=%.3f",(unsigned long long)NorthlightTrackedBuffers::noChangeWrites.load(std::memory_order_relaxed),double(NorthlightTrackedBuffers::noChangeBytes.load(std::memory_order_relaxed))/1048576,(unsigned long long)NorthlightTrackedBuffers::skippedWrites.load(std::memory_order_relaxed),double(NorthlightTrackedBuffers::skippedBytes.load(std::memory_order_relaxed))/1048576);
            const auto ranges=NorthlightTrackedBuffers::rangeStats();
            logf("BUFFER ranges queries=%llu preserved=%llu coarseFallbacks=%llu pageKiB=%.3f pageBytes=%zu budgetKiB=%zu",(unsigned long long)ranges.queries,(unsigned long long)ranges.preserved,(unsigned long long)ranges.fallbacks,double(NorthlightBufferRanges::residentBytes.load(std::memory_order_relaxed))/1024,NorthlightBufferRanges::PageBytes,NorthlightBufferRanges::Budget/1024);}
        if(captureSampled)logf("WORLD optimized caches terrainTrackedHits=%llu terrainAvoidedBytes=%llu boundsHits=%llu boundsMisses=%llu boundsAvoidedVertices=%llu boundsLookupMs=%.3f staticMaintenanceVisits=%llu staticPublications=%llu",
            (unsigned long long)terrainBoundsCache.trackedHits(),(unsigned long long)terrainBoundsCache.avoidedReadBytes(),
            (unsigned long long)replayBoundsCache.hits(),(unsigned long long)replayBoundsCache.misses(),(unsigned long long)replayBoundsCache.avoidedVertices(),replayBoundsCache.lookupMilliseconds(),
            (unsigned long long)staticCasters.stats().maintenanceVisits,(unsigned long long)staticCasters.stats().publications);
        if(captureSampled){const auto& t=terrainBoundsCache.maintenance();
            const auto& p=terrainBoundsCache.capturePhases();const double ms=captureFrequency.QuadPart>0?1000.0/double(captureFrequency.QuadPart):0;
            logf("TERRAIN cpuPhases stateMs=%.6f lookupMs=%.6f indexMs=%.6f vertexMs=%.6f buildMs=%.6f retainMs=%.6f clockReads=%u lookupProbes=%u eraseProbes=%u roomChecks=%u sourceCompareVolume=%llu decodedCompareVolume=%llu offsetComparisons=%llu rendererFrame=%u scope=exclusive-sampled-buffered-capture",
                p.ticks[0]*ms,p.ticks[1]*ms,p.ticks[2]*ms,p.ticks[3]*ms,p.ticks[4]*ms,p.ticks[5]*ms,p.clockReads,t.lookupProbes,t.eraseProbes,t.roomChecks,(unsigned long long)t.sourceComparedBytes,(unsigned long long)t.decodedComparedBytes,(unsigned long long)t.offsetCompared,rendererFrame);
            logf("TERRAIN content cache entryLimit=%zu byteLimit=%llu entries=%zu bytes=%llu lookupMisses=%u evictions=%u countPressure=%u bytePressure=%u readBytes=%llu rendererFrame=%u",
                terrainBoundsCache.persistentEntryLimit(),(unsigned long long)terrainBoundsCache.persistentByteLimit(),terrainBoundsCache.persistentEntries(),(unsigned long long)terrainBoundsCache.persistentBytes(),
                t.lookupMisses,t.evictions,t.countPressure,t.bytePressure,(unsigned long long)terrainBoundsCache.bytesRead(),rendererFrame);
            logf("TERRAIN revalidation tracked=%u unproven=%u lifetimeChanged=%u vertexChanged=%u indexChanged=%u unstable=%u exactReuses=%u rebuilds=%u indexTokenHits=%u rendererFrame=%u",
                t.trackedHits,t.unproven,t.lifetimeChanged,t.vertexChanged,t.indexChanged,t.unstable,t.exactReuses,t.rebuilds,t.indexTokenHits,rendererFrame);
            logf("TERRAIN metadata batches=%u hits=%u fallbacks=%u",t.metadataBatches,t.metadataHits,t.metadataFallbacks);
            logf("TERRAIN capture copies vertexSpanBytes=%llu rawVertexCopyBytes=%llu xyzCopyBytes=%llu directColdMisses=%u directUnstable=%u rendererFrame=%u",
                (unsigned long long)t.vertexSpanBytes,(unsigned long long)t.rawVertexCopyBytes,(unsigned long long)t.xyzCopyBytes,t.directColdMisses,t.directUnstable,rendererFrame);
            logf("TERRAIN index plans hits=%u trackedRangeHits=%u freshVertices=%llu avoidedIndexDecodes=%llu rendererFrame=%u",t.indexPlanHits,t.indexPlanTokenHits,(unsigned long long)t.indexPlanVertices,(unsigned long long)t.indexPlanIndices,rendererFrame);
            const auto indices=terrainBoundsCache.indexStorage();
            logf("TERRAIN index storage compactEntries=%zu wideEntries=%zu indexMiB=%.3f avoidedIndexMiB=%.3f sourceIndexBlocks=%zu sourceIndexReferences=%zu sourceIndexMiB=%.3f sourceIndexAvoidedMiB=%.3f sourceOffsetCompact=%zu sourceOffsetWide=%zu sourceOffsetMiB=%.3f sourceOffsetAvoidedMiB=%.3f sourceOffsetPhysicalMiB=%.3f sourceOffsetSharedReferences=%zu rendererFrame=%u",indices.compact,indices.wide,double(indices.bytes)/1048576,double(indices.avoided)/1048576,indices.sourceBlocks,terrainBoundsCache.persistentEntries(),double(indices.sourceBytes)/1048576,double(indices.sourceAvoided)/1048576,indices.offsetCompact,indices.offsetWide,double(indices.offsetBytes)/1048576,double(indices.offsetAvoided)/1048576,double(indices.offsetPhysicalBytes)/1048576,indices.offsetSharedReferences,rendererFrame);
            logf("TERRAIN decoded storage logicalMiB=%.3f physicalMiB=%.3f sharedBlocks=%zu sharedAllocationMiB=%.3f liveBlockMiB=%.3f weakOwners=%zu expiredWeakOwners=%zu positionMiB=%.3f positionCapacityMiB=%.3f rendererFrame=%u",double(indices.bytes)/1048576,double(indices.physicalBytes)/1048576,indices.sharedBlocks,double(indices.sharedAllocationBytes)/1048576,double(indices.liveBlockBytes)/1048576,indices.weakOwners,indices.expiredWeakOwners,double(indices.positionBytes)/1048576,double(indices.positionCapacityBytes)/1048576,rendererFrame);}
        if(captureSampled)logf("MODEL index cache hits=%u misses=%u entries=%u bytes=%zu snapshotCacheHits=%u snapshotCacheMisses=%u snapshotCacheEntries=%zu snapshotCacheBytes=%zu revalidated=%u revalidationMismatches=%u trackedHits=%u avoidedReadBytes=%zu hashPrefixHits=%u hashFull=%u",replaySnapshots.indexCacheHits(),replaySnapshots.indexCacheMisses(),replaySnapshots.indexCacheEntries(),replaySnapshots.indexCacheBytes(),replaySnapshots.snapshotCacheHits(),replaySnapshots.snapshotCacheMisses(),replaySnapshots.snapshotCacheEntries(),replaySnapshots.snapshotCacheBytes(),replaySnapshots.snapshotRevalidated(),replaySnapshots.snapshotRevalidationMismatches(),replaySnapshots.trackedHits(),replaySnapshots.avoidedReadBytes(),replaySnapshots.maintenance().hashPrefixHits,replaySnapshots.maintenance().hashFull);
        if(captureSampled){const auto& m=replaySnapshots.maintenance();
            logf("MODEL snapshot maintenance snapshotEvictions=%u snapshotEvictedBytes=%zu snapshotMs=%.3f indexEvictions=%u indexEvictedBytes=%zu indexMs=%.3f missEvicted=%u missRevision=%u missUnknown=%u missCollision=%u missRevalidated=%u historySlots=2048",
                m.snapshotEvictions,m.snapshotEvictedBytes,m.snapshotMs,m.indexEvictions,m.indexEvictedBytes,m.indexMs,
                m.missEvicted,m.missRevision,m.missUnknown,m.missCollision,replaySnapshots.snapshotRevalidationMismatches());}
        if(captureSampled)logf("MODEL capture metadata batches=%u knownBuffers=%u fallbackBuffers=%u fastSnapshotHits=%u drawHandoffs=%u",replaySnapshots.metadataBatches(),replaySnapshots.metadataHits(),replaySnapshots.metadataFallbacks(),replaySnapshots.fastCacheHits(),captureSourceHandoffs);
        if(captureSampled)logf("MODEL_SOURCE_LIFETIME frame=%u participants=%zu sharedHits=%u limitMiB=%zu retainedBytes=%zu liveBytes=%zu liveObjects=%zu metadataBytes=%zu gpuIdentityHits=%u gpuReconstructedHits=%u globalIdentityBytes=%zu globalCpuBytes=%zu",rendererFrame,
            replaySnapshots.sharedSourceParticipants(),replaySnapshots.sharedSourceHits(),replaySnapshots.sharedSourceLimit()>>20,replaySnapshots.sharedSourceBytes(),replaySnapshots.sourceLiveBytes(),replaySnapshots.sourceLiveObjects(),replaySnapshots.sharedSourceMetadataBytes(),
            replayGpuCache.stats().identityHits,replayGpuCache.stats().reconstructedHits,NorthlightSourceIdentity::identityLiveBytes.load(std::memory_order_relaxed),NorthlightSourceIdentity::cpuLiveBytes.load(std::memory_order_relaxed));
        if(captureSampled)logf("MODEL frame capture skinnedCandidates=%u accepted=%u blendRejected=%u fadeRejected=%u projectionRejected=%u budgetRejected=%u snapshotRejected=%u acceptedSkinnedBytes=%zu acceptedOtherBytes=%zu rejectedReadBytes=%zu totalReadBytes=%zu compactedDraws=%u spanVertices=%zu uniqueVertices=%zu savedVertexBytes=%zu nearAdmitted=%u nearBytes=%zu nearRefused=%u nearSelf=%d nearReserve=%zu radiusGated=%u radiusGatedBytes=%zu cutRejected=%u cutYards=%.1f insideRefused=%u insideRefusedActors=%u noRoot=%u admissionCentre=%s rootReads=%u rootMemoHits=%u",skinnedCandidates,skinnedAccepted,skinnedBlendRejected,skinnedFadeRejected,skinnedProjectionRejected,skinnedBudgetRejected,skinnedSnapshotRejected,acceptedSkinnedBytes,acceptedOtherBytes,captureRejectedBytes,replaySnapshots.bytesRead(),replaySnapshots.compactedDraws(),replaySnapshots.sourceSpanVertices(),replaySnapshots.uniqueVertices(),replaySnapshots.savedVertexBytes(),nearAdmitted,nearBytes,nearRefused,int(nearAnchorReady&&nearAnchor.self),replaySnapshots.nearReserve(),
            admission.counters().radiusGated,admission.counters().radiusGatedBytes,admission.counters().cutRejected,admission.cutActive()?double(admission.cut()):0.0,admission.counters().insideRefusedDraws,admission.counters().insideRefusedActors,admission.counters().noRoot,
            admissionOn?(admission.centreSelf()?"self":"pivot"):"off",rootReads,rootMemoHits);
        if(captureSampled)logf("MODEL frame capture other blendRejected=%u fadeRejected=%u projectionRejected=%u budgetRejected=%u",otherBlendRejected,otherFadeRejected,otherProjectionRejected,otherBudgetRejected);
        if(captureSampled&&captureFrequency.QuadPart>0){
            const double ms=1000.0/double(captureFrequency.QuadPart);
            // Raw totals of the rotating subset; do not multiply by 16. Rejected
            // candidates contribute only the phases they actually reached.
            const auto& c=capturePhases.ticks;
            logf("MODEL capture phases stride=16 measured=%u accepted=%u clockReads=%u stateMs=%.3f projectionMs=%.3f snapshotMs=%.3f constantsMs=%.3f materialMs=%.3f finalizeMs=%.3f actorMs=%.3f alphaQueriesSkipped=%u shaderAlphaCaptured=%u shaderAlphaRejected=%u",capturePhaseDraws,capturePhaseAccepted,capturePhases.clockReads,c[CaptureState]*ms,c[CaptureProjection]*ms,c[CaptureSnapshot]*ms,c[CaptureConstants]*ms,c[CaptureMaterial]*ms,c[CaptureFinalize]*ms,c[CaptureActor]*ms,alphaStateQueriesSkipped,shaderAlphaCaptured,shaderAlphaRejected);
            const auto& a=actorPhases.ticks;
            logf("MODEL actor capture phases candidates=%u textureReads=%u packets=%u vertices=%u clockReads=%u probeMs=%.3f textureMs=%.3f packetMs=%.3f",actorPhaseCandidates,actorTextureReads,actorDraws,actorVerticesEvaluated,actorPhases.clockReads,a[0]*ms,a[1]*ms,a[2]*ms);
            const auto& u=replayUploadPhases.ticks;
            logf("MODEL GPU upload phases calls=%u fallbackRetries=%u clockReads=%u cacheMs=%.3f bulkMs=%.3f",replayUploadCalls,replayUploadFallbacks,replayUploadPhases.clockReads,u[0]*ms,u[1]*ms);
        }
        if(captureSampled){const auto& c=constantEpochProfile;const auto& s=constantEpochStats; /* 0.3.180 (C0): epoch/block counters of this sample frame; pose and self-check counters */
            logf("MODEL capture constants epochTests=%llu snapshotHits=%llu bytesAvoided=%llu poseFastHits=%llu blockTests=%llu blockShared=%llu blockRewrites=%llu blockCopies=%llu registersReused=%llu registersFetched=%llu poseBlockHits=%llu poseBytesAvoided=%llu selfChecks=%llu selfCheckMismatches=%llu",
            (unsigned long long)c.tests,(unsigned long long)c.hits,(unsigned long long)c.bytesAvoided,(unsigned long long)s.poseFastHits,
            (unsigned long long)c.blockTests,(unsigned long long)c.blockShared,(unsigned long long)c.blockRewrites,(unsigned long long)c.blockCopies,
            (unsigned long long)c.registersReused,(unsigned long long)c.registersFetched,(unsigned long long)s.poseBlockHits,
            (unsigned long long)s.poseBytesAvoided,(unsigned long long)s.selfChecks,(unsigned long long)s.selfCheckMismatches);
            char blocks[16*21]={};size_t used=0;
            for(unsigned n=0;n<NorthlightConstantEpoch::FloatBlocks&&used<sizeof blocks;++n)
                used+=std::snprintf(blocks+used,sizeof blocks-used,n?",%llu":"%llu",(unsigned long long)c.dirtyBlocks[n]);
            logf("MODEL capture constant blocks registersPerBlock=16 dirty=%s",blocks);}
        if(captureSampled)logf("MODEL triangle histogram bins=lt25,25to49,50to99,100to499,500to1999,ge2000 candidates=%u,%u,%u,%u,%u,%u skinned=%u,%u,%u,%u,%u,%u captured=%u,%u,%u,%u,%u,%u capturedSkinned=%u,%u,%u,%u,%u,%u smallEarly=%u smallGI=%u",
            replayTriangleBins[0],replayTriangleBins[1],replayTriangleBins[2],replayTriangleBins[3],replayTriangleBins[4],replayTriangleBins[5],
            skinnedTriangleBins[0],skinnedTriangleBins[1],skinnedTriangleBins[2],skinnedTriangleBins[3],skinnedTriangleBins[4],skinnedTriangleBins[5],
            acceptedTriangleBins[0],acceptedTriangleBins[1],acceptedTriangleBins[2],acceptedTriangleBins[3],acceptedTriangleBins[4],acceptedTriangleBins[5],
            acceptedSkinnedTriangleBins[0],acceptedSkinnedTriangleBins[1],acceptedSkinnedTriangleBins[2],acceptedSkinnedTriangleBins[3],acceptedSkinnedTriangleBins[4],acceptedSkinnedTriangleBins[5],smallShadowEarly,smallShadowGI);
        for(auto& n:replayTriangleBins)n=0;for(auto& n:skinnedTriangleBins)n=0;for(auto& n:acceptedTriangleBins)n=0;for(auto& n:acceptedSkinnedTriangleBins)n=0;
        smallShadowEarly=smallShadowGI=0;shadowSelectionDone=false;captureShortfall=false;
        if(captureSampled)logf("GRASS shadows candidates=%u attempts=%u captured=%u vertices=%u bytes=%zu maskRejected=%u rangeRejected=%u budgetRejected=%u blendRejected=%u range=64",detailCandidates,detailBudget.attempts,detailBudget.draws,detailBudget.vertices,detailBudget.bytes,detailMaskRejected,detailRangeRejected,detailBudgetRejected,detailBlendRejected);
        detailBudget={};detailCandidates=detailMaskRejected=detailRangeRejected=detailBudgetRejected=detailBlendRejected=0;
        lastCapturePhaseReads=capturePhases.clockReads+actorPhases.clockReads;clearCaptureDiagnostics();if(captureSampled)++capturePhaseSerial;
        skinnedCandidates=skinnedBlendRejected=skinnedFadeRejected=skinnedProjectionRejected=skinnedBudgetRejected=skinnedSnapshotRejected=skinnedAccepted=0;otherBlendRejected=otherFadeRejected=otherBudgetRejected=otherProjectionRejected=0;
        captureRejectedBytes=acceptedSkinnedBytes=acceptedOtherBytes=0;nearAdmitted=nearRefused=0;nearBytes=0;nearAnchorReady=false;
        admission.endFrame(size_t(captureBudgetMiB)*1048576,replaySnapshots.bytesRead());admissionOn=false;rootMemo.clear();rootReads=rootMemoHits=0;gatedBodies.clear(); /* 0.3.211 (task 31): the next frame's cut, after the log read this frame's counters; no-op when no skinned draw armed it */
        previousCacheHits=terrainBoundsCache.persistentHits();capturedConstantBytes=capturedConstantCalls=0;capturedSM1Draws=capturedRelativeDraws=0;
        terrainCaptureTicks=replayCaptureTicks=0;terrainCaptureCalls=terrainUPCalls=replayCaptureCalls=unknownCaptureCalls=captureSourceHandoffs=0;captureSampled=false;
#ifdef NORTHLIGHT_REPLAY_STAND
        captureProjectionGets=captureProjectionLoans=0;
        terrainCameraGets=terrainCameraBorrows=0;
        capturePacketReads=capturePacketLoans=capturePacketReadRegisters=capturePacketLoanRegisters=0;
        captureWmoProjectionBorrows=captureWmoPacketBorrows=captureWmoPacketRegisters=0;
#endif
        rejSmall=rejGate=rejCut=rejBudget=rejOther=0;
        valid=false;traceContext=0;traceFog=0;legacyFogKnown=false;shadowFrameReady=false;legacyFog=NorthlightLegacyFog::Constants{};
        // Bound retained vector capacities across changing scenes. Reuse storage,
        // never old geometry: each subsequent draw still re-reads every byte.
        // Give the current scene first claim on the pool, instead of letting
        // unused records from a previous crowded frame monopolize its budget.
        if(retainPool){
            for(auto& unused:freeReplays){pooledSnapshotBytes-=unused->retainedBytes();unused->snapshot=NorthlightDrawSnapshot::Mesh{};std::vector<D3DVERTEXELEMENT9>().swap(unused->elements);pooledSnapshotBytes+=unused->retainedBytes();}
            while(!freeReplays.empty()&&pooledSnapshotBytes+freeReplays.size()*sizeof(Replay)>replayPoolLimit()){
                pooledSnapshotBytes-=freeReplays.back()->retainedBytes();freeReplays.pop_back();
            }
            for(auto& p:replays)recycleReplay(p.release());replays.clear();
            for(auto& p:heldShadowReplays)recycleReplay(p.release());heldShadowReplays.clear();
        }else{
            if(prepareUnsettled()){for(auto& p:replays)recycleReplay(p.release());for(auto& p:heldShadowReplays)recycleReplay(p.release());} /* 0.3.177: quarantined */
            replays.clear();heldShadowReplays.clear();freeReplays.clear();pooledSnapshotBytes=0;}
        prepareFrameRelease(); /* 0.3.179: after the recycle */
        terrainBoundsCache.clearFrame();liveTerrainChunks.clear();frameTerrain.clear();frameTerrainVertices=frameTerrainIndices=0;pointLiveBatches.clear();pointReady=false;pointWork={};
        /* The snapshot frame advances on capture frames only. With the version provider set here every
           untracked hit revalidates anyway; without it the serial&15==frame&15 slice, counted in all
           frames, could never reach some serials when captures run every N frames. */
        if(captureMode!=CaptureSkipped)replaySnapshots.clearFrame();
        lastCaptureSkipped=captureMode==CaptureSkipped;actorCaptureDecided=false;actorJob.reset();captureMode=CaptureUndecided;actorVerticesEvaluated=actorDraws=actorSkippedAlpha=0;
        releaseExpiredMesh();
        if(geometryTargetsReady&&(!active||!active->bvh)&&(memoryPressure||geometryMemoryDemand.load(std::memory_order_relaxed))){
            releaseGeometryTargets();releasePointGPU();flashShadows.reset();
            logf("WORLD expired scene targets released reason=memory-pressure-or-demand");
        }
        reclaimInactiveCasters();
        if(++resourceMemoryProfileFrames%600==1)profileResourceMemory();
    }
    bool screenReady()const{return valid&&!failed&&!workerFault();}
    bool ready()const{return screenReady()&&active&&active->bvh;}
    void profileResourceMemory(){
        if(!NorthlightDiagnostics::enabled())return;
        const DWORD now=GetTickCount();if(resourceMemoryProfileAt&&now-resourceMemoryProfileAt<10000)return;resourceMemoryProfileAt=now;
        const auto started=std::chrono::steady_clock::now();
        {NorthlightMemoryFootprint::Counter cpu;probeAtlas.accountMirror(cpu);logCpuFootprint("probe-upload-mirror","resources",uploadedSerial,cpu);}
        using namespace NorthlightResourceMemory;
        Inventory inventory;
        // Surface aliases of texture levels and borrowed game/material targets
        // are excluded. Inspect each owned resource exactly once.
        for(auto& page:meshBuffers){inventory.buffer(page.vb,Geometry,D3DVERTEXBUFFER_DESC{});inventory.buffer(page.ib,Geometry,D3DINDEXBUFFER_DESC{});}
        for(auto* texture:materials)inventory.texture(texture,Geometry);
        if(pendingMesh){for(auto* texture:pendingMesh->textures)inventory.texture(texture,Geometry);inventory.texture(pendingMesh->materialUpload.texture(),Geometry);inventory.texture(pendingMesh->white,Geometry);}
        probeAtlas.visitTextures([&](auto* texture){inventory.texture(texture,ProbeTextures);});
        directionalMaps.visit([&](auto* texture){inventory.texture(texture,ShadowMaps);},
                              [&](auto* surface){inventory.surface(surface,ShadowMaps);});
        actorMaps.forEachTexture([&](IDirect3DTexture9* texture){inventory.texture(texture,ShadowMaps);});
        for(const auto& transition:shadowTransition.slots()){inventory.texture(transition.history,ShadowMaps);inventory.texture(transition.dynamic,ShadowMaps);}
        inventory.texture(directionalVisibility.texture(),ShadowMaps);
        shadowReplayAlpha.forEachSurface([&](auto* surface){inventory.surface(surface,ShadowMaps);});
        pointTargets.visit([&](auto* cube){inventory.cube(cube,ShadowMaps);},
            [&](auto* texture){inventory.texture(texture,ShadowMaps);},
            [&](auto* surface){inventory.surface(surface,ShadowMaps);});
        pointMixedFaces.visitTextures([&](auto* texture){inventory.texture(texture,ShadowMaps);});
        inventory.buffer(shadowPalette.buffer(),ShadowMaps,D3DVERTEXBUFFER_DESC{});
        shadowReplayIndices.buffers([&](auto* buffer){inventory.buffer(buffer,ShadowMaps,D3DINDEXBUFFER_DESC{});});
        flashShadows.visitTextures([&](auto* texture){inventory.texture(texture,ShadowMaps);});
        for(auto* texture:{screenLighting.raw().texture,fogScreen.raw().texture,fogScreen.scratch().texture,composite.scratchTexture(),screenLighting.baseline().texture,worldNormals.texture(),fogScreen.history().texture,screenLighting.prelit().texture})inventory.texture(texture,PostTargets);
        for(auto* texture:fogVolume.texture)inventory.texture(texture,PostTargets);
        inventory.texture(history.pending().light.texture,PostTargets);inventory.texture(history.produced().light.texture,PostTargets);
        inventory.texture(history.pending().distance.texture,PostTargets);inventory.texture(history.produced().distance.texture,PostTargets);
        inventory.surface(localLighting.profileTarget(),PostTargets);
        for(auto* texture:{regionalFog.texture(),regionalFog.heightAllocation(),moonFloorFog.texture(),screenFallbacks.white(),screenFallbacks.zero(),screenFallbacks.ao(),screenFallbacks.dither()})inventory.texture(texture,Noise);
        inventory.volume(regionalFogGpu.noise(),Noise);
        for(unsigned s=0;s<4;++s)inventory.buffer(replayBuffers.vertexBuffer(s),Dynamic,D3DVERTEXBUFFER_DESC{});
        inventory.buffer(replayBuffers.indexBuffer(),Dynamic,D3DINDEXBUFFER_DESC{});inventory.buffer(liveTerrainIndices.buffer(),Dynamic,D3DINDEXBUFFER_DESC{});
        gpuScene->native.buffers([&](auto* buffer){using Buffer=std::remove_pointer_t<decltype(buffer)>;if constexpr(std::is_same_v<Buffer,IDirect3DVertexBuffer9>)inventory.buffer(buffer,Geometry,D3DVERTEXBUFFER_DESC{});else inventory.buffer(buffer,Geometry,D3DINDEXBUFFER_DESC{});});
        NorthlightMemoryProfile::Gpu sample;
        sample.managed=inventory.pools[D3DPOOL_MANAGED];sample.defaults=inventory.pools[D3DPOOL_DEFAULT];sample.system=inventory.pools[D3DPOOL_SYSTEMMEM];
        sample.geometry=inventory.groups[Geometry];sample.probes=inventory.groups[ProbeTextures];sample.shadows=inventory.groups[ShadowMaps];sample.post=inventory.groups[PostTargets];sample.noise=inventory.groups[Noise];sample.dynamic=inventory.groups[Dynamic];
        sample.retiredMaterials=retiredMaterials.bytes();sample.materialReuseCpu=materialReuseBytes;sample.unknown=inventory.unknown;sample.complete=inventory.complete();
        sample.staticDefaultLedger=staticCasters.stats().residentBytes;sample.replayDefaultLedger=gpuScene->native.bytes();sample.liveTerrainDefault=liveTerrainGPU.bytes();
        sample.captureCpu=NorthlightSourceIdentity::cpuLiveBytes.load(std::memory_order_relaxed);sample.captureIdentity=NorthlightSourceIdentity::identityLiveBytes.load(std::memory_order_relaxed);NorthlightMemoryProfile::store().gpu(sample);
        logf("WORLD MEMORY cache-ledgers staticDefaultMiB=%.3f replayDefaultMiB=%.3f liveTerrainDefaultMiB=%.3f captureCpuUniqueMiB=%.3f captureIdentityUniqueMiB=%.3f extraGpuPoolsExcludedFromWorldInventory=1 backingAndResidencyNotMeasured=1",
             double(sample.staticDefaultLedger)/1048576,double(sample.replayDefaultLedger)/1048576,double(sample.liveTerrainDefault)/1048576,double(sample.captureCpu)/1048576,double(sample.captureIdentity)/1048576);
        float layer=0,aboveGround=0;bool inside=false;
        if(regionalFog.field()){const auto& field=*regionalFog.field();const float x=(context.camera[0]-field.originX)/NorthlightRegionalFog::Spacing,y=(context.camera[1]-field.originY)/NorthlightRegionalFog::Spacing;
            inside=x>=0&&y>=0&&x<NorthlightRegionalFog::N-1&&y<NorthlightRegionalFog::N-1;
            if(inside){const auto& cell=field.texels[unsigned(y)*NorthlightRegionalFog::N+unsigned(x)];layer=cell.height;aboveGround=context.camera[2]-cell.ground;}}
        logf("FOG STATE enabled=%u field=%u cameraInField=%u layerTag=%.4f aboveGround=%.2f groundCells=%u airCells=%u geometry=%u sunSource=%u moonSource=%u ambient=%.4f,%.4f,%.4f map=\"%s\" fieldMap=\"%s\" independentField=1",
             unsigned(effects.fog),unsigned(regionalFog.field()!=nullptr),unsigned(inside),layer,aboveGround,regionalFog.field()?regionalFog.field()->groundCells:0,regionalFog.field()?regionalFog.field()->airCells:0,unsigned(fogGeometryReady),unsigned(NorthlightGI::dot(sourceColors[0],sourceColors[0])>1e-10f),unsigned(NorthlightGI::dot(sourceColors[1],sourceColors[1])>1e-10f),context.ambient[0],context.ambient[1],context.ambient[2],lastRequest.map.c_str(),regionalFog.map().c_str());
        logf("WORLD MEMORY resources scope=world-owned knownManagedLogicalMiB=%.3f knownDefaultLogicalMiB=%.3f knownSystemLogicalMiB=%.3f geometryMiB=%.3f probeTextureMiB=%.3f shadowMapMiB=%.3f postTargetMiB=%.3f noiseMiB=%.3f dynamicMiB=%.3f retiredMaterialLedgerMiB=%.3f materialReuseCpuMiB=%.3f complete=%u unknown=%u inventoryMs=%.3f basis=logical-resource-size residentVramMeasured=0 managedSystemBackingMeasured=0 driverPaddingAndRenameCopiesExcluded=1 staticAndReplayCachesReportedSeparately=1",
             double(inventory.pools[D3DPOOL_MANAGED])/1048576,double(inventory.pools[D3DPOOL_DEFAULT])/1048576,double(inventory.pools[D3DPOOL_SYSTEMMEM])/1048576,
             double(inventory.groups[Geometry])/1048576,double(inventory.groups[ProbeTextures])/1048576,double(inventory.groups[ShadowMaps])/1048576,double(inventory.groups[PostTargets])/1048576,double(inventory.groups[Noise])/1048576,double(inventory.groups[Dynamic])/1048576,
             double(retiredMaterials.bytes())/1048576,double(materialReuseBytes)/1048576,unsigned(inventory.complete()),inventory.unknown,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
    }
    // task 42 (MEMMAP northlight line): the render thread's own view, no lock and no counters beyond plain reads. Render thread only
    // (`active` and the mesh pools are its own). Not included, as they need the streamer's or the builder's locks or are worker locals:
    // static shadow cpu/gpu, local scene retained/flat/bvh, replay caches.
    int memmapTally(char* out,size_t size)const{
        const double pool=double(meshStore.poolBytes()),idle=double(meshStore.idleBytes());
        double plan=0,payload=0,bvh=0;
        if(active){if(active->meshPlan){plan=double(active->meshPlan->cpuBytes());payload=double(active->meshPlan->payload.bytes());}if(active->bvh)bvh=double(active->bvh->retainedBytes());}
        return std::snprintf(out,size,"meshPoolMiB=%.1f meshPlanCpuMiB=%.1f uploadPayloadLiveMiB=%.1f materialReuseMiB=%.1f giBvhMiB=%.1f geometryDemandMiB=%.1f meshIdleMiB=%.1f captureCpuUniqueMiB=%.1f captureIdentityUniqueMiB=%.1f capturePoolLimitMiB=%.1f",pool/1048576.0,plan/1048576.0,payload/1048576.0,double(materialReuseBytes)/1048576.0,bvh/1048576.0,
            double(geometryMemoryDemand.load(std::memory_order_relaxed))/1048576.0,idle/1048576.0,
            double(NorthlightSourceIdentity::cpuLiveBytes.load(std::memory_order_relaxed))/1048576.0,double(NorthlightSourceIdentity::identityLiveBytes.load(std::memory_order_relaxed))/1048576.0,double(replaySnapshots.sharedSourceLimit())/1048576.0);
    }
    const char* lastSkipReason()const{return skipReason;} /* 0.3.169: valid after render() returned false */
    bool captureSkippedLastFrame()const{return lastCaptureSkipped;} /* for the sampled CPU profile, logged after endFrame */
    unsigned capturePhaseReadsLastFrame()const{return lastCapturePhaseReads;} /* 0.3.150: clock reads of the capture-phase subset (inside the capture timers), likewise */
    bool hasContext()const{return valid&&!failed&&!workerFault();}
    bool hdrExposureContext(std::string& map,float camera[3],float& reference)const{
        if(!hasContext())return false;map=lastRequest.map;memcpy(camera,context.camera,12);
        float extra[3]{};NorthlightNightFloor::nightAmbient(quality.nightBrightness,NorthlightNightFloor::weight(celestialValid,celestialValid?sourceDirections[0].z:0.f),context.ambient,extra);
        const float ambient=.2126f*(context.ambient[0]+extra[0])+.7152f*(context.ambient[1]+extra[1])+.0722f*(context.ambient[2]+extra[2]);
        reference=.18f*std::clamp(ambient/.3f,.05f,1.5f);return true;
    }
    NorthlightQuality::Preset qualityPreset()const{return quality.preset;}
    const NorthlightQuality::Settings& surfaceSettings()const{return quality;}
    IDirect3DTexture9* materialDetail=nullptr,*materialOcclusion=nullptr,*modelNormals=nullptr;float materialBumpGain=0,materialBumpDistance=48,materialOcclusionDistance=8;bool materialCaptureResident=false;
    NorthlightMaterialProfile* materialProfile=nullptr;
    void setMaterialProfiler(NorthlightMaterialProfile* p){materialProfile=p;}
    void setCaptureRasterStateSource(void* context,bool(*reader)(void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*)){captureRasterContext=context;captureRasterReader=reader;}
    // Explicit diagnostics only; normal-view capture never changes a quality
    // setting, debug mode, shadow refresh or temporal-history decision.
    bool requestNormalGpuDiagnosticCapture(){if(gpuDiagnosticCaptures==~0u||gpuNormalDiagnosticRequested)return false;gpuNormalDiagnosticRequested=true;return true;}
    bool diagnosticCaptureInLastRender()const{return gpuDiagnosticFrame;}
    bool emptyDiagnosticView(IDirect3DSurface9* target,UINT w,UINT h,int debug){
        SavedState saved(d,&stateBlocks);if(!saved.ok)return false;
        if(!composite.clearDiagnostic(d,target,w,h))return false;
        composited=true;lastRenderDebug=debug;shadowsComposited=false;fogGeometryReady=false;return true;
    }
    bool needsDiagnosticFallback()const{return gpuNormalDiagnosticRequested||(gpuDiagnosticFrame&&!composited);}
#ifdef NORTHLIGHT_REPLAY_STAND
    void capturePointCubes(const std::string& directory,unsigned frame,UINT width,UINT height){
        if(!pointShadowAuditEnabled())return;
        NorthlightLightingCapture::Capture capture(d,directory,frame,frames,frame,width,height,"point-shadow-cubes");
        capture.pointSources(pointTargets.slots(),GetTickCount(),pointReady);
        capture.finish(pointReady&&composited&&lastRenderDebug==0);
    }
#endif
    void captureParityLocalSources(const std::string& directory,unsigned frame,UINT width,UINT height){
        NorthlightLightingCapture::Capture capture(d,directory,frame,frames,frame,width,height,"frame-parity-lights");
        capture.sources(atmosphere.lights,effects.fog);
        capture.finish(composited&&lastRenderDebug==0);
    }
    bool parityGeometryReady()const{return fogGeometryReady&&(!directionalShadowWork()||shadowFrameReady);}
    void captureProducts(const std::string& directory,unsigned capture,unsigned rendererFrame,UINT w,UINT h,bool current,bool fogOnly=false){
        const bool ready=current&&composited&&lastRenderDebug==0;
        auto image=[&](const char* name,IDirect3DSurface9* surface,bool valid,UINT x,UINT y,D3DFORMAT format){NorthlightWorldDiagnostics::product(d,surface,directory,name,capture,valid,std::max(1u,x),std::max(1u,y),format,rendererFrame);};
        if(!fogOnly){
            const unsigned source=NorthlightGI::dot(sourceColors[0],sourceColors[0])>1e-10f?0:1;
            const bool shadowCurrent=ready&&fogGeometryReady&&shadowFrameReady&&directionalShadowWork()&&(menuDomain||nativeShadowOwned)&&NorthlightGI::dot(sourceColors[source],sourceColors[source])>1e-10f;
            image("shadow-near",directionalMaps.current(source*2).surface,shadowCurrent,1024,1024,D3DFMT_R32F);
            image("shadow-far",directionalMaps.current(source*2+1).surface,shadowCurrent,1024,1024,D3DFMT_R32F);
            image("normals",worldNormals.surface(),ready,w/2,h/2,D3DFMT_A16B16G16R16F);
#ifdef NORTHLIGHT_REPLAY_STAND
            if(std::getenv("NORTHLIGHT_RETRACE_LEGACY_ACTOR_INSET"))
                image("shadow-actor",actorMaps.depth(source).surface,shadowCurrent&&actorValid[source],NorthlightActorShadowMap::Size,NorthlightActorShadowMap::Size,D3DFMT_R32F);
            NorthlightWorldDiagnostics::textureProduct(d,modelNormals,directory,"model-normals",capture,ready&&modelNormals,w,h,D3DFMT_A16B16G16R16F,rendererFrame);
#endif
            image("baseline",screenLighting.baseline().surface,ready,w/2,h/2,D3DFMT_A16B16G16R16F);
            image("current-local-light",screenLighting.raw().surface,ready&&diagnosticLocalLighting,w/2,h/2,D3DFMT_A16B16G16R16F);
            image("temporal-light",history.produced().light.surface,ready,w/2,h/2,D3DFMT_A16B16G16R16F);
            image("distance",history.produced().distance.surface,ready,w/2,h/2,D3DFMT_R32F);
            NorthlightWorldDiagnostics::textureProduct(d,materialDetail,directory,"material-height",capture,ready&&materialDetail,w,h,D3DFMT_A16B16G16R16F,rendererFrame);
            NorthlightWorldDiagnostics::textureProduct(d,materialOcclusion,directory,"material-occlusion",capture,ready&&materialOcclusion,w,h,D3DFMT_A16B16G16R16F,rendererFrame);
        }
        image("fog",fogScreen.final(diagnosticFogResolved).surface,ready&&effects.fog&&(menuDomain||regionalFog.field()),w/2,h/2,D3DFMT_A16B16G16R16F);
    }
    bool captureSkippedDiagnostic(unsigned rendererFrame,UINT w,UINT h){
        if(!needsDiagnosticFallback())return false;
        if(gpuNormalDiagnosticRequested){if(gpuDiagnosticCaptures==~0u){gpuNormalDiagnosticRequested=false;return false;}++gpuDiagnosticCaptures;gpuNormalDiagnosticRequested=false;}
        gpuDiagnosticFrame=true;
        try{if(gpuDiagnosticDirectory.empty())gpuDiagnosticDirectory=NorthlightWorldDiagnostics::directory(root);
            if(!gpuDiagnosticDirectory.empty()){captureProducts(gpuDiagnosticDirectory,gpuDiagnosticCaptures,rendererFrame,w,h,false);
                NorthlightWorldDiagnostics::product(d,nullptr,gpuDiagnosticDirectory,"scene",gpuDiagnosticCaptures,false,w,h,D3DFMT_A8R8G8B8,rendererFrame);
                NorthlightLightingCapture::Capture record(d,gpuDiagnosticDirectory,gpuDiagnosticCaptures,frames,rendererFrame,w,h);
                record.skipped("world",0,0,"effect-pass-not-rendered");record.finish(true);}}
        catch(...){/* Diagnostics must never fault or enable an effect. */}
        logf("WORLD GPU skipped capture=%u rendererFrame=%u empty=1 diagnostic=1 fpsEligible=0",gpuDiagnosticCaptures,rendererFrame);
        return true;
    }
    void pollMaterialProfile(){materialPixelAudit.poll();}
    void setMaterialDetail(IDirect3DTexture9* texture,float gain,float distance,IDirect3DTexture9* occlusion=nullptr,float occlusionDistance=8,bool captureResident=false){materialDetail=texture;materialBumpGain=gain;materialBumpDistance=distance;materialOcclusion=occlusion;materialOcclusionDistance=occlusionDistance;materialCaptureResident=captureResident||texture!=nullptr;}
    void setHdrBaseCompositeEnabled(bool enabled){composite.permitBase(enabled);}
    void setTemporalZeroWeightEnabled(bool enabled){history.permitZeroWeight(enabled);}
    void setModelNormals(IDirect3DTexture9* texture){modelNormals=texture;}
    static constexpr float MoonTerrainSpecularDistance=128.f;
    float sunSurfaceSpecularGain()const{
        if(!hasContext()||!celestialValid||!paletteFrameValid||!NorthlightQuality::foreverCelestial(quality)||
           NorthlightGI::dot(sourceColors[0],sourceColors[0])<=1e-10f)return 0;
        return framePalette.sunSurfaceSpecular*std::clamp((sourceWeights[0]-.6f)*2.5f,0.f,1.f);
    }
    float moonTerrainSpecularGain()const{
        return hasContext()&&celestialValid&&paletteFrameValid&&NorthlightQuality::foreverCelestial(quality)&&
            NorthlightGI::dot(sourceColors[1],sourceColors[1])>1e-10f
            ?framePalette.moonTerrainSpecular*(1-sourceWeights[0])*
                (framePalette.sunSurfaceSpecular>0?std::clamp((.6f-sourceWeights[0])*2.5f,0.f,1.f):1.f):0.f;
    }
    std::uint64_t materialLightStamp()const{return (materialLightRevision<<1)|std::uint64_t(hasContext());}
    bool materialLight(float* light,float* inverseView=nullptr)const{
        if(inverseView)std::memcpy(inverseView,context.inverseView,48);
        std::fill(light,light+4,0.f);if(!hasContext())return false;
        const unsigned body=NorthlightGI::dot(sourceColors[1],sourceColors[1])>NorthlightGI::dot(sourceColors[0],sourceColors[0])?1:0;
        if(NorthlightGI::dot(sourceColors[body],sourceColors[body])<=1e-10f)return true;
        const V direction=sourceDirections[body];
        for(unsigned i=0;i<3;++i)light[i]=direction.x*context.inverseView[i*4]+direction.y*context.inverseView[i*4+1]+direction.z*context.inverseView[i*4+2];
        light[3]=float(body+1);return true;
    }
    bool surfaceContext(NorthlightSurfaceFrame& frame)const{
        if(!hasContext())return false;
        std::memcpy(frame.c[1],projection,12);std::memcpy(frame.c[4],context.inverseView,64);
        std::memcpy(frame.c[8],context.ambient,12);std::memcpy(frame.c[9],context.direct,12);
        const unsigned body=NorthlightGI::dot(sourceColors[1],sourceColors[1])>NorthlightGI::dot(sourceColors[0],sourceColors[0])?1:0;
        if(NorthlightGI::dot(rawSourceDirections[body],rawSourceDirections[body])>.5f){std::memcpy(frame.c[9],&sourceColors[body].x,12);}return true;
    }
    float weatherLensHeat()const{
        if(!hasContext()||!active||!active->bvh||active->map!=lastRequest.map)return 0;
        float player[3]={};const float* self=nullptr;
        if(pivotSelfCaptured&&frames-pivotSelfFrame<=4&&actorShadowHistory.selfHold()>0){
            const auto* p=actorShadowHistory.selfAt();player[0]=p[0];player[1]=p[1];player[2]=p[2]+1.5f;self=player;
        }
        return NorthlightWeatherLensHeat::sample(atmosphere.lights.sources,context.camera,self,[&](const float* origin,const float* source){
            const V start=vec(origin),delta=vec(source)-start;const float distance=std::sqrt(NorthlightGI::dot(delta,delta));
            return distance>.1f&&active->bvh->occluded(start,delta*(1.f/distance),distance-.1f);
        });
    }
    bool supplementalWeatherOutdoors(){
        if(!hasContext()||!active||!active->bvh||active->map!=lastRequest.map)return false;
        if(weatherShelterUnavailable)return false;
        if(!weatherShelter)try{weatherShelter=std::make_unique<NorthlightWeatherShelter::Worker>();}catch(...){weatherShelterUnavailable=true;logf("WEATHER supplemental shelter worker unavailable; native precipitation retained");return false;}
        return weatherShelter->sample(active->bvh,vec(context.camera),active->map,GetTickCount64());
    }
    unsigned frameTraceFog()const{return traceFog;}
    unsigned frameTraceContext()const{return traceContext;} /* 0.3.200 (frame trace) */
    bool actorShadowsEnabled()const{return quality.actorShadows!=0;}
    bool commandStream()const{return quality.commandStream!=0;} /* 0.3.192 (CS): the replay-thread stream was requested; creation-time key, see stream_hooks.h */
    unsigned blobShadowStrength()const{return quality.blobShadows?quality.blobShadowStrength:0;}
    bool blobShadowsEnabled()const{return quality.blobShadows!=0;}
    // 0.3.198 (rain): the smoothed weather state, set once per frame at the frame boundary (Device::finishFrameImpl); no effect reads it yet.
    NorthlightWeather::State weatherState{},worldWeatherState{};
    void setWeather(const NorthlightWeather::State& s){worldWeatherState=s;if(!menuDomain)weatherState=s;}
    bool isMenu()const{return menuDomain;}
    void setEffectCoverage(IDirect3DTexture9* texture,bool emission=false){effectCoverage=texture;effectEmission=emission;}
    void setUnlitMask(IDirect3DSurface9* depthStencil){composite.setUnlitMask(depthStencil);}
    void setPrelitMask(IDirect3DSurface9* depthStencil){composite.setPrelitMask(depthStencil);}
    void setAuthoredMask(IDirect3DSurface9* depthStencil,unsigned levels=1){composite.setAuthoredMask(depthStencil,levels);}
    bool authoredLightingShader(IDirect3DVertexShader9* shader,bool model,bool wmo)const{
        return (model||wmo)&&nativeLightingShaders.count(shader);
    }
    bool authoredReceiver(IDirect3DVertexShader9* shader,bool model,unsigned* roomLevel=nullptr){
        return authoredLightingShader(shader,model,wmoShaders.count(shader)!=0)&&authoredReceiverLighting(model,roomLevel);
    }
    bool authoredReceiverLighting(bool model,unsigned* roomLevel){
        if(!hasContext())return false;
        float native[8];const auto* mirror=constantEpochSource.mirror;
        const float* values=native;
        if(mirror&&mirror->active()&&mirror->vsFloatKnown[10]&&mirror->vsFloatKnown[11])values=mirror->vsFloat[10];
        else if(FAILED(d->GetVertexShaderConstantF(10,native,2)))return false;
        // Reuse only the arithmetic. The live bank and shader proof still
        // describe this draw, including fallback reads with the mirror off.
        auto& memo=authoredLightingMemo[model];
        if(memo.revision!=materialLightRevision||std::memcmp(memo.bank,values,sizeof memo.bank)){
            memo.revision=materialLightRevision;
            std::memcpy(memo.bank,values,sizeof memo.bank);
            memo.authored=NorthlightUnlitReceiver::lightingOverride(memo.bank,context.ambient,context.direct,model);
            memo.roomLevel=memo.authored?NorthlightUnlitReceiver::roomAmbientLevel(memo.bank):0;
        }
        if(memo.authored&&roomLevel)*roomLevel=memo.roomLevel;
        return memo.authored;
    }
    void setNameMask(IDirect3DSurface9* depthStencil){composite.setNameMask(depthStencil);} /* 0.3.212 (task 32): the world depth-stencil holding this frame's name marks (non-owning, for the next render() only; null: no marks) */
    void setRainMask(IDirect3DTexture9* t){rainMask=t;} /* 0.3.202 (rain): non-owning, for the next render() only */
    const NorthlightWeather::State& weather()const{return weatherState;}
    // 0.3.198 (rain): the settings (Weather 0/1, RainFog 0..2) and the per-frame scalars derived from them and weather():
    // render() and the celestial renderers read this one place; all zero (identity) with Weather=0 or no weather.
    unsigned weatherSetting()const{return quality.weather;}
    unsigned rainFogSetting()const{return quality.rainFog;}
    // 0.3.200 (gpu budget): GpuBudgetMs (0 = off) and the controller's level for the next frames; off forces level 0 (the full path, unchanged).
    unsigned gpuBudgetMs()const{return quality.gpuBudgetMs;}
    void setGpuBudgetLevel(unsigned level){const auto next=quality.gpuBudgetMs?std::min(level,NorthlightGpuBudget::MaxLevel):0u;if(next!=gpuBudgetLevel)fogScreen.invalidateHistory();gpuBudgetLevel=next;}
    // task 45: ActorShadowCpuMs (0 = off) and the CPU controller's level; off forces level 0. effectiveActorShadowRadius() is the one place the radius is read
    // for work: the capture admission and the selection use it (the setting itself stays in the logs that describe the setting).
    unsigned actorShadowCpuMs()const{return quality.actorShadowCpuMs;}
    void setCpuBudgetLevel(unsigned level){cpuBudgetLevel=quality.actorShadowCpuMs?std::min(level,NorthlightGpuBudget::MaxLevel):0u;}
    unsigned effectiveActorShadowRadius()const{return NorthlightGpuBudget::actorShadowRadius(quality.actorShadowRadius,cpuBudgetLevel);}
    bool rainBlendSetting()const{return quality.weather!=0;} /* 0.3.199 (rain): the draw hook's alpha-blended rain streaks and rain-only mist skip, with Weather=1 */
    NorthlightWeatherEffects::Frame weatherEffects()const{return NorthlightWeatherEffects::derive(weatherState,quality.weather,quality.rainFog);}
    bool nameMask()const{return quality.nameMask!=0;} bool nameMaskDebug()const{return quality.nameMaskDebug!=0;} /* 0.3.212 (task 32): read once at device creation */
    bool contactAO()const{return quality.contactAO!=0;}
    unsigned aoMethod()const{return quality.aoMethod;}
    unsigned aoQuality()const{return quality.aoQuality;}
    float aoStrength()const{return .6f*float(quality.aoStrength)/100.f;}
    float aoRadius()const{return float(quality.aoRadius)/100.f;}
    bool equipmentLightsOn()const{return pipelineActive&&quality.localLights&&(menuDomain||quality.equipmentLights!=0)&&quality.localLightLimit!=0;}
    NorthlightParticleLights::Frame& particleFrame(){return particleLights;}
    bool particleProbeBudgetSpent()const{return particleLights.probeBudgetSpent()&&active&&particleMap==active->map;}
    bool particleContext(float* inverseView,float* camera,float* projectionOut){
        if(!equipmentLightsOn()||!hasContext()||!active)return false;
        if(particleMap!=active->map){particleLights.reset();particleMap=active->map;}
        std::memcpy(inverseView,context.inverseView,64);std::memcpy(camera,context.camera,12);std::memcpy(projectionOut,projection,12);return true;
    }
    void registerNativePointShader(IDirect3DVertexShader9* shader,const DWORD* words,size_t count,bool model,bool terrainVariant=false){
        nativeLightingShaders.erase(shader);
        if(shader&&(model||wmoShaders.count(shader))&&NorthlightUnlitReceiver::nativeLightingShader(reinterpret_cast<const uint32_t*>(words),count,model))nativeLightingShaders.insert(shader);
        // EquipmentLights can be enabled live after these shaders were created.
        // Keep only its small proven layout, never shader bytecode or a COM owner.
        bool rgbOnly=false;const bool terrain=terrainVariant||terrainShaders.count(shader)!=0;nativeTerrainPointShaders.erase(shader);
        const unsigned lights=NorthlightEquipmentLightProof::analyze(reinterpret_cast<const uint32_t*>(words),count,&rgbOnly,terrain);
        if(terrain&&NorthlightDiagnostics::enabled())logf("NATIVE_TERRAIN_SHADER major=%u minor=%u pointSlots=%u rgbOnly=%u variant=%u",words&&count?unsigned((words[0]>>8)&255):0,words&&count?unsigned(words[0]&255):0,lights,unsigned(rgbOnly),unsigned(terrainVariant));
        if(!shader||!lights){equipmentShaders.erase(shader);nativePointShaders.erase(shader);nativePointOwners.erase(shader);return;}
        nativePointShaders[shader]=lights;nativePointOwners[shader]=rgbOnly;
        if(terrain)nativeTerrainPointShaders.insert(shader);
        if(model)equipmentShaders[shader]=lights;else equipmentShaders.erase(shader);
        if(model&&equipmentLightsOn()&&NorthlightDiagnostics::enabled())logf("EQUIPMENT shader proven nativePointCount=%u shader=%p",lights,static_cast<void*>(shader));
    }
    void captureEquipmentLights(IDirect3DVertexShader9* shader,IDirect3DSurface9* mainDepth){
        if(!equipmentLightsOn()||!hasContext()||!active)return;
        const auto it=equipmentShaders.find(shader);if(it==equipmentShaders.end()||!it->second)return;
        if(equipmentMap!=active->map){equipmentLights.reset();equipmentMap=active->map;}
        const bool actor=isSkinnedShader(shader);
        const auto* mirror=constantEpochSource.mirror;
        const bool cached=NorthlightEquipmentLights::cachedBank(mirror);
        if(!equipmentCapture.take(actor,cached))return;
        NorthlightDrawSnapshot::Ref<IDirect3DSurface9> ds;
        if(!mainDepth||(cached?mirror->depth!=mainDepth:FAILED(d->GetDepthStencilSurface(ds.out()))||ds.p!=mainDepth)){++equipmentCapture.domainRejected;return;}
        // The proven constants are view-space: require the current main-world
        // projection before using this frame's inverse view (portraits excluded).
        float readProjection[16],readBank[44];const float* q=cached?mirror->vsFloat[2]:readProjection;
        if(!cached&&FAILED(d->GetVertexShaderConstantF(2,readProjection,4))){++equipmentCapture.domainRejected;return;}
        for(unsigned i=0;i<16;++i)if(!std::isfinite(q[i])){++equipmentCapture.domainRejected;return;}
        if(std::fabs(q[0]-projection[0])>.005f||std::fabs(q[5]-projection[1])>.005f||std::fabs(q[14]-projection[2])>.005f||std::fabs(q[15])>.001f){++equipmentCapture.domainRejected;return;}
        const float* bank=cached?mirror->vsFloat[17]:readBank;
        if((!cached&&FAILED(d->GetVertexShaderConstantF(17,readBank,11)))||!equipmentLights.admit(bank,it->second,actor))return;
        const auto now=visualTime();
        NorthlightEquipmentLights::Native values[4];bool valid[4];NorthlightEquipmentLights::decodeBank(bank,it->second,context.inverseView,context.camera,values,valid);
        if(NorthlightLocalLightTrace::auditInputs()){
            equipmentBankAudit.add(bank,44);
            for(unsigned i=0;i<it->second;++i)if(valid[i]){
                equipmentDrawAudit.add(values[i].position,3);
                equipmentDrawAudit.add(values[i].color,3);
                equipmentDrawAudit.add(values[i].attenuation,3);
            }
        }
        for(unsigned i=0;i<it->second;++i){
            if(!valid[i]){++equipmentCapture.rejected;continue;}
            ++equipmentCapture.decoded;
            equipmentLights.observe(values[i],now,active->localLights,context.camera,0,0,0,active->localLightIndex.get());
        }
    }
    const NorthlightNativeLights::Point* nativeDirectSource(const NorthlightLocalLights::Light& selected,const NorthlightNativeLights::Snapshot& scene)const{
        if(!active||selected.kind!=2||(selected.flags&NorthlightLocalLights::Flash))return nullptr;
        if((selected.sourceId>>52)==0xfec){
            for(unsigned i=0;i<equipmentLights.count;++i){const auto& entry=equipmentLights.entries[i];if((0xfec0000000000000ull|entry.id)!=selected.sourceId||!entry.nativeId)continue;
                for(unsigned k=0;k<scene.count;++k)if(scene.lights[k].id==entry.nativeId)return &scene.lights[k];}
            return nullptr;
        }
        const auto attached=lightAttachments.nativeBindings.find(selected.sourceId);
        if(attached!=lightAttachments.nativeBindings.end()&&attached->second.present){
            for(unsigned i=0;i<scene.count;++i)if(scene.lights[i].id==attached->second.id)return &scene.lights[i];
            return nullptr;
        }
        const NorthlightLocalLights::Light* reference=nullptr;
        for(const auto& light:active->localLights)if(light.sourceId==selected.sourceId){reference=&light;break;}
        if(!reference)return nullptr;
        const auto* native=NorthlightNativeDirect::source(*reference,scene);if(!native)return nullptr;
        // A coincident second authored source cannot borrow the same native slot.
        for(const auto& light:active->localLights)if(light.sourceId!=reference->sourceId&&light.kind==2&&
            NorthlightEquipmentLights::distance2(light.position,reference->position)<.005f*.005f&&
            NorthlightNativeDirect::source(light,scene)==native)return nullptr;
        return native;
    }
    std::array<float,3> nativeDirectEmitter(const NorthlightLocalLights::Light& selected,const NorthlightNativeLights::Point& native)const{
        if((selected.sourceId>>52)==0xfec)return NorthlightNativeLights::emitterPosition(native);
        const auto attached=lightAttachments.nativeBindings.find(selected.sourceId);
        if(attached!=lightAttachments.nativeBindings.end()&&attached->second.present&&attached->second.id==native.id){
            const auto* position=attached->second.light.position;return {position[0],position[1],position[2]};
        }
        return {native.position[0],native.position[1],native.position[2]};
    }
    static void nativeColdTrace(void*,const NorthlightNativeLocalLighting::ColdTrace& t)noexcept{
        try{logf("NATIVE_DIRECT cold rendererFrame=%u count=%u slots=%u,%u,%u stage=%s success=%u sourceVS=%016llx sourceVSKnown=%u sourcePS=%016llx sourcePSKnown=%u pom=%u shared=%u dual=%u displacement=%u gpuHeight=%u vertexReused=%u pixelReused=%u vertexCreates=%u pixelCreates=%u duplicateRetry=%u hrKnown=%u hr=%08lx firstPixelCreateHr=%08lx legacyFilter=%u coverageRecipe=%u scope=cold-pair bounded=128",
            t.frame,t.count,t.slots[0],t.slots[1],t.slots[2],NorthlightNativeLocalLighting::coldStageName(t.stage),unsigned(t.success),(unsigned long long)t.sourceVSHash,unsigned(t.sourceVSHashKnown),(unsigned long long)t.sourcePSHash,unsigned(t.sourcePSHashKnown),unsigned(t.pom),unsigned(t.shared),unsigned(t.dual),unsigned(t.displacement),unsigned(t.gpu),unsigned(t.vertexReused),unsigned(t.pixelReused),t.vertexCreates,t.pixelCreates,unsigned(t.duplicateRetry),unsigned(t.hrKnown),(unsigned long)t.hr,(unsigned long)t.firstPixelCreateHr,unsigned(t.legacyFilter),t.coverageRecipe);}catch(...){}
    }
    bool nativeReceiverPair(IDirect3DVertexShader9* source,IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel)const{
        return nativeDirectRenderer&&nativeDirectRenderer->provesReceiverPair(source,vertex,pixel);
    }
    bool ensureNativeDiagnosticCapture(unsigned rendererFrame){
        if(nativeDiagnosticCapture)return nativeDiagnosticCapture->active();
        if(!gpuNormalDiagnosticRequested||!active)return false;
        nativeDiagnosticMap=active->map;if(gpuDiagnosticDirectory.empty())gpuDiagnosticDirectory=NorthlightWorldDiagnostics::directory(root);
        D3DVIEWPORT9 vp{};d->GetViewport(&vp);
        nativeDiagnosticCapture=std::make_unique<NorthlightLightingCapture::Capture>(d,gpuDiagnosticDirectory,gpuDiagnosticCaptures+1,frames,rendererFrame,vp.Width,vp.Height,"native-inputs");
        nativeDiagnosticWmoImages=nativeDiagnosticTerrainDraws=0;
        NorthlightNativeLights::Snapshot scene;NorthlightNativeLights::frame(active->map.c_str(),context.camera,scene);nativeDiagnosticCapture->nativeScene(scene);
        return nativeDiagnosticCapture->active();
    }
    template<class Draw> HRESULT captureNativeSceneDraw(unsigned rendererFrame,unsigned primitives,Draw submit){
        if(!gpuNormalDiagnosticRequested||!active)return submit();
        unsigned token=0;try{
            if(NorthlightLightingCapture::traceTargetMatches(d,width,height)&&ensureNativeDiagnosticCapture(rendererFrame))
                token=nativeDiagnosticCapture->traceBefore(rendererFrame,primitives);
        }catch(...){}
        const HRESULT result=submit();if(token&&nativeDiagnosticCapture)nativeDiagnosticCapture->traceAfter(token,result);return result;
    }
    template<class Draw> HRESULT captureNativeWmoDraw(unsigned rendererFrame,IDirect3DSurface9* mainDepth,IDirect3DVertexShader9* sourceVS,IDirect3DPixelShader9* sourcePS,bool prepared,Draw submit){
        const bool terrain=terrainShaders.count(sourceVS)!=0;
        if(!gpuNormalDiagnosticRequested||(!terrain&&!wmoShaders.count(sourceVS))||!mainDepth)return submit();
        if(nativeDiagnosticCapture&&nativeDiagnosticTerrainDraws<4&&nativeDiagnosticCapture->nativeDraws_>=124)return submit();
        NorthlightDrawSnapshot::Ref<IDirect3DSurface9> depth;float q[16];DWORD writes=FALSE,blend=TRUE;
        if(FAILED(d->GetDepthStencilSurface(depth.out()))||depth.p!=mainDepth||FAILED(d->GetVertexShaderConstantF(terrain?4:2,q,4)))return submit();
        for(float value:q)if(!std::isfinite(value))return submit();
        if(std::fabs(q[0]-projection[0])>.005f||std::fabs(q[5]-projection[1])>.005f||std::fabs(q[terrain?11:14]-projection[2])>.005f||std::fabs(q[15])>.001f||
            (!terrain&&(FAILED(d->GetRenderState(D3DRS_ZWRITEENABLE,&writes))||!writes||FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||blend)))return submit();
        unsigned token=0;try{if(ensureNativeDiagnosticCapture(rendererFrame)){const uint64_t sources[4]{};
            token=nativeDiagnosticCapture->nativeBefore(rendererFrame,0,~0u,prepared,nativeDiagnosticWmoImages<4,sourceVS,sourcePS,sources,terrain||nativeDiagnosticTerrainDraws>=4||nativeDiagnosticCapture->nativeImages_<4);
            if(token&0x80000000u)++nativeDiagnosticWmoImages;}}catch(...){}
        const HRESULT result=submit();if(nativeDiagnosticCapture)nativeDiagnosticCapture->nativeAfter(token,0,result,terrain?"native-terrain-without-proven-point-bank":"native-wmo-without-proven-point-bank");return result;
    }
    void requestNativeExtra(bool value){nativeExtraRequested=value;}
    bool ownsNativeExtra()const{return nativePointSubmit.ownsExtra()&&nativeExtraOwners;}
    void nativeExtraRgb(bool terrain,float* bank){
        if(nativePointSubmit.ownsExtra())NorthlightNativeDirect::ownExtraBank(terrain,bank,context.inverseView,context.camera,nativeDirectBindings.data(),nativeDirectBindingCount);
    }
    template<class Draw> HRESULT nativeLocalDraw(unsigned rendererFrame,IDirect3DSurface9* mainDepth,Draw submit,bool prepared=false,const NorthlightNativePOM* pom=nullptr,IDirect3DVertexShader9* materialSource=nullptr,const NorthlightNativeDisplacement* displacement=nullptr,const NorthlightEffectCoverage::NativePixel* coverage=nullptr){
        const bool nativeTelemetry=profileSampled();
        if(prepared&&!pom&&!displacement&&!gpuNormalDiagnosticRequested){if(nativeTelemetry)++nativeDirectPreparedBypass;return submit();}
        if(!pipelineActive||!effects.shadows||!quality.localLights||!quality.localLightLimit||!hasContext()||!active)return submit();
        const auto* mirror=constantEpochSource.mirror;NorthlightDrawSnapshot::Ref<IDirect3DVertexShader9> vsOwner;NorthlightDrawSnapshot::Ref<IDirect3DPixelShader9> psOwner;
        IDirect3DVertexShader9* vs=mirror&&mirror->active()&&mirror->vertexShaderKnown?mirror->vertexShader:nullptr;
        IDirect3DPixelShader9* ps=mirror&&mirror->active()&&mirror->pixelShaderKnown?mirror->pixelShader:nullptr;
        bool privateMismatch=false;
        if(pom||displacement){
            if(FAILED(d->GetVertexShader(vsOwner.out()))||FAILED(d->GetPixelShader(psOwner.out())))return submit();
            privateMismatch=pom?!pom->matches(vsOwner.p,psOwner.p):!displacement->matches(vsOwner.p);if(privateMismatch&&!gpuNormalDiagnosticRequested){if(nativeTelemetry)++nativeDirectPrivateBypass;return submit();}
            vs=vsOwner.p;ps=psOwner.p;
        }
        if(!vs){if(FAILED(d->GetVertexShader(vsOwner.out())))return submit();vs=vsOwner.p;}
        IDirect3DVertexShader9* sourceVS=pom?pom->originalVS:displacement&&displacement->originalVS?displacement->originalVS:prepared&&materialSource?materialSource:vs;
        const auto shader=nativePointShaders.find(sourceVS);if(shader==nativePointShaders.end()){
            if(!gpuNormalDiagnosticRequested||privateMismatch)return submit();
            if(!ps&&SUCCEEDED(d->GetPixelShader(psOwner.out())))ps=psOwner.p;
            return captureNativeWmoDraw(rendererFrame,mainDepth,sourceVS,pom?pom->originalPS:ps,prepared,submit);
        }
        const bool terrainReceiver=nativeTerrainPointShaders.count(sourceVS)!=0;
        const auto* snapshot=NorthlightStream::activeSnapshot;
        const uint64_t stamp=snapshot&&snapshot->nativeLights.valid?(uint64_t(snapshot->nativeLights.scene)<<32)|snapshot->nativeLights.time:0;
        const unsigned sceneCount=snapshot?snapshot->nativeLights.count:0;
        if(nativeDirectFrame!=rendererFrame||nativeDirectMap!=active->map||nativeDirectStamp!=stamp||nativeDirectSceneCount!=sceneCount||nativeDirectSnapshot!=snapshot||nativeDirectWorld!=active.get()||nativeDirectCubeRevision!=nativeCubeRevision||std::memcmp(nativeDirectView,context.inverseView,64)){
            nativeDirectBankMemo.reset();
            try{nativeDirectMap=active->map;}catch(const std::bad_alloc&){return submit();}
            nativeDirectFrame=rendererFrame;nativeDirectStamp=stamp;nativeDirectSceneCount=sceneCount;nativeDirectSnapshot=snapshot;nativeDirectWorld=active.get();std::memcpy(nativeDirectView,context.inverseView,64);
            nativeDirectCubeRevision=nativeCubeRevision;
            if(menuDomain)nativeDirectScene=menuLights;else NorthlightNativeLights::frame(active->map.c_str(),context.camera,nativeDirectScene);nativeDirectBindingCount=0;nativeExtraOwners=false;
            if(!NorthlightNativeLights::wellFormed(nativeDirectScene))nativeDirectScene.clear();
            if(quality.pointShadows)for(const auto& candidate:pointTargets.slots()){
                if(!candidate.cube||!candidate.ready||!candidate.schedule.usable||candidate.map!=active->map)continue;
                const auto* native=(candidate.selected.sourceId>>52)==0xfec?nativeDirectSource(candidate.selected,nativeDirectScene):nullptr;
                const NorthlightLocalLights::Light* authored=nullptr;
                if(candidate.selected.kind==2&&!(candidate.selected.flags&NorthlightLocalLights::Flash)&&(candidate.selected.sourceId>>52)!=0xfec){
                    for(const auto& light:active->localLights)if(light.sourceId==candidate.selected.sourceId){authored=&light;break;}
                    if(authored)for(const auto& light:active->localLights)if(light.sourceId!=authored->sourceId&&NorthlightNativeDirect::sameAuthored(light,*authored)){authored=nullptr;break;}
                    if(!authored||NorthlightEquipmentLights::distance2(candidate.selected.position,authored->position)>=.005f*.005f||!NorthlightNativeDirect::close(candidate.selected.attenuationEnd,authored->attenuationEnd,.0001f))continue;
                }
                if(!authored&&(!native||NorthlightEquipmentLights::distance2(candidate.selected.position,native->position)>=.005f*.005f))continue;
                NativeDirectBinding* duplicate=nullptr;for(unsigned i=0;native&&i<nativeDirectBindingCount;++i)if(nativeDirectBindings[i].native==native){duplicate=&nativeDirectBindings[i];break;}
                if(duplicate){duplicate->light.cube=nullptr;continue;}
                auto& binding=nativeDirectBindings[nativeDirectBindingCount++];binding={};binding.native=native;binding.authored=authored;binding.selected=&candidate.selected;binding.cube=&candidate.cube;
                binding.ready=&candidate.ready;binding.usable=&candidate.schedule.usable;binding.gain=&candidate.selectionGain;binding.updated=&candidate.schedule.updatedAt;binding.map=&candidate.map;binding.sourceId=candidate.selected.sourceId;
                std::memcpy(binding.selectedPosition,candidate.selected.position,sizeof binding.selectedPosition);
                binding.light.cube=candidate.cube;binding.light.cubeIsWhite=pointCubeIsWhite(candidate);binding.light.range=candidate.selected.attenuationEnd;std::memcpy(binding.light.position,candidate.cubeOrigin(),12);std::memcpy(binding.light.inverseView,context.inverseView,64);
            }
            // Last completed selection is available during native submissions.
            // A fresh scene value or proven native bank must agree with it.
            const uint32_t ownerNow=GetTickCount();
            if(screenLighting.raw().texture&&worldNormals.texture()&&composite.standardShader()&&localLighting.directShader()&&localLightMap==active->map)for(unsigned i=0;i<atmosphere.lights.count;++i){
                const auto& selected=atmosphere.lights.sources[i];const auto* native=nativeDirectSource(selected,nativeDirectScene);
                NorthlightNativeLights::Point observed;const auto resolved=native?nativeDirectEmitter(selected,*native):std::array<float,3>{};
                float gain=native?NorthlightNativeDirect::ownershipGain(selected,*native,resolved.data()):0;bool drawWitness=false;
                if(!native)for(unsigned j=0;j<equipmentLights.count;++j){const auto& entry=equipmentLights.entries[j];
                    gain=NorthlightNativeDirect::observedOwnershipGain(selected,entry,ownerNow,nativeDirectScene);if(!(gain>0))continue;
                    std::copy_n(entry.source.position,3,observed.position);std::copy_n(entry.source.color,3,observed.color);std::copy_n(entry.source.attenuation,3,observed.attenuation);
                    native=&observed;drawWitness=true;break;
                }
                if(!(gain>0))continue;
                NativeDirectBinding* binding=nullptr;
                for(unsigned j=0;j<nativeDirectBindingCount;++j)if(nativeDirectBindings[j].sourceId==selected.sourceId||nativeDirectBindings[j].native==native){binding=&nativeDirectBindings[j];break;}
                if(!binding){binding=&nativeDirectBindings[nativeDirectBindingCount++];*binding={};binding->sourceId=selected.sourceId;
                    if(drawWitness){binding->observed=observed;binding->native=&binding->observed;binding->drawWitness=true;}else binding->native=native;}
                binding->ownerGain=gain;nativeExtraOwners=true;
            }
        }
        if(!nativeDirectBindingCount&&!gpuNormalDiagnosticRequested)return submit();
        const bool cached=terrainReceiver?NorthlightEquipmentLights::cachedTerrainBank(mirror):NorthlightEquipmentLights::cachedBank(mirror);NorthlightDrawSnapshot::Ref<IDirect3DSurface9> depth;
        if(!mainDepth||(cached?mirror->depth!=mainDepth:FAILED(d->GetDepthStencilSurface(depth.out()))||depth.p!=mainDepth))return submit();
        float projectionBank[16],lightBank[44];const float* q=cached?mirror->vsFloat[terrainReceiver?4:2]:projectionBank;
        if(!cached&&FAILED(d->GetVertexShaderConstantF(terrainReceiver?4:2,projectionBank,4)))return submit();
        for(unsigned i=0;i<16;++i)if(!std::isfinite(q[i]))return submit();
        if(std::fabs(q[0]-projection[0])>.005f||std::fabs(q[5]-projection[1])>.005f||std::fabs(q[terrainReceiver?11:14]-projection[2])>.005f||std::fabs(q[15])>.001f)return submit();
        float terrainOriginal[36],terrainOwned[36],terrainView[16];
        const float* bank=cached&&!terrainReceiver?mirror->vsFloat[17]:lightBank;
        if(terrainReceiver){
            if(cached)std::copy_n(mirror->vsFloat[28],36,terrainOriginal);
            else if(FAILED(d->GetVertexShaderConstantF(0,terrainView,4))||FAILED(d->GetVertexShaderConstantF(28,terrainOriginal,9)))return submit();
            if(!NorthlightNativeDirect::terrainBank(terrainOriginal,cached?mirror->vsFloat[0]:terrainView,lightBank))return submit();}
        else if(!cached&&FAILED(d->GetVertexShaderConstantF(17,lightBank,11)))return submit();
        NorthlightNativeLocalDraw planned,second,third;const NativeDirectBinding* selected=nullptr;unsigned matched=0,capturedSources=0;uint64_t captureSources[4]{};bool captureFocus=gpuNormalDiagnosticRequested&&terrainReceiver&&(!nativeDiagnosticCapture||nativeDiagnosticTerrainDraws<4);nativeDirectSlots+=shader->second;if(nativeTelemetry)++nativeDirectSlotCounts[std::min(shader->second,4u)];
        bool bankHit=false;const auto& bankMatch=nativeDirectBankMemo.resolve(bank,shader->second,context.inverseView,context.camera,nativeDirectBindings.data(),nativeDirectBindingCount,bankHit);
        if(nativeTelemetry){if(bankHit)++nativeDirectBankHits;else ++nativeDirectBankMisses;}
        const auto& nativeValues=bankMatch.values;const auto& nativeValid=bankMatch.valid;const auto& nativeBindings=bankMatch.bindings;
        float ownedRGB[16],originalRGB[16];unsigned ownerMask=0;
        const auto owner=nativePointOwners.find(sourceVS);
        const bool canOwn=!privateMismatch&&(!prepared||pom||displacement)&&owner!=nativePointOwners.end()&&owner->second;
        const unsigned candidateMask=canOwn||gpuNormalDiagnosticRequested?NorthlightNativeDirect::ownedBank(bank,shader->second,bankMatch,nativeDirectBindings.data(),ownedRGB):0;
        bool extraCanOwn=false;
        if(canOwn&&(candidateMask||nativeExtraRequested)){
            if(!ps&&SUCCEEDED(d->GetPixelShader(psOwner.out())))ps=psOwner.p;
            auto* sourcePS=pom?pom->originalPS:coverage&&coverage->matches(ps)?coverage->source:ps;
            const auto pixelOwner=nativePointPixelOwners.find(sourcePS);
            DWORD writes=FALSE,blend=TRUE,sourceBlend=0,destinationBlend=0,blendOperation=0;
            const bool known=SUCCEEDED(d->GetRenderState(D3DRS_ZWRITEENABLE,&writes))&&SUCCEEDED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend));
            const bool layered=known&&blend&&wmoShaders.count(sourceVS)&&
                SUCCEEDED(d->GetRenderState(D3DRS_SRCBLEND,&sourceBlend))&&SUCCEEDED(d->GetRenderState(D3DRS_DESTBLEND,&destinationBlend))&&SUCCEEDED(d->GetRenderState(D3DRS_BLENDOP,&blendOperation));
            if(pixelOwner!=nativePointPixelOwners.end()&&pixelOwner->second&&known&&
               NorthlightNativeDirect::receiverOwnership(terrainReceiver,layered,writes!=FALSE,blend!=FALSE,sourceBlend,destinationBlend,blendOperation)){
                extraCanOwn=true;ownerMask=candidateMask;if(ownerMask)std::copy_n(bank,16,originalRGB);
            }
        }
        if(terrainReceiver&&ownerMask){std::copy_n(terrainOriginal,36,terrainOwned);for(unsigned slot=0;slot<3;++slot)if(ownerMask&(1u<<slot))std::copy_n(ownedRGB+4*slot,3,terrainOwned+12*slot+4);}
        // Scope the constant change to the real submit, including private pairs.
        // Captures above the submit still see the original game's input bank.
        const auto nativeOwnedBefore=nativePointSubmit.work().draws;
        const NorthlightNativePointSubmit::Bank submitBank{ownerMask,terrainReceiver,
            terrainReceiver?terrainOriginal:originalRGB,terrainReceiver?terrainOwned:ownedRGB};
        const auto ownedSubmit=[&]{return nativePointSubmit.draw(d,submitBank,extraCanOwn,failed,submit);};
        for(unsigned k=0;k<4;++k)if((ownerMask&(1u<<k))&&nativeDirectBindings[unsigned(nativeBindings[k])].drawWitness)++nativeObservedOwnedSlots;
        if(gpuNormalDiagnosticRequested&&candidateMask)for(unsigned slot=0;slot<shader->second;++slot)if(candidateMask&(1u<<slot)){
            captureFocus=true;captureSources[capturedSources++]=nativeDirectBindings[unsigned(nativeBindings[slot])].sourceId;
        }
        for(unsigned slot=0;slot<shader->second;++slot){if(!nativeValid[slot]){if(nativeTelemetry)++nativeDirectMissReasons[0];continue;}const auto& value=nativeValues[slot];
            if(ownerMask&(1u<<slot))continue; // Renderer direct already owns this source, including its cube visibility.
            if(gpuNormalDiagnosticRequested)for(const auto& candidate:pointTargets.slots())if(candidate.ready&&candidate.schedule.usable)captureFocus|=NorthlightEquipmentLights::distance2(value.position,candidate.selected.position)<.005f*.005f;
            const NativeDirectBinding* binding=nativeBindings[slot]>=0?&nativeDirectBindings[unsigned(nativeBindings[slot])]:nullptr;
            if(!binding||!binding->light.cube||!*binding->ready||!*binding->usable||!std::isfinite(*binding->gain)||*binding->cube!=binding->light.cube||
                *binding->map!=active->map||binding->selected->sourceId!=binding->sourceId||std::memcmp(binding->selected->position,binding->selectedPosition,sizeof binding->selectedPosition)||binding->selected->attenuationEnd!=binding->light.range){++nativeDirectMissingCube;
                if(nativeTelemetry){const unsigned reason=!binding?(nativeBindings[slot]==-2?2u:1u):
                    !binding->light.cube||!*binding->ready||!*binding->usable?3u:!std::isfinite(*binding->gain)?4u:*binding->cube!=binding->light.cube?5u:6u;
                    ++nativeDirectMissReasons[reason];}
                continue;}
            if(gpuNormalDiagnosticRequested){if(capturedSources<4)captureSources[capturedSources++]=binding->sourceId;captureFocus=true;}
            if(++matched>3){++nativeDirectMultiple;continue;} // Four-source endpoints exceed SM3 varyings.
            auto& output=matched==1?planned:matched==2?second:third;if(matched==1)selected=binding;
            output=binding->light;output.slot=slot;output.rendererFrame=rendererFrame;output.gain=std::clamp(*binding->gain,0.f,1.f);
        }
        if(nativeTelemetry)++nativeDirectMatchCounts[std::min(matched,4u)];
        if(!matched&&!gpuNormalDiagnosticRequested)return ownedSubmit();if(matched)++nativeDirectMatched;
        if(!ps){if(FAILED(d->GetPixelShader(psOwner.out())))return ownedSubmit();ps=psOwner.p;}if(!ps)return ownedSubmit();
        unsigned nativeCaptureToken=0;
        if(gpuNormalDiagnosticRequested&&(terrainReceiver||!nativeDiagnosticCapture||nativeDiagnosticTerrainDraws>=4||nativeDiagnosticCapture->nativeDraws_<124)){
            try{ensureNativeDiagnosticCapture(rendererFrame);
                unsigned captureSlot=matched?planned.slot:~0u;if(!matched&&candidateMask){captureSlot=0;while(!(candidateMask&(1u<<captureSlot)))++captureSlot;}
                nativeCaptureToken=nativeDiagnosticCapture->nativeBefore(rendererFrame,selected?selected->sourceId:captureSources[0],captureSlot,prepared,captureFocus,sourceVS,pom?pom->originalPS:ps,captureSources,terrainReceiver||nativeDiagnosticTerrainDraws>=4||nativeDiagnosticCapture->nativeImages_<4);
                if(nativeCaptureToken&&terrainReceiver)++nativeDiagnosticTerrainDraws;
                if(nativeCaptureToken)for(unsigned slot=0;slot<shader->second;++slot){const auto& value=nativeValues[slot];const NativeDirectBinding* binding=nativeBindings[slot]>=0?&nativeDirectBindings[unsigned(nativeBindings[slot])]:nullptr;const char* status=nativeValid[slot]?"no-binding":"decode-rejected";const bool ambiguous=nativeBindings[slot]==-2;
                    int physical=-1;uint64_t source=0;
                    if(ambiguous)status="ambiguous-binding";
                    else if(binding){source=binding->sourceId;for(unsigned i=0;i<pointTargets.slots().size();++i)if(binding->selected==&pointTargets.slots()[i].selected){physical=int(i);break;}
                        status=(ownerMask&(1u<<slot))?"renderer-owned-direct":(candidateMask&(1u<<slot))?"selected-native-source-unmodified":!binding->light.cube?"missing-cube":!*binding->ready?"not-ready":!*binding->usable?"not-usable":!std::isfinite(*binding->gain)?"invalid-gain":*binding->cube!=binding->light.cube?"cube-identity-changed":*binding->map!=active->map?"map-changed":binding->selected->sourceId!=binding->sourceId||std::memcmp(binding->selected->position,binding->selectedPosition,sizeof binding->selectedPosition)||binding->selected->attenuationEnd!=binding->light.range?"source-identity-changed":"matched-ready";}
                    nativeDiagnosticCapture->nativeSlot(nativeCaptureToken,shader->second,slot,nativeValid[slot],value.position,value.color,value.attenuation,status,source,physical);
                }
            }catch(...){/* Explicit capture failure must not change the submitted draw. */}
        }
        if(terrainReceiver||!matched||privateMismatch||(prepared&&!pom&&!displacement)){const HRESULT result=ownedSubmit();if(nativeDiagnosticCapture)nativeDiagnosticCapture->nativeAfter(nativeCaptureToken,0,result,privateMismatch?"material-private-pair-mismatch":nativePointSubmit.work().draws!=nativeOwnedBefore?"renderer-owned-native-source":candidateMask?"selected-native-source-unmodified":terrainReceiver?"terrain-source-unmodified":!matched?"no-matched-ready-source":"prepared-displacement-bypass");return result;}
        if(!nativeDirectRenderer)try{nativeDirectRenderer=std::make_unique<NorthlightNativeLocalLighting>(d);nativeDirectRenderer->setSamplerStateSource(nativeSamplerContext,nativeSamplerReader);}catch(const std::bad_alloc&){return ownedSubmit();}
        nativeDirectRenderer->setColdTraceSink(NorthlightDiagnostics::enabled()&&NorthlightQuality::renderProfile(quality)?nativeColdTrace:nullptr);
        unsigned captureColdBefore=0,captureDeferredBefore=0;uint64_t captureFallbackBefore=0;
        if(nativeCaptureToken){captureColdBefore=nativeDirectRenderer->coldPairs;captureDeferredBefore=nativeDirectRenderer->deferredPairs;captureFallbackBefore=nativeDirectRenderer->fallbacks;}
        const auto before=nativeDirectRenderer->draws,multiBefore=nativeDirectRenderer->multiDraws,tripleBefore=nativeDirectRenderer->tripleDraws;const HRESULT result=nativeDirectRenderer->draw(planned,vs,ps,ownedSubmit,matched>1?&second:nullptr,pom,displacement,matched>2?&third:nullptr,coverage);
        const unsigned corrected=nativeDirectRenderer->tripleDraws!=tripleBefore?3:nativeDirectRenderer->multiDraws!=multiBefore?2:nativeDirectRenderer->draws!=before?1:0;
        if(nativeCaptureToken)nativeDiagnosticCapture->nativeAfter(nativeCaptureToken,corrected,result,corrected&&selected->authored?"corrected-authored-draw-slot":nullptr,nativeDirectRenderer->coldPairs-captureColdBefore,nativeDirectRenderer->deferredPairs-captureDeferredBefore,nativeDirectRenderer->fallbacks-captureFallbackBefore,true);
        if(matched>corrected)nativeDirectMultiple+=std::min(matched,3u)-corrected;
        if(NorthlightDiagnostics::enabled()&&nativeDirectTraces<16){const DWORD now=GetTickCount();++nativeDirectTraces;logf("NATIVE_DIRECT draw rendererFrame=%u supported=%u nativeSlots=%u shadowedSlot=%u shadowedCount=%u otherMatchedKept=%u nativeId=%llu sourceId=%llu cubeAgeMs=%lu visibility=pcf4 cubeUsable=1 rendererDirect=unchanged",rendererFrame,unsigned(corrected!=0),shader->second,corrected?planned.slot:~0u,corrected,matched-corrected,(unsigned long long)(selected->native?selected->native->id:0),(unsigned long long)selected->selected->sourceId,(unsigned long)uint32_t(now-*selected->updated));}
        return result;
    }
    void observeLightAttachments(const Replay& replay,const CaptureShader& metadata){
        if(!quality.localLights||!quality.localLightLimit||!active||!active->lightAttachments||active->lightAttachments->empty()||!metadata.program||!metadata.program->skinned||
           !(metadata.program->kernel.skin||metadata.program->kernel.commutedSkin()||NorthlightRigidGeometry::oneBoneTemplate(*metadata.program))||metadata.program->paletteBase<0)return;
        lightAttachments.bind(active->lightAttachments);
        const auto& mesh=replay.mesh();if(!mesh.primitiveCount||mesh.vertexCount<3)return;
        const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
        if(!declarationCache.get(replay.decl,elements,count))return;
        for(UINT i=0;i<count&&elements[i].Stream!=0xff;++i){const auto& e=elements[i];
            if(e.Usage!=D3DDECLUSAGE_POSITION||e.UsageIndex||e.Type!=D3DDECLTYPE_FLOAT3||e.Stream>=4)continue;
            const auto& stream=mesh.streams[e.Stream];std::array<float,9> witness{};
            for(unsigned v=0;v<3;++v){if(mesh.indexed&&mesh.indices.size()<=v)return;
                const unsigned index=mesh.indexed?mesh.indices[v]:v;if(index>=mesh.vertexCount)return;
                const size_t offset=size_t(index)*stream.stride+e.Offset;
                if(offset>stream.bytes.size()||12>stream.bytes.size()-offset)return;
                std::memcpy(witness.data()+3*v,stream.bytes.data()+offset,12);}
            lightAttachments.observe(witness,replay.constants,unsigned(metadata.program->paletteBase),context.inverseView);return;
        }
    }
    unsigned spellLayerSetting()const{return quality.spellLayer;} /* task 34: SpellLayer, SpellLayerClasses and SpellTrace are read once at device creation */
    unsigned spellLayerClassesSetting()const{return quality.spellLayerClasses;}
    unsigned spellTraceSetting()const{return quality.spellTrace;}
    void spellFogHint(const float game[3],bool bare){spellFogHold.addHint(game,bare);} /* SpellFog: a falling-ramp game constant the layer saw; fitted with the next publish (spell_fog.h Hold) */
    bool weatherMemorySetting()const{return quality.weatherMemory!=0;} /* WeatherMemory: the game's weather state drives the weather effects (weather_memory.h); 0 = the texture detector only */
    bool depthFuncAware()const{return quality.depthFuncAware!=0;} /* DepthFuncAware: read once at device creation */
    unsigned spellFogSetting()const{return quality.spellFog;} /* SpellFog (task 34): read once with the others */
    const NorthlightSpellFog::Frame& spellFogFrame()const{return spellFogHold.out;} /* Northlight's fog toward the view axis (previous frame's estimate while the next frame's records are made): the spell layer folds it into the recorded fog constants */
    bool frameDrawGates()const{return quality.frameDrawGates!=0;} /* 0.3.187: read once at device creation */ /* 0.3.158: ActorShadows=0 leaves actor shadows to the game's blobs */
    const float* legacyFogParameters()const{return legacyFog.parameters;}
    const NorthlightLegacyFog::Constants& legacyFogConstants()const{
        if(!legacyFogKnown&&active&&legacyFogHold.has&&legacyFogHold.map==active->map&&
           DWORD(visualTime()-legacyFogHold.lastUpdate)<=NorthlightLegacyFog::Hold::HoldMs)return legacyFogHold.held;
        return legacyFog;
    }
    bool hasOriginalFog()const{return legacyFogKnown;}
    const NorthlightCelestialProfiles::Profile& celestialPalette(const char* map,const float* camera){
        // Freeze once per frame: early disc, late halo, direct/GI and fog cannot
        // observe different worker publications or temporal transition states.
        if(paletteFrameValid&&paletteFrameMap==map)return framePalette;
        std::shared_ptr<const PaletteRegion> region;
        {std::lock_guard<std::mutex> lock(mutex);region=publishedPaletteRegion;}
#if NORTHLIGHT_DEV_TUNING
        devPoll(map,camera,region&&region->map==map?NorthlightRegionalFog::zoneAt(region->region,camera[0],camera[1]):0);
#endif
        auto target=celestialProfiles.fallback;
        if(region&&region->map==map)target=NorthlightCelestialProfiles::sample(celestialProfiles,region->region,map,camera[0],camera[1]);
        framePalette=paletteTransition.update(target,map,camera[0],camera[1],double(visualTime())*.001);
        paletteFrameMap=map;paletteFrameValid=true;
        DWORD now=GetTickCount();if(NorthlightDiagnostics::enabled()&&(!paletteLogAt||now-paletteLogAt>=10000)){paletteLogAt=now;
            uint32_t zone=region&&region->map==map?NorthlightRegionalFog::zoneAt(region->region,camera[0],camera[1]):0;
            logf("CELESTIAL palette map=\"%s\" zone=%u moonDisc=(%.3f %.3f %.3f) lightMix=%.3f/%.3f strength=%.3f/%.3f",map,zone,framePalette.disc[1][0],framePalette.disc[1][1],framePalette.disc[1][2],framePalette.mix[0],framePalette.mix[1],framePalette.strength[0],framePalette.strength[1]);
        }return framePalette;
    }
    // The celestial renderer keeps the native solar body/glare colours separate
    // from the reference-look world/fog palette. The sky phase uses the prior
    // published frame; failed native reads retain the bounded palette fallback.
    const NorthlightSunHue::GlowHue& glowHue()const{return celestialGlowHueFrame;}
    // the horizon-haze constants of the last world composite (optical depth 0 = no haze:
    // fog off, HorizonHaze=0, unknown fog colour, or no world frame yet). The veil reads haze[3], shape[2].
    const NorthlightHorizonHaze::Constants& horizonHazeConstants()const{return hazeFrame;}
    // CPU estimate (0..1) of the 128-unit fog volume's transmittance on a sky pixel toward
    // the sun, i.e. the fog.a the composite applies there. 1 with fog off or where the field has no air.
    float skyTransmittance()const{return skyTransmittanceFrame;}
    float underwaterScale()const{return underwaterFade.scale();}
    const NorthlightForever::SkyKey* celestialSkyPalette()const{
        return skyEnvironment.skyValid&&(isMenu()||NorthlightQuality::foreverCelestial(quality))?&skyEnvironment.sky:nullptr;
    }
    // The first foreground draw need not establish a world context. Reuse only
    // its validated projection/palette; read this frame's camera without marking
    // world capture valid or depending on the visibility of a celestial disc.
    bool skyBackgroundContext(float* inverseView,float* projectionOut)const{
        if(!pipelineActive||failed||workerFault()||!celestialSkyPalette())return false;
        if(hasContext())memcpy(inverseView,context.inverseView,64);
        else{
            char map[64]={};NorthlightWorldCamera::Camera camera;
            if(lastRequest.map.empty()||!NorthlightWorldCamera::readSkyPhase(map,projection[2],camera)||lastRequest.map!=map)return false;
            memcpy(inverseView,camera.inverseView,64);
        }
        memcpy(projectionOut,projection,12);return true;
    }
    bool celestialContext(NorthlightCelestial::Context& out,float* inverseView,float* projectionOut)const{
        if(!hasContext()||!celestialValid)return false;
        out=celestial;out.horizonSine=apparentHorizon.valid?apparentHorizon.value:0.f;memcpy(inverseView,context.inverseView,64);memcpy(projectionOut,projection,12);return true;
    }
    unsigned auroraStrength()const{return quality.auroraStrength;}
    const std::string& auroraMap()const{return lastRequest.map;}
    NorthlightAurora::Style auroraStyle()const{
        return NorthlightAurora::style(lastRequest.map.c_str(),quality.auroraStrength,hasContext()&&celestialValid,celestial.dayFraction,NorthlightQuality::foreverCelestial(quality));
    }
    // Render-thread handoff; worker requests retain immutable observations.
    void setAuroraSource(std::shared_ptr<const NorthlightAuroraSource::Snapshot> source,const std::string& map){
        if(source&&!NorthlightAuroraSource::valid(*source))source.reset();
        auroraSource=std::move(source);auroraSourceMap=map;
    }
    bool recognizesWmo(IDirect3DVertexShader9* shader)const{return wmoShaders.count(shader)!=0;}
    bool isWorldShader(IDirect3DVertexShader9* shader)const{return terrainShaders.count(shader)||captureShaders.count(shader)||wmoShaders.count(shader);}
    bool isSkinnedShader(IDirect3DVertexShader9* shader)const{auto it=actorPrograms.find(shader);return it!=actorPrograms.end()&&it->second->skinned;}
    // A main-world actor can be the only visible geometry while looking up.
    // Read this frame's camera/light; never carry the last terrain view forward.
    bool menuContext(IDirect3DVertexShader9* shader){
        auto it=captureShaders.find(shader);
        const auto reject=[&](unsigned reason){
            const auto* snapshot=NorthlightStream::activeSnapshot;
            const unsigned key=(std::min)(snapshot?snapshot->triggerKind:0u,4u)*10u+(std::min)(snapshot?snapshot->menuCaptureReject:0u,9u);
            static std::uint64_t reported[5]{};const auto bit=std::uint64_t(1)<<key;
            if(!(reported[reason]&bit)){reported[reason]|=bit;
            logf("MENU context rejected reason=%u shader=%p known=%u skinned=%u projection=%u lighting=%u snapshotTrigger=%u menuValid=%u captureReject=%u",reason,static_cast<void*>(shader),unsigned(it!=captureShaders.end()),unsigned(it!=captureShaders.end()&&it->second.skinned),it==captureShaders.end()?0u:it->second.projectionKind,unsigned(nativeLightingShaders.count(shader)!=0),snapshot?snapshot->triggerKind:0u,snapshot?snapshot->menu.valid:0u,snapshot?snapshot->menuCaptureReject:0u);
        }return false;};
        if(it==captureShaders.end()||!it->second.skinned||it->second.projectionKind!=2||!nativeLightingShaders.count(shader))return reject(1);
        NorthlightMenu::Snapshot menu;NorthlightNativeLights::Snapshot lights;
        if(const auto* snapshot=NorthlightStream::activeSnapshot){menu=snapshot->menu;lights=snapshot->nativeLights;}
#ifndef NORTHLIGHT_REPLAY_STAND
        else if(NorthlightMenu::capture(NorthlightWorldContext::readSelf,menu)){
            char map[64];menu.identity(map);auto& producer=NorthlightNativeLights::producer();
            producer.sample(map,menu.inverseView+12,menu.scene,menu.model);producer.copy(map,lights);
        }
#endif
        if(!menu.valid||!NorthlightMenu::wellFormed(menu))return reject(2);
        char map[64];menu.identity(map);
        if(!NorthlightNativeLights::wellFormed(lights)||lights.scene!=menu.scene||std::strcmp(lights.map,map))lights.clear();
        float columns[16]{},rows[16]{},bank[12]{},light[12]{};Projection decoded;
        if(FAILED(d->GetVertexShaderConstantF(2,columns,4))||!decodeColumnProjection(columns,decoded,rows)||
           FAILED(d->GetVertexShaderConstantF(10,bank,3)))return reject(3);
        // M2: c10 is direct, c11 ambient, c12 the negated view-space light.
        for(unsigned i=0;i<3;++i){light[i]=-bank[8+i];light[4+i]=bank[4+i];light[8+i]=bank[i];}
        NorthlightWorldContext::TerrainContext next;
        if(!NorthlightWorldContext::decodeTerrain(menu.view,light,next))return reject(4);
        context=next;projection[0]=rows[0];projection[1]=rows[5];projection[2]=rows[11];valid=true;
        menuLights=lights;
        updateWorldContext(map,context.camera,nullptr,&menu);readOriginalFog(30,it->second.fog);
        valid=true;return hasContext();
    }
    bool modelContext(IDirect3DVertexShader9* shader){
        if(failed)return false;if(valid)return true;
        auto it=captureShaders.find(shader);
        if(it==captureShaders.end()||!it->second.skinned||it->second.projectionKind!=2)return false;
        float columns[16],rows[16];Projection q;
        if(FAILED(d->GetVertexShaderConstantF(2,columns,4))||!decodeColumnProjection(columns,q,rows))return false;
        char map[64]={};NorthlightWorldCamera::Camera camera;NorthlightWorldCamera::Diagnostics why;
        NorthlightWmoContext::Lighting light;
        if(!NorthlightWorldCamera::read(map,rows[11],camera,&why)||!readWorldLighting(map,camera.camera,light))return false;
        auto lightHold=globalLightHold;lightHold.filter(light,map,GetTickCount());
        if(!NorthlightWmoContext::context(camera.view,light,context))return false;
        globalLightHold=std::move(lightHold);
        valid=true;traceContext=4;projection[0]=rows[0];projection[1]=rows[5];projection[2]=rows[11];
        readOriginalFog(30,it->second.fog);if(legacyFogKnown)traceFog=1;
        updateWorldContext(map,camera.camera,&light);
        if(!modelContexts++||(modelContexts%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD model context accepted map=\"%s\" count=%u fogProof=%d",map,modelContexts,it->second.fog);
        return hasContext();
    }
    unsigned modelContexts=0;
    // A sky-only frame has no terrain/model draw to supply the camera. The
    // projection was validated by the last world frame, but the basis and
    // lighting must be read anew at the effects boundary. World draws retain
    // their usual context path, including current projection/FOV and fog.
    bool skyContext(const float* currentProjection){
        if(failed)return false;if(valid)return true;
        char map[64]={};NorthlightWorldCamera::Camera camera;NorthlightWorldCamera::Diagnostics why;
        NorthlightWmoContext::Lighting light;
        if(!NorthlightWorldCamera::readSkyPhase(map,currentProjection[2],camera,&why)||lastRequest.map!=map||
           !readWorldLighting(map,camera.camera,light))return false;
        auto lightHold=globalLightHold;lightHold.filter(light,map,GetTickCount());
        if(!NorthlightWmoContext::context(camera.view,light,context))return false;
        globalLightHold=std::move(lightHold);valid=true;traceContext=5;
        std::memcpy(projection,currentProjection,sizeof projection);
        updateWorldContext(map,camera.camera,&light);
        return hasContext();
    }
    bool readWorldLighting(const char* map,const float* camera,NorthlightWmoContext::Lighting& light){
        static const bool supported=NorthlightWmoContext::supportedClient();
        if(!supported)return false;
        if(!worldEnvironment.read(NorthlightWorldContext::readSelf,map,camera,light,GetTickCount()))
            return NorthlightWmoContext::readGlobalLighting(camera,light);
        NorthlightWmoContext::proveGlobalLighting(light);return true;
    }
    bool wmoContext(IDirect3DVertexShader9* shader){
        if(failed)return false;
        auto it=wmoShaders.find(shader);if(it==wmoShaders.end())return false;
        if(valid){
            if(!legacyFogKnown&&it->second.fog){readOriginalFog(30,true);if(legacyFogKnown)traceFog=1;}
            return true;
        }
        float columns[16],rows[16];Projection q;
        if(FAILED(d->GetVertexShaderConstantF(2,columns,4))||!decodeColumnProjection(columns,q,rows))return false;
        char map[64]={};NorthlightWorldCamera::Camera camera;NorthlightWorldCamera::Diagnostics why;
        NorthlightWmoContext::Lighting light;
        bool cameraRead=NorthlightWorldCamera::read(map,rows[11],camera,&why);
        bool globalRead=cameraRead&&readWorldLighting(map,camera.camera,light);
        auto lightHold=globalLightHold;if(globalRead)lightHold.filter(light,map,GetTickCount()); /* kept below only when this read is accepted */
        const bool globalLighting=globalRead&&NorthlightWmoContext::context(camera.view,light,context);
        bool lightingRead=globalLighting;
        if(cameraRead&&!lightingRead&&it->second.lighting){float values[12];
            lightingRead=SUCCEEDED(d->GetVertexShaderConstantF(10,values,3))&&NorthlightWmoContext::decodeLitShader(it->second.hash,camera.view,values,context);
        }
        if(!cameraRead||!lightingRead){
            if(++wmoRejects==1||(wmoRejects%3600==0&&NorthlightDiagnostics::enabled()))logf("CITY context rejected camera=%s count=%u",NorthlightWorldCamera::rejectName(why.reason),wmoRejects);
            return false;
        }
        if(globalLighting)globalLightHold=std::move(lightHold);
        valid=true;traceContext=3;projection[0]=rows[0];projection[1]=rows[5];projection[2]=rows[11];
        readOriginalFog(30,it->second.fog);if(legacyFogKnown)traceFog=1;
        updateWorldContext(map,camera.camera,globalRead?&light:nullptr);
        if(++wmoContexts==1||(wmoContexts%600==0&&NorthlightDiagnostics::enabled()))logf("CITY WMO context accepted map=\"%s\" count=%u fogProof=%d globalLight=%d",map,wmoContexts,it->second.fog,globalRead);
        return true;
    }
    void terrainContext(){
        if(failed)return;
        // Camera context may already come from an actor whose shader has no fog.
        // A later terrain draw must still supply this frame's native atmosphere.
        if(valid){if(!legacyFogKnown){readOriginalFog(12,true);if(legacyFogKnown)traceFog=1;}return;}
        float view[16]={},lighting[12]={},p[16]={},camera[3]={};char map[64]={};
        bool registers=SUCCEEDED(d->GetVertexShaderConstantF(0,view,4))&&SUCCEEDED(d->GetVertexShaderConstantF(4,p,4));
        bool nativeRead=SUCCEEDED(d->GetVertexShaderConstantF(24,lighting,3));
        bool gameContext=NorthlightWorldContext::readMapAndCamera(map,camera);
        NorthlightWorldCamera::Camera independent;NorthlightWorldCamera::Diagnostics why;char cameraMap[64]={};
        bool cameraRead=registers&&gameContext&&NorthlightWorldCamera::read(cameraMap,p[11],independent,&why);
        bool cameraMatches=cameraRead&&!std::strcmp(map,cameraMap)&&NorthlightWorldCamera::agreesWithTerrain(independent,view);
        NorthlightWmoContext::Lighting global;
        bool globalRead=cameraMatches&&readWorldLighting(map,camera,global);
        auto lightHold=globalLightHold;if(globalRead)lightHold.filter(global,map,GetTickCount()); /* kept below only when this read is accepted */
        bool globalLighting=false;
        bool decoded=registers&&gameContext&&NorthlightWmoContext::terrainContext(view,nativeRead?lighting:nullptr,camera,globalRead?&global:nullptr,context,&globalLighting);
        traceContext=globalRead?1u:2u;
        bool agreement=decoded;
        if(!agreement){
            if(++contextRejects==1||(contextRejects%3600==0&&NorthlightDiagnostics::enabled()))logf("WORLD context rejected: registers=%d affineLight=%d clientRead=%d cameraAgreement=%d map=\"%s\" shaderCamera=(%.2f %.2f %.2f) gameCamera=(%.2f %.2f %.2f) light=(%.3f %.3f %.3f) count=%u",registers,decoded,gameContext,agreement,map,context.camera[0],context.camera[1],context.camera[2],camera[0],camera[1],camera[2],lighting[0],lighting[1],lighting[2],contextRejects);return;}
        if(++cameraChecks==1||(cameraChecks%3600==0&&NorthlightDiagnostics::enabled()))logf("CITY camera audit read=%d terrainAgreement=%d reject=%s permissiveSignatures=%d failingSignature=%d",cameraRead,cameraMatches,NorthlightWorldCamera::rejectName(why.reason),int(NorthlightWorldCamera::kPermissiveCameraSignatures),NorthlightWorldCamera::failingSignature());
        if(globalLighting)globalLightHold=std::move(lightHold);
        valid=true;projection[0]=p[0];projection[1]=p[5];projection[2]=p[11];
        readOriginalFog(12,true);if(legacyFogKnown)traceFog=1;
        updateWorldContext(map,camera,globalRead?&global:nullptr);
    }
    void readOriginalFog(UINT fogRegister,bool known){
        // Read the original terrain fog before any extension render pass changes constants/state.
        IDirect3DPixelShader9* originalPS=nullptr;FogShader fogShader;
        if(SUCCEEDED(d->GetPixelShader(&originalPS))&&originalPS){
            auto it=fogShaders.find(originalPS);if(it!=fogShaders.end())fogShader=it->second;
        }drop(originalPS);
        float fogParameters[4]={},fogColor[4]={};DWORD fogEnabled=0,fogTable=0,fogARGB=0;
        bool fogRegisters=known&&SUCCEEDED(d->GetVertexShaderConstantF(fogRegister,fogParameters,1));
        if(fogShader.major==3)fogRegisters=fogRegisters&&SUCCEEDED((fogShader.colorRegister>=0?d->GetPixelShaderConstantF(UINT(fogShader.colorRegister),fogColor,1):D3DERR_INVALIDCALL));
        else fogRegisters=fogRegisters&&SUCCEEDED(d->GetRenderState(D3DRS_FOGENABLE,&fogEnabled))&&SUCCEEDED(d->GetRenderState(D3DRS_FOGTABLEMODE,&fogTable))&&SUCCEEDED(d->GetRenderState(D3DRS_FOGCOLOR,&fogARGB));
        NorthlightLegacyFog::Constants measured;
        bool fogKnown=fogRegisters&&NorthlightLegacyFog::decode(fogShader.major,fogShader.verified,fogEnabled!=0,fogTable,fogParameters,fogColor,fogARGB,projection[2],measured);
        if(fogKnown){legacyFog=measured;legacyFogKnown=true;}
        if(++fogReports==1||(fogReports%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD legacy fog known=%d ps=%u verified=%d colorRegister=%d enabled=%.0f params=(%.7g %.7g %.7g) color=(%.5f %.5f %.5f)",fogKnown,fogShader.major,fogShader.verified,fogShader.colorRegister,legacyFog.parameters[3],legacyFog.parameters[0],legacyFog.parameters[1],legacyFog.parameters[2],legacyFog.color[0],legacyFog.color[1],legacyFog.color[2]);
    }
    void beginSceneDomain(const char* map,const float* camera,const NorthlightMenu::Snapshot* menu){
        const bool changedDomain=menu?menuIdentity!=map:menuDomain;
        if(changedDomain){
            endFrame();releaseGPU();active.reset();actorJobComplete.reset();actorJob=std::make_shared<NorthlightActorGeometry::ActorJob>();
            resetShadowPivot(); // The menu model's camera distance cannot seed the World camera.
            actorSceneMap_.clear();lastActorCapture=0;actorCaptureDecided=false;actorVerticesEvaluated=0;
            equipmentLights.reset();particleLights.reset();lightAttachments.reset();weatherShelter.reset();
            underwaterFade={};celestialHeldOk=false;celestialLightMotion.reset();foreverTransition={};apparentHorizon={};
            menuIdentity=menu?map:"";settingsRequestDirty=true;valid=true;
        }
        menuDomain=menu!=nullptr;
        if(!menu)weatherState=worldWeatherState;
        if(menu){menuSnapshot=*menu;
            if(!active){active=std::make_shared<Snapshot>();active->map=map;active->center=vec(camera);
                active->bvh=std::make_shared<NorthlightGI::BVH>();active->meshPlan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();}
        }
    }
    void updateWorldContext(const char* map,const float* camera,const NorthlightWmoContext::Lighting* global=nullptr,const NorthlightMenu::Snapshot* menu=nullptr){
        ++materialLightRevision;
        if(!pipelineActive)return;
        beginSceneDomain(map,camera,menu);
        if(unsigned fault=workerFault()){if(!failed)logf("WORLD worker stopped: %s; restart required",workerFaultMessage(fault));failed=true;valid=false;return;}
        if(!reportedContext){logf("WORLD context validated: map=\"%s\" camera=(%.2f %.2f %.2f) sun=(%.3f %.3f %.3f)",map,camera[0],camera[1],camera[2],context.lightDirection[0],context.lightDirection[1],context.lightDirection[2]);reportedContext=true;}
        {   // The camera's liquid type from the proven predictor reads (wmo_context.h); unproven frames hold the state for UnderwaterFade::HoldMs.
            const bool known=global&&global->predictors;const DWORD now=GetTickCount();
            underwaterFade.update(map,known,known&&global->liquidType!=0,now);
            if(underwaterFade.underwater!=underwaterLogged&&(DWORD(now-underwaterLogAt)>250||NorthlightDiagnostics::enabled())){underwaterLogAt=now;underwaterLogged=underwaterFade.underwater;logf("WORLD underwater %s liquid=%u",underwaterFade.underwater?"on":"off",known?global->liquidType:0u);} // a flip suppressed by the rate limit is logged later, once the state still differs
        }
        static const bool celestialCode=NorthlightWorldContext::supportedClient()&&NorthlightCelestial::verifyCode(NorthlightWorldContext::readSelf);
        if(menu){const auto environment=NorthlightMenu::environment(*menu,context.ambient,context.direct);
            celestial=environment.celestial;celestialValid=true;skyEnvironment=environment.atmosphere;weatherState=environment.weather;
        }else{
        celestialValid=global&&celestialCode&&worldEnvironment.current&&worldEnvironment.hasCelestial&&worldEnvironment.map==map;
        if(celestialValid){celestial=worldEnvironment.celestial;NorthlightCelestial::assumeSkybox(celestial,NorthlightCelestial::skyboxVerified());}
        else celestialValid=NorthlightCelestial::read(camera,context.direct,celestial);
        // 0.3.200: a sky block read rejected for one frame (the two copies differ while the game rewrites it, more often with the game frames
        // ahead) dropped the sun weight to 0 for that frame: the sun light, shadows and shafts flashed off. The last good read on the same map
        // stands in for up to CelestialHoldMs; a longer failure (loading, an indoor map without a sky) still ends at the fallback.
        {const DWORD now=GetTickCount();
         if(celestialValid){celestialHeld=celestial;celestialHeldMap=map;celestialHeldAt=now;celestialHeldOk=true;}
         else if(celestialHeldOk&&celestialHeldMap==map&&DWORD(now-celestialHeldAt)<=CelestialHoldMs){celestial=celestialHeld;celestialValid=true;++celestialHolds;}}
        const float storm=quality.weather&&weatherState.kind!=NorthlightWeather::Kind::None?weatherState.blend*weatherState.intensity:0;
        skyEnvironment=foreverAtmosphere.sample(map,context.camera,celestialValid?celestial.dayFraction:-1.f,storm);
        if(!skyEnvironment.valid&&(!std::strcmp(map,"Expansion01")||!std::strcmp(map,"Northrend"))){
            std::shared_ptr<const PaletteRegion> region;
            {std::lock_guard<std::mutex> lock(mutex);region=publishedPaletteRegion;}
            if(region&&region->map==map)skyEnvironment=NorthlightExpansionAtmosphere::sample(region->region,map,context.camera,celestialValid?celestial.dayFraction:-1.f,storm,context.direct,context.ambient);
        }
        const auto atmosphereTick=NorthlightFrameParityClock::enabled()?visualTime():GetTickCount64();
        skyEnvironment=foreverTransition.update(skyEnvironment,map,context.camera,double(atmosphereTick)*.001);
        }
        const auto* selectedSky=celestialSkyPalette();
        const float* renderDirect=selectedSky?skyEnvironment.direct:context.direct;
        const float* renderAmbient=selectedSky?skyEnvironment.ambient:context.ambient;
        if(celestialValid){
            // Selected placement is shared by discs, shadows and fog.
            const float nativeSunAlpha=celestial.sun.alpha,nativeMoonAlpha=celestial.moon.alpha;
            const double nativeSun=NorthlightCelestialOrbit::elevationDegrees(celestial.sun.direction[2]);
            const double nativeMoon=NorthlightCelestialOrbit::elevationDegrees(celestial.moon.direction[2]);
            const auto orbit=NorthlightCelestial::resolveRendererSky(celestial,renderDirect,NorthlightQuality::foreverCelestial(quality),map);
            const auto lightOrbit=celestialLightMotion.update(orbit,celestial.dayFraction,visualTime(),
                celestialLightMap!=map||different(celestialLightCamera,vec(camera),40));
            celestialLightMap=map;celestialLightCamera=vec(camera);
            celestialLight=celestial;
            std::memcpy(celestialLight.sun.direction,lightOrbit.sun.direction,sizeof celestialLight.sun.direction);
            std::memcpy(celestialLight.moon.direction,lightOrbit.moon.direction,sizeof celestialLight.moon.direction);
            NorthlightCelestial::applyRendererPolicy(celestialLight,renderDirect);
            const auto palette=celestialPalette(map,camera);
            NorthlightCelestialProfiles::apply(palette,renderDirect,celestial);
            NorthlightCelestialProfiles::apply(palette,renderDirect,celestialLight);
            continuousCelestialShadows=false; // Native-speed orbit: ordinary cache policy, no accelerated phases.
            if(++celestialOrbitReports%600==1&&(celestialOrbitReports==1||NorthlightDiagnostics::enabled()))logf("CELESTIAL orbit gameDay=%.6f sun=%.2f->%.2f moon=%.2f->%.2f schedule=%s weights=%.3f/%.3f nativeAlpha=%.3f/%.3f rendererAlpha=%.3f/%.3f map=%s nativePlanets=%u skyboxCover=%.3f",celestial.dayFraction,nativeSun,orbit.sun.elevation,nativeMoon,orbit.moon.elevation,NorthlightQuality::celestialStyleName(NorthlightQuality::foreverCelestial(quality)),celestial.sunWeight,celestial.moonWeight,nativeSunAlpha,nativeMoonAlpha,celestial.sun.alpha,celestial.moon.alpha,map,unsigned(celestial.nativePlanets),celestial.skyboxCover);
        }
        if(!celestialValid){celestialLightMotion.reset();continuousCelestialShadows=false;}
        // glow hue: the light slots of this frame's proven read, else held for 2 s on the same map.
        const DWORD slotsNow=GetTickCount();
        if(lightSlotsMap!=map||DWORD(slotsNow-lightSlotsAt)>NorthlightSunHue::HoldMs){lightSlotsValid=false;lightSlotsMap=map;}
        if(global&&global->slotsProven){memcpy(lightSlots,global->slots,sizeof lightSlots);lightSlotsValid=true;lightSlotsAt=slotsNow;}
        const auto glowPalette=celestialPalette(map,camera);
        glowHueFrame=NorthlightSunHue::compute(lightSlotsValid?lightSlots:nullptr,glowPalette.disc[1]);
        celestialGlowHueFrame=glowHueFrame;
        if(celestialValid&&NorthlightCelestial::luminousAppearance(celestial.sun))celestialGlowHueFrame=NorthlightSunHue::compute(lightSlotsValid?lightSlots:nullptr,glowPalette.disc[1],celestial.sun.nativeTint);
        if(quality.celestialEffects&&selectedSky){
            float solar[3];const auto& colors=selectedSky->color;
            const float* tint=std::max({colors[6][0],colors[6][1],colors[6][2]})>1.f/255?colors[6]:colors[7];
            if(NorthlightSunHue::normMax(tint,solar)){
                glowHueFrame=NorthlightSunHue::compute(nullptr,glowPalette.disc[1],solar);
                celestialGlowHueFrame=glowHueFrame;
                if(celestialValid)std::copy_n(solar,3,celestial.sun.tint);
            }
        }
        sourceDirections[0]=vec(context.lightDirection);sourceColors[0]=vec(context.direct);sourceGlowColors[0]=sourceColors[0];sourceGlowColors[1]=V();
        sourceDirections[1]=V(0,0,1);sourceColors[1]=V();sourceWeights[0]=1;sourceWeights[1]=0;
        // The sun and moon light (shadows, rays) from 6 deg at the lowest (MinLightElevation), its weight ending with the disc (discVisibility);
        // the discs and the twilight keep their real elevation.
        float sunLightDirection[3],moonLightDirection[3];std::memcpy(sunLightDirection,celestialLight.sun.direction,sizeof sunLightDirection);
        std::memcpy(moonLightDirection,celestialLight.moon.direction,sizeof moonLightDirection);
        NorthlightCelestial::clampLightElevation(sunLightDirection);NorthlightCelestial::clampLightElevation(moonLightDirection);
        auto resolvedSources=NorthlightCelestialSources::resolve(vec(context.lightDirection),vec(context.direct),
            celestialValid?vec(sunLightDirection):vec(context.lightDirection),
            celestialValid?vec(moonLightDirection):V(0,0,1),
            celestialValid?celestialLight.sunWeight:0.f,celestialValid?celestialLight.moonWeight:0.f);
        if(celestialValid){resolvedSources.sources[0].color=vec(celestialLight.sunColor);resolvedSources.sources[1].color=vec(celestialLight.moonColor);}
        // Menu models are lit for a fixed camera and authored key light, not a
        // world clock. An orbit-derived replacement removes that painted light
        // from the wrong side of the character and backdrop. Shadow and GI
        // geometry still use the full pipeline, with the measured menu light.
        if(menu){resolvedSources.sources[0]={vec(context.lightDirection),vec(context.direct),1};
            resolvedSources.sources[1]={V(0,0,1),V(),0};resolvedSources.authoredFill=0;}
        authoredFill=resolvedSources.authoredFill;
        for(unsigned source=0;source<2;++source){rawSourceDirections[source]=resolvedSources.sources[source].direction;sourceDirections[source]=continuousCelestialShadows?resolvedSources.sources[source].direction:NorthlightWorldMath::quantizeDirection(resolvedSources.sources[source].direction,float(quality.shadowDirectionSteps));sourceColors[source]=resolvedSources.sources[source].color;sourceGlowColors[source]=sourceColors[source];sourceWeights[source]=resolvedSources.sources[source].weight;}
        if(celestialValid){sourceGlowColors[0]=vec(celestialLight.sunGlowColor);sourceGlowColors[1]=vec(celestialLight.moonGlowColor);}
        if(menu){rawSourceDirections[0]=sourceDirections[0]=vec(context.lightDirection);sourceGlowColors[0]=sourceColors[0];sourceGlowColors[1]=V();}
        if(!menu)requestStaticCasters(map);
        Request r;r.menu=menu!=nullptr;if(menu)r.menuLights=menuLights;r.map=map;r.camera=vec(camera);r.light.sunDirection=sourceDirections[0];r.light.sunIrradiance=sourceColors[0]*3.14159265f;r.light.skyRadiance=vec(renderAmbient);r.light.maxBounces=quality.giBounces;r.giEnabled=giWorkEnabled();r.fogEnabled=effects.fog;
        if(auroraSourceMap==map&&NorthlightAurora::style(map,quality.auroraStrength,celestialValid,celestial.dayFraction,NorthlightQuality::foreverCelestial(quality)).enabled)r.light.aurora=auroraSource;
        r.auroraActive=bool(r.light.aurora);
        r.light.additionalDirections.push_back({sourceDirections[1],sourceColors[1]*3.14159265f});
        // 0.3.153 GIProbeAhead: centre the probe window ahead of the eye along the horizontal
        // view direction (the third-person camera sits behind the character). 0 = the eye.
        r.probeCenter=r.camera;
        if(quality.giProbeAhead){const V view=V(context.inverseView[8],context.inverseView[9],0)*projection[2];const float length=std::sqrt(NorthlightGI::dot(view,view));
            if(length>1e-3f)r.probeCenter=r.camera+view*(float(quality.giProbeAhead)/length);}
        // Keep the region stable under camera orbits; the total eye offset,
        // including travel lead, retains the existing 60-unit safety bound.
        const bool fixedGi=NorthlightFrameParityClock::fixedGiPublication();
        {const V forward=V(context.inverseView[8],context.inverseView[9],context.inverseView[10])*projection[2];const float length=std::sqrt(NorthlightGI::dot(forward,forward));
         const V pivot=length>1e-6f?r.camera+forward*(pivotDistance/length):r.camera;
         geometryMotion.update(r.map,r.camera,pivot,fixedGi?visualTime():GetTickCount());
         const float buildSeconds=fixedGi?1.f:float(geometryBuildEstimateMs.load(std::memory_order_relaxed))*.001f;
         r.geometryCenter=NorthlightWorldStreaming::anchoredCenter(r.camera,pivot,geometryMotion.velocity(),buildSeconds);
         geometryAnchorOffset=std::sqrt(NorthlightGI::dot(pivot-r.camera,pivot-r.camera));
         geometryLead=std::sqrt(NorthlightGI::dot(r.geometryCenter-r.camera,r.geometryCenter-r.camera));}
        if((menu||giActorWorkEnabled())&&lastRequest.map==r.map&&completedActorSceneMap()==r.map){r.actorJob=completedActorJob();r.actorSerial=actorJobSerial();}
        std::shared_ptr<Snapshot> retiredSnapshot,expiredPublication;
        bool admitScene=true;
        bool expectScene=false;
#ifdef NORTHLIGHT_REPLAY_STAND
        // Equipment draw capture consumes the CPU scene before the first GPU
        // mesh is ready. Match its captured adoption across Reset epochs.
        const unsigned sceneStart=NorthlightFrameParityClock::replayWorldCheckpoint(parityFrame).scene;
        admitScene=!sceneStart||menu||parityFrame>=sceneStart;
        expectScene=!menu&&sceneStart&&parityFrame==sceneStart;
#endif
        {std::unique_lock<std::mutex> lock(mutex);
            if(fixedGi){
                // Finish runnable probe work before observing its publication.
                // Geometry remains asynchronous until the recorded stand checkpoint.
                const bool settled=giSettled.wait_for(lock,std::chrono::seconds(15),[&] {
                    if(stopping||workerFaultCode.load())return true;
                    return !pending&&!workerBusy&&(!expectScene||
                        (published&&published->bvh&&NorthlightWorldStreaming::applicable(published->map,published->center,r.map,r.camera)));
                });
                if(!settled)logf("GI_PARITY_FAILURE present=%u stage=cpu-timeout",parityFrame);
            }
            // The publication is another owner of the old BVH/plan. Retiring only
            // active left it resident indefinitely while the builder waited for VA.
            // BEGIN EXPIRED PUBLICATION
            if(published&&published->bvh&&!NorthlightWorldStreaming::retained(published->map,published->center,r.map,r.camera))
                expiredPublication=std::move(published);
            // END EXPIRED PUBLICATION
            if(published&&published!=observedPublication.lock()){
                observedPublication=published;if(!published->message.empty()&&published->message!=loggedCacheMessage)logf("WORLD cache: %s",published->message.c_str());loggedCacheMessage=published->message;
            }
            if(admitScene&&published&&published!=active&&NorthlightWorldStreaming::adopts(published->map,published->center,bool(published->bvh),active&&active->bvh,r.map,r.camera)){
                bool newGI=published->serial&&(!active||active->serial!=published->serial);retiredSnapshot=std::move(active);active=published;
                if(newGI&&NorthlightDiagnostics::enabled())logf("GI activated tick=%lu id=%llu ageMs=%lu queueMs=%lu geometryMs=%lu actorMs=%lu solveMs=%lu reused=%u solved=%u cancelled=%u origin=(%.1f %.1f %.1f) dynamicReused=%u dynamicSolved=%u staticOnly=%u partial=%u processed=%u generation=%llu retargeted=%u actorTextureDecode=worker actorTextures=%u actorTextureBytes=%zu pathRecorded=%llu pathReplayedRays=%llu pathRetracedRays=%llu pathRecordKB=%zu actorWorkMs=%.3f solveWorkMs=%.3f actorDecodeMs=%.3f actorPoseMs=%.3f actorHashMs=%.3f actorBvhMs=%.3f",(unsigned long)GetTickCount(),(unsigned long long)active->requestId,(unsigned long)(GetTickCount()-active->requestedAt),(unsigned long)active->queueMs,(unsigned long)active->geometryMs,(unsigned long)active->actorMs,(unsigned long)active->solveMs,active->reusedProbes,active->solvedProbes,active->superseded,active->origin.x,active->origin.y,active->origin.z,active->dynamicReused,active->dynamicSolved,unsigned(active->staticOnly),unsigned(active->partial),active->processedProbes,(unsigned long long)active->lightingGeneration,active->retargeted,active->actorTexturesDecoded,active->actorTextureEncodedBytes,(unsigned long long)active->movingStats.recorded,(unsigned long long)active->movingStats.replayed,(unsigned long long)active->movingStats.retraced,active->pathRecordBytes/1024,active->actorWorkMs,active->solveWorkMs,active->actorDecodeMs,active->actorPoseMs,active->actorHashMs,active->actorBvhMs);
            }
            r.reason=(settingsRequestDirty?256u:0u)|(lastRequest.map!=r.map?1u:0u)|(menu&&lastRequest.actorSerial!=r.actorSerial?64u:0u)|(different(quantize(lastRequest.probeCenter,float(quality.giProbeMoveStep)),quantize(r.probeCenter,float(quality.giProbeMoveStep)),.1f)?2u:0u)|
                (different(lastRequest.light.sunDirection,r.light.sunDirection,.02f)?4u:0u)|(different(lastRequest.light.sunIrradiance,r.light.sunIrradiance,.03f)?8u:0u)|((!NorthlightGI::sameAngularSky(lastRequest.light,r.light)||different(lastRequest.light.skyRadiance,r.light.skyRadiance,.01f))?16u:0u)|
                (lastRequest.light.additionalDirections.empty()||different(lastRequest.light.additionalDirections[0].direction,r.light.additionalDirections[0].direction,.02f)||different(lastRequest.light.additionalDirections[0].irradiance,r.light.additionalDirections[0].irradiance,.03f)?32u:0u)|
                (NorthlightWorldStreaming::leadMoved(lastRequest.camera,lastRequest.geometryCenter,r.camera,r.geometryCenter)?128u:0u);
            const bool deferSky=r.reason==16u&&r.giEnabled&&NorthlightGI::deferAnimatedAuroraRefresh(
                lastRequest.light,r.light,pending||workerBusy.load(std::memory_order_relaxed));
            if(r.reason&&!deferSky){queueRequestLocked(r,GetTickCount(),true);settingsRequestDirty=false;wake.notify_one();
                if(NorthlightDiagnostics::enabled())logf("GI request tick=%lu id=%llu reason=%u camera=(%.2f %.2f %.2f) sun=(%.5f %.5f %.5f)",(unsigned long)r.queuedAt,(unsigned long long)r.id,r.reason,r.camera.x,r.camera.y,r.camera.z,r.light.sunDirection.x,r.light.sunDirection.y,r.light.sunDirection.z);
            }
            // 0.3.156 watchdog (log only): the builder waited for a free geometry generation
            // for GenerationStallLogMs; name the holders once per episode. Nothing is dropped.
            if(generationStall.due(GetTickCount(),GenerationStallLogMs))
                logf("WORLD geometry stalled ms=%lu pending=%u pendingMap=\"%s\" pendingActive=%u pendingPublished=%u active=%u activeId=%llu activeCenter=(%.1f %.1f %.1f) published=%u publishedId=%llu publishedMap=\"%s\" publishedCenter=(%.1f %.1f %.1f) publishedApplicable=%u camera=(%.1f %.1f %.1f) map=\"%s\"",
                    (unsigned long)(GetTickCount()-generationStall.since),unsigned(bool(pendingMesh)),pendingMesh?pendingMesh->map.c_str():"-",unsigned(pendingMesh&&active&&pendingMesh->bvh==active->bvh),unsigned(pendingMesh&&published&&pendingMesh->bvh==published->bvh),
                    unsigned(bool(active)),(unsigned long long)(active?active->requestId:0),active?active->center.x:0.f,active?active->center.y:0.f,active?active->center.z:0.f,
                    unsigned(bool(published)),(unsigned long long)(published?published->requestId:0),published?published->map.c_str():"-",published?published->center.x:0.f,published?published->center.y:0.f,published?published->center.z:0.f,
                    unsigned(published&&NorthlightWorldStreaming::applicable(published->map,published->center,r.map,r.camera)),r.camera.x,r.camera.y,r.camera.z,r.map.c_str());
            }
        {auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Retire);
         auto& reaper=NorthlightStreaming::cpuRetirement();retirementBacklog.retry(reaper);
         if(expiredPublication){
             logf("WORLD expired publication released map=\"%s\" cpuMiB=%.2f",expiredPublication->map.c_str(),double(expiredPublication->retirementBytes)/1048576);
             retirementBacklog.retireOrFree(reaper,expiredPublication,snapshotRetireBytes(*expiredPublication));
         }
         const bool adopted=retiredSnapshot&&active&&retiredSnapshot->bvh!=active->bvh; /* a new region, not a GI publication */
         if(retiredSnapshot)retirementBacklog.retireOrFree(reaper,retiredSnapshot,snapshotRetireBytes(*retiredSnapshot));
         retiredSnapshot.reset();
         // 0.3.169: hold the snapshot past its 96-unit coverage until a replacement applies (above)
         // or the hard limit or a map change retires it.
         const char* retired=active&&!NorthlightWorldStreaming::retained(active->map,active->center,r.map,r.camera)?(active->map!=r.map?"map":"retired"):nullptr;
         const V held=active?active->center:V();const bool heldDrawable=active&&active->bvh; /* issue #40: an uncached map's one error snapshot stays at the entry point */
         if(retired){expiredMeshMapChanged|=heldDrawable&&active->map!=r.map;retirementBacklog.retireOrFree(reaper,active,snapshotRetireBytes(*active));active.reset();expiredMeshPending|=heldDrawable;}
         releaseOrphanedPending("range");
         const float distance=active&&active->map==r.map?std::sqrt(NorthlightGI::dot(r.camera-active->center,r.camera-active->center)):0.f;
         if(distance>coverMax)coverMax=distance;
         const bool holding=active&&active->bvh&&!NorthlightWorldStreaming::applicable(active->map,active->center,r.map,r.camera); /* only drawable geometry is held */
         const bool logHold=holdEpisodes<=32||NorthlightDiagnostics::enabled();
         if((retired&&(heldDrawable||coverageHold))||(coverageHold&&(adopted||!holding))){
             if(!coverageHold){++holdEpisodes;holdStart=GetTickCount();holdMax=0;} /* retired in one step: a teleport or a map change */
             if(retired)holdMax=std::max(holdMax,std::sqrt(NorthlightGI::dot(r.camera-held,r.camera-held)));
             if(logHold)logf("WORLD coverage hold end ms=%lu maxDist=%.1f reason=%s",(unsigned long)(GetTickCount()-holdStart),holdMax,retired?retired:adopted?"adopted":"returned");
             coverageHold=false;
         }
         if(!coverageHold&&holding){coverageHold=true;++holdEpisodes;holdStart=GetTickCount();holdMax=distance;
             if(holdEpisodes<=32||NorthlightDiagnostics::enabled())logf("WORLD coverage hold begin dist=%.1f eye=(%.1f %.1f %.1f) centre=(%.1f %.1f %.1f) fwd=(%.3f %.3f %.3f) pivotDistance=%.1f lead=%.1f",distance,r.camera.x,r.camera.y,r.camera.z,active->center.x,active->center.y,active->center.z,
                 context.inverseView[8]*projection[2],context.inverseView[9]*projection[2],context.inverseView[10]*projection[2],pivotDistance,geometryLead);
         }else if(coverageHold)holdMax=std::max(holdMax,distance);}
        DWORD now=GetTickCount();if(NorthlightDiagnostics::enabled()&&now-diagnosticTick>=250){diagnosticTick=now;
            logf("WORLD camera tick=%lu rendered=%u eye=(%.2f %.2f %.2f) forward=(%.4f %.4f %.4f) sun=(%.5f %.5f %.5f) GI=%llu pivotDistance=%.1f pivotUpdates=%u coverMax=%.1f lead=%.1f anchorOffset=%.1f pivotSource=%s snapCorrections=%u selfDistance=%.1f radiusSelf=%d nearBlendAtSelf=%.2f",(unsigned long)now,frames,context.camera[0],context.camera[1],context.camera[2],context.inverseView[8]*projection[2],context.inverseView[9]*projection[2],context.inverseView[10]*projection[2],context.lightDirection[0],context.lightDirection[1],context.lightDirection[2],(unsigned long long)(active?active->serial:0),pivotDistance,pivotUpdates,coverMax,geometryLead,geometryAnchorOffset,
                NorthlightShadowPivot::name(pivotCorrection.source),pivotCorrection.snapCorrections,pivotCorrection.selfDistance,int(pivotCorrection.hasSelf),pivotCorrection.nearBlendAtSelf(sourceMatrices[0][0])); /* 0.3.190: the near matrix the shader gets (source 0) */
            coverMax=0;
        }
    }
    void terrainPixelProof(IDirect3DPixelShader9* shader,const uint32_t* words,size_t count){
        if(words)nativePointPixelOwners[shader]=NorthlightNativeLocalPatch::pointOwnerPixel(words,count);
        else nativePointPixelOwners.erase(shader);
    }
    void terrainVertexProof(IDirect3DVertexShader9* shader,const uint32_t* words,size_t count){
        if(words)registerNativePointShader(shader,reinterpret_cast<const DWORD*>(words),count,false,true);
        else{
            nativePointShaders.erase(shader);
            nativePointOwners.erase(shader);
            nativeTerrainPointShaders.erase(shader);
        }
    }
    IDirect3DPixelShader9* terrainShadowReplacement(IDirect3DPixelShader9* shader){
        return terrainShadowPrograms.neutral(d,shader,
            [&](auto* derived,const uint32_t* words,size_t count){terrainPixelProof(derived,words,count);},
            [](const char* format,auto... args){logf(format,args...);});
    }
    bool terrainShadowCoverage(IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel,
                               IDirect3DVertexShader9*& patchedVertex,IDirect3DPixelShader9*& patchedPixel,
                               unsigned& constants,unsigned& constantCount,float* rows){
        const auto result=terrainShadowPrograms.coverage(d,vertex,pixel,context.inverseView,
            [&](auto* derived,const uint32_t* words,size_t count){if(!words||terrainShaders.count(vertex))terrainVertexProof(derived,words,count);},
            [&](auto* derived,const uint32_t* words,size_t count){terrainPixelProof(derived,words,count);},
            [](const char* format,auto... args){logf(format,args...);});
        if(!result.pixel)return false;
        patchedVertex=result.vertex;patchedPixel=result.pixel;constants=result.first;constantCount=result.count;
        std::memcpy(rows,result.rows,constantCount*16);
        return true;
    }
    bool terrainShadowActive()const{return directionalShadowWork()&&ready()&&shadowsComposited;}
    bool nativeTerrainShadowsEnabled()const{return !quality.shadowProvider||(effects.shadows&&!nativeShadowOwned)||quality.disableOriginalTerrainShadows==0;}
    bool shadowsRequested()const{return effects.shadows;} /* 0.3.187: changes only in setEffects (frame boundary) */
    void registerPixelShader(IDirect3DPixelShader9* shader){
        fogShaders.erase(shader);nativePointPixelOwners.erase(shader);UINT size=0;
        terrainShadowPrograms.forgetPixel(shader,[&](auto* derived,const uint32_t* words,size_t count){terrainPixelProof(derived,words,count);});
        if(!shader||FAILED(shader->GetFunction(nullptr,&size))||size<8||size>65536||size%4)return;
        std::vector<uint32_t> code(size/4);if(FAILED(shader->GetFunction(code.data(),&size)))return;
        nativePointPixelOwners[shader]=NorthlightNativeLocalPatch::pointOwnerPixel(code.data(),code.size());
        FogShader info;info.major=(code[0]>>8)&255;info.detailDoodad=NorthlightDetailShadow::pixelShader(NorthlightShaderTags::fnv1a(code.data(),size));
        info.colorRegister=NorthlightLegacyFog::ps3FogColorRegister(code.data(),code.size());info.verified=info.colorRegister>=0;info.alpha=NorthlightShadowAlpha::analyze(code.data(),code.size());fogShaders[shader]=info;
    }
    void registerShader(IDirect3DVertexShader9* shader,uint64_t hash){
        terrainShadowPrograms.forgetVertex(shader,[&](auto* derived,const uint32_t* words,size_t count){terrainVertexProof(derived,words,count);});
        ++worldShaderGen; /* 0.3.196 (task 12): before any map changes */
        replayBoundsMetadata.invalidateShader(shader);
        wmoShaders.erase(shader);
        nativeLightingShaders.erase(shader);
        terrainShaders.erase(shader);if(contains(kTerrainVS,hash))terrainShaders.insert(shader);
        auto begin=std::begin(kWorldShaderSignatures),end=std::end(kWorldShaderSignatures);
        auto it=std::lower_bound(begin,end,hash,[](const WorldShaderSignature& a,uint64_t h){return a.hash<h;});
        auto old=captureShaders.find(shader);if(old!=captureShaders.end()){drop(old->second.replacement);retireProgram(std::move(old->second.program));captureShaders.erase(old);}
        {auto program=actorPrograms.find(shader); /* 0.3.177: the caches are cleared by their owner at its next open */
         if(program!=actorPrograms.end()){prepareCachesStale=true;retireProgram(std::move(program->second));actorPrograms.erase(program);}}
        actorUVPrograms.erase(shader);
        UINT size=0;if(!shader||FAILED(shader->GetFunction(nullptr,&size))||size<8||size>65536||size%4)return;
        std::vector<uint32_t> words(size/4),output;
        if(FAILED(shader->GetFunction(words.data(),&size)))return;
        const unsigned tags=NorthlightShaderTags::triggerTags(hash,words.data(),size);
        if(const auto* known=NorthlightWmoContext::signature(hash))wmoShaders.emplace(shader,*known);
        else if(tags&NorthlightShaderTags::kWmo){
            // Coordinate proof grants world capture only. Fog and native RGB
            // handoff are proven independently; c10..12 fallback stays exact.
            wmoShaders.emplace(shader,WmoShaderSignature{hash,false,NorthlightSpellFog::vsFogProven(words.data(),words.size())});
            if(NorthlightDiagnostics::enabled())logf("WORLD shader coordinate contract accepted hash=%016llx",(unsigned long long)hash);
        }
        if(!(tags&NorthlightShaderTags::kWorld))return;
        const bool detail=NorthlightDetailShadow::shader(hash);
        unsigned kind=contains(kTerrainVS,hash)?1:it!=end&&it->hash==hash?it->projectionKind:2;
        if(kind==1&&!detail)return; // Terrain uses an immediate position snapshot, including SM1.
        NorthlightShadowShader::PatchInfo info;
        std::vector<uint32_t> fadeWords;
        if(detail&&!NorthlightDetailShadow::shadowVertex(words,fadeWords))return;
        const auto& shadowInput=detail?fadeWords:words;
        if(!NorthlightShadowShader::patch(shadowInput.data(),shadowInput.size(),output,&info)||!info.texcoord0XY)return;
        NorthlightDrawSnapshot::Ref<IDirect3DVertexShader9> replacement;
        std::vector<uint32_t> compact;
        const auto* submitted=shadowPrograms.createCapture(d,output,compact,replacement.p);
        if(submitted){
            CaptureShader metadata;metadata.replacement=replacement.p;metadata.projectionKind=kind;metadata.color0W=info.color0W;metadata.detailDoodad=detail;
            metadata.wmo=wmoShaders.count(shader)!=0;
            metadata.usage=NorthlightShaderConstants::analyze(words.data(),words.size(),kind);
            if(metadata.usage.relativeFloat)
                metadata.usage.weightedFloats=NorthlightShaderConstants::analyze(submitted->data(),submitted->size(),kind).weightedFloats;
            metadata.fog=NorthlightSpellFog::vsFogProven(words.data(),words.size());
            NorthlightActorDeformation::Program program;
            if(NorthlightActorDeformation::compile(words.data(),words.size(),program,false,kind)){
                metadata.skinned=program.skinned;metadata.sm1=program.major==1;
                metadata.program=std::make_shared<const NorthlightActorDeformation::Program>(std::move(program));
                actorPrograms.emplace(shader,metadata.program);
            }
            if(detail&&(!metadata.program||metadata.skinned||!metadata.color0W))return;
            if(kind==2&&!metadata.skinned)
                metadata.pointRigidRows=NorthlightPointShadow::RigidPoseRows::fromProgram(submitted->data(),submitted->size());
            if(NorthlightActorDeformation::compile(words.data(),words.size(),program,true))actorUVPrograms.emplace(shader,std::make_shared<const NorthlightActorDeformation::Program>(std::move(program)));
            // Publish only complete metadata. RAII retains ownership on any
            // compiler/map allocation exception before this transfer.
            captureShaders.emplace(shader,metadata);replacement.p=nullptr;
        }
    }
    void appendTerrain(TerrainSnapshot snapshot){
        bool fresh=false;for(auto& chunk:snapshot->bounds.chunks)if(!liveTerrainChunks.count({chunk.x,chunk.y}))fresh=true;
        if(!fresh)return;
        if(frameTerrainVertices+snapshot->positions.size()>262144||frameTerrainIndices+snapshot->indices.size()>1572864)return;
        for(auto& chunk:snapshot->bounds.chunks)liveTerrainChunks.emplace(chunk.x,chunk.y);
        frameTerrainVertices+=snapshot->positions.size();frameTerrainIndices+=snapshot->indices.size();
        frameTerrain.push_back(std::move(snapshot));++terrainSnapshots;
    }
    void captureUP(D3DPRIMITIVETYPE type,UINT minimum,UINT vertexTotal,UINT count,const void* indexData,D3DFORMAT format,const void* vertexData,UINT stride,bool indexed,IDirect3DVertexShader9* shader,bool selfCheck,bool sample){
        constantSelfCheck=selfCheck;sample=sample&&NorthlightDiagnostics::enabled();
        if(!ready())return;
        if(!lookupShader(shader).terrain){captureSampled|=sample;captureModel(type,0,minimum,vertexTotal,0,count,indexed,shader,sample,indexData,format,vertexData,stride);return;}
        if(!effects.shadows)return; // Live terrain is consumed only by shadow passes; UP has no material bounds handoff.
        captureSampled|=sample;if(sample){++terrainCaptureCalls;++terrainUPCalls;}CpuScope cpu(sample?&terrainCaptureTicks:nullptr);
        ++terrainAttempts;NorthlightTerrainCapture::MeshSnapshot snapshot;NorthlightTerrainCapture::Diagnostics why;
        bool ok=indexed?terrainBoundsCache.readMeshUP(d,type,minimum,vertexTotal,count,indexData,format,vertexData,stride,context.view,snapshot,&why):
                        terrainBoundsCache.readPrimitiveUP(d,type,count,vertexData,stride,context.view,snapshot,&why);
        if(!ok){if(terrainFailures++<12)logf("TERRAIN UP snapshot rejected: %s hr=%08lx type=%u stride=%u",NorthlightTerrainCapture::rejectName(why.reason),(unsigned long)why.hr,why.positionType,why.stride);return;}
        appendTerrain(std::make_shared<const NorthlightTerrainCapture::MeshSnapshot>(std::move(snapshot)));
    }
    bool terrainShader(IDirect3DVertexShader9* shader){return lookupShader(shader).terrain;}
    void capture(D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertexTotal,UINT start,UINT count,bool indexed,IDirect3DVertexShader9* current,bool selfCheck,bool sample,NorthlightCaptureMetadata::PositionBounds* positions=nullptr,const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>* captured=nullptr,IDirect3DVertexDeclaration9* capturedDeclaration=nullptr,const NorthlightPoseBounds::Handoff* pose=nullptr,const NorthlightConstantEpoch::BankHandoff* bank=nullptr){
        if(positions)positions->known=false;
        constantSelfCheck=selfCheck;sample=sample&&NorthlightDiagnostics::enabled();
        if(!ready()||replays.size()>=4096||(type!=D3DPT_TRIANGLELIST&&type!=D3DPT_TRIANGLESTRIP))return;
        captureSampled|=sample;bool isTerrain=lookupShader(current).terrain;
        if(isTerrain){
            if(!effects.shadows&&!positions)return; // AutoBump may still need this draw's position bounds.
            if(sample)++terrainCaptureCalls;CpuScope cpu(sample?&terrainCaptureTicks:nullptr);
            ++terrainAttempts;
            if(!indexed){if(terrainFailures++<12)logf("TERRAIN snapshot rejected: non-indexed buffer draw");return;}
            const auto* mirror=constantEpochSource.mirror;
            const auto* projectionRows=mirror?mirror->borrowKnownVertexFloats(4,4):nullptr;
#ifdef NORTHLIGHT_REPLAY_STAND
            terrainCameraBorrows+=projectionRows!=nullptr;
            terrainCameraGets+=projectionRows==nullptr;
#endif
            float projectionScratch[4][4];
            if(!projectionRows){
                if(FAILED(d->GetVertexShaderConstantF(4,projectionScratch[0],4))){if(terrainFailures++<12)logf("TERRAIN projection constants unavailable");return;}
                projectionRows=projectionScratch;
            }
            if(std::fabs(projectionRows[0][0]-projection[0])>.005f||std::fabs(projectionRows[1][1]-projection[1])>.005f||std::fabs(projectionRows[2][3]-projection[2])>.005f){if(terrainFailures++<12)logf("TERRAIN projection mismatch");return;}
            const auto* viewRows=mirror?mirror->borrowKnownVertexFloats(0,4):nullptr;
            TerrainSnapshot snapshot;NorthlightTerrainCapture::Diagnostics why;
            terrainBoundsCache.profileCapture(sample);
            const bool captured=terrainBoundsCache.readMeshShared(d,type,base,min,vertexTotal,start,count,context.view,snapshot,&why,viewRows);
#ifdef NORTHLIGHT_REPLAY_STAND
            terrainCameraBorrows+=why.viewChecked&&viewRows;
            terrainCameraGets+=why.viewChecked&&!viewRows;
#endif
            if(!captured){
                if(terrainFailures++<12)logf("TERRAIN snapshot rejected: %s hr=%08lx vertexUsage=%lx indexUsage=%lx type=%u stride=%u",NorthlightTerrainCapture::rejectName(why.reason),(unsigned long)why.hr,(unsigned long)why.vertexUsage,(unsigned long)why.indexUsage,why.positionType,why.stride);return;}
            if(const auto* native=NorthlightAdtBounds::current)
                nativeAdtCoverage.record(*native,why.low,why.high,&logf);
            if(positions){std::copy(why.low,why.low+3,positions->low);std::copy(why.high,why.high+3,positions->high);positions->known=true;}
            appendTerrain(snapshot);return;
        }
        captureModel(type,base,min,vertexTotal,start,count,indexed,current,sample,nullptr,D3DFMT_INDEX16,nullptr,0,captured,capturedDeclaration,pose,bank);
    }
#include "world_replay_upload.inl"
    bool giWorkEnabled()const{return NorthlightQuality::giWork(quality,effects.gi);}
    bool giActorWorkEnabled()const{return giWorkEnabled()&&quality.giDynamicProbes;}
    DWORD actorCaptureClock()const{
        return NorthlightFrameParityClock::fixedProfileWork()?NorthlightFrameParityClock::milliseconds(parityFrame):visualTime();
    }
    bool actorCaptureEnabled(){if(!menuDomain&&!giActorWorkEnabled())return false;
        if(!actorCaptureDecided){actorCaptureDecided=true;actorCaptureDue=!lastActorCapture||actorCaptureClock()-lastActorCapture>=200;}if(actorCaptureDue&&!actorJob)actorJob=std::make_shared<NorthlightActorGeometry::ActorJob>();return actorCaptureDue;}
    void appendActor(IDirect3DVertexShader9* original,const Replay& replay,bool sample=false){
        auto it=actorPrograms.find(original);if(it==actorPrograms.end())return;const auto& program=*it->second;
        if(!menuDomain&&!program.skinned&&!replay.mesh().dynamic)return;
        if(actorJob->packets.size()>=(menuDomain?1024u:128u))return;
        if(actorVerticesEvaluated+replay.mesh().vertexCount>(menuDomain?262144u:16384u))return;
        NorthlightCapturePhases::Scope<3> phase(sample?&actorPhases:nullptr);
        if(sample)++actorPhaseCandidates;
        const D3DVERTEXELEMENT9* elements=nullptr;UINT n=0;if(!declarationCache.get(replay.decl,elements,n))return;
        // Evaluate one vertex first to avoid skinning distant crowds. 96 units
        // allows large models intersecting the exact 48-unit triangle region.
        NorthlightActorDeformation::Four inputs[16];
        if(!NorthlightActorDeformation::sampledVertexInputsAt(program,replay.mesh(),elements,n,0,inputs))return;
        const float origin[3]={};float unusedSquared=0,firstWorld[3];
        if(!NorthlightActorDeformation::sampledDistanceFromInputs(program,inputs,replay.constants,context.inverseView,origin,unusedSquared,firstWorld))return;
        V firstPosition=vec(firstWorld);V distance=firstPosition-vec(context.camera);if(!menuDomain&&NorthlightGI::dot(distance,distance)>96*96)return;
        actorVerticesEvaluated+=replay.mesh().vertexCount;
        phase.next(1); // Copy current game texture; pure decode runs on the GI worker.
        NorthlightGI::WorldMaterial material;material.albedo=V(.35f,.35f,.35f);material.alphaCutoff=NorthlightShadowAlpha::recipe(replay.alphaMode)==NorthlightShadowAlpha::Color?0.f:std::max(0.f,replay.cutoff);
        NorthlightActorTexture::Snapshot texture;
        auto uvProgram=actorUVPrograms.find(original);bool hasUV=uvProgram!=actorUVPrograms.end();
        if(sample&&hasUV)++actorTextureReads;
        bool textureRead=hasUV&&NorthlightActorTexture::read(replay.texture,texture);
        if(replay.cutoff>=0&&!textureRead){++actorSkippedAlpha;return;}
        material.addressU=replay.addressU;material.addressV=replay.addressV;
        phase.next(2); // Immutable packet construction and queueing.
        NorthlightActorGeometry::Packet packet;packet.sharedMesh=replay.shared;if(!packet.sharedMesh)packet.mesh=replay.mesh();packet.sharedPosition=it->second;
        if(hasUV)packet.sharedUV=uvProgram->second;packet.hasUV=hasUV;packet.alphaTest=replay.cutoff>=0&&NorthlightShadowAlpha::recipe(replay.alphaMode)!=NorthlightShadowAlpha::Color;
        packet.elements.assign(elements,elements+n);std::memcpy(packet.constants.data(),replay.constants,sizeof packet.constants);std::memcpy(packet.inverseView.data(),context.inverseView,64);
        packet.material=std::move(material);packet.texture=std::move(texture);actorJob->packets.push_back(std::move(packet));++actorDraws;
        if(sample&&NorthlightRenderThreadProbe::profiling())try{replayGiPacked.push_back(&replay);}catch(...){} /* capture waste (RenderProfile) */
    }
    void finishActorScene(){
        if(!actorCaptureEnabled())return;lastActorCapture=actorCaptureClock();actorJob->center=vec(context.camera);actorJob->menu=menuDomain;
        actorJobComplete=std::move(actorJob);actorCaptureDue=false;++actorJobSerial_;actorSceneMap_=active?active->map:std::string{};
        if(NorthlightFrameParityClock::enabled()&&NorthlightDiagnostics::enabled())
            logf("GI_ACTOR_CAPTURE present=%u actor=%llu packets=%zu vertices=%u menu=%u",parityFrame,
                (unsigned long long)actorJobSerial_,actorJobComplete->packets.size(),actorVerticesEvaluated,unsigned(menuDomain));
        if(captureSampled)deferLogf("WORLD actor packets draws=%u queuedVertices=%u skippedAlpha=%u snapshotReadBytes=%zu material=actual-rgba128-or-neutral035",actorDraws,actorVerticesEvaluated,actorSkippedAlpha,replaySnapshots.bytesRead());
    }
    std::shared_ptr<const NorthlightActorGeometry::ActorJob> completedActorJob()const{return actorJobComplete;}
    uint64_t actorJobSerial()const{return actorJobSerial_;}
    const std::string& completedActorSceneMap()const{return actorSceneMap_;}
    void prepareStaticProofs(bool directionalActive){
        for(auto& p:replays){p->staticProofMask=p->staticM2Mask=0;p->staticM2Model=nullptr;}
        // Only the sun/moon cache slots consume these dedup proofs. Keep the
        // caster cache warm for source reentry and weather flashes.
        if(!directionalActive)return;
        if(!staticScene||staticScene->map!=lastRequest.map||!staticCasters.stats().readyModels||replays.empty())return;
        StaticShadowDedup::Budget budget;
        try {
            // Expensive shader evaluation is bounded too, not only matching.
            // Rotate the starting point so a difficult first draw cannot starve
            // other eligible rigid WMO packets indefinitely.
            const size_t start=staticDedupCursor++%replays.size();size_t evaluated=0;
            for(size_t j=0;j<replays.size()&&!budget.expired()&&evaluated<1024;++j){
                auto& p=*replays[(start+j)%replays.size()];const auto& mesh=p.mesh();
                auto program=actorPrograms.find(p.originalShader);
                if(!recognizesWmo(p.originalShader)||program==actorPrograms.end()||program->second->skinned||mesh.dynamic||p.cutoff>=0||!mesh.vertexCount||mesh.vertexCount>256||mesh.primitiveCount>512)continue;
                if(program->second->operations.size()*mesh.vertexCount>8192)continue;
                const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                if(!declarationCache.get(p.decl,elements,count))continue;
                evaluated+=mesh.vertexCount;++staticDedupAttempts;
                std::vector<NorthlightActorDeformation::Position> positions;
                if(!NorthlightActorDeformation::worldPositions(*program->second,mesh,elements,count,p.constants,context.inverseView,positions)||budget.expired())continue;
                std::vector<StaticShadowDedup::Triangle> triangles;
                if(!StaticShadowDedup::makeTriangles(positions,mesh.indices,mesh.indexed,mesh.primitiveCount,mesh.topology==D3DPT_TRIANGLESTRIP,triangles,512))continue;
                auto proof=staticMatcher.match(*staticScene,triangles,{true,false,false,false},budget,
                    [this](const StaticShadow::Placement& place,uint64_t revision){return staticCasters.coverage(place,revision)?15u:0u;});
                if(!proof.valid)continue;
                ++staticDedupMatches;p.staticProofMask=proof.slotMask;p.staticProofModel=proof.modelKey;p.staticProofRevision=proof.modelRevision;
                p.staticProofLow=V(INFINITY,INFINITY,INFINITY);p.staticProofHigh=V(-INFINITY,-INFINITY,-INFINITY);
                for(const auto& t:triangles)for(auto v:t){p.staticProofLow.x=std::min(p.staticProofLow.x,v.x);p.staticProofLow.y=std::min(p.staticProofLow.y,v.y);p.staticProofLow.z=std::min(p.staticProofLow.z,v.z);p.staticProofHigh.x=std::max(p.staticProofHigh.x,v.x);p.staticProofHigh.y=std::max(p.staticProofHigh.y,v.y);p.staticProofHigh.z=std::max(p.staticProofHigh.z,v.z);}
            }
        }catch(...){for(auto& p:replays)p->staticProofMask=0;}
        try{prepareStaticM2();}catch(...){for(auto& p:replays){p->staticM2Mask=0;p->staticM2Model=nullptr;}}
    }
    // Rigid M2 doodads (static_m2_dedup.h): a captured opaque skinned draw that uses one bone of an audited palette
    // program, whose bone's world matrix is a static-cache placement (rigidStaticCovered's match) that has not moved
    // for StableFrames capture frames, and whose world-space vertices coincide with the placement's opaque model
    // vertices. The decision is remembered per identity (shared snapshot, program, declaration, range, matrix); the
    // matrix compare runs every capture frame, so a doodad that starts to move (a door, a windmill) gets its replay
    // back at once. staticM2Mask then names the slots whose cache holds the model; the replay loop also tests the
    // slot's cached matrix against the bounds. Needs the rigid placement index of this scene (a track builds it).
    void prepareStaticM2(){
        if(!StaticM2Dedup::Enabled||!staticScene||staticScene->map!=lastRequest.map||!staticCasters.stats().readyModels||!rigidIndexCurrent())return;
        const auto started=std::chrono::steady_clock::now();const unsigned long long sceneRevision=rigidSceneRevision(*staticScene);
        ++staticM2Frame;unsigned verifications=0;std::vector<NorthlightActorDeformation::Position> world;
        for(auto& up:replays){auto& p=*up;
            if(!p.shadowSkinned||p.cutoff>=0||!p.shared||p.type!=D3DPT_TRIANGLELIST)continue;
            const auto* program=rigidProgram(p.originalShader);if(!program)continue;
            float bone=NAN;
            if(p.boneKnown)bone=p.bone;
            else{const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;if(declarationCache.get(p.decl,elements,count))bone=prepareCaches->bones.bone(*program,p.mesh(),p.shared,p.decl,elements,count);}
            float matrix[12];
            if(!(bone>=0&&bone<=74&&std::floor(bone)==bone)||!NorthlightRigidGeometry::worldBone(p.constants+4*(31+3*unsigned(bone)),context.inverseView,matrix))continue;
            auto& e=staticM2Memory.touch(p.shared,p.originalShader,p.decl,p.start,p.count,matrix,staticM2Frame);
            if(e.sceneRevision!=sceneRevision){e.sceneRevision=sceneRevision;e.state=StaticM2Dedup::Entry::Unverified;}
            StaticM2Dedup::Facts facts;facts.rigid=true;facts.stableFrames=e.stable;
            if(e.state==StaticM2Dedup::Entry::Unverified&&e.stable>=StaticM2Dedup::StableFrames&&verifications<2&&
               std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<.3){
                ++verifications;e.state=StaticM2Dedup::Entry::Rejected;
                size_t found=SIZE_MAX;const auto& all=staticScene->placements;
                rigidIndex.find(matrix+9,.05f,[&](std::uint32_t i){const auto& place=all[i];const float t[3]={place.translation.x,place.translation.y,place.translation.z};
                    if(!NorthlightRigidGeometry::staticPlacement(matrix+9,matrix,t,place.matrix))return false;found=i;return true;});
                const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;auto model=found==SIZE_MAX?staticScene->models.end():staticScene->models.find(all[found].modelKey);
                if(found!=SIZE_MAX&&model!=staticScene->models.end()&&model->second&&declarationCache.get(p.decl,elements,count)&&p.mesh().vertexCount<=4096&&
                   NorthlightActorDeformation::worldPositions(*program,p.mesh(),elements,count,p.constants,context.inverseView,world)&&
                   StaticM2Dedup::boundsOf(world,e.low,e.high)&&StaticM2Dedup::inside(all[found],e.low,e.high)&&
                   p.count==p.mesh().primitiveCount&&StaticM2Dedup::covers(*model->second,all[found],world,p.mesh().indices,p.mesh().indexed,p.count)){
                    e.state=StaticM2Dedup::Entry::Matched;e.placementIndex=found;e.placement=all[found];e.revision=model->second->contentRevision;++staticM2Verified;}
                else ++staticM2Rejected;
            }
            facts.placed=facts.modelMatches=e.state==StaticM2Dedup::Entry::Matched;
            if(facts.placed){facts.boundsInside=StaticM2Dedup::inside(e.placement,e.low,e.high);facts.cacheValid=staticCasters.coverageAt(e.placementIndex,e.placement,e.revision);}
            if(StaticM2Dedup::decide(facts)!=StaticM2Dedup::Verdict::Skip)continue;
            p.staticM2Mask=15u;p.staticM2Model=&e.placement.modelKey;p.staticProofRevision=e.revision;p.staticProofLow=e.low;p.staticProofHigh=e.high;
        }
    }
    uint64_t staticSignature(const float* matrix){
        try {return staticCasters.signature(matrix);}
        catch(...){staticCasters.reset();staticOwnerGeneration=UINT64_MAX;staticRetryTick=GetTickCount();return 0;}
    }
    NorthlightLocalShadowSignature::Digest localShadowSignature(ShadowCacheKey& key,const float* matrix){
        return key.localMemo.get(uploadedLocalShadowRecords.get(),meshGeneration,fixedTerrainChunks(),matrix);
    }
    bool drawStaticCasters(const float* matrix,const std::vector<NorthlightShadowBounds::TexelRect>* rects=nullptr){
        try {if(staticCasters.draw(d,matrix,rects,long(ShadowCacheSize),StaticCacheDirtyMargin))return true;}catch(...){}
        if(staticDrawFailures++<8)logf("STATIC SHADOW draw retry: base terrain/local shadows retained");
        staticCasters.reset();staticOwnerGeneration=UINT64_MAX;staticRetryTick=GetTickCount();return false;
    }
#include "world_rigid_memory.inl"
#include "world_shadow_experiment.inl"
#include "world_memory_guard.inl"
#include "world_replay_probe.inl"
    bool shadowCaptureDemand()const{
        if(!NorthlightQuality::actorShadowWork(quality,effects.shadows)||!active||!active->bvh||failed||workerFault())return false;
        // Context is accepted once per frame; endFrame clears valid. Unknown
        // celestial reads/maps cannot prove the absence of a directional draw.
        if(!valid||!celestialValid||active->map!=lastRequest.map)return true;
        if(!(NorthlightGI::dot(sourceColors[0],sourceColors[0])<=1e-10f&&NorthlightGI::dot(sourceColors[1],sourceColors[1])<=1e-10f))return true;
        return pointRefreshPredicted();
    }
    // The frame's first model draw decides whether this frame captures replays at all
    // (NorthlightQuality::skipModelCapture). Terrain capture and the game's draws are unaffected.
    bool modelCaptureSkipped(){
        if(!pipelineActive)return true;
        if(menuDomain){captureMode=CaptureFresh;return false;}
        if(captureMode==CaptureUndecided){bool skip=false;
            // Unavailable geometry cannot consume directional or point replays.
            // Keep the existing GI cadence, pending-demand and diagnostic exceptions.
            const bool shadows=shadowCaptureDemand();
            if(!particleLights.hasFlash()&&NorthlightQuality::captureSkipPossible(quality,shadows)&&(!quality.localLights||!quality.localLightLimit||!lightAttachments.needsDrawPose())){NorthlightQuality::CaptureInputs in;
                in.shadows=shadows;in.demand=captureDemand;in.diagnostic=lastRenderDebug==1;in.actorDue=actorCaptureEnabled();in.nextPass=shadowPasses+1;
                for(int source=0;source<2;++source)in.sourceActive[source]=NorthlightGI::dot(sourceColors[source],sourceColors[source])>1e-10f;
                in.pointDue=in.shadows&&!in.actorDue&&!in.demand&&pointRefreshPredicted();
                skip=NorthlightQuality::skipModelCapture(quality,in,nearShadow,farShadow);}
            captureMode=skip?CaptureSkipped:CaptureFresh;if(skip)++captureSkippedFrames;else captureDemand=false;}
        return captureMode==CaptureSkipped;
    }
    // The 3 palette rows (bone 0, at paletteBase) of the draw about to be issued, answered by the
    // device mirror. Null: not a palette program, or the rows cannot be read.
    const NorthlightActorDeformation::Program* paletteRows(IDirect3DVertexShader9* shader,float* rows){
        auto program=actorPrograms.find(shader);
        if(program==actorPrograms.end()||program->second->paletteBase<0||program->second->paletteBase+2>=256||FAILED(d->GetVertexShaderConstantF(UINT(program->second->paletteBase),rows,3)))return nullptr;
        return program->second.get();
    }
    // Palette root (bone 0 origin, world) of the draw about to be issued: paletteRows() through
    // the capture's inverse view. False: not a palette program, or the rows cannot be read.
    bool drawRoot(IDirect3DVertexShader9* shader,float* root,const NorthlightActorDeformation::Program** used=nullptr){
        float rows[12];const auto* program=paletteRows(shader,rows);if(!program)return false;if(used)*used=program;
        float bank[4*256];std::memcpy(bank+4*program->paletteBase,rows,sizeof rows);
        return NorthlightActorDeformation::rootWorld(*program,bank,context.inverseView,root);
    }
    // 0.3.172 near capture reserve (near_reserve.h): asked only for a skinned draw the budget is
    // about to turn away for bytes. The anchor is taken once per capture frame, at the first ask
    // (the previous selection's self and this frame's camera; shadowPivot() is not called).
    NorthlightNearReserve::Anchor nearAnchor;bool nearAnchorReady=false;
    unsigned nearAdmitted=0,nearRefused=0;size_t nearBytes=0;
    /* 0.3.211 (task 31): capture admission (capture_admission.h). The centre is the near anchor's: taken once per capture frame at the first
       skinned draw (the previous selection's self, else the pivot eye+forward*pivotDistance). admissionOn: ActorShadows and a usable centre. */
    /* The root of consecutive skinned draws is read once: the mirror's write serials prove the palette rows unchanged (RootMemo). Inside the
       game draw's guard like captureBlocks(); a source that cannot certify falls back to the read. Cleared every frame (new inverse view). */
    NorthlightCaptureAdmission::RootMemo rootMemo;unsigned rootReads=0,rootMemoHits=0;
    bool drawRootMemo(IDirect3DVertexShader9* shader,float* root){
        if(!constantEpochSource.attached()||!constantEpochSource.valid()){++rootReads;return drawRoot(shader,root);}
        const auto& live=constantEpochSource.live();
        if(rootMemo.hit(shader,live.reset,live.floatBlock)){++rootMemoHits;if(rootMemo.valid)std::memcpy(root,rootMemo.root,sizeof rootMemo.root);return rootMemo.valid;}
        ++rootReads;const NorthlightActorDeformation::Program* program=nullptr;const bool ok=drawRoot(shader,root,&program);
        if(program)rootMemo.store(shader,unsigned(program->paletteBase),live.reset,live.floatBlock,root,ok); /* paletteRows() proved 0 <= paletteBase, paletteBase+2 < 256 */
        return ok;
    }
    NorthlightCaptureAdmission::Admission admission;bool admissionOn=false;std::vector<float> gatedBodies; /* roots of radius-gated bodies, for rigid memory */
    bool actorCaptureWouldRun()const{return giActorWorkEnabled()&&(actorCaptureDecided?actorCaptureDue:(!lastActorCapture||actorCaptureClock()-lastActorCapture>=200));}
    void nearAnchorTake(){
        if(!nearAnchorReady){nearAnchorReady=true;const float forward[3]={context.inverseView[8]*projection[2],context.inverseView[9]*projection[2],context.inverseView[10]*projection[2]};
            nearAnchor=NorthlightNearReserve::anchor(actorShadowHistory.selfHold()>0?actorShadowHistory.selfAt():nullptr,context.camera,forward,pivotDistance);
            NorthlightCaptureAdmission::Context centre;float pivot[3];centre.camera=context.camera;centre.yards=float(effectiveActorShadowRadius());centre.gi=actorCaptureWouldRun();
            if(nearAnchor.self){centre.centre=nearAnchor.selfAt;centre.self=true;}
            else if(nearAnchor.ray){for(unsigned k=0;k<3;++k)pivot[k]=nearAnchor.eye[k]+nearAnchor.forward[k]*pivotDistance;centre.centre=pivot;}
            admissionOn=quality.actorShadows&&centre.centre;if(admissionOn)admission.arm(centre);}
    }
    bool nearDraw(const float* root){return root&&NorthlightNearReserve::test(nearAnchor,root)!=NorthlightNearReserve::None;}
    bool nearCandidate(bool priority,const float* root){
        return priority&&replaySnapshots.nearReserve()&&!replaySnapshots.countExhausted(priority)&&nearDraw(root);}
    void captureModel(D3DPRIMITIVETYPE type,INT base,UINT minimum,UINT vertexTotal,UINT start,UINT count,bool indexed,IDirect3DVertexShader9* current,bool sample,const void* userIndices,D3DFORMAT userFormat,const void* userVertices,UINT userStride,const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>* capturedSource=nullptr,IDirect3DVertexDeclaration9* capturedDeclaration=nullptr,const NorthlightPoseBounds::Handoff* pose=nullptr,const NorthlightConstantEpoch::BankHandoff* bank=nullptr){
        if(modelCaptureSkipped())return; /* previous replays/actor packets are not needed this frame */
        const CaptureShader* found=lookupShader(current).capture;if(!found){if(sample)++unknownCaptureCalls;return;}
        const auto& metadata=*found;
        if(metadata.detailDoodad){
            ++detailCandidates;
            const unsigned vertices=indexed?vertexTotal:type==D3DPT_TRIANGLELIST?count*3:count+2;
            if(!NorthlightQuality::grassShadowWork(quality,effects.shadows)||count>8192||!detailBudget.room(vertices)){++detailBudgetRejected;return;}
        }
        // 0.3.173 rigid memory: did the game draw a remembered prop? Before every capture rejection.
        if(!rigidDrawKeys.empty()&&rigidDrawKeys.contains(current,count))rigidMemoryDrawn(current,count);
        // Shadow fate (diagnostic window only): the outcome recorded at return.
        struct FateScope {NorthlightShadowFate::Tracker& tracker;int slot=-1;NorthlightShadowFate::Fate reason=NorthlightShadowFate::Unknown;
            ~FateScope(){tracker.record(slot,reason,false);}} fate{shadowFate};
        if(shadowFate.active()){NorthlightShadowFate::Key key;IDirect3DVertexBuffer9* vb=nullptr;UINT offset=0,stride=0;IDirect3DIndexBuffer9* ib=nullptr;
            if(!userVertices&&SUCCEEDED(d->GetStreamSource(0,&vb,&offset,&stride))&&vb){key.vb=reinterpret_cast<std::uintptr_t>(vb);vb->Release();}
            else key.vb=reinterpret_cast<std::uintptr_t>(NorthlightStream::upIdentity?NorthlightStream::upIdentity:userVertices); /* 0.3.192 (CS): the game's pointer, not the recorded copy's (identity only; the data is read from userVertices) */
            if(indexed&&!userIndices&&SUCCEEDED(d->GetIndices(&ib))&&ib){key.ib=reinterpret_cast<std::uintptr_t>(ib);ib->Release();}
            key.shader=current;key.base=base;key.start=start;key.count=count;key.minimum=minimum;
            // Per instance: the palette root (identical models share buffers and ranges).
            float root[3];if(drawRoot(current,root))for(unsigned a=0;a<3;++a)key.cell[a]=std::int32_t(std::floor(root[a]));
            fate.slot=shadowFate.slot(key,metadata.skinned,count);fate.reason=NorthlightShadowFate::Cap4096;}
        if(replays.size()>=4096){captureShortfall=true;return;}
        if(sample)++replayCaptureCalls;CpuScope cpu(sample?&replayCaptureTicks:nullptr);
        // Rotate the 1/16 draw subset between sampled frames. No timing calls
        // or new diagnostic counters run in this path on ordinary frames.
        const bool detailed=sample&&((replayCaptureCalls-1)&15u)==(capturePhaseSerial&15u);
        NorthlightCapturePhases::Scope<CapturePhaseCount> phase(detailed?&capturePhases:nullptr,CaptureState);
        if(detailed)++capturePhaseDraws;
        const bool priority=metadata.skinned;
        if(sample&&priority)++skinnedCandidates;
        const unsigned triangleBin=NorthlightReplayShadowPolicy::bucket(count);
        if(sample){++replayTriangleBins[triangleBin];if(priority)++skinnedTriangleBins[triangleBin];}
        bool smallShadow=(type==D3DPT_TRIANGLELIST||type==D3DPT_TRIANGLESTRIP)&&
            NorthlightReplayShadowPolicy::small(priority,count,minSkinnedShadowTriangles);
        const bool smallNeedsProof=smallShadow&&metadata.program&&metadata.program->skinned;
        // Preserve actor GI captures at their existing 200 ms cadence. On other
        // frames skip only this shadow capture, before its device state queries.
        // The game's original draw and the shared draw-domain gate are untouched.
        fate.reason=NorthlightShadowFate::Small;
        if(!smallNeedsProof){if(smallShadow&&!actorCaptureEnabled()){if(sample)++smallShadowEarly;++rejSmall;return;}}
        // 0.3.215 (task 47): a faded actor (stealth, ghosts) draws a depth-only pre-pass (no RGB) before its blended colour pass.
        // It looks opaque, so reject it here, before the admission, the budget and the near reserve (its weapons and parts too).
        const D3DRENDERSTATETYPE captureRasterKeys[]={D3DRS_COLORWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE};DWORD captureRaster[3];
        const bool rasterKnown=captureRasterReader&&captureRasterReader(captureRasterContext,captureRasterKeys,3,captureRaster);
        auto readRaster=[&](unsigned slot,DWORD& value){if(rasterKnown){value=captureRaster[slot];return true;}return SUCCEEDED(d->GetRenderState(captureRasterKeys[slot],&value));};
        fate.reason=NorthlightShadowFate::Fade;
        {DWORD colorWrite=15;if(readRaster(0,colorWrite)&&(colorWrite&7)==0){if(sample&&priority)++skinnedFadeRejected;if(sample&&!priority)++otherFadeRejected;return;}}
        // Stock grass is straight-alpha blended even when it writes a cutout
        // depth pass. Prove its PS before allowing this exception; actors and
        // unknown/additive vegetation retain the normal conservative rejection.
        bool detailPixel=false;DWORD blend=0;
        if(metadata.detailDoodad){
            NorthlightDrawSnapshot::Ref<IDirect3DPixelShader9> pixel;
            if(FAILED(d->GetPixelShader(pixel.out()))){++detailMaskRejected;return;}
            const auto it=fogShaders.find(pixel.p);detailPixel=it!=fogShaders.end()&&it->second.detailDoodad;
            if(!detailPixel){++detailMaskRejected;return;}
        }
        fate.reason=NorthlightShadowFate::Blend;
        bool blendAllowed=readRaster(1,blend);
        if(blendAllowed&&blend){
            DWORD source=0,destination=0,operation=0;
            blendAllowed=metadata.detailDoodad&&SUCCEEDED(d->GetRenderState(D3DRS_SRCBLEND,&source))&&
                SUCCEEDED(d->GetRenderState(D3DRS_DESTBLEND,&destination))&&SUCCEEDED(d->GetRenderState(D3DRS_BLENDOP,&operation))&&
                NorthlightDetailShadow::blendedMaterial(detailPixel,source,destination,operation);
        }
        if(!blendAllowed){if(metadata.detailDoodad)++detailBlendRejected;if(sample&&priority)++skinnedBlendRejected;if(sample&&!priority)++otherBlendRejected;return;}
        // 0.3.211 (task 31): the palette root once per skinned draw, then the capture admission (radius gate, distance cut)
        // before any budget is spent; near-reserve hits always pass. A draw without a root is never gated or cut.
        float root[3];bool rooted=false;NorthlightCaptureAdmission::Decision admit;
        if(priority&&quality.actorShadows&&!menuDomain){nearAnchorTake();rooted=drawRootMemo(current,root);
            if(admissionOn){admit=admission.decide(reinterpret_cast<std::uintptr_t>(current),count,vertexTotal,rooted?root:nullptr,nearDraw(rooted?root:nullptr));
                if(admit.verdict!=NorthlightCaptureAdmission::Verdict::Admit){
                    if(admit.verdict==NorthlightCaptureAdmission::Verdict::RadiusGate)++rejGate;else ++rejCut;
                    if(admit.verdict==NorthlightCaptureAdmission::Verdict::RadiusGate){fate.reason=NorthlightShadowFate::RadiusGate;gatedBodies.insert(gatedBodies.end(),root,root+3);}
                    else{fate.reason=NorthlightShadowFate::DistanceCut;captureShortfall=true;}
                    return;}}}
        fate.reason=NorthlightShadowFate::CaptureBudget;
        // Only reject when no legal snapshot can fit, including cache hits
        // and UP draws. A previous oversized failure is not an exhaustion proof.
        // A near skinned draw may read from the near reserve once the main budget is spent.
        bool nearby=false;
        if(replaySnapshots.captureExhausted(priority)){const bool candidate=nearCandidate(priority,rooted?root:nullptr);
            if(candidate&&!replaySnapshots.captureExhausted(priority,true))nearby=true;
            else{nearRefused+=candidate;captureShortfall=true;++rejBudget;if(admit.rooted)admission.budgetRefused(admit,root,vertexTotal,count);
                if(sample&&priority){++skinnedSnapshotRejected;++skinnedBudgetRejected;}
                if(sample&&!priority)++otherBudgetRejected;
                return;}
        }
        DWORD alpha=0,ref=0,alphaFunc=D3DCMP_ALWAYS;
        fate.reason=NorthlightShadowFate::AlphaFunc;
        if(!readRaster(2,alpha))return;
        if(alpha){if(FAILED(d->GetRenderState(D3DRS_ALPHAREF,&ref))||FAILED(d->GetRenderState(D3DRS_ALPHAFUNC,&alphaFunc)))return;}
        else if(sample)alphaStateQueriesSkipped+=2;
        if(alpha&&alphaFunc!=D3DCMP_GREATER&&alphaFunc!=D3DCMP_GREATEREQUAL)return;
        float shaderCutoff=-1;unsigned alphaMode=0;
        if(alpha){
            NorthlightDrawSnapshot::Ref<IDirect3DPixelShader9> pixel;
            if(SUCCEEDED(d->GetPixelShader(pixel.out()))){
                const auto it=fogShaders.find(pixel.p);
                if(it!=fogShaders.end()&&it->second.alpha.outputKnown){
                    NorthlightDrawSnapshot::Ref<IDirect3DSurface9> target;D3DSURFACE_DESC desc{};
                    if(SUCCEEDED(d->GetRenderTarget(0,target.out()))&&target.p&&SUCCEEDED(target->GetDesc(&desc)))
                        alphaMode=NorthlightShadowAlpha::fixedTestMode(it->second.alpha,metadata.color0W,ref,alphaFunc==D3DCMP_GREATEREQUAL,unsigned(desc.Format));
                    // Unsupported MRT hardware retains the proved mask recipe
                    // with legacy clip precision, without retrying every map.
                    if(NorthlightShadowAlpha::fixed(alphaMode)&&!shadowReplayAlpha.supported(desc.Format))
                        alphaMode=unsigned(NorthlightShadowAlpha::recipe(alphaMode));
                    if(sample&&NorthlightShadowAlpha::fixed(alphaMode))++shaderAlphaCaptured;
                }
            }
        }else{
            NorthlightDrawSnapshot::Ref<IDirect3DPixelShader9> pixel;
            if(FAILED(d->GetPixelShader(pixel.out())))return;
            const auto it=fogShaders.find(pixel.p);
            if(it!=fogShaders.end()&&it->second.alpha.discard){
                const auto& mask=it->second.alpha;
                if(!mask.known||!metadata.color0W){if(sample)++shaderAlphaRejected;return;}
                float value[4];if(FAILED(d->GetPixelShaderConstantF(mask.ref,value,1)))return;
                shaderCutoff=value[mask.component];if(!std::isfinite(shaderCutoff)||shaderCutoff<0||shaderCutoff>1){if(sample)++shaderAlphaRejected;return;}
                alphaMode=unsigned(mask.mode);if(sample)++shaderAlphaCaptured;
            }
        }
        // Preserve native alpha-test cutoffs. Pure blended grass uses stable
        // UV coverage instead of turning a translucent blade into an opaque quad.
        if(metadata.detailDoodad){
            if(!alpha&&!blend){++detailMaskRejected;return;}
            if(!NorthlightShadowAlpha::fixed(alphaMode))alphaMode=alpha?1:3;if(!alpha)shaderCutoff=0;
        }
        // Validate the main camera before touching model buffers. Full constant
        // banks are copied only for snapshots that survive range/budget checks.
        phase.next(CaptureProjection);fate.reason=NorthlightShadowFate::Projection;
        float projectionScratch[16];const unsigned kind=metadata.projectionKind,first=kind==1?4:2;
        const auto* packetBank=bank;
        const auto* wmoMirror=metadata.wmo&&indexed&&!userIndices&&!userVertices?constantEpochSource.mirror:nullptr;
        const auto* projectionMirror=wmoMirror;
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool legacyProjectionBank=[]{
            const char* value=std::getenv("NORTHLIGHT_RETRACE_LEGACY_SHADOW_PROJECTION_BANK");
            const bool legacy=value&&value[0]=='1'&&!value[1];
            logf("SHADOW_CAPTURE_PROJECTION policy legacy=%u",unsigned(legacy));
            return legacy;
        }();
        if(legacyProjectionBank){bank=nullptr;projectionMirror=nullptr;}
#endif
        const float* q=constantEpochSource.borrow(bank,first,4);
#ifdef NORTHLIGHT_REPLAY_STAND
        if(q)++captureProjectionLoans;
#endif
        const auto* projectionRows=!q&&projectionMirror?projectionMirror->borrowKnownVertexFloats(first,4):nullptr;
#ifdef NORTHLIGHT_REPLAY_STAND
        if(projectionRows)++captureWmoProjectionBorrows;
#endif
        if(!q&&!projectionRows){
#ifdef NORTHLIGHT_REPLAY_STAND
            ++captureProjectionGets;
#endif
            if(FAILED(d->GetVertexShaderConstantF(first,projectionScratch,4)))return;
            q=projectionScratch;
            if(sample){capturedConstantBytes+=64;++capturedConstantCalls;}
        }
        const auto projectionValue=[q,projectionRows](unsigned index){return q?q[index]:projectionRows[index/4][index%4];};
        if(std::fabs(projectionValue(0)-projection[0])>.005f||std::fabs(projectionValue(5)-projection[1])>.005f||std::fabs(projectionValue(kind==1?11:14)-projection[2])>.005f||std::fabs(projectionValue(15))>.001f){if(sample&&priority)++skinnedProjectionRejected;if(sample&&!priority)++otherProjectionRejected;return;}
        // A rejected draw returns its record to the pool as well. Exhausted
        // geometry budgets must not allocate/zero a fresh constant bank per draw.
        phase.next(CaptureSnapshot);fate.reason=NorthlightShadowFate::Snapshot;
        std::unique_ptr<Replay,ReplayRecycle> p(acquireReplay().release(),ReplayRecycle{this});p->shadowSkinned=priority;p->shadowSelected=!smallShadow;p->shadowSmall=smallShadow;p->boneKnown=false;p->selfShadow=false;p->captureRootValid=rooted;if(rooted)std::memcpy(p->captureRoot,root,sizeof p->captureRoot);p->anchorValid=false;p->fateSlot=-1;p->fateClass=smallShadow?NorthlightShadowFate::Small:NorthlightShadowFate::NotRanked;p->fateDistance=0;p->projectionKind=kind;p->shader=metadata.replacement;p->shader->AddRef();p->originalShader=current;p->originalShader->AddRef();p->pointBounds={};
        if(replaySnapshots.lookup()==NorthlightDrawSnapshot::Frame::Lookup::Prefetch&&!freeReplays.empty())prefetchReplay(*freeReplays.back()); /* 0.3.181 (S2) */
        if(FAILED(d->GetVertexDeclaration(&p->decl))||!p->decl)return;
        if(metadata.detailDoodad){
            const D3DVERTEXELEMENT9* elements=nullptr;UINT n=0;
            if(!declarationCache.get(p->decl,elements,n))return;
            for(UINT i=0;i<n&&elements[i].Stream!=0xff;++i)if(elements[i].Stream!=0)return;
            UINT stride=userStride;
            if(!userVertices){NorthlightDrawSnapshot::Ref<IDirect3DVertexBuffer9> vb;UINT offset=0;if(FAILED(d->GetStreamSource(0,vb.out(),&offset,&stride)))return;}
            if(!stride||stride>64)return;
            const unsigned vertices=indexed?vertexTotal:type==D3DPT_TRIANGLELIST?count*3:count+2;
            const size_t bytes=size_t(vertices)*stride+(indexed?size_t(count)*3*4:0);
            if(!detailBudget.reserve(vertices,bytes)){++detailBudgetRejected;return;}
        }
        NorthlightDrawSnapshot::Draw draw{type,base,minimum,vertexTotal,start,count,indexed};NorthlightDrawSnapshot::Diagnostics why;
        const size_t readBefore=replaySnapshots.bytesRead();
        replaySnapshots.sampleMaintenance(sample);
        const bool borrowSource=!userVertices&&capturedSource&&capturedDeclaration==p->decl;
        auto read=[&]{
            if(borrowSource){bool served=false;if(!replaySnapshots.readShared(p->decl,draw,*capturedSource,p->shared,served,&why,priority,nearby))return false;if(served){p->snapshot.prepare();++captureSourceHandoffs;return true;}}
            return userVertices?replaySnapshots.readUP(p->decl,draw,userIndices,userFormat,userVertices,userStride,p->snapshot,&why,priority,nearby):replaySnapshots.read(d,p->decl,draw,p->snapshot,&why,priority,&p->shared,nearby);
        };
        bool captured=read();size_t nearFrom=readBefore;
        // Turned away for bytes: a near draw is read once more, from the near reserve.
        if(!captured&&why.error==NorthlightDrawSnapshot::Error::Budget&&!nearby&&nearCandidate(priority,rooted?root:nullptr)){
            nearby=true;nearFrom=replaySnapshots.bytesRead();captured=read();if(!captured)nearRefused+=why.error==NorthlightDrawSnapshot::Error::Budget;}
        if(captured&&nearby){++nearAdmitted;nearBytes+=replaySnapshots.bytesRead()-nearFrom;}
        if(captured&&admit.rooted)admission.admitted(admit,reinterpret_cast<std::uintptr_t>(current),count,root,replaySnapshots.bytesRead()-readBefore);
        if(!captured){if(why.error==NorthlightDrawSnapshot::Error::Budget){fate.reason=NorthlightShadowFate::CaptureBudget;captureShortfall=true;if(admit.rooted)admission.budgetRefused(admit,root,vertexTotal,count);}
            if(sample){captureRejectedBytes+=replaySnapshots.bytesRead()-readBefore;if(priority){++skinnedSnapshotRejected;skinnedBudgetRejected+=why.error==NorthlightDrawSnapshot::Error::Budget;}else otherBudgetRejected+=why.error==NorthlightDrawSnapshot::Error::Budget;}if(snapshotRejects++<12)logf("MODEL snapshot rejected: %s hr=%08lx stream=%u",NorthlightDrawSnapshot::errorName(why.error),(unsigned long)why.hr,why.stream);return;}
        // M2 fixtures share actor shaders. A single-influence shader alone is
        // insufficient: every captured vertex must use the same bone.
        if(smallNeedsProof){
            const D3DVERTEXELEMENT9* elements=nullptr;UINT n=0;
            const float bone=declarationCache.get(p->decl,elements,n)?smallShadowBones.bone(*metadata.program,p->mesh(),p->shared,p->decl,elements,n,metadata.program->kernel.singleSkin()?1:4):NAN;
            smallShadow=NorthlightReplayShadowPolicy::small(priority,count,minSkinnedShadowTriangles,std::isfinite(bone)&&bone>=0&&bone<=74&&std::floor(bone)==bone);
            p->shadowSelected=!smallShadow;p->shadowSmall=smallShadow;p->fateClass=smallShadow?NorthlightShadowFate::Small:NorthlightShadowFate::NotRanked;
            if(smallShadow&&!actorCaptureEnabled()){fate.reason=NorthlightShadowFate::Small;++rejSmall;return;}
        }
        phase.next(CaptureConstants);fate.reason=NorthlightShadowFate::Constants;
        const auto& usage=metadata.usage;
        p->constantUsage=usage;
        const auto f=usage.floats,b=usage.booleans,i=usage.integers;
        const Replay* previous=replays.empty()?nullptr:replays.back().get();
        const auto* packetMirror=wmoMirror;
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool legacyPacketBank=[]{
            const char* value=std::getenv("NORTHLIGHT_RETRACE_LEGACY_SHADOW_PACKET_BANK");
            const bool legacy=value&&value[0]=='1'&&!value[1];
            logf("SHADOW_CAPTURE_PACKET_BANK policy legacy=%u",unsigned(legacy));
            return legacy;
        }();
        if(legacyPacketBank){packetBank=nullptr;packetMirror=nullptr;}
#endif
        const auto readFloatRows=[&](unsigned first,unsigned count,float* out){
            if(FAILED(d->GetVertexShaderConstantF(first,out,count)))return false;
#ifdef NORTHLIGHT_REPLAY_STAND
            ++capturePacketReads;capturePacketReadRegisters+=count;
#endif
            if(sample){capturedConstantBytes+=16*count;++capturedConstantCalls;}
            return true;
        };
        const auto captureFloatRows=[&](unsigned first,unsigned count,float* out){
            if(constantEpochSource.copy(packetBank,first,count,out)){
#ifdef NORTHLIGHT_REPLAY_STAND
                ++capturePacketLoans;capturePacketLoanRegisters+=count;
#endif
                return true;
            }
            if(packetMirror){
                if(const auto* rows=packetMirror->borrowKnownVertexFloats(first,count)){
                    std::memcpy(out,rows,std::size_t(count)*sizeof(*rows));
#ifdef NORTHLIGHT_REPLAY_STAND
                    ++captureWmoPacketBorrows;captureWmoPacketRegisters+=count;
#endif
                    return true;
                }
            }
            return readFloatRows(first,count,out);
        };
        if(!NorthlightReplayCaptureConstants::captureBlocks<NorthlightReplayCaptureConstants::SharedFloatBanks>(*p,previous,constantEpochSource,[&]{
            float* owned=f.count?p->writableConstants():nullptr;
            if((f.count&&(!owned||!captureFloatRows(f.first,f.count,owned+4*f.first)))||
               (b.count&&FAILED(d->GetVertexShaderConstantB(b.first,p->boolStorage+b.first,b.count)))||
               (i.count&&FAILED(d->GetVertexShaderConstantI(i.first,p->intStorage+4*i.first,i.count))))return false;
            if(sample){capturedConstantBytes+=4*b.count+16*i.count;
                capturedConstantCalls+=(b.count!=0)+(i.count!=0);}
            return true;
        },captureFloatRows,sample?&constantEpochProfile:nullptr,constantSelfCheck,constantSelfCheckState,readFloatRows)){
            captureShortfall=true;return;
        }
        p->materialViewBounds={};
        if(kind==2&&pose&&pose->bounds.valid&&borrowSource&&p->shared&&p->shared==*capturedSource&&
           constantEpochSource.attached()&&constantEpochSource.mirror->heldByThisThread()&&
           pose->matches(p->shared.get(),current,p->decl,p->constantStamp)&&d->GetNPatchMode()==0)
            p->materialViewBounds=pose->bounds;
        if(sample){capturedSM1Draws+=metadata.sm1;capturedRelativeDraws+=usage.relativeFloat;}
        phase.next(CaptureMaterial);fate.reason=NorthlightShadowFate::Material;
        observeLightAttachments(*p,metadata);
        // Opaque shadows do not sample stage 0. GI still needs its actual albedo
        // and addressing on actor-update frames; float alpha retains the guard.
        const bool captureActor=!metadata.detailDoodad&&actorCaptureEnabled();
        const bool materialNeeded=alpha||alphaMode==1||alphaMode==3||shaderCutoff>=0||
            !NorthlightReplayDrawState::skipOpaque()||captureActor;
        if(materialNeeded){
            if(FAILED(d->GetTexture(0,&p->texture))||((alpha||alphaMode==1||alphaMode==3)&&!p->texture))return;
            if(FAILED(d->GetSamplerState(0,D3DSAMP_ADDRESSU,&p->addressU))||FAILED(d->GetSamplerState(0,D3DSAMP_ADDRESSV,&p->addressV)))return;
            if(p->addressU<D3DTADDRESS_WRAP||p->addressU>D3DTADDRESS_CLAMP||p->addressV<D3DTADDRESS_WRAP||p->addressV>D3DTADDRESS_CLAMP)return;
        }else p->addressU=p->addressV=D3DTADDRESS_WRAP;
        phase.next(CaptureFinalize);
        p->cutoff=alpha?(float(ref)+(alphaFunc==D3DCMP_GREATER?.5f:0.f))/255.f:shaderCutoff;p->alphaMode=alphaMode;
        if(metadata.detailDoodad){
            const D3DVERTEXELEMENT9* elements=nullptr;UINT n=0;
            NorthlightReplayBounds::Bounds bounds;
            if(!declarationCache.get(p->decl,elements,n)||
               !NorthlightDetailShadow::bounds(*metadata.program,p->mesh(),elements,n,p->constants,context.inverseView,bounds)||
               NorthlightReplayBounds::outsideSphere(bounds,context.camera,64)){++detailRangeRejected;return;}
            if(!detailBudget.take(p->mesh().vertexCount,p->mesh().byteSize())){++detailBudgetRejected;return;}
        }
        p->type=type;p->base=0;p->min=0;p->vertices=p->mesh().vertexCount;p->start=0;p->count=count;p->indexed=indexed;
        if(sample){if(priority){++skinnedAccepted;acceptedSkinnedBytes+=p->mesh().byteSize();}else acceptedOtherBytes+=p->mesh().byteSize();}
        p->constantGroup=replays.empty()?0:replays.back()->constantGroup+
            (NorthlightReplayCaptureConstants::samePose(*replays.back(),*p,sample?&constantEpochStats:nullptr)?0:1);
        phase.next(CaptureActor);
        if(captureActor)appendActor(current,*p,sample); // Grass shadows do not spend the actor GI/texture readback budget.
        phase.next(CaptureFinalize);
        if(sample){++acceptedTriangleBins[triangleBin];if(priority)++acceptedSkinnedTriangleBins[triangleBin];smallShadowGI+=smallShadow;}
        p->fateSlot=fate.slot;fate.slot=-1; /* the selection records this draw's outcome */
        // 0.3.179 (M1): the handoff (fill + publish) as one span on sampled records of RenderProfile sample frames.
        const bool handoff=p->shadowSelected&&p->shadowSkinned,timedHandoff=handoff&&prepareHandoffSample();
        const auto handoffStart=timedHandoff?prepareMeter.start():std::chrono::steady_clock::time_point{};
        if(handoff)prepareFill(*p,metadata); /* 0.3.177: the stable selection's inputs */
        replays.emplace_back(p.release());preparePublish();if(timedHandoff)prepareMeter.stop(handoffStart);if(detailed)++capturePhaseAccepted;
    }

    // One directional replay draw in the 0.3.142 order: geometry, pose constants,
    // material, draw. The cascade loop and DiagReplayProbe (world_replay_probe.inl)
    // share it. Split marks the state/own/draw spans on RenderProfile sample frames;
    // Off compiles them away. StateOnly/LogicOnly (probe modes) drop the draw / every
    // D3D9 call at compile time. ok(hr,stage) false stops the pass (the cascade: check()).
    // 0.3.200 (jobs): ReplayJobs=1 (core/job_system.h). The pool starts lazily on the renderer thread inside render(); the jobs are pure
    // CPU work on inputs that are final when they are kicked and untouched until their join (no D3D, no logs, no game memory).
    NorthlightJobs::System replayJobs_;bool replayJobsTried_=false;
    struct JobJoin {NorthlightJobs::System& s;NorthlightJobs::Counter& c;~JobJoin(){s.wait(c);c.clearFailure();}}; /* every kicked job is joined before render() returns */
    bool jobsOn()const{return quality.replayJobs!=0&&replayJobs_.started();}
    bool jobsStart(){
        if(quality.replayJobs&&!replayJobsTried_){replayJobsTried_=true;
            const unsigned cores=NorthlightStream::cores(); /* the replay thread excluded while the stream runs */
            if(replayJobs_.start(cores))logf("JOBS workers=%u cores=%u",replayJobs_.workers(),cores);
            else logf("JOBS unavailable: no worker thread; the per-frame work stays on the renderer thread");}
        return jobsOn();
    }
    void takeCullEarly(unsigned& kicked,unsigned& skipped){kicked=replayCullEarly;skipped=replayCullEarlySkipped;replayCullEarly=replayCullEarlySkipped=0;} /* 0.3.217: renderer.cpp's JOBS line */
    NorthlightJobs::Stats takeJobStats(){return replayJobs_.take();} /* renderer.cpp's JOBS line, RenderProfile sample frames */
#include "world_atmosphere_frame.inl"
    // The later cascades' replay culling (per packet: the static proof's containment in the slot's cached matrix, bit 0; the pose
    // bounds' clip test against the slot's matrix, bit 1). Kicked after the frame's first bounds join for every later active slot and
    // read at that slot's replay loop when both matrices still match (else the loop tests inline, as before). Reads the packets only.
    // 0.3.217 (jobs): the first rendering slot's own job may be kicked earlier, before its static and terrain draws, but only when the
    // bounds worker has already finished (replayCullKickOwn): the join then does not wait, and the later slots still go at the old join.
    struct ReplayCull {WorldRenderer* r=nullptr;int slot=0;bool kicked=false;float matrix[16]={},cached[16]={};std::vector<uint8_t> flags;NorthlightJobs::Counter done;
        void operator()(){r->replayCullWork(*this);}};
    ReplayCull replayCulls[4];
    void replayCullWork(ReplayCull& job){
        const size_t n=replays.size();job.flags.resize(n);const bool affine=NorthlightShadowBounds::affineLightMatrix(job.matrix);
        for(size_t index=0;index<n;++index){const auto& p=*replays[index];uint8_t f=0;
            if(((p.staticProofMask|p.staticM2Mask)&(1u<<job.slot))&&StaticShadow::containsBounds(job.cached,p.staticProofLow,p.staticProofHigh))f|=1;
            if(affine&&p.pointBounds.valid&&NorthlightShadowBounds::clipRejectAffine(V(p.pointBounds.low[0],p.pointBounds.low[1],p.pointBounds.low[2]),V(p.pointBounds.high[0],p.pointBounds.high[1],p.pointBounds.high[2]),job.matrix))f|=2;
            job.flags[index]=f;}
    }
    void replayCullKickSlot(int slot){
        auto& job=replayCulls[slot];
        job.r=this;job.slot=slot;std::memcpy(job.matrix,sourceMatrices[slot/2][slot%2],sizeof job.matrix);std::memcpy(job.cached,staticSlotMatrix[slot],sizeof job.cached);
        job.kicked=true;replayJobs_.kick(job.done,job);
    }
    void replayCullKick(int fromSlot,const bool* sourceActive){
        for(int slot=fromSlot+1;slot<4;++slot)if(sourceActive[slot/2])replayCullKickSlot(slot);
    }
    unsigned replayCullEarly=0,replayCullEarlySkipped=0; /* 0.3.217 (jobs): first-slot kicks before / without the static draws, per JOBS window */
    // 0.3.217 (jobs): the current slot's own job, only when the bounds job already finished: the join (apply) then never waits. Else nothing.
    void replayCullKickOwn(int slot){
        if(!replayBoundsWorker.finished()){++replayCullEarlySkipped;return;}
        replayBoundsJoin();++replayCullEarly;replayCullKickSlot(slot);
    }
    // Null: test inline (no job for the slot, or its inputs differ). The job is joined either way.
    const uint8_t* replayCullFor(int slot,const float* cached,const float* matrix){
        auto& job=replayCulls[slot];if(!job.kicked)return nullptr;
        job.kicked=false;const bool ok=replayJobs_.wait(job.done);job.done.clearFailure();
        return ok&&job.flags.size()==replays.size()&&!std::memcmp(job.matrix,matrix,sizeof job.matrix)&&!std::memcmp(job.cached,cached,sizeof job.cached)?job.flags.data():nullptr;
    }
    void replayCullSettle(){for(auto& job:replayCulls)if(job.kicked){job.kicked=false;replayJobs_.wait(job.done);job.done.clearFailure();}}
    template<class Draw> HRESULT shadowReplayDraw(const Replay& p,Draw submit){
        return shadowReplayAlpha.draw(p.alphaMode,[&](size_t bytes){
            return admitsGrowth("shadow alpha target",bytes);
        },logf,submit);
    }
    template<class Split,class Check>
    bool submitReplay(const Replay* p,NorthlightShadowReplay::Pass& pass,const float* rows,const float* worldMatrix,
                      Split& split,size_t& constantBytes,size_t& constantCalls,const Check& check,
                      const NorthlightReplayDrawState::Range* grouped=nullptr,ShadowIndexPair* pair=nullptr,
                      NorthlightShaderConstants::Range uploadFloats={}){
        return pass.submit(*p,rows,worldMatrix,split,constantBytes,constantCalls,check,
                           [&](auto draw){return shadowReplayDraw(*p,draw);},grouped,pair,uploadFloats);
    }
#include "world_directional_replays.inl"
#include "world_directional_pass.inl"
#include "world_screen_lighting.inl"
#include "world_local_lighting.inl"
#include "world_atmospheric_fog.inl"
#include "world_frame_constants.inl"
    // 0.3.174: true once this frame's WorldComposite quad has drawn (the proxy's FOLD fallback reads it).
    bool composited=false;
    // foldScene/foldAO (0.3.174 FOLD): the proxy's pre-AO scene copy replaces the world colour
    // copy, and its AOContactBloom target (bloom rgb, AO alpha) is applied by TemporalLight's
    // removal smoothing and WorldComposite. Null: the proxy composited already (neutral s10).
    // enclosingState transfers restoration to the caller until its enclosing
    // effects scope ends. Later passes must bind their own inputs/state.
    bool render(IDirect3DSurface9* targetSurface,IDirect3DTexture9* depth,UINT w,UINT h,D3DFORMAT fmt,float nearZ,float farZ,float minZ,float maxZ,int debug,NorthlightGpuProfile* profile,IDirect3DTexture9* waterMask,IDirect3DTexture9* foldScene=nullptr,IDirect3DTexture9* foldAO=nullptr,unsigned rendererFrame=0,IDirect3DTexture9* hdrBase=nullptr,bool hdrBaseEncoded=false,bool hdrCombineAllowed=true,bool hdrSkyBase=false,bool foldAOAlphaNeutral=false,const SavedState* enclosingState=nullptr,const std::string* lightHistoryAudit=nullptr){
        shadowReplayAlpha.beginFrame(GetTickCount());
        const bool wantsDirectionalShadows=directionalShadowWork();
        bool directionalDeferred=wantsDirectionalShadows&&shadowReplayAlpha.retryAt();
        const bool directionalShadows=wantsDirectionalShadows&&!directionalDeferred;
        bool applyDirectionalShadows=directionalShadows&&(menuDomain||nativeShadowOwned);
        replayBoundsBegin();
        if(!pipelineActive)return debug?emptyDiagnosticView(targetSurface,w,h,debug):false;
        const bool hdrMode=hdrBase&&debug==0;
        shadowsComposited=false;composited=false;lastRenderDebug=debug;gpuDiagnosticFrame=false;fogGeometryReady=false;
        if((debug==1&&!directionalShadows)||(debug==2&&!effects.gi)||(debug==3&&!effects.fog))return emptyDiagnosticView(targetSurface,w,h,debug);
        if(debug!=1)gpuDiagnosticArmed=true;
        const bool materialWanted=materialDetail&&(materialBumpGain>0||materialOcclusion)&&foldScene&&debug==0;
        const auto requirements=frameRequirements(materialWanted,debug!=0||gpuNormalDiagnosticRequested);
        // 0.3.169 lastSkipReason(): why this call returned false. Untagged returns below the
        // upload gate are failed device calls in the draw stages ("draw").
        skipReason="fault";if(workerFault())return false;
        DWORD submissionStart=GetTickCount();
        // false only when this frame's model capture was skipped: no replays, and every
        // per-frame replay consumer (selection history and fate frame, GPU cache, bounds,
        // proofs, snapshot frame) is left untouched. Moving shadow casters are captured every frame.
        const bool freshReplays=captureMode!=CaptureSkipped;
        // 0.3.158 ActorShadows=0: replays exist only on GI capture frames and no shadow consumes
        // them (replayShadows), while every map and cube is complete without them (replaysComplete),
        // so nothing defers or demands a capture. Without drawable geometry, only
        // the GI cadence and diagnostic/demand exceptions consume model captures.
        const bool actorShadows=quality.actorShadows!=0;
        bool admitGeometry=true;
#ifdef NORTHLIGHT_REPLAY_STAND
        const unsigned geometryStart=NorthlightFrameParityClock::replayWorldCheckpoint(rendererFrame).geometry;
        admitGeometry=!geometryStart||menuDomain||rendererFrame>=geometryStart;
#endif
        // Prepare CPU caster bounds while the world mesh is still staging, as
        // the live client does. geometryReady gates the upload and shadow draws.
        const bool replayShadows=effects.shadows&&freshReplays&&actorShadows&&ready();
        const bool replaysComplete=freshReplays||!actorShadows;
        replayProbeFrame(); /* DiagReplayProbe window of this frame (nothing unless RenderProfile) */
        finishActorScene();
        if(replayShadows)selectShadowReplays();
        bucket(NorthlightEffectsBuckets::Selection);
        bool sourceActive[2]={NorthlightGI::dot(sourceColors[0],sourceColors[0])>1e-10f,NorthlightGI::dot(sourceColors[1],sourceColors[1])>1e-10f};
        const bool directionalReplayBounds=directionalShadows&&replayShadows&&(sourceActive[0]||sourceActive[1]);
        if(directionalReplayBounds)replayBoundsKick(); /* joined by the first pointBounds reader */
        struct BoundsJoin {WorldRenderer& r;~BoundsJoin(){r.replayBoundsJoin();}} boundsJoin{*this};
        // 0.3.152 RenderProfile: the upload window after selection and the bounds kick (locks,
        // ready/resources/upload, live terrain) up to the replay upload; logged with WORLD profile frame.
        uploadWindowTicks=-1;const int64_t uploadWindowStart=profileSampled()?QpcClock::now():0;
        const bool fixedGiActorClock=NorthlightFrameParityClock::enabled();
        const DWORD actorRequestNow=fixedGiActorClock?visualTime():submissionStart;
        {std::lock_guard<std::mutex> lock(mutex);
         if(valid&&(menuDomain||giActorWorkEnabled())&&completedActorSceneMap()==lastRequest.map&&!pending&&!workerBusy&&actorJobSerial()!=lastRequest.actorSerial&&actorRequestNow-actorRequestTick>=250){
             Request r=lastRequest;r.actorJob=completedActorJob();r.actorSerial=actorJobSerial();r.reason=64;
             if(menuDomain)r.menuLights=menuLights;
             queueRequestLocked(r,submissionStart,false);actorRequestTick=actorRequestNow;wake.notify_one();
             if(fixedGiActorClock&&NorthlightDiagnostics::enabled())
                 logf("GI_ACTOR_REQUEST present=%u actor=%llu request=%llu base=%llu menu=%u",parityFrame,
                    (unsigned long long)r.actorSerial,(unsigned long long)r.id,(unsigned long long)r.baseId,unsigned(menuDomain));
         }}
        const bool coldMesh=meshStore.table().empty();
        NorthlightStreaming::Budget streamBudget(coldMesh?4u<<20:2u<<20,coldMesh?2.0:1.0);
        releaseOrphanedPending("render"); /* 0.3.156: also while !ready(), when upload() never runs */
        // Screen lighting/weather use scene/depth. Only these consumers need
        // the GPU world mesh; keep the CPU scene available for weather shelter.
        const bool geometryWanted=requirements.geometry||debug==1||debug==2;
        const bool screenResourcesReady=screenReady()&&resources(w,h,fmt,requirements);
        // Start idle workers during mesh staging, not alongside the first full
        // set of shadow draws. Jobs still consume only this render's final inputs.
        if(geometryWanted&&screenResourcesReady&&pendingMesh&&!replayJobsTried_&&streamBudget.available())jobsStart();
        if(geometryWanted&&screenResourcesReady&&pendingMesh&&!geometryTargetsReady)geometryResources(w,h,&streamBudget);
        if(requirements.local&&screenResourcesReady&&pendingMesh&&geometryTargetsReady)prewarmPointResources(atmosphere.lights,localLightMap,streamBudget);
        const bool hadTargets=geometryTargetsReady&&(geometryGroups&requirements.geometryGroups())==requirements.geometryGroups();
        bool meshReady=geometryWanted&&ready()&&screenResourcesReady&&upload(streamBudget);
#ifdef NORTHLIGHT_REPLAY_STAND
        if(!meshReady&&geometryWanted&&ready()&&screenResourcesReady&&pendingMesh&&!failed&&
           geometryStart&&rendererFrame==geometryStart&&!menuDomain){
            // Finish staged work at the recorded Present. Keep the real
            // allocation guards and refuse the checkpoint if work remains.
            constexpr size_t bytes=32u<<20;
            const double timeLimit=NorthlightFrameParityClock::fixedGeometryCheckpoint()?
                std::numeric_limits<double>::infinity():30.0;
            NorthlightStreaming::Budget checkpointBudget(bytes,timeLimit);
            const unsigned texturesBefore=pendingMesh->textureUploads;
            meshReady=upload(checkpointBudget,true);
            const size_t materialsRemaining=pendingMesh?pendingMesh->payload->materials.size()-pendingMesh->material:0;
            const size_t pagesRemaining=pendingMesh?meshStore.staging().upload.size()-pendingMesh->upload.pageIndex():0;
            logf("REPLAY_GEOMETRY_STAGING present=%u submittedBytes=%zu ready=%u texturesBefore=%u pagesRemaining=%zu materialsRemaining=%zu",
                 rendererFrame,bytes-checkpointBudget.remainingBytes(),unsigned(meshReady),texturesBefore,pagesRemaining,materialsRemaining);
        }
#endif
        const bool sceneTargetsReady=meshReady&&geometryResources(w,h);
        const bool geometryPrepared=sceneTargetsReady&&(hadTargets||upload(streamBudget));
        const bool geometryReady=geometryPrepared&&admitGeometry;
        if(!geometryReady)lightHistoryAudit=nullptr;
#ifdef NORTHLIGHT_REPLAY_STAND
        if(geometryStart&&!menuDomain&&rendererFrame==geometryStart)
            logf("REPLAY_GEOMETRY_CHECKPOINT present=%u ready=%u",rendererFrame,unsigned(geometryReady));
#endif
        // GI owns its textures and solved snapshot. Static shadow pages can still be uploading.
        if(!geometryReady&&screenResourcesReady&&requirements.probes)stageScreenProbes(lastRequest.map);
        if(failed)return false;
        if(geometryReady!=temporalGeometryReady){
            const DWORD now=GetTickCount();
            if(DWORD(now-traceGeometryTick)>=1000){traceGeometryTick=now;traceGeometryCount=0;}
            if(NorthlightDiagnostics::enabled()&&traceGeometryCount++<16)deferLogf("WORLD geometry transition frame=%u rendererFrame=%u tick=%lu ready=%u screen=%u mesh=%u targets=%u",unsigned(frames),rendererFrame,(unsigned long)now,unsigned(geometryReady),unsigned(screenResourcesReady),unsigned(meshReady),unsigned(sceneTargetsReady));
            fogScreen.invalidateHistory();history.invalidate();temporalGeometryReady=geometryReady;
        }
        fogGeometryReady=geometryReady;
        if(!geometryReady){
            skipReason=!valid?"context":failed?"failed":workerFault()?"fault":!active?"inactive":!active->bvh?"nobvh":
                !screenResourcesReady?"scene-targets":!meshReady?"mesh":!sceneTargetsReady?"scene-targets":
                geometryPrepared?"capture-checkpoint":"upload";
            // Screen-space effects consume current scene/depth, not the BVH.
            // Keep them alive while the geometry builder is deferred by memory.
            updateStaticCasters(false,&streamBudget);
            if(debug==1||debug==2)return emptyDiagnosticView(targetSurface,w,h,debug);
            if(!screenResourcesReady)return false;
        }
        const char* geometrySkip=geometryReady?"ready":geometryWanted?skipReason:"not-required";
        if(!composite.prepareShaders(d,hdrMode,requirements.lighting,hdrCombineAllowed,materialDetail!=nullptr,
             [](unsigned available,unsigned long slots,unsigned required){
                 logf("WORLD HDR material combinedAvailable=%u deviceSlots=%lu requiredSlots=%u",available,slots,required);
             },
             [&](const char* message){deferLogf("%s",message);})){skipReason="hdr-shader";return false;}
        const auto layers=composite.selectLayers(hdrMode,debug==0);
        const bool normalDiagnosticCapture=debug==0&&gpuNormalDiagnosticRequested;
        if(nativeDiagnosticCapture){nativeDiagnosticCapture->finish(normalDiagnosticCapture&&active&&nativeDiagnosticMap==active->map);nativeDiagnosticCapture.reset();}
        const unsigned shadowDiagnosticCapture=debug==1&&gpuDiagnosticArmed&&gpuDiagnosticCaptures<4?gpuDiagnosticCaptures+1:0;
        const unsigned diagnosticCapture=normalDiagnosticCapture?gpuDiagnosticCaptures+1:shadowDiagnosticCapture;
        const float moonTerrainSpecular=materialWanted&&materialBumpGain>0?moonTerrainSpecularGain():0.f;
        float sunSurfaceSpecular=materialWanted&&materialBumpGain>0?sunSurfaceSpecularGain():0.f;
        if(sunSurfaceSpecular>0&&!composite.prepareSolar(d,hdrMode,
            [&](const char* message){deferLogf("%s",message);}))sunSurfaceSpecular=0;
        const auto material=composite.selectMaterial(materialWanted,hdrMode,hdrCombineAllowed&&moonTerrainSpecular==0,
            diagnosticCapture!=0,sunSurfaceSpecular>0);
        const bool fullMaterial=material.full,combinedMaterial=material.combined;
        const auto wantedFormat=hdrMode?D3DFMT_A16B16G16R16F:fmt;
        if(!composite.prepareScratch(d,w,h,wantedFormat,!foldScene||(fullMaterial&&!combinedMaterial),materialCaptureResident,
             [&](HRESULT hr,const char* step){return check(hr,step);},[&]{stateBlocks.clear();}))return false;
        skipReason="state";
        const bool callerRestores=enclosingState&&enclosingState->coversDevice(d);
        std::optional<SavedState> save;
        if(!callerRestores){save.emplace(d,&stateBlocks);if(!save->ok)return false;}
        const std::string& frameMap=lastRequest.map;
        std::shared_ptr<const FogRegion> fogRegion;
        {std::lock_guard<std::mutex> lock(mutex);fogRegion=publishedFogRegion;}
        auto field=fogRegion&&fogRegion->map==frameMap?fogRegion->field:
            active&&active->map==frameMap?active->fogField:std::shared_ptr<const NorthlightRegionalFog::Field>{};
        if(!field&&regionalFog.map()==frameMap)field=regionalFog.field();
        if(!uploadRegionalFog(field,frameMap))return false;
        int firstSource=sourceActive[0]||!sourceActive[1]?0:1;
        DirectionalPassResult directionalResult;
        shadowReplayIndices.begin(frames);
        nativeSceneFrame.clear();
        if(menuDomain)nativeSceneFrame=menuLights;else if(equipmentLightsOn()&&active)NorthlightNativeLights::frame(active->map.c_str(),context.camera,nativeSceneFrame);
        bool jobs=false;
        auto atmosphereJob=[&]{atmosphereWork(atmosphere,nearZ,w,h,debug);};
        JobJoin atmosphereJoin{replayJobs_,atmosphereDone};
        if(diagnosticCapture){gpuDiagnosticFrame=true;
            ++gpuDiagnosticCaptures;if(normalDiagnosticCapture)gpuNormalDiagnosticRequested=false;else gpuDiagnosticArmed=false;
            logf("WORLD GPU_CAPTURE_ATTEMPT capture=%u worldFrame=%u rendererFrame=%u rendererFrameKnown=%u normalView=%u diagnostic=1 fpsEligible=0",diagnosticCapture,frames,rendererFrame,unsigned(rendererFrame!=0),unsigned(normalDiagnosticCapture));}
        if(diagnosticCapture&&gpuDiagnosticDirectory.empty()){
            try{
                gpuDiagnosticDirectory=NorthlightWorldDiagnostics::directory(root);
            }catch(...){/* An explicit diagnostic allocation failure cannot disable rendering. */}
            logf("WORLD GPU diagnostic directory=%s exclusive=%u",gpuDiagnosticDirectory.empty()?"unavailable":gpuDiagnosticDirectory.c_str(),unsigned(!gpuDiagnosticDirectory.empty()));
        }
        const unsigned diagnosticFiles=gpuDiagnosticDirectory.empty()?0:diagnosticCapture;
        std::optional<NorthlightLightingCapture::Capture> lightingCapture;
        if(normalDiagnosticCapture){lightingCapture.emplace(d,gpuDiagnosticDirectory,diagnosticCapture,frames,rendererFrame,w,h);
            lightingCapture->pointSources(pointTargets.slots(),GetTickCount(),effects.shadows&&quality.pointShadows&&quality.localLights&&quality.localLightLimit,true);}
        prepareWorldShadowState();
        if(geometryReady){
        streamBudget.pause();
        auto timedTerrain=[&]{auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Terrain);return uploadLiveTerrain();};
        auto timedReplay=[&]{if(uploadWindowStart)uploadWindowTicks=QpcClock::now()-uploadWindowStart;auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Replay);return uploadReplay();};
        skipReason="terrain";bool terrainUploaded=false;
        if((directionalShadows&&!(terrainUploaded=timedTerrain()))||(replayShadows&&!timedReplay())){if(terrainUploaded)skipReason="replay";streamBudget.resume();updateStaticCasters(false,&streamBudget);return false;}
        streamBudget.resume();
        skipReason="draw";
        if(profile)profile->mark("WorldUpload");
        bucket(NorthlightEffectsBuckets::Upload);
        // 0.3.200 (jobs): ReplayJobs=1 kicks the frame's D3D-free CPU work here, as soon as its inputs are final (the snapshot, the fog field, the
        // batches and terrain chunks committed by upload()); each result is joined right before the first D3D call that needs it, and the guards
        // join on every return. ReplayJobs=0 (or no worker): nothing is kicked and each body runs inline at its old place. Same code, same results.
        jobs=jobsStart();replayJobs_.take();replayJobs_.timing(jobs&&profileSampled()); /* this frame's JOBS accounting (sample frames: timed) */
        // The fog clouds' noise (and its quantile table) is requested from the settings alone: derive() cannot be active before the table exists, so a
        // request gated on cf.active would never start it. One background generation per process; FogClouds=0 or density 0 still does nothing.
        if(quality.fogClouds&&quality.fogCloudDensity&&effects.fog&&debug==0&&!fogCloudNoise().ready.load(std::memory_order_acquire))fogCloudNoise().request();
        if(jobs)replayJobs_.kick(atmosphereDone,atmosphereJob);
        std::optional<NorthlightLightingCapture::Capture> projectionAudit;
        if(lightHistoryAudit)projectionAudit.emplace(d,*lightHistoryAudit,rendererFrame,frames,rendererFrame,w,h,"frame-parity-directional");
        if(!renderDirectional(sourceActive,directionalShadows,directionalReplayBounds,replayShadows,actorShadows,
                              replaysComplete,jobs,streamBudget,profile,rendererFrame,diagnosticFiles,
                              shadowDiagnosticCapture,directionalResult,projectionAudit?&*projectionAudit:nullptr)){
            if(!shadowReplayAlpha.deferIfUnavailable())return false;
            directionalDeferred=true;applyDirectionalShadows=false;shadowFrameReady=false;
            for(unsigned source=0;source<2;++source){
                nearShadow[source].invalidate();farShadow[source].invalidate();
                actorValid[source]=nearActorSkip[source]=false;
            }
            logf("WORLD directional shadows deferred rendererFrame=%u cause=alpha-target sceneEffectsRetained=1",rendererFrame);
            if(debug==1)return emptyDiagnosticView(targetSurface,w,h,debug);
            prepareWorldShadowState();
        }else if(projectionAudit)projectionAudit->finish(true);
        }else{
            shadowFrameReady=pointReady=false;
            actorValid[0]=actorValid[1]=false;
            if(quality.fogClouds&&quality.fogCloudDensity&&effects.fog&&debug==0&&!fogCloudNoise().ready.load(std::memory_order_acquire))fogCloudNoise().request();
        }
        if(shadowReplayAlpha.setFallback(directionalDeferred)){
            fogScreen.invalidateHistory();history.invalidate();
        }
        const bool fogShadowMapsReady=geometryReady&&applyDirectionalShadows;
        if(lightingCapture){const unsigned productSource=NorthlightGI::dot(sourceColors[0],sourceColors[0])>1e-10f?0:1;
            for(unsigned slot=0;slot<4;++slot){const auto& reuse=slot%2?farShadow[slot/2]:nearShadow[slot/2];const auto& key=shadowCacheKey[slot];
                lightingCapture->directional(slot,geometryReady&&shadowFrameReady&&sourceActive[slot/2],cascadeActions[slot],sourceMatrices[slot/2][slot%2],directionalMaps.current(slot).texture,productSource,reuse.valid,reuse.pass,key.serial,key.staticSignature);}}
        skipReason="draw";
        // Point shadows use this frame's selected/faded direct-light sources, in daylight too.
        if(jobs)replayJobs_.wait(atmosphereDone);else atmosphereWork(atmosphere,nearZ,w,h,debug);
        if(NorthlightLocalLightTrace::auditInputs())logLocalLightInputs(rendererFrame,geometryReady);
        bucket(NorthlightEffectsBuckets::Selection); // Atmosphere work/join precedes the point-shadow pass.
        // With no geometry, no upload/cascade mark closes the world setup.
        // Its elapsed interval can include CPU queue idle; do not bill it to empty point shadows.
        if(profile&&!geometryReady)profile->mark("WorldSetup");
        const unsigned pointUpdatesBefore=pointUpdates;
        if(geometryReady&&effects.shadows&&debug==0&&quality.pointShadows)renderPointShadows(atmosphere.lights,replaysComplete,actorShadows,rendererFrame,directionalResult.directionalShadowTime);else pointReady=false;
        if(lightingCapture)lightingCapture->pointSources(pointTargets.slots(),GetTickCount(),pointReady);
        if(pointUpdates!=pointUpdatesBefore&&profileSampled())profilePointUsed(); /* capture waste (RenderProfile) */
        if(geometryReady&&effects.shadows&&debug==0)renderFlashShadows(atmosphere.lights,replaysComplete,actorShadows);else flashShadows.invalidate();
        if(profile)profile->mark("PointShadow");
        bucket(NorthlightEffectsBuckets::Point);
        double lightingSetupMs[4]={},spellFitMs=0,spellJoinMs=0;bool spellWorker=false;
        int64_t lightingClock=profileSampled()&&captureFrequency.QuadPart>0?QpcClock::now():0;
        auto lightingMark=[&](unsigned part){if(!lightingClock)return;const int64_t t=QpcClock::now();lightingSetupMs[part]=1000.*double(t-lightingClock)/double(captureFrequency.QuadPart);lightingClock=t;};
        if(!composite.prepareScreenInput(d,targetSurface,foldScene!=nullptr,
                                        [&](HRESULT hr,const char* name){return check(hr,name);}))return false;
        ScreenFrameConstants frameConstants;auto& c=frameConstants.rows;
        c[0][0]=1.f/w;c[0][1]=1.f/h;c[0][2]=nearZ;c[0][3]=farZ;
        // 0.3.198 (rain): the frame's weather scalars (all 0 and every gain exactly 1 without weather: the bank below is then unchanged).
        // The new constants live in c59.yzw (LocalLightFog[0].yzw; only .x of c59..c66 is read, by LocalFog) and are read by WorldLighting (z)
        // and WorldFog (w); the lamp fog batch overwrites c59..c62 after all three have run, the bank is uploaded whole each frame.
        const auto wx=weatherEffects();
        c[67][0]=c[67][1]=0.f;
        // Temporal history is valid only for the same map, a continuous camera
        // and normal rendering; teleports, F10 and diagnostics restart it.
        const NorthlightWorldLightHistory::View historyView{frameMap,vec(context.camera),context.view};
        const bool useHistory=prepareFrameHistory(historyView,requirements.lighting,debug,geometryReady,rendererFrame,c);
        // Up to 16 authored lamps, sent through existing SM3-safe 8-light
        // direct / 4-light fog batches. Never expand the shader register bank.
        const float lampNight=NorthlightRegionalFog::nightFactor(celestialValid?celestial.dayFraction:-1.f);
        const float lampGain=.9f*NorthlightLocalLightSelection::nightGain(lampNight);
        // the first lighting pass is the sun's (with real shadows) only then: its visibility is in the
        // baseline alpha. The renderer's own orbit gives the sun weight; without it (game light only) no dimming.
        const float lampSunlitCut=NorthlightLocalLightSelection::sunlitCut(celestialValid?sourceWeights[0]:0.f,sourceActive[0]&&geometryReady&&applyDirectionalShadows);
        // 0.3.200 (jobs): the selection, the clouds' state and frame, the lamps' fog extinction, the veil and the scissor rects come from
        // atmosphereWork(): on a job kicked after upload() (ReplayJobs=1), joined here, or inline here (ReplayJobs=0, the old place).
        lightingMark(0);
        lightingMark(1);
        auto& localLights=atmosphere.lights;
        recordFrameLocalSources(localLights,diagnosticCapture);
        const auto& volumePalette=paletteFrameValid?framePalette:celestialProfiles.fallback;
        auto& cf=atmosphere.cf;
        prepareAtmosphereConstants(c,wx,cf,lampGain,debug);
        // The next frame's spell constants can fit while this frame's remaining
        // GPU passes are submitted. Publish before any reader or return.
        NorthlightSpellFog::Frame spellRaw;const bool spellWanted=quality.spellFog&&effects.fog&&regionalFog.field()&&debug==0&&atmosphere.spellValid;
        if(spellWanted){spellRaw.active=true;for(unsigned i=0;i<NorthlightSpellFog::kSamples;++i)spellRaw.tau[i]=atmosphere.spellTau[cf.active?1:0][i];for(unsigned k=0;k<3;++k)spellRaw.scatter[k]=std::max(context.ambient[k],0.f)*.35f;}
        const DWORD spellTick=GetTickCount();const float spellDt=spellFogTick?float(DWORD(spellTick-spellFogTick))*.001f:0.f;spellFogTick=spellTick;
        std::optional<NorthlightSpellFog::PendingUpdate> spellUpdate;
        if(jobs&&(spellWanted||spellFogHold.out.active)){
            spellUpdate.emplace(spellFogHold,spellRaw,spellWanted,spellDt);
            if(!spellUpdate->environmentKnown)spellUpdate.reset();
        }
        const bool spellTimed=lightingClock!=0;
        const auto spellCaller=std::this_thread::get_id();
        auto spellJob=[&]{
            const int64_t fitStart=spellTimed?QpcClock::now():0;
            if(spellTimed)spellWorker=std::this_thread::get_id()!=spellCaller;
            if(spellUpdate)(*spellUpdate)();else spellFogHold.update(spellRaw,spellWanted,spellDt);
            if(fitStart)spellFitMs=1000.*double(QpcClock::now()-fitStart)/double(captureFrequency.QuadPart);
        };
        NorthlightJobs::Counter spellDone;
        struct SpellJoin {
            NorthlightJobs::System& jobs;NorthlightJobs::Counter& counter;std::optional<NorthlightSpellFog::PendingUpdate>& update;NorthlightSpellFog::Hold& hold;bool published=false;
            void finish(){if(!published){if(update){jobs.wait(counter);update->publish(hold);}published=true;}}
            ~SpellJoin(){finish();}
        } spellJoin{replayJobs_,spellDone,spellUpdate,spellFogHold};
        const bool spellAsync=spellUpdate.has_value();
        if(spellAsync)replayJobs_.kick(spellDone,spellJob);else spellJob();
        const ScreenFrameInputs screenInputs{w,h,farZ,minZ,maxZ,debug,firstSource,sourceActive,requirements.probes,hdrMode,frameMap};
        prepareFrameShaderBank(screenInputs,wx,cf,volumePalette,frameConstants);
        const auto& haze=frameConstants.haze;
        const auto& uploadedFog=frameConstants.uploadedFog;
        lightingMark(2);
        IDirect3DTexture9* textures[]={foldScene?foldScene:composite.scratchTexture(),depth,directionalMaps.current(0).texture,directionalMaps.current(1).texture,probeAtlas.channel(0),probeAtlas.channel(1),probeAtlas.channel(2),probeAtlas.channel(3),nullptr,nullptr,probeAtlas.channel(4),waterMask,nullptr,regionalFog.texture()};
        publishFrameShaderBank(frameConstants,waterMask!=nullptr,textures);
        const auto& shadowTuning=frameConstants.shadowTuning;
        if(lightingCapture)lightingCapture->inputs(0,1u<<1,1u<<1); // Actual full-resolution sampled depth, before any post-process reuses its slot.
        DirectionalVisibility visibility;
        const auto& actorTilesWideCurrent=visibility.actorTilesWide;
        FogBoundSource fogSource;
        auto directionFor=[&](int source,bool volume){
            // Shadow-map history controls visibility, never the sky's light direction.
            if(volume)return rawSourceDirections[source];
            const auto& transition=shadowTransition[source*2+(volume?1:0)];const auto selected=transition.state.current();
            return geometryReady&&applyDirectionalShadows&&transition.workingValid?V(selected.x,selected.y,selected.z):sourceDirections[source];
        };
        const auto sourceRGB=[&](int source,bool volume){
            const V& shown=volume?sourceGlowColors[source]:sourceColors[source]; // the fog volume's glow has its own strength and hue; surface passes keep the light colour
            std::array<float,4> rgb={sourceActive[source]?shown.x:0.f,sourceActive[source]?shown.y:0.f,sourceActive[source]?shown.z:0.f,geometryReady&&applyDirectionalShadows?1.f:0.f};
            if(volume){const float keep=quality.godrays?underwaterFade.scale():0.f;rgb[0]*=keep;rgb[1]*=keep;rgb[2]*=keep;} /* underwater: the fog/volume pass (shafts) fades with the sun; the lighting and actor passes keep the real colour (their shadow test and alpha read it) */
            // fog passes only: the sun's direct scatter takes the glow hue; the forward soft cap
            // rises so the aureole brightens toward the sun. c17/c18 are restored after the fog loop.
            // Missing/disabled shadow maps are neutral visibility. They must
            // not turn off sunlight, moonlight or their atmospheric aureoles.
            if(volume&&source==0)NorthlightSunHue::fogDirect(rgb.data(),glowHueFrame,rgb.data());
            return rgb;
        };
        const auto fogActorFor=[&](int source){return quality.godrays&&regionalFogGpu.actorVariants()&&geometryReady&&applyDirectionalShadows&&quality.actorShadows&&quality.actorShadowDetail&&sourceActive[source]&&actorValid[source]&&actorMaps.depth(source).texture&&actorCasterCount[source]>0;};
        const auto fogGainFor=[&](int source){return quality.godrays?1.2f*volumePalette.fogGain[source]*wx.shaftGain():0.f;};
        auto setSource=[&](int source,bool first,bool volume=false){
            c[2][2]=farMapRadius(source);
            // Collapse only shadow queries to the centre at depth .5. With a
            // cleared depth of 1, even slope-corrected PCF is exactly unoccluded.
            const float neutralMatrix[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,.5f,1};
            const V committed=directionFor(source,volume);
            const float dir[4]={committed.x,committed.y,committed.z,source==1?1.f:0.f}; // w selects moon surface relighting
            // Match the shadow-render activity threshold exactly. The mandatory
            // ambient-only volume pass must never read an unrendered map for
            // a sub-threshold celestial source.
            const auto rgb=sourceRGB(source,volume);
            c[27][0]=first?(volume?1.f:1.f-authoredFill):0.f;c[27][1]=sourceWeights[source];
            const bool shadowMaps=geometryReady&&applyDirectionalShadows;
            const NorthlightWorldScreenLighting::Source bindings{
                shadowMaps?directionalMaps.current(source*2).texture:screenFallbacks.white(),
                shadowMaps?directionalMaps.current(source*2+1).texture:screenFallbacks.white(),
                c[2],shadowMaps?sourceMatrices[source][0]:neutralMatrix,
                shadowMaps?sourceMatrices[source][1]:neutralMatrix,dir,rgb.data(),c[27]
            };
            screenLighting.bindSource(d,bindings,[&]() -> const float* {
                if(!volume)return nullptr;
                bindFogActorSource(source,fogActorFor(source),geometryReady&&applyDirectionalShadows,actorTilesWideCurrent[source],
                                   neutralMatrix,c,textures[5],fogSource);
                c[21][1]=fogGainFor(source);c[21][2]=hdrMode?64.f:source==0?NorthlightSunHue::SunForwardCap:NorthlightSunHue::MoonForwardCap;
                fogSource.ambientOnly=NorthlightFogAmbient::eligible(rgb.data(),c[21][1]);
                return c[21];
            });
        };
        lightingMark(3);
        bucket(NorthlightEffectsBuckets::LightingSetup);
        // Smoothed normals for the lighting, GI and local light passes (s14).
        const bool lightingNeeded=requirements.lighting;
        const bool wantsPrelit=(layers.prelit||layers.authored)&&lightingNeeded&&
            ((requirements.probes&&quality.giPrelitMix)||(geometryReady&&applyDirectionalShadows&&(sourceActive[0]||sourceActive[1])));
        const NorthlightWorldScreenLighting::FrameInput lightingFrame{
            w,h,wantsPrelit,materialOcclusion!=nullptr,materialOcclusionDistance,hdrMode&&hdrSkyBase,atmosphere.imported.valid
        };
        const bool prelitDaylightCurrent=screenLighting.prepareFrame(d,lightingFrame,[&]{
            const auto budget=NorthlightGeometryMemory::defaultTargetBudget();
            return NorthlightGeometryMemory::admits(geometryAdmission(budget),budget);
        });
        // Without shadow visibility changes, removal smoothing is a copy.
        // TemporalLight can consume raw light and produce distance itself.
        const bool normalsProduceDistance=lightingNeeded&&applyDirectionalShadows&&c[30][1]>0;
        const auto checkTemporal=[&](HRESULT hr,const char* pass){return check(hr,pass);};
        const bool anyShadowTransition=geometryReady&&(shadowTransitionActive(0)||shadowTransitionActive(1));
        auto* const smoothSurface=fogScreen.scratch().surface;
        if(lightingNeeded){
        if(!worldNormals.draw(d,w/2,h/2,modelNormals,normalsProduceDistance?history.pending().distance.surface:nullptr,
                              [&](HRESULT hr,const char* name){return check(hr,name);}))return false;
        if(profile)profile->mark("WorldNormals");
        bucket(NorthlightEffectsBuckets::Lighting);
        if(!resolveDirectionalVisibility(w,h,firstSource,sourceActive,fogShadowMapsReady,anyShadowTransition,
                                         c[15],c[24][0],textures,rendererFrame,profile,
                                         [&](int source){setSource(source,false);},visibility))return false;
        const CelestialLightingInput celestialInputs{
            w,h,rendererFrame,firstSource,sourceActive,geometryReady,applyDirectionalShadows,requirements.probes,c[20][3],
            prelitDaylightCurrent,fullMaterial,moonTerrainSpecular,sunSurfaceSpecular,textures[8],textures[13],geometrySkip,frameMap
        };
        if(!renderCelestialLighting(celestialInputs,visibility,profile,lightingCapture?&*lightingCapture:nullptr,setSource,lightHistoryAudit))return false;
        const NorthlightWorldLightHistory::LightingInput temporalInputs{
            screenLighting.raw().texture,foldAO?foldAO:screenFallbacks.ao(),fogScreen.scratch().texture,textures[13],smoothSurface,
            normalsProduceDistance,diagnosticCapture!=0
        };
        if(!history.resolveLighting(d,historyView,temporalInputs,w/2,h/2,profile,checkTemporal,
             [&](bool merged,bool removal,bool zeroWeight){
                 if(profile&&profile->recording())deferLogf("WORLD temporal rendererFrame=%u mergedRemoval=%u removalRequired=%u diagnostic=%u zeroWeight=%u",rendererFrame,unsigned(merged),unsigned(removal),unsigned(diagnosticCapture!=0),unsigned(zeroWeight));
             }))return false;
        if(profile)profile->mark("TemporalLight");
        // Now pending holds the previous pair. Local lighting and fog will
        // overwrite the directional target and can borrow that light slot.
        if(lightHistoryAudit){
            gpuDiagnosticFrame=true;
            const auto captureInput=[&](const char* name,IDirect3DSurface9* surface,bool used,D3DFORMAT format){
                NorthlightWorldDiagnostics::product(d,surface,*lightHistoryAudit,name,rendererFrame,used,w/2,h/2,format,rendererFrame);
            };
            captureInput("history-raw-light",screenLighting.raw().surface,true,D3DFMT_A16B16G16R16F);
            captureInput("history-prior-light",history.pending().light.surface,useHistory,D3DFMT_A16B16G16R16F);
            captureInput("history-prior-distance",history.pending().distance.surface,useHistory,D3DFMT_R32F);
            captureInput("history-directional-visibility",directionalVisibility.surface(),visibility.softResolved,D3DFMT_A16B16G16R16F);
            if(NorthlightFrameParityClock::auditLightHistoryInterval())
                captureInput("history-resolved-light",history.produced().light.surface,true,D3DFMT_A16B16G16R16F);
        }
        }else{
            if(!history.resolveDistance(d,historyView,requirements.distance,w/2,h/2,checkTemporal))return false;
            screenLighting.bindDistanceOnly(d,screenFallbacks.zero(),foldAO?foldAO:screenFallbacks.ao(),c[19][3]);
        }
        // Preserve diagnostic inputs before recycling the raw directional target.
        if(diagnosticFiles){
            const unsigned capture=diagnosticCapture;const std::string& directory=gpuDiagnosticDirectory;
            using NorthlightWorldDiagnostics::dump;
            dump(d,screenLighting.raw().surface,directory,"light",capture);
            if(prelitDaylightCurrent)dump(d,screenLighting.prelit().surface,directory,"prelit-daylight",capture);
            dump(d,normalsProduceDistance?smoothSurface:screenLighting.raw().surface,directory,"smoothed-light",capture);
        }
        bucket(NorthlightEffectsBuckets::PointLighting);
        // The raw directional target is no longer read after temporal resolve.
        // Reuse it for current local light, independent of shadow-removal history.
        const bool currentLocalLighting=debug==0&&(localDirectCount||pointReady);
        const LocalLightingInput localInputs{
            w,h,nearZ,farZ,minZ,maxZ,debug,currentLocalLighting,fullMaterial,diagnosticCapture!=0,sourceActive[0],
            lampSunlitCut,depth,waterMask,c,shadowTuning,textures
        };
        auto* const localCapture=lightingCapture?&*lightingCapture:nullptr;
        std::optional<NorthlightLightingCapture::Capture> pointInputs;
        if(lightHistoryAudit){
            static const bool capturePointInputs=[] {
                const char* value=std::getenv("NORTHLIGHT_FRAME_PARITY_POINT_INPUTS");
                return value&&std::strcmp(value,"1")==0;
            }();
            if(capturePointInputs)pointInputs.emplace(d,*lightHistoryAudit,rendererFrame,frames,rendererFrame,w,h,"point-light-inputs");
        }
        if(!renderLocalLighting(localInputs,localLights,atmosphere.directRects,profile,localCapture,pointInputs?&*pointInputs:nullptr))return false;
        if(pointInputs)pointInputs->finish(true);
        const auto fogVolumeVariant=[&](int source,bool importedNoise){
            const bool fading=geometryReady&&shadowVolumeTransitionActive(source)&&!NorthlightFogAmbient::eligible(sourceRGB(source,true).data(),fogGainFor(source));
            const bool actor=fogActorFor(source);
            return (quality.godrays?(fading?4u:0u)+(actor?2u:0u):8u)+(importedNoise?0u:1u);
        };
        const AtmosphericFogInput fogInputs{
            w,h,farZ,debug,firstSource,sourceActive,geometryReady,applyDirectionalShadows,anyShadowTransition,useHistory,
            requirements.distance,cf.active,atmosphere.imported,frameMap,c,textures
        };
        AtmosphericFogResult fogResult;
        if(!renderAtmosphericFog(fogInputs,fogSource,localLights,atmosphere.fogRects,profile,localCapture,
                                 [&](int source,bool first){setSource(source,first,true);},fogVolumeVariant,
                                 [&](int source){return directionFor(source,true);},fogResult))return false;
        const NorthlightWorldComposite::Input compositeInputs{
            targetSurface,w,h,fmt,hdrMode,hdrBaseEncoded,lightingNeeded,fogResult.active,currentLocalLighting,
            effectEmission,prelitDaylightCurrent,layers,material,haze.haze,uploadedFog,
            foldScene,hdrBase,screenFallbacks.dither(),screenFallbacks.zero(),history.produced().light.texture,
            fogResult.image.texture?fogResult.image.texture:screenFallbacks.ao(),screenLighting.raw().texture,effectCoverage,materialOcclusion,
            materialDetail,worldNormals.texture(),screenLighting.prelit().texture,textures[3],textures[7],profile,materialProfile,
            localCapture,NorthlightWorldLocalLighting::LocalDirectInputs,regionalFog.texture()
        };
        const auto composition=composite.draw(d,compositeInputs,rainMask,
             [&](HRESULT hr,const char* name){return check(hr,name);},[&]{
            if(materialProfile&&materialProfile->recording())logf("MATERIAL_APPLY frame=%llu gain=%.3f normalRange=%.1f occlusion=%u occlusionRange=%.1f sunRGB=%.4f,%.4f,%.4f moonRGB=%.4f,%.4f,%.4f ambientRGB=%.4f,%.4f,%.4f",(unsigned long long)materialProfile->frameNumber(),materialBumpGain,materialBumpDistance,unsigned(materialOcclusion!=nullptr),materialOcclusionDistance,sourceColors[0].x,sourceColors[0].y,sourceColors[0].z,sourceColors[1].x,sourceColors[1].y,sourceColors[1].z,context.ambient[0],context.ambient[1],context.ambient[2]);
            materialPixelAudit.sample(materialProfile,&stateBlocks);
        });
        if(!composition.ok)return false;
        composited=true;diagnosticFogResolved=fogResult.resolved;diagnosticLocalLighting=currentLocalLighting;
        reportLocalSources(sourceTrace,diagnosticCapture,rendererFrame,c[52][1],lampSunlitCut,c[58][2],w,h,nearZ,farZ,minZ,maxZ,waterMask!=nullptr,debug);
        if(diagnosticFiles){
            const unsigned capture=diagnosticCapture;
            const std::string& directory=gpuDiagnosticDirectory;
            float actual[79][4]={};HRESULT read=d->GetPixelShaderConstantF(0,actual[0],79);
            if(SUCCEEDED(read)){
                char suffix[80];std::snprintf(suffix,sizeof suffix,"/capture-%u-constants.f32",capture);
                if(FILE* f=NorthlightFiles::open((directory+suffix).c_str(),"wb")){std::fwrite(actual,sizeof actual,1,f);std::fclose(f);}
            }
            {char suffix[80];std::snprintf(suffix,sizeof suffix,"/capture-%u-shadow-tuning.f32",capture);
             if(FILE* f=NorthlightFiles::open((directory+suffix).c_str(),"wb")){std::fwrite(shadowTuning,sizeof shadowTuning,1,f);std::fclose(f);}}
            logf("WORLD GPU diagnostic capture=%u map=\"%s\" eye=(%.3f %.3f %.3f) moon=(%.6f %.6f %.6f) constantsHRESULT=%08lx",capture,frameMap.c_str(),context.camera[0],context.camera[1],context.camera[2],sourceDirections[1].x,sourceDirections[1].y,sourceDirections[1].z,(unsigned long)read);
            using NorthlightWorldDiagnostics::dump;
            captureProducts(directory,capture,rendererFrame,w,h,true);
            if(hdrBase){IDirect3DSurface9* base=nullptr;
                if(SUCCEEDED(hdrBase->GetSurfaceLevel(0,&base))){dump(d,base,directory,"hdr-base",capture);base->Release();}
                logf("WORLD GPU diagnostic capture=%u hdrBaseEncoded=%u",capture,unsigned(hdrBaseEncoded));}
            char layout[96];std::snprintf(layout,sizeof layout,"/capture-%u-lighting-layout.txt",capture);
            if(FILE* f=NorthlightFiles::open((directory+layout).c_str(),"wb")){
                std::fprintf(f,"separate-current-local-v1\ncurrentLocalLighting=%u\n",unsigned(currentLocalLighting));std::fclose(f);
            }
            // 0.3.174 FOLD: the proxy's scene copy, before AO and bloom (the colour copy was skipped).
            if(foldScene){IDirect3DSurface9* folded=nullptr;if(SUCCEEDED(foldScene->GetSurfaceLevel(0,&folded))){dump(d,folded,directory,"scene",capture);folded->Release();}}
            else dump(d,composite.scratchSurface(),directory,"scene",capture);
            logf("WORLD GPU diagnostic capture=%u scene=%s",capture,foldScene?"pre-AO (folded AO/bloom)":"post-AO composite");
        }
        const int64_t spellJoinStart=spellTimed?QpcClock::now():0;
        spellJoin.finish();
        if(spellJoinStart)spellJoinMs=1000.*double(QpcClock::now()-spellJoinStart)/double(captureFrequency.QuadPart);
        if(spellFogHold.resets&&!spellFogResetLogged){spellFogResetLogged=true;logf("SPELLFOG estimate was not finite: the hold was reset and SpellFog goes on (resets=%u)",spellFogHold.resets);}
        if(lightingCapture){lightingCapture->image("lighting-final-scene",targetSurface);lightingCapture->finish(true);}
        if(++frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD frame=%u GI probes=%u valid=%u rays=%u bounces=%u cacheTriangles=%zu replayDraws=%zu liveTerrainChunks=%zu terrainAttempts=%u terrainSnapshots=%u cascadesPerSource=2x1024 volumeStepsMax=49 fogWorldSpacing=2.667 fogShadowFilter=1.5 fogLightHeight=12 fogAmbient=0.35",frames,NorthlightGI::probeLayout().count(),active?active->validProbes:0,quality.giRays,quality.giBounces,active&&active->bvh?active->bvh->triangleCount():0,replays.size(),liveTerrainChunks.size(),terrainAttempts,terrainSnapshots);
        if(frames%600==0&&NorthlightDiagnostics::enabled())logf("WORLD global light holds=%u predicted=%u mispredicted=%u",globalLightHold.held,globalLightHold.predictedAccepts,globalLightHold.predictedMisses); /* 0.3.207: sky block reads between the copy and the glare darkening; colours matched / not matched by the glow prediction */
        if(captureSampled)logf("VOLUME sources sunRGB=%.5f,%.5f,%.5f moonRGB=%.5f,%.5f,%.5f ambientRGB=%.5f,%.5f,%.5f sunGain=1.2 moonGain=1.2 directCaps=0.38,0.24 airCells=%u airDensity=%.5f,%.5f forestAir=%.4f night=%.3f",
            sourceColors[0].x,sourceColors[0].y,sourceColors[0].z,sourceColors[1].x,sourceColors[1].y,sourceColors[1].z,
            context.ambient[0],context.ambient[1],context.ambient[2],regionalFog.field()?regionalFog.field()->airCells:0,c[32][0],c[32][1],c[22][3],c[31][3]);
        if(profileSampled()){ /* hue candidates (RenderProfile): compare at Brill / Durotar coast / Orgrimmar with the Light DBC */
            const auto& g=glowHueFrame;float fogDirectRGB[3],fogAmbientRGB[3],liftRGB[3],slot[5][3];
            const float sunRGB[3]={sourceGlowColors[0].x,sourceGlowColors[0].y,sourceGlowColors[0].z};NorthlightSunHue::fogDirect(sunRGB,g,fogDirectRGB);NorthlightSunHue::coolAmbient(c[18],g,sourceWeights[0],fogAmbientRGB);NorthlightSunHue::horizonLift(g,liftRGB);
            const unsigned slots[5]={NorthlightSunHue::SlotDirect,NorthlightSunHue::SlotFog,NorthlightSunHue::SlotSun,NorthlightSunHue::SlotSunHalo,NorthlightSunHue::SlotSkyAboveHorizon};
            for(unsigned i=0;i<5;++i)NorthlightSunHue::unpack(lightSlots[slots[i]],slot[i]);
            logf("SUNHUE valid=%d slots=%d native=%.3f strength=%.3f direct=%.3f,%.3f,%.3f legacyFog=%.3f,%.3f,%.3f band0=%.3f,%.3f,%.3f band7fog=%.3f,%.3f,%.3f band9sunGlare=%.3f,%.3f,%.3f band10sunHalo=%.3f,%.3f,%.3f slot6band5sky=%.3f,%.3f,%.3f "
                "H=%.3f,%.3f,%.3f core=%.3f,%.3f,%.3f moon=%.3f,%.3f,%.3f lift=%.3f,%.3f,%.3f fogDirect=%.3f,%.3f,%.3f fogAmbient=%.3f,%.3f,%.3f caps=%.2f,%.2f",
                int(g.valid),int(lightSlotsValid),g.native,g.strength,context.direct[0],context.direct[1],context.direct[2],legacyFog.color[0],legacyFog.color[1],legacyFog.color[2],
                slot[0][0],slot[0][1],slot[0][2],slot[1][0],slot[1][1],slot[1][2],slot[2][0],slot[2][1],slot[2][2],slot[3][0],slot[3][1],slot[3][2],slot[4][0],slot[4][1],slot[4][2],
                g.sun[0],g.sun[1],g.sun[2],g.sunCore[0],g.sunCore[1],g.sunCore[2],g.moon[0],g.moon[1],g.moon[2],liftRGB[0],liftRGB[1],liftRGB[2],
                fogDirectRGB[0],fogDirectRGB[1],fogDirectRGB[2],fogAmbientRGB[0],fogAmbientRGB[1],fogAmbientRGB[2],NorthlightSunHue::SunForwardCap,NorthlightSunHue::MoonForwardCap);
        }
        if(frames==1||frames%600==0){
            const auto& scissor=localLighting.scissor().work();
            if(frames==1||NorthlightDiagnostics::enabled())logf("LOCAL direct lights=%u limit=%u nearestReach=%.1f visibilityEnd=%.1f fogDistance=%.1f strength=%.3f lampGain=%.3f sunlitCut=%.3f available=%zu scissor=%d batches=%u clipped=%u skipped=%u coverage=%.3f fading=%u selectUs=%.1f selectMaxUs=%.1f offscreen=%.1f arranged=%u widePasses=%u arrangedCost=%.3f",localDirectCount,quality.localLightLimit,localDirectNearest,NorthlightLocalLightSelection::VisibilityEnd,localLights.fogDistance,c[52][1],lampGain,lampSunlitCut,active?active->localLights.size():size_t(0),
            int(NorthlightLocalLightScissor::Enabled),scissor.batches,scissor.clipped,scissor.skipped,scissor.batches?scissor.coverage/scissor.batches:1.,
            localLightTracker.fading,localSelectFrames?localSelectUsSum/localSelectFrames:0.,localSelectUsMax,
            localSelectFrames?double(localOffscreenSum)/localSelectFrames:0.,localArranged,localLighting.widePasses(),localArrangeBefore>0?localArrangeAfter/localArrangeBefore:1.);
            localLighting.resetWork();localSelectUsSum=localSelectUsMax=0;localSelectFrames=0;
            localOffscreenSum=localArranged=0;localArrangeBefore=localArrangeAfter=0;
            if(frames>1&&NorthlightDiagnostics::enabled())logf("GI probe blend publishes=%u blendedSlots=%u texture=%d",probeBlendPublishes,probeBlendSlots,int(probeAtlas.previous()!=nullptr));
            if(active&&frames>1&&NorthlightDiagnostics::enabled()){
                const auto& r=active->relightStats;
                logf("GI relight hits=%llu tracedProbes=%llu shapeResets=%llu cacheKiB=%.1f invalidAcrossLight=%llu",(unsigned long long)r.hits,(unsigned long long)r.misses,(unsigned long long)r.resets,double(r.bytes)/1024,(unsigned long long)r.invalidHits);
                const auto& v=active->visibilityStats;
                const auto coarseSlots=active->coarseAtlas?std::count_if(active->coarseAtlas->begin(),active->coarseAtlas->end(),[](const auto& e){return e.occupied;}):0;
                logf("GI cascades layers=2 spacing=8,32 coarseResident=%zu coarseRays=%u coarseStaticOnly=1 priority=near-first",size_t(coarseSlots),std::max(8u,quality.giRays/4));
                logf("GI local visibility built=%llu reused=%llu failed=%llu bytesPerProbe=648",(unsigned long long)v.built,(unsigned long long)v.reused,(unsigned long long)v.failed);
            }
            probeBlendPublishes=probeBlendSlots=0;}
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("HORIZON haze strength=%u start=%u band=%u terrain=%u fog=%u colorKnown=%d fogEnd=%.1f far=%.1f startZ=%.1f tau=%.3f zone=%.3f rgb=%.3f,%.3f,%.3f sun=%.4f,%.4f",
            quality.horizonHaze,quality.horizonHazeStart,quality.horizonHazeBand,quality.horizonHazeTerrain,unsigned(effects.fog),int(horizonHazeState.colorKnown),horizonHazeState.end,farZ,haze.shape[0],
            haze.haze[3]/NorthlightHorizonHaze::Log2e,frameConstants.hazeZone,haze.haze[0],haze.haze[1],haze.haze[2],haze.sun[0],haze.sun[1]);
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled())){ /* every 600 frames with Diagnostics=1 (RenderProfile needs it too), so one test session answers the hue question on stock and HD */
            logf("CELESTIAL native appearance valid=%u coreRGB=%.4f,%.4f,%.4f discRGB=%.4f,%.4f,%.4f glareRGB=%.4f,%.4f,%.4f nativeRadius=%.7f renderedRadius=%.7f",
                unsigned(celestialValid&&celestial.sun.nativeAppearance),celestial.sun.nativeTint[0],celestial.sun.nativeTint[1],celestial.sun.nativeTint[2],
                celestial.sun.tint[0],celestial.sun.tint[1],celestial.sun.tint[2],celestialGlowHueFrame.sun[0],celestialGlowHueFrame.sun[1],celestialGlowHueFrame.sun[2],celestial.sun.nativeAngularRadius,celestial.sun.angularRadius);
            const auto& g=glowHueFrame;float band9[3],band10[3];NorthlightSunHue::unpack(lightSlots[NorthlightSunHue::SlotSun],band9);NorthlightSunHue::unpack(lightSlots[NorthlightSunHue::SlotSunHalo],band10);
            logf("CELESTIAL glowHue src=%s native=%.2f strength=%.2f moonStrength=%.0f rgb=%.3f,%.3f,%.3f core=%.3f,%.3f,%.3f moon=%.3f,%.3f,%.3f band9=%.3f,%.3f,%.3f band10=%.3f,%.3f,%.3f direct=%.3f,%.3f,%.3f fog=%.3f,%.3f,%.3f",
                !g.valid||g.strength<=0?"fallback":g.native>=.5f?"native":"sunHalo",g.native,g.strength,g.moonStrength,g.sun[0],g.sun[1],g.sun[2],g.sunCore[0],g.sunCore[1],g.sunCore[2],g.moon[0],g.moon[1],g.moon[2],
                band9[0],band9[1],band9[2],band10[0],band10[1],band10[2],context.direct[0],context.direct[1],context.direct[2],legacyFog.color[0],legacyFog.color[1],legacyFog.color[2]);
        }
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("CELESTIAL valid=%d gameDay=%.6f source0Weight=%.4f moonWeight=%.4f authoredFill=%.4f sunZ=%.4f moonZ=%.4f regionalFog=%.4f",celestialValid,celestialValid?celestial.dayFraction:0.f,sourceWeights[0],sourceWeights[1],authoredFill,sourceDirections[0].z,sourceDirections[1].z,c[27][3]);
        if(frames==1||frames%600==0){if(frames==1||NorthlightDiagnostics::enabled())logf("WORLD shadow batches near=%u far=%u culledNear=%u culledFar=%u liveUploadBytes=%zu cacheRenders=%u cacheReuses=%u culledReplays=%u staticFull=%u staticPartial=%u partialRects=%u partialBounding=%u costFull=%u partialTexelPct=%.1f partialSkippedDraws=%u partialCalls=%llu partialFullCalls=%llu dedupUncommitted=%u verified=%u verifyMismatches=%u scissorLeaks=%u slicedBands=%u sliceRestarts=%u",directionalResult.drawnBatches[0],directionalResult.drawnBatches[1],directionalResult.culledBatches[0],directionalResult.culledBatches[1],terrainUploadBytes,shadowCacheRenders,shadowCacheReuses,culledReplayDraws,
            shadowCacheStaticFullRenders,shadowCachePartialRenders,shadowCachePartialRects,shadowCachePartialBounding,shadowCacheCostFull,shadowCachePartialRenders?100.0*double(shadowCachePartialTexels)/(double(shadowCachePartialRenders)*ShadowCacheSize*ShadowCacheSize):0.0,shadowCachePartialSkippedDraws,shadowCachePartialCalls,shadowCachePartialFullCalls,staticDedupUncommitted,shadowCacheVerified,shadowCacheVerifyMismatches,shadowCacheScissorLeaks,staticSliceBands,staticSliceRestarts);
            shadowCacheRenders=shadowCacheReuses=culledReplayDraws=0;shadowCacheStaticFullRenders=shadowCachePartialRenders=shadowCachePartialSkippedDraws=shadowCachePartialRects=shadowCachePartialBounding=shadowCacheCostFull=0;shadowCachePartialTexels=shadowCachePartialCalls=shadowCachePartialFullCalls=0;staticDedupUncommitted=0;staticSliceBands=staticSliceRestarts=0;}
        if(frames==1||frames%600==0){if(frames==1||NorthlightDiagnostics::enabled())logf("SHADOW reuse nearInterval=%u farInterval=%u nearReuses=%u farReuses=%u captureSkippedFrames=%u captureDeferrals=%u captureSkip=%u",
            quality.nearShadowInterval,quality.farShadowInterval,nearReuses,farReuses,captureSkippedFrames,captureDeferrals,unsigned(NorthlightQuality::captureSkipPossible(quality,NorthlightQuality::actorShadowWork(quality,applyDirectionalShadows))));
            nearReuses=farReuses=captureSkippedFrames=captureDeferrals=0;}
        if(profile&&profile->recording())deferLogf("WORLD composite rendererFrame=%u variant=%s width=%u height=%u fog=%u local=%u aoAlphaNeutral=%u combined=%u rainAbsent=%u nativeSurfaceShared=%u distanceFromNormals=%u fogClearSkipped=%u fogHistoryClearSkipped=%u callerStateShared=%u",rendererFrame,composition.aoBase?"ao-hdr":composition.base?"base-hdr":hdrMode?"standard-hdr":"standard-sdr",w,h,unsigned(fogResult.active),unsigned(currentLocalLighting),unsigned(!foldAO||foldAOAlphaNeutral),unsigned(combinedMaterial),unsigned(composition.rainAbsent),unsigned(composition.nativeSurfaceShared),unsigned(normalsProduceDistance),unsigned(fogResult.clearSkipped),unsigned(fogResult.historyClearSkipped),unsigned(callerRestores));
        if(lightingClock){
            const bool imported=atmosphere.imported.valid;
            const unsigned rays=spellWanted?NorthlightSpellFog::kSamples:0;
            logf("WORLD lighting setup prefixMs=%.3f atmosphereMs=%.3f constantsMs=%.3f bindingsMs=%.3f spellFitMs=%.3f spellFitRevision=%u spellAsync=%u spellWorker=%u spellJoinMs=%.3f spellRegionalSamples=%u spellImportedSamples=%u",
                 lightingSetupMs[0],lightingSetupMs[1],lightingSetupMs[2],lightingSetupMs[3],spellFitMs,spellFogHold.out.revision,unsigned(spellAsync),unsigned(spellWorker),spellJoinMs,
                 imported?0:rays*NorthlightSpellFog::RegionalRaySteps,imported?rays*NorthlightSpellFog::ImportedRaySteps:0);
        }
        if(captureSampled)logf("WORLD terrain reuse=%u snapshots=%zu uploadBytes=%zu vertexUploadBytes=%zu reusedVertices=%zu arenaMiB=%.2f vertexStride=%zu vertexCapacity=%u rollovers=%u change=%s newOwners=%u directionalBuilds=%u directionalAliases=%u indexCacheMiB=%.2f directionalTriangles=%zu straddlingTriangleTests=%zu collectVisited=%zu terrainMs=%.3f,%.3f,%.3f",unsigned(terrainUploadReused),frameTerrain.size(),terrainUploadBytes,liveTerrainGPU.uploadedBytes,liveTerrainGPU.reusedVertices,double(liveTerrainGPU.bytes())/1048576,sizeof(StaticShadow::Vertex),liveTerrainGPU.vertexCapacity(),liveTerrainGPU.rollovers,
            terrainChange,liveTerrainGPU.newOwners,liveTerrainGPU.directionalBuilds,liveTerrainGPU.directionalAliases,double(liveTerrainGPU.indexBytes())/1048576,liveTerrainGPU.directionalTriangles,terrainTriangleTests,liveTerrainGPU.collectVisited,terrainPartMs[0],terrainPartMs[1],terrainPartMs[2]);
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD replay constants bytes=%zu previousBytes=%zu calls=%zu previousCalls=%zu",directionalResult.replayConstantBytes,replays.size()*2*(1024*4+16*4+64*4+16*4),directionalResult.replayConstantCalls,replays.size()*2*4);
        if(frames==1||frames%600==0){
            if(frames==1||NorthlightDiagnostics::enabled())logf("WORLD replay cull tested=%llu,%llu,%llu,%llu bounded=%llu,%llu,%llu,%llu culled=%llu,%llu,%llu,%llu staticM2Skipped=%llu,%llu,%llu,%llu (sun near,far moon near,far)",
                replaySlotTested[0],replaySlotTested[1],replaySlotTested[2],replaySlotTested[3],replaySlotBounded[0],replaySlotBounded[1],replaySlotBounded[2],replaySlotBounded[3],
                replaySlotCulled[0],replaySlotCulled[1],replaySlotCulled[2],replaySlotCulled[3],replaySlotM2Skipped[0],replaySlotM2Skipped[1],replaySlotM2Skipped[2],replaySlotM2Skipped[3]);
            for(unsigned n=0;n<4;++n)replaySlotTested[n]=replaySlotBounded[n]=replaySlotCulled[n]=replaySlotM2Skipped[n]=0;}
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD replay poses draws=%zu groups=%u prepared=%zu reused=%zu scope=directional exact=1",replays.size(),replays.empty()?0:replays.back()->constantGroup+1,directionalResult.replayPosePrepared,directionalResult.replayPoseReused);
        DWORD elapsed=GetTickCount()-submissionStart;
        if(elapsed>40&&slowReports++<12)logf("WORLD slow submission: %lu ms; replay=%zu liveTerrainChunks=%zu",(unsigned long)elapsed,replays.size(),liveTerrainChunks.size());
        shadowsComposited=geometryReady&&applyDirectionalShadows&&(sourceActive[0]||sourceActive[1])&&debug==0;
        if(shadowsComposited)terrainShadowPrograms.publish(sourceActive,sourceMatrices);
        bucket(NorthlightEffectsBuckets::Composite); /* the world composite, temporal and the frame's logs */
        return true;
    }
};
