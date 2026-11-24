#pragma once
#include "device_mirror.h"
#include "game_indexed_ranges.h"
#include "model_normal_palette.h"

// The Device still sees every logical draw. A contiguous run with matching
// geometry and stencil combines only its final backend submissions.
class NorthlightGameIndexedPair {
    using Draw=NorthlightDrawSnapshot::Draw;
    using Mesh=NorthlightDrawSnapshot::Mesh;
    using Palette=NorthlightModelNormalPalette;
    using IndexRanges=NorthlightGameIndexedRanges;
    template<class T> using Ref=NorthlightDrawSnapshot::Ref<T>;
    static constexpr unsigned Base=NorthlightModelNormalPaletteShader::Base;
    static constexpr unsigned Rows=NorthlightModelNormalPaletteShader::RowsPerBone;
    static constexpr unsigned MaxBones=NorthlightModelNormalPaletteShader::MaxBones;
    static constexpr unsigned MaxInstanceConstants=NorthlightModelNormalPaletteShader::MaxInstanceConstants;
    static constexpr auto& Stencil=ExtensionDevice::IndexedStencilKeys;
    using StencilValues=ExtensionDevice::IndexedStencil;
    IDirect3DDevice9* d;
    ExtensionDevice* mirror;
    IDirect3DDevice9* state;
    Palette palette;
    IndexRanges joinedIndices;
    NorthlightCaptureMetadata::Read indexMetadata;
    NorthlightVertexDeclarations::Cache declarations;
    std::shared_ptr<const Mesh> source;
    IDirect3DVertexShader9* sourceShader=nullptr;
    IDirect3DVertexDeclaration9* sourceLayout=nullptr;
    const Palette::Program* program=nullptr;
    std::shared_ptr<const Mesh> firstSource;
    IDirect3DVertexDeclaration9* layout=nullptr;
    IDirect3DVertexDeclaration9* instancedLayout=nullptr;
    IDirect3DVertexBuffer9* instanceIds=nullptr;
    Draw first;
    StencilValues firstStencil{};
    float poses[(256-Base)*4];
    Palette::Span span;
    unsigned pending=0,capacity=0,stencilKind=0;
    IndexRanges::Ranges ranges{};
    unsigned rangeCount=0,rangePrimitives=0;
    UINT streamOffset=0,streamStride=0;
    bool mixedConstants=false,alphaTested=false,joinRanges=false;

    static bool sameDraw(const Draw& a,const Draw& b){
        return a.topology==b.topology&&a.base==b.base&&a.minimum==b.minimum&&a.vertices==b.vertices&&a.start==b.start&&a.primitives==b.primitives;
    }
    bool floats(UINT first,float* values,UINT count){
        const auto before=mirror?mirror->constantMirror()->forwarded:0;
        const bool ok=SUCCEEDED(state->GetVertexShaderConstantF(first,values,count));
        if(mirror&&mirror->constantMirror()->forwarded==before)++mirroredFloatReads;
        else ++backendFloatReads;
        return ok;
    }
    bool stencil(StencilValues& values,bool physical=false){
        if(mirror&&!physical&&mirror->pendingIndexedStencil(values))return true;
        if(mirror&&mirror->knownRenderStates(Stencil,unsigned(values.size()),values.data()))return true;
        for(unsigned i=0;i<values.size();++i){
            const HRESULT hr=mirror?mirror->MirrorDevice::GetRenderState(Stencil[i],&values[i]):d->GetRenderState(Stencil[i],&values[i]);
            if(FAILED(hr))return false;
        }
        return true;
    }
    bool setStencil(const StencilValues& current,const StencilValues& next){
        bool ok=true;
        for(unsigned i=0;i<current.size();++i)if(current[i]!=next[i])ok=retry([&]{return d->SetRenderState(Stencil[i],next[i]);})&&ok;
        return ok;
    }
    template<class Call> static bool retry(Call call){return SUCCEEDED(call())||SUCCEEDED(call());}
    HRESULT draw(const Draw& a){return d->DrawIndexedPrimitive(a.topology,a.base,a.minimum,a.vertices,a.start,a.primitives);}
    unsigned firstRow()const{return Base+Rows*span.first;}
    unsigned rowCount()const{return Rows*span.count;}
    unsigned packedRows()const{return program->instanceRows+rowCount();}
    bool readInstance(float* values){
        for(const auto& run:program->instanceReads){
            if(!floats(run.first,values,run.count))return false;
            values+=run.count*4;
        }
        return true;
    }
    bool writeInstance(const float* values){
        bool ok=true;
        for(const auto& run:program->instanceReads){
            ok=retry([&]{return d->SetVertexShaderConstantF(run.first,values,run.count);})&&ok;
            values+=run.count*4;
        }
        return ok;
    }
    bool captureInstance(unsigned instance){
        float* entry=poses+instance*packedRows()*4;
        std::fill_n(entry,program->instanceRows*4,0.f);
        return readInstance(entry)&&floats(firstRow(),entry+program->instanceRows*4,rowCount());
    }

