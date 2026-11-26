#pragma once
#include "capture_work_audit.h"
#include <d3d9.h>
#include "model_normal_programs.h"
#include "model_normal_draw_state.h"
#include "model_normal_palette.h"
#include <array>
#include "model_normal_native.h"
#include "draw_snapshot.h"
#include "scene_native_store.h"
#include "vertex_declaration_cache.h"
#include "pose_bounds_handoff.h"
#include "replay_constants.h"
#include "shader_constant_usage.h"
#include "replay_capture_constants.h"
#include "device_mirror.h"
#include "dynamic_ring.h"
#include "replay_bulk_layout.h"
#include "saved_state.h"
#include "m2_draw_section.h"
#include "capture_phase_profile.h"
#include <chrono>
#include <cstdlib>
#include <optional>

// Independent of AutoBump and actor-shadow capture budgets. Replay only opaque
// native depth writers; the consumer rejects captures hidden by final depth.
class NorthlightModelNormals {
    template<class T> static void release(T*& p){if(p)p->Release();p=nullptr;}
    using Vertex=NorthlightModelNormalPrograms::Vertex;
    using Pixel=NorthlightModelNormalPrograms::Pixel;
    NorthlightModelNormalPrograms programs;
    NorthlightModelNormalPalette palette;
    IDirect3DDevice9* d;
    NorthlightNativeModelNormals nativeMrt;
    ExtensionDevice* drawStateSource=nullptr;
    unsigned knownTargets=0,knownPixelShaders=0,mirroredSources=0;
    NorthlightM2Draw::ConsumerStats nativeSections;
    std::chrono::steady_clock::duration nativeSectionAuditTime{};
    enum CapturePhase { Admission, Source, Range, PacketState, Geometry, Constants, Publish, CapturePhaseCount };
    enum ReplayPhase { ReplayGeometry, ReplaySubmit, ReplayRetire, ReplayPhaseCount };
    NorthlightCapturePhases::Stats<CapturePhaseCount> capturePhases;
    NorthlightCapturePhases::Stats<ReplayPhaseCount> replayPhases;
    bool profileCapture=false;

    bool peekDeclaration(IDirect3DVertexDeclaration9*& out)const{
        return drawStateSource&&drawStateSource->constantMirror()->heldByThisThread()&&
            drawStateSource->peekVertexDeclaration(out);
    }

