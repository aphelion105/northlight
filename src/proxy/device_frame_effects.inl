// Effect scheduling at the native scene/UI boundary.
    void renderEffects() {
        if(!enabled&&worldDebug==0&&!diagnostics())return;
        if(hdr.active&&(!hdrAtUiBoundary||!hdrBackTarget()))return;
        nameMaskRunEnd(); /* 0.3.212 (task 32): the effect passes and SavedState meet the game's stencil states */
        CpuScope cpu(sampled()?&cpuEffects:nullptr);
        using NorthlightEffectsBuckets::Bucket;
        // 0.3.154: sceneMs end = the last entry before effects apply; the read is billed to cpuEffects.
        if((gateFrame||NorthlightRenderThreadProbe::profiling())&&!applied){QueryPerformanceCounter(&gateSceneEnd);stallSceneIdle=streamStats?NorthlightStream::get(streamStats->replayIdleNs):0;gateSceneDraws=drawCalls;gateSceneReads=CpuScope::reads;}
        if(!applied&&worldDebug&&terrain&&world&&(!enabled||failed||!projectionValid)){
            ExtensionDevice::RawScope raw(*ext);D3DSURFACE_DESC desc{};IDirect3DSurface9* target=nullptr;
            if(fullViewport(desc,nullptr,true)&&SUCCEEDED(ext->GetRenderTarget(0,&target))&&target){applied=world->emptyDiagnosticView(target,desc.Width,desc.Height,worldDebug);target->Release();}
            return;
        }
        if(!applied&&enabled&&!failed&&!projectionValid&&skySeen){D3DSURFACE_DESC desc{};if(fullViewport(desc,nullptr,true))recoverSkyContext(desc);}
        if (applied || !enabled || failed || !projectionValid) return;
        // Keep rare slow frames attributable too. No-op calls after composition
        // leave the valid first split untouched; raw draw counting stays sampled.
        effectsBucketed=NorthlightRenderThreadProbe::profiling();
        {static const unsigned drawMethods[4]={rawMethodIndex("DrawIndexedPrimitive"),rawMethodIndex("DrawPrimitive"),rawMethodIndex("DrawIndexedPrimitiveUP"),rawMethodIndex("DrawPrimitiveUP")};
         const std::uint32_t* counters[4]={};if(mirrorState.rawCounting)for(unsigned i=0;i<4;++i)counters[i]=drawMethods[i]<ExtensionDevice::RawMethods?&mirrorState.rawMethodCalls[drawMethods[i]]:nullptr;
         effectsBuckets.begin(effectsBucketed,&qpcNow,counters,4);}
        struct BucketsEnd {NorthlightEffectsBuckets::Frame& f;~BucketsEnd(){f.end();}} bucketsEnd{effectsBuckets};
        // task 45: the always-on effects span for the CPU budget (applying frames only; every return path below ends it).
        struct EffectsSpan { std::int64_t& out; std::int64_t start; ~EffectsSpan(){out=qpcNow()-start;} } effectsSpan{cpuEffectsQpc,qpcNow()};
        // RenderProfile: every applying frame (two clock reads), the FRAME cost effects series.
        CpuScope frameCostScope(!sampled()&&NorthlightRenderThreadProbe::profiling()?&cpuEffects:nullptr);
        // Flush sampled light/caster diagnostics together, inside both CPU spans.
        LogFlushBatch logBatch;
        // The scope outlives every SavedState, including nested depth passes.
        // Game captures remain outside; the device gate stays held throughout.
        rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */
        ExtensionDevice::RawScope rawEffects(*ext);
        // Every early return must leave a blank diagnostic view, including
        // missing depth/context and device-call failures. Never clear an
        // intermediate effect target: retain the original frame target here.
        struct DebugViewEnd {
            Device& owner;IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC desc{};
            explicit DebugViewEnd(Device& device):owner(device){
                if(owner.worldDebug&&owner.world&&SUCCEEDED(owner.ext->GetRenderTarget(0,&target))&&target)
                    if(FAILED(target->GetDesc(&desc))){target->Release();target=nullptr;}
            }
            ~DebugViewEnd(){if(target){if(!owner.diagnosticWorld)owner.world->emptyDiagnosticView(target,desc.Width,desc.Height,owner.worldDebug);target->Release();}}
        } debugViewEnd(*this);
        D3DSURFACE_DESC desc;
        const bool sceneSplit=gpuSceneProfile&&!gpuSceneSplit;
        if(sceneSplit)gpuSceneProfile->mark("GameScene");
        if (!fullViewport(desc) || !resources(desc.Width, desc.Height, desc.Format) || !resolveDepth()) return;
        if(sceneSplit)gpuSceneProfile->mark("DepthResolve");
        SavedState saved(ext,&stateBlocks);
        if (!saved.ok) return;
        const bool normalsView=worldDebug==NormalsView;
        const bool ssaoView=worldDebug==SsaoView;
        IDirect3DTexture9* coverage=effectCoverage.texture(ext,saved.targets[0]);
        // Include deferred material/model captures; in-scene MRT work is part
        // of GameScene and cannot be attributed by this interval alone.
        surfaceTimerOpen=world&&world->gpuBudgetMs()&&gpuTimer->begin(frame);
        IDirect3DSurface9* materialDepth=nullptr;
        if(autoBump&&autoBump->finishCaptures()){
            materialDepth=autoBump->completedPrivateDepth();
            if(!autoBump->finishFusedM2(worldDepth,depthTex,nearZ,farZ,worldMinDepth,worldMaxDepth))return;
        }
        if(sceneSplit)gpuSceneProfile->mark("MaterialCapture");
        if(modelNormals){const NorthlightModelNormals::DepthSeed seed{depthTex,nearZ,farZ,worldMinDepth,worldMaxDepth};
            const bool timed=sceneSplit&&NorthlightRenderThreadProbe::sampleFrame(frame)&&cpuFrequency.QuadPart>0;
            const auto started=timed?qpcNow():0;
            // After this boundary, composition either completes or disables
            // further capture for the frame. Material depth is no longer read.
            modelNormals->finishCaptures(true,&seed,materialDepth);
            if(timed)modelNormals->reportReplay(frame,1000.0*double(qpcNow()-started)/double(cpuFrequency.QuadPart),
                [](const char* format,auto... args){logf(format,args...);});
        }
        if(sceneSplit){gpuSceneProfile->mark("ModelNormals");gpuSceneSplit=true;}
        // 0.3.200 (gpu budget): the frame timer brackets every effect pass (closed on every return path by the guard, after the profile's end).
        if(diagnostics())gpuProfile->beginFrame(frame,NorthlightRenderThreadProbe::sampleFrame(frame)); // otherwise no queries; mark/endFrame are no-ops
        effectsBuckets.mark(Bucket::Setup);
        // spellReplay closes profiling after both deferred spells and precipitation.
        const auto* lightAudit=world&&debugMode==0&&!normalsView&&!ssaoView?frameParity.lightHistoryAudit(frame):nullptr;
        std::optional<NorthlightLightingCapture::Capture> atmosphereAudit;
        if(lightAudit)atmosphereAudit.emplace(ext,*lightAudit,frame,frame,frame,width,height,"frame-parity-atmosphere");
        IDirect3DTexture9* rainMaskTex=world&&debugMode==0&&!normalsView&&!ssaoView?rainMaskPrepare(atmosphereAudit?&*atmosphereAudit:nullptr):nullptr;
        IDirect3DTexture9* waterMask=nullptr;
        if(water&&water->maskRequested()&&debugMode==0&&!normalsView){
            if(atmosphereAudit){
                bool recorded=false;
                const bool resolved=water->resolveMask([&](const char* entry,unsigned mask,HRESULT hr,IDirect3DSurface9* source){
                    atmosphereAudit->bindings(entry,mask,hr,source);recorded=true;
                });
                if(!recorded)atmosphereAudit->bindingSkip("WaterMaskResolve",resolved?"no-resolve-draw":"resolve-failed");
            }else water->resolveMask();
            waterMask=water->maskTextureForDepth(worldMinDepth,worldMaxDepth);
        }else if(atmosphereAudit)atmosphereAudit->bindingSkip("WaterMaskResolve","mask-not-requested");
        IDirect3DTexture9* hdrDecodeDepth=depthTex;
        bool hdrWaterShaded=false,hdrWaterFinalized=false;
#ifdef NORTHLIGHT_WATER
        if(cooperativeWaterEnabled&&debugMode==0&&worldDebug==0){
#ifndef _WIN64
            prepareCooperativeWater(desc,saved.viewport,false);
#endif
            if(cooperativeWater.Current()){
                auto* liquidMask=cooperativeWater.Liquid()?tideglassCoverage(waterMask):nullptr;
                const bool keepRain=liquidMask&&snapshotTideglassRain(saved.targets[0],rainMaskTex);
                if(liquidMask){
                    cooperativeWater.Composite(depthTex,liquidMask,worldDepth,nearZ,farZ,worldMinDepth,worldMaxDepth,false);
                    WaterHostSurfaceStatus status{};
                    const bool shaded=cooperativeWater.SurfaceStatus(status)&&status.outcome==WaterHostSurfaceShaded;
                    if(shaded&&keepRain)restoreTideglassRain(saved.targets[0],rainMaskTex);
                    if(shaded&&hdr.active&&cooperativeWater.SurfaceDepth()){
                        hdrWaterShaded=true;
                        hdrDecodeDepth=cooperativeWater.SurfaceDepth();
                    }
                }
                if(cooperativeWater.HasSurface()&&cooperativeWater.CompositeSurfaces(depthTex,worldDepth)){
                    if(!resolveDepthInto(depthTex,false)||
                       !cooperativeWater.RefreshLiquidDepth(depthTex,liquidMask,nearZ,farZ,worldMinDepth,worldMaxDepth))return;
                    if(auto* effectsMask=cooperativeWater.EffectsMask(depthTex,worldDepth,waterMask))waterMask=effectsMask;
                    if(hdr.active)hdrDecodeDepth=cooperativeWater.SurfaceDepth();
                }
                // Tideglass takes encoded game colour. HDR decodes that result once.
                if(hdr.active)
                    hdrWaterFinalized=cooperativeWater.WorldDone(worldDepth,saved.targets[0],depthTex,&saved.viewport);
#ifdef _WIN64
                reportTideglassSurface();
#endif
            }
        }
#endif
        gpuProfile->mark("PreCelestial"); /* task 45: the rain and water mask preparation (and GPU work still pending from the game's scene) end here, so CelestialDiscs bills the discs and the terrain mask alone */
        // discs and glare, then the wrap-ring visibility in the same group of
        // 1x1 passes before the scene copy; the veil follows water (below).
        const bool celestial=celestialDiscs&&celestialEnabled()&&!skyLayers.native(frame)&&debugMode==0&&worldDebug==0;float sunElevation=0;
        auto renderCelestial=[&](IDirect3DSurface9* target){
            NorthlightCelestial::Context sky;float inverseView[16],projection[3];
            if(world->celestialContext(sky,inverseView,projection)){sunElevation=sky.sun.direction[2]-sky.horizonSine; /* over the camera's far edge, as the composite's haze band */
                celestialDiscs->setWeatherGain(world->weatherEffects().discGain()*world->underwaterScale()); /* 0.3.198 (rain): the discs, glare and veil follow the weather */
                celestialDiscs->setSkyLayers(skyLayers.texture(frame));
                celestialDiscs->render(target,depthTex,waterMask,width,height,world->legacyFogParameters(),worldMinDepth,worldMaxDepth,nearZ,farZ,projection,inverseView,sky,celestialGlowHue(),replacesWorldSkyBackground());}
        };
        if(celestial&&!hdr.active)renderCelestial(saved.targets[0]);
        gpuProfile->mark("CelestialDiscs");
        if(celestial&&!hdr.active){celestialDiscs->renderRing();gpuProfile->mark("CelestialRing");}
        effectsBuckets.mark(Bucket::Celestial); /* discs, glare, the terrain mask prepare, the ring */
        if (error(ext->StretchRect(saved.targets[0], nullptr, sceneSurface, nullptr, D3DTEXF_NONE), "copy scene")) return;
        if(atmosphereAudit)atmosphereAudit->image("scene-input-color",sceneSurface);
        effectState();
        if(normalsView){
            if(world)world->setModelNormals(modelNormals?modelNormals->texture():nullptr);
            diagnosticWorld=world&&world->render(saved.targets[0],depthTex,width,height,sceneFormat,nearZ,farZ,
                    worldMinDepth,worldMaxDepth,worldDebug,gpuProfile.get(),nullptr,scene);
            gpuProfile->mark("WorldNormalsView");
            applied=true;
            return;
        }
        const bool linearHdr=hdr.active&&debugMode==0&&worldDebug==0;
        const bool fold=world&&debugMode==0&&world->screenReady();
        bool neutralAOPending=false;
        auto effectError=[this](HRESULT hr,const char* stage){return error(hr,stage);};
        auto clearNeutralAO=[&]{return NorthlightContactAO::clearNeutral(ext,gpuProfile.get(),aoSurface,neutralAOPending,effectError);};
        float constants[]={1.f/width,1.f/height,nearZ,farZ,scaleX,scaleY,aoStrength,(menuScene||(world&&world->screenReady())?0.f:.12f),.08f,aoRadius,float(debugMode),0,worldMinDepth,1.f/(worldMaxDepth-worldMinDepth),worldMaxDepth,0};

        if(ssaoView){
            if(constants[6]<=0.f)constants[6]=.6f;
            constants[7]=constants[8]=0.f;
            constants[10]=1.f; // Composite's bilateral AO visualization.
        }
        float waterFlags[]={waterMask?1.f:0.f,0,0,0};
        auto bindLegacyFog=[&]{
            auto nativeFog=world?world->legacyFogConstants():NorthlightLegacyFog::Constants{};
            nativeFog.parameters[0]*=fogForward; // Position() reconstructs positive view distance.
            ext->SetPixelShaderConstantF(5,nativeFog.parameters,1);ext->SetPixelShaderConstantF(6,nativeFog.color,1);
        };
        auto bindEffects=[&](IDirect3DTexture9* ambient){
            ext->SetPixelShaderConstantF(4,waterFlags,1);ext->SetTexture(3,waterMask);
            ext->SetPixelShaderConstantF(0,constants,4);
            bindLegacyFog();
            ext->SetTexture(0,scene); ext->SetTexture(1,depthTex); ext->SetTexture(2,ambient);
        };
        bindEffects(nullptr);
        if(!NorthlightContactAO::render<CpuScope>(ext,gpuProfile.get(),effectsBuckets,
            {scene,ao,aoRaw,waterMask,aoSurface,aoRawSurface},
            {aoPS,aoOnlyPS,ssaoView?aoPS:aoContactBloomPS,contactBloomPS,aoBlurPS,aoBlurNoWaterPS},
            {width,height,ssaoView?0u:aoMethod,ssaoView||contactAO,linearHdr,fold,cpuFrequency.QuadPart>0,constants[7],constants[8]},
            {neutralAOPending,aoActorUsed,diagnosticAoRaw,diagnosticAo,cpuAOBlur},effectError,
            [this](unsigned w,unsigned h,bool viewport){return quad(w,h,viewport);},
            [this,ssaoView](bool alphaOnly)->NorthlightContactAO::Actor{
                if(ssaoView)return {};
                auto* texture=aoActorOn?aoActorMask->textureForDepth(worldDepth):nullptr;
                if(texture&&aoActorShaders(alphaOnly))return {texture,aoActorPreparePS,alphaOnly?aoActorAlphaPS:aoActorPS};
                return {};
            },[&]{
                NorthlightEffectCoverage::Capture::bind(ext,ssaoView?nullptr:effectCoverage.aoTexture(ext,saved.targets[0]));
                bindLegacyFog();
            }))return;
        // 0.3.174 FOLD: with a ready world and no effect debug view, WorldComposite applies the
        // AO and bloom (AOContactBloom: bloom rgb, AO alpha) to the scene copy itself, so the
        // full-resolution composite and the world's colour copy are skipped. LEGACY (any other
        // frame) keeps the composite before the world, exactly as before.
        /* 0.3.212 (task 32): this frame's name marks: the composite quads draw with the world depth-stencil bound and reject marked pixels; no marks, or the feature off: exactly the old calls */
        const bool maskOn=nameMask.armed()&&nameMask.hasMarks()&&maskStencil&&worldDepth&&worldDebug==0;nameMask.composited=maskOn;
        IDirect3DSurface9* outputTarget=linearHdr?hdrPresentBack:saved.targets[0];
        if(linearHdr)hdrFrameSettings=effectiveHdrSettings();
        const bool inlineHdrBase=linearHdr&&!hdrWaterShaded&&!hdrWaterFinalized&&fold&&!maskOn&&!celestial&&hdrFrameSettings.legacyExpansion==0&&
            (sceneFormat==D3DFMT_A8R8G8B8||sceneFormat==D3DFMT_X8R8G8B8);
        if(linearHdr){
            struct HdrSkyAudit{NorthlightLightingCapture::Capture* capture;bool drew=false;} skyAudit{atmosphereAudit?&*atmosphereAudit:nullptr};
            const auto recordSky=+[](void* context,HRESULT draw,IDirect3DSurface9* source){
                auto& audit=*static_cast<HdrSkyAudit*>(context);
                audit.capture->bindings("HdrDecodeWorldSky",3u,draw,source);audit.drew=true;
            };
            if(error(hdrFrame.prepareLegacy(ext,scene,hdrDecodeDepth,hdrSkyValid&&!hdrWaterFinalized?hdrSkyTarget():nullptr,width,height,worldMinDepth,worldMaxDepth,hdrFrameSettings,&stateBlocks,!inlineHdrBase,
                   atmosphereAudit?recordSky:nullptr,&skyAudit),"HDR legacy decode"))return;
            if(atmosphereAudit&&!skyAudit.drew)atmosphereAudit->bindingSkip("HdrDecodeWorldSky",hdrSkyValid?"no-sky-decode-draw":"no-captured-sky");
            gpuProfile->mark("HDRBase");
            if(celestial){renderCelestial(hdrFrame.surface());celestialDiscs->renderRing();gpuProfile->mark("HDRCelestial");}
            hdrLegacyPrepared=true;
            if(maskOn&&error(hdrFrame.copyBase(ext,outputTarget,&stateBlocks),"HDR base"))return;
        }else if(atmosphereAudit)atmosphereAudit->bindingSkip("HdrDecodeWorldSky","sdr-frame");
        auto legacyComposite=[&]{
            if (error(ext->SetRenderTarget(0,outputTarget),"composition render target")) return false;
            if(linearHdr){if(error(hdrFrame.bindLegacyComposite(ext,scene,depthTex,ao,worldMinDepth,worldMaxDepth,nearZ,farZ),"HDR fallback composite"))return false;}
            else{ext->SetTexture(2,ao); ext->SetPixelShader(compositePS);}
            NorthlightEffectCoverage::Capture::bind(ext,coverage,effectCoverage.hasEmission());
            {NameMaskCompositeBind mask(ext,maskOn?worldDepth:nullptr); /* 0.3.212 (task 32): the same bind as WorldComposite */
             if (error(quad(width,height),"composition pass")) return false;}
            gpuProfile->mark("AOComposite");effectsBuckets.mark(Bucket::Composite);return true;
        };
        if(ssaoView){
            diagnosticWorld=legacyComposite();
            if(diagnosticWorld&&world)world->render(outputTarget,depthTex,width,height,sceneFormat,nearZ,farZ,
                    worldMinDepth,worldMaxDepth,worldDebug,gpuProfile.get(),waterMask);
            applied=true;
            return;
        }
        if(!fold&&!legacyComposite())return;
        if(world&&debugMode==0){
            auroraSources.end();
            traceWorld=0;
            world->setMaterialDetail(autoBump?autoBump->texture():nullptr,autoBump?autoBump->gain():0.f,autoBump?autoBump->distance():48.f,autoBump?autoBump->occlusionTexture():nullptr,autoBump?autoBump->occlusionDistance():8.f,autoBump&&autoBump->active()&&autoBump->hasCaptureResources(),autoBump&&autoBump->capturedFoliage());
            world->setModelNormals(modelNormals?modelNormals->texture():nullptr);
            world->setNameMask(maskOn?worldDepth:nullptr);
            world->setEffectCoverage(coverage,effectCoverage.hasEmission());
            world->setPrelitMask(prelitMarks&&maskStencil&&!nameMask.disabled&&worldDebug==0?worldDepth:nullptr);
            world->setAuthoredMask(authoredMarks&&maskStencil&&!nameMask.disabled&&worldDebug==0?worldDepth:nullptr,roomLevels);
            world->setUnlitMask(unlitMarks&&maskStencil&&!nameMask.disabled&&worldDebug==0?worldDepth:nullptr); /* 0.3.212 (task 32) */
            world->setRainMask(rainMaskTex); /* 0.3.202 (rain): RainMask for this frame's composite only (resolved and scrubbed at the top of renderEffects) */
            // Expanded HDR can omit the FP16 world/material intermediate on
            // verified DXVK 3.1.1. DXVK 2 regresses with MSAA; retain its guard.
            const bool hdrMaterialCombine=hdrFrameSettings.legacyExpansion==0||backendHdrExpandedMaterial;
            // This is the terminal world pass inside saved. Fallback and later
            // celestial/mask passes bind their own state; saved restores the game.
            const bool rendered=world->render(outputTarget,depthTex,width,height,sceneFormat,nearZ,farZ,worldMinDepth,worldMaxDepth,worldDebug,gpuProfile.get(),waterMask,fold?scene:nullptr,fold&&!neutralAOPending?ao:nullptr,frame,linearHdr?(inlineHdrBase&&!hdrSkyValid?scene:hdrFrame.texture()):nullptr,inlineHdrBase&&!hdrSkyValid,hdrMaterialCombine,inlineHdrBase&&hdrSkyValid,!contactAO,&saved,lightAudit);
            diagnosticWorld=rendered;
            if(world->diagnosticCaptureInLastRender()){
                gpuDiagnosticFrame=true;
                logf("WORLD GPU_DIAGNOSTIC_FRAME rendererFrame=%u device=%ld worldRendered=%u hdrSky=%u hdrInlineBase=%u excludeFromFps=1",frame,diagnosticId,unsigned(rendered),unsigned(hdrSkyValid),unsigned(inlineHdrBase));
            }
            if(!rendered){
                if(++worldSkippedFrames<=8||(worldSkippedFrames%120==0&&diagnostics()))
                    logf("WORLD skipped frame=%u tick=%lu context=%d ready=%d count=%u reason=%s",frame,(unsigned long)GetTickCount(),world->hasContext(),world->ready(),worldSkippedFrames,world->lastSkipReason());
                worldSkipLast=world->lastSkipReason();if(!worldSkipRun++){worldSkipStart=GetTickCount();worldSkipFirst=worldSkipLast;
                    if(worldSkipEpisodes<32||diagnostics())logf("WORLD skip episode begin reason=%s",worldSkipFirst);}
            }else if(traceWorld=1,(worldDrawSeen?0:(worldDrawSeen=true,worldDrawFrame=frame,0)),(frameMarkers?frameMarker(0,frame):void()),worldSkipRun){
                if(++worldSkipEpisodes<=32||diagnostics())logf("WORLD skip episode reason=%s last=%s frames=%u ms=%lu",worldSkipFirst,worldSkipLast,worldSkipRun,(unsigned long)(GetTickCount()-worldSkipStart));
                worldSkipRun=0;
            }
            // A folded frame the world did not composite (skipped or failed; it left the target
            // untouched): re-establish the effect state it changed, then the legacy composite.
            if(fold&&!world->composited){
                if(inlineHdrBase&&error(hdrFrame.prepareLegacy(ext,scene,depthTex,hdrSkyValid?hdrFrame.surface():nullptr,width,height,worldMinDepth,worldMaxDepth,hdrFrameSettings,&stateBlocks),"HDR fallback decode"))return;
                effectState();
                if(neutralAOPending&&!clearNeutralAO())return;
                bindEffects(ao);if(!legacyComposite())return;
            }
        }
        if(celestial){
            const auto& haze=world->horizonHazeConstants();
            // HDR bloom spreads the visible linear source. The old SDR veil
            // would add a second, amplified halo directly over geometry.
            if(!linearHdr)celestialDiscs->renderVeil(outputTarget,NorthlightCelestialGlow::hazeAtSun(haze.haze[3],haze.shape[2],sunElevation),world->skyTransmittance());
            gpuProfile->mark("CelestialVeil");effectsBuckets.mark(Bucket::Veil);
        }
#ifdef NORTHLIGHT_WATER
        if(!hdr.active&&cooperativeWater.Current()){
            cooperativeWater.WorldDone(worldDepth,outputTarget,depthTex,&saved.viewport);
#ifdef _WIN64
            reportTideglassSurface();
#endif
        }
#endif
        if(maskOn&&nameMask.debug)nameMaskDebugView(outputTarget); /* 0.3.212 (task 32): the last effect pass, the mask itself */
        hdrSceneReady=linearHdr;
        if(atmosphereAudit){
            if(linearHdr&&frameParity.finalAuditFrame(frame))
                atmosphereAudit->image("hdr-after-world",outputTarget);
            atmosphereAudit->finish(true);
        }
        applied = true; ++appliedFrames;
        if (appliedFrames==1) logf("FIRST EFFECT FRAME: near=%.5f far=%.2f scale=%.4f,%.4f depthRange=%.9g..%.9g (before UI)",nearZ,farZ,scaleX,scaleY,worldMinDepth,worldMaxDepth);
    }
