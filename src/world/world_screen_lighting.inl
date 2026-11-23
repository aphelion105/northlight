    struct CelestialLightingInput {
        UINT w,h;
        unsigned rendererFrame;
        int firstSource;
        const bool (&sourceActive)[2];
        bool geometryReady,applyDirectionalShadows,probes;
        float probeGridActive;
        bool prelitDaylight,fullMaterial;
        float moonTerrainSpecular,sunSurfaceSpecular;
        IDirect3DTexture9 *probeBinding,*fogBinding;
        const char* geometryReason;
        const std::string& map;
    };

    // The current visibility and normals are bound. Write the raw light and
    // baseline for temporal resolve, plus the optional prelit daylight target.
    template<class BindSource>
    bool renderCelestialLighting(const CelestialLightingInput& in,const DirectionalVisibility& visibility,
                                 NorthlightGpuProfile* profile,NorthlightLightingCapture::Capture* lightingCapture,
                                 const BindSource& bindSource,const std::string* lightHistoryAudit){
        const float bumpControl[]={materialDetail?materialBumpGain:0.f,materialBumpDistance,in.fullMaterial?1.f:0.f,0};
        float materialLight[4][4]={{sourceDirections[0].x,sourceDirections[0].y,sourceDirections[0].z,in.sunSurfaceSpecular},
                                   {sourceDirections[1].x,sourceDirections[1].y,sourceDirections[1].z,in.moonTerrainSpecular},{},{}};
        for(unsigned source=0;source<2;++source)if(in.sourceActive[source]){
            materialLight[2+source][0]=sourceColors[source].x;
            materialLight[2+source][1]=sourceColors[source].y;
            materialLight[2+source][2]=sourceColors[source].z;
        }
        materialLight[3][3]=in.moonTerrainSpecular>0?MoonTerrainSpecularDistance:0.f;
        const NorthlightWorldScreenLighting::Input pass{
            in.w,in.h,in.rendererFrame,in.firstSource,sourceWeights,in.applyDirectionalShadows,
            visibility.softResolved,in.prelitDaylight,directionalVisibility.lighting(visibility.sharedResolved),directionalVisibility.texture(),
            {in.probes,in.probeGridActive,probeAtlas.localVisibility(),probeAtlas.previous(),in.probeBinding,in.fogBinding},
            materialDetail,bumpControl,materialLight
        };
        NorthlightWorldScreenLighting::Audit audit{lightHistoryAudit,lightingCapture,frames,
            active?active->serial:0,uploadedSerial,0,0,nullptr,nullptr,nullptr};
        if(lightHistoryAudit&&in.probeGridActive>.5f&&active&&active->serial==uploadedSerial&&active->probeRadiance.known){
            const auto& light=active->probeRadiance;
            audit.request=active->requestId;audit.generation=active->lightingGeneration;
            audit.sunDirection=&light.sunDirection.x;audit.sunIrradiance=&light.sunIrradiance.x;audit.skyRadiance=&light.skyRadiance.x;
        }
        return screenLighting.draw(d,pass,audit,profile,effectsBuckets,
            [&](HRESULT hr,const char* name){return check(hr,name);},bindSource,[&](bool unshadowedLighting){
                if(!profileSampled())return;
                logf("WORLD_LIGHTING rendererFrame=%u variant=%s shadows=%u geometry=%u gi=%u probes=%u fogShadowMaps=%u shadowTransitionStarts=%u shadowTransitionFailures=%u moonFade=%u,%u moonWeights=%.4f,%.4f",in.rendererFrame,unshadowedLighting?"unshadowed":visibility.softResolved?"soft":"standard",unsigned(in.applyDirectionalShadows),unsigned(in.geometryReady),unsigned(in.probes),unsigned(in.probeGridActive>.5f),unsigned(in.geometryReady&&in.applyDirectionalShadows),shadowTransitionStarts,shadowTransition.failures(),unsigned(shadowTransition[2].state.fading()),unsigned(shadowTransition[3].state.fading()),shadowTransition[2].state.weight(),shadowTransition[3].state.weight());
                logf("WORLD screen geometry=%u meshReason=%s material=%u fog=%u regional=%u heightBounds=%u colorScratch=%u map=\"%s\"",unsigned(in.geometryReady),in.geometryReason,unsigned(in.fullMaterial),unsigned(effects.fog),unsigned(regionalFog.field()!=nullptr),unsigned(regionalFog.field()&&regionalFog.heightTexture()),unsigned(composite.scratchTexture()!=nullptr),in.map.c_str());
            });
    }
