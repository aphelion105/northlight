// Per-frame replay ordering, palette instancing and exact range coalescing for local lights.
    using PointReplayRun=NorthlightReplayDrawState::Range;
    unsigned pointPaletteSpan(const Replay& p)const{
        const auto end=p.boundsWork.paletteSpan,first=p.boundsWork.paletteFirst;
        if(end<=first||end>75)return 0;
        const auto span=end-first;
        if(span>41||!p.pointBounds.valid||!p.indexed||p.type!=D3DPT_TRIANGLELIST||!p.count||!p.index||!p.stream[0]||
           p.projectionKind!=2||!(p.cutoff<0)||p.alphaMode||!p.program||!p.program->kernel.singleSkin())return 0;
        const auto& usage=p.constantUsage;
        if(usage.floats.first||usage.floats.count<31+3*end||usage.floats.count>256||usage.booleans.count||usage.integers.count)return 0;
        const auto& definitions=p.program->definitions;
        if(definitions.size()>1||(!definitions.empty()&&definitions[0].reg))return 0;
        const auto control=NorthlightActorDeformation::singleSkinControl(*p.program,p.constants);
        return control[0]==3&&control[1]==1?span:0;
    }
    // Both packets have independently passed pointPaletteSpan. The palette draw
    // binds its own shader, declaration and instance stream. Only indexed
    // primitive addresses and the palette window survive from the native draw.
    static PointPaletteGeometry pointPaletteGeometry(const Replay& p){
        return {reinterpret_cast<uintptr_t>(p.index),p.start,p.count,p.base,
            reinterpret_cast<uintptr_t>(p.stream[0]),p.offset[0],p.stride[0],
            p.boundsWork.paletteFirst,p.boundsWork.paletteSpan};
    }
    // Depth-only opaque casters commute. Keep cutouts and unknown bounds as
    // barriers; group compatible geometry once for all lights and cube faces.
    void pointOrderReplays(){
        if(pointReplayOrderReady||shadowPalette.unsupported())return;
        const bool timed=NorthlightQuality::renderProfile(quality);const auto started=timed?QpcClock::now():0;
        pointReplayOrder.resize(replays.size());pointReplayPalette.resize(replays.size());
        pointReplaySortKeys.resize(replays.size());
#ifdef NORTHLIGHT_REPLAY_STAND
        std::unordered_map<const void*,uintptr_t> resourceOrder;
#endif
        const auto orderKey=[&](const void* resource){
#ifdef NORTHLIGHT_REPLAY_STAND
            // Opaque geometry can commute. Give each resource a rank in capture
            // order so a different heap layout cannot move groups next to barriers.
            if(resource&&NorthlightFrameParityClock::fixedProfileWork())
                return resourceOrder.try_emplace(resource,resourceOrder.size()+1).first->second;
#endif
            return reinterpret_cast<uintptr_t>(resource);
        };
        for(size_t i=0;i<replays.size();++i){
            pointReplayOrder[i]=i;
            const auto& replay=*replays[i];
            const unsigned span=pointPaletteSpan(replay);
            pointReplayPalette[i]={i,span,span?pointPaletteGeometry(replay):PointPaletteGeometry{}};
#ifdef NORTHLIGHT_REPLAY_STAND
            if(span&&NorthlightFrameParityClock::fixedProfileWork()){
                auto& geometry=pointReplayPalette[i].geometry;
                std::get<0>(geometry)=orderKey(replay.index);
                std::get<4>(geometry)=orderKey(replay.stream[0]);
            }
#endif
            pointCold.orderEligible+=span!=0;
        }
        pointCold.orderPackets+=unsigned(replays.size());
        const auto opaque=[&](size_t i){const auto& p=*replays[i];return p.pointBounds.valid&&p.cutoff<=-1.f&&!p.alphaMode;};
        const auto replayKey=[&](size_t i)->PointReplaySortKey{const auto& p=*replays[i];
            // Bias D3D's signed 32-bit base vertex so unsigned key order
            // agrees with the signed order used by DrawIndexedPrimitive.
            return {orderKey(p.originalShader),orderKey(p.shader),orderKey(p.decl),orderKey(p.index),
                orderKey(p.stream[0]),p.offset[0],p.stride[0],orderKey(p.stream[1]),p.offset[1],p.stride[1],
                orderKey(p.stream[2]),p.offset[2],p.stride[2],orderKey(p.stream[3]),p.offset[3],p.stride[3],
                static_cast<uintptr_t>(p.type),p.indexed,uint32_t(p.base)^0x80000000u,p.constantGroup,p.start,i};
        };
        const auto less=[&](size_t a,size_t b){
            const bool pa=pointReplayPalette[a].span!=0,pb=pointReplayPalette[b].span!=0;
            if(pa!=pb)return pa;
            if(!pa)return pointReplaySortKeys[a]<pointReplaySortKeys[b];
            const auto& ga=pointReplayPalette[a].geometry;
            const auto& gb=pointReplayPalette[b].geometry;
            return ga!=gb?ga<gb:a<b;
        };
        for(size_t first=0;first<pointReplayOrder.size();){
            if(!opaque(first)){++first;continue;}
            size_t end=first+1;while(end<pointReplayOrder.size()&&opaque(end))++end;
            if(end-first>1){
                for(size_t i=first;i<end;++i)
                    if(!pointReplayPalette[i].span)pointReplaySortKeys[i]=replayKey(i);
                std::sort(pointReplayOrder.begin()+first,pointReplayOrder.begin()+end,less);
            }
            first=end;
        }
        // Certify repeated geometry once after sorting. Each cube face then
        // compares packet identities without re-reading the bone rows.
        for(size_t n=1;n<pointReplayOrder.size();++n){
            const size_t index=pointReplayOrder[n],previous=pointReplayOrder[n-1];
            auto& packet=pointReplayPalette[index];
            const auto& prior=pointReplayPalette[previous];
            if(!packet.span||packet.span!=prior.span||packet.geometry!=prior.geometry)continue;
            const auto& a=*replays[index];
            const auto& b=*replays[previous];
            const unsigned offset=4*(31+3*a.boundsWork.paletteFirst);
            if(a.constants==b.constants||!std::memcmp(a.constants+offset,b.constants+offset,packet.span*48))
                packet.pose=prior.pose;
        }
        pointReplayCasters.invalidate();
        pointReplayOrderReady=true;if(timed)pointCold.order+=QpcClock::now()-started;
    }
    bool pointPaletteResources(){
        return shadowPalette.ensure(d,frames,[&](UINT bytes){return admitsGrowth("point-palette",bytes);},
            [&](const auto& create){return pointAllocate(pointCold.shaders,create);},
            [&](const auto& create){return pointAllocate(pointCold.shared,create);});
    }
    // Batch only adjacent compatible casters. Unsupported/alpha packets
    // stay barriers; unique geometry needs no sorting or frame-local allocations.
    HRESULT pointPaletteDraw(const std::vector<size_t>& candidates,size_t& at,const float* rows,bool& drawn){
        drawn=false;if(!shadowPalette.canAttempt(frames)||at+1>=candidates.size())return D3D_OK;
        const auto index=candidates[at];const auto& p=*replays[index];
        // The order builder certifies each immutable packet after replayBoundsJoin().
        const auto& packet=pointReplayPalette[index];
        const UINT span=packet.span;
        if(!span)return D3D_OK;
        size_t end=at+1;
        while(end<candidates.size()){
            const auto& next=pointReplayPalette[candidates[end]];
            if(next.span!=span||next.geometry!=packet.geometry)break;
            ++end;
        }
        if(end-at<2||!pointPaletteResources())return D3D_OK;
        drawn=true;
        return shadowPalette.submit(d,p,span,rows,at,end,
            [&](size_t n){return candidates[n];},[&](size_t n){return pointReplayPalette[n].pose;},
            [&](size_t n){return replays[n]->constants;},
            [&](unsigned packets,UINT count){
                ++pointDraws;++pointWork.replay;++pointWork.paletteDraws;pointWork.palettePackets+=packets;pointWork.paletteRegisters+=count*span*3;
                pointWork.paletteRebased+=p.boundsWork.paletteFirst?count:0;pointWork.paletteSavedRegisters+=count*p.boundsWork.paletteFirst*3;
                pointWork.duplicateInstances+=packets-count;pointWork.triangles+=uint64_t(p.count)*count;
                pointBatchesMerged+=packets-1;pointWork.replayMerged+=packets-1;
            });
    }
    static bool pointReplayBatchState(const Replay& first,const Replay& next,bool opaque){
        return next.type==first.type&&next.indexed==first.indexed&&
            next.decl==first.decl&&next.shader==first.shader&&next.originalShader==first.originalShader&&
            next.alphaMode==first.alphaMode&&!std::memcmp(&next.cutoff,&first.cutoff,sizeof(float))&&
            !std::memcmp(next.stream,first.stream,sizeof first.stream)&&
            !std::memcmp(next.offset,first.offset,sizeof first.offset)&&
            !std::memcmp(next.stride,first.stride,sizeof first.stride)&&
            ((opaque&&first.cutoff<=-1.f&&next.cutoff<=-1.f)||
             (next.texture==first.texture&&next.addressU==first.addressU&&next.addressV==first.addressV));
    }
    static bool pointReplayRangesJoin(const Replay& first,const Replay& next){
        const uint64_t end=uint64_t(first.start)+uint64_t(first.count)*3;
        return first.base==next.base&&first.index==next.index&&next.start>=first.start&&
            next.start<=end&&(next.start-first.start)%3==0;
    }