    Pixel* currentPixel(bool alpha){
        IDirect3DPixelShader9* shader=nullptr;
        if(drawStateSource&&drawStateSource->constantMirror()->heldByThisThread()&&drawStateSource->peekPixelShader(shader)){
            // pixel() retains new programs before calling the backend.
            ++knownPixelShaders;
            return programs.pixel(shader,alpha);
        }
        NorthlightDrawSnapshot::Ref<IDirect3DPixelShader9> owned;
        return SUCCEEDED(d->GetPixelShader(owned.out()))&&owned.p?programs.pixel(owned.p,alpha):nullptr;
    }
    NorthlightModelNormalRange::Cache rangeCache;
    void* poseContext=nullptr;NorthlightConstantEpoch::Reader poseReader=nullptr;NorthlightConstantEpoch::KnownSpan poseSpan=nullptr;unsigned poseBoundsTests=0,poseBoundsCulled=0,poseViewportTests=0,poseViewportCulled=0;
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
    friend struct ModelNormalPacketTestAccess;
    friend struct ModelNormalMergeTest;
    friend struct ModelNormalStreamTest;
    friend struct ModelNormalPaletteTest;
    bool rangeEnabled=true;
    bool residentSourceEnabled=true;
    bool constantReuseEnabled=true;
    bool mergeEnabled=true;
    bool deduplicateEnabled=true;
    bool streamEnabled=true;
    bool depthSeedEnabled=true;
    bool instanceRowsEnabled=false;
#endif
    void* rasterContext=nullptr;bool(*rasterReader)(void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*)=nullptr;
    void* samplerContext=nullptr;bool(*samplerReader)(void*,DWORD,unsigned,DWORD*)=nullptr;
    template<size_t N> bool renderStates(const D3DRENDERSTATETYPE (&keys)[N],DWORD (&out)[N]){
        if(rasterReader&&rasterReader(rasterContext,keys,unsigned(N),out))return true;
        for(unsigned i=0;i<N;++i)if(FAILED(d->GetRenderState(keys[i],out+i)))return false;
        return true;
    }
    IDirect3DTexture9* normal=nullptr;
    IDirect3DPixelShader9* depthSeedPS=nullptr;unsigned depthSeeded=0;
    IDirect3DSurface9 *surface=nullptr,*depth=nullptr,*sharedDepth=nullptr,*frameTarget=nullptr;
    IDirect3DSurface9* hybridDepth=nullptr;
    bool hybridEnabled=false,mirrorSnapshots=false;
    unsigned hybridAttempts=0,hybridWritten=0;
    unsigned unusedDepthFrames=0;
    NorthlightStateBlockPool batchStates;
    UINT width=0,height=0;unsigned draws=0,compactDraws=0,mergedDraws=0,duplicateDraws=0,primitiveLimit=0,maxVertexIndex=0;bool cleared=false,bad=false;
    // Private immutable geometry permits one normal/depth pass instead of
    // interrupting the full-resolution MSAA scene for every opaque model.
    inline static constexpr D3DRENDERSTATETYPE rasterKeys[]={D3DRS_CULLMODE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,
        D3DRS_MULTISAMPLEANTIALIAS,D3DRS_MULTISAMPLEMASK,D3DRS_CLIPPING,D3DRS_DITHERENABLE,D3DRS_SHADEMODE,
        D3DRS_FILLMODE,D3DRS_LASTPIXEL,D3DRS_ANTIALIASEDLINEENABLE,D3DRS_WRAP0,D3DRS_WRAP1,D3DRS_WRAP2,
        D3DRS_WRAP3,D3DRS_WRAP4,D3DRS_WRAP6,D3DRS_WRAP8,D3DRS_WRAP9,D3DRS_WRAP10,D3DRS_WRAP11,
        D3DRS_WRAP12,D3DRS_WRAP13,D3DRS_WRAP14,D3DRS_WRAP15};
    struct SampledStage {
        IDirect3DBaseTexture9* texture=nullptr;
        std::array<DWORD,13> states{};
    };
    struct NonFloatConstants {
        int vertexInts[16*4],pixelInts[16*4];
        BOOL vertexBools[16],pixelBools[16];
    };
    // Only the most recent accepted draw can certify a reuse.
    struct ConstantCapture {
        float* constantStorage=nullptr;
        const float* constants=nullptr;
        int* intStorage=nullptr;
        BOOL* boolStorage=nullptr;
        const int* ints=nullptr;
        const BOOL* bools=nullptr;
        NorthlightShaderConstants::Usage constantUsage;
        NorthlightConstantEpoch::Stamp constantStamp;
        unsigned projectionKind=2;
    } lastConstants;
    struct Pending {
        Vertex* vertex=nullptr;Pixel* pixel=nullptr;
        IDirect3DVertexDeclaration9* declaration=nullptr;
        bool borrowedDeclaration=false;
        D3DVIEWPORT9 viewport{};DWORD raster[sizeof(rasterKeys)/sizeof(*rasterKeys)]={};
        const float* constants=nullptr;
        const int* ints=nullptr;
        const BOOL* bools=nullptr;
        std::unique_ptr<NonFloatConstants> nonFloat;
        std::vector<float> ps;
        NorthlightDrawSnapshot::Mesh mesh;
        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> sharedMesh;
        IDirect3DVertexBuffer9* gpuVertex[4]={};IDirect3DIndexBuffer9* gpuIndex=nullptr;
        NorthlightReplayGPU::Placement placement;bool resident=false,streamed=false,redundant=false,nativeProbed=false,nativeWritten=false;
        NorthlightModelNormalPalette::Span paletteSpan;
#ifdef NORTHLIGHT_REPLAY_STAND
        unsigned traceOrdinal=0;
#endif
        const NorthlightDrawSnapshot::Mesh& geometry()const{return sharedMesh?*sharedMesh:mesh;}
        // Entries follow the set bits in the patched pixel shader's sampler mask.
        std::vector<SampledStage> sampled;
        void recycle(){constants=nullptr;ints=nullptr;bools=nullptr;paletteSpan={};resident=streamed=redundant=nativeProbed=nativeWritten=false;for(auto*& b:gpuVertex)release(b);release(gpuIndex);sharedMesh.reset();
            if(!borrowedDeclaration)release(declaration);
            declaration=nullptr;borrowedDeclaration=false;
            for(auto& stage:sampled)release(stage.texture);sampled.clear();}
        ~Pending(){recycle();}
    };
    Pending* nativeCandidate=nullptr;unsigned nativeCommittedPackets=0;
    static bool sameCapture(const Pending& a,const Pending& b,const NorthlightModelNormalPalette::Program* paletteProgram=nullptr){
        if(a.vertex->shader!=b.vertex->shader||a.vertex->first!=b.vertex->first||a.vertex->count!=b.vertex->count||a.pixel!=b.pixel||a.declaration!=b.declaration||
           std::memcmp(&a.viewport,&b.viewport,sizeof a.viewport)||std::memcmp(a.raster,b.raster,sizeof a.raster))return false;
        const unsigned first=a.vertex->first,end=first+a.vertex->count;
        const auto sameFloatRange=[&](unsigned from,unsigned to){
            if(from>=to||!std::memcmp(a.constants+from*4,b.constants+from*4,size_t(to-from)*16))return true;
            // The enclosing bank includes gaps (native lighting, UV transforms)
            // which the capture shader never reads. They cannot split a run.
            if(!a.vertex->reads.exact||!b.vertex->reads.exact)return false;
            for(unsigned c=from;c<to;++c)if(a.vertex->reads.f[c>>6]&(1ull<<(c&63)))
                if(std::memcmp(a.constants+c*4,b.constants+c*4,16))return false;
            return true;
        };
        if(a.constants!=b.constants){
            if(paletteProgram){
                for(const auto& run:paletteProgram->fixedReads)
                    if(std::memcmp(a.constants+run.first*4,b.constants+run.first*4,run.count*16))return false;
            }else if(!sameFloatRange(first,end))return false;
        }
        if(a.ps.size()!=b.ps.size()||(!a.ps.empty()&&std::memcmp(a.ps.data(),b.ps.data(),a.ps.size()*sizeof(float))))return false;
        if((a.vertex->reads.i&&std::memcmp(a.ints,b.ints,sizeof(NonFloatConstants::vertexInts)))||
           (a.vertex->reads.b&&std::memcmp(a.bools,b.bools,sizeof(NonFloatConstants::vertexBools)))||
           (a.pixel->reads.i&&std::memcmp(a.nonFloat->pixelInts,b.nonFloat->pixelInts,sizeof(NonFloatConstants::pixelInts)))||
           (a.pixel->reads.b&&std::memcmp(a.nonFloat->pixelBools,b.nonFloat->pixelBools,sizeof(NonFloatConstants::pixelBools))))return false;
        if(a.sampled.size()!=b.sampled.size())return false;
        for(size_t i=0;i<a.sampled.size();++i)
            if(a.sampled[i].texture!=b.sampled[i].texture||a.sampled[i].states!=b.sampled[i].states)return false;
        return true;
    }
    // Large complete batches with no duplicates rarely benefit from hashing
    // every packet again. Recheck promptly after changes and at least every
    // 16 eligible frames. Duplicate draws always remain safe to submit.
    struct DuplicateProbe {
        size_t previousSize=0;
        unsigned delay=1,skip=0;
        bool ready(size_t size,bool wholeFrame){
            if(!wholeFrame||size<512){*this={};return true;}
            const auto tolerance=previousSize/8;
            if(size+tolerance<previousSize||size>previousSize+tolerance){skip=0;delay=1;}
            previousSize=size;
            if(!skip)return true;
            --skip;
            return false;
        }
        void observe(bool found){
            if(found){delay=1;skip=0;}
            else if(previousSize){skip=delay;delay=std::min(15u,delay*2);}
        }
    } duplicateProbe;
    std::uint64_t duplicateEligiblePackets=0,duplicateScannedPackets=0;
    bool findRedundant(){
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
        if(!mergeEnabled||!deduplicateEnabled)return false;
#endif
        // This private pass uses opaque overwrite, depth writes and LESS_EQUAL.
        // Only the last identical draw can win a depth tie; earlier copies
        // cannot change the final minimum depth or colour at any pixel.
        // Keep packets alive: their constant banks may be shared by later draws.
        std::array<std::uint16_t,4096> recent{};
        bool found=false;
        for(size_t at=pending.size();at--;){auto& p=*pending[at];const auto& m=p.geometry();p.redundant=false;
            if(!p.resident||p.streamed||!p.gpuIndex||!p.gpuVertex[0]||!m.indexed||m.topology!=D3DPT_TRIANGLELIST)continue;
            size_t hash=reinterpret_cast<std::uintptr_t>(p.gpuIndex)>>4;
            const auto word=[&](size_t value){hash^=value+size_t(0x9e3779b9u)+(hash<<6)+(hash>>2);};
            word(reinterpret_cast<std::uintptr_t>(p.gpuVertex[0])>>4);word(p.placement.vertexBase);word(p.placement.indexStart);
            word(reinterpret_cast<std::uintptr_t>(p.vertex->shader)>>4);word(reinterpret_cast<std::uintptr_t>(p.pixel)>>4);word(p.raster[0]);
            // Instances share geometry. Sample the root palette (or the last
            // captured rows) so different poses do not evict every duplicate.
            // sameCapture still compares the entire captured state on a hit.
            const unsigned end=p.vertex->first+p.vertex->count;
            const unsigned row=p.vertex->first<=31&&end>=34?31:std::max(p.vertex->first,end>3?end-3:0u);
            for(unsigned i=row*4;i<std::min(end,row+3)*4;++i){std::uint32_t value;std::memcpy(&value,p.constants+i,4);word(value);}
            hash^=hash>>16;hash*=size_t(0x7feb352du);hash^=hash>>15;hash*=size_t(0x846ca68bu);hash^=hash>>16;
            auto& slot=recent[hash&(recent.size()-1)];const auto candidate=slot;slot=std::uint16_t(at+1);
            if(!candidate)continue;auto& q=*pending[candidate-1];const auto& n=q.geometry();
            // A collision only misses an optimization. One candidate bounds
            // lookup work even when every instance has a different pose.
            if(p.gpuIndex==q.gpuIndex&&!std::memcmp(p.gpuVertex,q.gpuVertex,sizeof p.gpuVertex)&&
               p.placement.vertexBase==q.placement.vertexBase&&p.placement.indexStart==q.placement.indexStart&&
               p.placement.absoluteIndices==q.placement.absoluteIndices&&m.vertexCount==n.vertexCount&&
               m.primitiveCount==n.primitiveCount&&m.streams[0].stride==n.streams[0].stride&&sameCapture(p,q)){
                p.redundant=true;
                found=true;
            }
        }
        return found;
    }
    struct DrawRun {UINT count,min,vertices;unsigned packets=1;};
    DrawRun drawRun(size_t& at)const{
        const auto& first=*pending[at++];const auto& mesh=first.geometry();
        DrawRun run{mesh.primitiveCount,first.placement.minimumVertex(),mesh.vertexCount};
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
        if(!mergeEnabled)return run;
#endif
        if(!primitiveLimit||!first.resident||!first.placement.absoluteIndices||!mesh.indexed||mesh.topology!=D3DPT_TRIANGLELIST)return run;
        while(at<pending.size()){
            const auto& next=*pending[at];const auto& m=next.geometry();
            if(!next.resident||!next.placement.absoluteIndices||!m.indexed||m.topology!=D3DPT_TRIANGLELIST||
               next.gpuIndex!=first.gpuIndex||std::memcmp(next.gpuVertex,first.gpuVertex,sizeof first.gpuVertex)||
               m.streams[0].stride!=mesh.streams[0].stride)break;
            const bool adjacent=uint64_t(first.placement.indexStart)+uint64_t(run.count)*3==next.placement.indexStart;
            if((!adjacent&&next.placement.indexStart!=first.placement.indexStart)||!sameCapture(first,next))break;
            // Repeating the exact opaque call cannot change this private target.
            // Do not remove it after a different geometry range joined the run.
            if(run.count==mesh.primitiveCount&&run.min==first.placement.minimumVertex()&&run.vertices==mesh.vertexCount&&
               next.placement.indexStart==first.placement.indexStart&&next.placement.vertexBase==first.placement.vertexBase&&
               m.vertexCount==mesh.vertexCount&&m.primitiveCount==mesh.primitiveCount){++run.packets;++at;continue;}
            if(next.redundant)break;
            if(!adjacent)break;
            const auto low=std::min(run.min,next.placement.minimumVertex());
            const auto high=std::max(uint64_t(run.min)+run.vertices,uint64_t(next.placement.minimumVertex())+m.vertexCount);
            const auto count=uint64_t(run.count)+m.primitiveCount;
            if(count>primitiveLimit||high>UINT_MAX)break;
            run.count=UINT(count);run.min=low;run.vertices=UINT(high-low);++run.packets;++at;
        }
        return run;
    }
    NorthlightVertexDeclarations::Cache declarations;
    NorthlightDrawSnapshot::Frame snapshots{8u<<20,0};
#ifdef NORTHLIGHT_REPLAY_STAND
    NorthlightCaptureWork::Work retiredSnapshotWork;
#endif
    NorthlightGpuScene::NativeClient* geometryCache=nullptr;
    std::vector<std::unique_ptr<Pending>> pending,spare;
    // Pointers can outlive the packet that first captured a pose. Banks have
    // stable addresses until the entire batch finishes. A shared pose leaves
    // the current write bank available to the next capture.
    struct FloatBank {std::unique_ptr<float[]> values;unsigned registers=0;};
    std::vector<FloatBank> floatBanks;
#ifdef _WIN64
    static constexpr size_t batchLimit=32u<<20;
#else
    static constexpr size_t batchLimit=8u<<20;
#endif
    size_t ownedBanks=0,floatBankBytes=0,peakFloatBanks=0,peakPackets=0,peakBatchBytes=0;
    float* writeBank(unsigned end){
        if(ownedBanks==floatBanks.size())floatBanks.emplace_back();
        auto& bank=floatBanks[ownedBanks];
        end=std::max(1u,end);
        if(bank.registers<end){
            bank.values.reset(new float[end*4]);
            floatBankBytes+=size_t(end-bank.registers)*16;
            bank.registers=end;
        }
        return bank.values.get();
    }
    // Recycled buffers and queued geometry share the batch allowance.
    size_t pendingBytes=0,spareBytes=0;unsigned pendingTextureSlots=0;bool deferred=false,sourceSharing=false;
    std::array<const NorthlightDrawSnapshot::Mesh*,1024> chargedSources{};
    size_t sharedMeshBytesAvoided=0;
    static size_t packetBytes(const Pending& p){return sizeof(Pending)+(p.nonFloat?sizeof(NonFloatConstants):0)+p.ps.capacity()*sizeof(float)+p.sampled.capacity()*sizeof(SampledStage)+p.mesh.capacityBytes();}
    // A miss uploads into a bounded append ring instead of one UP allocation
    // per packet. Reuse only completed spans; DISCARD preserves in-flight draws.
    static constexpr UINT StreamVertexBytes=3u<<20,StreamIndexBytes=1u<<20;
    IDirect3DVertexBuffer9* streamVB=nullptr;IDirect3DIndexBuffer9* streamIB=nullptr;
    NorthlightDynamicRing::Ring streamVertices,streamIndices;NorthlightDynamicRing::FrameFence streamFence;
    unsigned streamedDraws=0,streamedBatches=0,streamedDiscards=0,streamFailures=0;size_t streamedBytes=0,streamSavedBytes=0;bool streamBlocked=false;
    NorthlightReplayBulk::Layout streamLayout;
    void releaseStream(){NorthlightDynamicRing::reset(streamVertices,0,streamFence);NorthlightDynamicRing::reset(streamIndices,0,streamFence);streamFence.drop();release(streamVB);release(streamIB);}
    bool streamPending(size_t first){
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
        if(!streamEnabled)return false;
#endif
        if(streamBlocked||first>=pending.size())return false;
        const auto stride=pending[first]->geometry().streams[0].stride;
        if(!stride||stride>4096)return false;
        size_t vertices=0,indices=0,saved=0,last=first;streamLayout.begin();
        // Resident draws use their own buffers. They split submission runs,
        // but do not need to split an upload or a shared-source lookup.
        for(;last<pending.size();++last){auto& p=*pending[last];if(p.resident)continue;
            const auto& m=p.geometry();const auto bytes=m.streams[0].bytes.size();
            if(!m.indexed||m.topology!=D3DPT_TRIANGLELIST||m.streams[0].stride!=stride||!bytes||bytes%stride||
               !m.vertexCount||m.vertexCount>bytes/stride||m.indices.size()!=uint64_t(m.primitiveCount)*3)break;
            const auto owner=streamLayout.add(p.sharedMesh.get(),last-first);
            if(owner!=last-first){p.placement=pending[first+owner]->placement;saved+=m.byteSize();continue;}
            if(bytes>StreamVertexBytes-stride-vertices||m.indices.size()*4>StreamIndexBytes-indices)break;
            bool valid=true;for(auto index:m.indices)if(index>=m.vertexCount){valid=false;break;}if(!valid)break;
            p.placement={UINT(vertices/stride),UINT(indices/4),true};vertices+=bytes;indices+=m.indices.size()*4;
        }
        if(last==first||!indices)return false;
        const auto failed=[&]{streamBlocked=true;++streamFailures;return false;};
        try{
            if(!streamVB||!streamIB){
                MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
                if(!GlobalMemoryStatusEx(&memory)||memory.ullAvailVirtual<2ull*(StreamVertexBytes+StreamIndexBytes)+(128ull<<20))return failed();
                releaseStream();
                if(FAILED(d->CreateVertexBuffer(StreamVertexBytes,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&streamVB,nullptr))||!streamVB||
                   FAILED(d->CreateIndexBuffer(StreamIndexBytes,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&streamIB,nullptr))||!streamIB){releaseStream();return failed();}
                streamFence.device=d;NorthlightDynamicRing::reset(streamVertices,StreamVertexBytes,streamFence);NorthlightDynamicRing::reset(streamIndices,StreamIndexBytes,streamFence);
            }
            const auto vslot=NorthlightDynamicRing::place(streamVertices,UINT(vertices),stride,streamFence);
            const auto islot=NorthlightDynamicRing::place(streamIndices,UINT(indices),4,streamFence);
            if(uint64_t(vslot.offset/stride)+vertices/stride-1>maxVertexIndex)return failed();
            for(auto slot:{vslot,islot})if(slot.wrapped)NorthlightLockMeter::ringWrap(slot.fenceReuse,slot.discarded);
            if(vslot.discarded){++streamedDiscards;NorthlightLockMeter::discard(NorthlightLockMeter::ReplayVB,StreamVertexBytes);}
            if(islot.discarded){++streamedDiscards;NorthlightLockMeter::discard(NorthlightLockMeter::ReplayIB,StreamIndexBytes);}
            void* memory=nullptr;if(FAILED(streamVB->Lock(vslot.offset,UINT(vertices),&memory,vslot.flags)))return failed();
            if(memory)for(size_t i=first;i<last;++i)if(!pending[i]->resident&&streamLayout.unique(i-first)){const auto& p=*pending[i];const auto& bytes=p.geometry().streams[0].bytes;std::memcpy(static_cast<uint8_t*>(memory)+size_t(p.placement.vertexBase)*stride,bytes.data(),bytes.size());}
            NorthlightLockMeter::staging(vertices);auto hr=streamVB->Unlock();if(!memory||FAILED(hr))return failed();
            memory=nullptr;if(FAILED(streamIB->Lock(islot.offset,UINT(indices),&memory,islot.flags)))return failed();
            if(memory)for(size_t i=first;i<last;++i)if(!pending[i]->resident&&streamLayout.unique(i-first)){const auto& p=*pending[i];const auto& m=p.geometry();auto* target=static_cast<uint32_t*>(memory)+p.placement.indexStart;
                const auto base=vslot.offset/stride+p.placement.vertexBase;for(auto index:m.indices)*target++=index+base;}
            NorthlightLockMeter::staging(indices);hr=streamIB->Unlock();if(!memory||FAILED(hr))return failed();
            UINT base=vslot.offset/stride,start=islot.offset/4;
            for(size_t i=first;i<last;++i){auto& p=*pending[i];if(p.resident)continue;
                for(auto& vb:p.gpuVertex)release(vb);release(p.gpuIndex);
                p.gpuVertex[0]=streamVB;streamVB->AddRef();p.gpuIndex=streamIB;streamIB->AddRef();p.placement.vertexBase+=base;p.placement.indexStart+=start;p.resident=p.streamed=true;
            }
            ++streamedBatches;streamedBytes+=vertices+indices;streamSavedBytes+=saved;return true;
        }catch(...){return failed();}
    }
    void(*pendingChanged)(void*)=nullptr;void* pendingContext=nullptr;
    unsigned queued=0,flushes=0,immediate=0,sourceReused=0,residentSourceHits=0,residentDraws=0,handoffs=0,rangeNarrow=0,rangeFull=0,rangeRegisters=0,packetPixelReadsSkipped=0;size_t sourceReadBytes=0,upBytes=0;
    unsigned declarationGets=0,sharedDeclarations=0,poseDeclarationGets=0,poseDeclarationPeeks=0;
    NorthlightReplayCaptureConstants::Stats constantStats;
    NorthlightReplayCaptureConstants::ClockSource<DeviceMirror> constantSource;
    unsigned constantReadRegisters=0,materialBankReads=0,materialBankRegisters=0,materialBankChecks=0,materialBankMismatches=0;
#ifdef NORTHLIGHT_REPLAY_STAND
    unsigned terrainMirrorReads=0,terrainMirrorRegisters=0;
    unsigned traceOrdinal=0,coveragePackets=0;
    uint64_t coverageOrder=0;
    void recordCoverage(unsigned ordinal){
        if(!ordinal)return;
        coverageOrder=(coverageOrder^ordinal)*1099511628211ull;
        ++coveragePackets;
    }
#endif
    unsigned rangeBankRegisters=0;
    bool currentMaterialBank(const NorthlightConstantEpoch::BankHandoff& bank)const{
        return !materialBankMismatches&&constantSource.attached()&&constantSource.mirror->heldByThisThread()&&
            constantSource.valid()&&NorthlightConstantEpoch::sameWords(bank,constantSource.live());
    }
    static bool legacyRangeBank(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool enabled=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_LEGACY_NORMAL_RANGE_BANK");
            return value&&std::strcmp(value,"1")==0;
        }();
        return enabled;
#else
        return false;
#endif
    }
    void clearPending(){nativeCandidate=nullptr;const bool hadPending=!pending.empty();
        if(hadPending)chargedSources.fill(nullptr);
        for(auto& p:pending){p->recycle();spareBytes+=packetBytes(*p);spare.push_back(std::move(p));}
        pending.clear();lastConstants={};ownedBanks=0;pendingBytes=0;pendingTextureSlots=0;
#ifdef NORTHLIGHT_REPLAY_STAND
        // A mid-frame flush resets the batch budget. The draw audit still
        // measures work since the last Present, including completed batches.
        retiredSnapshotWork.addSnapshots(snapshots);
#endif
        snapshots.clearFrame();if(hadPending&&pendingChanged)pendingChanged(pendingContext);
    }
    bool readMirroredSnapshot(IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* declaration,
                              const NorthlightDrawSnapshot::Draw& draw,std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& source){
        if(!mirrorSnapshots||!sourceSharing||!drawStateSource||!draw.indexed)return false;
        const auto* mirror=drawStateSource->constantMirror();
        UINT extent[4];
        if(!mirror->knownIndexedStreamZero(shader,declaration)||!declarations.captureLayout(declaration,extent))return false;
        const auto& stream=mirror->streams[0];
        if(!snapshots.readResidentIndexed(declaration,draw,extent,stream.buffer,stream.offset,stream.stride,mirror->indices,source))return false;
        ++mirroredSources;
        return true;
    }
    template<class Admit> bool record(Vertex* vs,Pixel* ps,const float* control,const D3DVIEWPORT9& viewport,Admit& admit,const NorthlightDrawSnapshot::Draw* geometry,
                const void* indices,D3DFORMAT format,const void* vertices,UINT stride,
                const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>* captured,IDirect3DVertexDeclaration9* capturedDeclaration,const NorthlightPoseBounds::Handoff* pose,
                std::shared_ptr<const NorthlightDrawSnapshot::Mesh>* published,IDirect3DVertexDeclaration9** publishedDeclaration,
                const NorthlightConstantEpoch::BankHandoff* materialBank,const NorthlightM2Draw::Section* nativeSection){
        // Keep a crowded scene's resident packets in one ordered replay.
        constexpr size_t packetLimit=4096;
        if(!deferred||!vs->reads.exact||vs->reads.samplers||!ps->reads.exact||!geometry||pending.size()>=packetLimit||d->GetNPatchMode()!=0)return false;
        NorthlightCapturePhases::Scope<CapturePhaseCount> phase(profileCapture?&capturePhases:nullptr,Source);
        try{
            std::unique_ptr<Pending> packet;
            if(spare.empty())packet=std::make_unique<Pending>();
            else {packet=std::move(spare.back());spare.pop_back();spareBytes-=packetBytes(*packet);}
            NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> ownedDeclaration;
            IDirect3DVertexDeclaration9* declaration=nullptr;
            // The previous accepted packet or an earlier packet in its run
            // pins this layout until the entire ordered batch is cleared.
            const bool sharedDeclaration=!legacyDeclarations()&&!pending.empty()&&peekDeclaration(declaration)&&
                declaration==pending.back()->declaration;
            if(!sharedDeclaration){
                ++declarationGets;
                if(FAILED(d->GetVertexDeclaration(ownedDeclaration.out()))||!ownedDeclaration.p)return false;
                declaration=ownedDeclaration.p;
            }
            // The material producer hands off the immutable RAW source from
            // this same serialized draw, before geometry replacement/dedup.
            if(captured&&*captured&&capturedDeclaration==declaration&&(*captured)->topology==geometry->topology&&
               (*captured)->primitiveCount==geometry->primitives&&(*captured)->indexed==geometry->indexed){packet->sharedMesh=*captured;++sourceReused;++handoffs;}
            else if(vertices){if(!snapshots.readUP(declaration,*geometry,indices,format,vertices,stride,packet->mesh))return false;}
            else {
                // Use the existing bounded source pool even when AutoBump did
                // not capture this draw. Current write revisions validate hits.
                const auto hits=snapshots.snapshotCacheHits();const auto read=snapshots.bytesRead();
                const bool needsVertices=vs->rangeKind==NorthlightModelNormalRange::Rigid||vs->rangeKind==NorthlightModelNormalRange::Single||
                    (vs->rangeKind==NorthlightModelNormalRange::Envelope&&pose&&pose->bounds.valid&&poseReader);
                auto lease=[&](const auto& words,UINT& vertexCount){
                    packet->resident=geometryCache->bindSource(d,words,vertexCount,packet->gpuVertex,packet->gpuIndex,packet->placement);
                    residentSourceHits+=packet->resident;return packet->resident;
                };
                bool canLease=geometryCache&&!needsVertices;
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
                canLease=canLease&&residentSourceEnabled;
#endif
                const bool captured=readMirroredSnapshot(vs->source,declaration,*geometry,packet->sharedMesh)||
                    snapshots.read(d,declaration,*geometry,packet->mesh,nullptr,false,sourceSharing?&packet->sharedMesh:nullptr,
                        false,false,canLease?&lease:nullptr);
                sourceReadBytes+=snapshots.bytesRead()-read;sourceReused+=snapshots.snapshotCacheHits()-hits;
                if(!captured)return false;
            }
            // The next shadow consumer can admit this same immutable source
            // without repeating buffer/declaration discovery. Publish before
            // receiver culling or replacing CPU bytes with a resident GPU lease.
            if(!vertices&&published&&publishedDeclaration&&!*published&&!*publishedDeclaration&&packet->sharedMesh){
                *published=packet->sharedMesh;declaration->AddRef();*publishedDeclaration=declaration;
            }
            const auto& mesh=packet->geometry();
            if(!packet->resident&&mesh.streams[0].bytes.empty())return false;
            for(unsigned i=1;i<4;++i)if(!mesh.streams[i].bytes.empty())return false;
            phase.next(Range);
            const bool retainedSource=nativeSection&&(packet->resident||(packet->sharedMesh&&packet->sharedMesh->sourceIdentity));
            if(vs->rangeKind==NorthlightModelNormalRange::Envelope&&pose&&pose->bounds.valid&&poseReader&&packet->sharedMesh
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
               &&rangeEnabled
#endif
            ){++poseBoundsTests;NorthlightConstantEpoch::Stamp current;current.requestHeld=true;
                if(poseReader(poseContext,current)&&pose->outside(packet->sharedMesh.get(),vs->source,declaration,current)){
                    ++poseBoundsCulled;packet->recycle();spareBytes+=packetBytes(*packet);spare.push_back(std::move(packet));return true;}}
            ConstantCapture bank;
            bool vsCaptured=false;
            if((vs->rangeKind==NorthlightModelNormalRange::Rigid||vs->rangeKind==NorthlightModelNormalRange::Single)&&packet->sharedMesh
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
               &&rangeEnabled
#endif
            ){
                const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;bool outside=false,known=false;
                if(declarations.get(declaration,elements,count)){
                    // One gate check for the whole proof. Single-influence
                    // programs may address any palette row through c255.
                    const unsigned end=vs->rangeKind==NorthlightModelNormalRange::Single?256u:34u;
                    if(materialBank&&materialBank->covers(2,end-2)&&!legacyRangeBank()&&currentMaterialBank(*materialBank)){
                        known=true;
                        outside=rangeCache.outsideView(packet->sharedMesh,elements,count,[&](unsigned first,unsigned n,float*){
                            rangeBankRegisters+=n;
                            return materialBank->bank+first*4;
                        },vs->rangeKind);
                    }else{
                        namespace E=NorthlightConstantEpoch;E::Stamp before;before.requestHeld=true;
                        known=poseSpan&&poseReader&&poseReader(poseContext,before)&&before.held;
                        if(known)outside=rangeCache.outsideRead(packet->sharedMesh,elements,count,[&](unsigned first,unsigned n,float* out){
                            E::Stamp copied;const bool valid=poseSpan(poseContext,first,n,out,copied)&&E::exact(before,copied);known&=valid;if(valid)rangeRegisters+=n;return valid;
                        },vs->rangeKind);
                    }
                    if(known)++rangeNarrow;
                    else {bank.constantStorage=writeBank(256);constantReadRegisters+=256;if(FAILED(d->GetVertexShaderConstantF(0,bank.constantStorage,256)))return false;vsCaptured=true;++rangeFull;rangeRegisters+=256;
                        outside=rangeCache.outside(packet->sharedMesh,elements,count,bank.constantStorage,vs->rangeKind);}
                }
                if(outside){
                    packet->recycle();spareBytes+=packetBytes(*packet);spare.push_back(std::move(packet));return true;}}
            phase.next(PacketState);
            if(!admit())return false;
            // Proven out-of-range draws need no target/texture/sampler capture.
            // Render-target inputs otherwise retain the immediate path;
            // ordinary inputs use the serialized texture-write guard.
            unsigned textureSlots=0;
            packet->sampled.reserve(__builtin_popcount(ps->reads.samplers));
            for(unsigned mask=ps->reads.samplers;mask;mask&=mask-1){
                const unsigned i=__builtin_ctz(mask);
                auto& stage=packet->sampled.emplace_back();
                NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> texture;
                if(FAILED(d->GetTexture(i,texture.out())))return false;
                if(!samplerReader||!samplerReader(samplerContext,i,(1u<<13)-1,stage.states.data()))
                    for(unsigned s=0;s<13;++s)if(FAILED(d->GetSamplerState(i,D3DSAMPLERSTATETYPE(s+1),&stage.states[s])))return false;
                if(!texture.p)continue;
                D3DSURFACE_DESC desc{};
                if(texture->GetType()!=D3DRTYPE_TEXTURE||
                   FAILED(static_cast<IDirect3DTexture9*>(texture.p)->GetLevelDesc(0,&desc))||
                   (desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL)))return false;
                stage.texture=texture.p;texture.p=nullptr;textureSlots|=1u<<i;
            }
            // A resident GPU lease already owns immutable source bytes. Keep
            // only draw metadata, so the CPU budget does not split warm scenes.
            phase.next(Geometry);
            if(geometryCache&&packet->sharedMesh&&packet->sharedMesh->sourceIdentity&&
               geometryCache->bind(d,packet->sharedMesh,packet->gpuVertex,packet->gpuIndex,[](size_t){return false;},packet->placement,true)){
                packet->resident=true;
                keepPaletteSpan(*packet,vs,ps,declaration);
                NorthlightDrawSnapshot::Mesh resident;
                resident.topology=mesh.topology;resident.vertexCount=mesh.vertexCount;resident.primitiveCount=mesh.primitiveCount;
                resident.indexed=mesh.indexed;resident.streams[0].stride=mesh.streams[0].stride;
                packet->mesh=std::move(resident);packet->sharedMesh.reset();packet->resident=true;
            }
            phase.next(PacketState);
            packet->ps.resize(ps->count*4);
            if(vs->reads.i||vs->reads.b||ps->reads.i||ps->reads.b){
                if(!packet->nonFloat)packet->nonFloat.reset(new NonFloatConstants);
                bank.intStorage=packet->nonFloat->vertexInts;
                bank.boolStorage=packet->nonFloat->vertexBools;
            }else packet->nonFloat.reset();
            // Every published pointer still has an owning packet in this batch.
            // A slot collision may charge a repeat again, so the limit remains conservative.
            const auto* shared=packet->sharedMesh.get();
            const auto key=reinterpret_cast<std::uintptr_t>(shared);
            auto& charged=chargedSources[((key>>4)^(key>>14))&(chargedSources.size()-1)];
            const bool sharedSource=shared&&charged==shared;
            const size_t sharedBytes=shared?shared->capacityBytes():0;
            const size_t bytes=packetBytes(*packet)+(sharedSource?0:sharedBytes);
            DWORD planes[1]={};const D3DRENDERSTATETYPE planeKeys[]={D3DRS_CLIPPLANEENABLE};
            if(!renderStates(planeKeys,planes)||planes[0]||!renderStates(rasterKeys,packet->raster))return false;
            phase.next(Constants);
            bank.constantUsage.floats={vs->first,vs->count};bank.constantUsage.booleans={0,vs->reads.b?16u:0u};bank.constantUsage.integers={0,vs->reads.i?16u:0u};
            auto reader=vsCaptured?nullptr:poseReader;
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
            if(!constantReuseEnabled)reader=nullptr;
#endif
            const auto readFloats=[&](unsigned first,unsigned count,float* out){
                constantReadRegisters+=count;
                return SUCCEEDED(d->GetVertexShaderConstantF(first,out,count));
            };
            auto fetchFloats=[&](unsigned first,unsigned count,float* out){
                if(materialBank&&materialBank->covers(first,count)&&currentMaterialBank(*materialBank)){
                    const auto* rows=materialBank->bank+first*4;
                    if(NorthlightReplayCaptureConstants::ValidateEvery||materialBankReads%512==0){
                        constantReadRegisters+=count;
                        if(FAILED(d->GetVertexShaderConstantF(first,out,count)))return false;
                        ++materialBankChecks;
                        if(std::memcmp(out,rows,count*16)){++materialBankMismatches;return true;}
                    }else std::memcpy(out,rows,count*16);
                    ++materialBankReads;materialBankRegisters+=count;return true;
                }
                // The terrain and normal captures can read the same live camera
                // rows. Only copy known values under the owning draw gate.
                if(vs->terrain&&reader&&constantSource.attached()){
                    if(const auto* rows=constantSource.mirror->borrowKnownVertexFloats(first,count)){
                        std::memcpy(out,rows,size_t(count)*sizeof(*rows));
#ifdef NORTHLIGHT_REPLAY_STAND
                        ++terrainMirrorReads;terrainMirrorRegisters+=count;
#endif
                        return true;
                    }
                }
                return readFloats(first,count,out);
            };
            auto fetch=[&]{return (vsCaptured||!vs->count||fetchFloats(vs->first,vs->count,bank.constantStorage+vs->first*4))&&
                (!vs->reads.i||SUCCEEDED(d->GetVertexShaderConstantI(0,bank.intStorage,16)))&&(!vs->reads.b||SUCCEEDED(d->GetVertexShaderConstantB(0,bank.boolStorage,16)));};
            // The existing epoch helper flattens shared banks to an earlier
            // accepted packet. Every owner stays alive until this batch flushes.
            // Only the owning device's typed source authorizes in-place reads.
            // Unheld calls retain the stamped reader's race-checked fallback.
            const auto* previous=pending.empty()?nullptr:&lastConstants;
            if(!vsCaptured)bank.constantStorage=writeBank(vs->first+vs->count);
            if(reader&&constantSource.attached()&&constantSource.mirror->heldByThisThread()){
                NorthlightReplayCaptureConstants::SelfCheck check{constantStats.blockTests,&constantStats};
                const auto checkFloats=[&](unsigned first,unsigned count,float* out){
                    return vs->terrain?readFloats(first,count,out):fetchFloats(first,count,out);
                };
                if(!NorthlightReplayCaptureConstants::captureBlocks<NorthlightReplayCaptureConstants::InlineBanks>(bank,previous,constantSource,fetch,fetchFloats,&constantStats,true,check,checkFloats))return false;
            }else if(!NorthlightReplayCaptureConstants::captureBlocks(bank,previous,poseContext,reader,fetch,fetchFloats,&constantStats))return false;
            // This packet overwrites its complete float bank with control below.
            if(ps->first==ps->control&&ps->count==1)++packetPixelReadsSkipped;
            else if(FAILED(d->GetPixelShaderConstantF(ps->first,packet->ps.data(),ps->count)))return false;
            if((ps->reads.i&&FAILED(d->GetPixelShaderConstantI(0,packet->nonFloat->pixelInts,16)))||(ps->reads.b&&FAILED(d->GetPixelShaderConstantB(0,packet->nonFloat->pixelBools,16))))return false;
            phase.next(Publish);
            packet->viewport=viewport;packet->declaration=declaration;packet->borrowedDeclaration=sharedDeclaration;
            ownedDeclaration.p=nullptr;
            packet->vertex=vs;packet->pixel=ps;
            std::copy(control,control+4,packet->ps.data()+(ps->control-ps->first)*4);
            packet->constants=bank.constants;packet->ints=bank.ints;packet->bools=bank.bools;
            const size_t neededBanks=ownedBanks+(bank.constants==bank.constantStorage);
            bank.constantStorage=nullptr;bank.intStorage=nullptr;bank.boolStorage=nullptr;
            const auto retained=[&]{return bytes+pendingBytes+spareBytes+floatBankBytes;};
            while(floatBanks.size()>neededBanks&&retained()>batchLimit){
                floatBankBytes-=size_t(floatBanks.back().registers)*16;
                floatBanks.pop_back();
            }
            while(!spare.empty()&&retained()>batchLimit){spareBytes-=packetBytes(*spare.back());spare.pop_back();}
            const size_t total=retained();
            if(total>batchLimit)return false;
            const bool texturesWerePending=pendingTextureSlots!=0;
#ifdef NORTHLIGHT_REPLAY_STAND
            packet->traceOrdinal=traceOrdinal;
#endif
            pending.push_back(std::move(packet));ownedBanks=neededBanks;peakFloatBanks=std::max(peakFloatBanks,ownedBanks);peakPackets=std::max(peakPackets,pending.size());nativeCandidate=pending.back().get();pendingBytes+=bytes;++queued;pendingTextureSlots|=textureSlots;
            if(shared){charged=shared;if(sharedSource)sharedMeshBytesAvoided+=sharedBytes;}
            lastConstants=bank;peakBatchBytes=std::max(peakBatchBytes,total);
            if(nativeSection){
                const auto begin=std::chrono::steady_clock::now();
                nativeSections.packet(*nativeSection,retainedSource,vs->first,vs->count,nativeCandidate->constants);
                nativeSectionAuditTime+=std::chrono::steady_clock::now()-begin;
            }
            sharedDeclarations+=sharedDeclaration;
            // Only the first sampled texture changes the write guard. Its
            // callback also ends the native coverage MRT batch.
            if(!texturesWerePending&&pendingTextureSlots&&pendingChanged)pendingChanged(pendingContext);return true;
        }catch(const std::bad_alloc&){return false;}
    }
    using State=NorthlightModelNormalDrawState;
    bool targets(UINT w,UINT h){
        if(w!=width||h!=height){reset(true);width=w;height=h;}
        if(normal&&surface)return true;
        const auto bytes=uint64_t(w)*h*8;MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
        if(bytes>(64ull<<20)||!GlobalMemoryStatusEx(&memory)||memory.ullAvailVirtual<bytes*2+(128ull<<20))return false;
        if(FAILED(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&normal,nullptr))||
           FAILED(normal->GetSurfaceLevel(0,&surface))){reset(true);return false;}
        return true;
    }
    IDirect3DSurface9* replayDepth(IDirect3DSurface9* materialDepth=nullptr){
        if(hybridDepth)return hybridDepth;
        if(sharedDepth)return sharedDepth;
        if(materialDepth&&!cleared&&!flushes&&!immediate){
            D3DSURFACE_DESC desc{};
            if(SUCCEEDED(materialDepth->GetDesc(&desc))&&desc.Width==width&&desc.Height==height&&
               desc.Format==D3DFMT_D24S8&&desc.MultiSampleType==D3DMULTISAMPLE_NONE){
                // Only the terminal replay can take the completed material
                // surface. Retain it for the rest of this frame's depth ties.
                sharedDepth=materialDepth;sharedDepth->AddRef();
                if(depth&&++unusedDepthFrames>=30)release(depth);
                return sharedDepth;
            }
        }
        unusedDepthFrames=0;
        if(!depth&&FAILED(d->CreateDepthStencilSurface(width,height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr)))release(depth);
        return depth;
    }
