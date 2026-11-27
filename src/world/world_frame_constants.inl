    struct ScreenFrameInputs {
        UINT w,h;
        float farZ,minZ,maxZ;
        int debug,firstSource;
        const bool (&sourceActive)[2];
        bool probes,hdrMode;
        const std::string& frameMap;
    };
    struct ScreenFrameConstants {
        float rows[68][4]={};
        float shadowTuning[4]={};
        NorthlightLegacyFog::Constants uploadedFog{};
        NorthlightHorizonHaze::Constants haze{};
        float hazeZone=0;
    };

    bool prepareFrameHistory(const NorthlightWorldLightHistory::View& historyView,bool lightingNeeded,
                             int debug,bool geometryReady,unsigned rendererFrame,float (&c)[68][4]){
        const bool useHistory=history.accepts(historyView,lightingNeeded,debug);
        if(useHistory!=traceHistory){
            const DWORD now=GetTickCount();
            if(DWORD(now-traceHistoryTick)>=1000){traceHistoryTick=now;traceHistoryCount=0;}
            if(NorthlightDiagnostics::enabled()&&traceHistoryCount++<16)logf("WORLD history transition frame=%u rendererFrame=%u tick=%lu used=%u valid=%u sameMap=%u cameraContinuous=%u debug=%d geometry=%u",unsigned(frames),rendererFrame,(unsigned long)now,unsigned(useHistory),unsigned(history.valid()),unsigned(history.sameMap(historyView)),unsigned(history.continuousCamera(historyView)),debug,unsigned(geometryReady));
            traceHistory=useHistory;
        }
        memcpy(c[53],history.previousView(),64);c[57][0]=useHistory?.75f:0.f;
        return useHistory;
    }

    void recordFrameLocalSources(const NorthlightLocalLightSelection::Selection& localLights,unsigned diagnosticCapture){
        sourceTrace.begin(NorthlightDiagnostics::enabled()&&NorthlightQuality::renderProfile(quality)&&
            (diagnosticCapture||(equipmentLightsOn()&&((localSourceTraces<32&&frames%127==0)||captureSampled))),
            equipmentLightsOn()?particleLights.output.data():nullptr,equipmentLightsOn()?particleLights.outputCount:0,localLights);
        if(sourceTrace.enabled)++localSourceTraces;
        localDirectCount=localLights.count;localDirectNearest=localLights.nearest;
        if(equipmentLightsOn()&&captureSampled){
            const auto uploads=NorthlightParticleUploads::cache().stats();
            logf("PARTICLELIGHT probes=%u distant=%u draws=%u background=%u accepted=%u deferred=%u triangles=%u points=%u output=%u shaderMiss=%u textureMiss=%u bufferMiss=%u stateMiss=%u (blendOp=%u alphaTest=%u colorWrite=%u depth=%u sampler=%u projection=%u) geometryMiss=%u dropped=%u uploadHits=%llu uploadMisses=%llu uploadKiB=%u ttlMs=0 readback=0",particleLights.probes,particleLights.distant,particleLights.draws,particleLights.backgroundDraws,particleLights.accepted,particleLights.deferred,particleLights.triangles,particleLights.points,particleLights.immediateCount,particleLights.shaderMiss,particleLights.textureMiss,particleLights.bufferMiss,particleLights.stateMiss,particleLights.stateWhy[0],particleLights.stateWhy[1],particleLights.stateWhy[2],particleLights.stateWhy[3],particleLights.stateWhy[4],particleLights.stateWhy[5],particleLights.geometryMiss,particleLights.dropped,static_cast<unsigned long long>(uploads.hits),static_cast<unsigned long long>(uploads.misses),NorthlightParticleUploads::cache().bytes()/1024);
            for(unsigned i=0;i<particleLights.textureMissCount;++i){const auto& m=particleLights.textureMisses[i];
                static const char* const seen[]={"never","held","large","evicted","invalidated","partial","failed","unsupported","released"};
                logf("PARTICLELIGHT textureMiss key=%p seen=%s type=%u size=%ux%u levels=%u format=0x%x pool=%u usage=0x%x",m.key,m.seen<9?seen[m.seen]:"?",m.type,m.width,m.height,m.levels,m.format,m.pool,m.usage);}
            particleLights.textureMissCount=0;
            for(unsigned i=0;i<particleLights.outputCount;++i){const auto& light=particleLights.output[i];
                if(std::find(particleLights.immediate.begin(),particleLights.immediate.begin()+particleLights.immediateCount,light.sourceId)==particleLights.immediate.begin()+particleLights.immediateCount)continue;
                logf("PARTICLELIGHT source id=%llu position=(%.3f %.3f %.3f) rgb=(%.4f %.4f %.4f) range=%.2f cameraYards=%.2f",static_cast<unsigned long long>(light.sourceId),light.position[0],light.position[1],light.position[2],light.diffuse[0],light.diffuse[1],light.diffuse[2],light.attenuationEnd,std::sqrt(NorthlightEquipmentLights::distance2(light.position,context.camera)));}
            logf("EQUIPMENT lights nativePoint=1 attempts=%u banks=%u observed=%u duplicateStatic=%u dropped=%u active=%u output=%u maxLights=%u memoBanks=%u ttlMs=250 falloff=bounded-linear-approx candidates=%u actorCandidates=%u actorReads=%u backgroundReads=%u backgroundBanks=%u duplicateBanks=%u orderedDuplicates=%u readLimited=%u cachedBanks=%u domainRejected=%u decoded=%u rejected=%u",equipmentCapture.reads,equipmentLights.banks,equipmentLights.observed,equipmentLights.duplicates,equipmentLights.dropped,equipmentLights.count,equipmentLights.outputCount,NorthlightEquipmentLights::Capacity,NorthlightEquipmentLights::MemoBanks,equipmentCapture.candidates,equipmentCapture.actorCandidates,equipmentCapture.actorReads,equipmentCapture.backgroundReads,equipmentLights.backgroundBanks,equipmentLights.bankDuplicates,equipmentLights.orderedBankDuplicates,equipmentCapture.readLimited,equipmentCapture.cached,equipmentCapture.domainRejected,equipmentCapture.decoded,equipmentCapture.rejected);
            for(unsigned i=0;i<equipmentLights.count;++i){const auto& entry=equipmentLights.entries[i];if(entry.gain<=.0001f)continue;
                const auto light=NorthlightEquipmentLights::light(entry.source,entry.id,entry.gain);
                const float selfDistance=actorShadowHistory.selfHold()>0?std::sqrt(NorthlightEquipmentLights::distance2(light.position,actorShadowHistory.selfAt())):-1.f;
                logf("EQUIPMENT source id=%llu position=(%.3f %.3f %.3f) nativeRGB=(%.4f %.4f %.4f) denominator=(%.4f %.4f %.4f) rgb=(%.4f %.4f %.4f) range=%.2f gain=%.3f cameraYards=%.2f selfYards=%.2f",static_cast<unsigned long long>(light.sourceId),light.position[0],light.position[1],light.position[2],entry.source.color[0],entry.source.color[1],entry.source.color[2],entry.source.attenuation[0],entry.source.attenuation[1],entry.source.attenuation[2],light.diffuse[0],light.diffuse[1],light.diffuse[2],light.attenuationEnd,entry.gain,std::sqrt(NorthlightEquipmentLights::distance2(light.position,context.camera)),selfDistance);}
        }
    }

    // Finalize the render-thread cloud admission before the spell fit chooses
    // its atmosphere sample and before the frame bank is published.
    void prepareAtmosphereConstants(float (&c)[68][4],const NorthlightWeatherEffects::Frame& wx,
                                    NorthlightFogClouds::Frame& cf,float lampGain,int debug){
        c[52][0]=float(std::min(localDirectCount,NorthlightLocalLightSelection::DirectBatchSize));c[52][1]=.9f*lampGain;
        // Near fade: no added fog within 3.5 units of the viewer, full at 15.5.
        // Same soft ramp, shifted 0.5 world units closer.
        // Isotropic scattering is much weaker than the diffuse surface response.
        // Keep the real medium/range; do not amplify it into a luminous sphere.
        c[58][0]=3.5f;c[58][1]=1.f/12;c[58][2]=.25f*lampGain*wx.lampFogGain();c[58][3]=.12f*lampGain*wx.lampFogGain();
        const float airFloor=atmosphere.airFloor; /* atmosphereWork: .0017f plus the rain's extra, thinned in dense zones */
        c[59][2]=std::max(wx.shadowSoften(),1.f-underwaterFade.scale()); /* underwater: the sun correction goes to 0 and the shadow softens fully (the rain path), alpha and baseline keep the true visibility */ c[59][3]=airFloor; /* 0.3.198 (rain): direct shadow softening (0 when dry), air extinction floor (WorldFog) */
        if(regionalFog.field()){c[31][0]=regionalFog.field()->originX;c[31][1]=regionalFog.field()->originY;}
        c[31][2]=1.f/(NorthlightRegionalFog::N*NorthlightRegionalFog::Spacing);
        c[31][3]=atmosphere.c[31][3]; /* nightFactor, atmosphereWork */
        // Neutral scattering albedo preserves the zone/source palette (c22.w and c32: atmosphereWork, the same expressions).
        c[22][0]=c[22][1]=c[22][2]=1;c[22][3]=atmosphere.c[22][3];memcpy(c[32],atmosphere.c[32],16);
        c[65][3]=regionalFog.field()&&regionalFog.heightTexture()?1.f:0.f;
        // 0.3.199 (fog clouds): the frame derived in atmosphereWork; the device part of its activity (the noise volume) is decided here.
        cf.active=cf.active&&effects.fog&&debug==0&&regionalFogGpu.clouds()&&ensureCloudNoise();
        if(cf.active){
            NorthlightFogClouds::shaderConstants(cf,&c[59]); /* c59.y, c60..c63 (host-folded for the shader's early outs) */
        }
        c[65][0]=regionalFog.globalHeights()[0]-.25f;
        c[65][1]=regionalFog.globalHeights()[1]+std::max({regionalFog.globalHeights()[2],6.f,c[32][2],c[32][3],cf.active?c[62][0]:0.f})+.25f;
        c[65][2]=regionalFog.field()&&regionalFog.globalHeights()[2]>0?1.f:0.f;
        if(profileSampled())logf("WORLD fog clouds active=%d coverage=%.3f threshold=%.3f height=%.1f speed=%.2f dir=(%.2f %.2f) sigmaMax=%.4f lush=%.2f noiseReady=%d",cf.active?1:0,cf.coverage,cf.threshold,cf.height,cloudWind.speed,cloudWind.dir[0],cloudWind.dir[1],cf.sigmaMax,cloudLush,fogCloudNoise().ready.load(std::memory_order_acquire)?1:0);
    }

    void setScreenCamera(float (&c)[68][4],UINT w,UINT h,float nearZ,float farZ,float minZ,float maxZ)const{
        c[0][0]=1.f/w;c[0][1]=1.f/h;c[0][2]=nearZ;c[0][3]=farZ;
        c[1][0]=projection[0];c[1][1]=projection[1];c[1][2]=projection[2];c[1][3]=minZ;
        c[2][0]=1.f/(maxZ-minZ);
        memcpy(c[3],context.inverseView,64);
        c[33][0]=float(w);c[33][1]=float(h);c[33][2]=float(w/2);c[33][3]=float(h/2);
    }

    void prepareFrameShaderBank(const ScreenFrameInputs& in,const NorthlightWeatherEffects::Frame& wx,
                                const NorthlightFogClouds::Frame& cf,const NorthlightCelestialProfiles::Profile& volumePalette,
                                ScreenFrameConstants& frame){
        auto& c=frame.rows;
        const UINT w=in.w;
        const float farZ=in.farZ;
        const int debug=in.debug,firstSource=in.firstSource;
        const auto& sourceActive=in.sourceActive;
        const bool hdrMode=in.hdrMode;
        const auto& frameMap=in.frameMap;
        skyTransmittanceFrame=cf.active?atmosphere.veil[1]:atmosphere.veil[0]; /* the clouds' term only when the pass is really active (1: no fog field or no fog) */
        setScreenCamera(c,w,in.h,c[0][2],farZ,in.minZ,in.maxZ);
        c[2][1]=NorthlightShadowCascades::NearRadius;c[2][2]=farMapRadius(firstSource);c[2][3]=1.f/1024;
        memcpy(c[7],sourceMatrices[firstSource][0],64);memcpy(c[11],sourceMatrices[firstSource][1],64);
        memcpy(c[15],context.camera,12);memcpy(c[16],context.lightDirection,12);memcpy(c[17],context.direct,12);memcpy(c[18],context.ambient,12);
        /* Ambient policies follow the selected solar/lunar schedule. */
        const auto naturalSky=NorthlightForeverSky::evaluateSelected(celestialValid?celestial.dayFraction:-1.,NorthlightQuality::foreverCelestial(quality));
        c[18][3]=NorthlightTwilightFill::gain(naturalSky.valid,naturalSky.valid?celestial.dayFraction:-1.,
            naturalSky.valid?naturalSky.sun.direction[2]:0.f,naturalSky.valid?naturalSky.moon.direction[2]:0.f);
        c[18][3]+=wx.ambientLift(); /* 0.3.198 (rain): a little more sky ambient from the GI pass in rain (+0 when dry; the probes themselves are untouched) */
        NorthlightNightFloor::nightAmbient(quality.nightBrightness,NorthlightNightFloor::weight(naturalSky.valid,naturalSky.valid?naturalSky.sun.direction[2]:0.f),c[18],c[19]);
        c[66][0]=float(quality.giPrelitMix)*.01f;c[66][1]=float(quality.giBrightness)*.01f; // GI only; volume passes later own c66 as FogActorBounds.
        c[19][3]=8; /* c19.rgb: NightBrightness light per unit of albedo, from c18's ambient (WorldComposite only; 0 = off); c19.w: probe spacing */
        c[20][0]=float(NorthlightGI::probeLayout().atlas);c[20][1]=in.probes?NorthlightQuality::giIntensity(quality):0.f;c[20][2]=std::max(.01f,std::min(1.f,quality.shadowStrength*.0085f));c[20][3]=in.probes&&probeAtlasReady(frameMap)?1.f:0.f;
        const DWORD now=visualTime();
        c[21][0]=regionalFog.field()&&(regionalFog.field()->fogCells||regionalFog.field()->airCells)?1.f:0.f;c[21][1]=1.2f*wx.shaftGain();c[21][2]=hdrMode?64.f:.38f;c[21][3]=128; /* HDR retains radiance above diffuse white until output mapping. */
        // 0.3.200 (gpu budget): fewer march intervals at reduced levels (c60.x clouds, c64.z WorldFog; both stay 0, the bank unchanged, at level 0).
        if(gpuBudgetLevel){if(cf.active)c[60][0]=NorthlightGpuBudget::spacingDelta(c[21][3],NorthlightGpuBudget::cloudSteps(gpuBudgetLevel),40);c[64][2]=NorthlightGpuBudget::spacingDelta(c[21][3],NorthlightGpuBudget::fogSteps(gpuBudgetLevel),48);}
        memcpy(c[23],context.camera,12);c[24][0]=NorthlightWorldMath::ShadowBiasWorld*NorthlightWorldMath::InverseShadowDepth;c[24][1]=2;c[24][2]=float(debug);c[24][3]=visualAnimationSeconds(now);
        // Readable fog as measured, else the held value, for every c25/c26 reader (WorldComposite, smoothRemoval, FogClouds); render() runs once per frame, so dt is one frame.
        frame.uploadedFog=legacyFogHold.update(frameMap,legacyFog,legacyFogKnown,now);
        const auto& uploadedFog=frame.uploadedFog;
        if(traceFog==0&&!legacyFogKnown&&legacyFogHold.has)traceFog=2;
        memcpy(c[25],uploadedFog.parameters,16);memcpy(c[26],uploadedFog.color,16);
        memcpy(c[28],context.lightDirection,12);memcpy(c[29],context.direct,12);
        c[27][2]=c[24][3];
        c[27][3]=c[21][0];
        c[35][0]=NorthlightWorldMath::InverseShadowDepth;
        // Horizon haze: the game's own fog end, smoothed per map and held while
        // unknown. Extras in free components c57.yzw / c67.zw; c34 is set for the
        // final pass only. Fog off (Ctrl+Shift+F7) or HorizonHaze=0: optical depth 0.
        horizonHazeState.update(frameMap,legacyFog.parameters,legacyFog.color,projection[2],double(now)*.001);
        const float hazeSun[3]={sourceDirections[0].x,sourceDirections[0].y,sourceDirections[0].z};
        frame.hazeZone=NorthlightCelestialProfiles::horizonHaze(volumePalette,c[31][3]);
        float hazeLift[3];NorthlightSunHue::horizonLift(glowHueFrame,hazeLift);
        // The warm lobe toward the sun goes with the sun's light, which ends with its disc (discVisibility).
        const float hazeSunWeight=underwaterFade.scale()*sourceWeights[0];
        frame.haze=NorthlightHorizonHaze::constants(horizonHazeState,{quality.horizonHaze,quality.horizonHazeStart,quality.horizonHazeBand,quality.horizonHazeTerrain},
            farZ,frame.hazeZone,effects.fog&&!atmosphere.imported.valid,celestialValid,hazeSun,hazeSunWeight,hazeLift,wx.hazeTauScale());
        if(debug==0&&!menuDomain){
            const auto look=NorthlightCelestialProfiles::appearance(volumePalette,horizonHazeState.color,effects.fog,appearanceTime());
            for(unsigned channel=0;channel<3;++channel)frame.haze.haze[channel]*=1.f+look[channel];
            c[20][2]=std::min(1.f,c[20][2]*(1.f+look[3]));
        }
        const auto& haze=frame.haze;
        c[57][1]=haze.shape[0];c[57][2]=haze.shape[1];c[57][3]=haze.shape[2];c[67][2]=haze.sun[0];c[67][3]=haze.sun[1];
        c[35][1]=haze.lift[0];c[35][2]=haze.lift[1];c[35][3]=haze.lift[2]; /* lift colour = the glow hue (WorldComposite reads c35.yzw) */
        hazeFrame=haze;
        {   // The far edge toward the sun for the discs and the glow ring, from the regional field's surface (water over ground).
            float target=0;
            if(celestialValid&&regionalFog.field()){const auto& field=*regionalFog.field();
                target=NorthlightApparentHorizon::sine(context.camera,celestial.sun.direction,farZ,[&](float x,float y,float& height){
                    const auto g=NorthlightSpellFog::fieldAt(field,x,y);height=g.ground;return g.coverage>.5f;});}
            c[64][3]=apparentHorizon.update(target,now); /* HorizonEdge.w: the composite's haze band starts at the far edge */
        }
        // 0.3.159 shadow removal smoothing (TemporalLight smoothRemoval, c30.yzw), always on. The drawn
        // lighting passes are the sources with a positive weight (plus firstSource at weight 0), so
        // their summed weight is the lighting-buffer alpha of a fully lit pixel. Without a drawn
        // weight nothing painted is removed and y stays 0. w: the 0.75-unit disc radius
        // (1.5 across) in half-res pixels at view distance 1.
        const float drawnWeight=std::max(sourceWeights[0],0.f)+std::max(sourceWeights[1],0.f);
        if(drawnWeight>.001f){c[30][1]=1;c[30][2]=1/drawnWeight;c[30][3]=.75f*std::fabs(projection[0])*float(w/2)*.5f;}
        // 0.3.170 TemporalLight (c15.w, TemporalReach): the largest direct-light channel per unit of
        // normalised visibility over the drawn, active sources. It bounds how far a flickering shadow
        // may carry the rgb history outside its neighbourhood (0: exactly the tight clamp).
        for(int source=0;source<2;++source)if(sourceWeights[source]>0&&sourceActive[source])
            c[15][3]=std::max(c[15][3],std::max({sourceColors[source].x,sourceColors[source].y,sourceColors[source].z})*drawnWeight/sourceWeights[source]);
    }

    void publishFrameShaderBank(ScreenFrameConstants& frame,bool waterReady,IDirect3DTexture9* const (&textures)[14]){
        auto& c=frame.rows;
        c[30][0]=waterReady?1.f:0.f;
        auto& shadowTuning=frame.shadowTuning;
        shadowTuning[0]=quality.shadowStrength*.01f;shadowTuning[1]=quality.shadowSoftness*.01f;
        shadowTuning[2]=quality.shadowContactRadius*.01f;shadowTuning[3]=1.f;
        screenLighting.bindFrameBank(d,c,shadowTuning,textures);
    }
