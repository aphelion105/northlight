// Included inside NorthlightModelNormals. Keep instancing within one ordered
// deferred replay batch and leave existing draw coalescing in charge first.
    struct PaletteWork {
        unsigned draws=0,packets=0,registers=0,failures=0,planned=0,linkedDraws=0,linkedPackets=0;
        unsigned inheritedDraws=0,skippedBinds=0,liftedDraws=0,liftedPackets=0;
    } paletteWork;
    struct PaletteDraw {
        const NorthlightModelNormalPalette::Program* program=nullptr;
        IDirect3DVertexDeclaration9* declaration=nullptr;
        IDirect3DVertexBuffer9* ids=nullptr;
        NorthlightModelNormalPalette::Span span;
        unsigned instances=0;
    };

    static bool samePaletteGeometry(const Pending& a,const Pending& b){
        const auto& m=a.geometry();
        const auto& n=b.geometry();
        return a.resident&&b.resident&&a.gpuVertex[0]&&a.gpuIndex&&!a.streamed&&!b.streamed&&!a.redundant&&!b.redundant&&
            m.indexed&&n.indexed&&m.topology==D3DPT_TRIANGLELIST&&n.topology==m.topology&&
            a.gpuIndex==b.gpuIndex&&!std::memcmp(a.gpuVertex,b.gpuVertex,sizeof a.gpuVertex)&&
            m.vertexCount==n.vertexCount&&m.primitiveCount==n.primitiveCount&&m.streams[0].stride==n.streams[0].stride&&
            a.placement.vertexBase==b.placement.vertexBase&&a.placement.indexStart==b.placement.indexStart&&
            a.placement.absoluteIndices==b.placement.absoluteIndices;
    }

    static bool paletteEnabled(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool disabled=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_NO_NORMAL_PALETTE");
            return value&&std::strcmp(value,"1")==0;
        }();
        return !disabled;
#elif defined(NORTHLIGHT_MODEL_NORMALS_TEST)
        return true;
#else
        static const bool enabled=[] {
            const char* value=std::getenv("NORTHLIGHT_NORMAL_PALETTE");
            return value&&(!std::strcmp(value,"1")||!std::strcmp(value,"2"));
        }();
        return enabled;
#endif
    }

    static bool retainPaletteBindings(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool retain=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_NO_NORMAL_PALETTE_BINDING_REUSE");
            return !value||std::strcmp(value,"1")!=0;
        }();
        return retain;
#else
        return true;
#endif
    }

    bool restorePaletteStreams(){
        bool restored=SUCCEEDED(d->SetStreamSourceFreq(0,1));
        restored=SUCCEEDED(d->SetStreamSourceFreq(1,1))&&restored;
        return SUCCEEDED(d->SetStreamSource(1,nullptr,0,0))&&restored;
    }

    void keepPaletteSpan(Pending& packet,const Vertex* vertex,const Pixel* pixel,IDirect3DVertexDeclaration9* declaration){
        if(!paletteEnabled()||palette.unavailable||pending.empty())return;
        auto& previous=*pending.back();
        if(previous.vertex->shader!=vertex->shader||previous.pixel!=pixel||previous.declaration!=declaration||!samePaletteGeometry(previous,packet))return;
        // Both packets lease exactly the same GPU bytes. Preserve only the
        // palette interval before the current CPU snapshot is released.
        if(!previous.paletteSpan.count)previous.paletteSpan=palette.span(packet.geometry(),declaration,declarations);
        packet.paletteSpan=previous.paletteSpan;
    }

    bool liftPaletteRows() const {
#if defined(NORTHLIGHT_REPLAY_STAND)
        static const bool enabled=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_NORMAL_INSTANCE_ROWS");
            return value&&std::strcmp(value,"1")==0;
        }();
        return enabled;
#elif defined(NORTHLIGHT_MODEL_NORMALS_TEST)
        return instanceRowsEnabled;
