    // The near cascade hands off exactly the replays omitted for this source.
    // Publish the actor map only after the entire list has been submitted.
    template<class GpuPass>
    bool drawDirectionalActors(GpuPass& gpuPass,int source,const float* inverseView,const float* am,
                               bool nearSkip,bool sideDiag,unsigned nearSkipped,bool legacyReplay,
                               unsigned diagnosticFiles,NorthlightGpuProfile* profile,
                               DirectionalPassResult& result,DirectionalReplayWork& actorReplayWork){
        if(!gpuPass.beginActor(actorMaps.depth(source).surface))return false;
        float actorRows[16];NorthlightWorldMath::replayProjection(inverseView,am,actorRows);
        gpuPass.bindActorProjection(am,shadowPrograms.replay());
        NorthlightShadowReplay::Pass actorPass(d);
        NorthlightRenderThreadProbe::Off actorSplit;unsigned drawnHere=0;bool fogBoundsKnown=true;float fogBounds[4]={1,1,-1,-1};
        // Bounds that cannot produce finite map coordinates keep the full fog
        // footprint. The near pass already validated this map's matrix.
        const auto actorIssue=[&](uint32_t index,const NorthlightReplayDrawState::Range* range,ShadowIndexPair* pair=nullptr,NorthlightShaderConstants::Range uploadFloats={}){
            const auto& p=replays[index];
            return submitReplay(p.get(),actorPass,actorRows,am,actorSplit,result.replayConstantBytes,result.replayConstantCalls,
                                [&](HRESULT h,const char* s){return check(h,s);},range,pair,uploadFloats);
        };
        const auto actorTrack=[&](uint32_t index){
            const auto& p=replays[index];
            ++drawnHere;
            if(fogBoundsKnown){float bounds[4];fogBoundsKnown=NorthlightActorShadowMap::fogBoundsForFiniteMatrix(p->pointBounds.valid,p->pointBounds.low,p->pointBounds.high,am,bounds);
                if(fogBoundsKnown)for(unsigned axis=0;axis<2;++axis){fogBounds[axis]=std::min(fogBounds[axis],bounds[axis]);fogBounds[axis+2]=std::max(fogBounds[axis+2],bounds[axis+2]);}}
        };
        if(nearSkip){
            if(!legacyReplay){if(!runDirectionalReplays(actorReplays,actorRows,am,actorPass,actorReplayWork,actorIssue,actorTrack))return false;}
            else for(const uint32_t index:actorReplays){const auto& p=replays[index];
                if(!actorIssue(index,nullptr))return false;
                actorTrack(index);
                ++actorReplayWork.packets;++actorReplayWork.draws;actorReplayWork.triangles+=p->count;
                if(NorthlightShadowAlpha::fixed(p->alphaMode)){++actorReplayWork.fixed;++actorReplayWork.scopes;}
            }
        }
        if(!fogBoundsKnown||!drawnHere){fogBounds[0]=fogBounds[1]=-1;fogBounds[2]=fogBounds[3]=1;}
        std::memcpy(actorFogBounds[source],fogBounds,sizeof fogBounds);
        result.replayPosePrepared+=actorPass.posesPrepared();result.replayPoseReused+=actorPass.posesReused();
        if(diagnosticFiles){char name[80];std::snprintf(name,sizeof name,"s%d-actor",source);NorthlightWorldDiagnostics::dump(d,actorMaps.depth(source).surface,gpuDiagnosticDirectory,name,diagnosticFiles);}
        memcpy(actorMatrices[source],am,64);actorValid[source]=true;actorCasterCount[source]=drawnHere;
        if(profile)profile->mark(source==0?"SunActor":"MoonActor");bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunActor+source));
        actorNearSkipped+=nearSkipped;actorDrawn+=drawnHere;if(sideDiag)actorSide[source].endFrame();
        const DWORD actorTick=GetTickCount();
        if(NorthlightDiagnostics::enabled()&&(!actorShadowLogAt[source]||actorTick-actorShadowLogAt[source]>=1000)){actorShadowLogAt[source]=actorTick;
            logf("WORLD actor shadow map source=%d drawn=%u nearSkipped=%u of %zu replays; totals drawn=%lu nearSkipped=%lu sideFlipsSinceLog=%u sideFlipFramesSinceLog=%u sideMatchedSinceLog=%u framesSinceLog=%u sideTracked=%u sideUnkeyed=%u invalidBoundsNear=%u unsupportedNear=%u selfSide=%s selfSideFlipsSinceLog=%u holdBand=%u holdOverflows=%lu nearListSize=%u nearWalkStops=%lu sideFlipsRootedSinceLog=%u sideFlipsUnrootedSinceLog=%u invalidBoundsNearSinceLog=%u unrootedNoBounds=%u unrootedNoBoundsSinceLog=%u selfUnrooted=%u selfNoBoundsFramesSinceLog=%u holdCarried=%u holdCarriedTotal=%lu",source,drawnHere,nearSkipped,replays.size(),actorDrawn,actorNearSkipped,
                actorSide[source].sideFlips,actorSide[source].sideFlipFrames,actorSide[source].sideMatched,actorSide[source].frames,actorSide[source].tracked,actorSide[source].unkeyedNow,actorSide[source].invalidBoundsNear,actorSide[source].unsupportedNow,NorthlightActorSide::name(actorSide[source].selfSide),actorSide[source].selfFlips,
                actorHold[source].bandHeld,actorHold[source].overflows,replayNearSize,replayNearStops,
                actorSide[source].sideFlipsRooted,actorSide[source].sideFlipsUnrooted,actorSide[source].invalidBoundsNearSum,actorSide[source].unrootedNoBounds,actorSide[source].unrootedNoBoundsSum,actorSide[source].selfUnrooted,actorSide[source].selfNoBoundsFrames,actorHold[source].carried,actorHold[source].carriedTotal);actorSide[source].resetWindow();}
        return true;
    }
