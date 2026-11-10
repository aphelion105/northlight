#pragma once
#include <d3d9.h>
#include <unordered_map>
#include <array>
#include "model_normal_patch.h"
#include "model_normal_slice.h"
#include "model_normal_native.h"
#include "native_material_capture_patch.h"
#include "draw_snapshot.h"
#include "shader_read_set.h"
#include "shader_tags.h"
#include "scene_native_store.h"
#include "vertex_declaration_cache.h"
#include "model_normal_range.h"
#include "pose_bounds_handoff.h"
#include "replay_constants.h"
#include "shader_constant_usage.h"
#include "replay_capture_constants.h"
#include "dynamic_ring.h"
#include "replay_bulk_layout.h"

// Independent of AutoBump and actor-shadow capture budgets. Replay only opaque
// native depth writers; the consumer rejects captures hidden by final depth.
class NorthlightModelNormals {
    template<class T> static void release(T*& p){if(p)p->Release();p=nullptr;}
    template<class Shader> static bool exactCode(Shader* shader,const std::vector<std::uint32_t>& program){
        std::vector<DWORD> exact(program.size());const UINT bytes=UINT(program.size()*sizeof(std::uint32_t));UINT actual=bytes;
        return SUCCEEDED(shader->GetFunction(exact.data(),&actual))&&actual==bytes&&!std::memcmp(exact.data(),program.data(),bytes);
    }
    struct Vertex {IDirect3DVertexShader9 *source=nullptr,*shader=nullptr;NorthlightShaderReads::ReadSet reads;unsigned first=0,count=256;NorthlightModelNormalRange::Kind rangeKind=NorthlightModelNormalRange::None;bool compact=false;std::uint64_t programHash=0;UINT programBytes=0;};
    struct Pixel {IDirect3DPixelShader9 *source=nullptr,*shader=nullptr;unsigned control=0,first=0,count=0;NorthlightShaderReads::ReadSet reads;bool compactVertex=false;Pixel* equivalent=nullptr;std::uint64_t programHash=0;UINT programBytes=0;};
    IDirect3DDevice9* d;
    NorthlightNativeModelNormals nativeMrt;
    NorthlightModelNormalRange::Cache rangeCache;
    void* poseContext=nullptr;NorthlightConstantEpoch::Reader poseReader=nullptr;NorthlightConstantEpoch::KnownSpan poseSpan=nullptr;unsigned poseBoundsTests=0,poseBoundsCulled=0,poseViewportTests=0,poseViewportCulled=0;
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
    friend struct ModelNormalPacketTestAccess;
    friend struct ModelNormalMergeTest;
    friend struct ModelNormalStreamTest;
    bool rangeEnabled=true;
    bool constantReuseEnabled=true;
    bool mergeEnabled=true;
    bool deduplicateEnabled=true;
    bool streamEnabled=true;
    bool pixelSharingEnabled=true;
    bool pixelCanonicalEnabled=true;
#endif
    void* rasterContext=nullptr;bool(*rasterReader)(void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*)=nullptr;
    void* samplerContext=nullptr;bool(*samplerReader)(void*,DWORD,unsigned,DWORD*)=nullptr;
    template<size_t N> bool renderStates(const D3DRENDERSTATETYPE (&keys)[N],DWORD (&out)[N]){
        if(rasterReader&&rasterReader(rasterContext,keys,unsigned(N),out))return true;
        for(unsigned i=0;i<N;++i)if(FAILED(d->GetRenderState(keys[i],out+i)))return false;
        return true;
    }
    std::unordered_map<IDirect3DVertexShader9*,Vertex> vertices;
    // Optional slices never consume the original shader cache's admission.
    std::unordered_map<IDirect3DVertexShader9*,Vertex> compactVertices;
    std::unordered_map<IDirect3DPixelShader9*,Pixel> pixels[2];
    IDirect3DTexture9* normal=nullptr;
    IDirect3DSurface9 *surface=nullptr,*depth=nullptr,*frameTarget=nullptr;
    IDirect3DStateBlock9* batchState=nullptr;
    UINT width=0,height=0;unsigned draws=0,compactDraws=0,compactBuilt=0,vertexBuilt=0,vertexReused=0,pixelBuilt=0,pixelReused=0,mergedDraws=0,duplicateDraws=0,primitiveLimit=0,maxVertexIndex=0;bool cleared=false,bad=false;
    // Private immutable geometry permits one normal/depth pass instead of
    // interrupting the full-resolution MSAA scene for every opaque model.
    inline static constexpr D3DRENDERSTATETYPE rasterKeys[]={D3DRS_CULLMODE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,
        D3DRS_MULTISAMPLEANTIALIAS,D3DRS_MULTISAMPLEMASK,D3DRS_CLIPPING,D3DRS_DITHERENABLE,D3DRS_SHADEMODE,
        D3DRS_FILLMODE,D3DRS_LASTPIXEL,D3DRS_ANTIALIASEDLINEENABLE,D3DRS_WRAP0,D3DRS_WRAP1,D3DRS_WRAP2,
        D3DRS_WRAP3,D3DRS_WRAP4,D3DRS_WRAP6,D3DRS_WRAP8,D3DRS_WRAP9,D3DRS_WRAP10,D3DRS_WRAP11,
        D3DRS_WRAP12,D3DRS_WRAP13,D3DRS_WRAP14,D3DRS_WRAP15};
    struct Pending {
        Vertex* vertex=nullptr;Pixel* pixel=nullptr;
        IDirect3DVertexDeclaration9* declaration=nullptr;
        D3DVIEWPORT9 viewport{};DWORD raster[sizeof(rasterKeys)/sizeof(*rasterKeys)]={};
        float constantStorage[256*4];const float* constants=constantStorage;
        int intStorage[16*4],pi[16*4];const int* ints=intStorage;
        BOOL boolStorage[16],pb[16];const BOOL* bools=boolStorage;
        NorthlightShaderConstants::Usage constantUsage;NorthlightConstantEpoch::Stamp constantStamp;unsigned projectionKind=2;
        std::vector<float> ps;
        NorthlightDrawSnapshot::Mesh mesh;
        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> sharedMesh;
        IDirect3DVertexBuffer9* gpuVertex[4]={};IDirect3DIndexBuffer9* gpuIndex=nullptr;
        NorthlightReplayGPU::Placement placement;bool resident=false,streamed=false,redundant=false,nativeProbed=false,nativeWritten=false;
        const NorthlightDrawSnapshot::Mesh& geometry()const{return sharedMesh?*sharedMesh:mesh;}
        IDirect3DBaseTexture9* textures[16]={};DWORD samplers[16][13]={};
        void recycle(){resident=streamed=redundant=nativeProbed=nativeWritten=false;for(auto*& b:gpuVertex)release(b);release(gpuIndex);sharedMesh.reset();release(declaration);for(auto*& t:textures)release(t);}
        ~Pending(){recycle();}
    };
    Pending* nativeCandidate=nullptr;unsigned nativeCommittedPackets=0;
    static bool sameCapture(const Pending& a,const Pending& b){
        if(a.vertex->shader!=b.vertex->shader||a.vertex->first!=b.vertex->first||a.vertex->count!=b.vertex->count||a.pixel!=b.pixel||a.declaration!=b.declaration||
           std::memcmp(&a.viewport,&b.viewport,sizeof a.viewport)||std::memcmp(a.raster,b.raster,sizeof a.raster))return false;
        const auto first=a.vertex->first,count=a.vertex->count;
        if(a.constants!=b.constants&&std::memcmp(a.constants+first*4,b.constants+first*4,size_t(count)*16))return false;
        if(a.ps.size()!=b.ps.size()||(!a.ps.empty()&&std::memcmp(a.ps.data(),b.ps.data(),a.ps.size()*sizeof(float))))return false;
        if((a.vertex->reads.i&&std::memcmp(a.ints,b.ints,sizeof a.intStorage))||
           (a.vertex->reads.b&&std::memcmp(a.bools,b.bools,sizeof a.boolStorage))||
           (a.pixel->reads.i&&std::memcmp(a.pi,b.pi,sizeof a.pi))||
           (a.pixel->reads.b&&std::memcmp(a.pb,b.pb,sizeof a.pb)))return false;
        for(unsigned i=0;i<16;++i)if(a.pixel->reads.samplers&(1u<<i))
            if(a.textures[i]!=b.textures[i]||std::memcmp(a.samplers[i],b.samplers[i],sizeof a.samplers[i]))return false;
        return true;
    }
    void findRedundant(){
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
        if(!mergeEnabled||!deduplicateEnabled)return;
#endif
        // This private pass uses opaque overwrite, depth writes and LESS_EQUAL.
        // Only the last identical draw can win a depth tie; earlier copies
        // cannot change the final minimum depth or colour at any pixel.
        // Keep packets alive: their constant banks may be shared by later draws.
        std::array<std::uint16_t,4096> recent{};
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
            }
        }
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
               m.streams[0].stride!=mesh.streams[0].stride||!sameCapture(first,next))break;
            // Repeating the exact opaque call cannot change this private target.
            // Do not remove it after a different geometry range joined the run.
            if(run.count==mesh.primitiveCount&&run.min==first.placement.minimumVertex()&&run.vertices==mesh.vertexCount&&
               next.placement.indexStart==first.placement.indexStart&&next.placement.vertexBase==first.placement.vertexBase&&
               m.vertexCount==mesh.vertexCount&&m.primitiveCount==mesh.primitiveCount){++run.packets;++at;continue;}
            if(next.redundant)break;
            if(uint64_t(first.placement.indexStart)+uint64_t(run.count)*3!=next.placement.indexStart)break;
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
    NorthlightGpuScene::NativeClient* geometryCache=nullptr;
    std::vector<std::unique_ptr<Pending>> pending,spare;
    // Recycled buffers and queued geometry share the same 8 MiB allowance.
    size_t pendingBytes=0,spareBytes=0;unsigned pendingTextureSlots=0;bool deferred=false,sourceSharing=false;
    static size_t packetBytes(const Pending& p){return sizeof(Pending)+p.ps.capacity()*sizeof(float)+p.mesh.capacityBytes()+(p.sharedMesh?p.sharedMesh->capacityBytes():0);}
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
        for(;last<pending.size();++last){auto& p=*pending[last];const auto& m=p.geometry();const auto bytes=m.streams[0].bytes.size();
            if(p.resident||!m.indexed||m.topology!=D3DPT_TRIANGLELIST||m.streams[0].stride!=stride||!bytes||bytes%stride||
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
            if(memory)for(size_t i=first;i<last;++i)if(streamLayout.unique(i-first)){const auto& p=*pending[i];const auto& bytes=p.geometry().streams[0].bytes;std::memcpy(static_cast<uint8_t*>(memory)+size_t(p.placement.vertexBase)*stride,bytes.data(),bytes.size());}
            NorthlightLockMeter::staging(vertices);auto hr=streamVB->Unlock();if(!memory||FAILED(hr))return failed();
            memory=nullptr;if(FAILED(streamIB->Lock(islot.offset,UINT(indices),&memory,islot.flags)))return failed();
            if(memory)for(size_t i=first;i<last;++i)if(streamLayout.unique(i-first)){const auto& p=*pending[i];const auto& m=p.geometry();auto* target=static_cast<uint32_t*>(memory)+p.placement.indexStart;
                const auto base=vslot.offset/stride+p.placement.vertexBase;for(auto index:m.indices)*target++=index+base;}
            NorthlightLockMeter::staging(indices);hr=streamIB->Unlock();if(!memory||FAILED(hr))return failed();
            UINT base=vslot.offset/stride,start=islot.offset/4;
            for(size_t i=first;i<last;++i){auto& p=*pending[i];
                for(auto& vb:p.gpuVertex)release(vb);release(p.gpuIndex);
                p.gpuVertex[0]=streamVB;streamVB->AddRef();p.gpuIndex=streamIB;streamIB->AddRef();p.placement.vertexBase+=base;p.placement.indexStart+=start;p.resident=p.streamed=true;
            }
            ++streamedBatches;streamedBytes+=vertices+indices;streamSavedBytes+=saved;return true;
        }catch(...){return failed();}
    }
    void(*pendingChanged)(void*)=nullptr;void* pendingContext=nullptr;
    unsigned queued=0,flushes=0,immediate=0,sourceReused=0,residentDraws=0,handoffs=0,rangeNarrow=0,rangeFull=0,rangeRegisters=0,packetPixelReadsSkipped=0;size_t sourceReadBytes=0,upBytes=0;
    NorthlightReplayCaptureConstants::Stats constantStats;
    unsigned constantReadRegisters=0;
    void clearPending(){nativeCandidate=nullptr;const bool hadPending=!pending.empty();
        for(auto& p:pending){p->recycle();spareBytes+=packetBytes(*p);spare.push_back(std::move(p));}
        pending.clear();pendingBytes=0;pendingTextureSlots=0;snapshots.clearFrame();if(hadPending&&pendingChanged)pendingChanged(pendingContext);}
    template<class Admit> bool record(Vertex* vs,Pixel* ps,const float* control,const D3DVIEWPORT9& viewport,Admit& admit,const NorthlightDrawSnapshot::Draw* geometry,
                const void* indices,D3DFORMAT format,const void* vertices,UINT stride,
                const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>* captured,IDirect3DVertexDeclaration9* capturedDeclaration,const NorthlightPoseBounds::Handoff* pose){
        if(!deferred||!vs->reads.exact||vs->reads.samplers||!ps->reads.exact||!geometry||pending.size()>=2048||d->GetNPatchMode()!=0)return false;
        try{
            std::unique_ptr<Pending> packet;
            if(spare.empty())packet=std::make_unique<Pending>();
            else {packet=std::move(spare.back());spare.pop_back();spareBytes-=packetBytes(*packet);}
            NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> declaration;
            if(FAILED(d->GetVertexDeclaration(declaration.out()))||!declaration.p)return false;
            // The material producer hands off the immutable RAW source from
            // this same serialized draw, before geometry replacement/dedup.
            if(captured&&*captured&&capturedDeclaration==declaration.p&&(*captured)->topology==geometry->topology&&
               (*captured)->primitiveCount==geometry->primitives&&(*captured)->indexed==geometry->indexed){packet->sharedMesh=*captured;++sourceReused;++handoffs;}
            else if(vertices){if(!snapshots.readUP(declaration.p,*geometry,indices,format,vertices,stride,packet->mesh))return false;}
            else {
                // Use the existing bounded source pool even when AutoBump did
                // not capture this draw. Current write revisions validate hits.
                const auto hits=snapshots.snapshotCacheHits();const auto read=snapshots.bytesRead();
                const bool captured=snapshots.read(d,declaration.p,*geometry,packet->mesh,nullptr,false,sourceSharing?&packet->sharedMesh:nullptr);
                sourceReadBytes+=snapshots.bytesRead()-read;sourceReused+=snapshots.snapshotCacheHits()-hits;
                if(!captured)return false;
            }
            const auto& mesh=packet->geometry();
            if(mesh.streams[0].bytes.empty())return false;
            for(unsigned i=1;i<4;++i)if(!mesh.streams[i].bytes.empty())return false;
            if(vs->rangeKind==NorthlightModelNormalRange::Envelope&&pose&&pose->bounds.valid&&poseReader&&packet->sharedMesh
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
               &&rangeEnabled
#endif
            ){++poseBoundsTests;NorthlightConstantEpoch::Stamp current;current.requestHeld=true;
                if(poseReader(poseContext,current)&&pose->outside(packet->sharedMesh.get(),vs->source,declaration.p,current)){
                    ++poseBoundsCulled;packet->recycle();spareBytes+=packetBytes(*packet);spare.push_back(std::move(packet));return true;}}
            bool vsCaptured=false;
            if((vs->rangeKind==NorthlightModelNormalRange::Rigid||vs->rangeKind==NorthlightModelNormalRange::Single)&&packet->sharedMesh
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
               &&rangeEnabled
#endif
            ){
                const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;bool outside=false,known=false;
                if(declarations.get(declaration.p,elements,count)){
                    namespace E=NorthlightConstantEpoch;E::Stamp before;before.requestHeld=true;
                    known=poseSpan&&poseReader&&poseReader(poseContext,before)&&before.held;
                    if(known)outside=rangeCache.outsideRead(packet->sharedMesh,elements,count,[&](unsigned first,unsigned n,float* out){
                        E::Stamp copied;const bool valid=poseSpan(poseContext,first,n,out,copied)&&E::exact(before,copied);known&=valid;if(valid)rangeRegisters+=n;return valid;
                    },vs->rangeKind);
                    if(known)++rangeNarrow;
                    else {constantReadRegisters+=256;if(FAILED(d->GetVertexShaderConstantF(0,packet->constantStorage,256)))return false;vsCaptured=true;++rangeFull;rangeRegisters+=256;
                        outside=rangeCache.outside(packet->sharedMesh,elements,count,packet->constantStorage,vs->rangeKind);}
                }
                if(outside){
                    packet->recycle();spareBytes+=packetBytes(*packet);spare.push_back(std::move(packet));return true;}}
            if(!admit())return false;
            // Proven out-of-range draws need no target/texture/sampler capture.
            // Render-target inputs otherwise retain the immediate path;
            // ordinary inputs use the serialized texture-write guard.
            for(unsigned i=0;i<16;++i)if(ps->reads.samplers&(1u<<i)){
                NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> texture;
                if(FAILED(d->GetTexture(i,texture.out())))return false;
                if(!samplerReader||!samplerReader(samplerContext,i,(1u<<13)-1,packet->samplers[i]))
                    for(unsigned s=0;s<13;++s)if(FAILED(d->GetSamplerState(i,D3DSAMPLERSTATETYPE(s+1),packet->samplers[i]+s)))return false;
                if(!texture.p)continue;
                D3DSURFACE_DESC desc{};
                if(texture->GetType()!=D3DRTYPE_TEXTURE||
                   FAILED(static_cast<IDirect3DTexture9*>(texture.p)->GetLevelDesc(0,&desc))||
                   (desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL)))return false;
                packet->textures[i]=texture.p;texture.p=nullptr;
            }
            // A resident GPU lease already owns immutable source bytes. Keep
            // only draw metadata, so the CPU budget does not split warm scenes.
            if(geometryCache&&packet->sharedMesh&&packet->sharedMesh->sourceIdentity&&
               geometryCache->bind(d,packet->sharedMesh,packet->gpuVertex,packet->gpuIndex,[](size_t){return false;},packet->placement,true)){
                NorthlightDrawSnapshot::Mesh resident;
                resident.topology=mesh.topology;resident.vertexCount=mesh.vertexCount;resident.primitiveCount=mesh.primitiveCount;
                resident.indexed=mesh.indexed;resident.streams[0].stride=mesh.streams[0].stride;
                packet->mesh=std::move(resident);packet->sharedMesh.reset();packet->resident=true;
            }
            packet->ps.resize(ps->count*4);
            const size_t bytes=packetBytes(*packet);
            while(!spare.empty()&&bytes+pendingBytes+spareBytes>(8u<<20)){spareBytes-=packetBytes(*spare.back());spare.pop_back();}
            if(bytes>(8u<<20)-pendingBytes)return false;
            DWORD planes[1]={};const D3DRENDERSTATETYPE planeKeys[]={D3DRS_CLIPPLANEENABLE};
            if(!renderStates(planeKeys,planes)||planes[0]||!renderStates(rasterKeys,packet->raster))return false;
            packet->constantUsage.floats={vs->first,vs->count};packet->constantUsage.booleans={0,vs->reads.b?16u:0u};packet->constantUsage.integers={0,vs->reads.i?16u:0u};
            auto reader=vsCaptured?nullptr:poseReader;
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
            if(!constantReuseEnabled)reader=nullptr;
#endif
            auto fetchFloats=[&](unsigned first,unsigned count,float* out){constantReadRegisters+=count;return SUCCEEDED(d->GetVertexShaderConstantF(first,out,count));};
            auto fetch=[&]{return (vsCaptured||!vs->count||fetchFloats(vs->first,vs->count,packet->constantStorage+vs->first*4))&&
                (!vs->reads.i||SUCCEEDED(d->GetVertexShaderConstantI(0,packet->intStorage,16)))&&(!vs->reads.b||SUCCEEDED(d->GetVertexShaderConstantB(0,packet->boolStorage,16)));};
            // The existing epoch helper flattens shared banks to an earlier
            // accepted packet. Every owner stays alive until this batch flushes.
            if(!NorthlightReplayCaptureConstants::captureBlocks(*packet,pending.empty()?nullptr:pending.back().get(),poseContext,reader,fetch,fetchFloats,&constantStats))return false;
            // This packet overwrites its complete float bank with control below.
            if(ps->first==ps->control&&ps->count==1)++packetPixelReadsSkipped;
            else if(FAILED(d->GetPixelShaderConstantF(ps->first,packet->ps.data(),ps->count)))return false;
            if((ps->reads.i&&FAILED(d->GetPixelShaderConstantI(0,packet->pi,16)))||(ps->reads.b&&FAILED(d->GetPixelShaderConstantB(0,packet->pb,16))))return false;
            packet->viewport=viewport;packet->declaration=declaration.p;declaration.p=nullptr;
            packet->vertex=vs;packet->pixel=ps;
            std::copy(control,control+4,packet->ps.data()+(ps->control-ps->first)*4);
            unsigned textureSlots=0;for(unsigned i=0;i<16;++i)if(packet->textures[i])textureSlots|=1u<<i;
            const bool texturesWerePending=pendingTextureSlots!=0;
            pending.push_back(std::move(packet));nativeCandidate=pending.back().get();pendingBytes+=bytes;++queued;pendingTextureSlots|=textureSlots;
            // Only the first sampled texture changes the write guard. Its
            // callback also ends the native coverage MRT batch.
            if(!texturesWerePending&&pendingTextureSlots&&pendingChanged)pendingChanged(pendingContext);return true;
        }catch(const std::bad_alloc&){return false;}
    }
    struct State {
        inline static constexpr D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,
            D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,
            D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_COLORWRITEENABLE,D3DRS_WRAP5,D3DRS_WRAP7};
        static constexpr unsigned Count=sizeof(keys)/sizeof(keys[0]);
        IDirect3DDevice9* d;IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
        IDirect3DSurface9 *rt[4]={},*ds=nullptr;IDirect3DVertexBuffer9* stream=nullptr;IDirect3DIndexBuffer9* index=nullptr;
        UINT offset=0,stride=0;D3DVIEWPORT9 viewport{};DWORD values[Count]={};float constant[4]={};unsigned control,renderChanged=0,targetChanged=0;bool changed=false,up;
        State(IDirect3DDevice9* device,unsigned c,bool user):d(device),control(c),up(user){}
        bool save(bool full=true){
            if(FAILED(d->GetViewport(&viewport))||FAILED(d->GetRenderTarget(0,&rt[0])))return false;
            return !full||saveRest();
        }
        bool saveRest(){
            if(FAILED(d->GetVertexShader(&vs))||FAILED(d->GetPixelShader(&ps))||
               FAILED(d->GetDepthStencilSurface(&ds))||FAILED(d->GetPixelShaderConstantF(control,constant,1)))return false;
            for(unsigned i=1;i<4;++i){auto hr=d->GetRenderTarget(i,&rt[i]);if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return false;}
            for(unsigned i=0;i<Count;++i)if(FAILED(d->GetRenderState(keys[i],values+i)))return false;
            return !up||(SUCCEEDED(d->GetStreamSource(0,&stream,&offset,&stride))&&SUCCEEDED(d->GetIndices(&index)));
        }
        bool restore(){
            if(!changed)return true;changed=false;
            auto retry=[](auto call){return SUCCEEDED(call())||SUCCEEDED(call());};bool ok=true;
            ok=retry([&]{return d->SetDepthStencilSurface(nullptr);})&&ok;
            for(unsigned i=1;i<4;++i)if(targetChanged&(1u<<i))ok=retry([&]{return d->SetRenderTarget(i,nullptr);})&&ok;
            for(unsigned i=0;i<4;++i)if(!i||(targetChanged&(1u<<i)))ok=retry([&]{return d->SetRenderTarget(i,rt[i]);})&&ok;
            ok=retry([&]{return d->SetDepthStencilSurface(ds);})&&ok;
            ok=retry([&]{return d->SetViewport(&viewport);})&&ok;
            ok=retry([&]{return d->SetVertexShader(vs);})&&ok;ok=retry([&]{return d->SetPixelShader(ps);})&&ok;
            ok=retry([&]{return d->SetPixelShaderConstantF(control,constant,1);})&&ok;
            for(unsigned i=0;i<Count;++i)if(renderChanged&(1u<<i))ok=retry([&]{return d->SetRenderState(keys[i],values[i]);})&&ok;
            if(up){ok=retry([&]{return d->SetStreamSource(0,stream,offset,stride);})&&ok;ok=retry([&]{return d->SetIndices(index);})&&ok;}
            return ok;
        }
        ~State(){restore();release(vs);release(ps);for(auto*& p:rt)release(p);release(ds);release(stream);release(index);}
    };
    Vertex* vertex(IDirect3DVertexShader9* source,bool compact=false){
        auto& cache=compact?compactVertices:vertices;
        auto it=cache.find(source);if(it!=cache.end())return compact&&!it->second.shader?vertex(source):&it->second;
        if(cache.size()>=(compact?64u:512u))return compact?vertex(source):nullptr;
        Vertex v;v.source=source;source->AddRef();UINT bytes=0;
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&bytes%4==0){
            std::vector<DWORD> code(bytes/4);std::vector<std::uint32_t> patched;
            if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&NorthlightModelNormal::vertex(reinterpret_cast<const uint32_t*>(code.data()),code.size(),patched)){
                v.rangeKind=NorthlightModelNormalRange::classify(reinterpret_cast<const uint32_t*>(code.data()),code.size(),true);
                if(compact){std::vector<std::uint32_t> sliced;
                    if(!NorthlightModelNormalSlice::vertex(patched.data(),patched.size(),sliced)||sliced.size()>=patched.size()){
                        cache.emplace(source,v);return vertex(source);
                    }
                    std::vector<std::uint32_t> canonical;
                    if(NorthlightModelNormalSlice::canonicalVertex(sliced.data(),sliced.size(),canonical))sliced.swap(canonical);
                    patched.swap(sliced);v.compact=true;
                }
                v.reads=NorthlightShaderReads::scan(patched.data(),patched.size());
                if(v.reads.exact){
                    unsigned first=256,last=0;
                    for(unsigned c=0;c<256;++c)if(v.reads.f[c>>6]&(1ull<<(c&63))){first=std::min(first,c);last=c+1;}
                    v.first=last?first:0;v.count=last-v.first;
                }
                v.programBytes=UINT(patched.size()*sizeof(std::uint32_t));v.programHash=NorthlightShaderTags::fnv1a(patched.data(),v.programBytes);
                for(auto& item:cache){const auto& existing=item.second;
                    if(existing.shader&&existing.compact==v.compact&&existing.programBytes==v.programBytes&&existing.programHash==v.programHash&&exactCode(existing.shader,patched)){
                        v.shader=existing.shader;v.shader->AddRef();++vertexReused;break;}}
                if(!v.shader){const HRESULT hr=d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&v.shader);
                    if(compact&&FAILED(hr))release(v.shader);
                    if(v.shader){++vertexBuilt;compactBuilt+=v.compact;}}
            }
        }
        auto* result=&cache.emplace(source,v).first->second;
        return compact&&!result->shader?vertex(source):result;
    }
    Pixel* pixel(IDirect3DPixelShader9* source,bool alpha){
        auto& map=pixels[alpha];auto it=map.find(source);if(it!=map.end())return it->second.equivalent?it->second.equivalent:&it->second;if(map.size()>=512)return nullptr;
        Pixel v;v.source=source;source->AddRef();UINT bytes=0;
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&bytes%4==0){
            std::vector<DWORD> code(bytes/4);std::vector<std::uint32_t> patched;
            if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&NorthlightNativeMaterialCapture::patch(reinterpret_cast<const uint32_t*>(code.data()),code.size(),patched,v.control,alpha,false,true)){
                // The original lighting can differ while receiver bytecode is
                // identical. Share an existing cache node before asking D3D to
                // create another program; no extra lookup on a warm source hit.
                const auto create=[&](const std::vector<std::uint32_t>& program,bool compact){
                    const UINT bytes=UINT(program.size()*sizeof(std::uint32_t));const auto hash=NorthlightShaderTags::fnv1a(program.data(),bytes);
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
                    if(pixelSharingEnabled)
#endif
                    for(auto& cache:pixels)for(auto& item:cache){auto& existing=item.second;
                        if(!existing.shader||existing.control!=v.control||existing.compactVertex!=compact||existing.programBytes!=bytes||existing.programHash!=hash)continue;
                        // Hashes only shortlist candidates. Ask the retained
                        // shader for exact bytes, without another bytecode cache.
                        if(exactCode(existing.shader,program)){
                            v.equivalent=&existing;++pixelReused;return true;}}
                    if(FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(program.data()),&v.shader))||!v.shader){release(v.shader);return false;}
                    v.programHash=hash;v.programBytes=bytes;v.compactVertex=compact;++pixelBuilt;return true;
                };
                std::vector<std::uint32_t> sliced,canonical;const auto originalControl=v.control;
                const bool compact=NorthlightModelNormalSlice::pixel(patched.data(),patched.size(),sliced);
                if(compact
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
                    &&pixelCanonicalEnabled
#endif
                    &&NorthlightModelNormalSlice::canonicalPixel(sliced.data(),sliced.size(),v.control,canonical)){sliced.swap(canonical);v.control=0;}
                if(compact&&create(sliced,true))patched.swap(sliced);
                else {v.control=originalControl;create(patched,false);}
                if(v.equivalent){auto* result=v.equivalent;map.emplace(source,std::move(v));return result;}
                v.reads=NorthlightShaderReads::scan(patched.data(),patched.size());
                v.reads.samplers=v.reads.sampledSamplers;
                unsigned last=v.control+1;v.first=v.control;
                for(unsigned c=0;c<224;++c)if(v.reads.f[c>>6]&(1ull<<(c&63))){v.first=std::min(v.first,c);last=std::max(last,c+1);}
                v.count=last-v.first;
            }
        }
        return &map.emplace(source,std::move(v)).first->second;
    }
    bool targets(UINT w,UINT h){
        if(w!=width||h!=height){reset(true);width=w;height=h;}
        if(normal&&surface&&depth)return true;
        const auto bytes=uint64_t(w)*h*8;MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
        if(bytes>(64ull<<20)||!GlobalMemoryStatusEx(&memory)||memory.ullAvailVirtual<bytes*2+(128ull<<20))return false;
        if(FAILED(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&normal,nullptr))||
           FAILED(normal->GetSurfaceLevel(0,&surface))||FAILED(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr))){reset(true);return false;}
        return true;
    }
