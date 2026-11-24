    static constexpr float LampFogNear=3.5f; /* c58.x: the lamp glow's near fade start, the scissor view's fog near */
    // The frame's D3D-free light and fog work. Its inputs are final once upload() has run (the snapshot and its lights, the fog field,
    // the camera, the celestial state, the settings); its outputs (the tracker, the clouds' clock and state, localScissorView and this
    // frame) are read only after the join at the constant bank. c holds the bank's c22.w, c31.w and c32 (the same expressions).
    struct AtmosphereFrame {
        NorthlightForever::Frame imported;
        NorthlightLocalLightSelection::Selection lights;NorthlightFogClouds::Frame cf;float c[33][4]={};
        float airFloor=.0017f,veil[2]={1,1}; /* veil: the sky transmittance toward the sun without / with the clouds' term */
        float spellTau[2][NorthlightSpellFog::kSamples]={};bool spellValid=false; /* SpellFog: the optical depth along the view axis at kDistance, without / with the clouds' term (the same sigma as the veil) */
        NorthlightLocalLightScissor::Rect directRects[NorthlightLocalLightSelection::Slots/NorthlightLocalLightSelection::DirectBatchSize],
            fogRects[NorthlightLocalLightSelection::Slots/NorthlightLocalLightSelection::FogBatchSize];
        NorthlightLocalLightTrace::InputHash authoredLightAudit;
        double lightAuditDelta=0;
        bool lightAuditContinuous=false;
    };
    AtmosphereFrame atmosphere;NorthlightJobs::Counter atmosphereDone;NorthlightNativeLights::Snapshot nativeSceneFrame;
    void logLocalLightInputs(unsigned rendererFrame,bool geometryReady){
        using NorthlightLocalLightTrace::InputHash;
        InputHash native,equipment,particles,immediate,selected,view;
        native.add(nativeSceneFrame.lights.data(),nativeSceneFrame.count);
        equipment.add(equipmentLights.output.data(),equipmentLights.outputCount);
        particles.add(particleLights.output.data(),particleLights.outputCount);
        immediate.add(particleLights.immediate.data(),particleLights.immediateCount);
        const auto& lights=atmosphere.lights;
        selected.add(lights.sources.data(),lights.count);
        selected.add(lights.position.data(),lights.count);
        selected.add(lights.color.data(),lights.count);
        selected.add(lights.fog.data(),lights.count);
        view.add(context.inverseView,16);view.add(context.camera,3);view.add(projection,3);
        const auto* snapshot=NorthlightStream::activeSnapshot;
        logf("LOCAL_INPUTS rendererFrame=%u worldFrame=%u ready=%u map=%s clock=%u dt=%.9g continuous=%u trigger=%u draw=%llu view=%016llx authored=%016llx native=%016llx nativeCount=%u nativeTime=%u banks=%016llx bankCount=%u drawLights=%016llx decoded=%u equipment=%016llx equipmentCount=%u particles=%016llx immediate=%016llx selected=%016llx selectedCount=%u",
             rendererFrame,frames,unsigned(geometryReady),active?active->map.c_str():"-",unsigned(visualTime()),atmosphere.lightAuditDelta,unsigned(atmosphere.lightAuditContinuous),
             snapshot?snapshot->triggerKind:0u,snapshot?static_cast<unsigned long long>(snapshot->triggerDraw):0ull,view.value,atmosphere.authoredLightAudit.value,
             native.value,nativeSceneFrame.count,nativeSceneFrame.time,equipmentBankAudit.value,equipmentLights.banks,equipmentDrawAudit.value,equipmentCapture.decoded,
             equipment.value,equipmentLights.outputCount,particles.value,immediate.value,selected.value,lights.count);
    }
    void atmosphereWork(AtmosphereFrame& a,float nearZ,UINT w,UINT h,int debug){
        auto& c=a.c;auto& localLights=a.lights;const auto wx=weatherEffects();
        a.imported=foreverFog.available()?skyEnvironment:NorthlightForever::Frame{};
        for(auto& layer:a.imported.layer)layer.p[1]*=1+(std::max(layer.p[2],0.f)-1)*layer.flag[0]*(1-(celestialValid?celestial.sunWeight:1.f));
        if(quality.localLights&&quality.localLightLimit){
        // 0.3.197: tracked selection. A camera jump (teleport), a long gap (F10 off, loading) or F12 debug snap the
        // factors without fades; a different map (and a rebuilt device, releasePointGPU) forgets the tracker.
        if(!active||active->map!=localLightMap)localLightTracker.reset();
        const int64_t selectT0=QpcClock::now();
        const int64_t selectNow=NorthlightFrameParityClock::enabled()
            ?NorthlightFrameParityClock::counter(parityFrame,captureFrequency.QuadPart):selectT0;
        const double selectDt=localLightQpc&&captureFrequency.QuadPart>0?double(selectNow-localLightQpc)/double(captureFrequency.QuadPart):0;
        const bool selectContinuous=active&&localLightQpc&&selectDt<=NorthlightLocalLightSelection::MaxGapSeconds&&active->map==localLightMap&&!different(vec(context.camera),localLightCamera,40)&&debug==0;
        lightAttachments.bind(active?active->lightAttachments:nullptr);
        if(active)lightAttachments.prune(active->localLights,context.camera);
        if(active&&equipmentLightsOn())lightAttachments.observeScene(nativeSceneFrame,active->localLights,visualTime());
        else lightAttachments.releaseScene();
        const auto* currentLights=active?&lightAttachments.apply(active->localLights,attachedLocalLights):nullptr;
        if(NorthlightLocalLightTrace::auditInputs()){
            a.authoredLightAudit={};
            if(currentLights)a.authoredLightAudit.add(currentLights->data(),currentLights->size());
            a.lightAuditDelta=selectDt;a.lightAuditContinuous=selectContinuous;
        }
        lightAttachments.forgetInactive(localLightTracker);
        if(equipmentLightsOn()){
            if(active&&equipmentMap!=active->map){equipmentLights.reset();equipmentMap=active->map;}
            const auto* fixedIndex=active&&currentLights==&active->localLights?active->localLightIndex.get():nullptr;
            if(currentLights)equipmentLights.observeScene(nativeSceneFrame,visualTime(),*currentLights,context.camera,lightAttachments.claimedNative.data(),lightAttachments.claimedNativeCount,fixedIndex);
            particleLights.projectSky(context.inverseView,context.camera);
            equipmentLights.publish(visualTime(),currentLights,fixedIndex);
            if(equipmentLights.suppressedCount){
                auto ids=equipmentLights.suppressed;std::sort(ids.begin(),ids.begin()+equipmentLights.suppressedCount);
                localLightTracker.forgetSorted(ids.data(),equipmentLights.suppressedCount);
            }
            NorthlightEquipmentLights::replaceFadingDuplicates(localLightTracker,equipmentLights.output.data(),equipmentLights.outputCount);
            if(active)particleLights.publish(equipmentLights.output.data(),equipmentLights.outputCount,*currentLights,context.camera,localLightTracker,selectContinuous?float(selectDt):0.f);
            else particleLights.reset();
        }
        // Same camera, projection, near plane and glow start as c0..c6/c58: the selection prefers lights in view (debug views keep the old order), and
        // each batch's rect of the direct (8) and fog (4) passes.
        localScissorView=NorthlightLocalLightScissor::view(context.inverseView,projection,nearZ,LampFogNear,w,h);
        a.lights=active?localLightTracker.update(*currentLights,context.camera,quality.localLightLimit,float(selectDt),selectContinuous,debug==0?&localScissorView:nullptr,equipmentLightsOn()?particleLights.output.data():nullptr,equipmentLightsOn()?particleLights.outputCount:0,equipmentLightsOn()?particleLights.immediate.data():nullptr,equipmentLightsOn()?particleLights.immediateCount:0,visualTime()) /* one shared dynamic cap; particles carry current-frame alpha */:NorthlightLocalLightSelection::Selection{};
        {const auto arranged=NorthlightLocalLightBatches::arrange(localLights,localScissorView); /* screen-coherent batches (the passes are sums) */
         if(NorthlightDiagnostics::enabled()){localOffscreenSum+=localLightTracker.offscreen;localArranged+=arranged.arranged;localArrangeBefore+=arranged.before;localArrangeAfter+=arranged.after;}} /* counted with selectUs */
        if(NorthlightDiagnostics::enabled()){const double us=double(QpcClock::now()-selectT0)*1e6/(captureFrequency.QuadPart>0?double(captureFrequency.QuadPart):1.);localSelectUsSum+=us;localSelectUsMax=std::max(localSelectUsMax,us);++localSelectFrames;} /* selectUs on LOCAL direct */
        localLightQpc=selectNow;localLightCamera=vec(context.camera);if(active&&localLightMap!=active->map)localLightMap=active->map;
        }else{
            // No lamp consumer: skip attachment transforms, copies, selection and scissor batches.
            a.lights={};localLightTracker.reset();lightAttachments.reset();attachedLocalLights.clear();
            equipmentLights.reset();particleLights.reset();localLightQpc=0;localLightMap.clear();
            a.authoredLightAudit={};a.lightAuditDelta=0;a.lightAuditContinuous=false;
        }
        // 0.3.199 (fog clouds): the frame clock of the clouds (a gap or the first frame counts as 0; advance() clamps) and the dense-zone damping: where the
        // regional fog is already thick (the field's profile tag at the camera, Duskwood 1) the rain's extra air and the clouds thin out, the profile
        // smoothed over ~3 s so a zone border never pops. The air palette scales the dry medium; rain adds its own extinction.
        const int64_t cloudNow=QpcClock::now();
        const float cloudDt=cloudQpc&&captureFrequency.QuadPart>0?float(double(cloudNow-cloudQpc)/double(captureFrequency.QuadPart)):0.f;
        cloudQpc=cloudNow;
        // The same camera texel says whether the zone is lush (forest/grass): without rain only lush zones get banks (no field, indoors or unknown: unchanged; it starts lush).
        {float denseTarget=0,lushTarget=cloudLush;
         if(regionalFog.field()){const auto& f=*regionalFog.field();
             const float fx=(context.camera[0]-f.originX)/NorthlightRegionalFog::Spacing,fy=(context.camera[1]-f.originY)/NorthlightRegionalFog::Spacing;
             if(fx>=0&&fy>=0&&fx<NorthlightRegionalFog::N-1&&fy<NorthlightRegionalFog::N-1){const unsigned k=unsigned(fy)*NorthlightRegionalFog::N+unsigned(fx);const auto& t=f.texels[k];
                 if(t.height>0)denseTarget=std::clamp((t.height-2.5f)/2.5f,0.f,1.f);
                 if(t.height>0)lushTarget=f.lush[k]?1.f:0.f;}}
         NorthlightFogClouds::smoothDensePair(cloudDenseZone,denseTarget,cloudLush,lushTarget,cloudDt);}
        const float denseDamp=NorthlightFogClouds::denseZoneDamp(cloudDenseZone);
        c[31][3]=NorthlightRegionalFog::nightFactor(celestialValid?celestial.dayFraction:-1.f);
        const auto& volumePalette=paletteFrameValid?framePalette:celestialProfiles.fallback;
        const float airDensity=NorthlightCelestialProfiles::fogDensity(volumePalette,c[31][3]);
        const float airFloor=.0017f*airDensity+wx.airExtinction()*denseDamp;
        a.airFloor=airFloor;
        static_assert(NorthlightLocalLightSelection::DirectBatchSize==2*NorthlightLocalLightSelection::FogBatchSize,"paired scissor batches");
        NorthlightLocalLightScissor::pairedBatchRects<NorthlightLocalLightSelection::FogBatchSize>(localScissorView,localLights.position,localLights.count,a.fogRects,a.directRects);
        // Neutral scattering albedo preserves the zone/source palette. Atmospheric
        // style is separate from ground fog and surface irradiance.
        c[22][3]=.0035f+.0031f*c[31][3]; // generic forest day .0035, night .0066 (+20%)
        // Forest atmosphere follows verified render time, independently of source
        // visibility. Smooth nightFactor avoids a camera-driven density switch.
        // STV total air including the shared .0017: .0054 day (2x),
        // .01245 night (1.5x). Duskwood and general forest air retain their density.
        c[32][0]=.0055f+.0089f*c[31][3];c[32][1]=.0037f+.00705f*c[31][3];
        const float volumeHeightScale=NorthlightCelestialProfiles::fogHeightScale(volumePalette,c[31][3]);
        // One medium for the GPU, lamp glow, celestial veil and spell attenuation.
        c[22][3]*=airDensity;c[32][0]*=airDensity;c[32][1]*=airDensity;
        c[32][2]=64.f*volumeHeightScale;c[32][3]=48.f*volumeHeightScale;
        // Extinction at each picked light for its glow in the fog: same ground
        // and air policy as the shader, evaluated at the light's own position.
        // 0.3.199 (fog clouds): the wind advances on the render thread's own clock (a gap or the first frame counts as 0, advance() clamps), the frame's
        // constants come from the settings and the weather. Inactive (off, no coverage, fog effect off, debug view, noise or shader missing):
        // c60..c63 keep their zeros, the pass is not drawn and sigmaAt below adds nothing.
        if(quality.fogClouds)cloudWind.advance(cloudDt,wx.fog);
        // The noise (and its quantile table) is requested in render() before this, from the settings alone: derive() cannot be active before
        // the table exists. The device part (ensureCloudNoise) is decided at the bank: here the clouds' term is a candidate and the veil keeps both sums.
        auto& cf=a.cf;
        cf=NorthlightFogClouds::derive(a.imported.valid?0:quality.fogClouds,unsigned(std::lround(float(quality.fogCloudDensity)*denseDamp)),wx.fog,c[31][3],cloudWind,context.camera,fogCloudNoise().ready.load(std::memory_order_acquire)?&fogCloudNoise().quantiles:nullptr,cloudLush);
        const bool cloudCandidate=cf.active&&effects.fog&&debug==0&&regionalFogGpu.clouds();
        const uint8_t* cloudData=cloudCandidate?fogCloudNoise().data.data():nullptr;
        a.veil[0]=a.veil[1]=1;a.spellValid=false;
        if(effects.fog&&regionalFog.field()){
            const auto& field=*regionalFog.field();const float night=c[31][3];
            // cloudy: the same sigma plus the clouds' term (the old sigma+= on an active pass), else the same sigma.
            NorthlightSpellFog::March medium;medium.night=night;std::memcpy(medium.volume,c[32],16);medium.forestAir=c[22][3];medium.airFloor=airFloor;
            auto sigmaAt=[&](float lx,float ly,float lz,bool clouds,float& cloudy){
                float sigma=0;cloudy=0;
                if(a.imported.valid){const auto fs=NorthlightSpellFog::fieldAt(field,lx,ly);cloudy=fs.coverage>0&&lz>=fs.ground?NorthlightForever::sigma(a.imported,128,lz,fs.ground)*fs.coverage:0;return cloudy;}
                NorthlightSpellFog::FieldSample sample;
                sigma=NorthlightSpellFog::sigma(field,medium,lx,ly,lz,sample);cloudy=sigma;
                if(clouds&&cloudCandidate&&sample.coverage>0&&lz>=sample.ground){const float point[3]={lx,ly,lz};cloudy+=NorthlightFogClouds::sigmaAt(cloudData,cf,context.camera,point,sample.ground,sample.height,sample.coverage);} // Lamp glow excludes clouds; veil samples share the bilinear medium.
                return sigma;
            };
            float unused=0;
            for(unsigned i=0;i<localLights.count;++i)localLights.fog[i][0]=sigmaAt(localLights.position[i][0],localLights.position[i][1],localLights.position[i][2],false,unused);
            // the veil's estimate of the composite's fog.a on a sky pixel toward the sun: the
            // 128-unit volume along the sun ray from the eye, with the shader's near fade.
            if(field.fogCells||field.airCells){
                float depth=0,cloudyDepth=0;const V ray=sourceDirections[0];
                for(unsigned i=0;i<16;++i){const float t=(float(i)+.5f)*8;
                    const float fade=std::clamp((t-3.5f)/12.f,0.f,1.f);float cloudy=0;
                    const float sigma=sigmaAt(context.camera[0]+ray.x*t,context.camera[1]+ray.y*t,context.camera[2]+ray.z*t,true,cloudy);
                    depth+=sigma*fade*fade*(3-2*fade)*8;cloudyDepth+=cloudy*fade*fade*(3-2*fade)*8;}
                a.veil[0]=std::exp(-depth);a.veil[1]=std::exp(-cloudyDepth);
                // SpellFog: WorldFog's extinction mirrored term by term (spell_fog_march.h: the bilinear field with its coverage, the night profile, VolumeAir, the forest air, the rain's air extinction,
                // the near fade, the heights relative to the ground at every step) for the rays a spell draw has: from the eye toward a point at the camera's heading, kDistance away, at the height of
                // an effect above the ground THERE (a lantern glow, a spell at the player's height), not along the horizontal axis at eye height, which can sit above the ground fog layer that
                // the world at that distance is seen through. The clouds' bank is added when its pass runs, at the same samples (their coverage as the shader's valid).
                if(quality.spellFog&&debug==0){V fwd=V(context.inverseView[8],context.inverseView[9],context.inverseView[10])*projection[2];const float fl=std::sqrt(NorthlightGI::dot(fwd,fwd));
                 if(fl>1e-6f){
                    NorthlightSpellFog::March m;m.night=night;std::memcpy(m.volume,c[32],16);m.forestAir=c[22][3];m.airFloor=airFloor;
                    float hx=fwd.x,hy=fwd.y;const float hl=std::sqrt(hx*hx+hy*hy);if(hl>.05f){hx/=hl;hy/=hl;}else{hx=1;hy=0;}
                    constexpr float kEffectHeight=2.5f;
                    const float eye[3]={context.camera[0],context.camera[1],context.camera[2]};
                    const auto noClouds=[](float,float,float,const NorthlightSpellFog::FieldSample&){return 0.f;};
                    const auto clouds=[&](float x,float y,float z,const NorthlightSpellFog::FieldSample& fs){
                        if(!cloudCandidate)return 0.f;
                        const float point[3]={x,y,z};return NorthlightFogClouds::sigmaAt(cloudData,cf,context.camera,point,fs.ground,fs.height,fs.coverage);};
                    for(unsigned k=0;k<NorthlightSpellFog::kSamples;++k){
                        const float z=NorthlightSpellFog::kDistance[k];
                        float to[3]={eye[0]+hx*z,eye[1]+hy*z,eye[2]};
                        const NorthlightSpellFog::FieldSample g=NorthlightSpellFog::fieldAt(field,to[0],to[1]);
                        if(g.coverage>0){ // the effect's height above the ground at the end point; the horizontal reach shortens so the ray is z long
                            const float dz=std::clamp(g.ground+kEffectHeight-eye[2],-.9f*z,.9f*z),dh=std::sqrt(std::max(z*z-dz*dz,1.f));
                            to[0]=eye[0]+hx*dh;to[1]=eye[1]+hy*dh;to[2]=eye[2]+dz;
                        }
                        float t0,t1;
                        if(a.imported.valid)t0=t1=NorthlightSpellFog::importedOpticalDepth(field,a.imported,eye,to,z);
                        else if(cloudCandidate&&!NorthlightSpellFog::cloudRayAboveSupport(regionalFog.globalHeights(),cf.height,eye[2],to[2]))
                            NorthlightSpellFog::opticalDepthPair(field,m,eye,to,NorthlightSpellFog::RegionalRaySteps,clouds,t0,t1);
                        else t0=t1=NorthlightSpellFog::opticalDepth(field,m,eye,to,NorthlightSpellFog::RegionalRaySteps,noClouds);
                        if(k){t0=std::max(t0,a.spellTau[0][k-1]);t1=std::max(t1,a.spellTau[1][k-1]);}
                        a.spellTau[0][k]=t0;a.spellTau[1][k]=t1;
                    }
                    a.spellValid=true;}}
            }
        }
    }
