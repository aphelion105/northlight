#pragma once
#include "native_local_light_patch.h"
#include "native_local_pom.h"
#include "native_local_displacement.h"
#include "displacement_shader_patch.h"
#include "auto_parallax_patch.h"
#include "local_light_compiled_shaders.h"
#include "effect_coverage.h"
#include <memory>

struct NorthlightNativeLocalDraw {
    unsigned slot=0,rendererFrame=0;
    IDirect3DCubeTexture9* cube=nullptr; // Borrowed complete world-space cube.
    float position[3]{},range=0,gain=1,inverseView[16]{};
    bool cubeIsWhite=false; // Proven current content; unknown sources retain their normal PCF.
};
class NorthlightNativeLocalLighting {
public:
    enum class ColdStage {Allocation,SourceVS,VertexPatch,SourcePS,PixelPatch,PomPixel,VertexForward,PomVertex,Displacement,CreateVS,CreatePS,CoveragePixel,Complete};
    struct ColdTrace {
        unsigned frame=0,count=0,slots[3]{},vertexCreates=0,pixelCreates=0,coverageRecipe=0;
        uint64_t sourceVSHash=0,sourcePSHash=0;
        bool sourceVSHashKnown=false,sourcePSHashKnown=false,pom=false,shared=false,dual=false,displacement=false,gpu=false;
        bool vertexReused=false,pixelReused=false,duplicateRetry=false,success=false,hrKnown=false,legacyFilter=false;
        ColdStage stage=ColdStage::Allocation;HRESULT hr=S_OK,firstPixelCreateHr=S_OK;
    };
    using ColdTraceSink=void(*)(void*,const ColdTrace&) noexcept;
    void setSamplerStateSource(void* context,bool(*reader)(void*,DWORD,unsigned,DWORD*)){samplerContext=context;samplerReader=context==d?reader:nullptr;}
    static const char* coldStageName(ColdStage stage){
        static const char* names[]={"allocation","source-vs-read","vs-patch","source-ps-read","ps-patch","pom-pixel-compose","vertex-forward","pom-vertex-compose","displacement-compose","create-vs","create-ps","coverage-pixel-compose","complete"};
        return names[unsigned(stage)];
    }
    void setColdTraceSink(ColdTraceSink sink,void* context=nullptr){coldTraceSink=sink;coldTraceContext=context;}
private:
    struct Key {IDirect3DVertexShader9* vs;IDirect3DPixelShader9* ps;unsigned slot;bool operator==(const Key& b)const{return vs==b.vs&&ps==b.ps&&slot==b.slot;}};
    struct Hash {size_t operator()(const Key& k)const{return (reinterpret_cast<size_t>(k.vs)*16777619u^reinterpret_cast<size_t>(k.ps))*16777619u^k.slot;}};
    template<class T> static void release(T*& p){if(p){p->Release();p=nullptr;}}
    struct Entry {
        IDirect3DVertexShader9 *originalVS=nullptr,*vs=nullptr;IDirect3DPixelShader9 *originalPS=nullptr,*ps=nullptr;
        NorthlightNativeLocalPatch::PixelInfo info;
        NorthlightNativeLocalPatch::VertexInfo vertexInfo;
        IDirect3DVertexShader9* sourceVS=nullptr;IDirect3DPixelShader9* sourcePS=nullptr;
        uint64_t sourceVSHash=0,sourcePSHash=0;
        bool pomShared=false,displacementGpu=false,pomDual=false,sourceVSHashKnown=false,sourcePSHashKnown=false,legacyFilter=false;
        unsigned displacementSampler=0;
        unsigned coverageRecipe=0;
        bool (*sharedVertex)(const DWORD*,size_t,std::vector<uint32_t>&)=nullptr;
        unsigned pomControl=0,pomCount=0,displacementControl=0,displacementCount=0;
        ~Entry(){release(vs);release(ps);release(originalVS);release(originalPS);release(sourceVS);release(sourcePS);}
    };
    std::unordered_map<Key,std::unique_ptr<Entry>,Hash> shaders;
    IDirect3DDevice9* d;
    void* samplerContext=nullptr;bool(*samplerReader)(void*,DWORD,unsigned,DWORD*)=nullptr;
    unsigned warmFrame=~0u,coldAttempts=0,coldTraceCount=0;ColdTraceSink coldTraceSink=nullptr;void* coldTraceContext=nullptr;
    Entry* acquire(IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,unsigned slot,unsigned frame,const NorthlightNativeLocalDraw* second,const NorthlightNativeLocalDraw* third,const NorthlightNativePOM* pom,const NorthlightNativeDisplacement* displacement,const NorthlightEffectCoverage::NativePixel* coverage){
        const Key key{vs,ps,slot|(second?(second->slot+1)<<8:0)|(pom?0x10000u:0)|(displacement?0x20000u:0)|(third?(third->slot+1)<<20:0)};auto old=shaders.find(key);if(old!=shaders.end())return old->second->ps?old->second.get():nullptr;
        if(!vs||!ps||shaders.size()>=128)return nullptr;
        if(warmFrame!=frame){warmFrame=frame;coldAttempts=0;}
        const unsigned requestedCount=third?3:second?2:1;
        if(coldAttempts){++deferredPairs;if(coldTraceSink)++deferredByCount[requestedCount-1];return nullptr;}++coldAttempts;if(coldTraceSink)++coldAttemptsByCount[requestedCount-1]; // One cold attempt/frame, including unsupported pairs.
        ColdTrace trace;trace.frame=frame;trace.count=requestedCount;trace.slots[0]=slot;trace.slots[1]=second?second->slot:0;trace.slots[2]=third?third->slot:0;
        trace.pom=pom!=nullptr;trace.shared=pom&&pom->shared;trace.dual=pom&&pom->dual;trace.displacement=displacement!=nullptr;trace.gpu=displacement&&displacement->gpu;trace.coverageRecipe=coverage?coverage->recipe():0;
        const auto sink=coldTraceCount<128?coldTraceSink:nullptr;Entry* entry=nullptr;
        struct Audit {NorthlightNativeLocalLighting& renderer;ColdTrace& trace;Entry*& entry;ColdTraceSink sink;void* context;
            ~Audit(){trace.success=entry&&entry->ps;trace.legacyFilter=entry&&entry->legacyFilter;if(trace.success){if(renderer.coldTraceSink)++renderer.successfulByCount[trace.count-1];if(renderer.coldTraceSink&&entry->legacyFilter)++renderer.legacyByCount[trace.count-1];trace.stage=ColdStage::Complete;trace.hr=S_OK;trace.hrKnown=true;}else if(renderer.coldTraceSink)++renderer.coldRejectedByCount[trace.count-1];
                if(sink){if(entry){trace.sourceVSHash=entry->sourceVSHash;trace.sourcePSHash=entry->sourcePSHash;trace.sourceVSHashKnown=entry->sourceVSHashKnown;trace.sourcePSHashKnown=entry->sourcePSHashKnown;}++renderer.coldTraceCount;sink(context,trace);}}
        } audit{*this,trace,entry,sink,coldTraceContext};
        auto owned=std::make_unique<Entry>();auto* created=owned.get();vs->AddRef();ps->AddRef();created->originalVS=vs;created->originalPS=ps;
        entry=shaders.emplace(key,std::move(owned)).first->second.get();++coldPairs;
        auto* sourceVS=pom?pom->originalVS:displacement?displacement->originalVS:vs;auto* sourcePS=pom?pom->originalPS:coverage?coverage->source:ps;
        entry->coverageRecipe=coverage?coverage->recipe():0;
        sourceVS->AddRef();entry->sourceVS=sourceVS;sourcePS->AddRef();entry->sourcePS=sourcePS;entry->pomShared=pom&&pom->shared;entry->pomDual=pom&&pom->dual;
        entry->sharedVertex=entry->pomShared?pom->sharedVertex:nullptr;
        entry->displacementGpu=displacement&&displacement->gpu;entry->displacementSampler=displacement?displacement->sampler:0;
        Entry* donor=nullptr;
        // The vertex program does not depend on the pixel material. Reuse only
        // an already successful pair with the same owned source and recipe.
        for(const auto& cached:shaders){auto* candidate=cached.second.get();
            if(candidate->vs&&candidate->ps&&cached.first.vs==vs&&cached.first.slot==key.slot&&candidate->sourceVS==sourceVS&&
               candidate->pomShared==entry->pomShared&&candidate->sharedVertex==entry->sharedVertex&&
               candidate->displacementGpu==entry->displacementGpu&&candidate->displacementSampler==entry->displacementSampler&&
               candidate->displacementCount==(displacement?displacement->count:0)){donor=candidate;break;}}
        trace.vertexReused=donor!=nullptr;trace.stage=ColdStage::SourceVS;
        UINT vb=0,pb=0;if(!donor&&(FAILED(sourceVS->GetFunction(nullptr,&vb))||vb<8||vb>65536||vb%4))return nullptr;
        std::vector<DWORD> vertex(vb/4),pixel;std::vector<uint32_t> patchedVS,patchedPS;NorthlightNativeLocalPatch::VertexInfo vi;
        if(donor){vi=donor->vertexInfo;entry->sourceVSHash=donor->sourceVSHash;entry->sourceVSHashKnown=donor->sourceVSHashKnown;}
        const unsigned count=third?3:second?2:1;const unsigned slots[]={slot,second?second->slot:0,third?third->slot:0};const DWORD* helper=third?kNativeLocalLighting3Shader:second?kNativeLocalLighting2Shader:kNativeLocalLighting1Shader;
        unsigned helperWords=third?sizeof(kNativeLocalLighting3Shader)/4:second?sizeof(kNativeLocalLighting2Shader)/4:sizeof(kNativeLocalLighting1Shader)/4;
        if(!donor){if(FAILED(sourceVS->GetFunction(vertex.data(),&vb)))return nullptr;
            if(sink){entry->sourceVSHash=NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<const uint32_t*>(vertex.data()),vertex.size());entry->sourceVSHashKnown=true;}
            trace.stage=ColdStage::VertexPatch;
            if(!NorthlightNativeLocalPatch::vertex(reinterpret_cast<const uint32_t*>(vertex.data()),vertex.size(),slots,count,patchedVS,vi,pom?0xc2u:0))return nullptr;}
        entry->vertexInfo=vi;
        Entry* pixelDonor=nullptr;
        // Slot removal and vertex deformation change the VS, not a PS with the
        // same source material and endpoint interface. Keep each caller pair.
        for(const auto& cached:shaders){auto* candidate=cached.second.get();
            if(candidate->vs&&candidate->ps&&candidate->sourcePS==sourcePS&&candidate->vertexInfo.count==vi.count&&
               std::equal(std::begin(vi.varying),std::end(vi.varying),std::begin(candidate->vertexInfo.varying))&&
               candidate->pomCount==(pom?pom->count:0)&&candidate->pomShared==entry->pomShared&&candidate->pomDual==entry->pomDual&&candidate->coverageRecipe==entry->coverageRecipe){pixelDonor=candidate;break;}}
        if(pixelDonor){entry->legacyFilter=pixelDonor->legacyFilter;entry->info=pixelDonor->info;entry->pomControl=pixelDonor->pomControl;entry->pomCount=pixelDonor->pomCount;entry->sourcePSHash=pixelDonor->sourcePSHash;entry->sourcePSHashKnown=pixelDonor->sourcePSHashKnown;}
        else {trace.stage=ColdStage::SourcePS;if(FAILED(sourcePS->GetFunction(nullptr,&pb))||pb<8||pb>65536||pb%4)return nullptr;pixel.resize(pb/4);
            if(FAILED(sourcePS->GetFunction(pixel.data(),&pb)))return nullptr;
            if(sink){entry->sourcePSHash=NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<const uint32_t*>(pixel.data()),pixel.size());entry->sourcePSHashKnown=true;}}
        trace.pixelReused=pixelDonor!=nullptr;
        const auto buildPixel=[&](bool share){
            trace.stage=ColdStage::PixelPatch;trace.hrKnown=false;if(!share)trace.duplicateRetry=true;
            entry->info={};entry->pomControl=entry->pomCount=0;
            if(!NorthlightNativeLocalPatch::pixel(reinterpret_cast<const uint32_t*>(pixel.data()),pixel.size(),vi,reinterpret_cast<const uint32_t*>(helper),helperWords,patchedPS,entry->info,share))return false;
            if(pom){trace.stage=ColdStage::PomPixel;std::vector<uint32_t> material;
                if(!NorthlightParallaxPatch::pixel(patchedPS.data(),patchedPS.size(),pom->dual,material,&entry->pomControl,pom->shared,nullptr,512))return false;
                std::vector<NorthlightHdrUi::Instruction> instructions;unsigned cost=0;
                if(!NorthlightHdrUi::parse(material.data(),material.size(),instructions,cost)||cost+3*count>512)return false;
                patchedPS.swap(material);entry->pomCount=pom->count;
            }
            if(coverage){trace.stage=ColdStage::CoveragePixel;if(!coverage->compose(patchedPS))return false;entry->info.addedSlots+=coverage->addedSlots();}
            return true;
        };
        if(!pixelDonor&&!buildPixel(true)&&(entry->info.sharedInstructions==0||!buildPixel(false))){
            // Preserve every formerly supported material when the stable filter
            // cannot fit its exact composition within the existing SM3 limits.
            helper=third?kNativeLocalLightingLegacy3Shader:second?kNativeLocalLightingLegacy2Shader:kNativeLocalLightingLegacy1Shader;
            helperWords=third?sizeof(kNativeLocalLightingLegacy3Shader)/4:second?sizeof(kNativeLocalLightingLegacy2Shader)/4:sizeof(kNativeLocalLightingLegacy1Shader)/4;
            entry->legacyFilter=true;
            if(!buildPixel(true)&&(entry->info.sharedInstructions==0||!buildPixel(false)))return nullptr;
        }
        if(pom&&!donor){
            trace.stage=ColdStage::VertexForward;trace.hrKnown=false;
            // Compose in factory order: native endpoints, position forwarding,
            // then ONE POM prefix shared by every material endpoint.
            std::vector<uint32_t> forwarded,material;
            if(NorthlightWaterShaderPatch::patch(patchedVS.data(),patchedVS.size(),forwarded))return nullptr;
            if(pom->shared){trace.stage=ColdStage::PomVertex;if(!pom->sharedVertex(reinterpret_cast<const DWORD*>(forwarded.data()),forwarded.size(),material))return nullptr;forwarded.swap(material);}
            unsigned vertexCost=0;for(size_t at=1;at+1<forwarded.size();){const unsigned op=forwarded[at]&65535,n=op==65534?(forwarded[at]>>16)&32767:(forwarded[at]>>24)&15;
                if(op!=31&&op!=81&&op!=65534)vertexCost+=NorthlightShadowShader::instructionSlots(op);at+=n+1;}
            if(vertexCost>512)return nullptr;
            patchedVS.swap(forwarded);
        }
        if(displacement&&!donor){
            trace.stage=ColdStage::Displacement;trace.hrKnown=false;
            std::vector<uint32_t> material;NorthlightDisplacementPatch::Binding binding;
            if(!NorthlightDisplacementPatch::patch(patchedVS.data(),patchedVS.size(),material,binding,true,displacement->gpu)||binding.count!=displacement->count||(displacement->gpu&&binding.sampler!=displacement->sampler))return nullptr;
            unsigned cost=0;for(size_t at=1;at+1<material.size();){const unsigned op=material[at]&65535,n=op==65534?(material[at]>>16)&32767:(material[at]>>24)&15;
                if(op!=31&&op!=81&&op!=65534)cost+=NorthlightShadowShader::instructionSlots(op);at+=n+1;}
            if(cost>512)return nullptr;patchedVS.swap(material);entry->displacementControl=binding.constant;entry->displacementCount=binding.count;
        }
        if(donor){entry->vs=donor->vs;entry->vs->AddRef();entry->displacementControl=donor->displacementControl;entry->displacementCount=donor->displacementCount;}
        else {trace.stage=ColdStage::CreateVS;trace.hrKnown=true;++trace.vertexCreates;trace.hr=d->CreateVertexShader(reinterpret_cast<const DWORD*>(patchedVS.data()),&entry->vs);if(FAILED(trace.hr)){release(entry->vs);return nullptr;}}
        const auto createPixel=[&]{trace.stage=ColdStage::CreatePS;trace.hrKnown=true;++trace.pixelCreates;trace.hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(patchedPS.data()),&entry->ps);if(trace.pixelCreates==1)trace.firstPixelCreateHr=trace.hr;return trace.hr;};
        if(pixelDonor){entry->ps=pixelDonor->ps;entry->ps->AddRef();}
        else if(FAILED(createPixel())){
            release(entry->ps);
            // Retry the existing duplicate path with the same POM composition.
            if(!entry->info.sharedInstructions||!buildPixel(false)||FAILED(createPixel())){release(entry->vs);release(entry->ps);return nullptr;}
        }
        shaderSlots+=entry->info.addedSlots;return entry;
    }