    HRESULT flushRanges(){
        const unsigned count=rangeCount;
        rangeCount=0;
        StencilValues live{};
        if(!stencil(live,true)){++failures;return E_FAIL;}
        const bool stamped=setStencil(live,firstStencil);
        HRESULT hr=stamped?D3D_OK:E_FAIL;
        bool submitted=false,restored=true;
        if(stamped&&count>1){
            Draw combined=first;
            UINT end=first.minimum+first.vertices;
            bool contiguous=true;
            for(unsigned i=1;i<count;++i){
                combined.minimum=std::min(combined.minimum,ranges[i].minimum);
                end=std::max(end,ranges[i].minimum+ranges[i].vertices);
                contiguous&=ranges[i-1].start+3*ranges[i-1].primitives==ranges[i].start;
            }
            combined.vertices=end-combined.minimum;combined.primitives=rangePrimitives;
            if(contiguous){hr=draw(combined);submitted=true;}
            else if(count>=3){
                Ref<IDirect3DIndexBuffer9> liveIndices;
                if(SUCCEEDED(state->GetIndices(liveIndices.out()))&&liveIndices.p){
                    const NorthlightCaptureMetadata::Request request{liveIndices.p,true};
                    NorthlightCaptureMetadata::Info info{};
                    indexMetadata(&request,&info,1);
                    if(auto* joined=joinedIndices.buffer(liveIndices.p,info,ranges,count,rangePrimitives)){
                        combined.start=0;
                        if(SUCCEEDED(d->SetIndices(joined))){hr=draw(combined);submitted=true;}
                        restored=retry([&]{return d->SetIndices(liveIndices.p);});
                    }
                }
            }
            if(submitted&&SUCCEEDED(hr)){++rangeBatches;rangeSaved+=count-1;}
        }
        if(stamped&&!submitted&&restored){
            Draw original=first;
            for(unsigned i=0;i<count&&SUCCEEDED(hr);++i){
                const auto& range=ranges[i];
                original.minimum=range.minimum;original.vertices=range.vertices;
                original.start=range.start;original.primitives=range.primitives;
                hr=draw(original);++rangeDrained;
            }
        }
        restored=setStencil(firstStencil,live)&&restored;
        if(FAILED(hr)||!restored)++failures;
        return restored?hr:E_FAIL;
    }
    template<class Submit> HRESULT submitRange(const Draw& a,unsigned kind,Submit serial){
        StencilValues stamp{};
        const bool admitted=kind>=2&&a.topology==D3DPT_TRIANGLELIST&&a.primitives&&a.primitives<=IndexRanges::MaxPrimitives&&
            a.vertices&&a.vertices<=UINT_MAX-a.minimum&&a.start<=UINT_MAX-3*a.primitives&&stencil(stamp)&&stamp[0];
        if(rangeCount&&(!admitted||a.base!=first.base||kind!=stencilKind||stamp!=firstStencil||
                        a.primitives>IndexRanges::MaxPrimitives-rangePrimitives)){
            if(const HRESULT hr=flushRanges();FAILED(hr))return hr;
        }
        if(!admitted)return serial();
        if(!rangeCount){
            DWORD blend=TRUE,zwrite=FALSE;UINT frequency0=0,frequency1=0;
            if(FAILED(state->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||blend||
               FAILED(state->GetRenderState(D3DRS_ZWRITEENABLE,&zwrite))||!zwrite||
               FAILED(state->GetStreamSourceFreq(0,&frequency0))||frequency0!=1||
               FAILED(state->GetStreamSourceFreq(1,&frequency1))||frequency1!=1||
               d->GetSoftwareVertexProcessing()||state->GetNPatchMode()!=0)return serial();
            first=a;firstStencil=stamp;stencilKind=kind;rangePrimitives=0;
        }
        ranges[rangeCount++]={a.minimum,a.vertices,a.start,a.primitives};rangePrimitives+=a.primitives;++rangeQueued;
        return rangeCount==IndexRanges::MaxDraws?flushRanges():D3D_OK;
    }

public:
    unsigned queuedDraws=0,batches=0,drained=0,sourceMisses=0,proofMisses=0,failures=0;
    unsigned saved=0,receiverSaved=0,peak=0,mixedSaved=0,constantBarriers=0,alphaSaved=0;
    unsigned stateMisses=0,shaderMisses=0,rangeMisses=0,setupMisses=0;
    unsigned mirroredFloatReads=0,backendFloatReads=0;
    unsigned deferredStencils=0,materializedStencils=0,skippedStencilSets=0;
    unsigned rangeQueued=0,rangeBatches=0,rangeSaved=0,rangeDrained=0;
    const IndexRanges& indexRangeCache()const{return joinedIndices;}
    explicit NorthlightGameIndexedPair(IDirect3DDevice9* device,ExtensionDevice* reader=nullptr,
                                       NorthlightCaptureMetadata::Read metadata=nullptr)
        :d(device),mirror(reader),state(reader?static_cast<IDirect3DDevice9*>(reader):device),palette(device),joinedIndices(device),indexMetadata(metadata){}
    void reset(){clearSource();firstSource.reset();pending=rangeCount=0;palette.reset();joinedIndices.reset();declarations.clear();}
    void clearCounters(){
        queuedDraws=batches=drained=sourceMisses=proofMisses=failures=saved=receiverSaved=peak=mixedSaved=constantBarriers=0;
        stateMisses=shaderMisses=rangeMisses=setupMisses=mirroredFloatReads=backendFloatReads=alphaSaved=0;
        deferredStencils=materializedStencils=skippedStencilSets=0;
        rangeQueued=rangeBatches=rangeSaved=rangeDrained=0;joinedIndices.clearCounters();
    }
    void setMixedConstants(bool enabled){mixedConstants=enabled;}
    void setJoinRanges(bool enabled){joinRanges=enabled&&indexMetadata;}
    bool joiningRanges()const{return joinRanges;}
    void clearSource(){source.reset();sourceShader=nullptr;sourceLayout=nullptr;}
    void noteSource(IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* declaration,const std::shared_ptr<const Mesh>& mesh){
        source=mesh;sourceShader=shader;sourceLayout=declaration;
    }
    bool knownSource()const{return source&&sourceShader&&sourceLayout&&source->sourceIdentity&&source->hasCheckedIndices();}
    std::shared_ptr<const Mesh> pendingSource(const Draw& draw,IDirect3DVertexShader9* shader)const{
        if(!pending||!draw.indexed||shader!=program->source.p||!sameDraw(first,draw))return {};
        return firstSource;
    }
    bool allowsConstants(UINT first,const float* values,UINT count){
        if(rangeCount)return false;
        if(!pending)return true;
        if(!values||first>=256||!count||count>256-first)return false;
        if(!mixedConstants)return first>=Base&&first<program->limit&&count<=program->limit-first;
        // A broad update can include live shared rows. It is only safe to
        // advance them before the deferred draw if their bits are unchanged.
        for(const auto& run:program->fixedReads){
            const unsigned lo=std::max(first,run.first),hi=std::min(first+count,run.first+run.count);
            if(hi<=lo)continue;
            float live[256*4];
            if(!floats(lo,live,hi-lo)||std::memcmp(live,values+(lo-first)*4,(hi-lo)*16)){
                ++constantBarriers;return false;
            }
        }
        return true;
    }
    HRESULT flush(){
        if(rangeCount)return flushRanges();
        if(!pending)return D3D_OK;
        const unsigned instances=pending, rows=rowCount(), packed=packedRows(), shared=program->instanceRows;
        const bool instancing=instances>1;
        const unsigned restoreFirst=instancing?Base:firstRow();
        const unsigned restoreRows=instancing?std::max(firstRow()-Base+rows,instances*packed):rows;
        pending=0;firstSource.reset();
        StencilValues liveStencil{};
        float liveRows[(256-Base)*4],liveInstance[MaxInstanceConstants*4],liveBias[4];
        // Snapshot all state before the temporary raw binds. The mirror keeps
        // the logical game state while the batch binds and restores the device.
        if(!stencil(liveStencil,true)||!floats(restoreFirst,liveRows,restoreRows)||!readInstance(liveInstance)||
           (instancing&&shared&&!floats(program->biasConstant,liveBias,1))){++failures;return E_FAIL;}
        const bool stamped=setStencil(liveStencil,firstStencil);
        bool issued=false,restored=true;
        HRESULT hr=stamped?D3D_OK:E_FAIL;
        if(stamped&&instancing){
            const UINT offset=(MaxBones-1+shared/Rows-span.first)*sizeof(float),stride=packed/Rows*sizeof(float);
            const float bias[]={float(int(firstRow()-Base)-int(shared)),0,0,0};
            issued=SUCCEEDED(d->SetVertexShader(program->instanced.p))&&SUCCEEDED(d->SetVertexDeclaration(instancedLayout))&&
                (!shared||SUCCEEDED(d->SetVertexShaderConstantF(program->biasConstant,bias,1)))&&
                SUCCEEDED(d->SetVertexShaderConstantF(Base,poses,instances*packed))&&SUCCEEDED(d->SetStreamSource(1,instanceIds,offset,stride))&&
                SUCCEEDED(d->SetStreamSourceFreq(1,D3DSTREAMSOURCE_INSTANCEDATA|1))&&SUCCEEDED(d->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|instances));
            if(issued){
                hr=draw(first);
                if(SUCCEEDED(hr)){
                    ++batches;saved+=instances-1;
                    if(stencilKind>=3)receiverSaved+=instances-1;
                    if(mixedConstants)mixedSaved+=instances-1;
                    if(alphaTested)alphaSaved+=instances-1;
                    peak=std::max(peak,instances);
                }
            }
            restored=retry([&]{return d->SetStreamSourceFreq(0,1);});
            restored=retry([&]{return d->SetStreamSourceFreq(1,1);})&&restored;
            restored=retry([&]{return d->SetStreamSource(1,nullptr,streamOffset,streamStride);})&&restored;
            restored=retry([&]{return d->SetVertexShader(program->source.p);})&&restored;
            restored=retry([&]{return d->SetVertexDeclaration(layout);})&&restored;
            if(shared)restored=retry([&]{return d->SetVertexShaderConstantF(program->biasConstant,liveBias,1);})&&restored;
            if(!issued||FAILED(hr)||!restored)palette.unavailable=true;
        }
        if(stamped&&!issued&&restored){
            for(unsigned i=0;i<instances&&SUCCEEDED(hr);++i){
                const float* entry=poses+i*packed*4;
                const bool bound=writeInstance(entry)&&retry([&]{return d->SetVertexShaderConstantF(firstRow(),entry+shared*4,rows);});
                hr=bound?draw(first):E_FAIL;
                if(bound)++drained;
            }
            restored=writeInstance(liveInstance)&&restored;
        }
        restored=retry([&]{return d->SetVertexShaderConstantF(restoreFirst,liveRows,restoreRows);})&&restored;
        restored=setStencil(firstStencil,liveStencil)&&restored;
        if(FAILED(hr)||!restored)++failures;
        return restored?hr:E_FAIL;
    }
    template<class Submit> HRESULT submit(const Draw& a,unsigned kind,Submit serial){
        if(joinRanges)return submitRange(a,kind,serial);
        if(pending){
            StencilValues current{};
            if(kind==stencilKind&&sameDraw(first,a)&&knownSource()&&sourceShader==program->source.p&&sourceLayout==layout&&
               NorthlightSourceIdentity::same(firstSource->sourceIdentity,source->sourceIdentity)&&
               stencil(current)&&current==firstStencil&&captureInstance(pending)){
                ++pending;++queuedDraws;
                return pending==capacity?flush():D3D_OK;
            }
            if(const auto hr=flush();FAILED(hr))return hr;
        }
        if(kind<2||palette.unavailable||a.topology!=D3DPT_TRIANGLELIST)return serial();
        {
            // The same-draw snapshot checks all referenced indices. Its compacted
            // vertex count can be smaller than the caller's declared interval.
            if(!knownSource()||source->primitiveCount!=a.primitives){++sourceMisses;return serial();}
            Ref<IDirect3DVertexShader9> vertex;Ref<IDirect3DVertexDeclaration9> decl;Ref<IDirect3DVertexBuffer9> stream;
            DWORD blend=TRUE,alpha=TRUE,zwrite=FALSE;UINT off=0,stride=0,freq0=0,freq1=0;
            if(FAILED(state->GetVertexShader(vertex.out()))||vertex.p!=sourceShader||FAILED(state->GetVertexDeclaration(decl.out()))||decl.p!=sourceLayout){
                ++proofMisses;++stateMisses;return serial();
            }
            program=palette.program(vertex.p,nullptr,mixedConstants);
            if(!program){++proofMisses;++shaderMisses;return serial();}
            if(FAILED(state->GetStreamSource(1,stream.out(),&off,&stride))||stream.p||
               FAILED(state->GetStreamSourceFreq(0,&freq0))||FAILED(state->GetStreamSourceFreq(1,&freq1))||freq0!=1||freq1!=1||
               d->GetSoftwareVertexProcessing()||state->GetNPatchMode()!=0||
               FAILED(state->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||blend||FAILED(state->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha))||
               FAILED(state->GetRenderState(D3DRS_ZWRITEENABLE,&zwrite))||!zwrite||!stencil(firstStencil)||!firstStencil[0]){
                ++proofMisses;++stateMisses;return serial();
            }
            span=palette.span(*source,decl.p,declarations);
            if(!span.count||firstRow()+rowCount()>program->sourceLimit||Base+2*packedRows()>program->limit){++proofMisses;++rangeMisses;return serial();}
            auto* nextLayout=palette.declaration(decl.p,program->semantic,declarations);
            auto* ids=nextLayout?palette.instanceIds(packedRows()/Rows*sizeof(float)):nullptr;
            if(!ids||!captureInstance(0)){++proofMisses;++setupMisses;return serial();}
            layout=decl.p;instancedLayout=nextLayout;instanceIds=ids;first=a;firstSource=source;
            capacity=(program->limit-Base)/packedRows();stencilKind=kind;alphaTested=alpha!=FALSE;streamOffset=off;streamStride=stride;
            pending=1;++queuedDraws;
            return D3D_OK;
        }
    }
};
