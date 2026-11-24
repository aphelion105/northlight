// VB/IB command payloads, CPU shadows, and sequence-owned slices.
struct UnlockBufferArgs {ProxyBase* proxy;UINT off,size,flags,inlineData;};
// 0.3.204 (task 21): a zero-copy unlock: the replay thread reads the bytes from the game-side slice itself (src = the slice base; the range starts at src+off). The slice is kept alive until this command retires (see BufferState::sliceSeq).
struct UnlockBufferRefArgs {ProxyBase* proxy;const unsigned char* src;UINT off,size,flags,firstSize;};

// ---- buffer locks ----
// 0.3.192 (CS): kMaxStaticShadow: a NON-DYNAMIC buffer up to this size may hold a shadow (a larger one stays pass-through).
constexpr std::size_t kMaxStaticShadow=std::size_t(4)<<20;
struct BufferState {
    SliceMem shadow;bool shadowOn=false,written=false,whole=false;
    BufferRangeShadow range;void* passData=nullptr;
    // lastUse: StreamCore::frameNo of its last shadow lock. lru: StreamCore::bufRegular / bufLarge, least recently locked first, so lastUse never decreases from the head
    // (an eviction scan stops at the first entry locked in the current frame). proxy: the owner, set when the shadow is registered.
    std::uint64_t lastUse=0;LruNode<BufferState> lru;ProxyBase* proxy=nullptr;
    enum Mode:std::uint8_t{Free,Shadow,Staged,Pass,Scratch,Busy} mode=Free;UINT off=0,size=0;DWORD flags=0;Block* stage=nullptr;unsigned char scratchSlot=0;   // Scratch: a small staged lock in StreamCore::scratch
    // canShadow: made by the game and shadow-able (DYNAMIC of any size, or non-DYNAMIC <= kMaxStaticShadow). shadowDead: never a shadow again
    // (GPU-written by ProcessVertices, or the readback failed). A buffer without a shadow (refused at creation, evicted) is not
    // "refused for good": it gets one at its next write re-lock (one readback) or, DYNAMIC, at a DISCARD lock, whenever room can be made.
    bool canShadow=false,shadowDead=false;
    bool large=false;   // the shadow lives in the large allowance (Queue::largeAdmit), not in the regular cap
    // 0.3.204 (task 21): zero-copy slices of a DYNAMIC buffer. `shadow` is the current slice; `ring` the buffer's other slices (at most kMaxSlices-1: framesAhead+2 in all), each with the seq of the last recorded zero-copy
    // unlock that reads it (0 = none) and the frame it left the current role (idle trimming). sliceSeq: the same for the current slice. The game may write, free or reuse a slice only once replayedSeq() >= its seq.
    // zc: the current lock is a DISCARD/NOOVERWRITE write lock on a slice.
    struct Slice {SliceMem mem;std::uint64_t seq=0,frame=0;};
    std::vector<Slice> ring;std::uint64_t sliceSeq=0;bool zc=false,copyP=false;   // copyP: this lock would have been zero-copy but memory pressure keeps it on the copy path
    // DISCARD locks per frame (StreamCore::frameNo): discNow in frame discFrame, discMax the largest count of the last kDiscardWindowFrames frames (it decays); they size the ring (StreamCore::allowedSlices).
    std::uint64_t discFrame=0,discMaxFrame=0;unsigned discNow=0,discMax=0;
    // 0.3.205 (#34): the zero-copy refs of the CURRENT slice the replay thread has not passed, in seq order, with their hull pendMin/pendMax (a quick "no overlap" exit). Each reads its range of the slice until it retires:
    // a NOOVERWRITE lock overlapping one would write bytes the replay thread is still reading. Such a lock is staged instead (prepareSliceWrite, lockBuffer): the game writes scratch or a Block, its unlock queues the data, and the
    // bytes wait here as a patch until the replay thread has passed seq (every ref that read the range); applyPatches then writes them into the slice. Patches are in creation order; one that overlaps an earlier one has a
    // seq >= the earlier one's (overlapSeq counts unapplied patches too), so applying every passed one in order keeps the newest bytes. While a patch is unapplied no lock gets the slice's pointer over its range and no
    // ref or copy is recorded from it (such a lock is staged too). patchBytes: their sum, at most the buffer's length (else the lock waits); patchMinSeq: their lowest seq (applyPatches has nothing to do below it).
    // stageSeq: a staged lock waits for this earlier reader before applying its patch (0 = an ordinary staged lock).
    struct PendingRef {std::uint64_t seq;UINT off,end;};
    std::vector<PendingRef> pend;UINT pendMin=0,pendMax=0;
    struct Patch {std::uint64_t seq;UINT off;std::vector<unsigned char> bytes;};
    std::vector<Patch> patches;std::size_t patchBytes=0;std::uint64_t patchMinSeq=0,stageSeq=0;
};
inline bool rangeShadowCandidate(const ProxyBase& p,const BufferState& s){return s.canShadow&&!s.shadowDead&&p.info.length>kMaxLargeShadow&&(p.info.usage&D3::kUsageDynamic);}
inline void clearRangeShadow(ProxyBase& p,BufferState& s){
    const auto bytes=s.range.capacity();s.range.clear();
    if(bytes)p.core->q.stats.rangeShadowBytes.fetch_sub(std::int64_t(bytes),std::memory_order_relaxed);
}
inline void rememberBufferRange(ProxyBase& p,BufferState& s,const void* data){
    if(!rangeShadowCandidate(p,s)||!s.size)return;
    auto& st=p.core->q.stats;const auto before=s.range.capacity();
    const auto total=std::size_t(std::max<std::int64_t>(0,st.rangeShadowBytes.load(std::memory_order_relaxed)));
    s.range.remember(s.off,s.size,data,RangeShadowBudgetBytes-std::min(total,RangeShadowBudgetBytes));
    const auto after=s.range.capacity();
    if(after!=before)st.rangeShadowBytes.fetch_add(std::int64_t(after)-std::int64_t(before),std::memory_order_relaxed);
    if(s.range.covers(s.off,s.size))own(st.rangeShadowStores);else own(st.rangeShadowRefused);
}
inline void clearPending(BufferState& s){s.pend.clear();s.pendMin=s.pendMax=0;}
// 0.3.205 (#34): the slice's contents no longer matter (a DISCARD, a rename, a dropped or fresh shadow): the patches go unapplied (their bytes are in the queue already, in the staged unlocks).
inline void dropPatches(Queue& q,BufferState& s){if(s.patches.empty())return;q.stats.zcPatchBytes.fetch_sub(std::int64_t(s.patchBytes),std::memory_order_relaxed);s.patches.clear();s.patchBytes=0;s.patchMinSeq=0;}
// 0.3.205 (#34): game thread. Writes the patches the replay thread has passed into the slice (no unreplayed ref reads their bytes any more: the ones recorded before a patch have seqs <= its seq, none is recorded over it later).
inline void applyPatches(Queue& q,BufferState& s){
    const std::uint64_t rep=q.replayedSeq();
    if(s.patches.empty()||rep<s.patchMinSeq)return;
    std::size_t j=0,freed=0;std::uint64_t minSeq=~std::uint64_t(0);
    for(std::size_t i=0;i<s.patches.size();++i){
        auto& p=s.patches[i];
        if(p.seq<=rep){std::memcpy(s.shadow.data()+p.off,p.bytes.data(),p.bytes.size());freed+=p.bytes.size();}
        else{if(p.seq<minSeq)minSeq=p.seq;if(j!=i)s.patches[j]=std::move(p);++j;}
    }
    s.patches.resize(j);s.patchMinSeq=j?minSeq:0;s.patchBytes-=freed;if(freed)q.stats.zcPatchBytes.fetch_sub(std::int64_t(freed),std::memory_order_relaxed);
}
// 0.3.205 (#34): the game-side truth of [off, off+size): the slice with the unapplied patches over it (reading the slice while the replay thread reads it too is no race).
inline void readSliceRange(const BufferState& s,unsigned char* dst,UINT off,UINT size){
    std::memcpy(dst,s.shadow.data()+off,size);
    for(const auto& p:s.patches){
        const UINT pe=p.off+UINT(p.bytes.size()),a=off>p.off?off:p.off,b=off+size<pe?off+size:pe;
        if(a<b)std::memcpy(dst+(a-off),p.bytes.data()+(a-p.off),b-a);
    }
}
// 0.3.205 (#34): drops the entries the replay has passed (and recomputes the hull of the rest).
inline void prunePending(Queue& q,BufferState& s){
    const std::uint64_t rep=q.replayedSeq();const std::size_t n=s.pend.size();std::size_t h=0;
    while(h<n&&s.pend[h].seq<=rep)++h;
    if(!h)return;
    if(h==n){clearPending(s);return;}
    s.pend.erase(s.pend.begin(),s.pend.begin()+std::ptrdiff_t(h));s.pendMin=s.pend[0].off;s.pendMax=s.pend[0].end;
    for(const auto& r:s.pend){if(r.off<s.pendMin)s.pendMin=r.off;if(r.end>s.pendMax)s.pendMax=r.end;}
}
inline void pushPending(Queue& q,BufferState& s,std::uint64_t seq,UINT off,UINT end){
    prunePending(q,s);
    if(s.pend.empty()){s.pendMin=off;s.pendMax=end;}else{if(off<s.pendMin)s.pendMin=off;if(end>s.pendMax)s.pendMax=end;}
    s.pend.push_back({seq,off,end});
}
// 0.3.205 (#34): the seq the replay thread must pass before the game may write [off,end) of the current slice: the highest among the unreplayed refs and the unapplied patches that overlap it (after applying the passed
// ones), 0 = none. No seq is above sliceSeq (the newest ref; a patch's seq is a ref's), so the scans stop once they reach it.
inline std::uint64_t overlapSeq(Queue& q,BufferState& s,UINT off,UINT end){
    prunePending(q,s);applyPatches(q,s);
    std::uint64_t best=0;
    if(!s.pend.empty()&&end>s.pendMin&&off<s.pendMax)for(auto r=s.pend.rbegin();r!=s.pend.rend()&&best<s.sliceSeq;++r)if(r->off<end&&off<r->end&&r->seq>best)best=r->seq;
    for(auto p=s.patches.rbegin();p!=s.patches.rend()&&best<s.sliceSeq;++p)if(p->off<end&&off<p->off+UINT(p->bytes.size())&&p->seq>best)best=p->seq;
    return best;
}
// Buffer shadows (game-side memory, persistent CPU copies of the buffer; every lock of one returns shadow memory and Unlock copies the locked range into
// the queue). They share one cap (ShadowBudgetBytes, adaptive, outside the queue budget) and ONE LRU: a DYNAMIC buffer gets its shadow at creation when
// the cap admits it; any buffer without one gets it later (see lockBuffer). The game thread takes and (on eviction) drops them, the replay
// thread frees one when the proxy dies; StreamCore::bufList (under texMutex) makes those safe against each other. No queued command refers to a
// shadow, so dropping an unlocked one is always safe.
inline bool dynamicBuffer(const ProxyBase& p){return (p.info.usage&D3::kUsageDynamic)!=0;}
inline LruList<BufferState>& bufListOf(StreamCore& c,const BufferState& s){return s.large?c.bufLarge:c.bufRegular;}
inline void registerShadow(ProxyBase& p,BufferState& s,bool large=false){
    s.shadowOn=true;s.large=large;s.proxy=&p;clearPending(s);dropPatches(p.core->q,s);   // 0.3.205 (#34): a fresh shadow (takeShadow / readBackShadow) has no pending refs or patches
    if(large){p.core->q.addLargeBytes(std::int64_t(p.info.length));add(p.core->q.stats.largeShadowGrants);}else p.core->q.addShadowBytes(std::int64_t(p.info.length));s.lastUse=p.core->frameNo;
    std::lock_guard<std::mutex> l(p.core->texMutex);bufListOf(*p.core,s).pushBack(&s);
}
// 0.3.204 (task 21): frees the retired slices the replay thread has passed (any thread; the game thread at every Present and before a large allocation).
inline void trimRetired(StreamCore& c){
    if(c.q.stats.retiredBytes.load(std::memory_order_relaxed)<=0)return;
    std::vector<SliceMem> dead;std::size_t freedLarge=0,freedRegular=0;
    {
        std::lock_guard<std::mutex> l(c.texMutex);const std::uint64_t rep=c.q.replayedSeq();std::size_t j=0;
        for(std::size_t i=0;i<c.retired.size();++i){
            if(c.retired[i].seq<=rep){(c.retired[i].large?freedLarge:freedRegular)+=c.retired[i].bytes;dead.push_back(std::move(c.retired[i].mem));}
            else{if(j!=i)c.retired[j]=std::move(c.retired[i]);++j;}
        }
        c.retired.resize(j);
    }
    if(freedLarge){c.q.stats.retiredLargeBytes.fetch_sub(std::int64_t(freedLarge),std::memory_order_relaxed);}
    if(freedRegular){c.q.stats.retiredRegularBytes.fetch_sub(std::int64_t(freedRegular),std::memory_order_relaxed);}
    if(freedLarge+freedRegular)c.q.stats.retiredBytes.fetch_sub(std::int64_t(freedLarge+freedRegular),std::memory_order_relaxed);
}   // `dead` is freed here, outside the lock
// texMutex held. A slice goes: freed now when no recorded command reads it any more, else moved (the storage, not the BufferState) to the retire list with its last reader's seq.
// Accounting: the current slice (a shadow) leaves shadowBytes/largeShadowBytes at once, a ring slice leaves the ring counters; what is still read counts in retired*Bytes (the ring budget, memory() totals) until trimRetired frees it.
inline void retireSliceLocked(ProxyBase& p,SliceMem& v,std::uint64_t seq,bool isRing,bool large){
    if(v.empty())return;
    StreamCore& c=*p.core;const std::size_t len=p.info.length;auto& st=c.q.stats;
    if(isRing){st.ringBytes.fetch_sub(std::int64_t(len),std::memory_order_relaxed);st.ringSlices.fetch_sub(1,std::memory_order_relaxed);(large?st.ringLargeBytes:st.ringRegularBytes).fetch_sub(std::int64_t(len),std::memory_order_relaxed);}
    else{if(large)c.q.addLargeBytes(-std::int64_t(len));else c.q.addShadowBytes(-std::int64_t(len));}
    if(seq>c.q.replayedSeq()){c.retired.push_back(StreamCore::Retired{std::move(v),seq,len,large});st.retiredBytes.fetch_add(std::int64_t(len),std::memory_order_relaxed);(large?st.retiredLargeBytes:st.retiredRegularBytes).fetch_add(std::int64_t(len),std::memory_order_relaxed);}
    SliceMem().swap(v);   // (empty after a move; frees otherwise)
}
inline void dropShadowLocked(ProxyBase& p,BufferState& s){
    if(!s.shadowOn)return;
    bufListOf(*p.core,s).remove(&s);
    retireSliceLocked(p,s.shadow,s.sliceSeq,false,s.large);for(auto& sl:s.ring)retireSliceLocked(p,sl.mem,sl.seq,true,s.large);s.ring.clear();   // 0.3.204 (task 21): every slice of the ring (large or regular cap), retired while a recorded zero-copy unlock still reads them
    if(s.large){add(p.core->q.stats.largeShadowDrops);s.large=false;}
    s.sliceSeq=0;clearPending(s);dropPatches(p.core->q,s);s.zc=false;s.shadowOn=false;
}
inline void noteShadowEvicted(StreamCore& c,const ProxyBase& p){add(dynamicBuffer(p)?c.q.stats.dynShadowEvicted:c.q.stats.stShadowEvicted);}
// Game thread: the shadow was locked: most recently used now (frame and list position). A repeated lock of the one at the tail takes no lock.
inline void touchBufferShadow(StreamCore& c,BufferState& s){
    s.lastUse=c.frameNo;
    if(bufListOf(c,s).isTail(&s))return;
    std::lock_guard<std::mutex> l(c.texMutex);auto& list=bufListOf(c,s);list.remove(&s);list.pushBack(&s);
}
// 0.3.204 (task 21): a holder whose slices unreplayed zero-copy unlocks still read frees nothing when dropped (the slices retire with their bytes counted), so it is no eviction victim until the replay has passed.
inline bool slicesBusy(const StreamCore& c,const BufferState& s){const std::uint64_t r=c.q.replayedSeq();if(r<s.sliceSeq)return true;for(const auto& sl:s.ring)if(r<sl.seq)return true;return false;}
// Game thread: makes `bytes` fit the cap by evicting shadows, least recently locked first (the head of the LRU list), never a locked one, `keep`,
// or one locked in the CURRENT frame (anti-thrash: it is in use now; the requester is refused instead; the list is in lastUse order, so the scan ends there).
// Thrash signal for the adaptive cap: evicting a HOT shadow (locked within kShadowHotFrames) or, for a re-lock (`relock`), finding no victim at all grows the
// cap one step if allowed (interval) and then re-checks before evicting anything. A request above a quarter of the cap never evicts (one buffer
// must not flush the rest). The large allowance is a list of its own (a large shadow frees nothing of the regular cap). false = still no room.
inline bool makeRoomForBufferShadow(StreamCore& c,std::size_t bytes,bool relock,const BufferState* keep){
    while(!c.q.shadowAdmit(bytes)){
        if(bytes>c.q.shadowItemLimit())return false;
        std::lock_guard<std::mutex> l(c.texMutex);
        BufferState* victim=nullptr;
        for(BufferState* s=c.bufRegular.head;s&&s->lastUse<c.frameNo;s=s->lru.next)if(s!=keep&&s->mode==BufferState::Free&&!slicesBusy(c,*s)){victim=s;break;}   // 0.3.204 (task 21): never a holder whose slices unreplayed unlocks still read
        if(!victim){if(relock&&c.q.growShadowCap(c.frameNo))continue;return false;}
        const bool hot=c.frameNo-victim->lastUse<kShadowHotFrames;
        if(hot&&c.q.growShadowCap(c.frameNo))continue;
        ProxyBase& vp=*victim->proxy;noteShadowEvicted(c,vp);if(hot)add(c.q.stats.hotShadowEvicted);dropShadowLocked(vp,*victim);
    }
    return true;
}
// Large allowance (see kMaxLargeShadow): a DYNAMIC buffer too big for the regular cap. true = the allowance has room for it (when taken by another
// large shadow and mayEvict, that one is dropped if unlocked and idle for kLargeIdleFrames, LRU). Game thread.
inline bool largeCandidate(const ProxyBase& p){const std::size_t len=p.info.length;return dynamicBuffer(p)&&len>p.core->q.shadowItemLimit()&&len<=kMaxLargeShadow;}
inline bool makeRoomForLarge(StreamCore& c,std::size_t bytes,bool mayEvict,const BufferState* keep){
    trimRetired(c);   // 0.3.204 (task 21): retired slices the replay thread has passed count against the allowance until freed
    if(c.q.largeAdmit(bytes))return true;
    if(!mayEvict||bytes>kMaxLargeShadow)return false;
    {   // 0.3.204: up to two holders: evict only if the idle unlocked ones (LRU prefix) free enough, else refuse and drop nothing
        std::lock_guard<std::mutex> l(c.texMutex);std::size_t freeable=0;
        for(const BufferState* s=c.bufLarge.head;s&&c.frameNo>=s->lastUse+kLargeIdleFrames;s=s->lru.next)if(s!=keep&&s->mode==BufferState::Free&&!slicesBusy(c,*s))freeable+=s->proxy->info.length;
        if(c.q.largeBytes()-std::min(freeable,c.q.largeBytes())+bytes>LargeShadowBudgetBytes)return false;
    }
    while(!c.q.largeAdmit(bytes)){
        std::lock_guard<std::mutex> l(c.texMutex);
        BufferState* victim=nullptr;
        for(BufferState* s=c.bufLarge.head;s&&c.frameNo>=s->lastUse+kLargeIdleFrames;s=s->lru.next)if(s!=keep&&s->mode==BufferState::Free&&!slicesBusy(c,*s)){victim=s;break;}   // (at most a few entries)
        if(!victim)return false;
        ProxyBase& vp=*victim->proxy;noteShadowEvicted(c,vp);dropShadowLocked(vp,*victim);
    }
    return true;
}
inline bool takeShadow(ProxyBase& p,BufferState& s,bool relock=false){   // a zero-filled shadow (creation, or a DISCARD lock: the contents are undefined anyway)
    const std::size_t len=p.info.length;
    const bool large=len&&largeCandidate(p);
    if(!len||!(large?makeRoomForLarge(*p.core,len,relock,&s):makeRoomForBufferShadow(*p.core,len,relock,&s)))return false;
    try{s.shadow.assign(len+kLockSlack,0);}catch(...){return false;}
    registerShadow(p,s,large);return true;
}
inline void initShadow(ProxyBase& p,BufferState& s,bool game=true){   // game=false: an implicit proxy made on the replay thread never shadows
    if(!game)return;
    const bool dyn=dynamicBuffer(p);s.canShadow=p.info.length&&(dyn||p.info.length<=kMaxStaticShadow);
    if(!dyn||!s.canShadow)return;
    if(!takeShadow(p,s)){add(p.core->q.stats.shadowRefused);add(p.core->q.stats.shadowRefusedBytes,p.info.length);}   // no shadow yet: see BufferState::canShadow
}
inline void dropShadow(ProxyBase& p,BufferState& s){   // a GPU-side write or the proxy's end: the shadow no longer mirrors the buffer
    std::lock_guard<std::mutex> l(p.core->texMutex);dropShadowLocked(p,s);clearRangeShadow(p,s);
}
// A GPU-side write (ProcessVertices into the buffer): the shadow is stale for good, and never made again (a read-back right after a GPU write is not
// one the NOOVERWRITE flag may skip the wait for).
inline void markBufferGpuWritten(ProxyBase& p,BufferState& s){dropShadow(p,s);s.written=true;s.shadowDead=true;}
// Game thread: makes a shadow of the whole, already written buffer by ONE synchronous readback (the replay thread locks the real buffer with
// readBackLock() flags: READONLY, and NOOVERWRITE on DXVK >= 3 so it does not wait for the GPU; the tracked Buffer wrapper keeps the revision because
// READONLY stays set). A buffer above a quarter of the cap (or kMaxStaticShadow when not DYNAMIC) stays pass-through. 1 = shadow made, 0 = none
// (refused or unreadable: the lock takes the old paths), -1 = could not wait (nested in a pumped wait).
inline int readBackShadow(ProxyBase& self,BufferState& s){
    StreamCore& core=*self.core;Queue& q=core.q;const UINT len=self.info.length;const bool dyn=dynamicBuffer(self);
    const bool large=largeCandidate(self);
    if(!(large?makeRoomForLarge(core,len,true,&s):(len<=q.shadowItemLimit()&&makeRoomForBufferShadow(core,len,true,&s)))){add(q.stats.relockRefused);add(q.stats.relockRefusedBytes,len);return 0;}
    try{s.shadow.assign(std::size_t(len)+kLockSlack,0);}catch(...){return 0;}
    bool ok=false;
    const bool ran=runTask(core,[&](StreamCore& c){
        if(!self.inner||self.dead.load())return;
        const DWORD fl=c.readBackLock?c.readBackLock():D3::kLockReadOnly;void* got=nullptr;const bool vb=self.kind==Kind::VertexBuffer;
        const HRESULT hr=vb?static_cast<IDirect3DVertexBuffer9*>(self.inner)->Lock(0,len,&got,fl):static_cast<IDirect3DIndexBuffer9*>(self.inner)->Lock(0,len,&got,fl);
        if(FAILED(hr)||!got)return;
        std::memcpy(s.shadow.data(),got,len);ok=true;
        if(vb)static_cast<IDirect3DVertexBuffer9*>(self.inner)->Unlock();else static_cast<IDirect3DIndexBuffer9*>(self.inner)->Unlock();},Cmd::SyncLock);
    if(!ran){SliceMem().swap(s.shadow);return -1;}
    if(!ok){SliceMem().swap(s.shadow);s.shadowDead=true;return 0;}   // the real buffer cannot be read: pass-through as before
    add(dyn?q.stats.dynShadowReadbacks:q.stats.stShadowReadbacks);
    registerShadow(self,s,large);return 1;
}
// Memory pressure (0.3.216): no buffer shadow is dropped or capped (see Queue::shadowItemLimit); only the free ring slices go (releaseBufferRings).
// texMutex held, game thread: frees the ring slices no reader needs any more (all of them under memory pressure; olderThan = idle trimming: only those free for that many frames). The current slice stays.
inline void releaseRingLocked(ProxyBase& p,BufferState& s,std::uint64_t olderThan=0){
    if(s.mode!=BufferState::Free)return;   // (a lock in progress, maybe waiting in a pumped wait: its slices stay)
    const std::uint64_t rep=p.core->q.replayedSeq();
    for(std::size_t i=s.ring.size();i-->0;){
        auto& sl=s.ring[i];
        if(rep<sl.seq||p.core->frameNo<sl.frame+olderThan)continue;
        retireSliceLocked(p,sl.mem,sl.seq,true,s.large);s.ring.erase(s.ring.begin()+i);
    }
}
// Game thread, every Present (scanning every 16th frame): ring slices that have been free for kLargeIdleFrames frames are freed (the memory returns when the crowd goes away).
inline void trimIdleRings(StreamCore& c){
    if(c.frameNo%16!=0||c.q.stats.ringSlices.load(std::memory_order_relaxed)<=0)return;   // (the idle threshold is 60 frames: a scan every 16 is plenty)
    std::lock_guard<std::mutex> l(c.texMutex);
    for(BufferState* s=c.bufRegular.head;s;s=s->lru.next)if(!s->ring.empty())releaseRingLocked(*s->proxy,*s,kLargeIdleFrames);
    for(BufferState* s=c.bufLarge.head;s;s=s->lru.next)if(!s->ring.empty())releaseRingLocked(*s->proxy,*s,kLargeIdleFrames);
}
// 0.3.216 (issue #66 follow-up): under pressure only the free ring slices go (a shadow never does: its buffer's next lock would be a synchronous readback). The shadows that stay keep their current slice.
inline void releaseBufferRings(StreamCore& c){
    std::lock_guard<std::mutex> l(c.texMutex);
    for(BufferState* s=c.bufRegular.head;s;s=s->lru.next)releaseRingLocked(*s->proxy,*s);
    for(BufferState* s=c.bufLarge.head;s;s=s->lru.next)releaseRingLocked(*s->proxy,*s);
}
// Range rules (our reading of DXVK, not verified against it): an offset beyond the end fails with INVALIDCALL; a size of 0 or one
// that runs past the end becomes "to the end" instead of failing, so the staged/shadow range and the replayed Lock are the clamped one.
// 0.3.204 (task 21): game thread. Waits (pumped, exact) until the replay thread has retired command seq; counted as a rename wait, a write wait (flags 0) or, 0.3.205 (#34), an overlap wait (a NOOVERWRITE lock over a pending ref); the time also lands in syncNs, so the split timers stay right.
// false = nested in a pumped wait (a message handler DXVK's pump dispatched): nothing waited, the caller takes the fallback.
enum class SliceWait {Rename,Write,Overlap};
inline bool waitSliceSeq(StreamCore& c,std::uint64_t seq,SliceWait kind){
    Queue& q=c.q;if(q.replayedSeq()>=seq)return true;
    if(inPumpedWait){add(q.stats.nestedSyncs);return false;}
    const std::uint64_t t0=nowNs();q.waitReplayed(seq,WaitKind::Sync);const std::uint64_t ns=nowNs()-t0;
    own(kind==SliceWait::Rename?q.stats.renameWaits:kind==SliceWait::Write?q.stats.writeWaits:q.stats.zcOverlapWaits);own(kind==SliceWait::Rename?q.stats.renameWaitNs:kind==SliceWait::Write?q.stats.writeWaitNs:q.stats.zcOverlapWaitNs,ns);return true;
}
constexpr std::uint64_t kDiscardWindowFrames=60;
// Game thread, at every DISCARD write lock of a slice-capable buffer: counts it in the current frame; the largest per-frame count of the last kDiscardWindowFrames frames decays. Returns the count that sizes the ring.
inline unsigned noteDiscard(StreamCore& c,BufferState& s){
    if(s.discFrame!=c.frameNo){   // a new frame: the one just ended competes for the window's maximum (an expired maximum is replaced by it)
        if(s.discNow>=s.discMax||c.frameNo>=s.discMaxFrame+kDiscardWindowFrames){s.discMax=s.discNow;s.discMaxFrame=s.discFrame;}
        s.discFrame=c.frameNo;s.discNow=0;
    }
    ++s.discNow;raiseMax(c.q.stats.maxDiscards,s.discNow);
    return s.discNow>s.discMax?s.discNow:s.discMax;
}
constexpr std::size_t kZeroCopyMin=4096;   // a smaller range keeps the inline copy (cheaper than a reference and its bookkeeping)
// Game thread: a write lock of a DYNAMIC buffer's current slice (s.shadow). The replay thread may still read that slice for zero-copy unlocks recorded earlier (s.sliceSeq), so:
// no pending reader: as it is. DISCARD (the contents are undefined): the game gets another slice of the buffer's ring, the old one stays (renamed) for its readers: the oldest free one (its readers retired); else a new one if the
// ring has room, the separate ring budget allows it and the allocation succeeds; else the game waits for the oldest busy slice.
// NOOVERWRITE: the D3D9 contract says the game writes only bytes the GPU does not read, but the replay thread's zero-copy unlocks (refs) read game-side bytes of ranges the GPU never uses: a NOOVERWRITE lock overlapping the range of an
// unreplayed ref (or of an unapplied patch) returns that seq in `stage` (counted zcOverlapLocks), and lockBuffer stages the lock instead of handing out the slice (0.3.205, #34: no wait; the cost is copies of the locked bytes only).
// A NOOVERWRITE lock over no such range: as it is. Any other write: waits for the readers (exact).
// false = nested in a pumped wait with the slice busy: the caller drops the shadow (it retires with its readers) and takes the unshadowed path.
// Memory pressure (the 32-bit address space is nearly full): no new reader is created: a DISCARD/NOOVERWRITE unlock COPIES into the queue as before zero-copy (zc=false). 0.3.216: a DISCARD takes or adds a ring slice as without pressure (a wait for the current slice per lock would be a sync storm). Older text follows.
// (no new pending readers); a DISCARD whose current slice still has readers takes a free or new ring slice, and waits only when the ring is at its allowed size.
inline bool prepareSliceWrite(ProxyBase& self,BufferState& s,DWORD eff,bool& zc,std::uint64_t& stage){
    StreamCore& c=*self.core;Queue& q=c.q;const bool pressure=q.pressure();
    zc=(eff&(D3::kLockDiscard|D3::kLockNoOverwrite))!=0&&!pressure;stage=0;
    const unsigned perFrame=(eff&D3::kLockDiscard)?noteDiscard(c,s):0;
    if(!(eff&D3::kLockDiscard)&&(eff&D3::kLockNoOverwrite)){   // 0.3.205 (#34): the range may still be read by an unreplayed ref (this slice's, whatever its unlock flags were, and also with zc off under memory pressure)
        if((stage=overlapSeq(q,s,s.off,s.off+s.size)))own(q.stats.zcOverlapLocks);
        return true;
    }
    if(q.replayedSeq()>=s.sliceSeq){if(eff&D3::kLockDiscard)dropPatches(q,s);else applyPatches(q,s);clearPending(s);return true;}
    if(eff&D3::kLockDiscard){
        const std::size_t len=self.info.length;trimRetired(c);
        const std::size_t allowed=StreamCore::allowedSlices(c.framesAhead,perFrame);
        bool atMax=false,noRoom=false,allocFailed=false;
        const std::uint64_t rep=q.replayedSeq();std::size_t pick=s.ring.size();
        for(std::size_t i=0;i<s.ring.size();++i)if(rep>=s.ring[i].seq&&(pick==s.ring.size()||s.ring[i].seq<s.ring[pick].seq))pick=i;
        if(pick==s.ring.size()&&s.ring.size()+1>=allowed)atMax=true;
        else if(pick==s.ring.size()){
            // A resource released on the replay thread can move its slices to the retire list.
            std::lock_guard<std::mutex> lock(c.texMutex);
            const auto regular=std::size_t(q.stats.ringRegularBytes.load(std::memory_order_relaxed)+
                                           q.stats.retiredRegularBytes.load(std::memory_order_relaxed));
            const auto large=std::size_t(q.stats.ringLargeBytes.load(std::memory_order_relaxed)+
                                         q.stats.retiredLargeBytes.load(std::memory_order_relaxed));
            // A busy regular ring can borrow unused space from the large allowance.
            // Keep at least 20 MiB available to large rings and count retired slices too.
            constexpr std::size_t regularBorrowBytes=16u<<20;
            const auto regularLimit=RingRegularBudgetBytes+(pressure?0:regularBorrowBytes);
            const bool room=(s.large?large+len<=LargeShadowBudgetBytes:regular+len<=regularLimit)&&
                regular+large+len<=RingRegularBudgetBytes+LargeShadowBudgetBytes;
            if(!room)noRoom=true;
            else{
                try{BufferState::Slice sl;sl.mem.resize(len+kLockSlack);sl.frame=c.frameNo;s.ring.push_back(std::move(sl));pick=s.ring.size()-1;}catch(...){pick=s.ring.size();allocFailed=true;}   // (resize: uninitialized, the contents of a DISCARD are undefined)
                if(pick<s.ring.size()){q.stats.ringBytes.fetch_add(std::int64_t(len),std::memory_order_relaxed);(s.large?q.stats.ringLargeBytes:q.stats.ringRegularBytes).fetch_add(std::int64_t(len),std::memory_order_relaxed);q.stats.ringSlices.fetch_add(1,std::memory_order_relaxed);own(q.stats.renameAllocs);raiseMax(q.stats.maxRing,s.ring.size());}
            }
        }
        if(pick==s.ring.size()){   // none free, none allowed: why (the zerocopy line's waitWhy)
            // A short DISCARD can start in a queued block while the old readers keep their slice.
            // Keep their sequence and ranges intact for the staged write and following appends.
            if(!s.large&&!pressure&&!inPumpedWait&&s.size&&s.size<=MaxInlinePayload-kLockSlack){
                if(Block* block=q.tryAllocBlock(s.size+kLockSlack,false)){
                    dropPatches(q,s);
                    std::memset(block->data(),0,s.size);
                    s.stage=block;
                    stage=s.sliceSeq;
                    return true;
                }
            }
            own(atMax?q.stats.waitMax:noRoom?q.stats.waitBudget:allocFailed?q.stats.waitAlloc:q.stats.waitMax);
            if(s.ring.empty()){if(!waitSliceSeq(c,s.sliceSeq,SliceWait::Rename))return false;clearPending(s);dropPatches(q,s);return true;}   // no second slice: the readers must finish before the game overwrites this one
            pick=0;for(std::size_t i=1;i<s.ring.size();++i)if(s.ring[i].seq<s.ring[pick].seq)pick=i;   // the oldest busy one
            if(!waitSliceSeq(c,s.ring[pick].seq,SliceWait::Rename))return false;
            if(!s.shadowOn)return true;   // a nested path (ProcessVertices) dropped the shadow and its ring during the wait: nothing left to swap; the caller takes the unshadowed path
        }
        auto& sl=s.ring[pick];s.shadow.swap(sl.mem);sl.seq=s.sliceSeq;sl.frame=c.frameNo;s.sliceSeq=0;clearPending(s);dropPatches(q,s);own(q.stats.renames);return true;
    }
    if(!waitSliceSeq(c,s.sliceSeq,SliceWait::Write))return false;
    applyPatches(q,s);clearPending(s);return true;
}
inline HRESULT lockBuffer(ProxyBase& self,BufferState& s,UINT off,UINT size,void** pp,DWORD flags){
    LockScope _ts(*self.core);Queue& q=self.core->q;const UINT length=self.info.length;
    if(!pp||s.mode!=BufferState::Free||off>length)return D3DERR_INVALIDCALL;
    s.whole=!size||(off==0&&size>=length);
    if(!size||std::uint64_t(off)+size>length)size=length-off;
    s.off=off;s.size=size;s.flags=flags;
    const DWORD eff=effectiveLockFlags(flags,self.info.pool);
    const bool rangeHit=rangeShadowCandidate(self,s)&&!(flags&D3::kLockReadOnly)&&!(eff&D3::kLockDiscard)&&s.range.canRead(off,size);
    // a buffer without a shadow (refused at creation, evicted, dropped) gets one now when room can be made: where its contents are undefined anyway (a DYNAMIC
    // buffer's DISCARD lock, default pool) a zero-filled one; at a WRITE lock of a written buffer one whole-buffer readback (a first write / non-DYNAMIC DISCARD
    // stays on the staged path; a READONLY lock stays a pass-through like a texture's); a refusal keeps the old paths
    if(!s.shadowOn&&s.canShadow&&!s.shadowDead){
        if(dynamicBuffer(self)&&(eff&D3::kLockDiscard)){if(takeShadow(self,s,true))add(q.stats.shadowLate);}
        else if(s.written&&!(eff&D3::kLockDiscard)&&!(flags&D3::kLockReadOnly)&&!rangeHit){
            if(readBackShadow(self,s)<0)return D3DERR_INVALIDCALL;
        }
    }
    s.zc=false;s.copyP=false;
    if(s.shadowOn&&!s.patches.empty()&&(flags&D3::kLockReadOnly)){   // 0.3.205 (#34): the game reads the slice: the patches over the range go in first (a wait for the newest, exact; nested in a pumped wait the shadow goes)
        std::uint64_t w=0;applyPatches(q,s);for(const auto& p:s.patches)if(p.off<off+size&&off<p.off+UINT(p.bytes.size())&&p.seq>w)w=p.seq;
        if(w){
            s.mode=BufferState::Shadow;const bool waited=waitSliceSeq(*self.core,w,SliceWait::Overlap);s.mode=BufferState::Free;
            if(s.shadowOn){if(waited)applyPatches(q,s);else{std::lock_guard<std::mutex> l(self.core->texMutex);dropShadowLocked(self,s);}}
        }
    }
    if(s.shadowOn&&dynamicBuffer(self)&&self.info.pool==D3::kPoolDefault&&!(flags&D3::kLockReadOnly)){   // 0.3.204 (task 21): a write lock of a DYNAMIC default-pool buffer's slice, large allowance or regular cap (see prepareSliceWrite)
        bool zc=false;std::uint64_t stage=0;
        // the buffer is busy while prepareSliceWrite may wait (a PUMPED wait): a nested Lock of it fails as a double lock, evictions and ring trimming skip it; a nested ProcessVertices
        // can still drop its shadow (checked below)
        s.mode=BufferState::Shadow;bool ok=prepareSliceWrite(self,s,eff,zc,stage);s.mode=BufferState::Free;
        if(s.shadowOn&&ok&&stage){   // The unlock keeps a patch for the held slice until its earlier readers finish.
            Block* b=s.stage;
            if(!b&&s.patchBytes+size<=length){
                if(unsigned char* scratch=self.core->scratch.take(size,s.scratchSlot)){
                    readSliceRange(s,scratch,off,size);
                    s.mode=BufferState::Scratch;s.stageSeq=stage;touchBufferShadow(*self.core,s);
                    *pp=scratch;own(q.stats.lockAsync);return D3D_OK;
                }
            }
            if(b||(s.patchBytes+size<=length&&(b=q.tryAllocBlock(size+kLockSlack)))){
                if(!(eff&D3::kLockDiscard))readSliceRange(s,b->data(),off,size);
                b->used=size;s.mode=BufferState::Staged;s.stage=b;s.stageSeq=stage;touchBufferShadow(*self.core,s);*pp=b->data();own(q.stats.lockAsync);return D3D_OK;
            }
            // no Block or the patches already hold a whole buffer: the exact wait (the range is free once the replay thread has passed `stage` and the passed patches are in)
            s.mode=BufferState::Shadow;ok=waitSliceSeq(*self.core,stage,SliceWait::Overlap);s.mode=BufferState::Free;
            if(s.shadowOn&&ok)applyPatches(q,s);
        }
        if(!s.shadowOn){}   // dropped during the wait: the unshadowed paths below
        else if(ok){s.zc=zc;s.copyP=!zc&&(eff&(D3::kLockDiscard|D3::kLockNoOverwrite))!=0;}
        else{   // nested in a pumped wait: the shadow (retired with its readers) goes. A DISCARD stages below; any other write copies the range's current bytes (the game-side truth) into a staged Block, which its unlock queues without a copy
            Block* b=nullptr;
            if(!(eff&D3::kLockDiscard)&&(b=q.tryAllocBlock(size+kLockSlack))){readSliceRange(s,b->data(),off,size);b->used=size;}
            {std::lock_guard<std::mutex> l(self.core->texMutex);dropShadowLocked(self,s);}
            if(b){s.mode=BufferState::Staged;s.stage=b;*pp=b->data();own(q.stats.lockAsync);return D3D_OK;}
        }
    }
    if(s.shadowOn){s.mode=BufferState::Shadow;touchBufferShadow(*self.core,s);*pp=s.shadow.data()+off;own(q.stats.lockAsync);return D3D_OK;}
    PassReason why=PassReason::NoShadow;
    if(flags&D3::kLockReadOnly)why=PassReason::ReadOnly;
    else if(!s.written||(eff&D3::kLockDiscard)||rangeHit){
        // 0.3.192 (CS): a small lock stages in game-side scratch and unlocks as an inline record (like the shadow path): no pooled Block, no cross-thread pool mutex.
        // The range is zeroed (bytes the game leaves unwritten replay deterministically); the slack past it is never replayed and stays as it was.
        if(unsigned char* sp=self.core->scratch.take(size,s.scratchSlot)){
            s.mode=BufferState::Scratch;if(rangeHit){s.range.read(sp,off,size);own(q.stats.rangeShadowHits);}else{if(rangeShadowCandidate(self,s)&&(eff&D3::kLockDiscard))s.range.discard();else s.range.invalidate();std::memset(sp,0,size);}
            *pp=sp;own(q.stats.lockAsync);return D3D_OK;
        }
        s.mode=BufferState::Busy; // A budget wait may pump a nested Lock, Unlock or GPU write.
        Block* staged=q.tryAllocBlock(size+kLockSlack);s.mode=BufferState::Free;
        if(staged&&((rangeHit&&(!rangeShadowCandidate(self,s)||!s.range.canRead(off,size)))||(!rangeHit&&s.written&&!(eff&D3::kLockDiscard)))){q.freeBlock(staged);staged=nullptr;}
        if(Block* b=staged){
            s.mode=BufferState::Staged;s.stage=b;b->used=size;
            if(rangeHit){s.range.read(b->data(),off,size);own(q.stats.rangeShadowHits);}else{if(rangeShadowCandidate(self,s)&&(eff&D3::kLockDiscard))s.range.discard();else s.range.invalidate();std::memset(b->data(),0,size);}
            *pp=b->data();own(q.stats.lockAsync);return D3D_OK;
        }
        why=PassReason::Budget;
    }
    countPass(self,why);
    HRESULT hr=D3DERR_INVALIDCALL;void* got=nullptr;
    const bool ran=runTask(*self.core,[&](StreamCore&){
        if(!self.inner||self.dead.load())return;
        hr=self.kind==Kind::VertexBuffer?static_cast<IDirect3DVertexBuffer9*>(self.inner)->Lock(off,size,&got,flags)
                                         :static_cast<IDirect3DIndexBuffer9*>(self.inner)->Lock(off,size,&got,flags);},Cmd::SyncLock);
    if(!ran)return D3DERR_INVALIDCALL;
    if(SUCCEEDED(hr)){s.mode=BufferState::Pass;s.passData=got;if(eff&D3::kLockDiscard)s.range.invalidate();*pp=got;}
    return hr;
}
inline void noteRecorded(ProxyBase& p,const BufferState& s,UINT bytes){auto& st=p.core->q.stats;own(st.lockRecordedBytes,bytes);if(s.whole)own(st.wholeLockBytes,bytes);}   // game thread is the only writer: no locked instruction per buffer unlock
inline bool appendBufferWrite(Queue& q,ProxyBase& self,BufferState& s){
    if(s.flags!=D3::kLockNoOverwrite||s.pend.empty())return false;
    auto* a=static_cast<UnlockBufferRefArgs*>(q.unpublishedTailPayload(std::uint16_t(Cmd::UnlockBufferRef)));
    if(!a||a->proxy!=&self||a->src!=s.shadow.data()||a->flags!=s.flags||a->firstSize||a->off+a->size!=s.off)return false;
    auto& ref=s.pend.back();
    if(ref.seq!=s.sliceSeq||ref.off!=a->off||ref.end!=s.off)return false;
    // The existing sequence now owns both ranges. A later write that overlaps
    // either one must stage or wait until the combined upload retires.
    ref.end=s.off+s.size;s.pendMax=std::max(s.pendMax,ref.end);
    a->firstSize=a->size;a->size+=s.size;
    return true;
}
inline HRESULT unlockBufferImpl(ProxyBase& self,BufferState& s,bool& zcDone,bool& overlapStaged){
    LockScope _ts(*self.core);Queue& q=self.core->q;
    switch(s.mode){
    case BufferState::Busy:return D3DERR_INVALIDCALL;
    case BufferState::Free:return D3DERR_INVALIDCALL;
    case BufferState::Shadow:{
        s.mode=BufferState::Free;const bool zc=s.zc;s.zc=false;
        if(!s.shadowOn)return D3D_OK;   // (a GPU write dropped it under a lock: nothing left to copy)
        if((s.flags&D3::kLockReadOnly)||!s.size)return D3D_OK;   // (a zero-length range must not be replayed: Lock(off,0) means to the end)
        noteRecorded(self,s,s.size);   // zero-copy unlocks count too (before/after comparisons stay valid); zeroCopyBytes says how much of it went without a copy
        if(zc&&appendBufferWrite(q,self,s)){
            own(q.stats.zeroCopyUnlocks);own(q.stats.zeroCopyBytes,s.size);zcDone=true;
        }else if(zc&&s.size>=kZeroCopyMin){   // 0.3.204 (task 21): no copy: the replay thread reads the slice itself; the slice stays alive and unwritten (for conflicting locks) until this command retires
            // 0.3.205 (#34): the ref reads s.shadow[off, off+size) until it retires; a later NOOVERWRITE lock into that range is staged (prepareSliceWrite), so the replay thread's memcpy never overlaps a game write
            auto* a=static_cast<UnlockBufferRefArgs*>(q.reserve((std::uint16_t)Cmd::UnlockBufferRef,sizeof(UnlockBufferRefArgs),kFlagWaitTarget));   // flagged: prepareSliceWrite may wait for it
            *a=UnlockBufferRefArgs{&self,s.shadow.data(),s.off,s.size,s.flags,0};q.commit();s.sliceSeq=q.recordedSeq();pushPending(q,s,s.sliceSeq,s.off,s.off+s.size);
            own(q.stats.zeroCopyUnlocks);own(q.stats.zeroCopyBytes,s.size);zcDone=true;
        }else{
        if(s.size<=MaxInlinePayload-sizeof(UnlockBufferArgs)-16){
            auto* a=static_cast<UnlockBufferArgs*>(q.reserve((std::uint16_t)Cmd::UnlockBuffer,std::uint32_t(sizeof(UnlockBufferArgs)+s.size)));
            *a=UnlockBufferArgs{&self,s.off,s.size,s.flags,1};std::memcpy(a+1,s.shadow.data()+s.off,s.size);q.commit();
        }else if(Block* b=q.tryAllocBlock(s.size)){
            auto* a=static_cast<UnlockBufferArgs*>(q.reserveWithBlock((std::uint16_t)Cmd::UnlockBuffer,sizeof(UnlockBufferArgs),b));
            *a=UnlockBufferArgs{&self,s.off,s.size,s.flags,0};b->used=s.size;std::memcpy(b->data(),s.shadow.data()+s.off,s.size);q.commit();
        }else{   // no Block: the same range in inline pieces (DISCARD only on the first)
            const UINT piece=128u<<10;
            for(UINT done=0;done<s.size;done+=piece){
                const UINT n=s.size-done<piece?s.size-done:piece;
                auto* a=static_cast<UnlockBufferArgs*>(q.reserve((std::uint16_t)Cmd::UnlockBuffer,std::uint32_t(sizeof(UnlockBufferArgs)+n)));
                *a=UnlockBufferArgs{&self,s.off+done,n,done?(s.flags&~D3::kLockDiscard):s.flags,1};std::memcpy(a+1,s.shadow.data()+s.off+done,n);q.commit();
            }
        }
        }
        s.written=true;
        return D3D_OK;}
    case BufferState::Scratch:
    case BufferState::Staged:{
        const bool scratch=s.mode==BufferState::Scratch;
        auto& slots=self.core->scratch;
        Block* block=s.stage;
        const unsigned char* data=scratch?slots.slot[s.scratchSlot].data():block->data();
        if(!s.size){
            if(scratch)slots.give(s.scratchSlot);
            else q.freeBlock(block);
            s.stage=nullptr;s.stageSeq=0;s.mode=BufferState::Free;return D3D_OK;
        }
        noteRecorded(self,s,s.size);
        rememberBufferRange(self,s,data);
        if(const std::uint64_t ov=s.stageSeq){   // Apply this patch only after readers of the old slice have passed.
            s.stageSeq=0;overlapStaged=true;
            if(s.shadowOn){
                bool kept=false;
                try{s.patches.push_back(BufferState::Patch{ov,s.off,std::vector<unsigned char>(data,data+s.size)});kept=true;}catch(...){}
                if(kept){s.patchBytes+=s.size;if(!s.patchMinSeq||ov<s.patchMinSeq)s.patchMinSeq=ov;q.stats.zcPatchBytes.fetch_add(std::int64_t(s.size),std::memory_order_relaxed);}
                else if(waitSliceSeq(*self.core,ov,SliceWait::Overlap)){if(s.shadowOn){applyPatches(q,s);std::memcpy(s.shadow.data()+s.off,data,s.size);}}   // no memory for the patch: the exact wait, then the slice directly
                else if(s.shadowOn){std::lock_guard<std::mutex> l(self.core->texMutex);dropShadowLocked(self,s);}   // (nested in a pumped wait: the shadow goes, the bytes are in the queue)
            }
        }
        const UnlockBufferArgs upload{&self,s.off,s.size,s.flags,0};
        s.mode=BufferState::Busy;
        if(scratch||(s.size<block->used&&s.size<=MaxInlinePayload-sizeof(UnlockBufferArgs)-16)){
            auto* a=static_cast<UnlockBufferArgs*>(q.reserve((std::uint16_t)Cmd::UnlockBuffer,sizeof(UnlockBufferArgs)+upload.size));
            *a=upload;a->inlineData=1;
            std::memcpy(a+1,data,upload.size);
            if(scratch)slots.give(s.scratchSlot);
            else q.freeBlock(block);
        }else{
            auto* a=static_cast<UnlockBufferArgs*>(q.reserveWithBlock((std::uint16_t)Cmd::UnlockBuffer,sizeof(UnlockBufferArgs),block));
            *a=upload;
        }
        q.commit();s.stage=nullptr;s.mode=BufferState::Free;s.written=true;return D3D_OK;}
    default:{
        HRESULT hr=D3DERR_INVALIDCALL;
        if(s.mode==BufferState::Pass&&s.passData&&!(s.flags&D3::kLockReadOnly))rememberBufferRange(self,s,s.passData);
        if(!runTask(*self.core,[&](StreamCore&){if(self.inner)hr=self.kind==Kind::VertexBuffer?static_cast<IDirect3DVertexBuffer9*>(self.inner)->Unlock():static_cast<IDirect3DIndexBuffer9*>(self.inner)->Unlock();},Cmd::SyncUnlock))return D3DERR_INVALIDCALL;
        s.passData=nullptr;if(FAILED(hr))clearRangeShadow(self,s);
        s.mode=BufferState::Free;if(!(s.flags&D3::kLockReadOnly))s.written=true;return hr;}
    }
}
// 0.3.204 (task 21): why a DISCARD/NOOVERWRITE write unlock of a DYNAMIC buffer did not go zero-copy (the CSTREAM zerocopy line): not the default pool (the flags mean nothing there), no shadow
// slice (staged, scratch, pass-through or a dropped shadow), a range below kZeroCopyMin, a NOOVERWRITE lock staged over an unreplayed ref (0.3.205, #34), anything else.
inline HRESULT unlockBuffer(ProxyBase& self,BufferState& s){
    const BufferState::Mode m=s.mode;const DWORD fl=s.flags;const UINT sz=s.size;const bool cp=s.copyP;bool zcDone=false,ovl=false;
    const HRESULT hr=unlockBufferImpl(self,s,zcDone,ovl);
    if(!zcDone&&m!=BufferState::Free&&dynamicBuffer(self)&&(fl&(D3::kLockDiscard|D3::kLockNoOverwrite))&&!(fl&D3::kLockReadOnly)&&SUCCEEDED(hr)){
        Counters& st=self.core->q.stats;
        const bool underPressure=cp&&m==BufferState::Shadow&&sz>=kZeroCopyMin&&self.info.pool==D3::kPoolDefault;
        if(underPressure){own(st.copyPressureUnlocks);own(st.copyPressureBytes,sz);}   // would have been zero-copy: copied because of memory pressure
        if(ovl&&(fl&D3::kLockDiscard))own(st.stagedDiscards);
        else if(ovl){own(st.zcOverlapStaged);own(st.zcOverlapCopyBytes,sz);}
        if(ovl)own(sz>=kZeroCopyMin?st.zcSkipOverlap:st.zcSkipSmall);
        else own(self.info.pool!=D3::kPoolDefault?st.zcSkipPool:m!=BufferState::Shadow?st.zcSkipNoShadow:sz<kZeroCopyMin?st.zcSkipSmall:underPressure?st.zcSkipPressure:st.zcSkipOther);
    }
    return hr;
}
inline void releaseStage(ProxyBase& p,BufferState& s){s.stageSeq=0;if(s.stage){p.core->q.freeBlock(s.stage);s.stage=nullptr;}if(s.mode==BufferState::Scratch){p.core->scratch.give(s.scratchSlot);s.mode=BufferState::Free;}}

