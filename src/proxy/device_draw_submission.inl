// Coordinates the single native submission and its capture and replay adapters.
    template<class Capture,class Draw> HRESULT drawHook(D3DPRIMITIVETYPE t,UINT count,Capture capture,Draw draw,bool* nativeSky=nullptr,bool* skyCaptured=nullptr,[[maybe_unused]] const NorthlightAutoDisplacement::Args* geometry=nullptr){
#ifdef NORTHLIGHT_WATER
        cooperativeSurfaceDraw=WaterHostLiquidUnknown;
        cooperativeDrawArgs=geometry;
#endif
        skyPlanet.clear();
        auroraDrawProven=false;auroraDrawScale={};auroraDrawIntensity=1;
        const HRESULT resumed=hdrResumeWorld();if(FAILED(resumed))return resumed;
        if(!enabled&&!failed&&!hdr.active&&worldDebug==0&&!diagnostics()){++drawCalls;return draw();}
        if(gpuScene&&!gpuScene->native.currentFrame(frame)){ExtensionDevice::RawScope raw(*ext);gpuScene->setFrame(frame);}
        if(autoBump)autoBump->beginDraw();
        bool claimed=false,rainBlend=false,mist=false,nativeRain=false;
        auto nativeDraw=[&]{if(autoBump)autoBump->beforeNativeDraw();return nativeEffectDraw(nativeRain,[&]{return aoActorNativeDraw(draw);});};
        bool linearSky=false;
        if(hdr.active)extensionWork("HDR sky preparation",[&]{linearSky=hdrSkyAdmission();});
        if(nativeSky)*nativeSky=linearSky;
        // 0.3.198 (rain): the whole per-draw cost of weather detection, before any other work and in both gate paths: one pointer comparison.
        if(weatherDetect.hot&&mirrorState.textureKnown[0]&&mirrorState.textures[0]==weatherDetect.hot){weatherSample.primitives+=count;++weatherSample.draws;nativeRain=weatherDetect.hotKind==NorthlightWeather::Kind::Rain;rainBlend=nativeRain&&world&&world->rainBlendSetting();} /* 0.3.199 (rain): counted after applied too (the rest of the rain); 0.3.202: no boundary here, the effects run at the UI again */
        else if(weatherDetect.mistArmed)mist=mistDraw(count); /* 0.3.199 (rain mist): a mist puff in rain is skipped; unarmed, one bool test */
        if(!skyBandSeen&&mirrorState.viewportKnown&&NorthlightWorldDrawDomain::classify(worldMinDepth,worldMaxDepth,mirrorState.viewport.MinZ,mirrorState.viewport.MaxZ)==NorthlightWorldDrawDomain::Band::Sky)skyBandSeen=true; /* Weather diagnostics: sky-band evidence, never an exposure or weather-state gate. */
        if(rainMrtBound&&!rainBlend)rainMrtUnbind(); /* 0.3.202 (rain mask MRT): every other draw (and all its capture/effect work) meets the game's targets only; dry frames one bool test */
        waterPending.valid=false; /* 0.3.214 (water MRT): set by this draw's captureWater only */
        if(water&&water->isMrtBound()&&(rainBlend||applied||!enabled||failed||!nextDrawIsWater()))rainMrtUnbind(); /* the water mask's target 1 stays bound across consecutive water draws only */
        NorthlightSkyComposition::Role skyRole=NorthlightSkyComposition::Role::Unknown;
        mirrorState.gate.noteFirst(mirrorState.gate.drawTid);
        dropTerrainShadowSwap();dropBlobFaint();
        const bool sky=count<=4&&drawGates.sky;
        if(!frameDrawGates){
            extensionWork("draw capture/effects",[&]{prepareDrawImpl(capture,count);composeSkyBackground();});
            {CpuScope hooks(sampledHookTimer());
                extensionWork("native sky claim",[&]{skyLayersBefore(t,count,skyRole);skyClaim(t,count,claimed);});
                if(!claimed&&blobFilterActive())extensionWork("blob shadow filter",[&]{blobFilter(count,claimed);});}
        }else{
            const char* stage="draw capture/effects";
            extensionWork(stage,[&]{
                prepareDrawImpl(capture,count);
                composeSkyBackground();
                CpuScope hooks(sampledHookTimer());
                stage="skybox cloud order";skyLayersBefore(t,count,skyRole);
                if(sky){stage="native sky claim";skyClaim(t,count,claimed);}
                if(!claimed&&drawGates.blob){stage="blob shadow filter";if(blobFilterActive())blobFilter(count,claimed);}
            });
        }
        if(skyRole==NorthlightSkyComposition::Role::Planet)
            extensionWork("skybox planet scale",[&]{prepareSkyPlanet();});
        if(skyRole==NorthlightSkyComposition::Role::Celestial){
            extensionWork("skybox celestial ownership",[&]{if(!claimed)claimed=celestialDiscs->claimSkyArtwork();});
            skyRole=NorthlightSkyComposition::Role::Unknown;
        }
        NorthlightParticleCapture::Prepared particle;
        const bool particleSkipped=claimed||mist||rainBlend||blobOriginal||shadowSwapOriginal;
        if(mayPrepareParticleDraw(t,count,particleSkipped))extensionWork("particle light capture",[&]{particle=prepareParticleDraw(particleSkipped);});
        if(spellOn&&spellGate()&&spellDraw(claimed||mist||blobOriginal||shadowSwapOriginal,rainBlend||mist)){
            submitParticleDraw(particle,true,true);return D3D_OK;
        }
        skyLayersReplay(skyRole,!claimed&&!mist,draw);
        auto submit=[&]{return waterMrtDraw(nativeDraw,[&]{return nameMaskDraw(rainBlend,claimed||mist,nativeDraw);});};
#ifdef NORTHLIGHT_WATER
        const HRESULT hr=cooperativeWaterEnabled
            ? cooperativeSurface(t,geometry?geometry->stride:0,particleSkipped,submit) : submit();
#else
        const HRESULT hr=submit();
#endif
        if(SUCCEEDED(hr))hdrReplaySky(linearSky,claimed||mist||(skyCaptured&&*skyCaptured),draw);
        if(FAILED(hr)&&skyRole!=NorthlightSkyComposition::Role::Unknown)skyLayers.invalidate();
        submitParticleDraw(particle,SUCCEEDED(hr)&&!claimed&&!mist&&!blobOriginal&&!shadowSwapOriginal,false);
        if((!frameDrawGates||sky)&&!claimed){CpuScope hooks(sampledHookTimer());extensionWork("native sky observation",[&]{skyObserve(hr,t,count);});}
        return hr;
    }
