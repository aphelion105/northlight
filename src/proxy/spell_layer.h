#pragma once
// Task 34: the spell layer. Translucent M2 draws (particles, ribbons, spell batches, projected ground effects) are drawn before the effects
// boundary B and so get relight/shadow/AO/fog computed from the background behind them. The layer DEFERS those draws: it captures the exact device
// state of each (a D3DSBT_ALL block), guards the vertex/index bytes it reads, skips the real draw, and replays all of them in order right after the
// effects at B, against the final world depth. Nothing here logs and nothing calls Windows; only IDirect3DDevice9 (the extension device) and the
// resources reached through it. Single thread: the one that executes the device (replay thread with the stream on, else the game thread); no locks.
//
// Wiring (per draw, before B, in this order):
//   facts=DrawFacts{...}; fade.update(...) feeds facts.fadeRun; r=classify(facts,mask);
//   Defer     -> if(layer.defer(args,rt0,ds,r.sub)) return D3D_OK (real draw skipped); else draw in place (layer already flushed what was pending)
//   Hazard    -> layer.hazard(r.reason) then draw in place;  InPlace -> layer.noteInPlace(r.inPlace), draw in place;  others -> draw in place.
// Other events: Clear/StretchRect/... -> hazard(Clear|Copy); texture writes -> onTextureWrite(tex); ProcessVertices dest -> onBufferGpuWrite(buf);
// Buffer::Lock BEFORE forwarding, only when the buffer is marked (markGuard callback) -> onBufferWrite(rawBuffer,offset,size,flags,usage,locks);
// EndScene/Present before B -> hazard(Frame, false); frame start -> beginFrame(pressure); at B after the effects -> replay();
// device Reset/destroy/extension fault -> reset() (drops everything, no draw). Keys are the interface pointers GetStreamSource/GetIndices/GetTexture
// return on the device given to attach() (raw, unwrapped objects); the integration must pass those same pointers to the hooks.
// Include order: this header includes saved_state.h itself (pragma once), which needs the D3D9 API and SUCCEEDED/FAILED in scope.
#ifndef NORTHLIGHT_SPELL_LAYER_TEST_API
#include <d3d9.h>
#endif
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "saved_state.h"

