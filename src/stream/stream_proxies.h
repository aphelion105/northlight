#pragma once
// 0.3.192 (CS): the game-facing stream proxies and the state shared with the replay thread (StreamCore, Registry).
// The game sees these objects instead of the Target's resources. A proxy answers what it can from its creation
// arguments, records the rest (generated record_* functions) or makes a sync call, and wraps the Target-level object
// (`inner`) that the replay thread creates when the queued create executes. Lifetime is reference counting by the queue:
// when game references plus StreamState binds reach zero the game thread records one Cmd::Destroy; every command that
// uses the proxy precedes it, so the replay thread needs no per-argument atomic.
//
// Locks (the rule: the game never sees bytes that differ from the real resource except bytes D3D9 leaves undefined):
//   buffers   a persistent CPU shadow (DYNAMIC at creation or later, or <= 4 MiB non-DYNAMIC; all evictable LRU, made by one readback at a
//             write re-lock, for DYNAMIC also at a DISCARD lock, while the budget admits it); every lock returns shadow memory and Unlock
//             records the locked range. Buffers above the full-shadow limit retain one bounded exact interval for staged re-locks;
//             unknown ranges pass through. First writes and DISCARD need no old bytes and stage directly.
//   textures  the first write lock of a level/face after creation (or any DISCARD lock) is asynchronous through a staging
//             Block with our own pitch; everything else passes through. Surfaces of render targets and depth buffers always do.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "command_queue.h"
#include "buffer_range_shadow.h"
#include "query_poll.h"
#include "query_backend_threads.h"
#ifdef NORTHLIGHT_REPLAY_STAND
#include "render_thread_probe.h"
#endif
#include "command_stream.inl"
#include "tex_readback_diag.h"
#include "atlas_upload_proof.h"
#include "particle_texture_stats.h"
#include "tree_canopy_textures.h"
#include "particle_upload_range_12340.h"