#else
        static const bool enabled=[] {
            const char* value=std::getenv("NORTHLIGHT_NORMAL_PALETTE");
            return value&&std::strcmp(value,"2")==0;
        }();
        return enabled;
#endif
    }

    PaletteDraw planPaletteDraw(size_t at){
        using namespace NorthlightModelNormalPaletteShader;
        if(!paletteEnabled()||palette.unavailable||at+1>=pending.size())return {};
        const auto& first=*pending[at];
        const auto& second=*pending[at+1];
        if(first.vertex->shader!=second.vertex->shader||
           first.pixel!=second.pixel||first.declaration!=second.declaration||!samePaletteGeometry(first,second))return {};
        auto span=first.paletteSpan;
        if(!span.count)span=palette.span(first.geometry(),first.declaration,declarations);
        if(!span.count)return {};
        // Normal replay disables fixed-function fog. The full receiver can
        // still carry lighting outputs which its pixel shader never reads.
        auto* linkedPixel=first.vertex->compact&&!first.pixel->reads.sampledSamplers?nullptr:first.pixel->shader;
        const auto* program=palette.program(first.vertex->shader,linkedPixel);
        if(!program)return {};
        const unsigned start=Base+RowsPerBone*span.first,end=start+RowsPerBone*span.count;
        const unsigned capturedFirst=first.vertex->first,capturedEnd=capturedFirst+first.vertex->count;
        if(capturedFirst>start||capturedEnd<end||end>program->sourceLimit)return {};
        const auto captured=[&](const auto& runs){
            return std::all_of(runs.begin(),runs.end(),[&](const auto& run){
                return capturedFirst<=run.first&&run.first+run.count<=capturedEnd;
            });
        };
        const auto groupLength=[&](const auto* candidate){
            if(!captured(candidate->fixedReads)||!captured(candidate->instanceReads))return 1u;
            const unsigned packed=RowsPerBone*span.count+candidate->instanceRows;
            const unsigned capacity=(candidate->limit-Base)/packed;
            unsigned count=1;
            while(count<capacity&&at+count<pending.size()){
                const auto& next=*pending[at+count];
                if(!samePaletteGeometry(first,next)||!sameCapture(first,next,candidate))break;
                ++count;
            }
            return count;
        };
        unsigned count=groupLength(program);
        if(count<2&&liftPaletteRows()&&captured(program->fixedReads)&&
           second.vertex->first==capturedFirst&&second.vertex->first+second.vertex->count==capturedEnd){
            // Reuse the GameScene constant layout and its cached variant per VS/PS.
            if(const auto* lifted=palette.program(first.vertex->shader,linkedPixel,true);
               lifted&&lifted->instanceRows){
                count=groupLength(lifted);
                if(count>=2)program=lifted;
            }
        }
        if(count<2)return {};
        auto* declaration=palette.declaration(first.declaration,program->semantic,declarations);
        auto* ids=declaration?palette.instanceIds((span.count+program->instanceRows/RowsPerBone)*sizeof(float)):nullptr;
        if(!ids)return {};
        ++paletteWork.planned;
        return {program,declaration,ids,span,count};
    }

    HRESULT drawPalette(size_t at,const PaletteDraw& batch,bool& submitted,const Pending* previous,const PaletteDraw& previousBatch,
                        bool retainBindings,bool& streamsBound){
        using namespace NorthlightModelNormalPaletteShader;
        const auto& first=*pending[at];
        const auto& mesh=first.geometry();
        const unsigned rows=RowsPerBone*batch.span.count, start=Base+RowsPerBone*batch.span.first;
        const unsigned shared=batch.program->instanceRows, packed=rows+shared;
        float bank[MaxBones*RowsPerBone*4];
        for(unsigned i=0;i<batch.instances;++i){
            auto* data=bank+4*packed*i;
            const auto* source=pending[at+i]->constants;
            if(shared){
                std::fill_n(data,shared*4,0.f);
                unsigned row=0;
                for(const auto& run:batch.program->instanceReads){
                    std::memcpy(data+row*4,source+run.first*4,run.count*16);
                    row+=run.count;
                }
            }
            std::memcpy(data+4*shared,source+4*start,rows*16);
        }
        const UINT offset=(MaxBones-1+shared/RowsPerBone-batch.span.first)*sizeof(float);
        const UINT stride=packed/RowsPerBone*sizeof(float);
        const bool reuseFixed=previous&&previousBatch.program==batch.program;
        const bool reuseProgram=streamsBound&&reuseFixed;
        const bool reuseLayout=streamsBound&&previousBatch.declaration==batch.declaration;
        const bool reuseStream=streamsBound&&previousBatch.ids==batch.ids&&
            previousBatch.span.first==batch.span.first&&previousBatch.span.count==batch.span.count&&
            previousBatch.program->instanceRows==shared;
        const bool reuseRate=streamsBound&&previousBatch.instances==batch.instances;
        submitted=true;
        for(const auto& run:batch.program->fixedReads){
            const auto* data=first.constants+run.first*4;
            if(reuseFixed&&!std::memcmp(data,previous->constants+run.first*4,run.count*16))continue;
            if(FAILED(d->SetVertexShaderConstantF(run.first,data,run.count))){submitted=false;break;}
        }
        const float bias[]={float(int(start-Base)-int(shared)),0,0,0};
        submitted=submitted&&(!shared||(reuseStream&&reuseFixed)||
            SUCCEEDED(d->SetVertexShaderConstantF(batch.program->biasConstant,bias,1)))&&(reuseProgram||SUCCEEDED(d->SetVertexShader(batch.program->instanced.p)))&&
            (reuseLayout||SUCCEEDED(d->SetVertexDeclaration(batch.declaration)))&&
            SUCCEEDED(d->SetVertexShaderConstantF(Base,bank,packed*batch.instances))&&
            (reuseStream||SUCCEEDED(d->SetStreamSource(1,batch.ids,offset,stride)))&&
            (streamsBound||SUCCEEDED(d->SetStreamSourceFreq(1,D3DSTREAMSOURCE_INSTANCEDATA|1)))&&
            (reuseRate||SUCCEEDED(d->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|batch.instances)));
        HRESULT result=D3D_OK;
        if(submitted){
            result=d->DrawIndexedPrimitive(mesh.topology,first.placement.baseVertex(),first.placement.minimumVertex(),mesh.vertexCount,
                first.placement.indexStart,mesh.primitiveCount);
            if(SUCCEEDED(result)){
                ++paletteWork.draws;
                paletteWork.packets+=batch.instances;
                paletteWork.registers+=packed*batch.instances;
                if(shared){++paletteWork.liftedDraws;paletteWork.liftedPackets+=batch.instances;}
                if(batch.program->materialPixel.p){++paletteWork.linkedDraws;paletteWork.linkedPackets+=batch.instances;}
                paletteWork.inheritedDraws+=unsigned(streamsBound);
                paletteWork.skippedBinds+=unsigned(reuseProgram)+unsigned(reuseLayout)+unsigned(reuseStream)+unsigned(reuseRate)+unsigned(streamsBound);
            }
        }
        // Keep stream frequencies only through a successful instanced draw.
        // Scalar replay, a failed draw and the end of the saved state scope
        // all restore the streams before continuing.
        streamsBound=retainBindings&&submitted&&SUCCEEDED(result);
        if(streamsBound)return result;
        bool restored=restorePaletteStreams();
        if(!submitted){
            // An optional bind may fail before any draw. Restore the ordinary
            // packet, then let the same batch submit each original draw.
            ++paletteWork.failures;
            palette.unavailable=true;
            restored=SUCCEEDED(d->SetVertexDeclaration(first.declaration))&&restored;
            restored=SUCCEEDED(d->SetVertexShader(first.vertex->shader))&&restored;
            restored=SUCCEEDED(d->SetVertexShaderConstantF(first.vertex->first,first.constants+first.vertex->first*4,first.vertex->count))&&restored;
        }
        return restored?result:E_FAIL;
    }