public:
    explicit NorthlightModelNormals(IDirect3DDevice9* device):programs(device),palette(device),d(device),nativeMrt(device),batchStates(device){snapshots.setSnapshotCacheLimit(0);pending.reserve(128);spare.reserve(128);
        D3DCAPS9 caps{};if(d&&SUCCEEDED(d->GetDeviceCaps(&caps))){primitiveLimit=caps.MaxPrimitiveCount;maxVertexIndex=caps.MaxVertexIndex;}
        snapshots.setDeclarationProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& elements,UINT& count){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(decl,elements,count);});
        snapshots.setLayoutProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(decl,extent);});
    }
    ~NorthlightModelNormals(){pendingChanged=nullptr;reset();}
    void setCaptureProfiling(bool enabled){profileCapture=enabled;}
    template<class Log> void reportCaptureProfile(unsigned frame,Log log)const{
        if(!capturePhases.clockReads)return;
        LARGE_INTEGER frequency{};
        if(!QueryPerformanceFrequency(&frequency)||!frequency.QuadPart)return;
        const double scale=1000.0/double(frequency.QuadPart);
        const auto& c=capturePhases.ticks;
        const auto& r=replayPhases.ticks;
        log("MODEL_NORMALS CPU phases frame=%u admissionMs=%.3f sourceMs=%.3f rangeMs=%.3f stateMs=%.3f geometryMs=%.3f constantsMs=%.3f publishMs=%.3f replayGeometryMs=%.3f replaySubmitMs=%.3f replayRetireMs=%.3f clockReads=%u",
            frame,c[Admission]*scale,c[Source]*scale,c[Range]*scale,c[PacketState]*scale,c[Geometry]*scale,c[Constants]*scale,c[Publish]*scale,
            r[ReplayGeometry]*scale,r[ReplaySubmit]*scale,r[ReplayRetire]*scale,capturePhases.clockReads+replayPhases.clockReads);
        if(geometryCache){
            const auto& stats=geometryCache->stats();
            log("MODEL_NORMALS residency pressure frame=%u ownerVictimVisits=%u evictionChecks=%u evictions=%u roomRejected=%u",
                frame,stats.ownerVictimVisits,stats.evictionChecks,stats.evictions,stats.roomRejected);
        }
    }
    void endFrame(){declarationGets=sharedDeclarations=poseDeclarationGets=poseDeclarationPeeks=0;knownTargets=knownPixelShaders=mirroredSources=0;peakFloatBanks=peakPackets=peakBatchBytes=sharedMeshBytesAvoided=0;depthSeeded=0;clearPending();
        capturePhases.clear();replayPhases.clear();profileCapture=false;
        paletteWork={};
        release(sharedDepth);
        release(hybridDepth);hybridAttempts=hybridWritten=0;
        nativeSections={};nativeSectionAuditTime={};
#ifdef NORTHLIGHT_REPLAY_STAND
        retiredSnapshotWork={};
        terrainMirrorReads=terrainMirrorRegisters=0;
        coveragePackets=0;
        coverageOrder=0;
#endif
        nativeMrt.beginFrame();nativeCommittedPackets=0;streamFence.endFrame();streamedDraws=streamedBatches=streamedDiscards=streamFailures=0;streamedBytes=streamSavedBytes=0;streamBlocked=false;rangeCache.beginFrame();constantStats={};constantReadRegisters=materialBankReads=materialBankRegisters=materialBankChecks=materialBankMismatches=0;poseBoundsTests=poseBoundsCulled=poseViewportTests=poseViewportCulled=0;compactDraws=draws=queued=flushes=immediate=sourceReused=residentSourceHits=residentDraws=handoffs=rangeNarrow=rangeFull=rangeRegisters=rangeBankRegisters=packetPixelReadsSkipped=mergedDraws=duplicateDraws=0;sourceReadBytes=upBytes=0;cleared=bad=false;release(frameTarget);}
    void reset(bool preserveCaptureSources=false){endFrame();unusedDepthFrames=0;duplicateProbe={};duplicateEligiblePackets=duplicateScannedPackets=0;
        palette.reset();
        nativeMrt.reset();releaseStream();rangeCache.clear();if(!preserveCaptureSources)snapshots.clearSnapshotCache();declarations.clear();spare.clear();floatBanks.clear();floatBankBytes=spareBytes=0;batchStates.clear();release(surface);release(normal);release(depth);release(depthSeedPS);width=height=0;}
    IDirect3DTexture9* texture()const{return draws&&!bad?normal:nullptr;}
    bool shareCaptureSources(NorthlightDrawSnapshot::Frame& source,NorthlightDrawSnapshot::Frame::IdentityFn identity,
                            NorthlightDrawSnapshot::Frame::VersionFn version,NorthlightCaptureMetadata::Read metadata){
        snapshots.setIdentityProvider(identity);snapshots.setVersionProvider(version);snapshots.setMetadataProvider(metadata);
        snapshots.setChargeResidentHits(false);sourceSharing=snapshots.shareSnapshotPool(source);return sourceSharing;
    }
    void setRasterStateSource(void* context,bool(*reader)(void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*)){rasterContext=context;rasterReader=context==d?reader:nullptr;nativeMrt.setRasterStateSource(context,reader);}
    void setEmptyTargetSource(void* context,bool(*reader)(void*)){nativeMrt.setEmptyTargetSource(context,reader);}
    void setSamplerStateSource(void* context,bool(*reader)(void*,DWORD,unsigned,DWORD*)){samplerContext=context;samplerReader=context==d?reader:nullptr;}
    void setDrawStateSource(ExtensionDevice& source){drawStateSource=static_cast<IDirect3DDevice9*>(&source)==d?&source:nullptr;}
    void setTrustedPoseSource(NorthlightConstantEpoch::TrustedSource source){lastConstants.constantStamp={};poseContext=source.context();poseReader=source&&source.context()==d?source.reader():nullptr;poseSpan=poseReader?source.span():nullptr;
        constantSource={};if(poseReader){const auto* mirror=static_cast<ExtensionDevice*>(d)->constantMirror();constantSource={&mirror->constantEpoch,mirror};}}
    void setGeometryCache(NorthlightGpuScene::NativeClient* cache){geometryCache=cache;}
    void setMirroredSnapshots(bool on){mirrorSnapshots=on;}
    unsigned queuedDraws()const{return queued;}
    unsigned cachedDraws()const{return residentDraws;}
    unsigned reusedSources()const{return sourceReused;}
    size_t readSourceBytes()const{return sourceReadBytes;}
    template<class Log> void reportNativeSections(unsigned frame,const char* domain,Log log)const{
        const auto& s=nativeSections;
        if(!s.seen)return;
        log("NATIVE_M2 normals frame=%u producerFrame=%llu domain=%s seen=%u eligible=%u queued=%u immutable=%u fallback=%u rangeMismatch=%u coverageMismatch=%u paletteChecks=%u paletteMismatch=%u paletteUnavailable=%u auditUs=%.3f",
            frame,(unsigned long long)s.producerFrame,domain,s.seen,s.eligible,s.queued,s.immutable,s.seen-s.queued,s.rangeMismatch,s.coverageMismatch,
            s.paletteChecks,s.paletteMismatch,s.paletteUnavailable,std::chrono::duration<double,std::micro>(nativeSectionAuditTime).count());
    }
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
    size_t testPendingPackets()const{return pending.size();}
    void testRange(bool enabled){rangeEnabled=enabled;}
    void testDepthSeed(bool enabled){depthSeedEnabled=enabled;}
    unsigned testDepthSeeded()const{return depthSeeded;}
    unsigned testRangeCulled()const{return rangeCache.culled+poseBoundsCulled;}
    unsigned testPoseDeclarationPeeks()const{return poseDeclarationPeeks;}
    unsigned testPoseDeclarationGets()const{return poseDeclarationGets;}
    size_t testRangeBytes()const{return sizeof(rangeCache);}
    unsigned testRangeRegisters()const{return rangeRegisters;}
    void testConstantReuse(bool enabled){constantReuseEnabled=enabled;lastConstants.constantStamp={};}
    const NorthlightReplayCaptureConstants::Stats& testConstantStats()const{return constantStats;}
    size_t testPeakFloatBanks()const{return peakFloatBanks;}
    unsigned testConstantReadRegisters()const{return constantReadRegisters;}
    unsigned testNativeCommittedPackets()const{return nativeCommittedPackets;}
