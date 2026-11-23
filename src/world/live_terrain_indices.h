#pragma once
#include "dynamic_ring.h"
#include "lock_meter.h"

// Directional, flash and point shadows borrow the same uploaded index slice.
// The frame coordinator issues the shared EVENT after all of those consumers.
class NorthlightLiveTerrainIndices {
    IDirect3DIndexBuffer9* buffer_=nullptr;
    NorthlightDynamicRing::Ring ring_;
    UINT base_=0;

    void dropBuffer(){if(buffer_){buffer_->Release();buffer_=nullptr;}}

    template<class Check>
    bool recreate(IDirect3DDevice9* d,UINT capacity,NorthlightDynamicRing::FrameFence& queries,const Check& check){
        dropBuffer();
        NorthlightDynamicRing::reset(ring_,0,queries);
        clearBase();
        if(!check(d->CreateIndexBuffer(capacity,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&buffer_,nullptr),
                  "live index buffer")){
            dropBuffer();
            return false;
        }
        NorthlightDynamicRing::reset(ring_,capacity,queries);
        return true;
    }

public:
    enum class Upload {Ready,Deferred,Failed};

    NorthlightLiveTerrainIndices()=default;
    NorthlightLiveTerrainIndices(const NorthlightLiveTerrainIndices&)=delete;
    NorthlightLiveTerrainIndices& operator=(const NorthlightLiveTerrainIndices&)=delete;
    ~NorthlightLiveTerrainIndices(){
        NorthlightDynamicRing::FrameFence remainingReferences;
        release(remainingReferences);
    }

    IDirect3DIndexBuffer9* buffer()const{return buffer_;}
    UINT base()const{return base_;}
    void clearBase(){base_=0;}
    void touch(NorthlightDynamicRing::FrameFence& queries){NorthlightDynamicRing::touch(ring_,queries);}
    void release(NorthlightDynamicRing::FrameFence& queries){
        NorthlightDynamicRing::reset(ring_,0,queries);
        queries.drop();
        dropBuffer();
        clearBase();
    }

    template<class Admit,class Write,class Check>
    Upload upload(IDirect3DDevice9* d,UINT bytes,NorthlightDynamicRing::FrameFence& queries,
                  const Admit& admit,const Write& write,const Check& check){
        using namespace NorthlightDynamicRing;
        if(bytes>ring_.capacity){
            const UINT capacity=ringCapacity(bytes);
            if(!admit(capacity))return Upload::Deferred;
            if(!recreate(d,capacity,queries,check))return Upload::Failed;
        }
        auto slot=place(ring_,bytes,4,queries);
        if(slot.discarded&&oversized(ring_,ring_.peak)){
            if(!recreate(d,ringCapacity(ring_.peak),queries,check))return Upload::Failed;
            slot=place(ring_,bytes,4,queries);
            NorthlightLockMeter::ringShrink();
        }
        if(slot.wrapped)NorthlightLockMeter::ringWrap(slot.fenceReuse,slot.discarded);
        if(slot.discarded)NorthlightLockMeter::discard(NorthlightLockMeter::LiveIB,ring_.capacity);
        void* data=nullptr;
        if(!check(buffer_->Lock(slot.offset,bytes,&data,slot.flags),"live index upload"))return Upload::Failed;
        write(static_cast<std::uint32_t*>(data));
        if(!check(buffer_->Unlock(),"live index unlock"))return Upload::Failed;
        base_=slot.offset/sizeof(std::uint32_t);
        return Upload::Ready;
    }
};