namespace NorthlightSpellLayer {
constexpr unsigned kBlendZero=1,kBlendOne=2,kSrcColor=3,kInvSrcColor=4,kSrcAlpha=5,kInvSrcAlpha=6,kDestAlpha=7,kInvDestAlpha=8,kDestColor=9,kInvDestColor=10,kBlendOpAdd=1; /* D3DBLEND_*, D3DBLENDOP_ADD */
constexpr unsigned kLockReadOnly=0x10,kLockNoOverwrite=0x1000,kLockDiscard=0x2000,kUsageDynamic=0x200; /* D3DLOCK_*, D3DUSAGE_DYNAMIC */
static_assert(unsigned(D3DLOCK_READONLY)==0x10&&unsigned(D3DLOCK_NOOVERWRITE)==0x1000&&unsigned(D3DLOCK_DISCARD)==0x2000&&unsigned(D3DUSAGE_DYNAMIC)==0x200,"lock flag values");
constexpr unsigned kCooldownFrames=30,kMaxStreams=4,kMaxTextures=16,kMaxDecl=65;

// ---- a) classification (pure) ----
enum class Family:uint8_t{None,Particle,ParticleUnlit,Model,Projected}; // M2 vertex shader family, known at shader creation
enum Sub:unsigned{SubAdditive=1,SubAlpha=2,SubModulate=4,SubOther=8};
enum class Kind:uint8_t{Ignore,Opaque,TranslucentZ,Defer,InPlace,Hazard};
enum class InPlaceKind:uint8_t{None,Water,Rain,Name,Fade};
enum class Reason:uint8_t{Clear,Copy,TexWrite,Sample,BufWrite,GpuWrite,Order,Query,Capacity,Capture,Frame,Count};
constexpr unsigned kReasons=unsigned(Reason::Count);
inline const char* reasonName(Reason r){
    static const char* const n[]={"clear","copy","texwrite","sample","bufwrite","gpuwrite","order","query","capacity","capture","frame","none"};
    return n[unsigned(r)<=kReasons?unsigned(r):kReasons];
}
struct DrawFacts{
    bool zEnable=false,zWrite=false,blend=false,separateAlpha=false,stencil=false;
    unsigned srcBlend=kBlendOne,destBlend=kBlendZero,blendOp=kBlendOpAdd,colorWrite=15; // colorWrite: the RGB bits (7) matter
    Family family=Family::None;
    bool skinned=false,water=false,uiTag=false,rain=false,claimed=false,fixedFunction=false,worldDomain=false;
    bool rt0IsTarget=false;               // RT0 is the layer target (or would become it: the first candidate's back-buffer-sized world target)
    bool dsIsWorldDepth=false,mrtBound=false,instanced=false,occlusionActive=false;
    bool samplesRenderTargetTexture=false; // the draw samples the layer target's container texture
    bool fadeRun=false;                    // FadeRun::update() result for this draw
};
struct Result{Kind kind=Kind::Ignore;unsigned sub=0;InPlaceKind inPlace=InPlaceKind::None;Reason reason=Reason::Count;};
inline bool opaqueBlend(const DrawFacts& f){return !f.blend||(f.srcBlend==kBlendOne&&f.destBlend==kBlendZero);}
inline unsigned subClass(const DrawFacts& f){
    if(f.separateAlpha||f.blendOp!=kBlendOpAdd)return SubOther;
    if(f.srcBlend==kDestColor||f.destBlend==kSrcColor)return SubModulate;
    if(f.destBlend==kBlendOne)return SubAdditive;
    if(f.destBlend==kInvSrcAlpha)return SubAlpha;
    return SubOther;
}
// Z-writing draws never defer and stay in place (their depth occludes the replay). Colour-writing no-Z draws into the target are deferred when they
// are one of the four families with a non-opaque blend, kept in place when they are an accepted reorder (water, rain, names, a fade colour pass),
// otherwise a Hazard (unknown order dependency while draws are pending: the caller flushes). Claimed draws and draws into other targets are Ignore
// (unless they sample the target: Hazard/Sample). A non-world draw that writes colour into the target is a Hazard.
inline Result classify(const DrawFacts& f,unsigned classMask){
    Result r;
    auto hazard=[&](Reason why){r.kind=Kind::Hazard;r.reason=why;return r;};
    if(f.samplesRenderTargetTexture)return hazard(Reason::Sample);
    if(!f.rt0IsTarget||f.claimed)return r;
    const bool rgb=(f.colorWrite&7)!=0;
    if(!f.worldDomain)return rgb?hazard(Reason::Order):r;
    if(f.zWrite){r.kind=(opaqueBlend(f)||!rgb)?Kind::Opaque:Kind::TranslucentZ;return r;}
    if(!rgb)return r;
    if(f.water||f.rain||f.uiTag||(f.fadeRun&&f.skinned)){
        r.kind=Kind::InPlace;r.inPlace=f.water?InPlaceKind::Water:f.rain?InPlaceKind::Rain:f.uiTag?InPlaceKind::Name:InPlaceKind::Fade;return r;
    }
    if(f.family==Family::None||f.fixedFunction||opaqueBlend(f)||!f.zEnable||f.stencil||!f.dsIsWorldDepth||f.mrtBound||f.instanced)return hazard(Reason::Order);
    if(f.occlusionActive)return hazard(Reason::Query);
    const unsigned sub=subClass(f);
    if(!(classMask&sub))return hazard(Reason::Order);
    r.kind=Kind::Defer;r.sub=sub;return r;
}
// A skinned Z-writing colour-less draw (the depth pre-pass of a fading actor) starts a run; the next non-skinned draw ends it.
struct FadeRun{
    bool active=false;
    bool update(bool skinned,bool zWrite,unsigned colorWrite){
        if(skinned&&zWrite&&(colorWrite&7)==0)active=true;else if(!skinned)active=false;
        return active;
    }
    void reset(){active=false;}
};

// ---- b) frame state machine and counters (pure) ----
enum class Mode:uint8_t{Off,Armed,Deferring,Fallback,Cooldown};
inline const char* modeName(Mode m){static const char* const n[]={"off","armed","deferring","fallback","cooldown"};return n[unsigned(m)];}
// Fallback: a hazard flushed (or a defer failed) this frame, or B has passed (frame closed): every later candidate is drawn in place. A hazard
// frame is followed by kCooldownFrames Cooldown frames; memory pressure gives Cooldown frames without counting them down.
class FrameState{
    Mode mode_=Mode::Off;unsigned cooldown_=0;bool cooldownNext_=false;
public:
    Mode mode()const{return mode_;}
    unsigned cooldownLeft()const{return cooldown_;}
    bool canDefer()const{return mode_==Mode::Armed||mode_==Mode::Deferring;}
    void begin(bool on,bool pressure){
        if(mode_==Mode::Fallback&&cooldownNext_)cooldown_=kCooldownFrames;
        cooldownNext_=false;
        if(!on)mode_=Mode::Off;
        else if(pressure)mode_=Mode::Cooldown;
        else if(cooldown_){--cooldown_;mode_=Mode::Cooldown;}
        else mode_=Mode::Armed;
    }
    void deferred(){if(mode_==Mode::Armed)mode_=Mode::Deferring;}
    void fallback(bool cooldown){if(canDefer()){mode_=Mode::Fallback;cooldownNext_=cooldown;}}
    void close(){fallback(false);} // B reached: no deferral for the rest of the frame, no cooldown
};
struct Counters{
    unsigned deferred[4]={},inPlace[4]={};        // by sub-class (additive, alpha, modulate, other); by kind (water, rain, name, fade)
    unsigned hazards[kReasons]={};
    unsigned flushes=0,replays=0,replayedDraws=0,replaySkipped=0,discarded=0;
    unsigned conflicts=0,materialized=0,blocksCaptured=0;
    uint64_t conflictBytes=0;
    unsigned arenaHigh=0;                         // bytes
    void add(const Counters& o){
        for(int i=0;i<4;++i){deferred[i]+=o.deferred[i];inPlace[i]+=o.inPlace[i];}
        for(unsigned i=0;i<kReasons;++i)hazards[i]+=o.hazards[i];
        flushes+=o.flushes;replays+=o.replays;replayedDraws+=o.replayedDraws;replaySkipped+=o.replaySkipped;discarded+=o.discarded;
        conflicts+=o.conflicts;materialized+=o.materialized;blocksCaptured+=o.blocksCaptured;conflictBytes+=o.conflictBytes;
        arenaHigh=std::max(arenaHigh,o.arenaHigh);
    }
    unsigned totalDeferred()const{return deferred[0]+deferred[1]+deferred[2]+deferred[3];}
    unsigned totalHazards()const{unsigned n=0;for(unsigned h:hazards)n+=h;return n;}
};

// ---- c) guard bookkeeping (pure) ----
// Per raw buffer a list of [lo,hi) byte ranges and the entry (and stream slot, 0xFF = index buffer) owning each. A write conflicts when it is not
// READONLY and is DISCARD, size 0 (whole), or overlaps a live range; NOOVERWRITE outside every range is free. A bounding box per buffer rejects the
// ring-buffer append (the common write) in O(1); only a write inside the box scans the ranges. Slots are reused across frames (no allocation once warm).
class GuardTable{
public:
    struct Range{uint32_t lo,hi,entry;uint8_t slot;bool live;};
    struct Hit{uint32_t buf,range;};
    struct Buf{void* key=nullptr;bool index=false;unsigned live=0;uint32_t boxLo=0,boxHi=0;std::vector<Range> ranges;};
private:
    std::vector<Buf> bufs_;size_t used_=0;
    static bool overlaps(const Range& r,uint64_t lo,uint64_t hi){return r.live&&r.lo<hi&&lo<r.hi;}
public:
    const Buf* find(void* key)const{for(size_t i=0;i<used_;++i)if(bufs_[i].key==key)return &bufs_[i];return nullptr;}
    int findIndex(void* key)const{for(size_t i=0;i<used_;++i)if(bufs_[i].key==key)return int(i);return -1;}
    bool guarded(void* key)const{const Buf* b=find(key);return b&&b->live;}
    const Buf& buf(uint32_t i)const{return bufs_[i];}
    size_t buffers()const{return used_;}
    // True when this is the buffer's first live guard (the caller marks the buffer).
    bool add(void* key,bool index,uint32_t lo,uint32_t hi,uint32_t entry,uint8_t slot){
        if(lo>=hi)return false;
        int i=findIndex(key);
        if(i<0){
            if(used_==bufs_.size())bufs_.emplace_back();
            i=int(used_++);Buf& b=bufs_[size_t(i)];b.key=key;b.index=index;b.live=0;b.boxLo=lo;b.boxHi=hi;b.ranges.clear();
        }
        Buf& b=bufs_[size_t(i)];
        const bool first=b.live==0;
        if(first){b.boxLo=lo;b.boxHi=hi;}else{b.boxLo=std::min(b.boxLo,lo);b.boxHi=std::max(b.boxHi,hi);}
        b.ranges.push_back(Range{lo,hi,entry,slot,true});++b.live;
        return first;
    }
    static bool writeConflict(const Buf& b,uint32_t offset,uint32_t size,uint32_t flags){
        if(!b.live||(flags&kLockReadOnly))return false;
        if((flags&kLockDiscard)||!size)return true;
        const uint64_t lo=offset,hi=lo+size;
        if(lo>=b.boxHi||hi<=b.boxLo)return false;
        for(const Range& r:b.ranges)if(overlaps(r,lo,hi))return true;
        return false;
    }
    bool conflicts(void* key,uint32_t offset,uint32_t size,uint32_t flags)const{const Buf* b=find(key);return b&&writeConflict(*b,offset,size,flags);}
    // Appends the live ranges the write conflicts with; returns how many.
    size_t collect(void* key,uint32_t offset,uint32_t size,uint32_t flags,std::vector<Hit>& out)const{
        const int i=findIndex(key);if(i<0||!writeConflict(bufs_[size_t(i)],offset,size,flags))return 0;
        const Buf& b=bufs_[size_t(i)];const bool all=(flags&kLockDiscard)||!size;const uint64_t lo=offset,hi=lo+size;size_t n=0;
        for(size_t k=0;k<b.ranges.size();++k)if(b.ranges[k].live&&(all||overlaps(b.ranges[k],lo,hi))){out.push_back(Hit{uint32_t(i),uint32_t(k)});++n;}
        return n;
    }
    // Drops one range; true when the buffer has no live guard left (the caller unmarks it).
    bool drop(uint32_t buf,uint32_t range){
        Buf& b=bufs_[buf];Range& r=b.ranges[range];
        if(r.live){r.live=false;--b.live;}
        return b.live==0;
    }
    const Range& range(const Hit& h)const{return bufs_[h.buf].ranges[h.range];}
    // Calls unmark(key) for every buffer that still has a live guard, then forgets all ranges (capacity kept).
    template<class F> void clear(F&& unmark){
        for(size_t i=0;i<used_;++i){Buf& b=bufs_[i];if(b.live)unmark(b.key);b.key=nullptr;b.live=0;b.ranges.clear();}
        used_=0;
    }
};

// ---- d) the layer (execution) ----
enum class DrawType:uint8_t{Primitive,Indexed,PrimitiveUP,IndexedUP};
inline uint32_t vertexCount(unsigned prim,uint32_t primCount){ // D3DPT_* vertex count of primCount primitives
    switch(prim){case 1:return primCount;case 2:return primCount*2;case 3:return primCount+1;case 4:return primCount*3;case 5:case 6:return primCount+2;default:return 0;}
}
// The arguments of the draw call, as the game made it. `first`: StartVertex (Primitive) or StartIndex (Indexed). UP data pointers are only read
// during defer(). Indexed: baseVertex/minIndex/numVertices as in DrawIndexedPrimitive; IndexedUP: minIndex/numVertices/indexData/indexFormat/vertexData.
struct DrawArgs{
    DrawType type=DrawType::Primitive;D3DPRIMITIVETYPE prim=D3DPT_TRIANGLELIST;
    INT baseVertex=0;UINT minIndex=0,numVertices=0,first=0,primCount=0;
    const void* vertexData=nullptr;UINT stride=0;const void* indexData=nullptr;D3DFORMAT indexFormat=D3DFMT_INDEX16;
    static DrawArgs primitive(D3DPRIMITIVETYPE p,UINT start,UINT count){DrawArgs a;a.type=DrawType::Primitive;a.prim=p;a.first=start;a.primCount=count;return a;}
    static DrawArgs indexed(D3DPRIMITIVETYPE p,INT base,UINT minIdx,UINT num,UINT startIndex,UINT count){
        DrawArgs a;a.type=DrawType::Indexed;a.prim=p;a.baseVertex=base;a.minIndex=minIdx;a.numVertices=num;a.first=startIndex;a.primCount=count;return a;
    }
    static DrawArgs primitiveUP(D3DPRIMITIVETYPE p,UINT count,const void* data,UINT stride){
        DrawArgs a;a.type=DrawType::PrimitiveUP;a.prim=p;a.primCount=count;a.vertexData=data;a.stride=stride;return a;
    }
    static DrawArgs indexedUP(D3DPRIMITIVETYPE p,UINT minIdx,UINT num,UINT count,const void* idx,D3DFORMAT fmt,const void* data,UINT stride){
        DrawArgs a;a.type=DrawType::IndexedUP;a.prim=p;a.minIndex=minIdx;a.numVertices=num;a.primCount=count;a.indexData=idx;a.indexFormat=fmt;a.vertexData=data;a.stride=stride;return a;
    }
};
struct Config{
    bool on=false;unsigned classMask=SubAdditive|SubAlpha|SubModulate|SubOther;
    unsigned maxEntries=512,maxBlocks=512;size_t arenaBytes=4u<<20,uploadVbBytes=1u<<20,uploadIbBytes=256u<<10;
    bool conflictCopy=true;                       // false on a non-DXVK backend: a conflicting write flushes instead of copying
    DWORD readBackLockFlags=D3DLOCK_READONLY;     // NorthlightUpload::readBackLock()
    void (*markGuard)(void* ctx,void* rawBuffer,bool guarded)=nullptr;void* markCtx=nullptr; // sets/clears the per-buffer "call onBufferWrite" flag
    NorthlightStateBlockPool* savedPool=nullptr;  // the renderer's SavedState pool for the replay scope (optional)
};

class Layer{
    struct Stream{IDirect3DVertexBuffer9* vb=nullptr;uint32_t off=0,stride=0,lo=0,hi=0,sno=0;int32_t blob=-1;};
    struct Entry{
        DrawType type=DrawType::Primitive;D3DPRIMITIVETYPE prim=D3DPT_TRIANGLELIST;
        INT base=0;UINT minIndex=0,numVertices=0,first=0,primCount=0;
        IDirect3DStateBlock9* block=nullptr;
        uint8_t nStreams=0,nTex=0,isz=0;Stream streams[kMaxStreams];
        IDirect3DIndexBuffer9* ib=nullptr;uint32_t ibLo=0,ibHi=0;int32_t ibBlob=-1;
        uint32_t upVtx=0,upStride=0,upIdx=0;D3DFORMAT upFmt=D3DFMT_INDEX16;
        IDirect3DBaseTexture9* tex[kMaxTextures]={};
    };
    struct Blob{uint32_t arenaOff,len,base,uploadOff;bool index;}; // conflict copy of one merged byte span of a buffer; entries share it
    struct Seg{uint32_t lo,hi;size_t firstHit,lastHit;};
    IDirect3DDevice9* dev_=nullptr;Config cfg_;FrameState fm_;
    std::vector<Entry> entries_;unsigned count_=0;
    std::vector<IDirect3DStateBlock9*> pool_;
    std::vector<uint8_t> arena_;uint32_t arenaUsed_=0;
    std::vector<Blob> blobs_;
    GuardTable guards_;std::vector<GuardTable::Hit> hits_;std::vector<Seg> segs_;std::vector<uint32_t> order_;
    std::vector<void*> samples_;
    IDirect3DVertexBuffer9* upVb_=nullptr;IDirect3DIndexBuffer9* upIb_=nullptr;uint32_t vbUsed_=0,ibUsed_=0;
    IDirect3DSurface9* target_=nullptr;IDirect3DSurface9* ds_=nullptr;
    Counters frame_,last_,total_;bool firstHazard_=false,firstTaken_=false;Reason firstReason_=Reason::Count;