#endif
    void enableNativeMrt(bool on){nativeMrt.configure(on);}
    void enableHybridMrt(bool on,std::chrono::milliseconds warmup={}){hybridEnabled=on;nativeMrt.setHybridShaderWarmup(warmup);}
    bool hybridRequested()const{return hybridEnabled;}
    bool beginHybridMrt(IDirect3DSurface9* mainDepth){
#ifdef _WIN64
        if(!hybridEnabled||!deferred||!mainDepth||bad)return false;
        if(hybridDepth)return hybridDepth==mainDepth;
        if(frameTarget||draws||!pending.empty()||!nativeMrt.acceptingHybrid())return false;
        NorthlightDrawSnapshot::Ref<IDirect3DSurface9> color,depth;
        D3DSURFACE_DESC desc{},z{};
        if(FAILED(d->GetDepthStencilSurface(depth.out()))||depth.p!=mainDepth||
           FAILED(d->GetRenderTarget(0,color.out()))||!color.p||FAILED(color.p->GetDesc(&desc))||
           FAILED(mainDepth->GetDesc(&z))||desc.MultiSampleType!=D3DMULTISAMPLE_NONE||
           z.MultiSampleType!=D3DMULTISAMPLE_NONE||desc.Width!=z.Width||desc.Height!=z.Height||
           !targets(desc.Width,desc.Height)||!nativeMrt.clearHybrid(surface))return false;
        std::swap(frameTarget,color.p);std::swap(hybridDepth,depth.p);cleared=true;
        nativeMrt.frameTarget(frameTarget,desc);
        return true;
#else
        (void)mainDepth;
        return false;
#endif
    }
    bool hybridDepthMatches(){
        if(!hybridDepth)return true;
        NorthlightDrawSnapshot::Ref<IDirect3DSurface9> activeDepth;
        return SUCCEEDED(d->GetDepthStencilSurface(activeDepth.out()))&&activeDepth.p==hybridDepth;
    }
    template<class Draw,class Prove> HRESULT hybridDraw(Draw submit,IDirect3DVertexShader9* original,Prove prove,bool& written){
#ifdef _WIN64
        if(!hybridDepth||!nativeMrt.acceptingHybrid()||bad||!hybridDepthMatches())return submit();
        ++hybridAttempts;
        // Replays compare against the live depth without changing it. Finish
        // earlier fallback draws before the next native write to preserve depth ties.
        auto result=nativeMrt.draw(submit,surface,frameTarget,width,height,original,true,prove,false,this,
            [](void* owner){return static_cast<NorthlightModelNormals*>(owner)->finishCaptures();},
            drawStateSource?+[](void* owner,unsigned slot,IDirect3DSurface9* target){
                return static_cast<NorthlightModelNormals*>(owner)->drawStateSource->releaseAuxiliaryTarget(slot,target,true);
            }:nullptr);
        if(result.written){written=true;++hybridWritten;++nativeCommittedPackets;++draws;}
        return result.hr;
#else
        (void)original;(void)prove;(void)written;
        return submit();
#endif
    }
