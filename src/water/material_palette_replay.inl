    struct MaterialPaletteDraw {
        const NorthlightModelNormalPalette::Program* program=nullptr;
        IDirect3DVertexDeclaration9* declaration=nullptr;
        IDirect3DVertexBuffer9* ids=nullptr;
        NorthlightModelNormalPalette::Span span;
        unsigned instances=0;
    };

    static bool sameMaterialCaptureState(const Deferred& a,const Deferred& b){
        return !a.splitDepth&&!b.splitDepth&&a.tag==b.tag&&a.vertex==b.vertex&&a.pixel==b.pixel&&a.declaration==b.declaration&&
            a.vertexFirst==b.vertexFirst&&a.vertexCount==b.vertexCount&&
            a.vertexFloatOnly==b.vertexFloatOnly&&a.pixelFloatOnly==b.pixelFloatOnly&&
            (a.vertexFloatOnly||(!std::memcmp(a.vertexInt,b.vertexInt,sizeof a.vertexInt)&&!std::memcmp(a.vertexBool,b.vertexBool,sizeof a.vertexBool)))&&
            (a.pixelFloatOnly||(!std::memcmp(a.pixelInt,b.pixelInt,sizeof a.pixelInt)&&!std::memcmp(a.pixelBool,b.pixelBool,sizeof a.pixelBool)))&&
            a.first==b.first&&a.pixelConstants.size()==b.pixelConstants.size()&&
            !std::memcmp(a.pixelConstants.data(),b.pixelConstants.data(),a.pixelConstants.size()*sizeof(float))&&
            a.normal.get()==b.normal.get()&&a.normalStage==b.normalStage&&a.addressU==b.addressU&&a.addressV==b.addressV&&a.occlusion==b.occlusion&&
            !std::memcmp(&a.viewport,&b.viewport,sizeof a.viewport)&&!std::memcmp(&a.scissor,&b.scissor,sizeof a.scissor)&&
            !std::memcmp(a.states,b.states,sizeof a.states)&&!std::memcmp(a.raster,b.raster,sizeof a.raster)&&a.textures.size()==b.textures.size()&&
            std::equal(a.textures.begin(),a.textures.end(),b.textures.begin(),[](const auto& l,const auto& r){
                return l.stage==r.stage&&l.texture==r.texture&&!std::memcmp(l.sampler,r.sampler,sizeof l.sampler);
            });
    }

    static bool sameMaterialPaletteGeometry(const Deferred& a,const Deferred& b){
        if(a.gpuVertex[0]!=b.gpuVertex[0]||a.gpuIndex!=b.gpuIndex)return false;
        if(a.sourceReady&&b.sourceReady){
            // Both packets passed the live range check. Instance the original
            // range with its stream offset, rather than the compact snapshot.
            const auto& x=a.sourceDraw;const auto& y=b.sourceDraw;
            return a.sourceOffset==b.sourceOffset&&a.sourceStride==b.sourceStride&&x.topology==y.topology&&
                x.base==y.base&&x.minimum==y.minimum&&x.vertices==y.vertices&&x.start==y.start&&x.primitives==y.primitives;
        }
        if(!a.resident||!b.resident)return false;
        const auto& x=*a.mesh;const auto& y=*b.mesh;
        return y.indexed&&y.topology==x.topology&&
            a.placement.baseVertex()==b.placement.baseVertex()&&a.placement.minimumVertex()==b.placement.minimumVertex()&&
            a.placement.indexStart==b.placement.indexStart&&x.vertexCount==y.vertexCount&&x.primitiveCount==y.primitiveCount&&
            x.streams[0].stride==y.streams[0].stride;
    }

    static bool sameMaterialPaletteState(const Deferred& a,const Deferred& b){
        return sameMaterialPaletteGeometry(a,b)&&sameMaterialCaptureState(a,b);
    }

    MaterialPaletteDraw planMaterialPalette(size_t at){
        using namespace NorthlightModelNormalPaletteShader;
        if(materialPalette.unavailable||at+1>=deferred.size())return {};
        const auto& first=*deferred[at];
        if(!first.mesh)return {};
        const auto& mesh=*first.mesh;
#ifdef NORTHLIGHT_PROFILE_BUILD
        static const bool skipSourcePalette=[](){const char* value=std::getenv("NORTHLIGHT_GAME_SKIP_MATERIAL_SOURCE_PALETTE");return value&&value[0]=='1'&&!value[1];}();
        if(first.sourceReady&&skipSourcePalette)return {};
#endif
        if(!(first.resident||first.sourceReady)||!first.gpuVertex[0]||!first.gpuIndex||!mesh.indexed||mesh.topology!=D3DPT_TRIANGLELIST||
           first.vertexFirst||first.vertexCount!=256||first.states[5]||!sameMaterialPaletteState(first,*deferred[at+1]))return {};
        const auto* program=materialPalette.program(first.vertex,first.states[2]?nullptr:first.pixel); // ReplayState's FOGENABLE
        if(!program){++paletteShaderRefusals;return {};}
        const auto span=materialPalette.span(mesh,first.declaration,deferredDeclarations);
        if(!span.count||Base+RowsPerBone*(span.first+span.count)>program->limit)return {};
        const unsigned capacity=(program->limit-Base)/(RowsPerBone*span.count);
        unsigned instances=1;
        // The proof bounds the relocated palette and lists the external float
        // registers read directly by the shader. DEF values come from bytecode.
        while(instances<capacity&&at+instances<deferred.size()){
            const auto& next=*deferred[at+instances];
            if(instances>1&&!sameMaterialPaletteState(first,next))break;
            if(!std::all_of(program->fixedReads.begin(),program->fixedReads.end(),[&](const auto& run){
                const unsigned start=run.first*4;
                return !std::memcmp(first.vertexConstants+start,next.vertexConstants+start,run.count*4*sizeof(float));
            }))break;
            ++instances;
        }
        if(instances<2)return {};
        auto* declaration=materialPalette.declaration(first.declaration,program->semantic,deferredDeclarations);
        auto* ids=declaration?materialPalette.instanceIds(span.count*sizeof(float)):nullptr;
        return ids?MaterialPaletteDraw{program,declaration,ids,span,instances}:MaterialPaletteDraw{};
    }

    HRESULT drawMaterialPalette(size_t at,const MaterialPaletteDraw& batch,bool& submitted){
        using namespace NorthlightModelNormalPaletteShader;
        const auto& first=*deferred[at];
        const auto& mesh=*first.mesh;
        const auto& placement=first.placement;
        const unsigned rows=RowsPerBone*batch.span.count,start=Base+RowsPerBone*batch.span.first;
        float bank[MaxBones*RowsPerBone*4];
        for(unsigned i=0;i<batch.instances;++i)
            std::memcpy(bank+4*rows*i,deferred[at+i]->vertexConstants+4*start,rows*4*sizeof(float));
        const UINT offset=(MaxBones-1-batch.span.first)*sizeof(float),stride=batch.span.count*sizeof(float);
        submitted=SUCCEEDED(d->SetVertexShader(batch.program->instanced.p))&&SUCCEEDED(d->SetVertexDeclaration(batch.declaration))&&
            SUCCEEDED(d->SetVertexShaderConstantF(Base,bank,rows*batch.instances))&&
            SUCCEEDED(d->SetStreamSource(1,batch.ids,offset,stride))&&
            SUCCEEDED(d->SetStreamSourceFreq(1,D3DSTREAMSOURCE_INSTANCEDATA|1))&&
            SUCCEEDED(d->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|batch.instances));
        HRESULT draw=D3D_OK;
        if(submitted){
            if(first.sourceReady){
                const auto& original=first.sourceDraw;
                draw=d->DrawIndexedPrimitive(original.topology,original.base,original.minimum,original.vertices,original.start,original.primitives);
            }else draw=d->DrawIndexedPrimitive(mesh.topology,placement.baseVertex(),placement.minimumVertex(),mesh.vertexCount,placement.indexStart,mesh.primitiveCount);
            if(SUCCEEDED(draw)){
                ++paletteDraws;palettePackets+=batch.instances;
                if(first.sourceReady){++sourceDraws;sourcePaletteSaved+=batch.instances-1;}
            }
        }
        bool restored=SUCCEEDED(d->SetStreamSourceFreq(0,1));
        restored=SUCCEEDED(d->SetStreamSourceFreq(1,1))&&restored;
        restored=SUCCEEDED(d->SetStreamSource(1,nullptr,0,0))&&restored;
        if(!submitted){
            ++paletteBindRefusals;materialPalette.unavailable=true;
            restored=SUCCEEDED(d->SetVertexDeclaration(first.declaration))&&restored;
            restored=SUCCEEDED(d->SetVertexShader(first.vertex))&&restored;
            restored=SUCCEEDED(d->SetVertexShaderConstantF(0,first.vertexConstants,256))&&restored;
        }
        return restored?draw:E_FAIL;
    }
