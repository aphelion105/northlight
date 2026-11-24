// WorldRenderer supplies caster sets. Flash target and drawing state ownership
// stays with NorthlightWorldFlashShadows.
    bool drawFlashCasters(const float* matrix,bool withReplays,NorthlightWorldFlashShadows::CasterCommands& casters){
        for(const auto& b:batches){
            if(b.pointOnly||NorthlightShadowBounds::clipRejectAffine(b.boundsLow,b.boundsHigh,matrix))continue;
            if(b.material>=materials.size()||b.material>=uploadedAlphaCutoffs.size())return false;
            if(!casters.drawWorld(b,materials[b.material],uploadedAlphaCutoffs[b.material],
                                  [&](const Batch& batch,UINT& page){return bindMeshPage(batch,page);}))return false;
        }
        if(liveTerrainIndexCount||liveDirectionalIndexCount){
            if(!casters.drawTerrain({shadowPrograms.liveLayout(),liveTerrainGPU.vertices(),liveTerrainIndices.buffer(),sizeof(StaticShadow::Vertex),
                                    liveTerrainGPU.vertexCapacity(),UINT(liveTerrainIndices.base()),UINT((liveTerrainIndexCount+liveDirectionalIndexCount)/3)},
                                    [&]{liveTerrainIndices.touch(fence());}))return false;
        }
        if(withReplays){
            replayBoundsJoin();float rows[16];NorthlightWorldMath::replayProjection(context.inverseView,matrix,rows);
            casters.bindReplay();NorthlightShadowReplay::Pass replayPass(d);
            NorthlightRenderThreadProbe::Off split;size_t bytes=0,calls=0;
            for(const auto& p:replays){const auto& b=p->pointBounds;
                if(b.valid&&NorthlightShadowBounds::clipRejectAffine(vec(b.low),vec(b.high),matrix))continue;
                if(!submitReplay(p.get(),replayPass,rows,matrix,split,bytes,calls,[&](HRESULT h,const char* what){return casters.check(h,what);}))return false;
            }
        }
        return true;
    }
    void renderFlashShadows(const NorthlightLocalLightSelection::Selection& lights,bool fresh,bool withReplays){
        flashShadows.invalidate();
        const NorthlightWorldFlashShadows::CasterPipeline pipeline{directionalMaps.depth(),shadowPrograms.worldLayout(),shadowPrograms.casterVertex(),shadowPrograms.casterPixel(),shadowPrograms.replay()};
        unsigned slot=0;
        for(unsigned i=0;i<lights.count&&slot<NorthlightWorldFlashShadows::SlotCount;++i){const auto& light=lights.sources[i];
            if(!(light.flags&NorthlightLocalLights::Flash))continue;
            // A one-frame discharge requires the current actor pose.
            if(!fresh){captureDemand=true;continue;}
            const V pivot=staticPivotReady?staticFramePivot:shadowPivot();
            const V delta=vec(light.position)-pivot;
            if(NorthlightGI::dot(delta,delta)<1)continue;
            flashShadows.render(d,slot++,pipeline,light,pivot,NorthlightGI::normalized(delta),
                [&]{if(withReplays)replayBoundsRequest();},
                [&](const float* matrix){return drawStaticCasters(matrix);},
                [&](const float* matrix,auto& casters){return drawFlashCasters(matrix,withReplays,casters);});
        }
    }

    bool renderFlashLighting(){return flashShadows.drawLighting(d,width/2,height/2);}