    template<class F> void count(F&& f){f(frame_);f(total_);}
    static uint32_t align16(uint32_t n){return (n+15u)&~15u;}
    void mark(void* key,bool on){if(cfg_.markGuard)cfg_.markGuard(cfg_.markCtx,key,on);}
    void releaseEntry(Entry& e){
        for(unsigned i=0;i<e.nStreams;++i)if(e.streams[i].vb)e.streams[i].vb->Release();
        if(e.ib)e.ib->Release();
        for(unsigned i=0;i<e.nTex;++i)if(e.tex[i])e.tex[i]->Release();
        e=Entry{};
    }
    void clearFrame(){
        guards_.clear([&](void* key){mark(key,false);}); // before the entries drop their references: the marked buffers are still alive
        for(unsigned i=0;i<count_;++i)releaseEntry(entries_[i]);
        count_=0;samples_.clear();blobs_.clear();hits_.clear();
        frame_.arenaHigh=std::max(frame_.arenaHigh,arenaUsed_);total_.arenaHigh=std::max(total_.arenaHigh,arenaUsed_);
        arenaUsed_=vbUsed_=ibUsed_=0;
        if(target_){target_->Release();target_=nullptr;}
        if(ds_){ds_->Release();ds_=nullptr;}
    }
    void releaseUploads(){
        if(upVb_){upVb_->Release();upVb_=nullptr;}
        if(upIb_){upIb_->Release();upIb_=nullptr;}
    }
    void releasePool(size_t keep){
        while(pool_.size()>keep){if(pool_.back())pool_.back()->Release();pool_.pop_back();}
    }
    void noteHazard(Reason r){
        count([&](Counters& c){++c.hazards[unsigned(r)];});
        if(!firstHazard_){firstHazard_=true;firstReason_=r;}
    }
    // Every failure of defer() lands here: flush what is pending (in order, before the caller's in-place draw), then the frame falls back.
    bool fail(Reason r){
        noteHazard(r);
        if(count_){count([](Counters& c){++c.flushes;});drain();}
        fm_.fallback(r!=Reason::Frame);
        return false;
    }
    bool abortEntry(Entry& e,Reason r){releaseEntry(e);return fail(r);}
    void drain(){runReplay();clearFrame();}