#ifdef NORTHLIGHT_REPLAY_STAND
    void setTraceOrdinal(unsigned ordinal){traceOrdinal=ordinal;}
    template<class Log> void reportCoverage(unsigned frame,Log log)const{
        log("MODEL_NORMALS lifted rows frame=%u enabled=%u draws=%u packets=%u",
            frame,unsigned(liftPaletteRows()),paletteWork.liftedDraws,paletteWork.liftedPackets);
        log("MODEL_NORMALS coverage frame=%u packets=%u draws=%u order=0x%016llx",
            frame,coveragePackets,draws-duplicateDraws,(unsigned long long)coverageOrder);
    }
    NorthlightCaptureWork::Work captureWork()const{
        auto work=retiredSnapshotWork;
        work.addSnapshots(snapshots);
        work.packets=queued;
        work.geometryReuse=handoffs;
        work.bankReuse=materialBankReads+terrainMirrorReads;
        work.boundsTests=rangeCache.tested+poseBoundsTests;
        work.floatGets=constantReadRegisters;
        work.floatHandoff=materialBankRegisters+terrainMirrorRegisters;
        work.rangeReads=rangeRegisters;
        work.rangeHandoff=rangeBankRegisters;
        return work;
    }
#endif
    void beginNativeDraw(){nativeCandidate=nullptr;}
    bool nativeSurfaceWrite(IDirect3DSurface9* destination){
        if(hybridDepth){if(destination==hybridDepth){finishCaptures();return true;}return false;}
        if(!pending.empty()&&(destination==frameTarget||nativeMrt.usesDepth(destination)))nativeMrt.cancel("scene-surface-write");
        return false;
    }
    bool nativeDepthChanged(DWORD flags=D3DCLEAR_ZBUFFER){
        if(pending.empty()&&!hybridDepth)return false;
        IDirect3DSurface9* surface=nullptr;bool affected=false;
        if(flags&D3DCLEAR_TARGET){const auto hr=d->GetRenderTarget(0,&surface);affected=FAILED(hr)||surface==frameTarget;release(surface);}
        if(flags&D3DCLEAR_ZBUFFER){const auto hr=d->GetDepthStencilSurface(&surface);affected=affected||FAILED(hr)||(hybridDepth?surface==hybridDepth:nativeMrt.usesDepth(surface));release(surface);}
        if(hybridDepth){if(affected)finishCaptures();return affected;}
        if(affected)nativeMrt.cancel("scene-depth-write");
        return false;
    }
    void clearHybridDepthCopy(){
#ifdef _WIN64
        if(hybridDepth&&!nativeMrt.clearHybrid(surface))bad=true;
#endif
    }
    void clearHybridRegion(DWORD count,const D3DRECT* rectangles){
        if(!hybridDepth)return;
        D3DVIEWPORT9 vp{};
        if(FAILED(d->GetViewport(&vp))){bad=true;return;}
        const RECT bounds={LONG(vp.X),LONG(vp.Y),LONG(std::min<DWORD>(width,vp.X+vp.Width)),LONG(std::min<DWORD>(height,vp.Y+vp.Height))};
        if(!count){
            if(bounds.left==0&&bounds.top==0&&bounds.right==LONG(width)&&bounds.bottom==LONG(height))clearHybridDepthCopy();
            else if(FAILED(d->ColorFill(surface,&bounds,0)))bad=true;
            return;
        }
        for(DWORD i=0;i<count;++i){
            const auto& rect=rectangles[i];
            const RECT clipped={std::max(bounds.left,rect.x1),std::max(bounds.top,rect.y1),std::min(bounds.right,rect.x2),std::min(bounds.bottom,rect.y2)};
            if(clipped.left<clipped.right&&clipped.top<clipped.bottom&&FAILED(d->ColorFill(surface,&clipped,0)))bad=true;
        }
    }
    template<class Draw,class Prove> HRESULT nativeDraw(Draw submit,bool domain,Prove prove){
#ifdef _WIN64
        if(hybridDepth)return submit();
#endif
        if(bad||pending.empty()||!surface||!frameTarget||!nativeMrt.accepting())return submit();
        if(!domain){
            // A scene-depth writer outside the receiver domain owns its visible
            // pixels too. Preserve its native shaders/coverage and write zero
            // normals; no receiver projection or vertex provenance is needed.
            DWORD z=0,write=0;IDirect3DSurface9* ds=nullptr;
            const D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE};DWORD values[2]={};
            if(rasterReader&&rasterReader(rasterContext,keys,2,values)){z=values[0];write=values[1];}
            else if(FAILED(d->GetRenderState(D3DRS_ZENABLE,&z))||FAILED(d->GetRenderState(D3DRS_ZWRITEENABLE,&write))){nativeMrt.cancel("domain-state");return submit();}
            if(!z||!write)return submit();
            const auto hr=d->GetDepthStencilSurface(&ds);const bool scene=nativeMrt.usesDepth(ds);release(ds);
            if(FAILED(hr)){nativeMrt.cancel("domain-depth-state");return submit();}
            if(!scene)return submit();
            return nativeMrt.draw(submit,surface,frameTarget,width,height,nullptr,false,prove).hr;
        }
        auto* packet=nativeCandidate;
        auto result=nativeMrt.draw(submit,surface,frameTarget,width,height,packet?packet->vertex->source:nullptr,packet!=nullptr,prove,packet&&packet->vertex->terrain);
        if(packet){packet->nativeProbed=result.probed;packet->nativeWritten=result.written;}
        return result.hr;
    }
    bool pendingTextures()const{return pendingTextureSlots!=0;}
    bool samplesTexture(void* texture)const{if(!texture||!pendingTextureSlots)return false;for(const auto& p:pending)for(const auto& stage:p->sampled)if(stage.texture==texture)return true;return false;}
    void beforeTextureWrite(void* texture){if(samplesTexture(texture))finishCaptures();}
    void setDeferredCapture(bool on,void(*changed)(void*)=nullptr,void* context=nullptr){
        if(!on)finishCaptures();deferred=on;pendingChanged=changed;pendingContext=context;
    }
    template<class Log> void report(unsigned frame,Log log)const{log("MODEL_NORMALS frame=%u draws=%u queued=%u flushes=%u immediate=%u pending=%zu failed=%u sourceReused=%u sourceReadBytes=%zu sourcePoolBytes=%zu sourcePoolLimitMiB=%zu residentDraws=%u upBytes=%zu handoffs=%u rangeTests=%u rangeCulled=%u rangeSingleTests=%u rangeSingleUniform=%u rangeSingleMixed=%u rangeDeferred=%u poseBoundsTests=%u poseBoundsCulled=%u poseViewportTests=%u poseViewportCulled=%u rangeNarrow=%u rangeFull=%u rangeRegisters=%u packetPixelReadsSkipped=%u compactDraws=%u compactShaders=%u mergedDraws=%u depthSeeded=%u submittedDraws=%u streamedDraws=%u streamedBatches=%u streamedBytes=%zu streamedDiscards=%u streamFailures=%u streamCapacityBytes=%u streamSavedBytes=%zu duplicateDraws=%u pixelShaders=%u sharedPixelShaders=%u vertexShaders=%u sharedVertexShaders=%u rangeMixedCulled=%u residentSourceHits=%u knownTargets=%u knownPixelShaders=%u",frame,draws,queued,flushes,immediate,pending.size(),unsigned(bad),sourceReused,sourceReadBytes,snapshots.sharedSourceBytes(),snapshots.sharedSourceLimit()>>20,residentDraws,upBytes,handoffs,rangeCache.tested,rangeCache.culled,rangeCache.singleTests,rangeCache.singleUniform,rangeCache.singleMixed,rangeCache.deferred,poseBoundsTests,poseBoundsCulled,poseViewportTests,poseViewportCulled,rangeNarrow,rangeFull,rangeRegisters,packetPixelReadsSkipped,compactDraws,programs.compactBuilt,mergedDraws,depthSeeded,draws-mergedDraws-nativeCommittedPackets,streamedDraws,streamedBatches,streamedBytes,streamedDiscards,streamFailures,streamVB?StreamVertexBytes+StreamIndexBytes:0,streamSavedBytes,duplicateDraws,programs.pixelBuilt,programs.pixelReused,programs.vertexBuilt,programs.vertexReused,rangeCache.mixedCulled,residentSourceHits,knownTargets,knownPixelShaders);log("MODEL_NORMALS constants frame=%u fetchedRegisters=%u peakFloatBanks=%zu peakPackets=%zu epochHits=%llu blockHits=%llu registersReused=%llu avoidedBytes=%llu selfChecks=%llu mismatches=%llu",frame,constantReadRegisters,peakFloatBanks,peakPackets,(unsigned long long)constantStats.hits,(unsigned long long)constantStats.blockTests,(unsigned long long)constantStats.registersReused,(unsigned long long)constantStats.bytesAvoided,(unsigned long long)constantStats.selfChecks,(unsigned long long)constantStats.selfCheckMismatches);log("MODEL_NORMALS handoff frame=%u bankReads=%u bankRegisters=%u bankChecks=%u bankMismatch=%u",frame,materialBankReads,materialBankRegisters,materialBankChecks,materialBankMismatches);
        log("MODEL_NORMALS range bank frame=%u reads=%u borrowed=%u legacy=%u",
            frame,rangeRegisters,rangeBankRegisters,unsigned(legacyRangeBank()));
        log("MODEL_NORMALS source frame=%u mirroredSnapshots=%u",frame,mirroredSources);
        const size_t depthBytes=size_t(width)*height*4; // D24S8 logical allocation
        log("MODEL_NORMALS memory frame=%u peakBatchBytes=%zu limitBytes=%zu packetBytes=%zu privateDepthBytes=%zu sharedDepthBytes=%zu sharedMeshBytesAvoided=%zu",frame,peakBatchBytes,batchLimit,sizeof(Pending),depth?depthBytes:0,sharedDepth?depthBytes:0,sharedMeshBytesAvoided);nativeMrt.report(frame,log);
        if(hybridEnabled)log("MODEL_NORMALS hybrid frame=%u active=%u attempts=%u written=%u cpuPackets=%u cpuBatches=%u",frame,unsigned(bool(hybridDepth)),hybridAttempts,hybridWritten,queued,flushes);
    }
    struct DepthSeed {IDirect3DTexture9* texture;float nearZ,farZ,minZ,maxZ;};
    static bool legacyDeclarations(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool enabled=[]{
            const char* value=std::getenv("NORTHLIGHT_RETRACE_LEGACY_NORMAL_DECLARATIONS");
            return value&&std::strcmp(value,"1")==0;
        }();
        return enabled;
#else
        return false;
#endif
    }
    static bool legacyPoseDeclaration(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool enabled=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_LEGACY_NORMAL_POSE_DECLARATION");
            return value&&std::strcmp(value,"1")==0;
        }();
        return enabled;