#include "world_shadow_index_pairs.inl"
#ifdef NORTHLIGHT_REPLAY_STAND
    static bool pointIndexAdmissionReuse(){
        static const bool enabled=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_POINT_INDEX_ADMISSION_REUSE");
            return value&&value[0]=='1'&&value[1]=='\0';
        }();
        return enabled;
    }
#endif
    bool pointIndexRangeEligible(size_t index){
#ifdef NORTHLIGHT_REPLAY_STAND
        // pointOrderReplays runs after the bounds join and reseeds these
        // entries when the captured replay list changes for the next frame.
        auto& admission=pointReplayPalette[index].indexAdmission;
        if(pointIndexAdmissionReuse()&&admission!=PointIndexAdmission::Unknown){
            ++pointCold.indexAdmissionHits;
            return admission==PointIndexAdmission::Accepted;
        }
        ++pointCold.indexAdmissionTests;
        const bool eligible=shadowIndexPairEligible(*replays[index]);
        admission=eligible?PointIndexAdmission::Accepted:PointIndexAdmission::Rejected;
        return eligible;
#else
        return shadowIndexPairEligible(*replays[index]);
#endif
    }
    static bool pointIndexBatchesEnabled(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool enabled=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_POINT_INDEX_BATCHES");
            return !(value&&value[0]=='0'&&value[1]=='\0');
        }();
        return enabled;
