    // Executes a full redraw or the same submission sequence under disjoint scissors.
    template<class GpuPass,class MeshSubmitted>
    int drawDirectionalCache(GpuPass& gpu,IDirect3DSurface9* target,const float* cachedMatrix,unsigned cascade,
                             DirectionalPassResult& result,bool partial,bool real,bool band,MeshSubmitted&& meshSubmitted){
        auto cache=gpu.cache(partial,{shadowPrograms.cacheVertex(),shadowPrograms.cachePixel(),shadowPrograms.opaqueCache(),shadowPrograms.opaqueFastCache()});
        if(!cache.bind(target,directionalMaps.cacheDepth()))return -1;
        if(partial){
            // Disjoint scissors preserve each pixel's full-redraw order.
            staticDirtyClears.clear();
            for(const auto& r:staticDirty){
                staticDirtyClears.push_back({LONG(r.left),LONG(r.top),LONG(r.right),LONG(r.bottom)});
                shadowCachePartialTexels+=(unsigned long long)r.area();
            }
            if(!cache.clear(DWORD(staticDirtyClears.size()),staticDirtyClears.data()))return -1;
            if(!band){++shadowCachePartialRenders;shadowCachePartialRects+=unsigned(staticDirty.size());}
        }else if(!cache.clear())return -1;
        cache.bindCasters({shadowPrograms.worldLayout(),vertices,indices,sizeof(NorthlightGI::WorldVertex)},cachedMatrix);
        shadowDeviceCaps();
        NorthlightDirectionalShadowBatches cacheBatches(pointPrimitiveLimit);
        auto submitBatch=[&](const Batch& batch,IDirect3DPixelShader9* ps,const NorthlightShadowBounds::TexelRect* rect){
            return cache.draw(batch,ps,rect,uploadedAlphaCutoffs.data(),materials.data(),
                              [&](const Batch& b,UINT& page){return bindMeshPage(b,page);});
        };
        const bool affine=NorthlightShadowBounds::affineLightMatrix(cachedMatrix);
        const bool meshDrawn=directionalCacheMesh.visit(batches,meshGeneration,fixedTerrainChunks(),fixedTerrainBits,[&](const Batch& b){
            if(affine&&NorthlightShadowBounds::directionalClipRejectAffine(b.boundsLow,b.boundsHigh,cachedMatrix)){if(real)++result.culledBatches[cascade];return true;}
            size_t passes=1;
            if(partial){NorthlightShadowBounds::TexelRect footprint;const bool known=NorthlightShadowBounds::texelFootprint(b.boundsLow,b.boundsHigh,cachedMatrix,long(ShadowCacheSize),StaticCacheDirtyMargin,footprint);
                staticDirtyHits.clear();for(const auto& r:staticDirty)if(!known||NorthlightShadowBounds::intersects(footprint,r))staticDirtyHits.push_back(&r);
                if(staticDirtyHits.empty()){++shadowCachePartialSkippedDraws;return true;}passes=staticDirtyHits.size();}
            if(real)++result.drawnBatches[cascade];
            const bool interior=affine&&NorthlightShadowBounds::depthFullyInsideAffine(b.boundsLow,b.boundsHigh,cachedMatrix);
            const bool opaque=uploadedAlphaCutoffs[b.material]<=0;
            const auto pixel=shadowPrograms.cachePixel(interior,opaque);
            for(size_t k=0;k<passes;++k){
                if(!cacheBatches.add(b,pixel.shader,partial?staticDirtyHits[k]:nullptr,pixel.textureIndependent,submitBatch))return false;
            }
            return true;
        });
        if(!meshDrawn||!cacheBatches.flush(submitBatch))return -1;
        if(real)meshSubmitted(cacheBatches.submitted);
        const bool ok=drawStaticCasters(cachedMatrix,partial?&staticDirty:nullptr);
        if(partial){
            if(StaticCacheDirtyRectVerify&&cache.scissorDisabled())++shadowCacheScissorLeaks;
            shadowCachePartialSkippedDraws+=unsigned(staticCasters.stats().rectSkipped);
        }
        return ok?1:0;
    }