#else
        return false;
#endif
    }
    template<class Log> void reportReplay(unsigned frame,double cpuMs,Log log)const{
        log("MODEL_NORMALS replay frame=%u cpuMs=%.3f declarationGets=%u sharedDeclarations=%u legacy=%u",
            frame,cpuMs,declarationGets,sharedDeclarations,unsigned(legacyDeclarations()));
        log("MODEL_NORMALS palette instances frame=%u draws=%u packets=%u registers=%u failures=%u planned=%u linkedDraws=%u linkedPackets=%u enabled=%u retainBindings=%u inheritedDraws=%u skippedBinds=%u liftedDraws=%u liftedPackets=%u",
            frame,paletteWork.draws,paletteWork.packets,paletteWork.registers,paletteWork.failures,paletteWork.planned,paletteWork.linkedDraws,paletteWork.linkedPackets,
            unsigned(paletteEnabled()),unsigned(retainPaletteBindings()),paletteWork.inheritedDraws,paletteWork.skippedBinds,
            paletteWork.liftedDraws,paletteWork.liftedPackets);
        log("MODEL_NORMALS pose layout frame=%u gets=%u peeks=%u legacy=%u",
            frame,poseDeclarationGets,poseDeclarationPeeks,unsigned(legacyPoseDeclaration()));
        log("MODEL_NORMALS duplicate scan frame=%u eligiblePackets=%llu scannedPackets=%llu",
            frame,(unsigned long long)duplicateEligiblePackets,(unsigned long long)duplicateScannedPackets);
    }