    bool upload(bool index){
        void* p=nullptr;
        if(index){if(!upIb_||FAILED(upIb_->Lock(0,0,&p,D3DLOCK_DISCARD))||!p)return false;}
        else if(!upVb_||FAILED(upVb_->Lock(0,0,&p,D3DLOCK_DISCARD))||!p)return false;
        for(const Blob& b:blobs_)if(b.index==index)std::memcpy(static_cast<uint8_t*>(p)+b.uploadOff,arena_.data()+b.arenaOff,b.len);
        if(index)upIb_->Unlock();else upVb_->Unlock();
        return true;
    }
    // One entry: the block restores the game's state of that draw (it does not carry RT/DS: set before), then the materialized streams are rebound.
    // Fetch rule: vertex i of a stream reads offset+(base+i)*stride. Materialized stream bytes [lo,hi) now sit at P in the upload buffer. With f=base+min
    // (Indexed) or StartVertex (Primitive): mode A keeps base and puts offset P-f*stride (needs >=0); mode B uses base=-min (StartVertex 0), offset P,
    // and moves the other streams to offset+f*stride (always >=0). Index values are never rewritten.
    bool drawEntry(Entry& e,bool vbOk,bool ibOk){
        const bool idx=e.type==DrawType::Indexed;
        bool matV=false;
        for(unsigned i=0;i<e.nStreams;++i)if(e.streams[i].blob>=0)matV=true;
        if((matV&&!vbOk)||(e.ibBlob>=0&&!ibOk))return false;
        if(FAILED(e.block->Apply()))return false;
        INT base=e.base;UINT start=e.first;
        if(matV){
            const int64_t f=idx?int64_t(e.base)+e.minIndex:int64_t(e.first);
            bool modeA=true;
            for(unsigned i=0;i<e.nStreams;++i){
                const Stream& s=e.streams[i];if(s.blob<0)continue;
                const Blob& b=blobs_[size_t(s.blob)];
                if(int64_t(b.uploadOff)+(int64_t(s.lo)-b.base)<f*s.stride)modeA=false;
            }
            for(unsigned i=0;i<e.nStreams;++i){
                const Stream& s=e.streams[i];
                if(s.blob>=0){
                    const Blob& b=blobs_[size_t(s.blob)];const int64_t p=int64_t(b.uploadOff)+(int64_t(s.lo)-b.base);
                    dev_->SetStreamSource(s.sno,upVb_,UINT(modeA?p-f*s.stride:p),s.stride);
                }else if(!modeA)dev_->SetStreamSource(s.sno,s.vb,UINT(int64_t(s.off)+f*s.stride),s.stride);
            }
            if(!modeA){if(idx)base=-INT(e.minIndex);else start=0;}
        }
        if(e.ibBlob>=0){
            const Blob& b=blobs_[size_t(e.ibBlob)];
            dev_->SetIndices(upIb_);
            start=UINT((int64_t(b.uploadOff)+(int64_t(e.ibLo)-b.base))/e.isz);
        }
        switch(e.type){
        case DrawType::Primitive:dev_->DrawPrimitive(e.prim,start,e.primCount);break;
        case DrawType::Indexed:dev_->DrawIndexedPrimitive(e.prim,base,e.minIndex,e.numVertices,start,e.primCount);break;
        case DrawType::PrimitiveUP:dev_->DrawPrimitiveUP(e.prim,e.primCount,arena_.data()+e.upVtx,e.upStride);break;
        case DrawType::IndexedUP:
            dev_->DrawIndexedPrimitiveUP(e.prim,e.minIndex,e.numVertices,e.primCount,arena_.data()+e.upIdx,e.upFmt,
                reinterpret_cast<const void*>(uintptr_t(arena_.data()+e.upVtx)-uintptr_t(e.minIndex)*e.upStride),e.upStride);break; // only [min,min+num) is read
        }
        return true;
    }
    void runReplay(){
        if(!count_)return;
        SavedState scope(dev_,cfg_.savedPool);
        if(!scope.ok){count([&](Counters& c){c.replaySkipped+=count_;});return;}
        const bool vbOk=!vbUsed_||upload(false),ibOk=!ibUsed_||upload(true);
        dev_->SetDepthStencilSurface(nullptr);
        for(DWORD i=1;i<4;++i)dev_->SetRenderTarget(i,nullptr);
        const bool bound=SUCCEEDED(dev_->SetRenderTarget(0,target_));
        dev_->SetDepthStencilSurface(ds_);
        for(unsigned i=0;i<count_;++i){
            const bool ok=bound&&drawEntry(entries_[i],vbOk,ibOk);
            count([&](Counters& c){if(ok)++c.replayedDraws;else ++c.replaySkipped;});
        }
    }
    IDirect3DStateBlock9* acquireBlock(){
        IDirect3DStateBlock9* blk=nullptr;
        if(count_<pool_.size()){
            blk=pool_[count_];
            if(FAILED(blk->Capture())){blk->Release();pool_.erase(pool_.begin()+count_);return nullptr;}
        }else{
            if(pool_.size()>=cfg_.maxBlocks)return nullptr;
            if(FAILED(dev_->CreateStateBlock(D3DSBT_ALL,&blk))||!blk){if(blk)blk->Release();return nullptr;} // Create captures the current state itself
            pool_.push_back(blk);
        }
        count([](Counters& c){++c.blocksCaptured;});
        return blk;
    }
    bool ensureUpload(bool index){
        if(index){
            if(!upIb_&&(FAILED(dev_->CreateIndexBuffer(UINT(cfg_.uploadIbBytes),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&upIb_,nullptr))||!upIb_)){upIb_=nullptr;return false;}
        }else if(!upVb_&&(FAILED(dev_->CreateVertexBuffer(UINT(cfg_.uploadVbBytes),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&upVb_,nullptr))||!upVb_)){upVb_=nullptr;return false;}
        return true;
    }
    // Copy-on-conflict of the live ranges in hits_ (all of one buffer): merge them into byte spans, read each span once from the raw buffer into the
    // arena and give every affected entry stream/index the span's blob. False: a hazard was raised (everything pending was flushed first).
    bool copyOut(void* key){
        const GuardTable::Buf& b=guards_.buf(hits_[0].buf);const bool index=b.index;
        order_.clear();for(uint32_t i=0;i<hits_.size();++i)order_.push_back(i);
        std::sort(order_.begin(),order_.end(),[&](uint32_t x,uint32_t y){return guards_.range(hits_[x]).lo<guards_.range(hits_[y]).lo;});
        segs_.clear();
        for(size_t k=0;k<order_.size();++k){
            const GuardTable::Range& r=guards_.range(hits_[order_[k]]);
            if(!segs_.empty()&&r.lo<=segs_.back().hi){segs_.back().hi=std::max(segs_.back().hi,r.hi);segs_.back().lastHit=k;}
            else segs_.push_back(Seg{r.lo,r.hi,k,k});
        }
        uint64_t arena=arenaUsed_,up=index?ibUsed_:vbUsed_;
        for(const Seg& s:segs_){arena+=align16(s.hi-s.lo);up+=align16(s.hi-s.lo);}
        if(arena>arena_.size()||up>(index?cfg_.uploadIbBytes:cfg_.uploadVbBytes)||blobs_.size()+segs_.size()>size_t(cfg_.maxEntries)*(kMaxStreams+1)){hazard(Reason::Capacity);return false;}
        if(!ensureUpload(index)){hazard(Reason::Capture);return false;}
        count([](Counters& c){++c.conflicts;});
        for(const Seg& s:segs_){
            const uint32_t len=s.hi-s.lo;void* p=nullptr;
            HRESULT hr=index?static_cast<IDirect3DIndexBuffer9*>(key)->Lock(s.lo,len,&p,cfg_.readBackLockFlags):static_cast<IDirect3DVertexBuffer9*>(key)->Lock(s.lo,len,&p,cfg_.readBackLockFlags);
            if(FAILED(hr)||!p){ // a failed lock leaves nothing to unlock; a success with no pointer is unlocked again
                if(SUCCEEDED(hr)){if(index)static_cast<IDirect3DIndexBuffer9*>(key)->Unlock();else static_cast<IDirect3DVertexBuffer9*>(key)->Unlock();}
                hazard(Reason::BufWrite);return false;
            }
            std::memcpy(arena_.data()+arenaUsed_,p,len);
            if(index)static_cast<IDirect3DIndexBuffer9*>(key)->Unlock();else static_cast<IDirect3DVertexBuffer9*>(key)->Unlock();
            uint32_t& upUsed=index?ibUsed_:vbUsed_;
            blobs_.push_back(Blob{arenaUsed_,len,s.lo,upUsed,index});
            const int32_t id=int32_t(blobs_.size()-1);
            arenaUsed_+=align16(len);upUsed+=align16(len);
            count([&](Counters& c){c.conflictBytes+=len;});
            for(size_t k=s.firstHit;k<=s.lastHit;++k){
                const GuardTable::Hit h=hits_[order_[k]];const GuardTable::Range r=guards_.range(h);Entry& e=entries_[r.entry];
                if(r.slot==0xFF)e.ibBlob=id;else e.streams[r.slot].blob=id;
                count([](Counters& c){++c.materialized;});
                if(guards_.drop(h.buf,h.range))mark(key,false);
            }
        }
        return true;
    }
public:
    Layer()=default;
    Layer(const Layer&)=delete;Layer& operator=(const Layer&)=delete;
    ~Layer(){reset();}

