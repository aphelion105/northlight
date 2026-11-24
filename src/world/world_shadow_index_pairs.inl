    using ShadowIndexPair=NorthlightShadowReplay::IndexOverride;
    struct ShadowIndexPlan {
        NorthlightPointShadow::ReplayIndexBatches::Parts parts{};
        NorthlightReplayDrawState::Range run{};
        const Replay* last=nullptr;
        unsigned count=0;
        bool gap=false;

        // Preserve packet order. The ordinary contiguous path may remove
        // overlaps, so it retains ownership of such runs.
        bool append(const Replay& next,UINT primitiveLimit){
            const bool sameIndex=last&&last->index==next.index;
            const uint64_t lastEnd=last?uint64_t(last->start)+3ull*last->count:0;
            if(count==parts.size()||(sameIndex&&next.start<lastEnd)||
               !NorthlightReplayDrawState::appendIndexPart(run,{0,next.count,next.min,next.vertices},primitiveLimit))return false;
            gap|=last&&(!sameIndex||next.start>lastEnd);
            parts[count++]={&next.shared,next.min};
            last=&next;
            return true;
        }
    };
    static bool shadowIndexPairEligible(const Replay& p){
        return p.gpuCached&&p.index&&p.indexed&&p.type==D3DPT_TRIANGLELIST&&p.base==0&&
            p.stream[0]&&!p.stream[1]&&!p.stream[2]&&!p.stream[3]&&p.offset[0]==0&&
            p.cutoff<=-1.f&&!p.alphaMode&&p.pointBounds.valid&&p.shared&&p.count&&p.vertices&&
            p.shared->indexed&&p.shared->topology==p.type&&p.vertices==p.shared->vertexCount&&
            uint64_t(p.count)*3==p.shared->indices.size();
    }
    NorthlightReplayDrawState::Range admittedShadowIndexPairRange(const Replay& first,const Replay& next)const{
        if(pointReplayRangesJoin(first,next)||pointReplayRangesJoin(next,first))return {};
        const uint64_t triangles=uint64_t(first.count)+next.count;
        const UINT minimum=std::min(first.min,next.min);
        const uint64_t end=std::max(uint64_t(first.min)+first.vertices,uint64_t(next.min)+next.vertices);
        if(triangles>UINT_MAX||end>UINT_MAX||(pointPrimitiveLimit&&triangles>pointPrimitiveLimit))return {};
        return {0,UINT(triangles),minimum,UINT(end-minimum)};
    }
    ShadowIndexPair loadShadowIndexPair(const Replay& first,const Replay& next,NorthlightReplayDrawState::Range run){
        auto* buffer=shadowReplayIndices.get(d,first.shared,first.min,next.shared,next.min,[&](size_t bytes){
            return admitsGrowth("shadow replay index pairs",bytes);
        });
        return {buffer,run,2};
    }
    ShadowIndexPair loadShadowIndexBatch(const ShadowIndexPlan& plan){
        auto* buffer=shadowReplayIndices.get(d,plan.parts,plan.count,[&](size_t bytes){
            return admitsGrowth("shadow replay index pairs",bytes);
        });
        return {buffer,plan.run,plan.count};
    }