private:
    bool seedProjection(const DepthSeed* seed,float (&projection)[4])const{
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
        if(!depthSeedEnabled)return false;
#endif
        if(!seed||!seed->texture||cleared||flushes||immediate||pending.size()<8||
           !std::isfinite(seed->nearZ)||!std::isfinite(seed->farZ)||!(seed->nearZ>0&&seed->farZ>seed->nearZ)||
           !std::isfinite(seed->minZ)||!std::isfinite(seed->maxZ)||!(seed->maxZ>seed->minZ))return false;
        D3DSURFACE_DESC desc{};if(FAILED(seed->texture->GetLevelDesc(0,&desc))||desc.Width!=width||desc.Height!=height)return false;
        bool first=true;
        for(const auto& packet:pending){const auto& p=*packet;
            if(p.vertex->rangeKind==NorthlightModelNormalRange::None||p.vertex->first>2||p.vertex->first+p.vertex->count<6||
               p.viewport.X||p.viewport.Y||p.viewport.Width!=width||p.viewport.Height!=height||
               !(p.viewport.MinZ>=0&&p.viewport.MaxZ<=1&&p.viewport.MaxZ>p.viewport.MinZ)||
               (p.raster[1]&0x7fffffff)||(p.raster[2]&0x7fffffff)||!p.raster[5]||p.raster[8]!=D3DFILL_SOLID)return false;
            const float* c=p.constants+8;
            for(unsigned i=0;i<16;++i)if(!std::isfinite(c[i]))return false;
            // The audited VS stores dot(c4,view) / dot(c5,view). Its alpha
            // records positive clip.w; depth must depend on that value alone.
            if(c[8]!=0||c[9]!=0||c[12]!=0||c[13]!=0||c[15]!=0||
               (c[14]!=1&&c[14]!=-1)||!(c[10]/c[14]>1&&c[10]/c[14]<=2)||!(c[11]<0))return false;
            const float current[]={c[10]/c[14],c[11],p.viewport.MinZ,p.viewport.MaxZ-p.viewport.MinZ};
            if(!first&&std::memcmp(projection,current,sizeof current))return false;
            std::copy_n(current,4,projection);first=false;
        }
        return true;
    }
    bool seedDepth(const DepthSeed& seed,const float (&projection)[4]){
        const float view[]={seed.nearZ,seed.farZ,seed.minZ,1/(seed.maxZ-seed.minZ)};
        const D3DVIEWPORT9 viewport={0,0,width,height,0,1};
        struct Quad {float x,y,z,w,u,v;};
        const Quad quad[]={{-.5f,-.5f,0,1,0,0},{float(width)-.5f,-.5f,0,1,1,0},
            {-.5f,float(height)-.5f,0,1,0,1},{float(width)-.5f,float(height)-.5f,0,1,1,1}};
        bool ok=SUCCEEDED(d->SetViewport(&viewport))&&SUCCEEDED(d->SetVertexShader(nullptr))&&
            SUCCEEDED(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1))&&SUCCEEDED(d->SetPixelShader(depthSeedPS))&&
            SUCCEEDED(d->SetPixelShaderConstantF(0,view,1))&&SUCCEEDED(d->SetPixelShaderConstantF(1,projection,1))&&
            SUCCEEDED(d->SetTexture(0,seed.texture));
        const D3DSAMPLERSTATETYPE keys[]={D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_SRGBTEXTURE};
        const DWORD values[]={D3DTEXF_POINT,D3DTEXF_POINT,D3DTEXF_NONE,D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,FALSE};
        for(unsigned i=0;i<6;++i)ok=SUCCEEDED(d->SetSamplerState(0,keys[i],values[i]))&&ok;
        ok=SUCCEEDED(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0))&&ok;
        ok=SUCCEEDED(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE))&&ok;
        ok=SUCCEEDED(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffffu))&&ok;
        ok=SUCCEEDED(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID))&&ok;
        ok=SUCCEEDED(d->SetRenderState(D3DRS_WRAP0,0))&&ok;
        ok=SUCCEEDED(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))&&ok;
        ok=SUCCEEDED(d->SetRenderState(D3DRS_DEPTHBIAS,0))&&ok;
        ok=SUCCEEDED(d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0))&&ok;
        ok=SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,0))&&ok;
        ok=SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS))&&ok;
        if(ok)ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
        if(ok)ok=SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Quad)));
        ok=SUCCEEDED(d->SetTexture(0,nullptr))&&ok;
        ok=SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,15))&&ok;
        ok=SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL))&&ok;
        if(ok){cleared=true;++depthSeeded;}return ok;
    }
