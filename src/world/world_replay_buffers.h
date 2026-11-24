#pragma once
#include <cstring>
#include "dynamic_ring.h"
#include "lock_meter.h"
#include "replay_bulk_layout.h"
#include "streaming_phase_profile.h"

// Bulk shadow replay uses one ring per vertex stream and one index ring.
// The frame coordinator supplies the shared EVENT fence and the ordered layout.
class NorthlightWorldReplayBuffers {
    IDirect3DVertexBuffer9* vertices_[4]={};
    IDirect3DIndexBuffer9* indices_=nullptr;
    NorthlightDynamicRing::Ring vertexRings_[4],indexRing_;
    friend struct NorthlightWorldReplayBuffersTest;

    template<class T> static void drop(T*& buffer){if(buffer){buffer->Release();buffer=nullptr;}}

    template<class T> static void bind(T*& current,T* buffer){
        if(current==buffer)return;
        drop(current);
        if(buffer){current=buffer;buffer->AddRef();}
    }

    template<class Recreate>
    static bool reserve(NorthlightDynamicRing::Ring& ring,UINT bytes,UINT align,NorthlightLockMeter::Site site,
                        NorthlightDynamicRing::FrameFence& queries,NorthlightStreaming::PhaseProfile& phases,
                        std::uint64_t& growths,const Recreate& recreate,NorthlightDynamicRing::Slot& slot){
        using namespace NorthlightDynamicRing;
        if(ring.capacity<bytes){
            auto growth=phases.measure(NorthlightStreaming::PhaseProfile::ReplayGrow);
            ++growths;
            const UINT capacity=ringCapacity(bytes);
            reset(ring,0,queries);
            if(!recreate(capacity))return false;
            reset(ring,capacity,queries);
        }
        slot=place(ring,bytes,align,queries);
        if(slot.discarded&&oversized(ring,ring.peak)){
            const UINT capacity=ringCapacity(ring.peak);
            reset(ring,0,queries);
            if(!recreate(capacity))return false;
            reset(ring,capacity,queries);
            slot=place(ring,bytes,align,queries);
            NorthlightLockMeter::ringShrink();
        }
        if(slot.wrapped)NorthlightLockMeter::ringWrap(slot.fenceReuse,slot.discarded);
        if(slot.discarded)NorthlightLockMeter::discard(site,ring.capacity);
        return true;
    }

public:
    NorthlightWorldReplayBuffers()=default;
    NorthlightWorldReplayBuffers(const NorthlightWorldReplayBuffers&)=delete;
    NorthlightWorldReplayBuffers& operator=(const NorthlightWorldReplayBuffers&)=delete;
    ~NorthlightWorldReplayBuffers(){
        NorthlightDynamicRing::FrameFence remainingReferences;
        release(remainingReferences);
    }

    UINT vertexCapacity(unsigned stream)const{return vertexRings_[stream].capacity;}
    UINT indexCapacity()const{return indexRing_.capacity;}
    IDirect3DVertexBuffer9* vertexBuffer(unsigned stream)const{return vertices_[stream];}
    IDirect3DIndexBuffer9* indexBuffer()const{return indices_;}

    struct Growth {std::uint64_t capacity=0;bool needed=false;};
    Growth growthFor(const UINT (&totals)[4],UINT indexBytes)const{
        Growth result;
        for(unsigned s=0;s<4;++s){
            const bool grow=totals[s]>vertexCapacity(s);
            result.needed|=grow;
            result.capacity+=grow?NorthlightDynamicRing::ringCapacity(totals[s]):vertexCapacity(s);
        }
        const bool grow=indexBytes>indexCapacity();
        result.needed|=grow;
        result.capacity+=grow?NorthlightDynamicRing::ringCapacity(indexBytes):indexCapacity();
        return result;
    }