    // Not owned; the device must outlive the layer or reset()/attach(nullptr) runs first. Drops everything pending without drawing.
    void attach(IDirect3DDevice9* dev){reset();dev_=dev;}
    // Only while nothing is pending (else pending entries are dropped). Allocates the entry table and the arena once.
    void configure(const Config& c){
        reset();cfg_=c;
        if(cfg_.on){entries_.resize(cfg_.maxEntries);arena_.resize(cfg_.arenaBytes);pool_.reserve(cfg_.maxBlocks);hits_.reserve(cfg_.maxEntries);order_.reserve(cfg_.maxEntries);
            segs_.reserve(cfg_.maxEntries);blobs_.reserve(size_t(cfg_.maxEntries)*(kMaxStreams+1));samples_.reserve(64);}
        else{entries_=std::vector<Entry>();arena_=std::vector<uint8_t>();}
        fm_.begin(cfg_.on,false);
    }
    const Config& config()const{return cfg_;}

    // Frame boundary. Entries still pending (no B this frame, no hazard(Frame)) are dropped WITHOUT drawing and counted as discarded.
    // Memory pressure: no deferral this frame and the state-block pool is released.
    void beginFrame(bool memoryPressure){
        if(count_){count([&](Counters& c){c.discarded+=count_;});clearFrame();fm_.fallback(false);}
        last_=frame_;frame_=Counters{};
        fm_.begin(cfg_.on,memoryPressure);
        if(!cfg_.on||memoryPressure)trim(0);
    }
    bool armed()const{return cfg_.on&&dev_&&fm_.canDefer();}
    Mode mode()const{return fm_.mode();}
    unsigned cooldownLeft()const{return fm_.cooldownLeft();}
    unsigned pending()const{return count_;}
    IDirect3DSurface9* target()const{return target_;}     // the layer target T (RT0 of the first pending entry), null when nothing is pending
    IDirect3DSurface9* depth()const{return ds_;}
    const Counters& frameCounters()const{return frame_;}
    const Counters& lastFrameCounters()const{return last_;}
    const Counters& totalCounters()const{return total_;}
    size_t poolSize()const{return pool_.size();}
    uint32_t arenaUsed()const{return arenaUsed_;}
    // True once per session, after the first hazard: its reason, for a one-shot log line.
    bool takeFirstHazard(Reason& r){if(!firstHazard_||firstTaken_)return false;firstTaken_=true;r=firstReason_;return true;}
    void noteInPlace(InPlaceKind k){if(k!=InPlaceKind::None)count([&](Counters& c){++c.inPlace[unsigned(k)-1];});}