public:
    explicit NorthlightModelNormals(IDirect3DDevice9* device):d(device),nativeMrt(device){snapshots.setSnapshotCacheLimit(0);pending.reserve(128);spare.reserve(128);
        D3DCAPS9 caps{};if(d&&SUCCEEDED(d->GetDeviceCaps(&caps))){primitiveLimit=caps.MaxPrimitiveCount;maxVertexIndex=caps.MaxVertexIndex;}
        snapshots.setDeclarationProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& elements,UINT& count){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(decl,elements,count);});
        snapshots.setLayoutProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(decl,extent);});
    }
    ~NorthlightModelNormals(){pendingChanged=nullptr;reset();for(auto* map:{&vertices,&compactVertices})for(auto& v:*map){release(v.second.source);release(v.second.shader);}for(auto& map:pixels)for(auto& v:map){release(v.second.source);release(v.second.shader);}}
    void endFrame(){clearPending();nativeMrt.beginFrame();nativeCommittedPackets=0;streamFence.endFrame();streamedDraws=streamedBatches=streamedDiscards=streamFailures=0;streamedBytes=streamSavedBytes=0;streamBlocked=false;rangeCache.beginFrame();constantStats={};constantReadRegisters=0;poseBoundsTests=poseBoundsCulled=poseViewportTests=poseViewportCulled=0;compactDraws=draws=queued=flushes=immediate=sourceReused=residentDraws=handoffs=rangeNarrow=rangeFull=rangeRegisters=packetPixelReadsSkipped=mergedDraws=duplicateDraws=0;sourceReadBytes=upBytes=0;cleared=bad=false;release(frameTarget);}
    void reset(bool preserveCaptureSources=false){endFrame();nativeMrt.reset();releaseStream();rangeCache.clear();if(!preserveCaptureSources)snapshots.clearSnapshotCache();declarations.clear();spare.clear();spareBytes=0;release(batchState);release(surface);release(normal);release(depth);width=height=0;}
    IDirect3DTexture9* texture()const{return draws&&!bad?normal:nullptr;}
    bool shareCaptureSources(NorthlightDrawSnapshot::Frame& source,NorthlightDrawSnapshot::Frame::IdentityFn identity,
                            NorthlightDrawSnapshot::Frame::VersionFn version,NorthlightCaptureMetadata::Read metadata){
        snapshots.setIdentityProvider(identity);snapshots.setVersionProvider(version);snapshots.setMetadataProvider(metadata);
        snapshots.setChargeResidentHits(false);sourceSharing=snapshots.shareSnapshotPool(source);return sourceSharing;
    }
    void setRasterStateSource(void* context,bool(*reader)(void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*)){rasterContext=context;rasterReader=context==d?reader:nullptr;nativeMrt.setRasterStateSource(context,reader);}
    void setEmptyTargetSource(void* context,bool(*reader)(void*)){nativeMrt.setEmptyTargetSource(context,reader);}
    void setSamplerStateSource(void* context,bool(*reader)(void*,DWORD,unsigned,DWORD*)){samplerContext=context;samplerReader=context==d?reader:nullptr;}
    void setTrustedPoseSource(NorthlightConstantEpoch::TrustedSource source){for(auto& p:pending)p->constantStamp={};poseContext=source.context();poseReader=source&&source.context()==d?source.reader():nullptr;poseSpan=poseReader?source.span():nullptr;}
    void setGeometryCache(NorthlightGpuScene::NativeClient* cache){geometryCache=cache;}
    unsigned cachedDraws()const{return residentDraws;}
    unsigned reusedSources()const{return sourceReused;}
    size_t readSourceBytes()const{return sourceReadBytes;}
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
    size_t testPendingPackets()const{return pending.size();}
    void testRange(bool enabled){rangeEnabled=enabled;}
    unsigned testRangeCulled()const{return rangeCache.culled+poseBoundsCulled;}
    size_t testRangeBytes()const{return sizeof(rangeCache);}
    unsigned testRangeRegisters()const{return rangeRegisters;}
    void testConstantReuse(bool enabled){constantReuseEnabled=enabled;for(auto& p:pending)p->constantStamp={};}
    const NorthlightReplayCaptureConstants::Stats& testConstantStats()const{return constantStats;}
    unsigned testConstantReadRegisters()const{return constantReadRegisters;}