    // Reset also drops the coordinator's unissued EVENT, after forgetting
    // the replay spans and before releasing the buffers.
    void release(NorthlightDynamicRing::FrameFence& queries){
        for(auto& ring:vertexRings_)NorthlightDynamicRing::reset(ring,0,queries);
        NorthlightDynamicRing::reset(indexRing_,0,queries);
        queries.drop();
        for(auto& buffer:vertices_)drop(buffer);
        drop(indices_);
    }

    template<class Packets,class Check>
    bool upload(IDirect3DDevice9* d,NorthlightDynamicRing::FrameFence& queries,
                NorthlightStreaming::PhaseProfile& phases,std::uint64_t& growths,
                const UINT (&totals)[4],UINT indexBytes,const NorthlightReplayBulk::Layout& layout,
                const NorthlightReplayBulk::DrawList& draws,const Packets& packets,const Check& check){
        UINT vertexBase[4]={},indexBase=0;
        for(unsigned s=0;s<4;++s){
            if(!totals[s])continue;
            NorthlightDynamicRing::Slot slot;
            const auto recreate=[&](UINT capacity){
                drop(vertices_[s]);
                if(check(d->CreateVertexBuffer(capacity,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vertices_[s],nullptr),
                         "model snapshot vertex buffer"))return true;
                drop(vertices_[s]);return false;
            };
            if(!reserve(vertexRings_[s],totals[s],16,NorthlightLockMeter::ReplayVB,queries,phases,growths,recreate,slot))return false;
            auto copy=phases.measure(NorthlightStreaming::PhaseProfile::ReplayCopy);
            void* memory=nullptr;
            if(!check(vertices_[s]->Lock(slot.offset,totals[s],&memory,slot.flags),"model snapshot vertex lock"))return false;
            draws.visit(packets,[&](size_t i){
                if(!layout.unique(i))return;
                const auto& p=packets[i];const auto& source=p->mesh().streams[s];
                if(!source.bytes.empty())std::memcpy(static_cast<std::uint8_t*>(memory)+p->offset[s],source.bytes.data(),source.bytes.size());
            });
            if(!check(vertices_[s]->Unlock(),"model snapshot vertex unlock"))return false;
            vertexBase[s]=slot.offset;
        }
        if(indexBytes){
            NorthlightDynamicRing::Slot slot;
            const auto recreate=[&](UINT capacity){
                drop(indices_);
                if(check(d->CreateIndexBuffer(capacity,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&indices_,nullptr),
                         "model snapshot index buffer"))return true;
                drop(indices_);return false;
            };
            if(!reserve(indexRing_,indexBytes,4,NorthlightLockMeter::ReplayIB,queries,phases,growths,recreate,slot))return false;
            auto copy=phases.measure(NorthlightStreaming::PhaseProfile::ReplayCopy);
            void* memory=nullptr;
            if(!check(indices_->Lock(slot.offset,indexBytes,&memory,slot.flags),"model snapshot index lock"))return false;
            draws.visit(packets,[&](size_t i){
                if(!layout.unique(i))return;
                const auto& p=packets[i];const auto& indices=p->mesh().indices;
                if(p->indexed&&!indices.empty())std::memcpy(static_cast<std::uint32_t*>(memory)+p->start,indices.data(),indices.size()*4);
            });
            if(!check(indices_->Unlock(),"model snapshot index unlock"))return false;
            indexBase=slot.offset/4;
        }
        // Publish absolute offsets only after every stream and index copy succeeds.
        draws.visit(packets,[&](size_t i){
            const auto& p=packets[i];
            if(p->indexed)p->start+=indexBase;
            for(unsigned s=0;s<4;++s){
                const bool present=!p->mesh().streams[s].bytes.empty();
                if(present)p->offset[s]=p->bulkSingle?vertexBase[s]:p->offset[s]+vertexBase[s];
                bind(p->stream[s],present?vertices_[s]:nullptr);
            }
            bind(p->index,p->indexed?indices_:nullptr);
        });
        return true;
    }
};