    // For a draw classify() returned Defer for. true: deferred, the caller skips the real draw and returns D3D_OK (the side effects of a skipped UP
    // call on the device are already emulated). false: nothing was queued and the device is as before except possibly a flush: when pending
    // entries existed and the defer failed (Capacity/Capture/Order) they were all replayed now (SavedState scope, state restored) and the frame is
    // Fallback, so the caller draws this draw in place AFTER them (order kept). When the layer is off, not armed (fallback/cooldown/pressure/after
    // B) or the draw is empty it returns false without any device call. A later Defer draw whose RT0/DS differs from the first entry's is a hazard
    // (Order); the integration normally keeps those out via DrawFacts::rt0IsTarget.
    bool defer(const DrawArgs& a,IDirect3DSurface9* rt0,IDirect3DSurface9* ds,unsigned sub=SubOther){
        if(!cfg_.on||!dev_||!fm_.canDefer())return false;
        const uint32_t vcount=vertexCount(a.prim,a.primCount);
        if(!vcount)return false;
        if(count_>=cfg_.maxEntries)return fail(Reason::Capacity);
        if(!rt0||(count_&&(rt0!=target_||ds!=ds_)))return fail(count_?Reason::Order:Reason::Capture);
        const bool up=a.type==DrawType::PrimitiveUP||a.type==DrawType::IndexedUP,idx=a.type==DrawType::Indexed||a.type==DrawType::IndexedUP;
        Entry& e=entries_[count_];e=Entry{};
        e.type=a.type;e.prim=a.prim;e.base=a.baseVertex;e.minIndex=a.minIndex;e.numVertices=a.numVertices;e.first=a.first;e.primCount=a.primCount;
        uint32_t used=0;
        if(!up){
            IDirect3DVertexDeclaration9* decl=nullptr;dev_->GetVertexDeclaration(&decl);
            if(decl){
                D3DVERTEXELEMENT9 el[kMaxDecl];UINT n=0;bool ok=SUCCEEDED(decl->GetDeclaration(nullptr,&n))&&n<=kMaxDecl&&SUCCEEDED(decl->GetDeclaration(el,&n));
                decl->Release();
                if(!ok)return abortEntry(e,Reason::Capture);
                for(UINT i=0;i<n&&el[i].Stream!=0xFF;++i)if(el[i].Stream<16)used|=1u<<el[i].Stream;
            }else used=1; // FVF: stream 0
            const int64_t first=idx?int64_t(a.baseVertex)+a.minIndex:int64_t(a.first);
            const uint32_t verts=idx?a.numVertices:vcount;
            for(uint32_t s=0;s<16;++s){
                if(!(used>>s&1))continue;
                if(e.nStreams==kMaxStreams)return abortEntry(e,Reason::Capture);
                IDirect3DVertexBuffer9* vb=nullptr;UINT off=0,stride=0,freq=1;
                const bool got=SUCCEEDED(dev_->GetStreamSource(s,&vb,&off,&stride))&&vb;
                if(!got){if(vb)vb->Release();return abortEntry(e,Reason::Capture);}
                Stream& st=e.streams[e.nStreams++];st.vb=vb;st.off=off;st.stride=stride;st.sno=s;
                if(FAILED(dev_->GetStreamSourceFreq(s,&freq))||freq!=1)return abortEntry(e,Reason::Capture);
                const int64_t lo=int64_t(off)+first*stride,hi=lo+int64_t(verts)*stride;
                if(lo<0||hi>int64_t(0xFFFFFFFFu))return abortEntry(e,Reason::Capture);
                st.lo=uint32_t(lo);st.hi=uint32_t(hi);
            }
            if(a.type==DrawType::Indexed){
                D3DINDEXBUFFER_DESC d{};
                if(FAILED(dev_->GetIndices(&e.ib))||!e.ib)return abortEntry(e,Reason::Capture);
                if(FAILED(e.ib->GetDesc(&d))||(d.Format!=D3DFMT_INDEX16&&d.Format!=D3DFMT_INDEX32))return abortEntry(e,Reason::Capture);
                e.isz=d.Format==D3DFMT_INDEX16?2:4;
                const uint64_t lo=uint64_t(a.first)*e.isz,hi=lo+uint64_t(vcount)*e.isz;
                if(hi>0xFFFFFFFFu)return abortEntry(e,Reason::Capture);
                e.ibLo=uint32_t(lo);e.ibHi=uint32_t(hi);
            }
        }
        uint32_t upV=0,upI=0,vOff=0;
        if(up){
            if(!a.vertexData||!a.stride||(idx&&!a.indexData))return abortEntry(e,Reason::Capture);
            e.upStride=a.stride;
            if(idx){
                if(a.indexFormat!=D3DFMT_INDEX16&&a.indexFormat!=D3DFMT_INDEX32)return abortEntry(e,Reason::Capture);
                e.isz=a.indexFormat==D3DFMT_INDEX16?2:4;e.upFmt=a.indexFormat;
                vOff=a.minIndex*a.stride;upV=a.numVertices*a.stride;upI=vcount*e.isz;
            }else upV=vcount*a.stride;
            if(uint64_t(arenaUsed_)+align16(upV)+align16(upI)>arena_.size())return abortEntry(e,Reason::Capacity);
        }
        if(count_>=pool_.size()&&pool_.size()>=cfg_.maxBlocks)return abortEntry(e,Reason::Capacity);
        for(UINT t=0;t<kMaxTextures;++t){
            IDirect3DBaseTexture9* tx=nullptr;dev_->GetTexture(t,&tx);
            if(tx)e.tex[e.nTex++]=tx;
        }
        e.block=acquireBlock();
        if(!e.block)return abortEntry(e,Reason::Capture);
        // Committed from here: nothing below fails.
        if(up){
            std::memcpy(arena_.data()+arenaUsed_,static_cast<const uint8_t*>(a.vertexData)+vOff,upV);e.upVtx=arenaUsed_;arenaUsed_+=align16(upV);
            if(idx){std::memcpy(arena_.data()+arenaUsed_,a.indexData,upI);e.upIdx=arenaUsed_;arenaUsed_+=align16(upI);}
        }
        for(unsigned i=0;i<e.nStreams;++i){
            Stream& s=e.streams[i];
            if(guards_.add(static_cast<void*>(s.vb),false,s.lo,s.hi,count_,uint8_t(i)))mark(static_cast<void*>(s.vb),true);
        }
        if(e.ib&&guards_.add(static_cast<void*>(e.ib),true,e.ibLo,e.ibHi,count_,0xFF))mark(static_cast<void*>(e.ib),true);
        for(unsigned i=0;i<e.nTex;++i){
            void* k=static_cast<void*>(e.tex[i]);
            if(std::find(samples_.begin(),samples_.end(),k)==samples_.end())samples_.push_back(k);
        }
        if(!count_){target_=rt0;rt0->AddRef();ds_=ds;if(ds)ds->AddRef();}
        ++count_;fm_.deferred();
        count([&](Counters& c){unsigned i=0;while(i<3&&!(sub>>i&1))++i;++c.deferred[i];});
        if(a.type==DrawType::PrimitiveUP||a.type==DrawType::IndexedUP){ // the API side effects of the skipped call
            dev_->SetStreamSource(0,nullptr,0,0);
            if(a.type==DrawType::IndexedUP)dev_->SetIndices(nullptr);
        }
        return true;
    }

