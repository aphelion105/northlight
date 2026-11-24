// Committed mesh candidates and per-light terrain masks.
    NorthlightPointMesh::PreparedBounds pointMeshBounds;

    void pointRecordTerrain(UINT start,UINT count,const NorthlightTerrainCapture::Bounds& bounds){
        // Reuse the snapshot's already validated, referenced-triangle bounds;
        // do not rescan hundreds of thousands of terrain vertices each frame.
        if(count)pointLiveBatches.push_back({start,count,vec(bounds.low),vec(bounds.high)});
    }
    unsigned pointMask(V low,V high,const NorthlightPointShadow::FaceClip& clip,unsigned faces)const{
        float lo[]={low.x,low.y,low.z},hi[]={high.x,high.y,high.z};
        return clip.mask(lo,hi,true,faces);
    }
    void pointAppendCandidates(PointCandidates& lists,size_t index,unsigned mask,unsigned faces=63){
        pointCulled+=unsigned(__builtin_popcount(faces&~mask));
        for(mask&=faces;mask;mask&=mask-1)lists[__builtin_ctz(mask)].push_back(index);
    }
    const std::vector<unsigned char>& emptyTerrainMasks(){
        if(pointEmptyTerrainGeneration!=meshGeneration||pointEmptyTerrainMasks.size()!=pointMeshBatchCount()){
            pointEmptyTerrainMasks.assign(pointMeshBatchCount(),63);
            for(size_t i=0;i<batches.size();++i)if(batches[i].terrain)pointEmptyTerrainMasks[i]=0;
            pointEmptyTerrainGeneration=meshGeneration;
        }
        return pointEmptyTerrainMasks;
    }
    void preparePointTerrain(const NorthlightPointShadow::FaceClip& clip,unsigned faces,unsigned staticFaces=63){
        auto& memo=pointSlot().terrainMemo;const auto& light=pointSlot().selected;
        bool reuse=memo.valid&&memo.generation==meshGeneration&&memo.batchCount==pointMeshBatchCount()&&memo.range==light.attenuationEnd;
        for(unsigned i=0;i<3;++i)reuse=reuse&&memo.position[i]==light.position[i];
        if(reuse)++pointCold.terrainMemoHits;else ++pointCold.terrainMemoMisses;
        const bool emptyTerrain=reuse&&!(memo.occupiedFaces&faces);
        // A cached terrain mask needs no spatial query. Static gap merging
        // still reads the entire mask array when any static face is rebuilt.
        pointNearbyReady=(!reuse||staticFaces)&&pointTree&&pointTree->nearby(pointMeshBatchCount(),[&](V low,V high){
            return NorthlightPointShadow::recordOutsideSphere(low,high,light);},pointNearbyBatches);
        if(emptyTerrain&&!staticFaces){pointCulled+=unsigned(memo.terrainCount)*unsigned(__builtin_popcount(faces));return;}
        const bool listed=terrainBatchListGeneration==meshGeneration&&terrainBatchListSize==batches.size();
        const bool sparse=!reuse&&pointNearbyReady&&listed;
        const bool compact=reuse&&!memo.visible.empty();
        if(sparse||emptyTerrain||compact)pointBatchMasks=emptyTerrainMasks();
        else if(reuse)pointBatchMasks=memo.masks;
        else pointBatchMasks.assign(pointMeshBatchCount(),63);
        if(!reuse)memo.valid=false;
        // Interior lights often have no terrain in any face. Their cached
        // zero masks already prove every rejection, including partial cycles.
        if(emptyTerrain){pointCulled+=unsigned(memo.terrainCount)*unsigned(__builtin_popcount(faces));return;}
        if(compact){
            pointCulled+=unsigned(memo.terrainCount-memo.visible.size())*unsigned(__builtin_popcount(faces));
            for(const auto entry:memo.visible){
                const unsigned mask=entry.mask&faces;
                pointBatchMasks[entry.index]=static_cast<unsigned char>(mask);
                pointAppendCandidates(pointTerrainCandidates,entry.index,mask,faces);
            }
            return;
        }
        // Immutable bounds belong to meshGeneration. Live terrain and animated
        // casters are classified separately on every refresh, as before.
        if(!reuse)pointMeshBounds.prepare(meshGeneration,batches,pointSubBatches);
        size_t terrainCount=0,visibleCount=0;unsigned occupiedFaces=0;
        const auto append=[&](size_t i){const auto& batch=batches[i];
            const unsigned mask=reuse?(pointBatchMasks[i]&faces):pointNearbyReady&&!pointNearbyBatches[i]?0:pointMeshBounds.mask(i,batch,clip,faces);
            if(!reuse){++terrainCount;visibleCount+=mask!=0;occupiedFaces|=mask;}
            pointBatchMasks[i]=static_cast<unsigned char>(mask);
            pointAppendCandidates(pointTerrainCandidates,i,mask,faces);};
        if(sparse){
            // Unvisited terrain is already zero in the template. The tree
            // includes unknown bounds. Keep surviving batches in index order.
            NorthlightPointMesh::visitNearby(pointNearbyBatches.data(),batches.size(),[&](size_t i){
                if(batches[i].terrain)append(i);
            });
            pointCulled+=unsigned(terrainBatchList.size()-terrainCount)*unsigned(__builtin_popcount(faces));
            terrainCount=terrainBatchList.size();
        }else if(listed){
            for(size_t i:terrainBatchList)append(i);
        }else for(size_t i=0;i<batches.size();++i)if(batches[i].terrain)append(i);
        // Partial refreshes cannot publish masks for faces they did not test.
        // Cache allocation is optional: failure leaves the exact current lists.
        if(!reuse&&faces==63&&pointMeshBatchCount()<=PointTerrainMemo::Limit){
            try{
                // Only compress when both lookup work and retained bytes shrink.
                if(listed&&visibleCount&&visibleCount*sizeof(PointTerrainMemo::Visible)<terrainCount){
                    std::vector<PointTerrainMemo::Visible> visible;visible.reserve(visibleCount);
                    for(size_t i:terrainBatchList)if(pointBatchMasks[i])visible.push_back({uint16_t(i),pointBatchMasks[i]});
                    memo.visible.swap(visible);std::vector<unsigned char>().swap(memo.masks);
                }else{
                    std::vector<PointTerrainMemo::Visible>().swap(memo.visible);
                    if(!occupiedFaces)std::vector<unsigned char>().swap(memo.masks);
                    else if(memo.masks.capacity()<pointBatchMasks.size())std::vector<unsigned char>(pointBatchMasks).swap(memo.masks);
                    else memo.masks=pointBatchMasks;
                }
                memo.batchCount=pointMeshBatchCount();memo.terrainCount=terrainCount;memo.occupiedFaces=occupiedFaces;
                memo.generation=meshGeneration;memo.range=light.attenuationEnd;
                std::copy(light.position,light.position+3,memo.position);memo.valid=true;
            }catch(...){memo=PointTerrainMemo{};}
        }
    }
