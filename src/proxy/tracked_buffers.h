#pragma once
#include <atomic>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <type_traits>
#include "capture_buffer_metadata.h"
#include "mirror_guard.h"
#include "lock_meter.h"
#include "replay_copies.h"
#include "particle_uploads.h"
#include "buffer_range_revisions.h"

// Only client-created buffers are wrapped. The real device always receives
// real resources. Unknown/unwrapped resources return revision 0 (no fast cache).
// This does not intercept or patch the backend's vtables.
namespace NorthlightTrackedBuffers {
// The private token belongs to the underlying COM resource, not its address.
// Serialize first sight so concurrent legacy readers cannot replace each
// other's freshly assigned identity. Tracked batch reads never enter here.
inline std::atomic<std::uint64_t> identityClock{1};
inline std::mutex identityMutex;
inline std::uint64_t identity(void* buffer,bool index){
    if(!buffer)return 0;
    static const GUID token={0x6f1d2a4c,0x3b7e,0x4c21,{0x9a,0x55,0x0f,0x7e,0x21,0x66,0x9d,0x4b}};
    auto* resource=index?static_cast<IDirect3DResource9*>(static_cast<IDirect3DIndexBuffer9*>(buffer)):
                         static_cast<IDirect3DResource9*>(static_cast<IDirect3DVertexBuffer9*>(buffer));
    std::uint64_t value=0;DWORD size=sizeof value;
    if(SUCCEEDED(resource->GetPrivateData(token,&value,&size))&&size==sizeof value&&value)return value;
    std::lock_guard<std::mutex> guard(identityMutex);
    value=0;size=sizeof value;
    if(SUCCEEDED(resource->GetPrivateData(token,&value,&size))&&size==sizeof value&&value)return value;
    value=identityClock.fetch_add(1,std::memory_order_relaxed);
    if(!value||FAILED(resource->SetPrivateData(token,&value,sizeof value,0)))return 0;
    return value;
}
inline std::atomic<uint64_t> clock{1};
inline std::atomic<uint64_t> noChangeWrites{0},noChangeBytes{0};
inline std::atomic<uint64_t> skippedWrites{0},skippedBytes{0};
inline std::mutex mutex;
struct RangeStats {uint64_t queries=0,preserved=0,fallbacks=0;};
inline RangeStats bufferRangeStats; // guarded by the registry mutex
inline RangeStats rangeStats(){
    std::lock_guard<std::mutex> guard(mutex);
    return bufferRangeStats;
}
struct Record {
    void* raw=nullptr;void* exposed=nullptr;
    IUnknown* object=nullptr;
    std::atomic<uint64_t> revision{0};
    std::atomic<unsigned> locks{0};
    bool index=false,rangeObserved=false,rangeAllocationFailed=false;
    D3DPOOL pool=D3DPOOL_DEFAULT;bool poolKnown=false;
    NorthlightCaptureMetadata::Info metadata;
    std::unique_ptr<NorthlightBufferRanges::Revisions> ranges;
    std::atomic<bool> spellGuard{false}; // task 34: the spell layer guards bytes of this buffer; set/cleared through markSpellGuard, tested (relaxed) by Buffer::Lock
    std::atomic<bool> materialGuarded{false};
    NorthlightReplayCopies::Slot copy; // 0.3.192 (CS): the replay-side CPU copy (replay_copies.h); attached only while the stream is active
};
inline std::unordered_map<void*,Record*> records;
// Task 34: the spell layer's write hook. Installed by the Device (SpellLayer=1) before it marks any buffer and removed in its destructor; called by
// Buffer::Lock BEFORE the lock is forwarded, only for non-READONLY locks of a marked buffer, with the RAW buffer (the key the layer guards).
struct SpellHook {void (*write)(void* context,void* rawBuffer,UINT offset,UINT size,DWORD flags,DWORD usage,unsigned outstandingLocks)=nullptr;void* context=nullptr;};
inline SpellHook spellHook;
struct MaterialWriteHook {void (*beforeWrite)(void*)=nullptr;void* context=nullptr;};
struct MaterialReadGuard {MaterialWriteHook hook;unsigned readers=0;};
inline std::unordered_map<void*,MaterialReadGuard> materialGuards;
inline MaterialWriteHook materialWriter(void* raw){
    std::lock_guard<std::mutex> guard(mutex);
    const auto it=materialGuards.find(raw);
    return it==materialGuards.end()?MaterialWriteHook{}:it->second.hook;
}
inline NorthlightCaptureMetadata::ReadLease guardMaterialRead(void* raw,bool index,uint64_t endByte,MaterialWriteHook hook){
    const auto release=[](void* retained){
        {
            std::lock_guard<std::mutex> guard(mutex);
            const auto pending=materialGuards.find(retained);
            if(!--pending->second.readers){
                materialGuards.erase(pending);
                const auto wrapped=records.find(retained);
                if(wrapped!=records.end())wrapped->second->materialGuarded.store(false,std::memory_order_relaxed);
            }
        }
        static_cast<IDirect3DResource9*>(retained)->Release();
    };
    std::lock_guard<std::mutex> guard(mutex);
    const auto it=records.find(raw);
    if(it==records.end()||!hook.beforeWrite)return {nullptr,release};
    auto& buffer=*it->second;
    if(buffer.raw!=raw||buffer.index!=index||!buffer.metadata.known||!buffer.poolKnown||
       (buffer.pool!=D3DPOOL_MANAGED&&buffer.pool!=D3DPOOL_DEFAULT)||
       (buffer.metadata.usage&D3DUSAGE_DYNAMIC)||buffer.locks.load(std::memory_order_relaxed)||
       !buffer.revision.load(std::memory_order_relaxed))return {nullptr,release};
    if(index){
        if(endByte>buffer.metadata.size)return {nullptr,release};
        if(buffer.metadata.format==D3DFMT_INDEX16)endByte*=2;
        else if(buffer.metadata.format==D3DFMT_INDEX32)endByte*=4;
        else return {nullptr,release};
    }
    if(!endByte||endByte>buffer.metadata.size)return {nullptr,release};
    // Keep the native resource alive without retaining its proxy Device.
    // GetStreamSource/GetIndices may wrap the same resource again.
    auto [pending,inserted]=materialGuards.try_emplace(raw,MaterialReadGuard{hook});
    if(!inserted&&(pending->second.hook.context!=hook.context||
                   pending->second.hook.beforeWrite!=hook.beforeWrite))return {nullptr,release};
    ++pending->second.readers;
    buffer.materialGuarded.store(true,std::memory_order_relaxed);
    auto* resource=index?static_cast<IDirect3DResource9*>(static_cast<IDirect3DIndexBuffer9*>(raw)):
                         static_cast<IDirect3DResource9*>(static_cast<IDirect3DVertexBuffer9*>(raw));
    resource->AddRef();
    return {resource,release};
}
// Lock-free input resolution for our own wrappers. The first Buffer class
// built for an interface publishes one immutable {final vtable, Record offset}
// pair. A live input (the caller owns a COM reference, so its registration
// cannot be erased) whose first word equals that vtable is such a Buffer; its
// raw/exposed fields are immutable. Anything else takes the registry lookup.
static constexpr bool kBufferShapeResolve=true;
struct ShapeInfo {const void* vtable;std::ptrdiff_t record;};
template<class T> struct Shape {
    static inline std::atomic<const ShapeInfo*> info{nullptr};
    static const Record* match(const T* input){
        if(!kBufferShapeResolve)return nullptr;
        const ShapeInfo* shape=info.load(std::memory_order_acquire);
        if(!shape||*reinterpret_cast<const void* const*>(input)!=shape->vtable)return nullptr;
        auto* found=reinterpret_cast<const Record*>(reinterpret_cast<const char*>(input)+shape->record);
        return found->exposed==input?found:nullptr;
    }
};
inline bool isWrapped(void* p){
    if(!p)return false;
    std::lock_guard<std::mutex> guard(mutex);auto it=records.find(p);
    return it!=records.end()&&it->second->exposed==p;
}
inline void captureMetadata(const NorthlightCaptureMetadata::Request* requests,NorthlightCaptureMetadata::Info* output,unsigned count){
    if(!output)return;
    for(unsigned i=0;i<count;++i)output[i]={};
    if(!requests||!count)return;
    std::lock_guard<std::mutex> guard(mutex);
    for(unsigned i=0;i<count;++i){
        if(!requests[i].buffer)continue;
        auto found=records.find(requests[i].buffer);
        if(found==records.end())continue;
        auto& record=*found->second;
        if(record.index!=requests[i].index||!record.metadata.known)continue;
        output[i]=record.metadata;
        output[i].revision=record.locks.load(std::memory_order_acquire)?0:record.revision.load(std::memory_order_relaxed);   // 0.3.196 (task 12): acquire pairs with the unlock's release
        const auto& request=requests[i];
        if(output[i].revision&&request.bytes){
            ++bufferRangeStats.queries;
            uint64_t begin=request.begin,bytes=request.bytes;
            if(request.indexElements){
                const uint64_t width=record.metadata.format==D3DFMT_INDEX16?2:record.metadata.format==D3DFMT_INDEX32?4:0;
                if(!request.index||!width||begin>UINT64_MAX/width||bytes>UINT64_MAX/width){output[i].revision=0;continue;}
                begin*=width;bytes*=width;
            }
            if(begin>=record.metadata.size||bytes>record.metadata.size-begin){output[i].revision=0;continue;}
            record.rangeObserved=true;
            if(record.ranges){const auto whole=output[i].revision;output[i].revision=record.ranges->revision(whole,begin,bytes);
                if(output[i].revision!=whole)++bufferRangeStats.preserved;}
            else if(record.rangeAllocationFailed)++bufferRangeStats.fallbacks;
        }
    }
}
inline uint64_t version(void* raw,bool index){
    std::lock_guard<std::mutex> guard(mutex);
    auto it=records.find(raw);
    if(it==records.end()||it->second->index!=index||it->second->locks.load(std::memory_order_acquire))return 0;   // 0.3.196 (task 12): acquire pairs with the unlock's release
    return it->second->revision.load(std::memory_order_relaxed);
}
template<class T> T* unwrap(T* p){
    std::lock_guard<std::mutex> guard(mutex);auto it=records.find(p);
    return it==records.end()?p:static_cast<T*>(it->second->raw);
}
// The caller holds its normal COM input reference. Resolve classification and
// forwarding together, under the SAME registry lock and lifetime rules as the
// original isWrapped + unwrap pair. Never retain an unowned registry pointer.
template<class T> T* resolveInput(T* p,bool& wrapped){
    wrapped=false;if(!p)return nullptr;
    if(const Record* found=Shape<T>::match(p)){wrapped=true;return static_cast<T*>(found->raw);}
    std::lock_guard<std::mutex> guard(mutex);auto it=records.find(p);
    if(it==records.end())return p;
    wrapped=it->second->exposed==p;return static_cast<T*>(it->second->raw);
}
template<class T> void expose(T** out){
    if(!out||!*out)return;
    T* raw=*out;IUnknown* object=nullptr;
    {std::lock_guard<std::mutex> guard(mutex);auto it=records.find(raw);
     if(it!=records.end()){object=it->second->object;object->AddRef();*out=static_cast<T*>(it->second->exposed);}}
    if(object)raw->Release(); // exchange the real Get* reference for the wrapper reference
}
inline void written(void* p){
    std::lock_guard<std::mutex> guard(mutex);auto it=records.find(p);
    if(it!=records.end()){
        auto& r=*it->second;
        r.revision=clock.fetch_add(1);NorthlightReplayCopies::invalidate(r.copy,true);
        NorthlightParticleUploads::cache().forget(r.raw);NorthlightParticleUploads::atlasUploads().forget(r.raw);
    }
}
// The layer's markGuard callback: raw or exposed pointer of a wrapped buffer; anything else is ignored (the layer holds a reference, the record cannot be recycled).
inline void markSpellGuard(void* buffer,bool on){
    std::lock_guard<std::mutex> guard(mutex);auto it=records.find(buffer);
    if(it!=records.end())it->second->spellGuard.store(on,std::memory_order_relaxed);
}
inline void invalidateAll(bool preserveManaged=false){
    NorthlightParticleUploads::cache().clear();
    std::lock_guard<std::mutex> guard(mutex);
    for(auto& entry:records){auto& r=*entry.second;
        // Managed bytes survive Reset. Lock/Unlock still advances revisions;
        // unknown/default pools and in-flight writes keep full invalidation.
        if(!preserveManaged||!r.poolKnown||r.pool!=D3DPOOL_MANAGED||r.locks.load(std::memory_order_relaxed))r.revision=clock.fetch_add(1);
        NorthlightReplayCopies::invalidate(r.copy);}
}
template<class T,class Forward> class Buffer final:public Forward {
    LONG refs=1;IDirect3DDevice9* owner;Record record;
    void(*unsafeAccess)(IDirect3DDevice9*,const char*)=nullptr;
    // 0.3.180 (D0): the owner device's gate, for the thread census only (buffers take no gate). The
    // buffer holds a device reference, so the gate outlives every call that reports to it.
    MirrorGate* census=nullptr;
    // 0.3.192 (DXVK3): the size DXVK 3.x charges for a DISCARD of this buffer (DEFAULT|DYNAMIC only: direct-mapped), 0 otherwise.
    UINT discardBytes=0;
    NorthlightReplayCopies::PendingWrite pendingWrite; // 0.3.196 (task 12): this wrapper's outstanding write lock (Lock records, Unlock consumes; touched by the locking thread only)
    uint64_t previousWriteRevision=0,pendingWriteRevision=0;
    uint64_t rangePrevious=0,rangeBegin=0,rangeBytes=0;
    bool rangeWrite=false,rangeWhole=false;
    NorthlightParticleUploads::Pending particleWrite;
public:
    bool hasCopySlotForTest()const{return record.copy.attached;} // the CPU copy slot exists only for buffers created while the stream is active
    Buffer(T* real,IDirect3DDevice9* device,bool index,void(*unsafe)(IDirect3DDevice9*,const char*)=nullptr,MirrorGate* gate=nullptr):Forward(real),owner(device),unsafeAccess(unsafe),census(gate){
        record.raw=real;record.exposed=static_cast<T*>(this);record.object=this;record.index=index;
        record.revision=clock.fetch_add(1);
        // Descriptor and identity are immutable for this resource lifetime.
        // Failure disables metadata acceleration, never ordinary wrapping.
        if constexpr(std::is_same<T,IDirect3DIndexBuffer9>::value){
            D3DINDEXBUFFER_DESC desc={};
            if(index&&SUCCEEDED(real->GetDesc(&desc))&&desc.Size){
                record.pool=desc.Pool;record.poolKnown=true;record.metadata.size=desc.Size;record.metadata.usage=desc.Usage;record.metadata.format=desc.Format;
                record.metadata.identity=identity(real,true);record.metadata.known=record.metadata.identity!=0;
                discardBytes=desc.Pool==D3DPOOL_DEFAULT&&(desc.Usage&D3DUSAGE_DYNAMIC)?desc.Size:0;
            }
        }else{
            D3DVERTEXBUFFER_DESC desc={};
            if(!index&&SUCCEEDED(real->GetDesc(&desc))&&desc.Size){
                record.pool=desc.Pool;record.poolKnown=true;record.metadata.size=desc.Size;record.metadata.usage=desc.Usage;record.metadata.format=desc.Format;
                record.metadata.identity=identity(real,false);record.metadata.known=record.metadata.identity!=0;
                discardBytes=desc.Pool==D3DPOOL_DEFAULT&&(desc.Usage&D3DUSAGE_DYNAMIC)?desc.Size:0;
            }
        }
        if(NorthlightReplayCopies::enabled.load(std::memory_order_relaxed))NorthlightReplayCopies::attach(record.copy,record.raw,record.exposed,this,record.metadata.size,&record.locks); // size 0 (descriptor unknown): never copied
        {std::lock_guard<std::mutex> guard(mutex);
         bool first=false;
         record.materialGuarded.store(materialGuards.count(record.raw)!=0,std::memory_order_relaxed);
         try{first=records.emplace(record.raw,&record).second;records.emplace(record.exposed,&record);}catch(...){if(first)records.erase(record.raw);NorthlightReplayCopies::detach(record.copy);throw;}}
        owner->AddRef();
        // Identical for every instance of this final class; first class wins.
        static const ShapeInfo shape={*reinterpret_cast<const void* const*>(static_cast<T*>(this)),
            reinterpret_cast<const char*>(&record)-reinterpret_cast<const char*>(static_cast<T*>(this))};
        const ShapeInfo* expected=nullptr;Shape<T>::info.compare_exchange_strong(expected,&shape,std::memory_order_acq_rel);
    }
    ~Buffer(){
        {std::lock_guard<std::mutex> guard(mutex);records.erase(record.raw);records.erase(record.exposed);}
        NorthlightReplayCopies::detach(record.copy);
        NorthlightParticleUploads::cache().forget(record.raw);
        NorthlightParticleUploads::atlasUploads().forget(record.raw);
        this->real->Release();owner->Release();
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{
        // Serialize the final reference with GetStreamSource/GetIndices lookup.
        if(census)census->noteBuffer();
        ULONG count;
        {std::lock_guard<std::mutex> guard(mutex);count=InterlockedDecrement(&refs);
         if(!count){records.erase(record.raw);records.erase(record.exposed);}}
        if(!count)delete this;return count;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        if(!out)return E_POINTER;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DResource9)||id==__uuidof(T)){*out=static_cast<T*>(this);AddRef();return S_OK;}
        *out=nullptr;return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override{
        if(!out)return D3DERR_INVALIDCALL;*out=owner;owner->AddRef();return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(const GUID& guid,void* data,DWORD* size) override{
        // Arbitrary private data can contain an unwrapped COM interface.
        // Extension metadata reads use the raw buffer directly and avoid this.
        if(unsafeAccess)unsafeAccess(owner,"buffer private-data access");
        return this->real->GetPrivateData(guid,data,size);
    }
    HRESULT STDMETHODCALLTYPE Lock(UINT offset,UINT size,void** data,DWORD flags) override{
        if(census)census->noteBuffer();
        auto& source=NorthlightReplayCopies::unlockSource;
        // Preserve dynamic-buffer synchronization/rename boundaries: a later
        // NOOVERWRITE may rely on the last ordinary Lock or DISCARD completing.
        if(source.target==record.exposed&&data&&source.off==offset&&source.size==size&&record.metadata.known&&record.revision.load(std::memory_order_relaxed)&&
           ((flags==0&&!(record.metadata.usage&D3DUSAGE_DYNAMIC))||(discardBytes&&flags==D3DLOCK_NOOVERWRITE))&&
           NorthlightReplayCopies::enabled.load(std::memory_order_relaxed)&&NorthlightReplayCopies::unchangedWrite(record.copy,offset,size,source.bytes)){
            *data=const_cast<unsigned char*>(source.bytes);source.skipped=true;
            NorthlightParticleUploads::atlasUploads().write(record.raw,offset,size,source.bytes);
            noChangeWrites.fetch_add(1,std::memory_order_relaxed);noChangeBytes.fetch_add(size,std::memory_order_relaxed);
            skippedWrites.fetch_add(1,std::memory_order_relaxed);skippedBytes.fetch_add(size,std::memory_order_relaxed);return D3D_OK;
        }
        // Mark before entering the driver. Even failed writes conservatively
        // invalidate the old generation. READONLY never advances a generation.
        // Task 34: first of all, before the revision or the lock count move: a deferred draw may read the bytes this write replaces (spell_layer.h onBufferWrite).
        if(!(flags&D3DLOCK_READONLY)){
            if(record.materialGuarded.load(std::memory_order_relaxed)){
                const auto hook=materialWriter(record.raw);
                if(hook.beforeWrite)hook.beforeWrite(hook.context);
            }
            if(record.spellGuard.load(std::memory_order_relaxed)&&spellHook.write)
                spellHook.write(spellHook.context,record.raw,offset,size,flags,record.metadata.usage,record.locks.load(std::memory_order_relaxed));
        }
        // Publish lock admission and its generation together with metadata reads.
        // Driver calls and capture callbacks stay outside the registry lock.
        uint64_t previousRevision;unsigned lockedBefore;
        {
            std::lock_guard<std::mutex> guard(mutex);
            previousRevision=record.revision.load(std::memory_order_relaxed);
            lockedBefore=record.locks.fetch_add(1);
            if(!lockedBefore){rangePrevious=previousRevision;rangeWrite=rangeWhole=false;}
            if(!(flags&D3DLOCK_READONLY)){
                record.revision.store(clock.fetch_add(1),std::memory_order_relaxed);
                rangeWrite=true;rangeBegin=offset;
                rangeBytes=size?size:offset<=record.metadata.size?record.metadata.size-offset:0;
                rangeWhole|=(flags&D3DLOCK_DISCARD)||!record.metadata.known||offset>=record.metadata.size||!rangeBytes||rangeBytes>record.metadata.size-offset;
            }
            rangeWhole|=lockedBefore!=0;
        }
        const bool wasUnsafe=previousRevision==0;
        if((flags&D3DLOCK_DISCARD)&&discardBytes)NorthlightLockMeter::discard(NorthlightLockMeter::Game,discardBytes);
        // Reuse a content token only for a complete, successful, isolated write
        // whose live replay-side bytes prove unchanged. Partial DISCARD remains
        // invalidating: the driver may replace every byte outside that range.
        previousWriteRevision=pendingWriteRevision=0;
        if(!lockedBefore&&previousRevision&&!(flags&D3DLOCK_READONLY)&&record.metadata.known&&
           offset<=record.metadata.size&&(!size||uint64_t(offset)+size<=record.metadata.size)&&
           (!(flags&D3DLOCK_DISCARD)||(!offset&&(!size||size==record.metadata.size)))){
            previousWriteRevision=previousRevision;pendingWriteRevision=record.revision.load(std::memory_order_relaxed);
        }
        auto& atlas=NorthlightParticleUploads::atlasUploads();
        const bool lights=NorthlightParticleUploads::enabled.load(std::memory_order_relaxed);
        if((lights||atlas.watching(record.raw))&&!(flags&D3DLOCK_READONLY)){
            auto& uploads=NorthlightParticleUploads::cache();
            const bool valid=record.metadata.known&&offset<=record.metadata.size&&(!size||std::uint64_t(offset)+size<=record.metadata.size);
            const bool discard=(flags&D3DLOCK_DISCARD)||lockedBefore||!valid;
            if(lights)uploads.invalidate(record.raw,offset,size,discard);
            atlas.invalidate(record.raw,offset,size,discard);
            particleWrite={offset,valid?(size?size:UINT(record.metadata.size-offset)):0,!lockedBefore&&valid};
        }
        HRESULT hr=this->real->Lock(offset,size,data,flags);
        // 0.3.192 (CS): the write the stream replays (or a pass-through lock the game writes through) is mirrored into the CPU copy at Unlock.
        if(record.copy.attached&&!(flags&D3DLOCK_READONLY)&&NorthlightReplayCopies::enabled.load(std::memory_order_relaxed)){
            if(FAILED(hr))NorthlightReplayCopies::invalidate(record.copy);else NorthlightReplayCopies::writeLocked(record.copy,pendingWrite,lockedBefore,offset,size,data?*data:nullptr,flags);
        }
        if(FAILED(hr)){
            previousWriteRevision=pendingWriteRevision=0;
            particleWrite={};
            // A rejected write cannot repair tracking after an earlier failed
            // unlock. Keep the legacy exact-read path until a write succeeds.
            std::lock_guard<std::mutex> guard(mutex);
            if(wasUnsafe)record.revision.store(0,std::memory_order_relaxed);
            if(record.ranges)record.ranges->invalidate(record.revision.load(std::memory_order_relaxed));
            record.locks.fetch_sub(1,std::memory_order_release);
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override{
        if(census)census->noteBuffer();
        const bool copies=record.copy.attached&&NorthlightReplayCopies::enabled.load(std::memory_order_relaxed);
        const bool compare=previousWriteRevision&&record.revision.load(std::memory_order_relaxed)==pendingWriteRevision;
        const uint64_t bytes=pendingWrite.end-pendingWrite.off;
        const bool same=copies&&NorthlightReplayCopies::beforeUnlock(record.copy,pendingWrite,record.locks.load(std::memory_order_relaxed),compare); // pointer still mapped; compare uses the stream's cached source when available
        if(!copies)
            pendingWrite=NorthlightReplayCopies::PendingWrite{}; // copies turned off since Lock
        HRESULT hr=this->real->Unlock();
        if(particleWrite.active){
            const auto source=NorthlightReplayCopies::unlockSource;
            if(SUCCEEDED(hr)&&record.locks.load(std::memory_order_relaxed)==1&&source.bytes&&(!source.target||source.target==record.exposed)&&source.off<=particleWrite.offset&&
               std::uint64_t(particleWrite.offset-source.off)+particleWrite.size<=source.size){
                const auto* bytes=source.bytes+(particleWrite.offset-source.off);
                if(NorthlightParticleUploads::enabled.load(std::memory_order_relaxed))
                    NorthlightParticleUploads::cache().write(record.raw,record.metadata.identity,particleWrite.offset,
                        bytes,particleWrite.size,(record.metadata.usage&D3DUSAGE_DYNAMIC)!=0);
                NorthlightParticleUploads::atlasUploads().write(record.raw,particleWrite.offset,particleWrite.size,bytes);
            }
            else NorthlightParticleUploads::cache().forget(record.raw);
        }else if(FAILED(hr))NorthlightParticleUploads::cache().forget(record.raw);
        particleWrite={};
        if(copies&&FAILED(hr))NorthlightReplayCopies::invalidate(record.copy);
        std::lock_guard<std::mutex> guard(mutex);
        if(SUCCEEDED(hr)&&record.locks.load(std::memory_order_relaxed)){
            uint64_t expected=pendingWriteRevision;
            if(same&&record.revision.compare_exchange_strong(expected,previousWriteRevision,std::memory_order_relaxed)){
                noChangeWrites.fetch_add(1,std::memory_order_relaxed);noChangeBytes.fetch_add(bytes,std::memory_order_relaxed);
            }
            if(record.locks.load(std::memory_order_relaxed)==1&&rangeWrite){
                const auto current=record.revision.load(std::memory_order_relaxed);
                const bool whole=rangeWhole||rangeBytes==record.metadata.size;
                if(current!=rangePrevious){
                    // Draws can keep using the whole-resource stamp until the
                    // first partial write to a previously observed buffer.
                    if(!record.ranges&&record.rangeObserved&&!whole&&rangePrevious&&current>rangePrevious){
                        record.ranges=NorthlightBufferRanges::Revisions::create(record.metadata.size,rangePrevious);
                        record.rangeAllocationFailed=!record.ranges;
                    }
                    if(record.ranges)record.ranges->publish(rangePrevious,current,rangeBegin,rangeBytes,whole);
                }
            }
            record.locks.fetch_sub(1,std::memory_order_release);
        }
        else{record.revision.store(0,std::memory_order_relaxed);if(copies)NorthlightReplayCopies::invalidate(record.copy);} // tracking is unsafe until a subsequent successful write
        previousWriteRevision=pendingWriteRevision=0;return hr;
    }
};
template<class T,class Forward> void wrap(T** out,IDirect3DDevice9* owner,bool index,void(*unsafe)(IDirect3DDevice9*,const char*)=nullptr,MirrorGate* census=nullptr) noexcept{
    if(!out||!*out)return;
    try{*out=new Buffer<T,Forward>(*out,owner,index,unsafe,census);}catch(...){/* original resource remains valid, untracked */}
}
}
