// Per-frame replay ordering, palette instancing and exact range coalescing for local lights.
    struct PointReplayRun {UINT start,count,min,vertices;};
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
        if(pointReplayOrderReady||pointPaletteUnavailable)return;
        const bool timed=NorthlightQuality::renderProfile(quality);const auto started=timed?QpcClock::now():0;
        pointReplayOrder.resize(replays.size());pointReplayPalette.resize(replays.size());
        pointReplaySortKeys.resize(replays.size());
        for(size_t i=0;i<replays.size();++i){
            pointReplayOrder[i]=i;
            const auto& replay=*replays[i];
            const unsigned span=pointPaletteSpan(replay);
            pointReplayPalette[i]={i,span,span?pointPaletteGeometry(replay):PointPaletteGeometry{}};
            pointCold.orderEligible+=span!=0;
        }
        pointCold.orderPackets+=unsigned(replays.size());
        const auto opaque=[&](size_t i){const auto& p=*replays[i];return p.pointBounds.valid&&p.cutoff<=-1.f&&!p.alphaMode;};
        const auto replayKey=[&](size_t i)->PointReplaySortKey{const auto& p=*replays[i];
            // Bias D3D's signed 32-bit base vertex so unsigned key order
            // agrees with the signed order used by DrawIndexedPrimitive.
            return {reinterpret_cast<uintptr_t>(p.originalShader),reinterpret_cast<uintptr_t>(p.shader),
                reinterpret_cast<uintptr_t>(p.decl),reinterpret_cast<uintptr_t>(p.index),
                reinterpret_cast<uintptr_t>(p.stream[0]),p.offset[0],p.stride[0],
                reinterpret_cast<uintptr_t>(p.stream[1]),p.offset[1],p.stride[1],
                reinterpret_cast<uintptr_t>(p.stream[2]),p.offset[2],p.stride[2],
                reinterpret_cast<uintptr_t>(p.stream[3]),p.offset[3],p.stride[3],
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
        pointReplayGroupsReady=false;pointReplayOrderReady=true;if(timed)pointCold.order+=QpcClock::now()-started;
    }
    bool pointPaletteResources(){
        if(pointPaletteIds)return true;if(pointPaletteUnavailable||frames<pointPaletteRetryFrame)return false;
        pointPaletteRetryFrame=frames+120;D3DCAPS9 caps{};
        if(FAILED(d->GetDeviceCaps(&caps)))return false;
        if(caps.MaxStreams<2||caps.VertexShaderVersion<D3DVS_VERSION(3,0)){pointPaletteUnavailable=true;return false;}
        if(!admitsGrowth("point-palette",84*sizeof(float)))return false;
        if(!pointPaletteVS&&(FAILED(pointAllocate(pointCold.shaders,[&]{return d->CreateVertexShader(kLocalPaletteShadowVSShader,&pointPaletteVS);}))||!pointPaletteVS)){drop(pointPaletteVS);return false;}
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},
            {0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{1,0,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};
        if(!pointPaletteDecl&&(FAILED(d->CreateVertexDeclaration(elements,&pointPaletteDecl))||!pointPaletteDecl)){drop(pointPaletteDecl);return false;}
        NorthlightDrawSnapshot::Ref<IDirect3DVertexBuffer9> ids;
        if(FAILED(pointAllocate(pointCold.shared,[&]{return d->CreateVertexBuffer(84*sizeof(float),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,ids.out(),nullptr);})))return false;
        void* data=nullptr;if(!ids.p)return false;
        if(FAILED(ids->Lock(0,84*sizeof(float),&data,NorthlightUpload::FreshBufferLock))){if(data)ids->Unlock();return false;}
        if(!data){ids->Unlock();return false;}for(unsigned i=0;i<84;++i)static_cast<float*>(data)[i]=float(3*i);
        if(FAILED(ids->Unlock())){ids->Unlock();return false;}pointPaletteIds=ids.p;ids.p=nullptr;pointPaletteRetryFrame=0;return true;
    }
    // Batch only adjacent compatible casters. Unsupported/alpha packets
    // stay barriers; unique geometry needs no sorting or frame-local allocations.
    HRESULT pointPaletteDraw(const std::vector<size_t>& candidates,size_t& at,const float* rows,bool& drawn){
        drawn=false;if(pointPaletteUnavailable||frames<pointPaletteRetryFrame||at+1>=candidates.size())return D3D_OK;
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
        const UINT capacity=82/span;
        const float opaque[]={0,0,0,-1};HRESULT result=d->SetVertexDeclaration(pointPaletteDecl);
        if(SUCCEEDED(result))result=d->SetVertexShader(pointPaletteVS);
        if(SUCCEEDED(result))result=d->SetVertexShaderConstantF(0,rows,4);
        const float paletteInfo[]={float(3*p.boundsWork.paletteFirst),0,0,0};
        if(SUCCEEDED(result))result=d->SetVertexShaderConstantF(252,paletteInfo,1);
        if(SUCCEEDED(result))result=d->SetTexture(0,nullptr);
        if(SUCCEEDED(result))result=d->SetPixelShaderConstantF(0,opaque,1);
        if(SUCCEEDED(result))result=d->SetStreamSource(0,p.stream[0],p.offset[0],p.stride[0]);
        if(SUCCEEDED(result))result=d->SetIndices(p.index);
        if(SUCCEEDED(result))result=d->SetStreamSource(1,pointPaletteIds,0,span*sizeof(float));
        if(SUCCEEDED(result))result=d->SetStreamSourceFreq(1,D3DSTREAMSOURCE_INSTANCEDATA|1);
        const size_t poseBytes=size_t(span)*48;
        size_t previousPose=SIZE_MAX;
        while(at<end&&SUCCEEDED(result)){
            const size_t firstPacket=at;
            UINT count=0;
            float palette[248*4];
            while(at<end){
                const size_t packetIndex=candidates[at],pose=pointReplayPalette[packetIndex].pose;
                // Same opaque indexed geometry, projection and used bone rows:
                // a material replay of this pose writes the identical depth.
                if(previousPose==pose){
                    ++at;
                    continue;
                }
                if(count==capacity)break;
                std::memcpy(palette+count*span*12,replays[packetIndex]->constants+(31+3*p.boundsWork.paletteFirst)*4,poseBytes);
                previousPose=pose;
                ++count;
                ++at;
            }
            const unsigned packets=unsigned(at-firstPacket);
            result=d->SetVertexShaderConstantF(4,palette,count*span*3);
            if(SUCCEEDED(result))result=d->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|count);
            if(SUCCEEDED(result))result=d->DrawIndexedPrimitive(p.type,p.base,p.min,p.vertices,p.start,p.count);
            if(SUCCEEDED(result)){++pointDraws;++pointWork.replay;++pointWork.paletteDraws;pointWork.palettePackets+=packets;pointWork.paletteRegisters+=count*span*3;pointWork.paletteRebased+=p.boundsWork.paletteFirst?count:0;pointWork.paletteSavedRegisters+=count*p.boundsWork.paletteFirst*3;
                pointWork.duplicateInstances+=packets-count;
                pointWork.triangles+=uint64_t(p.count)*count;pointBatchesMerged+=packets-1;pointWork.replayMerged+=packets-1;}
        }
        for(unsigned stream=0;stream<2;++stream){auto hr=d->SetStreamSourceFreq(stream,1);if(FAILED(hr))hr=d->SetStreamSourceFreq(stream,1);if(FAILED(hr))result=hr;}
        drawn=true;return result;
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
            if(next.type!=first.type||next.indexed!=first.indexed||next.base!=first.base||
               next.decl!=first.decl||next.shader!=first.shader||next.originalShader!=first.originalShader||next.index!=first.index||
               next.alphaMode!=first.alphaMode||std::memcmp(&next.cutoff,&first.cutoff,sizeof(float))||
               std::memcmp(next.stream,first.stream,sizeof first.stream)||std::memcmp(next.offset,first.offset,sizeof first.offset)||std::memcmp(next.stride,first.stride,sizeof first.stride))break;
            if(!(opaque&&first.cutoff<=-1.f&&next.cutoff<=-1.f)&&
               (next.texture!=first.texture||next.addressU!=first.addressU||next.addressV!=first.addressV))break;
            // Groups identify consecutive captures. Face culling and opaque
            // sorting can reunite equal poses from different capture groups.
            if(!NorthlightReplayPoses::sameCaptured(first,next))break;
            const UINT low=std::min(run.min,next.min);const uint64_t high=std::max(uint64_t(run.min)+run.vertices,uint64_t(next.min)+next.vertices);
            const uint64_t count=std::max<uint64_t>(run.count,uint64_t(next.start-run.start)/3+next.count);
            if(high>UINT_MAX||count>UINT_MAX||(pointPrimitiveLimit&&count>pointPrimitiveLimit))break;
            pointWork.crossGroupMerged+=next.constantGroup!=first.constantGroup;
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
    bool submitPointReplayFace(NorthlightReplayDrawState::Cache& replayBindings,const std::vector<size_t>& candidates,
                               const float* matrix,[[maybe_unused]] unsigned rendererFrame,[[maybe_unused]] unsigned face){
        if(candidates.empty())return true;
        if(!pointReplayStateReuse())replayBindings.invalidate();
        // Each face clears texture zero on exit and supplies its own projection.
        replayBindings.beginMaterialPass();
        ++pointCold.replayFaces;
        float rows[16];NorthlightWorldMath::replayProjection(context.inverseView,matrix,rows);
        NorthlightReplayPoses::Pass<BOOL> poseConstants;
        // LocalShadowPS reads patched TEXCOORD7 and writes perspective z/w;
        // the directional replay PS assumes orthographic w=1 and is invalid here.
        const char* failure="cube animated draw";
        const auto failed=[&](HRESULT hr,const char* operation){failure=operation;return hr;};
        const auto submit=[&](size_t& at)->HRESULT {
            const auto& p=replays[candidates[at]];
            bool instanced=false;HRESULT hr=pointPaletteDraw(candidates,at,rows,instanced);
            if(FAILED(hr))return failed(hr,"cube palette replay");
            if(instanced){replayBindings.invalidate();poseConstants.reset();return D3D_OK;}
            hr=replayBindings.geometry(*p);
            if(FAILED(hr))return failed(hr,"cube replay geometry state");
            const size_t first=at;
            const auto run=pointReplayRun(candidates,at,replayBindings.skipping());
            NorthlightShaderConstants::Range uploadFloats{};
            if(p->constantUsage.weightedFloats.count){
                // A merged range consumes only triangles from these packets.
                // A larger submesh can reference bones beyond the first one.
                unsigned paletteEnd=0;
                for(size_t k=first;k<at;++k){
                    const auto& part=*replays[candidates[k]];
                    if(!part.gpuCached||!part.pointBounds.valid||!part.boundsWork.weightedPaletteEnd){paletteEnd=0;break;}
                    paletteEnd=std::max(paletteEnd,part.boundsWork.weightedPaletteEnd);
                }
                uploadFloats=NorthlightReplayPoses::weightedUploadRange(p->constantUsage,paletteEnd);
            }
            if(!poseConstants.prepare(*p,rows,matrix,uploadFloats))return failed(E_FAIL,"cube replay pose");
            const float* desired=poseConstants.desired();
            auto f=poseConstants.floats;auto b=poseConstants.booleans;auto i=poseConstants.integers;
            if(f.count&&FAILED(hr=d->SetVertexShaderConstantF(f.first,desired+4*f.first,f.count)))return failed(hr,"cube replay float constants");
            if(b.count&&FAILED(hr=d->SetVertexShaderConstantB(b.first,p->bools+b.first,b.count)))return failed(hr,"cube replay bool constants");
            if(i.count&&FAILED(hr=d->SetVertexShaderConstantI(i.first,p->ints+4*i.first,i.count)))return failed(hr,"cube replay integer constants");
            pointCold.constantCalls+=unsigned(f.count!=0)+unsigned(b.count!=0)+unsigned(i.count!=0);
            pointCold.constantBytes+=16*(f.count+i.count)+sizeof(BOOL)*b.count;
            pointCold.weightedConstantDraws+=uploadFloats.count!=0&&uploadFloats.count<p->constantUsage.floats.count;
            if(FAILED(hr=replayBindings.material(*p)))return failed(hr,"cube replay material state");
            hr=p->indexed?d->DrawIndexedPrimitive(p->type,p->base,run.min,run.vertices,run.start,run.count):d->DrawPrimitive(p->type,run.start,run.count);
            if(SUCCEEDED(hr)){++pointDraws;++pointWork.replay;pointWork.triangles+=run.count;pointCold.fixedDraws+=NorthlightShadowAlpha::fixed(p->alphaMode);}
            return hr;
        };
        const HRESULT replayResult=NorthlightShadowAlpha::drawSequence(d,candidates.size(),
            [&](size_t at){return replays[candidates[at]]->alphaMode;},
            [&](size_t at,auto draw){++pointCold.alphaScopes;return shadowReplayDraw(*replays[candidates[at]],draw);},
            submit);
        return pointCheck(replayResult,failure);
    }