#else
        return true;
#endif
    }
    ShadowIndexPair pointReplayIndexBatch(const std::vector<size_t>& candidates,size_t at,bool opaque){
        if(!pointReplayOrderReady||memoryPressure||candidates.size()-at<3)return {};
        const size_t firstIndex=candidates[at];
        const auto& first=*replays[firstIndex];
        if(pointReplayPalette[firstIndex].span||!pointIndexRangeEligible(firstIndex))return {};
        ShadowIndexPlan plan;
        if(!plan.append(first,pointPrimitiveLimit))return {};
        while(plan.count<plan.parts.size()&&at+plan.count<candidates.size()){
            const size_t index=candidates[at+plan.count];
            const auto& next=*replays[index];
            if(pointReplayPalette[index].span||next.constantGroup!=first.constantGroup||
               !pointIndexRangeEligible(index)||!pointReplayBatchState(first,next,opaque)||
               !plan.append(next,pointPrimitiveLimit))break;
        }
        if(!plan.gap||plan.count<3)return {};
        // Do not split a following contiguous run to fill the upload.
        if(at+plan.count<candidates.size()){
            const auto& next=*replays[candidates[at+plan.count]];
            if(pointReplayRangesJoin(*plan.last,next)&&pointReplayBatchState(*plan.last,next,opaque))return {};
        }
        return loadShadowIndexBatch(plan);
    }
    ShadowIndexPair pointReplayPair(const std::vector<size_t>& candidates,size_t at,bool opaque){
        if(!pointReplayOrderReady||memoryPressure||at+1>=candidates.size())return {};
        const size_t a=candidates[at],b=candidates[at+1];
        const auto& first=*replays[a];const auto& next=*replays[b];
        if(pointReplayPalette[a].span||pointReplayPalette[b].span||
           first.constantGroup!=next.constantGroup||!pointReplayBatchState(first,next,opaque))return {};
        if(!pointIndexRangeEligible(a)||!pointIndexRangeEligible(b))return {};
        const auto run=admittedShadowIndexPairRange(first,next);
        if(!run.count)return {};
        if(at+2<candidates.size()){
            const auto& after=*replays[candidates[at+2]];
            if(pointReplayRangesJoin(next,after)&&pointReplayBatchState(next,after,opaque))return {};
        }
        return loadShadowIndexPair(first,next,run);
    }