    // An unknown order/resource dependency (clear, copy, order, query, frame...). With entries pending: replay them now in order (own SavedState
    // scope), count the reason, and the frame becomes Fallback (a cooldown follows unless `cooldown` is false). Nothing pending: no effect.
    void hazard(Reason r,bool cooldown=true){
        if(!count_)return;
        noteHazard(r);count([](Counters& c){++c.flushes;});
        drain();fm_.fallback(cooldown);
    }
    // Buffer::Lock hook, called BEFORE the lock is forwarded, only for buffers the markGuard callback marked. READONLY and non-conflicting writes
    // return untouched. A conflicting write to a DYNAMIC buffer without outstanding locks (conflictCopy allowed) first copies the guarded bytes
    // out (the affected entries then replay from a Northlight-owned buffer); anything else, or a failed/null read-back lock, is a BufWrite hazard
    // (flush first, so the pending draws are issued before the write lands). Never leaves a guard on bytes that changed.
    void onBufferWrite(void* rawBuffer,uint32_t offset,uint32_t size,DWORD flags,DWORD usage,unsigned outstandingLocks){
        if(!count_||(flags&D3DLOCK_READONLY))return;
        hits_.clear();
        if(!guards_.collect(rawBuffer,offset,size,flags,hits_))return;
        if(!cfg_.conflictCopy||!(usage&D3DUSAGE_DYNAMIC)||outstandingLocks){hazard(Reason::BufWrite);return;}
        copyOut(rawBuffer);
    }
    void onBufferGpuWrite(void* rawBuffer){if(count_&&guards_.guarded(rawBuffer))hazard(Reason::GpuWrite);} // ProcessVertices destination
    bool pendingSamples(void* rawTexture)const{return count_&&std::find(samples_.begin(),samples_.end(),rawTexture)!=samples_.end();}
    void onTextureWrite(void* rawTexture){if(pendingSamples(rawTexture))hazard(Reason::TexWrite);}

    // At B, after the effects: draws all pending entries in their original order into T/DS and restores the device state (SavedState). Idempotent;
    // closes the frame (no more deferral until beginFrame, no cooldown). Entries whose upload could not be made are skipped and counted.
    void replay(){
        fm_.close();
        if(!count_)return;
        count([](Counters& c){++c.replays;});
        drain();
    }
    // Releases pooled state blocks (and the upload buffers) beyond `keep`; never below what is pending. For idle frames and memory pressure.
    void trim(size_t keep=0){
        releasePool(std::max<size_t>(keep,count_));
        if(!count_&&!keep)releaseUploads();
    }
    // Device Reset/destroy/extension fault: drops everything WITHOUT drawing, releases blocks, buffers and surfaces, unmarks guards.
    void reset(){
        clearFrame();releasePool(0);releaseUploads();fm_.close();
    }
};
}