#endif
    void enableNativeMrt(bool on){nativeMrt.configure(on);}
    void beginNativeDraw(){nativeCandidate=nullptr;}
    void nativeSurfaceWrite(IDirect3DSurface9* destination){if(!pending.empty()&&(destination==frameTarget||nativeMrt.usesDepth(destination)))nativeMrt.cancel("scene-surface-write");}
    void nativeDepthChanged(DWORD flags=D3DCLEAR_ZBUFFER){
        if(pending.empty())return;
        IDirect3DSurface9* surface=nullptr;bool affected=false;
        if(flags&D3DCLEAR_TARGET){const auto hr=d->GetRenderTarget(0,&surface);affected=FAILED(hr)||surface==frameTarget;release(surface);}
        if(flags&D3DCLEAR_ZBUFFER){const auto hr=d->GetDepthStencilSurface(&surface);affected=affected||FAILED(hr)||nativeMrt.usesDepth(surface);release(surface);}
        if(affected)nativeMrt.cancel("scene-depth-write");
    }
    template<class Draw,class Prove> HRESULT nativeDraw(Draw submit,bool domain,Prove prove){
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
        auto result=nativeMrt.draw(submit,surface,frameTarget,width,height,packet?packet->vertex->source:nullptr,packet!=nullptr,prove);
        if(packet){packet->nativeProbed=result.probed;packet->nativeWritten=result.written;}
        return result.hr;
    }
    bool pendingTextures()const{return pendingTextureSlots!=0;}
    bool samplesTexture(void* texture)const{if(!texture||!pendingTextureSlots)return false;for(const auto& p:pending)for(unsigned i=0;i<16;++i)if((pendingTextureSlots&(1u<<i))&&p->textures[i]==texture)return true;return false;}
    void beforeTextureWrite(void* texture){if(samplesTexture(texture))finishCaptures();}
    void setDeferredCapture(bool on,void(*changed)(void*)=nullptr,void* context=nullptr){
        if(!on)finishCaptures();deferred=on;pendingChanged=changed;pendingContext=context;
    }
    template<class Log> void report(unsigned frame,Log log)const{log("MODEL_NORMALS frame=%u draws=%u queued=%u flushes=%u immediate=%u pending=%zu failed=%u sourceReused=%u sourceReadBytes=%zu sourcePoolBytes=%zu sourcePoolLimitMiB=%zu residentDraws=%u upBytes=%zu handoffs=%u rangeTests=%u rangeCulled=%u rangeSingleTests=%u rangeSingleUniform=%u rangeSingleMixed=%u rangeDeferred=%u poseBoundsTests=%u poseBoundsCulled=%u poseViewportTests=%u poseViewportCulled=%u rangeNarrow=%u rangeFull=%u rangeRegisters=%u packetPixelReadsSkipped=%u compactDraws=%u compactShaders=%u mergedDraws=%u submittedDraws=%u streamedDraws=%u streamedBatches=%u streamedBytes=%zu streamedDiscards=%u streamFailures=%u streamCapacityBytes=%u streamSavedBytes=%zu duplicateDraws=%u pixelShaders=%u sharedPixelShaders=%u vertexShaders=%u sharedVertexShaders=%u",frame,draws,queued,flushes,immediate,pending.size(),unsigned(bad),sourceReused,sourceReadBytes,snapshots.sharedSourceBytes(),snapshots.sharedSourceLimit()>>20,residentDraws,upBytes,handoffs,rangeCache.tested,rangeCache.culled,rangeCache.singleTests,rangeCache.singleUniform,rangeCache.singleMixed,rangeCache.deferred,poseBoundsTests,poseBoundsCulled,poseViewportTests,poseViewportCulled,rangeNarrow,rangeFull,rangeRegisters,packetPixelReadsSkipped,compactDraws,compactBuilt,mergedDraws,draws-mergedDraws-nativeCommittedPackets,streamedDraws,streamedBatches,streamedBytes,streamedDiscards,streamFailures,streamVB?StreamVertexBytes+StreamIndexBytes:0,streamSavedBytes,duplicateDraws,pixelBuilt,pixelReused,vertexBuilt,vertexReused);log("MODEL_NORMALS constants frame=%u fetchedRegisters=%u epochHits=%llu blockHits=%llu registersReused=%llu avoidedBytes=%llu selfChecks=%llu mismatches=%llu",frame,constantReadRegisters,(unsigned long long)constantStats.hits,(unsigned long long)constantStats.blockTests,(unsigned long long)constantStats.registersReused,(unsigned long long)constantStats.bytesAvoided,(unsigned long long)constantStats.selfChecks,(unsigned long long)constantStats.selfCheckMismatches);nativeMrt.report(frame,log);}
    bool finishCaptures(bool final=false){
        if(final){
            bool probed=true,written=true;for(const auto& p:pending){probed=probed&&p->nativeProbed;written=written&&p->nativeWritten;}
            if(!bad&&nativeMrt.finish(probed,written,unsigned(pending.size()))){
                nativeCommittedPackets+=unsigned(pending.size());draws+=unsigned(pending.size());clearPending();return true;
            }
        }else nativeMrt.cancel("capture-hazard");
        if(pending.empty())return !bad;
        bool ok=!bad;
        if(ok&&geometryCache){
            // Reuse the existing scene budget/promotion limits. Every packet
            // keeps buffer references across eviction; misses retain UP replay.
            for(auto& p:pending)if(p->sharedMesh&&p->sharedMesh->sourceIdentity)
                p->resident=geometryCache->bind(d,p->sharedMesh,p->gpuVertex,p->gpuIndex,[](size_t){return true;},p->placement);
            if(geometryCache->commitPending())
                for(auto& p:pending)if(!p->resident&&p->sharedMesh&&p->sharedMesh->sourceIdentity)
                    p->resident=geometryCache->bind(d,p->sharedMesh,p->gpuVertex,p->gpuIndex,[](size_t){return true;},p->placement,true);
        }
        if(ok)findRedundant();
        {
            State saved(d,0,true);
            ok=saved.save()&&SUCCEEDED(batchState?batchState->Capture():d->CreateStateBlock(D3DSBT_ALL,&batchState))&&ok;
            if(ok){
                saved.changed=true;saved.targetChanged=14;saved.renderChanged=(1u<<State::Count)-1;
                ok=SUCCEEDED(d->SetDepthStencilSurface(nullptr));
                for(unsigned i=1;i<4;++i)ok=SUCCEEDED(d->SetRenderTarget(i,nullptr))&&ok;
                ok=SUCCEEDED(d->SetRenderTarget(0,surface))&&ok;
                ok=SUCCEEDED(d->SetDepthStencilSurface(depth))&&ok;
                for(unsigned i=0;i<16;++i)ok=SUCCEEDED(d->SetTexture(i,nullptr))&&ok;
                ok=SUCCEEDED(d->SetStreamSourceFreq(0,1))&&ok;
                ok=SUCCEEDED(d->SetRenderState(D3DRS_CLIPPLANEENABLE,0))&&ok;
                const DWORD values[]={TRUE,TRUE,D3DCMP_LESSEQUAL,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,15,0,0};
                for(unsigned i=0;i<State::Count;++i)ok=SUCCEEDED(d->SetRenderState(State::keys[i],values[i]))&&ok;
                ++flushes;
                const Pending* previous=nullptr;IDirect3DVertexBuffer9* boundVertex=nullptr;IDirect3DIndexBuffer9* boundIndex=nullptr;UINT boundStride=0;bool bufferKnown=false;
                for(size_t at=0;at<pending.size();){
                    if(!ok)break;
                    if(pending[at]->redundant){++draws;++residentDraws;++mergedDraws;++duplicateDraws;compactDraws+=pending[at]->vertex->compact;++at;continue;}
                    if(!pending[at]->resident)streamPending(at);const auto& p=pending[at];const auto run=drawRun(at);
                    if(!previous||std::memcmp(&previous->viewport,&p->viewport,sizeof p->viewport))ok=SUCCEEDED(d->SetViewport(&p->viewport))&&ok;
                    if(!previous||previous->declaration!=p->declaration)ok=SUCCEEDED(d->SetVertexDeclaration(p->declaration))&&ok;
                    for(unsigned i=0;i<sizeof(rasterKeys)/sizeof(*rasterKeys);++i)if(!previous||previous->raster[i]!=p->raster[i])ok=SUCCEEDED(d->SetRenderState(rasterKeys[i],p->raster[i]))&&ok;
                    for(unsigned i=0;i<16;++i)if(p->pixel->reads.samplers&(1u<<i)){
                        const bool known=previous&&(previous->pixel->reads.samplers&(1u<<i));
                        if(!known||previous->textures[i]!=p->textures[i])ok=SUCCEEDED(d->SetTexture(i,p->textures[i]))&&ok;
                        for(unsigned s=0;s<13;++s)if(!known||previous->samplers[i][s]!=p->samplers[i][s])ok=SUCCEEDED(d->SetSamplerState(i,D3DSAMPLERSTATETYPE(s+1),p->samplers[i][s]))&&ok;
                    }
                    // Both packets must have captured the whole compared range;
                    // a shader switch may expose previously uninitialized lanes.
                    auto dirty=NorthlightReplayConstants::DirtySpan{p->vertex->first,p->vertex->count};
                    if(previous&&previous->vertex->first<=dirty.first&&previous->vertex->first+previous->vertex->count>=dirty.first+dirty.count){
                        const auto relative=previous->constants==p->constants?NorthlightReplayConstants::DirtySpan{}:NorthlightReplayConstants::Detail::knownFloatSpan(previous->constants+dirty.first*4,p->constants+dirty.first*4,dirty.count);
                        dirty.first+=relative.first;dirty.count=relative.count;
                    }
                    if(dirty.count)ok=SUCCEEDED(d->SetVertexShaderConstantF(dirty.first,p->constants+dirty.first*4,dirty.count))&&ok;
                    if(!previous||previous->pixel->first!=p->pixel->first||previous->pixel->count!=p->pixel->count||
                       std::memcmp(previous->ps.data(),p->ps.data(),p->pixel->count*16))
                        ok=SUCCEEDED(d->SetPixelShaderConstantF(p->pixel->first,p->ps.data(),p->pixel->count))&&ok;
                    if(p->vertex->reads.i)ok=SUCCEEDED(d->SetVertexShaderConstantI(0,p->ints,16))&&ok;
                    if(p->pixel->reads.i)ok=SUCCEEDED(d->SetPixelShaderConstantI(0,p->pi,16))&&ok;
                    if(p->vertex->reads.b)ok=SUCCEEDED(d->SetVertexShaderConstantB(0,p->bools,16))&&ok;
                    if(p->pixel->reads.b)ok=SUCCEEDED(d->SetPixelShaderConstantB(0,p->pb,16))&&ok;
                    if(ok&&!cleared){ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));cleared=ok;}
                    if(!previous||previous->vertex->shader!=p->vertex->shader)ok=SUCCEEDED(d->SetVertexShader(p->vertex->shader))&&ok;
                    if(!previous||previous->pixel!=p->pixel)ok=SUCCEEDED(d->SetPixelShader(p->pixel->shader))&&ok;
                    const auto& mesh=p->geometry();
                    if(p->resident){
                        const auto stride=mesh.streams[0].stride;
                        if(!bufferKnown||boundVertex!=p->gpuVertex[0]||boundStride!=stride)ok=SUCCEEDED(d->SetStreamSource(0,p->gpuVertex[0],0,stride))&&ok;
                        if(!bufferKnown||boundIndex!=p->gpuIndex)ok=SUCCEEDED(d->SetIndices(p->gpuIndex))&&ok;
                        bufferKnown=ok;boundVertex=p->gpuVertex[0];boundIndex=p->gpuIndex;boundStride=stride;
                        if(ok)ok=SUCCEEDED(mesh.indexed?d->DrawIndexedPrimitive(mesh.topology,p->placement.baseVertex(),run.min,run.vertices,p->placement.indexStart,run.count):
                            d->DrawPrimitive(mesh.topology,p->placement.vertexBase,mesh.primitiveCount));
                        if(ok){if(p->streamed)streamedDraws+=run.packets;else residentDraws+=run.packets;}
                    }else{
                        if(ok)ok=SUCCEEDED(mesh.indexed?
                            d->DrawIndexedPrimitiveUP(mesh.topology,0,mesh.vertexCount,mesh.primitiveCount,mesh.indices.data(),D3DFMT_INDEX32,mesh.streams[0].bytes.data(),mesh.streams[0].stride):
                            d->DrawPrimitiveUP(mesh.topology,mesh.primitiveCount,mesh.streams[0].bytes.data(),mesh.streams[0].stride));
                        bufferKnown=false;if(ok)upBytes+=mesh.byteSize();
                    }
                    if(ok){draws+=run.packets;compactDraws+=unsigned(p->vertex->compact)*run.packets;mergedDraws+=run.packets-1;previous=pending[at-1].get();}
                }
                ok=saved.restore()&&ok;
                ok=(SUCCEEDED(batchState->Apply())||SUCCEEDED(batchState->Apply()))&&ok;
                ok=(SUCCEEDED(d->SetViewport(&saved.viewport))||SUCCEEDED(d->SetViewport(&saved.viewport)))&&ok;
            }
        }
        clearPending();bad|=!ok;return ok;
    }
    template<class Draw> void capture(IDirect3DVertexShader9* source,Draw draw,bool userMemory=false,
            const NorthlightDrawSnapshot::Draw* geometry=nullptr,const void* indices=nullptr,D3DFORMAT format=D3DFMT_UNKNOWN,const void* vertices=nullptr,UINT stride=0,
            const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>* captured=nullptr,IDirect3DVertexDeclaration9* capturedDeclaration=nullptr,const NorthlightPoseBounds::Handoff* pose=nullptr){
        if(bad||!source)return;
        // Reuse the current pose's receiver range/clip proof before admission.
        // Solid triangles only; caster capture remains independent of the camera.
        if(pose&&(pose->outsideRange||pose->outsideViewport)&&poseReader&&geometry&&captured&&*captured&&
           (*captured)->topology==geometry->topology&&(*captured)->primitiveCount==geometry->primitives&&(*captured)->indexed==geometry->indexed
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
           &&rangeEnabled
#endif
        ){
            NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> declaration;
            NorthlightConstantEpoch::Stamp current;current.requestHeld=true;++poseBoundsTests;
            const D3DRENDERSTATETYPE keys[]={D3DRS_CLIPPING,D3DRS_FILLMODE};DWORD clipping[2]={};
            const bool triangles=geometry->topology==D3DPT_TRIANGLELIST||geometry->topology==D3DPT_TRIANGLESTRIP||geometry->topology==D3DPT_TRIANGLEFAN;
            const bool clip=!pose->outsideRange&&pose->outsideViewport&&triangles&&renderStates(keys,clipping)&&clipping[0]&&clipping[1]==D3DFILL_SOLID;
            poseViewportTests+=clip;
            if(d->GetNPatchMode()==0&&SUCCEEDED(d->GetVertexDeclaration(declaration.out()))&&declaration.p&&
               capturedDeclaration==declaration.p&&poseReader(poseContext,current)&&pose->outside(captured->get(),source,declaration.p,current,clip)){
                poseViewportCulled+=clip;++poseBoundsCulled;return;}
        }
        const D3DRENDERSTATETYPE admissionKeys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_COLORWRITEENABLE,
            D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,D3DRS_ZFUNC};
        DWORD states[8]={};if(!renderStates(admissionKeys,states)||!states[0]||!states[1]||states[2]||!(states[3]&7))return;
        const DWORD alpha=states[4],ref=states[5],func=states[6],comparison=states[7];
        if((alpha&&func!=D3DCMP_GREATER&&func!=D3DCMP_GREATEREQUAL)||(comparison!=D3DCMP_LESS&&comparison!=D3DCMP_LESSEQUAL&&comparison!=D3DCMP_EQUAL))return;
        IDirect3DPixelShader9* native=nullptr;if(FAILED(d->GetPixelShader(&native))||!native)return;
        auto* ps=pixel(native,alpha!=0);release(native);if(!ps||!ps->shader)return;
        auto* vs=vertex(source,ps->compactVertex);if(!vs||!vs->shader)return;
        State saved(d,ps->control,userMemory);bool admissionTried=false,admissionOK=false;
        auto admit=[&]{
            if(admissionTried)return admissionOK;admissionTried=true;
            if(!saved.save(!deferred)||!saved.rt[0]||(frameTarget&&frameTarget!=saved.rt[0])||saved.viewport.X||saved.viewport.Y)return false;
            if(frameTarget){
                // The retained surface has immutable dimensions. Reset/endFrame
                // release this identity before another target can be admitted.
                if(width!=saved.viewport.Width||height!=saved.viewport.Height)return false;
            }else{
                D3DSURFACE_DESC desc{};if(FAILED(saved.rt[0]->GetDesc(&desc))||desc.Width!=saved.viewport.Width||desc.Height!=saved.viewport.Height)return false;
                if(!targets(desc.Width,desc.Height)){bad=true;return false;}
                frameTarget=saved.rt[0];frameTarget->AddRef();
            }
            return admissionOK=true;
        };
        // Establish a frame target before preparing packets: a resize resets
        // source caches/counters. Later far draws can avoid target reads entirely.
        if(!frameTarget&&!admit())return;
        const float control[]={0,48.f,alpha?(float(ref)+(func==D3DCMP_GREATER?.5f:0.f))/255.f:.001f,alpha?1.f:0.f};
        if(record(vs,ps,control,saved.viewport,admit,geometry,indices,format,vertices,stride,captured,capturedDeclaration,pose))return;
        if(!admit())return;
        if(deferred&&!pending.empty()){
            if(!finishCaptures())return;
            if(record(vs,ps,control,saved.viewport,admit,geometry,indices,format,vertices,stride,captured,capturedDeclaration,pose))return;
        }
        if(deferred&&!saved.saveRest())return;
        // Keep depth-tie ordering when a draw cannot use immutable replay.
        if(!finishCaptures())return;
        ++immediate;
        saved.changed=true;
        bool ok=SUCCEEDED(d->SetDepthStencilSurface(nullptr));
        for(unsigned i=1;i<4;++i)if(saved.rt[i]){saved.targetChanged|=1u<<i;ok=SUCCEEDED(d->SetRenderTarget(i,nullptr))&&ok;}
        ok=SUCCEEDED(d->SetRenderTarget(0,surface))&&ok;ok=SUCCEEDED(d->SetDepthStencilSurface(depth))&&ok;
        ok=SUCCEEDED(d->SetViewport(&saved.viewport))&&ok;
        // RT1..3 are unbound; their color-write masks cannot affect this pass.
        const DWORD values[]={TRUE,TRUE,D3DCMP_LESSEQUAL,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,15,0,0};
        // Most native opaque draws already have these values. Track attempted
        // changes before the driver call, so partial failures still restore.
        for(unsigned i=0;i<State::Count;++i)if(values[i]!=saved.values[i]){saved.renderChanged|=1u<<i;ok=SUCCEEDED(d->SetRenderState(State::keys[i],values[i]))&&ok;}
        if(ok&&!cleared){ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));cleared=ok;}
        ok=SUCCEEDED(d->SetVertexShader(vs->shader))&&ok;ok=SUCCEEDED(d->SetPixelShader(ps->shader))&&ok;
        ok=SUCCEEDED(d->SetPixelShaderConstantF(ps->control,control,1))&&ok;
        if(ok)ok=SUCCEEDED(draw());
        ok=saved.restore()&&ok;if(ok){++draws;compactDraws+=unsigned(vs->compact);}else bad=true;
    }
};