#ifdef NORTHLIGHT_REPLAY_STAND
    void auditPointReplayFace(const std::vector<size_t>& candidates,unsigned frame,unsigned face,bool skipOpaque)const{
        if(candidates.empty())return;
        unsigned opaque=0,cutout=0,unknown=0,single=0,weighted=0,singleRuns=0,bones=0;
        unsigned reindexPairs=0;
        size_t reindexWords=0;
        const Replay* previous=nullptr;
        for(size_t index:candidates){
            const auto& p=*replays[index];
            if(p.cutoff>-1.f||p.alphaMode){++cutout;bones=0;previous=nullptr;continue;}
            if(!p.pointBounds.valid){++unknown;bones=0;previous=nullptr;continue;}
            ++opaque;
            const unsigned span=pointPaletteSpan(p);
            if(span){
                ++single;
                if(!bones||bones+span>82){++singleRuns;bones=0;}
                bones+=span;
            }else{
                bones=0;
                using Kernel=NorthlightActorDeformation::Kernel;
                if(p.program&&p.indexed&&p.type==D3DPT_TRIANGLELIST&&p.projectionKind==2){
                    switch(p.program->kernel.evaluator){
                    case Kernel::Weighted: case Kernel::WeightedR0:
                    case Kernel::WeightedCommuted: case Kernel::WeightedR0Commuted: ++weighted;break;
                    default: break;
                    }
                }
            }
            if(previous&&previous->gpuCached&&p.gpuCached&&p.index&&p.indexed&&p.type==D3DPT_TRIANGLELIST&&
               p.count&&previous->count&&pointReplayBatchState(*previous,p,skipOpaque)&&
               previous->constantGroup==p.constantGroup){
                if(!pointReplayRangesJoin(*previous,p)){++reindexPairs;reindexWords+=size_t(p.count)*3;}
            }
            previous=&p;
        }
        logf("POINT_BATCH_AUDIT rendererFrame=%u slot=%u face=%u packets=%zu opaque=%u cutout=%u unknownBounds=%u single=%u singleRuns=%u weighted=%u reindexPairs=%u reindexTailWords=%zu",
            frame,pointIndex,face,candidates.size(),opaque,cutout,unknown,single,singleRuns,weighted,reindexPairs,reindexWords);
    }
#endif
    static bool pointEffectivePose(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool enabled=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_POINT_EFFECTIVE_POSE");
            return !(value&&value[0]=='0'&&value[1]=='\0');
        }();
        return enabled;
#else
        return true;
#endif
    }
    bool samePointRigidPose(const Replay& first,const Replay& next)const{
        if(!pointEffectivePose()||first.shadowSkinned||next.shadowSkinned||
           first.projectionKind!=2||next.projectionKind!=2||!first.gpuCached||!next.gpuCached||
           !(first.cutoff<=-1.f)||first.alphaMode||!first.shader)return false;
        // The caller has matched both shader handles. Re-registration can
        // replace the record while an older captured shader is still alive.
        const auto shader=captureShaders.find(first.originalShader);
        return shader!=captureShaders.end()&&shader->second.replacement==first.shader&&
            shader->second.pointRigidRows.sameBanks(first,next);
    }
    PointReplayRun pointReplayRun(const std::vector<size_t>& candidates,size_t& at,bool opaque){
        const auto& first=*replays[candidates[at++]];PointReplayRun run{first.start,first.count,first.min,first.vertices};
        if(!first.indexed||first.type!=D3DPT_TRIANGLELIST)return run;
        while(at<candidates.size()){
            const size_t index=candidates[at];const auto& next=*replays[index];
            // Identical triangles write identical depth. Their union is one
            // contiguous triangle list only on the same three-index boundary.
            const uint64_t end=uint64_t(run.start)+uint64_t(run.count)*3;
            if(next.start<run.start||next.start>end||(next.start-run.start)%3)break;
            if(next.base!=first.base||next.index!=first.index||!pointReplayBatchState(first,next,opaque))break;
            // Groups identify consecutive captures. Face culling and opaque
            // sorting can reunite equal poses from different capture groups.
            const bool crossGroup=next.constantGroup!=first.constantGroup;
            const bool differentBanks=crossGroup&&!NorthlightReplayPoses::sameCaptured(first,next);
            if(differentBanks&&(next.start!=end||!samePointRigidPose(first,next)))break;
            const UINT low=std::min(run.min,next.min);const uint64_t high=std::max(uint64_t(run.min)+run.vertices,uint64_t(next.min)+next.vertices);
            const uint64_t count=std::max<uint64_t>(run.count,uint64_t(next.start-run.start)/3+next.count);
            if(high>UINT_MAX||count>UINT_MAX||(pointPrimitiveLimit&&count>pointPrimitiveLimit))break;
#ifdef NORTHLIGHT_REPLAY_STAND
            pointCold.rigidPoseMerges+=differentBanks;
#endif
            pointWork.crossGroupMerged+=crossGroup;
            run.count=UINT(count);run.min=low;run.vertices=UINT(high-low);++at;++pointBatchesMerged;++pointWork.replayMerged;
        }
        return run;
    }

    static bool pointReplayStateReuse(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool reuse=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_POINT_REPLAY_STATE_REUSE");
            return !(value&&value[0]=='0'&&value[1]=='\0');
        }();
        return reuse;
