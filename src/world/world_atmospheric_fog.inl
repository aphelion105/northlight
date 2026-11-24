    void releaseFogVolume(){
        fogVolume.reset();
        fogVolumeField.reset();
        fogVolumeKey=0;
    }

    struct FogBoundSource {
        bool actor=false,tiledActor=false,ambientOnly=false;
    };
    void bindFogActorSource(int source,bool fogActor,bool directionalReady,bool wideTileMapReady,
                            const float (&neutralMatrix)[16],const float (&c)[68][4],
                            IDirect3DTexture9* fallbackProbe,FogBoundSource& fogSource){
        fogSource.actor=fogActor;
        NorthlightAtmosphereCommands commands(d);
        const float wholeActorMap[4]={-1,-1,1,1};
        commands.bindActor(fogActor?actorMaps.depth(source).texture:screenFallbacks.white(),
            fogActor?actorMatrices[source]:neutralMatrix,fogActor?actorFogBounds[source]:wholeActorMap,fogActor);
        if(profileSampled())logf("FOG_CASTERS frame=%u source=%d static=%u actor=%u actorDraws=%u extraGeometryDraws=0 mapCoverage=%.4f",unsigned(frames),source,unsigned(directionalReady),unsigned(fogActor),fogActor?actorCasterCount[source]:0u,fogActor?(actorFogBounds[source][2]-actorFogBounds[source][0])*(actorFogBounds[source][3]-actorFogBounds[source][1])*.25f:0.f);
        fogSource.tiledActor=fogSource.actor&&wideTileMapReady&&actorMaps.fogTiled()&&actorMaps.cloudsTiled()&&NorthlightActorShadowMap::tiledFogRange(actorMatrices[source],c[15],c[24][0]);
        commands.bindProbeOrTiles(fogSource.tiledActor?actorMaps.tiles(source).texture:fallbackProbe);
        if(profileSampled())logf("FOG_ACTOR_TILES frame=%u source=%d enabled=%u wideCurrent=%u",unsigned(frames),source,unsigned(fogSource.tiledActor),unsigned(wideTileMapReady));
    }

    struct AtmosphericFogInput {
        UINT w,h;
        float farZ;
        int debug,firstSource;
        const bool (&sourceActive)[2];
        bool geometryReady,applyDirectionalShadows,anyShadowTransition,useHistory,distance,clouds;
        const NorthlightForever::Frame& imported;
        const std::string& frameMap;
        const float (&constants)[68][4];
        IDirect3DTexture9* const* textures;
    };
    struct AtmosphericFogResult {
        NorthlightFogScreen::Image image;
        bool active=false,resolved=false,clearSkipped=false,historyClearSkipped=false;
    };
    struct FogVolumePass {
        bool volume,noise,reuse,moving;
        uint64_t key;
        const char* reason;
        unsigned variants[2];
    };

    // Admit both source variants before touching the atlas. The cache key
    // covers the current medium, camera, and visibility inputs.
    template<class VariantFor,class VolumeDirection>
    FogVolumePass prepareFogVolume(const AtmosphericFogInput& in,bool regionalFogActive,bool fogActive,
                                    const VariantFor& variantFor,const VolumeDirection& volumeDirection){
        const UINT w=in.w,h=in.h;
        const float farZ=in.farZ;
        const int debug=in.debug,firstSource=in.firstSource;
        const auto& sourceActive=in.sourceActive;
        const bool geometryReady=in.geometryReady,applyDirectionalShadows=in.applyDirectionalShadows,useHistory=in.useHistory;
        const auto& c=in.constants;
        bool volumeFog=effects.fog&&regionalFogActive&&in.imported.valid&&debug==0&&!fogVolume.refused;
        unsigned volumeVariant[2]={};
        const bool importedNoise=fogActive&&in.imported.valid&&
            (in.imported.layer[0].flag[2]>0||in.imported.layer[1].flag[2]>0||in.imported.layer[2].flag[2]>0)&&ensureCloudNoise();
        // Resolve both sources before writing the first atlas slice.
        for(int source=0;volumeFog&&source<2;++source){
            if((!quality.godrays||!sourceActive[source])&&source!=firstSource)continue;
            volumeVariant[source]=variantFor(source,importedNoise);
            volumeFog=fogVolume.injectShader(d,volumeVariant[source])!=nullptr;
        }
        if(volumeFog)volumeFog=fogVolumeResources(w,h);
        uint64_t volumeKey=14695981039346656037ull;
        auto volumeHash=[&](const void* p,size_t n){const auto* bytes=static_cast<const unsigned char*>(p);for(size_t i=0;i<n;++i){volumeKey^=bytes[i];volumeKey*=1099511628211ull;}};
        bool volumeMoving=false;
        if(volumeFog){
            volumeHash(c[31],sizeof(float)*3); // The imported medium does not read regional night fraction.
            volumeHash(in.imported.layer,sizeof in.imported.layer);volumeHash(in.imported.direct,sizeof in.imported.direct);volumeHash(in.imported.ambient,sizeof in.imported.ambient);
            volumeHash(&quality.godrays,sizeof quality.godrays);
            if(quality.godrays)for(unsigned source=0;source<2;++source){const V dir=volumeDirection(source);const float xyz[]={dir.x,dir.y,dir.z};volumeHash(xyz,sizeof xyz);}
            if(quality.godrays){volumeHash(sourceWeights,sizeof sourceWeights);volumeHash(&geometryReady,sizeof geometryReady);
            volumeHash(sourceActive,sizeof sourceActive);volumeHash(&applyDirectionalShadows,sizeof applyDirectionalShadows);
            const float fade=underwaterFade.scale();volumeHash(&fade,sizeof fade);}
            auto* field=regionalFog.field().get();volumeHash(&field,sizeof field);
            volumeHash(c[0],sizeof(float)*4*7);volumeHash(c[15],sizeof c[15]);
            bool shadowed=false;
            for(const auto& layer:in.imported.layer)if(layer.flag[3]>0&&layer.p[1]>0){
                shadowed|=layer.flag[0]>0;
                if(layer.flag[2]>0)for(unsigned axis=0;axis<3;++axis){
                    volumeMoving|=layer.p[20]>0&&(layer.p[22]>0||layer.p[23]<=0)&&layer.p[11+axis]!=0;
                    volumeMoving|=layer.p[21]>0&&layer.p[23]>0&&layer.p[14+axis]!=0;
                }
            }
            // A cached quiet fallback must wake when the asynchronous noise
            // becomes available, including on a stationary windless frame.
            volumeHash(volumeVariant,sizeof volumeVariant);
            if(shadowed&&quality.godrays){
                volumeHash(&fogShadowRevision,sizeof fogShadowRevision);
                for(unsigned source=0;source<2;++source)volumeMoving|=sourceActive[source]&&
                    ((geometryReady&&applyDirectionalShadows&&quality.actorShadows&&quality.actorShadowDetail&&actorValid[source]&&actorCasterCount[source])||shadowVolumeTransitionActive(source));
            }
            fogVolume.grid(d,std::clamp(farZ,128.f,4000.f));
        }else fogVolume.valid=false;
        const bool volumeReuse=volumeFog&&fogVolume.valid&&fogVolume.cacheReady&&useHistory&&!volumeMoving&&volumeKey==fogVolumeKey;
        const char* volumeReason=volumeReuse?"reuse":!fogVolume.valid?"cold":!useHistory?"history-reset":volumeMoving?"moving":volumeKey!=fogVolumeKey?"inputs":"settle";
        return {volumeFog,importedNoise,volumeReuse,volumeMoving,volumeKey,volumeReason,{volumeVariant[0],volumeVariant[1]}};
    }

    // Draw the base medium and clouds, then restore the samplers and constant
    // ranges borrowed from lighting before screen history reads them.
    template<class BindSource>
    bool drawAtmosphericFog(const AtmosphericFogInput& in,const FogVolumePass& volume,const FogBoundSource& sourceState,
                            NorthlightGpuProfile* profile,const BindSource& bindSource){
        const UINT w=in.w,h=in.h;
        const float farZ=in.farZ;
        const int firstSource=in.firstSource;
        const auto& sourceActive=in.sourceActive;
        const bool geometryReady=in.geometryReady,anyShadowTransition=in.anyShadowTransition,useHistory=in.useHistory;
        const auto& c=in.constants;
        const auto textures=in.textures;
        const auto& frameMap=in.frameMap;
        const bool& fogActorCurrent=sourceState.actor;
        const bool& fogActorTiledCurrent=sourceState.tiledActor;
        const bool& fogAmbientOnlyCurrent=sourceState.ambientOnly;
        const auto& [volumeFog,importedNoise,volumeReuse,volumeMoving,volumeKey,volumeReason,volumeVariant]=volume;
        fogScreen.beginRawWrite();
        regionalFog.bindVolumeInputs(d);
        NorthlightAtmosphereCommands commands(d);
        float fogAmbient[4]={0,0,0,c[18][3]};NorthlightSunHue::coolAmbient(c[18],glowHueFrame,sourceWeights[0],fogAmbient);
        commands.begin(fogScreen.raw().surface,regionalFogGpu.base(),fogAmbient);
        bool first=true;
        int lastFogSource=firstSource;bool lastFogFirst=true;
        if(volumeFog&&!volumeReuse){
            if(!fogVolume.target(d,fogVolume.raw()))return false;
        }
        float importedConstants[38][4];
        if(!volumeReuse&&in.imported.valid){
            if(!volumeFog)foreverFog.createRaymarchVariants(d);
            NorthlightForever::constants(in.imported,importedConstants,std::clamp(farZ,128.f,4000.f),0.f);
            if(regionalFog.field())importedConstants[0][3]=NorthlightSpellFog::fieldAt(*regionalFog.field(),context.camera[0],context.camera[1]).ground;
            if(!quality.godrays)std::fill_n(importedConstants[9],3,0.f);
            if(!importedNoise)std::fill_n(importedConstants[34],3,0.f);
        }
        if(!volumeReuse)for(int source=0;source<2;++source){if((!quality.godrays||!sourceActive[source])&&source!=firstSource)continue;
            bindSource(source,first);const bool fading=geometryReady&&shadowVolumeTransitionActive(source)&&!fogAmbientOnlyCurrent;
            if(fading)bindShadowTransition(source);
            auto* volumeShader=fading?shadowTransition.fogShader(fogActorCurrent,fogActorTiledCurrent):fogVolumeShader(fogAmbientOnlyCurrent,fogActorCurrent,fogActorTiledCurrent);
            commands.sourceMode(first);
            const bool floorEligible=!in.imported.valid&&moonFloorSourceCurrent(source,geometryReady,!fogAmbientOnlyCurrent,frameMap);
            bool volumeNoise=false;
            if(in.imported.valid){
                importedConstants[11][3]=quality.godrays?sourceWeights[source]*underwaterFade.scale():0.f;
                const bool noise=importedNoise;
                const auto selected=volumeFog
                    ?NorthlightWorldForeverFogShaders::Selection{fogVolume.inject[volumeVariant[source]],fogActorCurrent,!noise,false}
                    :foreverFog.select(fogActorCurrent,fogActorTiledCurrent,fading,noise);
                volumeShader=selected.shader;
                if(profileSampled())logf("FOG imported worldFrame=%u source=%d noise=%u noNoiseShader=%u actor=%u transition=%u tiledShader=%u cameraAboveGround=%.3f first=%.3f shadow=%.3f,%.3f,%.3f",frames,source,unsigned(noise),unsigned(selected.quiet),unsigned(selected.actor),unsigned(fading),unsigned(selected.tiled),context.camera[2]-importedConstants[0][3],importedConstants[34][3],importedConstants[9][0],importedConstants[9][1],importedConstants[9][2]);
                commands.bindImportedVolume(noise?regionalFogGpu.noise():nullptr,importedConstants[0]);
                volumeNoise=noise;
            }
            if(volumeFog){if(!fogVolume.injection(d,volumeShader,std::clamp(farZ,128.f,4000.f),volumeNoise,first))return false;}
            else if(!check(drawFogVolume(w/2,h/2,volumeShader,floorEligible,fogActorCurrent,fogActorTiledCurrent,fading,textures[0],c[65]),"celestial volumetric raymarch"))return false;
            lastFogSource=source;lastFogFirst=first;first=false;
        }
        if(volumeFog){
            // The atlas temporal resolve temporarily borrows the near shadow on s2.
            commands.beforeAtlasResolve(c[53]);
            if(!volumeReuse){
                // Once a moving volume stops, compute one exact frame before
                // caching it; freezing a blended frame would freeze its lag.
                const bool history=useHistory&&fogVolumeField==regionalFog.field()&&(volumeMoving||volumeKey!=fogVolumeKey);
                if(!fogVolume.integrate(d,std::clamp(farZ,128.f,4000.f),c[1],history))return false;
                fogVolumeKey=volumeKey;fogVolumeField=regionalFog.field();
            }else ++fogVolume.reuses;
            if(!fogVolume.composite(d,fogScreen.raw().surface,w/2,h/2))return false;
            if(profileSampled())logf("FOG_VOLUME frame=%u grid=%ux%ux64 cached=%u updates=%u reuses=%u reason=%s",frames,fogVolume.cellWidth,fogVolume.cellHeight,unsigned(volumeReuse),fogVolume.updates,fogVolume.reuses,volumeReason);
            commands.afterAtlasResolve(textures[0],textures[2]);
        }
        if(profile)profile->mark("FogMarch");
        // 0.3.199 (fog clouds): the moving fog banks, a second half-resolution raymarch into the same fog buffer: rgb added (ONE), the fog already
        // there attenuated by the cloud transmittance (alpha, SRCALPHA); alpha out is the old alpha times it. The first source lights it, ambient once
        // (c27.x=1 in setSource). Afterwards every state the later passes read is put back exactly as the loop above left it (the last source's
        // setSource again, blend, write mask, s14 and the alpha-blend operands); inactive frames skip all of this.
        if(in.clouds&&c[21][0]>=.5f){
            /* 0.3.199 (optimisation): the separate-alpha operands are not saved: the effects run inside the game-state save (SavedState) and with
               SEPARATEALPHABLENDENABLE back at FALSE below they are ignored. The common sun-only frame drew the source loop once with firstSource
               and first=true: setSource then already holds this pass's state, so it is neither set here nor put back after. */
            const bool sameSource=lastFogSource==firstSource&&lastFogFirst;
            if(!sameSource)bindSource(firstSource,true);
            float cloudColour[4],cloudFog[4]={c[25][0],c[25][1],c[25][2],1};
            const float fw=c[25][3];NorthlightFogClouds::colour(c[26],fw>0,c[31][3],cloudColour);
            if(fw>0&&fw<1){float off[4];NorthlightFogClouds::colour(c[26],false,c[31][3],off);for(int i=0;i<4;++i)cloudColour[i]=off[i]+(cloudColour[i]-off[i])*fw;}
            commands.beginClouds(regionalFogGpu.noise(),cloudFog,cloudColour);
            const bool cloudFade=quality.godrays&&geometryReady&&shadowVolumeTransitionActive(firstSource);if(cloudFade)bindShadowTransition(firstSource);
            auto* cloudShader=!quality.godrays?regionalFogGpu.ambientClouds():cloudFade?shadowTransition.cloudsShader(fogActorCurrent,fogActorTiledCurrent):
                (fogActorCurrent?(fogActorTiledCurrent?actorMaps.cloudsTiled():regionalFogGpu.actorClouds()):regionalFogGpu.clouds());
            const bool cloudsDrawn=check(commands.drawClouds(cloudShader,w/2,h/2),"fog clouds raymarch");
            if(!sameSource)bindSource(lastFogSource,lastFogFirst);
            commands.endClouds(c[25],lastFogFirst,fogVolumeShader(fogAmbientOnlyCurrent,fogActorCurrent,fogActorTiledCurrent));
            if(!cloudsDrawn)return false;
            if(profile)profile->mark("FogClouds");
        }
        commands.restoreLighting(textures,screenLighting.baseline().texture,anyShadowTransition,c[17]);
        return true;
    }

    template<class BindSource,class VariantFor,class VolumeDirection>
    bool renderAtmosphericFog(const AtmosphericFogInput& in,const FogBoundSource& sourceState,
                              const NorthlightLocalLightSelection::Selection& localLights,
                              const NorthlightLocalLightScissor::Rect* fogRects,
                              NorthlightGpuProfile* profile,NorthlightLightingCapture::Capture* lightingCapture,
                              const BindSource& bindSource,const VariantFor& variantFor,const VolumeDirection& volumeDirection,
                              AtmosphericFogResult& result){
        const UINT w=in.w,h=in.h;
        const int debug=in.debug;
        const bool useHistory=in.useHistory;
        const auto& c=in.constants;
        // Every view ray ends within c21.w. Unknown bounds fail open; local
        // lamps have their own longer range, so retain their path independently.
        if(effects.fog&&!quality.godrays&&!regionalFogGpu.ensureAmbientClouds(d))return false;
        const bool regionalFogActive=c[21][0]>=.5f&&(in.imported.valid||!(c[65][2]>0&&
            (c[15][2]-c[21][3]>c[65][1]||c[15][2]+c[21][3]<c[65][0])));
        const bool localFogActive=localDirectCount&&debug==0&&localLights.hasFog(localDirectCount);
        const bool fogActive=effects.fog&&(regionalFogActive||localFogActive);
        const auto volume=prepareFogVolume(in,regionalFogActive,fogActive,variantFor,volumeDirection);
        const bool volumeFog=volume.volume;
        const bool fogClearSkipped=!fogActive&&fogScreen.rawNeutral();
        const auto checkFog=[&](HRESULT hr,const char* name){return check(hr,name);};
        if(lightingCapture&&localDirectCount&&!localFogActive&&debug==0)lightingCapture->skipped("LocalFog",0,localDirectCount,"zero-extinction");
        if(fogActive){
            if(!drawAtmosphericFog(in,volume,sourceState,profile,bindSource))return false;
        }else if(!fogScreen.clearNeutral(d,w/2,h/2,checkFog))return false;
        NorthlightAtmosphereCommands commands(d);
        bucket(NorthlightEffectsBuckets::Fog);commands.sourceMode(true);
        NorthlightFogScreen::HistoryResult fogHistory;
        const NorthlightFogScreen::HistoryInput fogHistoryInput{
            w/2,h/2,effects.fog&&debug==0,fogActive,volumeFog,useHistory,in.clouds,c[53],history.pending().distance.texture
        };
        if(!fogScreen.resolveHistory(d,fogHistoryInput,fogHistory,checkFog))return false;
        const bool fogResolved=fogHistory.resolved;
        const bool fogHistoryClearSkipped=fogHistory.neutralClearSkipped;
        if(fogResolved&&profile)profile->mark("FogTemporal");
        // TemporalLight already rotated the distance slot.
        if(!fogScreen.blur(d,w/2,h/2,in.distance?history.produced().distance.texture:nullptr,
                           fogActive,fogResolved,checkFog))return false;
        if(fogActive&&profile)profile->mark("FogBlur");
        const auto& finalFog=fogScreen.final(fogResolved);
        if(effects.fog&&localFogActive&&
           !renderLocalFog(w,h,finalFog.surface,localLights,fogRects,c[21],lightingCapture))return false;
        commands.sourceMode(true);
        if(profile)profile->mark("Fog");
        result={finalFog,fogActive,fogResolved,fogClearSkipped,fogHistoryClearSkipped};
        return true;
    }