namespace NorthlightStream {
// D3D9 ABI values the stream needs (identical in d3d9.h / d3d9types.h; kept here so the code reads the same under the test stub).
namespace D3 {
constexpr DWORD kUsageRT=0x1,kUsageDS=0x2,kUsageWriteOnly=0x8,kUsageDynamic=0x200,kUsageAutoGen=0x400;
constexpr DWORD kLockReadOnly=0x10,kLockNoOverwrite=0x1000,kLockDiscard=0x2000;
constexpr DWORD kPoolDefault=0,kPoolManaged=1,kPoolSystem=2,kPoolScratch=3;
constexpr unsigned kTypeSurface=1,kTypeVolume=2,kTypeTexture=3,kTypeVolumeTexture=4,kTypeCube=5,kTypeVB=6,kTypeIB=7;
constexpr unsigned kFmtVertexData=100,kFmtIndex16=101,kFmtIndex32=102;
constexpr DWORD kSpdIUnknown=1,kGetDataFlush=1,kIssueEnd=1,kIssueBegin=2;
constexpr unsigned kQVcache=4,kQEvent=8,kQOcclusion=9,kQTimestamp=10,kQDisjoint=11,kQFreq=12;
struct FormatInfo {unsigned bytes,bw,bh;bool ok;};
inline FormatInfo formatInfo(unsigned f){
    switch(f){
    case 21:case 22:case 31:case 32:case 33:case 34:case 35:case 62:case 63:case 64:case 67:case 71:case 75:case 77:case 79:case 83:case 112:case 114:return {4,1,1,true};
    case 23:case 24:case 25:case 26:case 29:case 30:case 51:case 60:case 61:case 70:case 73:case 80:case 81:case 111:return {2,1,1,true};
    case 28:case 50:case 52:return {1,1,1,true};
    case 36:case 110:case 113:case 115:return {8,1,1,true};
    case 116:return {16,1,1,true};
    case 0x31545844:return {8,4,4,true};                                           // DXT1
    case 0x32545844:case 0x33545844:case 0x34545844:case 0x35545844:return {16,4,4,true};   // DXT2..DXT5
    default:return {0,1,1,false};
    }
}
inline UINT mipDim(UINT v,UINT level){const UINT r=v>>level;return r?r:1;}
inline UINT fullChain(UINT w,UINT h,UINT d){UINT m=w>h?w:h;if(d>m)m=d;UINT n=1;while(m>1){m>>=1;++n;}return n;}
}

enum class Kind:std::uint8_t{Surface,Texture,CubeTexture,VolumeTexture,Volume,VertexBuffer,IndexBuffer,VertexShader,PixelShader,VertexDeclaration,StateBlock,Query,SwapChain};
// Why a lock (or create) took the synchronous path: the CSTREAM census.
enum class PassReason:std::uint8_t{ReadOnly,NoShadow,Written,Format,Size,Budget,RenderTarget,Other,Count};
inline const char* passReasonName(unsigned r){static const char* n[]={"readonly","noshadow","written","format","size","budget","rendertarget","other"};return r<8?n[r]:"?";}

struct StreamCore;
struct ProxyBase;
struct SubRes;
// Proxies alive in the process (a leak check for the tests and the CSTREAM diagnostics).
inline std::atomic<long> liveProxyObjects{0};

// Creation arguments of any resource kind; what GetDesc/GetLevelDesc/GetLevelCount answer from.
struct Info {
    UINT w=0,h=0,d=1,levels=1,length=0;DWORD usage=0,pool=0,fvf=0,msq=0;unsigned fmt=0,ms=0,type=0;
};

constexpr std::size_t kLockSlack=4096;   // bytes past a staged range / a shadow, never replayed: a small game overrun stays inside the allocation (shadows zero them, staging does not need to)

// A Task runs on the replay thread while the game thread waits (pumped): pass-through locks, creates under memory
// pressure, GetData(FLUSH), Reset, the defaults batch, final release. fn executes with the replay-side core.
struct Task {void (*fn)(void*,StreamCore&);void* arg;};

// The retirement link shares the allocation with the bytes so a queued
// reader can keep a discarded slice alive without any further allocation.
struct SliceMem {
    struct Header {
        unsigned char* allocation;
        Header* next=nullptr;
        std::uint64_t seq=0;
        std::size_t bytes=0;
        bool large=false;
    };
    struct Reclaim {
        void operator()(Header* node)const noexcept{
            while(node){
                Header* next=node->next;
                delete[] node->allocation;
                node=next;
            }
        }
    };
    using Storage=std::unique_ptr<Header,Reclaim>;
    Storage storage;
    void allocate(std::size_t size,bool zero){
        if(size>SIZE_MAX-sizeof(Header))throw std::bad_alloc();
        auto* allocation=new unsigned char[sizeof(Header)+size];
        storage.reset(::new(static_cast<void*>(allocation)) Header{allocation});
        if(zero)std::memset(data(),0,size);
    }
    unsigned char* data(){return storage->allocation+sizeof(Header);}
    const unsigned char* data()const{return storage->allocation+sizeof(Header);}
};
struct PrivEntry {std::string key;std::vector<unsigned char> bytes;IUnknown* unk=nullptr;};
#ifdef NORTHLIGHT_REPLAY_STAND
inline std::atomic<std::uint64_t> standProxySerial{0};
inline std::uint64_t nextStandProxyGeneration(){
    auto value=standProxySerial.load(std::memory_order_relaxed);
    while(value!=~std::uint64_t(0))if(standProxySerial.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return value+1;
    return 0; // Exhaustion remains permanently unprovable, never wraps to an old identity.
}
#endif

struct ProxyBase {
#ifdef NORTHLIGHT_REPLAY_STAND
    const std::uint64_t standGeneration=nextStandProxyGeneration();
#endif
    StreamCore* core;Kind kind;IUnknown* unk=nullptr;   // unk: the proxy as the interface pointer the game holds (registry key)
    IUnknown* inner=nullptr;                            // the Target-level object: replay thread only, set when the create ran
    IUnknown* raw=nullptr;                              // the backend object behind `inner` (what Device would unwrap it to), cached with it; null = not provably a pure unwrap: replay through the Device
    std::atomic<LONG> refs{1},use{1},pendingDestroy{0};  // refs: what Release reports; use = refs + the in-use children (a parent's); StreamState binds are counted in `binds`, not here
    // 0.3.204 (task 21): StreamState binds of this proxy. Single writer at any time: the game thread, or the replay thread while the game thread waits for it (the defaults batch of Reset / init: runTask is the handoff) - so it
    // is updated with a plain load+store (no locked instruction on the per-draw bind path) and read by the replay thread only in Replayer::destroy (an atomic, so the read is race-free). The proxy is "in use" (not destroyable,
    // keeps its parent in use) while use>0 || binds>0; the Destroy is recorded by whichever operation takes the total to 0: useDec when binds==0, bindRelease when use==0. Audit: `use` is written by AddRef/Release (game thread; a final
    // Release records the Destroy, which only the producer thread may do), by the replay thread's implicit-proxy hand-outs (also under a game wait), and read by Replayer::destroy; nothing else touches use/binds. A final COM Release from a foreign thread was already unsupported (it records the Destroy on the
    // producer queue) and stays so: use and binds then have one writer at a time, which the split relies on.
    std::atomic<LONG> binds{0};
    // Replayer::destroy's test. `use` is read FIRST (seq_cst): a game AddRef -> bind -> Release of a re-handed proxy then shows either use>0 or, after the
    // Release (a release RMW sequenced after the bind store), binds>0 to the binds load that follows. Two independent loads in either order could miss both.
    LONG inUse()const{const LONG u=use.load(std::memory_order_seq_cst);const LONG b=binds.load(std::memory_order_seq_cst);return u+b;}
    std::uint64_t readySeq=0;                            // 0.3.206 (task 31): the recorded seq of the command that binds `inner` (Create / Derive); 0 = unknown (replay-side implicit proxies): never an express readback
    std::atomic<bool> dead{false};                       // the real create failed: commands on it are dropped
    ProxyBase* parent=nullptr;std::vector<ProxyBase*> kids;   // children (levels, faces, back buffers): owned by the parent
    Info info;DWORD priority=0;std::vector<PrivEntry> priv;
    ProxyBase(StreamCore* c,Kind k):core(c),kind(k){}
    virtual ~ProxyBase();
    ProxyBase(const ProxyBase&)=delete;ProxyBase& operator=(const ProxyBase&)=delete;
    Queue& streamQueue();
    struct CallScope callScope(std::uint16_t id);   // 0.3.204 (task 21): sampled game-thread timer of a generated method body (defined below StreamCore)
    // The DXVK model for the device's lifetime: a top-level proxy with public references (refs above `baseline`) holds one
    // reference on the StreamDevice, so the device, the queue and the replay thread outlive every object the game still holds.
    // StreamState binds count in `binds` only (see there): they never pin the device (or it would never die). A child's public reference
    // is its parent's public reference; a child in use keeps its parent in use.
    LONG baseline=0;   // refs the proxy holds itself without pinning (the swap chain's own)
    void pinDevice();
    ULONG comAddRef(){
        const LONG n=refs.fetch_add(1)+1;const bool pin=!parent&&n==baseline+1;
        if(n==1&&parent)parent->comAddRef();
        useInc();if(pin)pinDevice();return ULONG(n);}
    ULONG comRelease();
    void adoptKid(){comAddRef();useInc();}   // a new child starts with one public reference and one use: the same on this parent
    void useInc(){if(use.fetch_add(1)==0&&binds.load(std::memory_order_relaxed)==0&&parent)parent->useInc();}
    void useDec();                                                        // game thread: use 1->0 with no binds records Destroy (or releases the container)
    void gone();                                                          // the total (use+binds) reached 0: record Destroy, or release the container
    void bindAdd(){const LONG b=binds.load(std::memory_order_relaxed);binds.store(b+1,std::memory_order_relaxed);if(b==0&&use.load(std::memory_order_relaxed)==0&&parent)parent->useInc();}   // StreamState bind: no locked instruction
    void bindRelease(){const LONG b=binds.load(std::memory_order_relaxed)-1;binds.store(b,std::memory_order_relaxed);if(b==0&&use.load(std::memory_order_relaxed)==0)gone();}
    // Local private data (nothing of it reaches the Target).
    template<class G> static std::string guidKey(const G& g){return std::string(reinterpret_cast<const char*>(&g),sizeof g);}
    template<class G> HRESULT setPrivate(const G& guid,const void* data,DWORD size,DWORD flags){
        if(!data&&size)return D3DERR_INVALIDCALL;
        freePrivate(guid);PrivEntry e;e.key=guidKey(guid);
        if(flags&D3::kSpdIUnknown){if(size!=sizeof(IUnknown*))return D3DERR_INVALIDCALL;e.unk=*static_cast<IUnknown* const*>(data);if(e.unk)e.unk->AddRef();}
        else e.bytes.assign(static_cast<const unsigned char*>(data),static_cast<const unsigned char*>(data)+size);
        try{priv.push_back(std::move(e));}catch(...){if(e.unk)e.unk->Release();return E_OUTOFMEMORY;}
        return D3D_OK;
    }
    template<class G> HRESULT getPrivate(const G& guid,void* data,DWORD* size){
        if(!size)return D3DERR_INVALIDCALL;
        const std::string k=guidKey(guid);
        for(auto& e:priv)if(e.key==k){
            const DWORD need=e.unk?DWORD(sizeof(IUnknown*)):DWORD(e.bytes.size());
            if(!data){*size=need;return D3D_OK;}
            if(*size<need){*size=need;return D3DERR_MOREDATA;}
            if(e.unk){e.unk->AddRef();std::memcpy(data,&e.unk,sizeof e.unk);}else if(need)std::memcpy(data,e.bytes.data(),need);
            *size=need;return D3D_OK;
        }
        return D3DERR_NOTFOUND;
    }
    template<class G> HRESULT freePrivate(const G& guid){
        const std::string k=guidKey(guid);
        for(std::size_t i=0;i<priv.size();++i)if(priv[i].key==k){if(priv[i].unk)priv[i].unk->Release();priv.erase(priv.begin()+i);return D3D_OK;}
        return D3DERR_NOTFOUND;
    }
    void dropPrivate(){for(auto& e:priv)if(e.unk)e.unk->Release();priv.clear();}
    // The proxy behind an interface pointer the game passed (a virtual QueryInterface with a private IID: no lock, no lookup);
    // nullptr for an object that is not a stream proxy. No reference is taken.
    static ProxyBase* of(IUnknown* u);        // lock-free shape check (below the proxy classes)
    template<class T> static ProxyBase* of(T* u);
    static ProxyBase* ofSlow(IUnknown* u);    // a virtual QueryInterface with a private IID: any pointer, however unknown its shape
    template<class... T> static bool iidIs(REFIID id){return id==__uuidof(IUnknown)||((id==__uuidof(T))||...);}
    HRESULT devGet(IDirect3DDevice9** pp);
    // Helpers every resource macro class needs: generic hooks with nothing to do, and the sync forwarder.
    template<Cmd C,class... A> void observe(CmdTag<C>,A&&...){}
    template<Cmd C,class... A> typename MethodTraits<C>::Ret syncCall(CmdTag<C> t,A... a);
};

class Registry {
    std::mutex m_;std::unordered_map<const void*,ProxyBase*> byProxy_,byInner_;
public:
    void addProxy(ProxyBase* p){std::lock_guard<std::mutex> l(m_);byProxy_[p->unk]=p;}
    ProxyBase* findProxy(const void* iface){std::lock_guard<std::mutex> l(m_);auto i=byProxy_.find(iface);return i==byProxy_.end()?nullptr:i->second;}
    // Direct replay: Registry::rawOf resolves the backend object behind a Device-level one, once, right here (replay thread, outside the lock).
    std::function<IUnknown*(IUnknown*,Kind)> rawOf;
    static bool takesRaw(Kind k){return k==Kind::Surface||k==Kind::Texture||k==Kind::CubeTexture||k==Kind::VolumeTexture||k==Kind::VertexBuffer||k==Kind::IndexBuffer||k==Kind::VertexShader||k==Kind::PixelShader||k==Kind::VertexDeclaration;}
    void bindInner(ProxyBase* p,IUnknown* inner){
        if(p->kind==Kind::Texture){NorthlightParticleTextures::cache().erase(p->raw,p);NorthlightMaterialTextures::cache().erase(p->raw);NorthlightTreeCanopy::forget(p->raw);}
        {std::lock_guard<std::mutex> l(m_);p->inner=inner;byInner_[inner]=p;}
        p->raw=rawOf&&inner&&takesRaw(p->kind)?rawOf(inner,p->kind):nullptr;
    }   // the newest proxy of an object wins identity
    void unbindInner(ProxyBase* p){if(p->kind==Kind::Texture){NorthlightParticleTextures::cache().erase(p->raw,p);NorthlightMaterialTextures::cache().erase(p->raw);NorthlightTreeCanopy::forget(p->raw);}p->raw=nullptr;std::lock_guard<std::mutex> l(m_);if(p->inner){auto i=byInner_.find(p->inner);if(i!=byInner_.end()&&i->second==p)byInner_.erase(i);}}
    ProxyBase* findInner(const void* inner){std::lock_guard<std::mutex> l(m_);auto i=byInner_.find(inner);return i==byInner_.end()?nullptr:i->second;}
    // Replay thread (destroy): forget the proxy; the inner entry only if it still names this proxy.
    void erase(ProxyBase* p){if(p->kind==Kind::Texture){NorthlightParticleTextures::cache().erase(p->raw,p);NorthlightMaterialTextures::cache().erase(p->raw);NorthlightTreeCanopy::forget(p->raw);}std::lock_guard<std::mutex> l(m_);byProxy_.erase(p->unk);if(p->inner){auto i=byInner_.find(p->inner);if(i!=byInner_.end()&&i->second==p)byInner_.erase(i);}}
    // The Target-level object behind a proxy pointer the game stored in its own memory (an opaque value, never dereferenced).
    const void* innerOf(const void* maybeProxy){std::lock_guard<std::mutex> l(m_);auto i=byProxy_.find(maybeProxy);return i==byProxy_.end()||!i->second->inner?maybeProxy:static_cast<const void*>(i->second->inner);}
#ifdef NORTHLIGHT_REPLAY_STAND
    static bool textureFamily(Kind kind){return kind==Kind::Texture||kind==Kind::CubeTexture||kind==Kind::VolumeTexture;}
    bool textureGeneration(const void* exposed,std::uint64_t& generation){
        generation=0;std::lock_guard<std::mutex> lock(m_);const auto found=byProxy_.find(exposed);
        if(found==byProxy_.end())return false;const auto* proxy=found->second;
        if(!textureFamily(proxy->kind)||proxy->dead.load()||!proxy->standGeneration||proxy->inUse()<=0)return false;
        generation=proxy->standGeneration;return true;
    }
    const void* textureInner(const void* exposed,std::uint64_t generation){
        std::lock_guard<std::mutex> lock(m_);const auto found=byProxy_.find(exposed);
        if(found==byProxy_.end())return nullptr;const auto* proxy=found->second;
        // Public references may already be released by the producer. Only the
        // ordered replay Destroy removes the generation before later lookups.
        return textureFamily(proxy->kind)&&generation&&proxy->standGeneration==generation&&!proxy->dead.load()&&proxy->inner?
            static_cast<const void*>(proxy->inner):nullptr;
    }
#endif
    // Init failure only (the replay thread is gone or about to be): release every Target object the proxies hold, and hand the proxies out to be deleted.
    void releaseInners(){std::lock_guard<std::mutex> l(m_);for(auto& e:byProxy_)if(e.second->inner){e.second->inner->Release();e.second->inner=nullptr;e.second->dead.store(true);}byInner_.clear();}
    void takeAll(std::vector<ProxyBase*>& out){std::lock_guard<std::mutex> l(m_);for(auto& e:byProxy_)out.push_back(e.second);byProxy_.clear();byInner_.clear();}
    std::size_t proxies(){std::lock_guard<std::mutex> l(m_);return byProxy_.size();}
};

// 0.3.192 (CS): intrusive LRU list (the CPU shadows' victim order). head = least recently used, pushBack = most recent; every mutation under StreamCore::texMutex
// (the game thread touches and evicts, the replay thread removes a dying proxy's shadow). T has a member `lru`. Eviction is O(victims), a touch O(1); a touch of the
// element that is already the tail needs no lock (only the owning game thread appends, and a replay-thread removal never concerns a shadow that is being locked).
template<class T> struct LruNode {T* prev=nullptr;T* next=nullptr;bool linked=false;};
template<class T> struct LruList {
    T* head=nullptr;std::atomic<T*> tail{nullptr};
    void pushBack(T* x){auto& n=x->lru;n.prev=tail.load(std::memory_order_relaxed);n.next=nullptr;if(n.prev)n.prev->lru.next=x;else head=x;tail.store(x,std::memory_order_relaxed);n.linked=true;}
    void remove(T* x){
        auto& n=x->lru;if(!n.linked)return;
        if(n.prev)n.prev->lru.next=n.next;else head=n.next;
        if(n.next)n.next->lru.prev=n.prev;else tail.store(n.prev,std::memory_order_relaxed);
        n.prev=n.next=nullptr;n.linked=false;
    }
    bool isTail(const T* x)const{return tail.load(std::memory_order_relaxed)==x;}
};
struct BufferState;
// 0.3.192 (CS): game-side scratch for SMALL staged buffer locks (first write / DISCARD without a shadow): a handful of slots, taken and given back without a lock
// or a Block (the pooled Blocks share a mutex with the replay thread's retire). Game thread takes; give() may run on the replay thread (a buffer destroyed while locked).
struct LockScratch {
    static constexpr std::size_t kMaxBytes=std::size_t(64)<<10,kSlots=4;
    std::vector<unsigned char> slot[kSlots];std::atomic<bool> busy[kSlots]{};
    unsigned char* take(std::size_t bytes,unsigned char& idx){
        if(bytes>kMaxBytes)return nullptr;
        for(unsigned i=0;i<kSlots;++i){
            if(busy[i].load(std::memory_order_acquire))continue;
            const std::size_t need=((bytes+kLockSlack)+4095)&~std::size_t(4095);   // the slack: a small game overrun stays inside the allocation (never replayed, so not zeroed)
            if(slot[i].size()<need){try{slot[i].resize(need);}catch(...){return nullptr;}}
            busy[i].store(true,std::memory_order_relaxed);idx=(unsigned char)i;return slot[i].data();
        }
        return nullptr;
    }
    void give(unsigned char idx){busy[idx].store(false,std::memory_order_release);}
    void trim(){for(unsigned i=0;i<kSlots;++i)if(!busy[i].load(std::memory_order_acquire))std::vector<unsigned char>().swap(slot[i]);}   // game thread (pressure)
};

// Everything the game thread and the replay thread share. One per stream.
struct StreamCore {
    Queue q;Registry reg;
    IDirect3DDevice9* target=nullptr;   // the Device (Target): replay thread only after the handoff
    IDirect3DDevice9* game=nullptr;     // the StreamDevice, for GetDevice
    IDirect3DDevice9* ext=nullptr;      // the ExtensionDevice (same real device, same mirror): direct replay calls it with raw pointers; null = through the Device
    std::atomic<bool> replayFailure{false};      // a replayed setter failed: StreamState invalidates itself at its next Get
    std::atomic<bool> memoryPressure{false};     // published by the replay side (Device's memory guard), consumed at Present
    std::atomic<unsigned> availableTextureMem{0};
    std::atomic<std::uint64_t> syncOnly[14]{};    // bit per audited scalar slot, see StreamState
    std::atomic<std::uint64_t> framesReplayed{0};
    // The live level shadows (see SubRes): inserted and evicted on the game thread, removed by a proxy's destructor on the replay thread.
    // Live level shadows in LRU order (never re-locked "fresh" ones are evicted before re-locked ones) and live buffer shadows (the large allowance apart): all under texMutex.
    std::mutex texMutex;LruList<SubRes> texFresh,texRelocked;LruList<BufferState> bufRegular,bufLarge;
    // 0.3.200 (pipeline): allocations of level shadows the game thread evicted, kept (under texMutex) for the next level shadow instead of a free + malloc pair;
    // at most kTexSpares of at most kTexSpareMaxBytes each (~0.5 MiB outside the texture-shadow cap), none kept under memory pressure (trimTexSpares).
    static constexpr unsigned kTexSpares=2;static constexpr std::size_t kTexSpareMaxBytes=(std::size_t(256)<<10)+kLockSlack;
    std::vector<unsigned char> texSpare[kTexSpares];
    LockScratch scratch;             // game thread: small staged buffer locks
    bool occlusionLocal=true;        // StreamOcclusionLocal: a FLUSH GetData on an occlusion query with a polled result is answered on the game thread (StreamQuery::GetData)
    unsigned framesAhead=1;          // 0.3.204 (task 21): StreamFramesAhead (clamped), set once by the StreamDevice: the buffers' slice rings hold framesAhead+2 slices at most
    // 0.3.204 (task 21): large-buffer slices that were dropped or swapped away while the replay thread may still read them (a zero-copy unlock refers to the bytes until it retires): the storage waits here
    // (under texMutex) with the seq of the last command that reads it, and is freed by trimRetired once replayedSeq has passed it. Their bytes leave shadowBytes/largeShadowBytes at once and count in the ring budget (retired*Bytes) until freed.
    SliceMem::Storage retired;
    // 0.3.204 (task 21): Diagnostics split timers. timing is the game thread's own copy of the Diagnostics switch, refreshed at each Present (StreamDevice::presentCommon);
    // timingN counts the generated calls for the 1-in-16 sampling. Both are game-thread only: the replay thread never runs a game-facing method or a Lock/Unlock path.
    bool timing=false;unsigned timingN=0;
    TexReadbackDiag texDiag;   // 0.3.206 (task 31): per-level texture readback report (game thread; recorded only while timing)
    IdleImageLocks idleImages; // bounded RenderProfile-only Lock entry/return/fill provenance; atomic replay observations
    const std::function<void(const char*)>* idleLog=nullptr; // existing Replayer sink, producer-only use; cleared after join
    std::uint64_t idleLogWindow=0;unsigned idleLogCount=0;
    void reportIdleImage(const IdleImageLock& p){
        if(!idleImages.enabled()||!p.id||!idleLog||!*idleLog||!callerModule)return;
        const auto window=frameNo/120;if(window!=idleLogWindow){idleLogWindow=window;idleLogCount=0;}
        if(idleLogCount>=4)return;++idleLogCount;
        // The module formatter has a game-thread-only static buffer. Resolve
        // only long completed locks here, never from replay or every Lock.
        try{char line[800];std::snprintf(line,sizeof line,"CSTREAM IMAGE_LOCK_LONG id=%llu entryNs=%llu returnedNs=%llu unlockEntryNs=%llu caller=%s rawCaller=0x%llx gameFrame=%llu epoch=%u fmt=%u size=%ux%ux%u level=%u face=%u pool=%u usage=0x%x flags=0x%x route=%u mode=%u lockApiNs=%lld fillNs=%lld scope=observed-lock-interval-not-cpu-work",
            (unsigned long long)p.id,(unsigned long long)p.entryNs,(unsigned long long)p.returnedNs,(unsigned long long)p.unlockNs,callerModule(reinterpret_cast<void*>(std::uintptr_t(p.caller))),
            (unsigned long long)p.caller,(unsigned long long)p.frame,p.epoch,p.fmt,p.w,p.h,p.d,p.level,p.face,p.pool,p.usage,p.flags,p.route,p.mode,
            IdleGapProfile::delta(p.entryNs,p.returnedNs),IdleGapProfile::delta(p.returnedNs,p.unlockNs));(*idleLog)(line);}catch(...){}
    }
    // Game-thread counters; reuse runTask's wait clock, without timing each GetData again.
    using QueryWait=QueryWaitProfile;
    QueryWait queryWait[21]{},queryFrame[21]{};unsigned queryWaitFrames=0; // 20 includes unknown query types
    EventQueueProfile eventQueueFrame;
    QueryLongPause queryLongPause; // replay-thread only; at most one lazy timer per stream
    QueryBackendThreads queryBackendThreads; // game-thread only; bounded RenderProfile diagnostics
    std::function<void(const char*)> queryThreadLog; // Options::log, like QUERYFRAME; legacy logLine is unset
    void clearQueryWait(){for(auto& item:queryWait)item={};for(auto& item:queryFrame)item={};eventQueueFrame={};queryWaitFrames=0;queryBackendThreads.clear();}
    void reportQueryFrame(const std::function<void(const char*)>& sink){
#ifdef NORTHLIGHT_REPLAY_STAND
        const bool sampled=NorthlightRenderThreadProbe::sampleFrame(unsigned(frameNo),true);
#else
        const bool sampled=frameNo%127==0;
#endif
        if(sink)for(unsigned type=0;type<21;++type){const auto& s=queryFrame[type];if(!s.frameReportDue(sampled))continue;
            char line[1024];std::snprintf(line,sizeof line,"CSTREAM QUERYFRAME gameFrame=%llu replayed=%llu type=%u calls=%llu pending=%llu errors=%llu syncWaitMs=%.4f beforeReplayMs=%.4f replayMs=%.4f afterReplayMs=%.4f tripMs=%.4f maxTripMs=%.4f eventFastTrips=%llu eventLongTrips=%llu eventFastMs=%.4f eventLongMs=%.4f observedPauseCalls=%llu observedPauseMs=%.4f observedPauseOverMs=%.4f observedPauseMaxMs=%.4f observedPauseMaxRequestedUs=%.3f observedPauseMaxOverMs=%.4f backendUntouched=%llu backendFalse=%llu backendReady=%llu backendOther=%llu fenceCalls=%llu fenceMs=%.4f fenceTimeout=%llu",
                (unsigned long long)frameNo,(unsigned long long)framesReplayed.load(std::memory_order_relaxed),type,
                (unsigned long long)s.calls,(unsigned long long)s.pending,(unsigned long long)s.errors,
                double(s.waitNs)/1e6,double(s.beforeNs)/1e6,double(s.replayNs)/1e6,double(s.afterNs)/1e6,double(s.tripNs)/1e6,double(s.maxTripNs)/1e6,
                (unsigned long long)s.eventFastTrips,(unsigned long long)s.eventLongTrips,double(s.eventFastNs)/1e6,double(s.eventLongNs)/1e6,
                (unsigned long long)s.eventPauses.count,double(s.eventPauses.actualNs)/1e6,double(s.eventPauses.overshootNs)/1e6,double(s.eventPauses.maxNs)/1e6,double(s.eventPauses.maxRequestedNs)/1e3,double(s.eventPauses.maxOvershootNs)/1e6,(unsigned long long)s.backendUntouched,(unsigned long long)s.backendFalse,(unsigned long long)s.backendReady,(unsigned long long)s.backendOther,(unsigned long long)s.fenceCalls,double(s.fenceNs)/1e6,(unsigned long long)s.fenceTimeouts);sink(line);}
        if(sink&&(sampled||queryFrame[8].frameReportDue(false))&&queryFrame[8].eventCpuSamples){
            char line[220];std::snprintf(line,sizeof line,"CSTREAM EVENT_CPU gameFrame=%llu cycles=%llu samples=%llu legacyGpuSpin=%u shortGpuTrips=%llu",
                (unsigned long long)frameNo,(unsigned long long)queryFrame[8].eventCpuCycles,
                (unsigned long long)queryFrame[8].eventCpuSamples,unsigned(NorthlightStream::legacyEventGpuSpin()),
                (unsigned long long)queryFrame[8].eventShortGpuTrips);sink(line);
        }
        const auto& e=eventQueueFrame;
        if(sink&&e.samples&&(sampled||queryFrame[8].frameReportDue(false))){
            char line[440];std::snprintf(line,sizeof line,
                "CSTREAM EVENT_QUEUE gameFrame=%llu samples=%u drained=%u publishMs=%.4f handoffMs=%.4f drainMs=%.4f entryMs=%.4f queuedCommands=%llu retiredCommands=%llu maxHandoffMs=%.4f maxHandoffBehind=%u",
                (unsigned long long)frameNo,e.samples,e.drained,double(e.publishNs)/1e6,double(e.handoffNs)/1e6,
                double(e.drainNs)/1e6,double(e.entryNs)/1e6,(unsigned long long)e.queuedCommands,
                (unsigned long long)e.retiredCommands,double(e.maxHandoffNs)/1e6,e.maxHandoffBehind);sink(line);
        }
        eventQueueFrame={};
        for(auto& item:queryFrame)item={};
    }
    void reportQueryWait(const std::function<void(const char*)>& sink){
        if(sink)for(unsigned type=0;type<21;++type){const auto& s=queryWait[type];if(!s.calls)continue;
            char line[1024];std::snprintf(line,sizeof line,"CSTREAM QUERY frames=%u type=%u calls=%llu flush=%llu unissued=%llu pending=%llu errors=%llu syncWaitMs=%.3f beforeReplayMs=%.3f replayMs=%.3f afterReplayMs=%.3f tripMs=%.3f maxTripMs=%.3f eventFastTrips=%llu eventLongTrips=%llu eventFastMs=%.3f eventLongMs=%.3f observedPauseCalls=%llu observedPauseMs=%.3f observedPauseOverMs=%.3f observedPauseMaxMs=%.3f observedPauseMaxRequestedUs=%.3f observedPauseMaxOverMs=%.3f backendUntouched=%llu backendFalse=%llu backendReady=%llu backendOther=%llu fenceCalls=%llu fenceMs=%.4f fenceTimeout=%llu",
                queryWaitFrames,type,(unsigned long long)s.calls,(unsigned long long)s.flush,(unsigned long long)s.unissued,
                (unsigned long long)s.pending,(unsigned long long)s.errors,double(s.waitNs)/1e6,
                double(s.beforeNs)/1e6,double(s.replayNs)/1e6,double(s.afterNs)/1e6,double(s.tripNs)/1e6,double(s.maxTripNs)/1e6,
                (unsigned long long)s.eventFastTrips,(unsigned long long)s.eventLongTrips,double(s.eventFastNs)/1e6,double(s.eventLongNs)/1e6,
                (unsigned long long)s.eventPauses.count,double(s.eventPauses.actualNs)/1e6,double(s.eventPauses.overshootNs)/1e6,double(s.eventPauses.maxNs)/1e6,double(s.eventPauses.maxRequestedNs)/1e3,double(s.eventPauses.maxOvershootNs)/1e6,(unsigned long long)s.backendUntouched,(unsigned long long)s.backendFalse,(unsigned long long)s.backendReady,(unsigned long long)s.backendOther,(unsigned long long)s.fenceCalls,double(s.fenceNs)/1e6,(unsigned long long)s.fenceTimeouts);sink(line);}
        clearQueryWait();
    }
    std::uint64_t clockNs=0;   // the cost of one nowNs() as the scopes see it (measured once at the first timed Present), subtracted from every timed span
    std::uint64_t frameNo=0;         // game thread: Presents so far (the idle clock of buffer shadows)
    DWORD (*readBackLock)()=nullptr;             // flags of the stream's own READONLY read-backs of buffers (NorthlightUpload::readBackLock in the DLL); null = READONLY
    void (*logLine)(const char*)=nullptr;        // diagnostics sink (renderer.cpp's logf); may be null
    void (*queryCaller)(std::uint64_t,void*,void*)=nullptr; // first EVENT/FLUSH per query, game thread, diagnostics only
    static constexpr std::size_t slicesFor(unsigned framesAhead){return std::size_t(clampFramesAhead(framesAhead))+2;}   // 0.3.204 (task 21): the least slices a buffer may own in all (current + ring)
    // 0.3.204 (task 21): a buffer that DISCARDs n times per frame has n x (framesAhead+1) + 1 slices in flight at most; never below slicesFor, never above kRingHardMax (still subject to the memory rules)
    static constexpr std::size_t kRingHardMax=12;
    static constexpr std::size_t allowedSlices(unsigned framesAhead,unsigned discardsPerFrame){
        const std::size_t want=std::size_t(discardsPerFrame)*(clampFramesAhead(framesAhead)+1)+1,base=slicesFor(framesAhead);
        return (want>base?want:base)<kRingHardMax?(want>base?want:base):kRingHardMax;}
    explicit StreamCore(std::size_t budget=BudgetBytes):q(budget){}
    void log(const char* text){if(logLine)logLine(text);}
    // Replay thread: Target-level interface pointer -> game-facing proxy (taking over the reference the Target returned).
    template<class T> T* toProxy(T* innerRef);
    template<class T> ProxyBase* toProxyBound(T* innerRef){return innerRef?proxyFor(innerRef,true):nullptr;}   // for StreamState: one use (the bind), no game reference
    template<class T> ProxyBase* proxyFor(T* innerRef,bool bound);
    ProxyBase* makeImplicit(IUnknown* inner);
    std::atomic<HRESULT> presentResult{D3D_OK};
    // 0.3.204 (task 21): the cooperative level TestCooperativeLevel reports, written ONLY by the replay thread (after each real Present, in the Reset task and in the
    // sync TestCooperativeLevel task); the game thread answers D3D_OK from it locally and goes synchronous for anything else. Loss shows up at most StreamFramesAhead frames late.
    std::atomic<HRESULT> coopState{D3D_OK};
    std::atomic<std::uint32_t> queryEpoch{0}; // invalidate cached EVENT completion across Reset
    const char* (*callerModule)(void* address)=nullptr;   // diagnostics: "module+0xoffset" of a TestCooperativeLevel caller and, since 0.3.206 (task 31), of a texture lock caller in the TEXREADBACK lines (renderer.cpp, Windows; logged only with Diagnostics on); null in tests
    // Real HRESULT of the last Present commands (the game reads the one StreamFramesAhead frames back). 0.3.200 (pipeline): the slot is the Present's ordinal
    // (the replay's framesReplayed before it counts this one = the game's frameNo when it recorded it), seq still checked: with at most kMaxFramesAhead+1
    // Presents in flight no newer one can take the slot first (keyed by seq%kRing it could, and the game then read D3D_OK).
    static constexpr unsigned kRing=8;
    static_assert(kRing>kMaxFramesAhead+1,"every Present in flight has its own slot");
    struct PresentEntry {std::atomic<std::uint64_t> seq{0};std::atomic<HRESULT> hr{D3D_OK};};
    PresentEntry presentRing[kRing];
    // 0.3.200 (frame skip): StreamFrameSkip. The game thread classifies every frame it records: frameHazards collects why it must be drawn (FrameHazard), and at
    // its Present the bits go to frameRing[ordinal%kRing] (tag = ordinal+1, stored last), then presentsRecorded = ordinal+1 (after the Present is published,
    // before the game waits). The replay thread reads both at a frame's first command (Replayer::beginFrame); a slot is reused only kRing frames later.
    enum FrameHazard:std::uint32_t{kHazardTarget=1,kHazardQuery=2,kHazardCopy=4};   // offscreen render target / depth texture bound; a non-event query issued; a copy from the back buffer or a depth surface
    struct FrameEntry {std::atomic<std::uint64_t> tag{0};std::atomic<std::uint32_t> hazards{0};};
    FrameEntry frameRing[kRing];std::atomic<std::uint64_t> presentsRecorded{0};
    std::uint32_t frameHazards=0;   // game thread: the frame being recorded
    void replayFailureOnFail(HRESULT hr){if(FAILED(hr)){add(q.stats.replayFailures);replayFailure.store(true);}}
    template<class T> T* inner(T* proxy){
        if(!proxy)return nullptr;
        ProxyBase* b=ProxyBase::of(static_cast<IUnknown*>(proxy));
        if(!b){add(q.stats.foreignPointers);return proxy;}   // not ours (a foreign object): pass it through unchanged
        return b->dead.load(std::memory_order_relaxed)||!b->inner?nullptr:static_cast<T*>(b->inner);
    }
};
inline Queue& ProxyBase::streamQueue(){return core->q;}
// 0.3.204 (task 21): game-thread split timers, active only while core.timing (Diagnostics on); off: one bool test. Waits (sync, backpressure, present) are subtracted so they stay in their own counters.
inline std::uint64_t gameWaitsNs(const Counters& s){return get(s.syncNs)+get(s.backpressureNs)+get(s.presentNs);}
struct CallScope {   // 1 in 16 generated method bodies: 16 x (elapsed - waits - snapshot capture) into recordSampledNs
    Counters* st=nullptr;std::uint64_t t0=0,w0=0,clock=0;std::uint16_t id=0;
    CallScope(StreamCore& c,std::uint16_t cmd){if(c.timing&&(++c.timingN&15)==0){st=&c.q.stats;clock=c.clockNs;id=cmd<kMaxCmdIds?cmd:0;w0=gameWaitsNs(*st)+get(st->snapNs);t0=nowNs();}}
    CallScope(const CallScope&)=delete;CallScope& operator=(const CallScope&)=delete;
    ~CallScope(){if(st){const std::uint64_t e0=nowNs()-t0,e=e0>clock?e0-clock:0,w=gameWaitsNs(*st)+get(st->snapNs)-w0,ns=(e>w?e-w:0)*16;own(st->recordSampledNs,ns);own(st->cmdSampledNs[id],ns);own(st->cmdSamples[id]);}}
};
struct LockScope {   // every buffer/image Lock or Unlock: elapsed minus the waits inside into lockNs
    Counters* st=nullptr;std::uint64_t t0=0,w0=0,clock=0;
    explicit LockScope(StreamCore& c){if(c.timing){st=&c.q.stats;clock=c.clockNs;w0=gameWaitsNs(*st);t0=nowNs();}}
    LockScope(const LockScope&)=delete;LockScope& operator=(const LockScope&)=delete;
    ~LockScope(){if(st){const std::uint64_t e0=nowNs()-t0,e=e0>clock?e0-clock:0,w=gameWaitsNs(*st)-w0;own(st->lockNs,e>w?e-w:0);}}
};
inline CallScope ProxyBase::callScope(std::uint16_t id){return CallScope(*core,id);}
#include "stream_proxy_lifetime.inl"
template<Cmd C,class... A> typename MethodTraits<C>::Ret ProxyBase::syncCall(CmdTag<C> t,A... a){return runSync(core->q,t,a...);}

// Runs fn(StreamCore&) on the replay thread after everything recorded so far; returns once it ran. false when it could
// not wait (nested in a pumped wait): fn did not run. fn must not throw.
template<class F> inline bool runTask(StreamCore& c,F&& fn,Cmd label=Cmd::Quiesce){   // label: the census name of this sync call
    if(inPumpedWait){add(c.q.stats.nestedSyncs);return false;}
    using Fn=typename std::remove_reference<F>::type;
    Task t{[](void* a,StreamCore& core){(*static_cast<Fn*>(a))(core);},&fn};
    Task* p=&t;own(c.q.stats.census[(std::size_t)label]);own(c.q.stats.syncCalls);
    std::memcpy(c.q.reserve((std::uint16_t)Cmd::Quiesce,sizeof p,kFlagWaitTarget),&p,sizeof p);c.q.commit();   // 0.3.196 (task 12): flagged, the wait below targets it
    c.q.waitReplayed(c.q.recordedSeq(),WaitKind::Sync);
    return true;
}

inline void countPass(ProxyBase& p,PassReason r){own(p.core->q.stats.passThrough[unsigned(r)]);}   // game thread only
// DISCARD and NOOVERWRITE only mean something in the default pool; elsewhere the old bytes stay, so such a lock is a plain lock.
inline DWORD effectiveLockFlags(DWORD flags,DWORD pool){return pool==D3::kPoolDefault?flags:(flags&~(D3::kLockDiscard|D3::kLockNoOverwrite));}

// ---- the proxies ----
using IidType=typename std::remove_cv<typename std::remove_reference<REFIID>::type>::type;
template<class T> inline T makeProxyIid(){
    if constexpr(std::is_arithmetic<T>::value)return T(0x5ca1ab1e);
    else return T{0x8a1c2e0du,0x4f10,0x4d5e,{0x91,0xa3,0x6c,0x2b,0x77,0x1e,0x5d,0x09}};
}
inline const IidType& proxyIid(){static const IidType iid=makeProxyIid<IidType>();return iid;}
inline ProxyBase* ProxyBase::ofSlow(IUnknown* u){
    if(!u)return nullptr;void* out=nullptr;
    return u->QueryInterface(proxyIid(),&out)==S_OK?static_cast<ProxyBase*>(out):nullptr;
}
inline HRESULT ProxyBase::devGet(IDirect3DDevice9** pp){if(!pp)return D3DERR_INVALIDCALL;*pp=core->game;core->game->AddRef();return D3D_OK;}

#define NL_PROXY_COM(Main,...) \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{ \
        if(!out)return E_POINTER;*out=nullptr; \
        if(id==proxyIid()){*out=static_cast<ProxyBase*>(this);return S_OK;} \
        if(iidIs<Main,##__VA_ARGS__>(id)){*out=static_cast<Main*>(this);comAddRef();return S_OK;} \
        add(core->q.stats.qiMisses);return E_NOINTERFACE;} \
    ULONG STDMETHODCALLTYPE AddRef() override{return comAddRef();} \
    ULONG STDMETHODCALLTYPE Release() override{return comRelease();}
// The local methods every resource shares (the generator classifies them local): device, private data, priority, type.
#define NL_RES_LOCALS(S) \
    HRESULT local(CmdTag<Cmd::S##_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);} \
    HRESULT local(CmdTag<Cmd::S##_SetPrivateData>,REFGUID g,const void* d,DWORD n,DWORD f){return setPrivate(g,d,n,f);} \
    HRESULT local(CmdTag<Cmd::S##_GetPrivateData>,REFGUID g,void* d,DWORD* n){return getPrivate(g,d,n);} \
    HRESULT local(CmdTag<Cmd::S##_FreePrivateData>,REFGUID g){return freePrivate(g);} \
    DWORD local(CmdTag<Cmd::S##_SetPriority>,DWORD v){const DWORD o=priority;priority=v;return o;} \
    DWORD local(CmdTag<Cmd::S##_GetPriority>){return priority;} \
    D3DRESOURCETYPE local(CmdTag<Cmd::S##_GetType>){return (D3DRESOURCETYPE)info.type;}

// ---- concrete proxies ----
// The vtable pointer of each concrete proxy class, learned when the first one is constructed: ProxyBase::of recognizes a stream proxy by it.
inline std::atomic<const void*> proxyShape[13];
template<class D> struct ProxyInit {   // registers a freshly constructed proxy
    static void apply(D* d,IUnknown* u){proxyShape[unsigned(d->kind)].store(*reinterpret_cast<const void* const*>(u),std::memory_order_relaxed);d->unk=u;d->core->reg.addProxy(d);liveProxyObjects.fetch_add(1);}
};

#include "stream_image_locks.inl"
#include "stream_image_proxies.inl"
#include "stream_buffer_locks.inl"

// Bytecode length in DWORDs up to and including the 0x0000FFFF end token (comment blocks skipped by their length).
inline std::size_t shaderTokens(const DWORD* code){
    if(!code)return 0;std::size_t i=1;
    for(;i<(1u<<20);){const DWORD t=code[i];
        if((t&0xFFFF)==0xFFFE){i+=1+((t>>16)&0x7FFF);continue;}
        if(t==0x0000FFFF)return i+1;
        i+=1+((t>>24)&0xF);}
    return 0;
}
struct StreamVertexShader final:IDirect3DVertexShader9,ProxyBase {
    std::vector<DWORD> code;unsigned tags=0;std::uint64_t hash=0;
    explicit StreamVertexShader(StreamCore* c):ProxyBase(c,Kind::VertexShader){ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DVertexShader9*>(this));}
    ~StreamVertexShader(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_VERTEXSHADER_METHODS
    NL_PROXY_COM(IDirect3DVertexShader9)
    HRESULT local(CmdTag<Cmd::VertexShader_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    HRESULT local(CmdTag<Cmd::VertexShader_GetFunction>,void* data,UINT* size){
        if(!size)return D3DERR_INVALIDCALL;const UINT bytes=UINT(code.size()*4);
        if(!data){*size=bytes;return D3D_OK;}if(*size<bytes)return D3DERR_MOREDATA;std::memcpy(data,code.data(),bytes);*size=bytes;return D3D_OK;}
};
struct StreamPixelShader final:IDirect3DPixelShader9,ProxyBase {
    std::vector<DWORD> code;
    explicit StreamPixelShader(StreamCore* c):ProxyBase(c,Kind::PixelShader){ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DPixelShader9*>(this));}
    ~StreamPixelShader(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_PIXELSHADER_METHODS
    NL_PROXY_COM(IDirect3DPixelShader9)
    HRESULT local(CmdTag<Cmd::PixelShader_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    HRESULT local(CmdTag<Cmd::PixelShader_GetFunction>,void* data,UINT* size){
        if(!size)return D3DERR_INVALIDCALL;const UINT bytes=UINT(code.size()*4);
        if(!data){*size=bytes;return D3D_OK;}if(*size<bytes)return D3DERR_MOREDATA;std::memcpy(data,code.data(),bytes);*size=bytes;return D3D_OK;}
};
struct StreamVertexDeclaration final:IDirect3DVertexDeclaration9,ProxyBase {
    std::vector<D3DVERTEXELEMENT9> elements;   // including the end element
    explicit StreamVertexDeclaration(StreamCore* c):ProxyBase(c,Kind::VertexDeclaration){ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DVertexDeclaration9*>(this));}
    ~StreamVertexDeclaration(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_VERTEXDECLARATION_METHODS
    NL_PROXY_COM(IDirect3DVertexDeclaration9)
    HRESULT local(CmdTag<Cmd::VertexDeclaration_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    HRESULT local(CmdTag<Cmd::VertexDeclaration_GetDeclaration>,D3DVERTEXELEMENT9* out,UINT* count){
        if(!count)return D3DERR_INVALIDCALL;if(out)std::memcpy(out,elements.data(),elements.size()*sizeof(D3DVERTEXELEMENT9));*count=UINT(elements.size());return D3D_OK;}
};
struct StreamStateBlock final:IDirect3DStateBlock9,ProxyBase {
    explicit StreamStateBlock(StreamCore* c):ProxyBase(c,Kind::StateBlock){ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DStateBlock9*>(this));}
    ~StreamStateBlock(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_STATEBLOCK_METHODS
    NL_PROXY_COM(IDirect3DStateBlock9)
    HRESULT local(CmdTag<Cmd::StateBlock_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    std::function<void(ProxyBase&,bool)> hook;   // set by the StreamDevice: Apply (true) invalidates StreamState
    void observe(CmdTag<Cmd::StateBlock_Apply>){if(hook)hook(*this,true);}
    using ProxyBase::observe;
};
#include "stream_query_proxy.inl"
struct StreamSwapChain final:IDirect3DSwapChain9,ProxyBase {
    D3DPRESENT_PARAMETERS pp{};UINT index=0;
    explicit StreamSwapChain(StreamCore* c):ProxyBase(c,Kind::SwapChain){ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DSwapChain9*>(this));}
    ~StreamSwapChain(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_SWAPCHAIN_METHODS
    NL_PROXY_COM(IDirect3DSwapChain9)
    HRESULT local(CmdTag<Cmd::SwapChain_GetDevice>,IDirect3DDevice9** out){return devGet(out);}
    HRESULT local(CmdTag<Cmd::SwapChain_GetPresentParameters>,D3DPRESENT_PARAMETERS* out){if(!out)return D3DERR_INVALIDCALL;*out=pp;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Present(const RECT* src,const RECT* dst,HWND window,const RGNDATA* dirty,DWORD flags) override;   // stream_device.h
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT index,D3DBACKBUFFER_TYPE type,IDirect3DSurface9** out) override;
};

// A game-held interface pointer to its proxy without a call or an atomic: compare the object's vtable pointer with the known proxy classes
// (mirror_resources.h does the same for its wrappers) and cast by kind. Anything else (a foreign object) takes the QueryInterface path.
template<class T> inline ProxyBase* ProxyBase::of(T* u){
    if(!u)return nullptr;
    const void* vp=*reinterpret_cast<const void* const*>(u);
    // Set/Get arguments already prove the interface type. Try its one shape
    // before the general classifier; foreign objects retain the private-IID path.
#define NORTHLIGHT_TYPED_PROXY(I,P,K) if constexpr(std::is_same<T,I>::value){if(vp==proxyShape[unsigned(Kind::K)].load(std::memory_order_relaxed))return static_cast<P*>(u);}
    NORTHLIGHT_TYPED_PROXY(IDirect3DSurface9,StreamSurface,Surface)
    NORTHLIGHT_TYPED_PROXY(IDirect3DTexture9,StreamTexture,Texture)
    NORTHLIGHT_TYPED_PROXY(IDirect3DCubeTexture9,StreamCubeTexture,CubeTexture)
    NORTHLIGHT_TYPED_PROXY(IDirect3DVolumeTexture9,StreamVolumeTexture,VolumeTexture)
    NORTHLIGHT_TYPED_PROXY(IDirect3DVolume9,StreamVolume,Volume)
    NORTHLIGHT_TYPED_PROXY(IDirect3DVertexBuffer9,StreamVertexBuffer,VertexBuffer)
    NORTHLIGHT_TYPED_PROXY(IDirect3DIndexBuffer9,StreamIndexBuffer,IndexBuffer)
    NORTHLIGHT_TYPED_PROXY(IDirect3DVertexShader9,StreamVertexShader,VertexShader)
    NORTHLIGHT_TYPED_PROXY(IDirect3DPixelShader9,StreamPixelShader,PixelShader)
    NORTHLIGHT_TYPED_PROXY(IDirect3DVertexDeclaration9,StreamVertexDeclaration,VertexDeclaration)
    NORTHLIGHT_TYPED_PROXY(IDirect3DStateBlock9,StreamStateBlock,StateBlock)
    NORTHLIGHT_TYPED_PROXY(IDirect3DQuery9,StreamQuery,Query)
    NORTHLIGHT_TYPED_PROXY(IDirect3DSwapChain9,StreamSwapChain,SwapChain)
#undef NORTHLIGHT_TYPED_PROXY
    return of(static_cast<IUnknown*>(u));
}
inline ProxyBase* ProxyBase::of(IUnknown* u){
    if(!u)return nullptr;
    const void* vp=*reinterpret_cast<const void* const*>(u);
    for(unsigned k=0;k<13;++k){
        if(vp!=proxyShape[k].load(std::memory_order_relaxed))continue;
        switch(Kind(k)){
        case Kind::Surface:return static_cast<StreamSurface*>(static_cast<IDirect3DSurface9*>(u));
        case Kind::Texture:return static_cast<StreamTexture*>(static_cast<IDirect3DTexture9*>(u));
        case Kind::CubeTexture:return static_cast<StreamCubeTexture*>(static_cast<IDirect3DCubeTexture9*>(u));
        case Kind::VolumeTexture:return static_cast<StreamVolumeTexture*>(static_cast<IDirect3DVolumeTexture9*>(u));
        case Kind::Volume:return static_cast<StreamVolume*>(static_cast<IDirect3DVolume9*>(u));
        case Kind::VertexBuffer:return static_cast<StreamVertexBuffer*>(static_cast<IDirect3DVertexBuffer9*>(u));
        case Kind::IndexBuffer:return static_cast<StreamIndexBuffer*>(static_cast<IDirect3DIndexBuffer9*>(u));
        case Kind::VertexShader:return static_cast<StreamVertexShader*>(static_cast<IDirect3DVertexShader9*>(u));
        case Kind::PixelShader:return static_cast<StreamPixelShader*>(static_cast<IDirect3DPixelShader9*>(u));
        case Kind::VertexDeclaration:return static_cast<StreamVertexDeclaration*>(static_cast<IDirect3DVertexDeclaration9*>(u));
        case Kind::StateBlock:return static_cast<StreamStateBlock*>(static_cast<IDirect3DStateBlock9*>(u));
        case Kind::Query:return static_cast<StreamQuery*>(static_cast<IDirect3DQuery9*>(u));
        case Kind::SwapChain:return static_cast<StreamSwapChain*>(static_cast<IDirect3DSwapChain9*>(u));
        }
    }
    return ofSlow(u);
}
}