public:
    std::array<uint64_t,3> requestsByCount{},coldAttemptsByCount{},deferredByCount{},coldRejectedByCount{},successfulByCount{},legacyByCount{};
    unsigned coldPairs=0,deferredPairs=0,shaderSlots=0;uint64_t draws=0,multiDraws=0,tripleDraws=0,fallbacks=0;
    uint64_t samplerBulkHits=0,samplerBulkFallbacks=0;
    uint64_t whiteCubeSkips=0,whiteCubeBypasses=0;
    uint64_t zeroGainSkips=0,zeroGainBypasses=0;
#ifdef NORTHLIGHT_TEST_NEUTRAL_GAINS
    bool testZeroGainFastPath=true;
#endif
    explicit NorthlightNativeLocalLighting(IDirect3DDevice9* device):d(device){}
    void reset(){shaders.clear();} // A same-frame resource reset keeps the cold-attempt budget.
    template<class Draw> HRESULT draw(const NorthlightNativeLocalDraw& light,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,Draw submit,const NorthlightNativeLocalDraw* second=nullptr,const NorthlightNativePOM* pom=nullptr,const NorthlightNativeDisplacement* displacement=nullptr,const NorthlightNativeLocalDraw* third=nullptr,const NorthlightEffectCoverage::NativePixel* coverage=nullptr){
        if(coverage&&!coverage->matches(ps))coverage=nullptr;
        if(coldTraceSink)++requestsByCount[second?(third?2:1):0];
        // Establish the original correction first. Trying an unknown dual pair
        // must not consume the cold budget ahead of a working single source.
        if((pom&&!pom->matches(vs,ps))||(displacement&&(!displacement->matches(vs)||pom))){++fallbacks;return submit();}
        const auto white=[](const NorthlightNativeLocalDraw& source){return source.cube&&source.cubeIsWhite&&std::isfinite(source.gain);};
        const auto zero=[&](const NorthlightNativeLocalDraw& source){
#ifdef NORTHLIGHT_TEST_NEUTRAL_GAINS
            if(!testZeroGainFastPath)return false;
#endif
            return source.cube&&std::isfinite(source.gain)&&source.gain==0.f;
        };
        if(white(light)&&(!second||white(*second))&&(!third||white(*third))){++whiteCubeSkips;return submit();}
        if((white(light)||zero(light))&&(!second||white(*second)||zero(*second))&&(!third||white(*third)||zero(*third))){++zeroGainSkips;return submit();}
        Entry* shader=nullptr;try{shader=acquire(vs,ps,light.slot,light.rendererFrame,nullptr,nullptr,pom,displacement,coverage);
            if(second){auto* dual=acquire(vs,ps,light.slot,light.rendererFrame,second,nullptr,pom,displacement,coverage);if(dual)shader=dual;}
            if(second&&third){auto* triple=acquire(vs,ps,light.slot,light.rendererFrame,second,third,pom,displacement,coverage);if(triple)shader=triple;}
        }catch(const std::bad_alloc&){if(!shader){++fallbacks;return submit();}}
        if(shader&&shader->vertexInfo.count<3)third=nullptr;if(shader&&shader->vertexInfo.count<2)second=nullptr;
        if(!shader||!light.cube){++fallbacks;return submit();}
        if((second&&!second->cube)||(third&&!third->cube)){++fallbacks;return submit();}
        const unsigned count=third?3:second?2:1,constantCount=third?12:second?10:8,first=shader->info.control+2;
        const D3DSAMPLERSTATETYPE keys[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE};
        constexpr unsigned samplerMask=(1u<<(D3DSAMP_ADDRESSU-1))|(1u<<(D3DSAMP_ADDRESSV-1))|(1u<<(D3DSAMP_ADDRESSW-1))|
            (1u<<(D3DSAMP_MINFILTER-1))|(1u<<(D3DSAMP_MAGFILTER-1))|(1u<<(D3DSAMP_MIPFILTER-1))|(1u<<(D3DSAMP_SRGBTEXTURE-1));
        const DWORD wanted[]={D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,D3DTEXF_POINT,D3DTEXF_POINT,D3DTEXF_NONE,FALSE};
        float saved[12][4],pomSaved[12]{},pomValues[12]{},displacementSaved[8]{},displacementValues[8]{};DWORD values[3][7]{};IDirect3DBaseTexture9* textures[3]{};
        bool read=SUCCEEDED(d->GetPixelShaderConstantF(first,saved[0],constantCount));
        if(shader->pomCount)read=read&&pom&&SUCCEEDED(d->GetPixelShaderConstantF(pom->sourceControl,pomValues,shader->pomCount))&&SUCCEEDED(d->GetPixelShaderConstantF(shader->pomControl,pomSaved,shader->pomCount));
        if(shader->displacementCount)read=read&&displacement&&SUCCEEDED(d->GetVertexShaderConstantF(displacement->control,displacementValues,shader->displacementCount))&&SUCCEEDED(d->GetVertexShaderConstantF(shader->displacementControl,displacementSaved,shader->displacementCount));
        for(unsigned s=0;s<count&&read;++s){
            read=SUCCEEDED(d->GetTexture(shader->info.samplers[s],textures+s));
            if(!read)break;
            DWORD bank[D3DSAMP_DMAPOFFSET];
            const bool bulk=samplerReader&&samplerReader(samplerContext,shader->info.samplers[s],samplerMask,bank);
            if(coldTraceSink){if(bulk)++samplerBulkHits;else ++samplerBulkFallbacks;}
            if(bulk)
                for(unsigned i=0;i<7;++i)values[s][i]=bank[keys[i]-1];
            else for(unsigned i=0;i<7&&read;++i)read=SUCCEEDED(d->GetSamplerState(shader->info.samplers[s],keys[i],&values[s][i]));
        }
        if(!read){for(auto*& texture:textures)release(texture);++fallbacks;return submit();}
        struct Restore {NorthlightNativeLocalLighting& r;Entry* e;IDirect3DBaseTexture9** textures;const D3DSAMPLERSTATETYPE* keys;const DWORD (*values)[7];const DWORD* wanted;const float* constants;unsigned first,count,constantCount;const float* pomSaved;const float* displacementSaved;bool restored=false;
            bool restore(){if(restored)return true;auto retry=[](auto call){return SUCCEEDED(call())||SUCCEEDED(call());};bool ok=true;
                ok=retry([&]{return r.d->SetVertexShader(e->originalVS);})&&ok;ok=retry([&]{return r.d->SetPixelShader(e->originalPS);})&&ok;
                ok=retry([&]{return r.d->SetPixelShaderConstantF(first,constants,constantCount);})&&ok;
                if(e->pomCount)ok=retry([&]{return r.d->SetPixelShaderConstantF(e->pomControl,pomSaved,e->pomCount);})&&ok;
                if(e->displacementCount)ok=retry([&]{return r.d->SetVertexShaderConstantF(e->displacementControl,displacementSaved,e->displacementCount);})&&ok;
                for(unsigned s=0;s<count;++s){ok=retry([&]{return r.d->SetTexture(e->info.samplers[s],textures[s]);})&&ok;
                    for(unsigned i=0;i<7;++i)if(values[s][i]!=wanted[i])ok=retry([&]{return r.d->SetSamplerState(e->info.samplers[s],keys[i],values[s][i]);})&&ok;}
                restored=ok;return ok;}
            ~Restore(){restore();for(unsigned s=0;s<count;++s)release(textures[s]);}
        } restore{*this,shader,textures,keys,values,wanted,saved[0],first,count,constantCount,pomSaved,displacementSaved};
        // Preserve the complete recipe for immediate caster entry. A proven
        // white source takes the existing far-range exit with visibility one.
        const bool whiteSources[]={white(light),second&&white(*second),third&&white(*third)};
        const bool zeroSources[]={zero(light),second&&zero(*second),third&&zero(*third)};
        float constants[12][4]{};constants[0][3]=.025f;std::memcpy(constants[1],light.inverseView,64);
        // Keep receiver and sources camera-relative; avoid cancellation at large world coordinates.
        for(unsigned k=0;k<3;++k){constants[4][k]=0;constants[5][k]=light.position[k]-light.inverseView[12+k];}
        constants[7][0]=.1f;constants[7][1]=whiteSources[0]||zeroSources[0]?0.f:light.range;constants[7][2]=1.f/256;constants[7][3]=light.gain;
        if(second){for(unsigned k=0;k<3;++k)constants[8][k]=second->position[k]-light.inverseView[12+k];constants[9][1]=whiteSources[1]||zeroSources[1]?0.f:second->range;constants[9][3]=second->gain;}
        if(third){for(unsigned k=0;k<3;++k)constants[10][k]=third->position[k]-light.inverseView[12+k];constants[11][1]=whiteSources[2]||zeroSources[2]?0.f:third->range;constants[11][3]=third->gain;}
        bool bound=SUCCEEDED(d->SetPixelShaderConstantF(first,constants[0],constantCount));
        if(shader->pomCount)bound=bound&&SUCCEEDED(d->SetPixelShaderConstantF(shader->pomControl,pomValues,shader->pomCount));
        if(shader->displacementCount)bound=bound&&SUCCEEDED(d->SetVertexShaderConstantF(shader->displacementControl,displacementValues,shader->displacementCount));
        for(unsigned s=0;s<count&&bound;++s){bound=SUCCEEDED(d->SetTexture(shader->info.samplers[s],s==2?third->cube:s?second->cube:light.cube));for(unsigned i=0;i<7&&bound;++i)if(values[s][i]!=wanted[i])bound=SUCCEEDED(d->SetSamplerState(shader->info.samplers[s],keys[i],wanted[i]));}
        bound=bound&&SUCCEEDED(d->SetVertexShader(shader->vs))&&SUCCEEDED(d->SetPixelShader(shader->ps));
        if(!bound){++fallbacks;if(!restore.restore())return E_FAIL;return submit();}
        ++draws;if(second)++multiDraws;if(third)++tripleDraws;const HRESULT result=submit();if(SUCCEEDED(result)){whiteCubeBypasses+=unsigned(whiteSources[0])+unsigned(whiteSources[1])+unsigned(whiteSources[2]);zeroGainBypasses+=unsigned(zeroSources[0])+unsigned(zeroSources[1])+unsigned(zeroSources[2]);}return restore.restore()?result:E_FAIL;
    }
};