struct StreamVertexBuffer final:IDirect3DVertexBuffer9,ProxyBase {
    BufferState buf;
    StreamVertexBuffer(StreamCore* c,UINT length,DWORD usage,DWORD fvf,DWORD pool,bool game=true):ProxyBase(c,Kind::VertexBuffer){
        info.length=length;info.usage=usage;info.fvf=fvf;info.pool=pool;info.fmt=D3::kFmtVertexData;info.type=D3::kTypeVB;
        initShadow(*this,buf,game);ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DVertexBuffer9*>(this));}
    ~StreamVertexBuffer(){dropShadow(*this,buf);releaseStage(*this,buf);liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_VERTEXBUFFER_METHODS
    NL_PROXY_COM(IDirect3DVertexBuffer9,IDirect3DResource9)
    NL_RES_LOCALS(VertexBuffer)
    HRESULT local(CmdTag<Cmd::VertexBuffer_GetDesc>,D3DVERTEXBUFFER_DESC* d){
        if(!d)return D3DERR_INVALIDCALL;d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_VERTEXBUFFER;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->Size=info.length;d->FVF=info.fvf;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Lock(UINT off,UINT size,void** pp,DWORD flags) override{return lockBuffer(*this,buf,off,size,pp,flags);}
    HRESULT STDMETHODCALLTYPE Unlock() override{
        using namespace NorthlightGx12340;
        constexpr DWORD usage=D3::kUsageDynamic|D3::kUsageWriteOnly;
        const bool staged=(buf.mode==BufferState::Staged||buf.mode==BufferState::Scratch)&&buf.stageSeq;
        if(verifiedParticleUpload&&(buf.mode==BufferState::Shadow||staged)&&buf.size==ParticleUploadReservation&&
           info.pool==D3::kPoolDefault&&(info.usage&usage)==usage&&buf.off%ParticleVertexBytes==0&&
           (buf.flags==D3::kLockNoOverwrite||buf.flags==D3::kLockDiscard)){
            unsigned used;
            if(particleUploadBytes(__builtin_frame_address(0),used)&&used<buf.size&&(used||buf.flags==D3::kLockNoOverwrite)){
                auto& stats=core->q.stats;
                own(stats.particleUploadTrims);own(stats.particleUploadBytesSaved,buf.size-used);
                if(staged)own(stats.particleStagedTrims);
                if(!used)own(stats.particleUploadEmpty);
                buf.size=used;
            }
        }
        return unlockBuffer(*this,buf);
    }
};
struct StreamIndexBuffer final:IDirect3DIndexBuffer9,ProxyBase {
    BufferState buf;
    StreamIndexBuffer(StreamCore* c,UINT length,DWORD usage,unsigned fmt,DWORD pool,bool game=true):ProxyBase(c,Kind::IndexBuffer){
        info.length=length;info.usage=usage;info.fmt=fmt;info.pool=pool;info.type=D3::kTypeIB;
        initShadow(*this,buf,game);ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DIndexBuffer9*>(this));}
    ~StreamIndexBuffer(){dropShadow(*this,buf);releaseStage(*this,buf);liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_INDEXBUFFER_METHODS
    NL_PROXY_COM(IDirect3DIndexBuffer9,IDirect3DResource9)
    NL_RES_LOCALS(IndexBuffer)
    HRESULT local(CmdTag<Cmd::IndexBuffer_GetDesc>,D3DINDEXBUFFER_DESC* d){
        if(!d)return D3DERR_INVALIDCALL;d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_INDEXBUFFER;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->Size=info.length;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Lock(UINT off,UINT size,void** pp,DWORD flags) override{return lockBuffer(*this,buf,off,size,pp,flags);}
    HRESULT STDMETHODCALLTYPE Unlock() override{return unlockBuffer(*this,buf);}
};
