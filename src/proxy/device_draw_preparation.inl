// Draw preparation runs inside the Device fault boundary.
    template<class Capture> void prepareDrawImpl(Capture capture,UINT count) {
        // Both FrameDrawGates paths enter here, including non-model/early exits.
        if(modelNormals)modelNormals->beginNativeDraw();
        if(!gpuFrameStarted){
            gpuFrameStarted=true;
            if(gpuSceneProfile&&diagnostics())gpuSceneProfile->beginFrame(frame,NorthlightRenderThreadProbe::sampleFrame(frame));
            if(materialProfile)materialProfile->beginFrame(frame,world&&NorthlightQuality::renderProfile(world->surfaceSettings())&&NorthlightRenderThreadProbe::sampleFrame(frame));
        }
        aoActorBeginFrame();aoActorDrawKnown=aoActorDrawSkin=aoActorDepthWrite=false;
        ++drawCalls;maskKind=0;nativeLocalDomain=false;if(nameRun)nameMaskRunBreak();traceUiDraw();
        if(spellOn){spellDomain=false;spellVs=nullptr;} /* task 34: this draw's reads for spellDrawImpl, valid only if this function gets that far */
        if(failed||!enabled)return;
        if(applied){
            const bool sample=sampled();
            if(sample){
                DWORD z=0,write=0;if(readDrawRenderState(D3DRS_ZENABLE,z)&&readDrawRenderState(D3DRS_ZWRITEENABLE,write)){if(z&&write)++postEffectDepthWrites;}else ++postEffectDepthStateFailures;
            }
            const bool hideNative=drawGates.terrainShadow&&world&&terrain&&!debugMode&&!worldDebug&&!world->nativeTerrainShadowsEnabled();
            if(sample||hideNative){
                IDirect3DVertexShader9* late=nullptr;const bool borrowed=ext->peekVertexShader(late); // 0.3.196 (task 12): identity lookup only
                if((borrowed||(SUCCEEDED(ext->GetVertexShader(&late))&&late))&&world){
                    if(hideNative)planTerrainShadowSwap(late);
                    if(sample){
                    const VsClass& vc=classifyVs(late);
                    if(vc.world)++postEffectWorldDraws;
                    if(vc.skinned)++postEffectSkinnedDraws;
                    if(vc.entry&kWaterTag)++waterAfterEffects;
                    observeGrassReceiver(late,true);
                    }
                }if(!borrowed)drop(late);
            }return;
        }
        CpuScope cpu(sampledDrawTimers()?&cpuPrep:nullptr);if(gateFrame){++gateCounts.prep;weatherProbeDraw(count);} /* 0.3.198 (rain): RenderProfile sample frames only, pre-effects draws */
        IDirect3DVertexShader9* vs=nullptr;
        // 0.3.196 (task 12): borrowed (no reference). The shader stays bound through this hook (the game's draw is issued after it); capture AddRefs whatever it keeps,
        // after renderEffects applied=true stops further use of vs, and resolveDepth's SavedState restores the binding. Released only when not borrowed.
        const bool borrowedVS=ext->peekVertexShader(vs);
        if(!borrowedVS&&FAILED(ext->GetVertexShader(&vs))){drawUiHashVS=false;++missingVS;drop(vs);return;}
        if(!vs){drawUiHashVS=false;
            ++missingVS;D3DSURFACE_DESC desc{};
            if((terrain||skySeen)&&fixedUiGeometry()&&fullViewport(desc,nullptr,true)&&hdrBackTarget()&&hdrUiInputs()){
                hdrAtUiBoundary=true;
                if(sampled())logf("EFFECT boundary frame=%u draw=%u kind=fixed-ui",frame,drawCalls);
                renderEffects();spellReplay();
            }
            if(spellOn)spellFixedDomain();return;
        }
        struct ShaderRelease {IDirect3DVertexShader9*& p;bool borrowed;~ShaderRelease(){if(!borrowed)drop(p);}} shaderRelease{vs,borrowedVS};
        if(spellOn)spellVs=vs; /* identity only */
        if(sampled())observeGrassReceiver(vs,false);
        beforeDraw(vs);planTerrainShadowSwap(vs);
        if(world&&!applied&&enabled&&!failed&&projectionValid){
            const auto [viewport,depthEnabled,statesRead,worldDomain]=
                NorthlightDrawPreparation::readHeldWorldState(ext,mirrorState,projectionValid,worldMinDepth,worldMaxDepth);
            nativeLocalDomain=worldDomain;
            if(spellOn){spellDomain=worldDomain;spellZEnable=spellDepthTested(depthEnabled);} /* task 34 */
            if(worldDomain){if(gateFrame)++gateCounts.capture;
                /* 0.3.188 (task 3): lazy state reads for the early resolve, its undo and the census, each read at most once per draw */
                const auto readState=[this](D3DRENDERSTATETYPE type){return drawRenderState(type);};
                auto rs=[&](D3DRENDERSTATETYPE type){return NorthlightDrawPreparation::lazyState(readState,type);};
                auto zw=rs(D3DRS_ZWRITEENABLE);auto ab=rs(D3DRS_ALPHABLENDENABLE);auto sb=rs(D3DRS_SRCBLEND);auto db=rs(D3DRS_DESTBLEND);auto cw=rs(D3DRS_COLORWRITEENABLE);
                if(aoActorOn){aoActorDrawKnown=true;aoActorDrawSkin=classifyVs(vs).skinned;aoActorDepthWrite=zw()!=FALSE;}
                if(terrain&&!nameMask.disabled&&worldDebug==0){
                unsigned roomLevel=0;
                const auto& receiver=classifyVs(vs);
                // Native M2 light banks also contain ambient on rigid doodads.
                const bool modelRoomLighting=NorthlightUnlitReceiver::modelRoomBank(menuScene,receiver.family);
                const bool authoredReceiver=receiver.authoredLighting&&!(receiver.entry&(kUnlitTag|kPrelitTag))&&
                    world->authoredReceiverLighting(receiver.family==3,modelRoomLighting?&roomLevel:nullptr);
                if((nameMask.armed()&&drawUiHashVS)||nameMask.hasMarks()||unlitMarks||prelitMarks||authoredMarks||authoredReceiver||(receiver.entry&(kUnlitTag|kPrelitTag))){
                    const bool sampledTimed=sampledDrawTimers()&&(++nameDecideCalls&7)==1;if(sampledTimed)++nameDecideTimed;
                    CpuScope decision(sampledTimed?&cpuNameDecide:nullptr);nameMaskDecide(drawWaterVS,zw,ab,sb,db,cw,viewport);
                    // A WMO's room-light layer can follow its outdoor layer
                    // with INVSRCALPHA/ONE. It still owns authored RGB at this
                    // depth; outdoor subtraction would erase that contribution.
                    if(((receiver.entry&(kUnlitTag|kPrelitTag))||authoredReceiver)&&zw()&&(cw()&7)&&
                       (!ab()||db()==D3DBLEND_ZERO||(authoredReceiver&&receiver.wmo&&NorthlightNativeDirect::independentBlend(sb(),db(),drawRenderState(D3DRS_BLENDOP))))&&
                       nameMaskReady()&&NorthlightUnlitReceiver::stencilFormat(worldDepthDesc.Format,MAKEFOURCC('I','N','T','Z'))&&nameMaskFullViewport(viewport)&&worldDepthBound()){
                        maskKind=NorthlightUnlitReceiver::receiverMaskKind(
                            (receiver.entry&kPrelitTag)!=0,authoredReceiver,roomLevel);
                    }
                    if(maskKind==1&&!nameMask.armed())maskKind=0;}
                }
                if(captured&&earlyDepth.earlyCaptured&&NorthlightTranslucentDepth::isUndo(drawWaterVS,zw,ab,sb,db,cw)){captured=false;if(aoActorOn)aoActorMask->resume();earlyDepth.undo();++earlyResolveUndone;++earlyResolveUndoneTotal;} /* undo: later opaque or water Z draw after the early resolve */
                if(terrain&&!captured&&earlyDepth.armed()&&NorthlightTranslucentDepth::shouldResolve(drawWaterVS,zw,ab,sb,db,cw,[&]{return classifyVs(vs).skinned;})&&earlyDepth.attempt()){ /* 0.3.188 (2A): early depth for translucent actors, every frame, minimum state reads */
                    ExtensionDevice::RawScope raw(*ext);
                    if(resolveDepth()){earlyDepth.success();++earlyResolves;++earlyResolveTotal;if(sampled())earlyResolveAt=censusDraws+1;}}
                if(sampled()){const unsigned at=++censusDraws; /* 0.3.188 (task 3): read-only census before capture(vs) so no early return hides a draw; sample frames only */
                    if(!drawWaterVS&&NorthlightTranslucentDepth::opaqueZWrite(zw,ab,sb,db,cw))lastOpaqueZAt=at;}
                if(world->equipmentLightsOn())world->captureEquipmentLights(vs,worldDepth);
                if(autoBump&&autoBump->active()){
                    const bool updated=autoBump->syncWorldLighting(*world);
                    if(updated){
                        autoBump->setTerrainSpecularDistance(world->moonTerrainSpecularGain()>0?WorldRenderer::MoonTerrainSpecularDistance:0.f);
                        autoBump->setSolidMaterialSpecular(world->solidMaterialSpecularWanted());
                    }
                    if(gateFrame){++gateCounts.materialViews;gateCounts.materialViewUpdates+=updated;}
                }
                {const unsigned long long reads=CpuScope::reads;{CpuScope cap(sampledDrawTimers()?&cpuCapture:nullptr);capture(vs);}if(CpuScope::reads!=reads)cpuCaptureReads+=CpuScope::reads-reads-1;}
                if(mirrorState.active()&&mirrorAuditSchedule.afterWorldCapture(frame,
                    mirrorState.vsFloatKnown[0]&&mirrorState.vsFloatKnown[DeviceMirror::VsFloat-1])){
                    CpuScope auditCost(diagnostics()?&cpuMirrorAudit:nullptr); // the audit itself always runs
                    if(gateFrame)++gateCounts.audits;
                    if(const char* field=ext->audit())logf("MIRROR mismatch field=%s frame=%u; cache disabled, rendering retained",field,frame);
                }
            }
            else if(classifyVs(vs).world){
                if(++nonWorldCaptureRejects<=4||(nonWorldCaptureRejects%3600==0&&diagnostics()))
                    logf("WORLD non-caster draw rejected: worldDepth=%.7f..%.7f drawDepth=%.7f..%.7f z=%lu states=%d count=%u",worldMinDepth,worldMaxDepth,viewport.MinZ,viewport.MaxZ,(unsigned long)depthEnabled,statesRead,nonWorldCaptureRejects);
            }
        }
        if(!borrowedVS)drop(vs);
    }
