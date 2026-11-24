// Optional screen-space filtering of the existing directional maps.
    NorthlightWorldDirectionalVisibility directionalVisibility;
    void releaseSoftShadows(){
        resetShadowTransitions();
        directionalVisibility.reset();
    }
    bool softShadowResources(UINT w,UINT h){
        return directionalVisibility.prepare(d,w,h,[&]{
            const auto budget=NorthlightGeometryMemory::defaultTargetBudget();
            return NorthlightGeometryMemory::admits(geometryAdmission(budget),budget);
        },[&]{resetShadowTransitions();},[](HRESULT hr){
            logf("WORLD PCSS unavailable hr=%08lx; ordinary shadows retained",(unsigned long)hr);
        });
    }

    struct DirectionalVisibility {
        bool actorTilesWide[2]={};
        bool softResolved=false,sharedResolved=false;
    };

    // Normals and frame samplers are bound. Fog reuses the actor tiles after
    // lighting; the shared resolve may fail and require a separate actor mask.
    template<class BindSource>
    bool resolveDirectionalVisibility(UINT w,UINT h,int firstSource,const bool (&sourceActive)[2],
                                      bool mapsReady,bool anyShadowTransition,const float* viewDepthRow,
                                      float maxFogDistance,IDirect3DTexture9* const* textures,
                                      unsigned rendererFrame,NorthlightGpuProfile* profile,
                                      const BindSource& bindSource,DirectionalVisibility& result){
        bool actorPass[2]={},anyActorPass=false;
        for(int source=0;source<2;++source){
            actorPass[source]=!((sourceWeights[source]<=0&&source!=firstSource)||!mapsReady||!sourceActive[source]||!actorValid[source]||!actorMaps.ready(source));
            anyActorPass=anyActorPass||actorPass[source];
        }
        const bool sharedResolveRequested=anyActorPass&&quality.softShadows&&!anyShadowTransition&&
            softShadowResources(w,h)&&directionalVisibility.prepareShared(d);
        const auto checkActor=[&](HRESULT hr,const char* name){return check(hr,name);};
        if(anyActorPass){
            if(!actorMaps.buildTiles(d,actorPass,quality.godrays&&effects.fog,actorMatrices,viewDepthRow,maxFogDistance,
                                       result.actorTilesWide,checkActor))return false;
            if(profile)profile->mark("ActorTiles");
        }
        const auto resolveActorVisibility=[&]{
            return !anyActorPass||actorMaps.resolveVisibility(d,w/2,h/2,actorPass,quality.softShadows,actorMatrices,
                textures[10],checkActor,[&](int source){bindSource(source);});
        };
        if(!sharedResolveRequested&&!resolveActorVisibility())return false;
        if(profile)profile->mark("ActorShadow");bucket(NorthlightEffectsBuckets::ActorShadow);
        bool softResolved=false;
        if((quality.softShadows||anyShadowTransition)&&mapsReady&&(sourceActive[0]||sourceActive[1])&&softShadowResources(w,h)){
            NorthlightWorldDirectionalVisibility::Resolve input{w/2,h/2,sharedResolveRequested,anyShadowTransition,
                NorthlightWorldActorShadows::Info,shadowTransition.directionalShader(),shadowTransition.directionalShader(true),{}};
            for(int source=0;source<2;++source){
                const bool enabled=sourceActive[source]&&!(sourceWeights[source]<=0&&source!=firstSource);
                input.sources[source]={enabled,actorPass[source],shadowTransitionActive(source),actorMatrices[source],
                    actorPass[source]?actorMaps.depth(source).texture:screenFallbacks.white(),
                    actorPass[source]?actorMaps.tiles(source).texture:screenFallbacks.white()};
            }
            softResolved=directionalVisibility.resolve(d,input,textures,bindSource,
                [&](int source,bool previous){bindShadowTransition(source,previous);});
            if(profile)profile->mark("ShadowFilter");
        }
        const bool sharedResolved=sharedResolveRequested&&softResolved;
        if(sharedResolveRequested){
            directionalVisibility.restoreReceiverMetadata(d,textures[10]);
            // A failed optional shared draw must restore the actor mask before
            // the ordinary lighting fallback: those casters left the near map.
            if(!softResolved&&!resolveActorVisibility())return false;
        }
        if(profileSampled())logf("WORLD_SHADOW_RESOLVE rendererFrame=%u shared=%u requested=%u actors=%u transitions=%u visibilityBits=%u",rendererFrame,unsigned(sharedResolved),unsigned(sharedResolveRequested),unsigned(actorPass[0])+unsigned(actorPass[1]),unsigned(anyShadowTransition),sharedResolved?16u:8u);
        if(anyShadowTransition&&!softResolved)releaseShadowTransitions();
        actorMaps.bindLightingVisibility(d,!sharedResolved&&anyActorPass,screenFallbacks.white());
        result.softResolved=softResolved;
        result.sharedResolved=sharedResolved;
        return true;
    }
