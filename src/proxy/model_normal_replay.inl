// Included inside NorthlightModelNormals. Submit one ordered batch of deferred normal draws.
    bool finishCaptures(bool final=false,const DepthSeed* seed=nullptr,IDirect3DSurface9* materialDepth=nullptr){
        if(final){
            bool probed=true,written=true;for(const auto& p:pending){probed=probed&&p->nativeProbed;written=written&&p->nativeWritten;}
            if(!bad&&nativeMrt.finish(probed,written,unsigned(pending.size()))){
#ifdef NORTHLIGHT_REPLAY_STAND
                for(const auto& p:pending)recordCoverage(p->traceOrdinal);
#endif
                duplicateProbe={};nativeCommittedPackets+=unsigned(pending.size());draws+=unsigned(pending.size());clearPending();return true;
            }
        }else nativeMrt.cancel("capture-hazard");
        if(pending.empty()){if(final)duplicateProbe={};return !bad;}
        bool ok=!bad;
        auto* drawDepth=ok?replayDepth(final?materialDepth:nullptr):nullptr;
        ok=drawDepth!=nullptr;
        if(ok&&geometryCache){
            // Reuse the existing scene budget/promotion limits. Every packet
            // keeps buffer references across eviction; misses retain UP replay.
            for(auto& p:pending)if(p->sharedMesh&&p->sharedMesh->sourceIdentity)
                p->resident=geometryCache->bind(d,p->sharedMesh,p->gpuVertex,p->gpuIndex,[](size_t){return true;},p->placement);
            if(geometryCache->commitPending())
                for(auto& p:pending)if(!p->resident&&p->sharedMesh&&p->sharedMesh->sourceIdentity)
                    p->resident=geometryCache->bind(d,p->sharedMesh,p->gpuVertex,p->gpuIndex,[](size_t){return true;},p->placement,true);
        }
        float seedMatrix[4]={};const bool seeded=final&&ok&&seedProjection(seed,seedMatrix)&&
            (depthSeedPS||(SUCCEEDED(d->CreatePixelShader(kModelNormalDepthSeedShader,&depthSeedPS))&&depthSeedPS));
        if(ok){
            duplicateEligiblePackets+=pending.size();
            if(duplicateProbe.ready(pending.size(),final&&!flushes&&!immediate)){
                duplicateScannedPackets+=pending.size();
                duplicateProbe.observe(findRedundant());
            }
        }
        {
            SavedState saved(d,&batchStates);
            ok=saved.ok&&ok;
            if(ok){
                ok=SUCCEEDED(d->SetDepthStencilSurface(nullptr));
                for(unsigned i=1;i<4;++i)ok=SUCCEEDED(d->SetRenderTarget(i,nullptr))&&ok;
                ok=SUCCEEDED(d->SetRenderTarget(0,surface))&&ok;
                ok=SUCCEEDED(d->SetDepthStencilSurface(drawDepth))&&ok;
                for(unsigned i=0;i<16;++i)ok=SUCCEEDED(d->SetTexture(i,nullptr))&&ok;
                ok=SUCCEEDED(d->SetStreamSourceFreq(0,1))&&ok;
                ok=SUCCEEDED(d->SetRenderState(D3DRS_CLIPPLANEENABLE,0))&&ok;
                const DWORD values[]={TRUE,TRUE,D3DCMP_LESSEQUAL,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,15,0,0};
                for(unsigned i=0;i<State::Count;++i)ok=SUCCEEDED(d->SetRenderState(State::keys[i],values[i]))&&ok;
                if(ok&&seeded&&!seedDepth(*seed,seedMatrix)){
                    // A partial optional seed must not affect ordinary replay.
                    const D3DVIEWPORT9 viewport={0,0,width,height,0,1};
                    ok=SUCCEEDED(d->SetTexture(0,nullptr));
                    ok=SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,15))&&ok;
                    ok=SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL))&&ok;
                    ok=SUCCEEDED(d->SetViewport(&viewport))&&ok;
                    if(ok)ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
                    cleared=ok;
                }
                ++flushes;
                const Pending* previous=nullptr;IDirect3DVertexBuffer9* boundVertex=nullptr;IDirect3DIndexBuffer9* boundIndex=nullptr;UINT boundStride=0;bool bufferKnown=false;
                PaletteDraw previousBatch;
                const bool retainBindings=retainPaletteBindings();
                bool paletteStreamsBound=false;
                for(size_t at=0;at<pending.size();){
                    if(!ok)break;
                    if(pending[at]->redundant){++draws;++residentDraws;++mergedDraws;++duplicateDraws;compactDraws+=pending[at]->vertex->compact;++at;continue;}
                    if(!pending[at]->resident)streamPending(at);
                    const size_t first=at;
                    const auto& p=pending[at];
                    auto run=drawRun(at);
                    const auto instances=run.packets==1?planPaletteDraw(first):PaletteDraw{};
                    if(!instances.instances&&paletteStreamsBound){
                        paletteStreamsBound=false;
                        if(!restorePaletteStreams()){ok=false;break;}
                    }
                    bool submittedInstances=false;
                    if(!previous||std::memcmp(&previous->viewport,&p->viewport,sizeof p->viewport))ok=SUCCEEDED(d->SetViewport(&p->viewport))&&ok;
                    if(!instances.instances&&(!previous||previousBatch.instances||previous->declaration!=p->declaration))
                        ok=SUCCEEDED(d->SetVertexDeclaration(p->declaration))&&ok;
                    for(unsigned i=0;i<sizeof(rasterKeys)/sizeof(*rasterKeys);++i)if(!previous||previous->raster[i]!=p->raster[i])ok=SUCCEEDED(d->SetRenderState(rasterKeys[i],p->raster[i]))&&ok;
                    const unsigned previousMask=previous?previous->pixel->reads.samplers:0;
                    size_t sampled=0;
                    for(unsigned mask=p->pixel->reads.samplers;mask;mask&=mask-1){
                        const unsigned i=__builtin_ctz(mask),bit=1u<<i;
                        const auto& stage=p->sampled[sampled++];
                        const auto* prior=(previousMask&bit)?&previous->sampled[__builtin_popcount(previousMask&(bit-1))]:nullptr;
                        if(!prior||prior->texture!=stage.texture)ok=SUCCEEDED(d->SetTexture(i,stage.texture))&&ok;
                        for(unsigned s=0;s<13;++s)if(!prior||prior->states[s]!=stage.states[s])ok=SUCCEEDED(d->SetSamplerState(i,D3DSAMPLERSTATETYPE(s+1),stage.states[s]))&&ok;
                    }
                    // Both packets must have captured the whole compared range;
                    // a shader switch may expose previously uninitialized lanes.
                    auto dirty=NorthlightReplayConstants::DirtySpan{p->vertex->first,p->vertex->count};
                    if(previous&&!previousBatch.instances&&previous->vertex->first<=dirty.first&&previous->vertex->first+previous->vertex->count>=dirty.first+dirty.count){
                        const auto relative=previous->constants==p->constants?NorthlightReplayConstants::DirtySpan{}:NorthlightReplayConstants::Detail::knownFloatSpan(previous->constants+dirty.first*4,p->constants+dirty.first*4,dirty.count);
                        dirty.first+=relative.first;dirty.count=relative.count;
                    }
                    if(!instances.instances&&dirty.count)ok=SUCCEEDED(d->SetVertexShaderConstantF(dirty.first,p->constants+dirty.first*4,dirty.count))&&ok;
                    if(!previous||previous->pixel->first!=p->pixel->first||previous->pixel->count!=p->pixel->count||
                       std::memcmp(previous->ps.data(),p->ps.data(),p->pixel->count*16))
                        ok=SUCCEEDED(d->SetPixelShaderConstantF(p->pixel->first,p->ps.data(),p->pixel->count))&&ok;
                    if(p->vertex->reads.i)ok=SUCCEEDED(d->SetVertexShaderConstantI(0,p->ints,16))&&ok;
                    if(p->pixel->reads.i)ok=SUCCEEDED(d->SetPixelShaderConstantI(0,p->nonFloat->pixelInts,16))&&ok;
                    if(p->vertex->reads.b)ok=SUCCEEDED(d->SetVertexShaderConstantB(0,p->bools,16))&&ok;
                    if(p->pixel->reads.b)ok=SUCCEEDED(d->SetPixelShaderConstantB(0,p->nonFloat->pixelBools,16))&&ok;
                    if(ok&&!cleared){ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));cleared=ok;}
                    if(!instances.instances&&(!previous||previousBatch.instances||previous->vertex->shader!=p->vertex->shader))
                        ok=SUCCEEDED(d->SetVertexShader(p->vertex->shader))&&ok;
                    if(!previous||previous->pixel!=p->pixel)ok=SUCCEEDED(d->SetPixelShader(p->pixel->shader))&&ok;
                    const auto& mesh=p->geometry();
                    if(p->resident){
                        const auto stride=mesh.streams[0].stride;
                        if(!bufferKnown||boundVertex!=p->gpuVertex[0]||boundStride!=stride)ok=SUCCEEDED(d->SetStreamSource(0,p->gpuVertex[0],0,stride))&&ok;
                        if(!bufferKnown||boundIndex!=p->gpuIndex)ok=SUCCEEDED(d->SetIndices(p->gpuIndex))&&ok;
                        bufferKnown=ok;boundVertex=p->gpuVertex[0];boundIndex=p->gpuIndex;boundStride=stride;
                        if(ok&&instances.instances){
                            ok=SUCCEEDED(drawPalette(first,instances,submittedInstances,previous,previousBatch,retainBindings,paletteStreamsBound));
                            if(submittedInstances){run.packets=instances.instances;at=first+run.packets;}
                        }
                        if(ok&&!submittedInstances)ok=SUCCEEDED(mesh.indexed?d->DrawIndexedPrimitive(mesh.topology,p->placement.baseVertex(),run.min,run.vertices,p->placement.indexStart,run.count):
                            d->DrawPrimitive(mesh.topology,p->placement.vertexBase,mesh.primitiveCount));
                        if(ok){if(p->streamed)streamedDraws+=run.packets;else residentDraws+=run.packets;}
                    }else{
                        if(ok)ok=SUCCEEDED(mesh.indexed?
                            d->DrawIndexedPrimitiveUP(mesh.topology,0,mesh.vertexCount,mesh.primitiveCount,mesh.indices.data(),D3DFMT_INDEX32,mesh.streams[0].bytes.data(),mesh.streams[0].stride):
                            d->DrawPrimitiveUP(mesh.topology,mesh.primitiveCount,mesh.streams[0].bytes.data(),mesh.streams[0].stride));
                        bufferKnown=false;if(ok)upBytes+=mesh.byteSize();
                    }
                    // Instancing overwrites the palette, but leaves the other replay state bound.
#ifdef NORTHLIGHT_REPLAY_STAND
                    if(ok&&p->traceOrdinal)
                        for(size_t i=first;i<first+run.packets;++i)recordCoverage(pending[i]->traceOrdinal);
#endif
                    if(ok){draws+=run.packets;compactDraws+=unsigned(p->vertex->compact)*run.packets;mergedDraws+=run.packets-1;previous=p.get();previousBatch=submittedInstances?instances:PaletteDraw{};}
                }
                if(paletteStreamsBound)ok=restorePaletteStreams()&&ok;
                ok=saved.restore()&&ok;
            }
        }
        clearPending();bad|=!ok;return ok;
    }