public:
#include "model_normal_instancing.inl"
#include "model_normal_replay.inl"
    template<class Draw> void capture(IDirect3DVertexShader9* source,Draw draw,bool userMemory=false,
            const NorthlightDrawSnapshot::Draw* geometry=nullptr,const void* indices=nullptr,D3DFORMAT format=D3DFMT_UNKNOWN,const void* vertices=nullptr,UINT stride=0,
            const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>* captured=nullptr,IDirect3DVertexDeclaration9* capturedDeclaration=nullptr,const NorthlightPoseBounds::Handoff* pose=nullptr,
            std::shared_ptr<const NorthlightDrawSnapshot::Mesh>* published=nullptr,IDirect3DVertexDeclaration9** publishedDeclaration=nullptr,bool terrain=false,
            const NorthlightConstantEpoch::BankHandoff* materialBank=nullptr){
        if(bad||!source)return;
        NorthlightCapturePhases::Scope<CapturePhaseCount> phase(profileCapture?&capturePhases:nullptr,Admission);
#ifdef _WIN64
        if(!hybridDepthMatches())return;
#endif
        // The stream's scope owns this proof for this draw only. Compare
        // against actual capture inputs before admitting a tagged packet.
        auto* nativeSection=NorthlightM2Draw::current;
        // Reuse the current pose's receiver range/clip proof before admission.
        // Solid triangles only; caster capture remains independent of the camera.
        if(pose&&(pose->outsideRange||pose->outsideViewport)&&poseReader&&geometry&&captured&&*captured&&
           (*captured)->topology==geometry->topology&&(*captured)->primitiveCount==geometry->primitives&&(*captured)->indexed==geometry->indexed
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
           &&rangeEnabled
#endif
        ){
            NorthlightConstantEpoch::Stamp current;current.requestHeld=true;++poseBoundsTests;
            const D3DRENDERSTATETYPE keys[]={D3DRS_CLIPPING,D3DRS_FILLMODE};DWORD clipping[2]={};
            const bool triangles=geometry->topology==D3DPT_TRIANGLELIST||geometry->topology==D3DPT_TRIANGLESTRIP||geometry->topology==D3DPT_TRIANGLEFAN;
            const bool clip=!pose->outsideRange&&pose->outsideViewport&&triangles&&renderStates(keys,clipping)&&clipping[0]&&clipping[1]==D3DFILL_SOLID;
            poseViewportTests+=clip;
            if(d->GetNPatchMode()==0){
                // The material handoff pins its layout for this draw. Check
                // the current binding under the gate before using that proof.
                IDirect3DVertexDeclaration9* declaration=nullptr;
                NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> owned;
                if(!legacyPoseDeclaration()&&peekDeclaration(declaration))++poseDeclarationPeeks;
                else{
                    ++poseDeclarationGets;
                    if(SUCCEEDED(d->GetVertexDeclaration(owned.out())))declaration=owned.p;
                }
                if(declaration&&capturedDeclaration==declaration&&poseReader(poseContext,current)&&
                   pose->outside(captured->get(),source,declaration,current,clip)){
                    poseViewportCulled+=clip;++poseBoundsCulled;return;
                }
            }
        }
        const D3DRENDERSTATETYPE admissionKeys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_COLORWRITEENABLE,
            D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,D3DRS_ZFUNC};
        DWORD states[8]={};if(!renderStates(admissionKeys,states)||!states[0]||!states[1]||states[2]||!(states[3]&7))return;
        const DWORD alpha=states[4],ref=states[5],func=states[6],comparison=states[7];
        if(nativeSection){
            const bool indexed=!terrain&&!userMemory&&geometry&&geometry->indexed&&geometry->topology==D3DPT_TRIANGLELIST;
            NorthlightM2Draw::Range range;
            if(indexed)range={geometry->base,geometry->minimum,geometry->vertices,geometry->start,geometry->primitives};
            if(!nativeSections.admit(*nativeSection,indexed?&range:nullptr,!alpha))nativeSection=nullptr;
        }
        if((alpha&&func!=D3DCMP_GREATER&&func!=D3DCMP_GREATEREQUAL)||(comparison!=D3DCMP_LESS&&comparison!=D3DCMP_LESSEQUAL&&comparison!=D3DCMP_EQUAL))return;
        auto* ps=currentPixel(alpha!=0);if(!ps||!ps->shader)return;
        auto* vs=programs.vertex(source,ps->compactVertex,terrain);if(!vs||!vs->shader)return;
        State saved(d,ps->control,userMemory);bool admissionTried=false,admissionOK=false;
        auto admit=[&]{
            if(admissionTried)return admissionOK;admissionTried=true;
            bool sameTarget=false;
            if(deferred&&frameTarget&&drawStateSource&&drawStateSource->knownFrameViewport(frameTarget,sameTarget,saved.viewport)){
                ++knownTargets;
                if(!sameTarget)return false;
            }else if(!saved.save(!deferred)||!saved.rt[0]||(frameTarget&&frameTarget!=saved.rt[0]))return false;
            if(saved.viewport.X||saved.viewport.Y)return false;
            if(frameTarget){
                // The retained surface has immutable dimensions. Reset/endFrame
                // release this identity before another target can be admitted.
                if(width!=saved.viewport.Width||height!=saved.viewport.Height)return false;
            }else{
                D3DSURFACE_DESC desc{};if(FAILED(saved.rt[0]->GetDesc(&desc))||desc.Width!=saved.viewport.Width||desc.Height!=saved.viewport.Height)return false;
                if(!targets(desc.Width,desc.Height)){bad=true;return false;}
                frameTarget=saved.rt[0];frameTarget->AddRef();nativeMrt.frameTarget(frameTarget,desc);
            }
            return admissionOK=true;
        };
        // Establish a frame target before preparing packets: a resize resets
        // source caches/counters. Later far draws can avoid target reads entirely.
        if(!frameTarget&&!admit())return;
        const float control[]={0,terrain?65504.f:48.f,alpha?(float(ref)+(func==D3DCMP_GREATER?.5f:0.f))/255.f:.001f,alpha?1.f:0.f};
        phase.stop();
        if(record(vs,ps,control,saved.viewport,admit,geometry,indices,format,vertices,stride,captured,capturedDeclaration,pose,published,publishedDeclaration,materialBank,nativeSection))return;
        if(!admit())return;
        if(deferred&&!pending.empty()){
            if(!finishCaptures())return;
            if(record(vs,ps,control,saved.viewport,admit,geometry,indices,format,vertices,stride,captured,capturedDeclaration,pose,published,publishedDeclaration,materialBank,nativeSection))return;
        }
        if(deferred&&!saved.saveRest(frameTarget))return;
        // Keep depth-tie ordering when a draw cannot use immutable replay.
        if(!finishCaptures())return;
        auto* drawDepth=replayDepth();if(!drawDepth){bad=true;return;}
        ++immediate;
        saved.changed=true;
        bool ok=SUCCEEDED(d->SetDepthStencilSurface(nullptr));
        for(unsigned i=1;i<4;++i)if(saved.rt[i]){saved.targetChanged|=1u<<i;ok=SUCCEEDED(d->SetRenderTarget(i,nullptr))&&ok;}
        ok=SUCCEEDED(d->SetRenderTarget(0,surface))&&ok;ok=SUCCEEDED(d->SetDepthStencilSurface(drawDepth))&&ok;
        ok=SUCCEEDED(d->SetViewport(&saved.viewport))&&ok;
        // RT1..3 are unbound; their color-write masks cannot affect this pass.
        const DWORD values[]={TRUE,DWORD(!hybridDepth),DWORD(hybridDepth?D3DCMP_EQUAL:D3DCMP_LESSEQUAL),FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,15,0,0};
        // Most native opaque draws already have these values. Track attempted
        // changes before the driver call, so partial failures still restore.
        for(unsigned i=0;i<State::Count;++i)if(values[i]!=saved.values[i]){saved.renderChanged|=1u<<i;ok=SUCCEEDED(d->SetRenderState(State::keys[i],values[i]))&&ok;}
        if(ok&&!cleared){ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));cleared=ok;}
        ok=SUCCEEDED(d->SetVertexShader(vs->shader))&&ok;ok=SUCCEEDED(d->SetPixelShader(ps->shader))&&ok;
        ok=SUCCEEDED(d->SetPixelShaderConstantF(ps->control,control,1))&&ok;
        if(ok)ok=SUCCEEDED(draw());
#ifdef NORTHLIGHT_REPLAY_STAND
        if(ok)recordCoverage(traceOrdinal);
#endif
        ok=saved.restore()&&ok;if(ok){++draws;compactDraws+=unsigned(vs->compact);}else bad=true;
    }
};