#else
        return true;
#endif
    }
    template<class Check>
    bool submitPointReplayFace(NorthlightShadowReplay::Pass& replayPass,
                               const std::vector<size_t>& candidates,
                               const float* matrix,[[maybe_unused]] unsigned rendererFrame,[[maybe_unused]] unsigned face,
                               const Check& check){
        if(candidates.empty())return true;
        // Each face clears texture zero on exit and supplies its own projection.
        replayPass.beginFace(pointReplayStateReuse());
        ++pointCold.replayFaces;
        float rows[16];NorthlightWorldMath::replayProjection(context.inverseView,matrix,rows);
        // LocalShadowPS reads patched TEXCOORD7 and writes perspective z/w;
        // the directional replay PS assumes orthographic w=1 and is invalid here.
#ifdef NORTHLIGHT_REPLAY_STAND
        if(pointShadowAuditEnabled()&&profileSampled())auditPointReplayFace(candidates,rendererFrame,face,replayPass.skipping());
#endif
        const char* failure="cube animated draw";
        const auto failed=[&](HRESULT hr,const char* operation){failure=operation;return hr;};
        const auto submit=[&](size_t& at)->HRESULT {
            const auto& p=replays[candidates[at]];
            bool instanced=false;HRESULT hr=pointPaletteDraw(candidates,at,rows,instanced);
            if(FAILED(hr))return failed(hr,"cube palette replay");
            if(instanced){replayPass.afterPalette();return D3D_OK;}
            ShadowIndexPair pair;
            if(pointIndexBatchesEnabled())pair=pointReplayIndexBatch(candidates,at,replayPass.skipping());
            if(!pair.buffer)pair=pointReplayPair(candidates,at,replayPass.skipping());
            hr=replayPass.bindGeometry(*p,&pair);
            if(FAILED(hr))return failed(hr,"cube replay geometry state");
            const size_t first=at;
            const auto run=pair.buffer?pair.run:pointReplayRun(candidates,at,replayPass.skipping());
            const auto uploadFloats=NorthlightReplayPoses::weightedRunUploadRange(p->constantUsage,
                pair.buffer?pair.packets:at-first,[&](size_t i){return NorthlightReplayPoses::weightedPacketPaletteEnd(*replays[candidates[first+i]]);});
            if(!replayPass.preparePose(*p,rows,matrix,uploadFloats))return failed(E_FAIL,"cube replay pose");
            unsigned constantCalls=0,constantBytes=0;
            if(!replayPass.writeConstants(*p,NorthlightShadowReplay::PointConstants,
                [&](HRESULT result,const char* stage){
                    if(SUCCEEDED(result))return true;
                    hr=failed(result,stage);
                    return false;
                },[&](auto,size_t bytes){++constantCalls;constantBytes+=unsigned(bytes);}))return hr;
            pointCold.constantCalls+=constantCalls;
            pointCold.constantBytes+=constantBytes;
            pointCold.weightedConstantDraws+=uploadFloats.count!=0&&uploadFloats.count<p->constantUsage.floats.count;
            if(FAILED(hr=replayPass.bindMaterial(*p)))return failed(hr,"cube replay material state");
            hr=replayPass.draw(*p,run);
            if(pair.buffer&&SUCCEEDED(hr)){
                at+=pair.packets;
                pointBatchesMerged+=pair.packets-1;
                pointWork.replayMerged+=pair.packets-1;
                if(pair.packets==2)++pointIndexedPairDraws;
                else {++pointIndexedBatchDraws;pointIndexedBatchPackets+=pair.packets;}
            }
            if(SUCCEEDED(hr)){++pointDraws;++pointWork.replay;pointWork.triangles+=run.count;pointCold.fixedDraws+=NorthlightShadowAlpha::fixed(p->alphaMode);}
            return hr;
        };
        const HRESULT replayResult=NorthlightShadowAlpha::drawSequence(d,candidates.size(),
            [&](size_t at){return replays[candidates[at]]->alphaMode;},
            [&](size_t at,auto draw){++pointCold.alphaScopes;return shadowReplayDraw(*replays[candidates[at]],draw);},
            submit);
        return check(replayResult,failure);
    }
