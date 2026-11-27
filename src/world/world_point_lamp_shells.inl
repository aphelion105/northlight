    using PointReplayRuns=std::vector<std::pair<UINT,UINT>>;
    using PointLampSplits=std::map<size_t,PointReplayRuns>;
    struct PointLampReplay {
        Batch placed;
        const NorthlightEquipmentLights::Tracker::Entry* native=nullptr;
    };
    NorthlightWmoLampShadow::SourceIndex pointLampSources;
    struct NativeShellMemo {
        std::weak_ptr<const NorthlightDrawSnapshot::Mesh> mesh;
        std::weak_ptr<const NorthlightNativeFixtures::Catalog> catalog;
        std::shared_ptr<const NorthlightActorDeformation::Program> program;
        std::array<float,1024> constants{};std::array<float,16> inverse{};
        std::array<float,3> point{};std::uint64_t declaration=0;unsigned tag=0,source=0;
        NorthlightNativeFixtureShadow::Prepared prepared;
        NorthlightDrawSnapshot::Mesh ownedMesh;
        Batch::LampTriangles local,worldTriangles;
        std::vector<NorthlightActorDeformation::Position> world;
        bool matched=false;PointReplayRuns runs;
    };
    std::array<NativeShellMemo,32> nativeShellMemo;unsigned nativeShellNext=0;
    bool pointOwnNativeShell(const Replay& p,const NorthlightLocalLights::Light& light,PointReplayRuns& remaining,const NorthlightEquipmentLights::Tracker::Entry* entry){
        if(!entry||!nativeFixtures||entry->fixtureTag>nativeFixtures->records.size()||(p.cutoff>=0&&p.cutoff!=.5f)||!p.count||p.count>4096||
           p.mesh().vertexCount>12288||p.count!=p.mesh().primitiveCount||(p.type!=D3DPT_TRIANGLELIST&&p.type!=D3DPT_TRIANGLESTRIP))return false;
        if(!p.pointBounds.valid||!nativeFixtures->records[entry->fixtureTag-1].shellCount)return false;
        for(unsigned k=0;k<3;++k)
            if(std::fabs(light.position[k]-(entry->source.position[k]+entry->emitterOffset[k]))>.0025f)return false;
        for(unsigned k=0;k<3;++k)if(entry->source.position[k]<p.pointBounds.low[k]-.003f||entry->source.position[k]>p.pointBounds.high[k]+.003f)return false;
        const auto program=actorPrograms.find(p.originalShader);const D3DVERTEXELEMENT9* elements=nullptr;UINT elementCount=0;
        if(program==actorPrograms.end()||!program->second||!declarationCache.get(p.decl,elements,elementCount))return false;
        const auto declaration=NorthlightNativeFixtures::hash(elements,elementCount*sizeof(*elements));
        const auto source=unsigned((entry->nativeId-1)&63);
        NativeShellMemo transient,*memo=nullptr;
        if(p.shared)for(auto& cached:nativeShellMemo)if(cached.catalog.lock()==nativeFixtures&&cached.tag==entry->fixtureTag&&cached.source==source&&cached.program==program->second&&cached.declaration==declaration&&cached.mesh.lock()==p.shared){memo=&cached;break;}
        if(memo){if(!memo->prepared.valid())return false;
            if(!std::memcmp(memo->constants.data(),p.constants,sizeof memo->constants)&&!std::memcmp(memo->inverse.data(),context.inverseView,sizeof memo->inverse)&&!std::memcmp(memo->point.data(),entry->source.position,sizeof memo->point)){
                remaining=memo->runs;return memo->matched;}}
        else{
        memo=p.shared?&nativeShellMemo[nativeShellNext++%nativeShellMemo.size()]:&transient;*memo=NativeShellMemo{};
        memo->mesh=p.shared;memo->catalog=nativeFixtures;memo->program=program->second;memo->declaration=declaration;memo->tag=entry->fixtureTag;memo->source=source;
        const auto& record=nativeFixtures->records[entry->fixtureTag-1];
        const NorthlightNativeFixtures::ShellView shell{nativeFixtures->shells.data()+record.shellFirst,record.shellCount};
        const D3DVERTEXELEMENT9* position=nullptr;
        for(UINT i=0;i<elementCount&&elements[i].Stream!=0xff;++i)if(elements[i].Usage==D3DDECLUSAGE_POSITION&&!elements[i].UsageIndex){if(position)return false;position=elements+i;}
        if(!position||position->Stream>=4||position->Method!=D3DDECLMETHOD_DEFAULT)return false;
        const auto& stream=p.mesh().streams[position->Stream];const unsigned width=NorthlightDrawSnapshot::declarationBytes(position->Type);
        if(!width||unsigned(position->Offset)+width>stream.stride||!p.mesh().vertexCount||std::uint64_t(p.mesh().vertexCount-1)*stream.stride+position->Offset+width>stream.bytes.size())return false;
        std::vector<NorthlightActorDeformation::Position> raw(p.mesh().vertexCount);
        for(UINT i=0;i<p.mesh().vertexCount;++i){NorthlightActorDeformation::Four value;
            if(!NorthlightActorDeformation::decodeElement(stream.bytes.data()+std::size_t(i)*stream.stride+position->Offset,position->Type,value))return false;
            raw[i]={value[0],value[1],value[2]};}
        if(!StaticShadowDedup::makeTriangles(raw,p.mesh().indices,p.mesh().indexed,p.count,p.type==D3DPT_TRIANGLESTRIP,memo->local,4096))return false;
        memo->prepared=NorthlightNativeFixtureShadow::prepare(shell,source,memo->local);if(!memo->prepared.valid())return false;
        const auto& excluded=memo->prepared.excluded;
        for(UINT first=0;first<excluded.size();){if(excluded[first]){++first;continue;}UINT end=first+1;while(end<excluded.size()&&!excluded[end])++end;memo->runs.push_back({first,end-first});first=end;}
        NorthlightActorDeformation::Binding bindings[16];std::size_t bindingCount=0;unsigned provided=0,streams=0;
        if(!NorthlightActorDeformation::transientBindings(*program->second,p.mesh(),elements,elementCount,bindings,bindingCount,provided)){memo->prepared={};return false;}
        for(std::size_t i=0;i<bindingCount;++i)streams|=1u<<bindings[i].element.Stream;
        if(!NorthlightNativeFixtureShadow::compact(p.mesh(),p.type==D3DPT_TRIANGLESTRIP,streams,memo->prepared,memo->local,memo->ownedMesh)){memo->prepared={};return false;}
        }
        // Membership survives camera/pose changes. Only the current rigid fit,
        // source anchor and all owned vertices are checked again, in linear work.
        if(!NorthlightActorDeformation::worldPositions(*program->second,memo->ownedMesh,elements,elementCount,p.constants,context.inverseView,memo->world)||
           !StaticShadowDedup::makeTriangles(memo->world,memo->ownedMesh.indices,true,memo->ownedMesh.primitiveCount,false,memo->worldTriangles,4096))return false;
        const bool matched=NorthlightNativeFixtureShadow::verify(memo->prepared,memo->local,memo->worldTriangles,entry->source.position);memo->matched=matched;
        remaining.clear();if(matched)remaining=memo->runs;
        std::copy_n(p.constants,1024,memo->constants.begin());std::copy_n(context.inverseView,16,memo->inverse.begin());std::copy_n(entry->source.position,3,memo->point.begin());
        return matched;
    }
    bool pointOwnShell(const Replay& p,const Batch& shell,PointReplayRuns& remaining){
        if(!shell.lampTriangles||shell.count!=shell.lampTriangles->size()||(p.cutoff>=0&&p.cutoff!=.5f)||!p.count||p.count>4096||
           p.mesh().vertexCount>12288||p.count!=p.mesh().primitiveCount||(p.type!=D3DPT_TRIANGLELIST&&p.type!=D3DPT_TRIANGLESTRIP))return false;
        if(p.pointBounds.valid){const auto& b=p.pointBounds;const float lo[]={shell.boundsLow.x,shell.boundsLow.y,shell.boundsLow.z},hi[]={shell.boundsHigh.x,shell.boundsHigh.y,shell.boundsHigh.z};
            for(unsigned k=0;k<3;++k)if(b.high[k]<lo[k]-.003f||b.low[k]>hi[k]+.003f)return false;}
        const auto program=actorPrograms.find(p.originalShader);const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
        if(program==actorPrograms.end()||!program->second||!declarationCache.get(p.decl,elements,count))return false;
        std::vector<NorthlightActorDeformation::Position> positions;Batch::LampTriangles triangles;
        if(!NorthlightActorDeformation::worldPositions(*program->second,p.mesh(),elements,count,p.constants,context.inverseView,positions)||
           !StaticShadowDedup::makeTriangles(positions,p.mesh().indices,p.mesh().indexed,p.count,p.type==D3DPT_TRIANGLESTRIP,triangles,4096))return false;
        const auto excluded=NorthlightWmoLampShadow::matching(*shell.lampTriangles,triangles);
        if(excluded.empty()||std::none_of(excluded.begin(),excluded.end(),[](bool hit){return hit;}))return false;
        remaining.clear();
        for(UINT first=0;first<excluded.size();){if(excluded[first]){++first;continue;}
            UINT end=first+1;while(end<excluded.size()&&!excluded[end])++end;
            remaining.push_back({first,end-first});first=end;}
        return true;
    }
    PointLampReplay pointLampReplay(const NorthlightLocalLights::Light& light,const std::vector<size_t>& owned){
        PointLampReplay lamp;
        if(owned.empty()){
            lamp.native=equipmentLights.fixture(light.sourceId);
            return lamp;
        }
        lamp.placed=batches[owned.front()];
        if(owned.size()==1)return lamp;
        auto triangles=std::make_shared<Batch::LampTriangles>();
        for(size_t i:owned){
            const auto& part=batches[i];
            triangles->insert(triangles->end(),part.lampTriangles->begin(),part.lampTriangles->end());
            auto& lo=lamp.placed.boundsLow;
            auto& hi=lamp.placed.boundsHigh;
            lo={std::min(lo.x,part.boundsLow.x),std::min(lo.y,part.boundsLow.y),std::min(lo.z,part.boundsLow.z)};
            hi={std::max(hi.x,part.boundsHigh.x),std::max(hi.y,part.boundsHigh.y),std::max(hi.z,part.boundsHigh.z)};
        }
        lamp.placed.count=UINT(triangles->size());
        lamp.placed.lampTriangles=std::move(triangles);
        return lamp;
    }
    bool pointKeepLampReplay(size_t index,const NorthlightLocalLights::Light& light,const PointLampReplay& lamp,PointLampSplits& splits){
        PointReplayRuns remaining;
        if(!(lamp.placed.lampTriangles&&pointOwnShell(*replays[index],lamp.placed,remaining))&&
           !(lamp.native&&pointOwnNativeShell(*replays[index],light,remaining,lamp.native)))return true;
        if(remaining.empty())return false;
        splits.emplace(index,std::move(remaining));
        return true;
    }
