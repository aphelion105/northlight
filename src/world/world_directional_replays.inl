    // Consumes the culled list for one map and its fixed projection.
    struct DirectionalReplayWork {
        unsigned packets=0,draws=0,fixed=0,scopes=0,pairEligible=0,pairDraws=0;
        unsigned batchEligible=0,batchDraws=0,batchPackets=0;
        unsigned paletteDraws=0,palettePackets=0;
        uint64_t triangles=0;
    };
    static bool legacyDirectionalReplay(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool legacy=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_LEGACY_DIRECTIONAL_REPLAY");
            return value&&value[0]=='1'&&value[1]=='\0';
        }();
        return legacy;
#else
        return false;
#endif
    }
    static bool directionalIndexPairsEnabled(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool enabled=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_DIRECTIONAL_INDEX_PAIRS");
            return !(value&&value[0]=='0'&&value[1]=='\0');
        }();
        return enabled;
#else
        return true;
#endif
    }
    template<class Compatible>
    ShadowIndexPair directionalIndexBatch(const std::vector<uint32_t>& candidates,size_t start,size_t adjacentEnd,
                                         Compatible compatible,DirectionalReplayWork& work){
        const auto& first=*replays[candidates[start]];

        ShadowIndexPlan plan;
        while(start+plan.count<candidates.size()&&plan.count<plan.parts.size()){
            const auto n=start+plan.count;
            const auto& next=*replays[candidates[n]];
            const bool checked=n==start||n==adjacentEnd;
            if((!checked&&!shadowIndexPairEligible(next))||(n>adjacentEnd&&!compatible(first,next))||
               !plan.append(next,pointPrimitiveLimit))break;
        }
        if(!plan.gap||plan.count<3||plan.count<=adjacentEnd-start)return {};
        if(start+plan.count<candidates.size()){
            const auto& tail=*replays[candidates[start+plan.count]];
            if(pointReplayRangesJoin(*plan.last,tail)&&compatible(*plan.last,tail))return {};
        }
        ++work.batchEligible;
        return loadShadowIndexBatch(plan);
    }

    // Preserve depth tie order at every pixel. A caster may move ahead of
    // another opaque caster only when their padded raster footprints are disjoint.
    struct DirectionalFootprint {
        uint32_t packet=UINT32_MAX;
        NorthlightShadowBounds::TexelRect rect;
        bool valid=false;
    };
    size_t gatherDirectionalPalette(std::vector<uint32_t>& order,size_t start,const float* matrix,unsigned span,
                                    std::array<DirectionalFootprint,64>& footprints){
        constexpr size_t lookahead=32;
        const auto& first=*replays[order[start]];
        const auto geometry=pointPaletteGeometry(first);
        const auto footprint=[&](uint32_t index)->const DirectionalFootprint&{
            auto& cached=footprints[index%footprints.size()];
            if(cached.packet!=index){
                const auto& b=replays[index]->pointBounds;
                cached.packet=index;
                cached.valid=b.valid&&NorthlightShadowBounds::texelFootprint(
                    V(b.low[0],b.low[1],b.low[2]),V(b.high[0],b.high[1],b.high[2]),
                    matrix,NorthlightWorldDirectionalTargets::Size,2,cached.rect);
            }
            return cached;
        };
        size_t end=start+1;
        for(size_t next=end;next<std::min(order.size(),start+lookahead);++next){
            const auto& p=*replays[order[next]];
            if(!p.pointBounds.valid||!(p.cutoff<=-1.f)||p.alphaMode)break;
            if(p.min!=first.min||p.vertices!=first.vertices||pointPaletteGeometry(p)!=geometry||pointPaletteSpan(p)!=span)continue;
            if(next!=end){
                const auto moved=footprint(order[next]);
                if(!moved.valid)continue;
                size_t crossed=end;
                for(;crossed<next;++crossed){
                    const auto& other=footprint(order[crossed]);
                    if(!other.valid||NorthlightShadowBounds::intersects(moved.rect,other.rect))break;
                }
                if(crossed!=next)continue;
            }
            if(end==start+1&&!pointPaletteResources())break;
            if(next!=end)std::rotate(order.begin()+end,order.begin()+next,order.begin()+next+1);
            ++end;
        }
        return end;
    }

    template<class Submit,class Track>
    bool runDirectionalReplays(const std::vector<uint32_t>& original,const float* rows,const float* matrix,
                               NorthlightShadowReplay::Pass& replayPass,
                               DirectionalReplayWork& work,Submit submit,Track track){
        using NorthlightReplayDrawState::Range;
        shadowDeviceCaps();
        const bool skipOpaque=replayPass.skipping();
        bool palette=skipOpaque&&!memoryPressure&&shadowPalette.canAttempt(frames)&&original.size()>1;
        if(palette){try{directionalPaletteOrder=original;}catch(const std::bad_alloc&){palette=false;}}
        const auto& candidates=palette?directionalPaletteOrder:original;
        std::array<DirectionalFootprint,64> footprints;
        const bool pairEnabled=directionalIndexPairsEnabled(),measurePairs=pairEnabled||captureSampled;
        const auto compatible=[&](const Replay& first,const Replay& next){
            return pointReplayBatchState(first,next,skipOpaque)&&
                (first.constantGroup==next.constantGroup||NorthlightReplayPoses::sameCaptured(first,next));
        };
        bool submissionFailed=false;
        const HRESULT hr=NorthlightShadowAlpha::drawSequence(d,candidates.size(),
            [&](size_t at){return replays[candidates[at]]->alphaMode;},
            [&](size_t at,auto draw){++work.scopes;return shadowReplayDraw(*replays[candidates[at]],draw);},
            [&](size_t& at)->HRESULT {
                const size_t start=at;
                const auto& first=*replays[candidates[start]];
                if(palette&&shadowPalette.canAttempt(frames)&&first.cutoff<=-1.f){
                    const auto span=pointPaletteSpan(first);
                    if(span){
                        const size_t end=gatherDirectionalPalette(directionalPaletteOrder,start,matrix,span,footprints);
                        if(end>start+1){
                            const HRESULT result=shadowPalette.submit(d,first,span,rows,at,end,
                                [&](size_t i){return candidates[i];},[](size_t i){return i;},
                                [&](size_t i){return replays[i]->constants;},
                                [&](unsigned packets,UINT instances){
                                    ++work.draws;++work.paletteDraws;work.packets+=packets;work.palettePackets+=packets;
                                    work.triangles+=uint64_t(first.count)*instances;
                                });
                            replayPass.afterPalette();
                            for(size_t n=start;n<at;++n)track(candidates[n]);
                            return result;
                        }
                    }
                }
                ++at;
                Range run{first.start,first.count,first.min,first.vertices};
                if(first.indexed&&first.index&&first.type==D3DPT_TRIANGLELIST){
                    while(at<candidates.size()){
                        const auto& next=*replays[candidates[at]];
                        if(next.index!=first.index||next.base!=first.base||
                           uint64_t(run.start)+3ull*run.count!=next.start||
                           !compatible(first,next)||
                           !NorthlightReplayDrawState::appendAdjacent(run,{next.start,next.count,next.min,next.vertices},pointPrimitiveLimit))break;
                        ++at;
                    }
                }
                ShadowIndexPair pair;
                if(measurePairs&&!memoryPressure&&at<candidates.size()&&
                   at-start<NorthlightPointShadow::ReplayIndexBatches::MaxParts&&
                   (pairEnabled||at-start==1)&&shadowIndexPairEligible(first)){
                    const auto& next=*replays[candidates[at]];
                    if(shadowIndexPairEligible(next)){
                        const auto paired=at-start==1?admittedShadowIndexPairRange(first,next):Range{};
                        const auto& last=*replays[candidates[at-1]];
                        const bool batchable=pairEnabled&&candidates.size()-start>=3&&
                            (next.index!=last.index||next.start>=uint64_t(last.start)+3ull*last.count);
                        const bool sameState=(paired.count||batchable)&&compatible(first,next);
                        if(sameState&&batchable)pair=directionalIndexBatch(candidates,start,at,compatible,work);
                        if(!pair.buffer&&paired.count&&sameState){
                            const auto* after=at+1<candidates.size()?replays[candidates[at+1]].get():nullptr;
                            // Prefer a following contiguous range over an upload.
                            if(!after||after->index!=next.index||after->base!=next.base||
                               uint64_t(next.start)+3ull*next.count!=after->start||!compatible(next,*after)){
                                ++work.pairEligible;
                                if(pairEnabled)pair=loadShadowIndexPair(first,next,paired);
                            }
                        }
                    }
                }
                // A merged draw may use bones beyond the first packet's range.
                const auto uploadFloats=NorthlightReplayPoses::weightedRunUploadRange(first.constantUsage,
                    pair.buffer?pair.packets:at-start,[&](size_t i){return replays[candidates[start+i]]->directionalPaletteEnd;});
                if(!submit(candidates[start],&run,&pair,uploadFloats)){submissionFailed=true;return E_FAIL;}
                if(pair.buffer){
                    at=start+pair.packets;
                    run=pair.run;
                    if(pair.packets>2){++work.batchDraws;work.batchPackets+=pair.packets;}
                    else ++work.pairDraws;
                }
                const unsigned packets=unsigned(at-start);
                work.packets+=packets;++work.draws;work.triangles+=run.count;
                if(NorthlightShadowAlpha::fixed(first.alphaMode))work.fixed+=packets;
                for(size_t n=start;n<at;++n)track(candidates[n]);
                return D3D_OK;
            });
        return !submissionFailed&&hr!=E_PENDING&&check(hr,"directional replay alpha sequence");
    }
