// Image subresources, shadow residency, and staged uploads.
// A texture level / cube face / volume level, or a standalone surface.
struct SubRes {
    bool written=false;                 // a GPU write or an earlier lock: later write locks need a shadow or pass through
    enum Mode:std::uint8_t{Free,Staged,Pass,Shadowed} mode=Free;
    Block* stage=nullptr;DWORD flags=0;bool hasRect=false;RECT rect{};D3DBOX box{};UINT rows=0,slices=1,rowBytes=0,pitch=0,slicePitch=0;
    // A CPU copy of the whole level in our own tight layout (block rows of levelRowBytes), plus slack. Locks return pointers into it
    // and Unlock records the locked rows; shadowDead: never again (GPU-written, readback failed, ...).
    std::vector<unsigned char> shadow;bool shadowOn=false,shadowDead=false;UINT levelRowBytes=0,levelRows=0,levelSlices=0;std::size_t shadowOffset=0;
    // Eviction bookkeeping (StreamCore::texFresh/texRelocked): the shadow may be evicted at any time it is not locked; the level then simply takes
    // the readback path at its next re-lock. relocked: served a second lock (or made by a readback): evicted last. fromFresh: kept from a first write.
    LruNode<SubRes> lru;bool relocked=false,fromFresh=false;   // lru: StreamCore::texFresh (never re-locked) or texRelocked, least recently locked first
    // 0.3.196 (task 12): the list this level's shadow was on when makeRoomForShadow evicted it (None: never shadowed, or shadowed again since); only for the readback-cause counters.
    enum Gone:std::uint8_t{GoneNone,GoneFresh,GoneRelocked,GoneSkipped} gone=GoneNone;
    // 0.3.206 (task 31): Diagnostics: ONLY the id of the level's history in TexReadbackDiag::hists (0 = never recorded); the lock history itself lives in that side table, allocated while Diagnostics is on
    // (memory: sizeof(SubRes) is 184, the inline lock history of the first version made it 248, i.e. -64 bytes per texture level; CPU when Diagnostics is off: one branch per lock).
    std::uint32_t diagId=0;
    // 0.3.206 (task 31): the recorded seq of the last command that writes this level's real content (set at every unlock that records one; ~0 = a GPU write); 0 = none recorded.
    // A readback of the level may run as an express task once replayedSeq() has passed it (and the owner's readySeq): see lockImage.
    std::uint64_t contentSeq=0;
    std::uint64_t lastLockFrame=0;   // StreamCore::frameNo of the shadow's creation or last lock: a fresh keep may be evicted for a new one only once this is kFreshEvictAgeFrames old
};
// 0.3.206 (task 31): TexReadbackDiag copies these constants (it knows nothing of SubRes / the queue)
static_assert(TexReadbackDiag::kGoneNever==SubRes::GoneNone&&TexReadbackDiag::kGoneFresh==SubRes::GoneFresh&&TexReadbackDiag::kGoneRelocked==SubRes::GoneRelocked&&TexReadbackDiag::kGoneSkipped==SubRes::GoneSkipped,"TexReadbackDiag::kGone* = SubRes::Gone");
// What a staged Unlock records (followed by nothing: the bytes are in the command's Block).
struct UnlockImageArgs {ProxyBase* proxy;UINT level,face;DWORD flags;UINT hasRect,rows,slices,rowBytes,pitch,slicePitch;LONG l,t,r,b;UINT bf,bk;UINT route;};
enum ImageRoute:UINT{RouteSurface=0,RouteTexture=1,RouteCube=2,RouteVolumeTexture=3,RouteVolume=4};

inline ProxyBase* particleTextureRoot(ProxyBase* p){if(p&&p->kind==Kind::Surface)p=p->parent;return p&&p->kind==Kind::Texture?p:nullptr;}
inline void invalidateParticleTexture(ProxyBase* p,bool material=true){if(auto* root=particleTextureRoot(p)){NorthlightParticleTextures::cache().invalidate(root->raw);NorthlightTreeCanopy::forget(root->raw);if(material)NorthlightMaterialTextures::cache().invalidate(root->raw);}}
inline void captureParticleTexture(const UnlockImageArgs& a,const unsigned char* src,bool success){
    auto* root=particleTextureRoot(a.proxy);if(!root||a.flags&D3::kLockReadOnly)return;
    // Only immutable creation metadata is read here, on the replay thread.
    unsigned level=a.level;if(a.proxy!=root){level=0;for(;level<root->info.levels;++level)if(D3::mipDim(root->info.w,level)==a.proxy->info.w&&D3::mipDim(root->info.h,level)==a.proxy->info.h)break;if(level>=root->info.levels){invalidateParticleTexture(root);return;}}
    const unsigned w=D3::mipDim(root->info.w,level),h=D3::mipDim(root->info.h,level);
    const bool whole=a.slices==1&&(!a.hasRect||(a.l==0&&a.t==0&&a.r==LONG(w)&&a.b==LONG(h)));
    if(!(root->info.usage&(D3::kUsageDS|D3::kUsageRT))&&((root->info.usage&D3::kUsageDynamic)||((root->info.usage&D3::kUsageAutoGen)&&root->info.fmt==21)||(root->info.levels==1&&root->info.fmt==21&&root->info.w>=256&&root->info.h>=256))){auto& cache=NorthlightMaterialTextures::cache();if(whole)cache.upload(root->raw,root,w,h,level,root->info.fmt,src,std::size_t(a.rows)*a.pitch,a.pitch,true,success,root->info.pool);else if(a.hasRect&&a.slices==1)cache.updateRect(root->raw,root,w,h,level,root->info.fmt,src,std::size_t(a.rows)*a.pitch,a.pitch,unsigned(a.l),unsigned(a.t),unsigned(a.r),unsigned(a.b),success);else cache.invalidate(root->raw);}
    if(root->info.usage&(D3::kUsageRT|D3::kUsageDS|D3::kUsageAutoGen)){NorthlightParticleTextures::cache().invalidate(root->raw);return;}
    NorthlightParticleTextures::cache().upload(root->raw,root,w,h,level,root->info.fmt,src,std::size_t(a.rows)*a.pitch,a.pitch,whole,success,root->info.pool);
    NorthlightTreeCanopy::upload(root->raw,level,root->info.fmt,w,h,src,a.rows,a.pitch,whole&&success);
}

// Both need StreamCore::texMutex held. A shadow is game-side memory and no queued command refers to it (Unlock copies the rows into a Block).
inline LruList<SubRes>& texListOf(StreamCore& c,const SubRes& s){return s.relocked?c.texRelocked:c.texFresh;}
// 0.3.200 (pipeline): recycle (the game thread's evictions only): the allocation goes to an empty spare slot, or replaces a smaller spare, unless under pressure or too large.
inline void spareTexShadowLocked(StreamCore& c,std::vector<unsigned char>& v){
    const std::size_t cap=v.capacity();if(!cap||cap>StreamCore::kTexSpareMaxBytes||c.q.pressure())return;
    std::vector<unsigned char>* slot=nullptr;
    for(auto& x:c.texSpare)if(!slot||x.capacity()<slot->capacity())slot=&x;   // the empty or smallest slot
    if(slot->capacity()<cap){slot->swap(v);add(c.q.stats.texShadowSpared);}
}
inline void dropShadowLocked(StreamCore& c,SubRes& s,bool recycle=false){
    if(!s.shadowOn)return;
    texListOf(c,s).remove(&s);
    c.q.addTexShadowBytes(-std::int64_t(std::size_t(s.levelRows)*s.levelRowBytes*s.levelSlices));
    if(recycle)spareTexShadowLocked(c,s.shadow);
    std::vector<unsigned char>().swap(s.shadow);s.shadowOn=false;
}
// 0.3.200 (pipeline): game thread, before a level shadow is made: the smallest spare that holds `bytes` without wasting more than half of it moves into `out`
// (its assign() then neither frees nor allocates). Zero-filled as before (assign): a fresh level's unwritten bytes stay deterministic.
inline void takeTexSpare(StreamCore& c,std::vector<unsigned char>& out,std::size_t bytes){
    if(out.capacity()>=bytes)return;
    std::lock_guard<std::mutex> l(c.texMutex);std::vector<unsigned char>* best=nullptr;
    for(auto& x:c.texSpare)if(x.capacity()>=bytes&&x.capacity()-bytes<=bytes/2&&(!best||x.capacity()<best->capacity()))best=&x;
    if(best){out.swap(*best);std::vector<unsigned char>().swap(*best);add(c.q.stats.texShadowSpareReuses);}
}
inline void trimTexSpares(StreamCore& c){std::lock_guard<std::mutex> l(c.texMutex);for(auto& x:c.texSpare)std::vector<unsigned char>().swap(x);}   // game thread (pressure)
inline void dropSubShadow(StreamCore& c,SubRes& s){std::lock_guard<std::mutex> l(c.texMutex);dropShadowLocked(c,s);}
inline void dropSubStage(StreamCore& c,SubRes& s){if(c.idleImages.enabled())c.idleImages.retire(std::uint64_t(reinterpret_cast<std::uintptr_t>(&s)));if(s.stage){c.q.freeBlock(s.stage);s.stage=nullptr;}}   // destroyed while a level is still locked: the staged Block goes back (freeBlock takes the pool mutex)
// Game thread: makes `bytes` fit the texture-shadow cap by evicting least-recently-locked shadows, never-re-locked ones (fresh keeps) first,
// never one that is locked now or `keep`. mayEvictRelocked=false leaves the re-locked ones alone (no caller: 0.3.196 (task 12): a fresh keep no longer evicts anything,
// it is taken only into free room, see lockImage). false = still no room.
// The victim is the first eligible entry of the LRU list (fresh list, then the re-locked one): O(victims), not a rescan of every level.
inline bool makeRoomForShadow(StreamCore& c,std::size_t bytes,bool mayEvictRelocked,const SubRes* keep){
    while(!c.q.texShadowAdmit(bytes)){
        std::lock_guard<std::mutex> l(c.texMutex);
        SubRes* victim=nullptr;
        for(SubRes* s=c.texFresh.head;s&&!victim;s=s->lru.next)if(s!=keep&&s->mode!=SubRes::Shadowed)victim=s;
        if(!victim&&mayEvictRelocked)for(SubRes* s=c.texRelocked.head;s&&!victim;s=s->lru.next)if(s!=keep&&s->mode!=SubRes::Shadowed)victim=s;
        if(!victim)return false;
        victim->gone=victim->relocked?SubRes::GoneRelocked:SubRes::GoneFresh;   // which list it was on: the cause of the readback its next write lock costs
        dropShadowLocked(c,*victim,true);add(c.q.stats.texShadowEvicted);   // 0.3.200 (pipeline): the allocation may serve the next shadow
    }
    return true;
}
// 0.3.196 (task 12): a FRESH keep may evict never-re-locked shadows, but only ones not locked for kFreshEvictAgeFrames Presents (the list head is the oldest, so a young head ends it): the
// cap never freezes on stale write-once levels, and a level written and re-locked within the window keeps its shadow. false = skip the keep.
// Chosen over "fresh keeps only into free room": that rule left the cap full of stale write-once keeps, so a newly written level that is locked again paid a SyncLock
// readback (a queue drain) every time (0 -> 1 per level in tests). The age limit bounds the keep churn to about one cap turnover per 60 Presents instead of removing it.
constexpr std::uint64_t kFreshEvictAgeFrames=60;
inline bool makeRoomForFreshKeep(StreamCore& c,std::size_t bytes,const SubRes* keep){
    while(!c.q.texShadowAdmit(bytes)){
        std::lock_guard<std::mutex> l(c.texMutex);
        SubRes* victim=nullptr;
        for(SubRes* s=c.texFresh.head;s&&!victim;s=s->lru.next)if(s!=keep&&s->mode!=SubRes::Shadowed)victim=s;
        if(!victim||c.frameNo-victim->lastLockFrame<kFreshEvictAgeFrames)return false;
        victim->gone=SubRes::GoneFresh;dropShadowLocked(c,*victim,true);add(c.q.stats.texShadowEvicted);   // 0.3.200 (pipeline): the fresh keep that evicted it usually takes this allocation
    }
    return true;
}
inline void listShadow(StreamCore& c,SubRes& s){std::lock_guard<std::mutex> l(c.texMutex);s.lastLockFrame=c.frameNo;texListOf(c,s).pushBack(&s);}
// Game thread: the level's shadow was locked again: most recently used now; the first re-lock moves it to the re-locked list (evicted last).
inline void touchTexShadow(StreamCore& c,SubRes& s,bool becomesRelocked){
    if(!becomesRelocked&&texListOf(c,s).isTail(&s)){s.lastLockFrame=c.frameNo;return;}
    std::lock_guard<std::mutex> l(c.texMutex);s.lastLockFrame=c.frameNo;
    texListOf(c,s).remove(&s);if(becomesRelocked)s.relocked=true;texListOf(c,s).pushBack(&s);
}
// A GPU-side write (StretchRect, UpdateTexture, ColorFill, mip generation, ...): the CPU copy is stale for good.
inline void markGpuWritten(StreamCore& c,SubRes& s){s.written=true;s.contentSeq=~std::uint64_t(0);dropSubShadow(c,s);s.shadowDead=true;}

// ---- image locks (textures, cube faces, volume levels, standalone surfaces) ----
inline HRESULT callLock(UINT route,IUnknown* in,UINT level,UINT face,D3DLOCKED_RECT* lr,D3DLOCKED_BOX* lb,const RECT* rect,const D3DBOX* box,DWORD flags){
    switch(route){
    case RouteSurface:return static_cast<IDirect3DSurface9*>(in)->LockRect(lr,rect,flags);
    case RouteTexture:return static_cast<IDirect3DTexture9*>(in)->LockRect(level,lr,rect,flags);
    case RouteCube:return static_cast<IDirect3DCubeTexture9*>(in)->LockRect((D3DCUBEMAP_FACES)face,level,lr,rect,flags);
    case RouteVolumeTexture:return static_cast<IDirect3DVolumeTexture9*>(in)->LockBox(level,lb,box,flags);
    default:return static_cast<IDirect3DVolume9*>(in)->LockBox(lb,box,flags);
    }
}
inline HRESULT callUnlock(UINT route,IUnknown* in,UINT level,UINT face){
    switch(route){
    case RouteSurface:return static_cast<IDirect3DSurface9*>(in)->UnlockRect();
    case RouteTexture:return static_cast<IDirect3DTexture9*>(in)->UnlockRect(level);
    case RouteCube:return static_cast<IDirect3DCubeTexture9*>(in)->UnlockRect((D3DCUBEMAP_FACES)face,level);
    case RouteVolumeTexture:return static_cast<IDirect3DVolumeTexture9*>(in)->UnlockBox(level);
    default:return static_cast<IDirect3DVolume9*>(in)->UnlockBox();
    }
}
constexpr std::size_t kMaxStagedImage=std::size_t(8)<<20;
inline void noteAtlasUpload(const UnlockImageArgs& a,const unsigned char* src,HRESULT result){
    if(!NorthlightAtlasProof::enabled.load(std::memory_order_relaxed))return;
    ProxyBase* p=a.proxy;ProxyBase* root=p->parent?p->parent:p;
    if(root->kind!=Kind::Texture||a.level||p->info.w!=root->info.w||p->info.h!=root->info.h)return;
    const std::size_t bytes=a.rows?std::size_t(a.rows-1)*a.pitch+a.rowBytes:0;
    NorthlightAtlasProof::cache().upload(root->raw,root,root->info.fmt,root->info.w,root->info.h,0,
        unsigned(a.l),unsigned(a.t),unsigned(a.r),unsigned(a.b),src,bytes,a.pitch,SUCCEEDED(result),unsigned(result));
}
// What the replay thread does for a recorded image unlock: lock the real level with the game's flags/rect, copy rows from the
// tight source (rows of rowBytes, `pitch` apart), unlock. Also the synchronous fallback when no Block is available.
inline bool applyImageUnlock(StreamCore& core,const UnlockImageArgs& a,const unsigned char* src){
    ProxyBase* p=a.proxy;
    if(!p->inner||p->dead.load()||!src){captureParticleTexture(a,src,false);add(core.q.stats.replayFailures);return false;}
    D3DLOCKED_RECT lr{};D3DLOCKED_BOX lb{};RECT rect=RECT{a.l,a.t,a.r,a.b};D3DBOX box{};box.Left=UINT(a.l);box.Top=UINT(a.t);box.Right=UINT(a.r);box.Bottom=UINT(a.b);box.Front=a.bf;box.Back=a.bk;
    const bool volume=a.route==RouteVolume||a.route==RouteVolumeTexture;
    const HRESULT locked=callLock(a.route,p->inner,a.level,a.face,&lr,&lb,a.hasRect&&!volume?&rect:nullptr,a.hasRect&&volume?&box:nullptr,a.flags);
    if(FAILED(locked)){captureParticleTexture(a,src,false);noteAtlasUpload(a,src,locked);add(core.q.stats.replayFailures);return false;}
    auto* dst=static_cast<unsigned char*>(volume?lb.pBits:lr.pBits);const INT rowPitch=volume?lb.RowPitch:lr.Pitch;const INT slicePitch=volume?lb.SlicePitch:0;
    if(dst)for(UINT s=0;s<a.slices;++s)for(UINT r=0;r<a.rows;++r)
        std::memcpy(dst+std::ptrdiff_t(s)*slicePitch+std::ptrdiff_t(r)*rowPitch,src+std::size_t(s)*a.slicePitch+std::size_t(r)*a.pitch,a.rowBytes);
    const HRESULT unlockResult=callUnlock(a.route,p->inner,a.level,a.face);const bool success=dst&&SUCCEEDED(unlockResult);
    captureParticleTexture(a,src,success);
    noteAtlasUpload(a,src,success?D3D_OK:(FAILED(unlockResult)?unlockResult:D3DERR_INVALIDCALL));
    if(!success)add(core.q.stats.replayFailures);return success;
}
inline UnlockImageArgs makeUnlockArgs(ProxyBase& self,const SubRes& sub,UINT route,UINT level,UINT face){
    UnlockImageArgs a{};a.proxy=&self;a.level=level;a.face=face;a.flags=sub.flags;a.hasRect=sub.hasRect;a.rows=sub.rows;a.slices=sub.slices;a.rowBytes=sub.rowBytes;a.pitch=sub.pitch;a.slicePitch=sub.slicePitch;
    a.l=sub.rect.left;a.t=sub.rect.top;a.r=sub.rect.right;a.b=sub.rect.bottom;a.bf=sub.box.Front;a.bk=sub.box.Back;a.route=route;return a;
}
constexpr std::size_t kFreshShadowMax=std::size_t(256)<<10;   // a fresh level up to this size keeps its first-write bytes as its shadow
// self: the object the game called (its inner receives the replayed Lock); root: where creation info and `sub` live.
inline HRESULT lockImage(ProxyBase& self,ProxyBase& root,SubRes& sub,UINT route,UINT level,UINT face,UINT lw,UINT lh,UINT ld,
                         D3DLOCKED_RECT* lr,D3DLOCKED_BOX* lb,const RECT* rect,const D3DBOX* box,DWORD flags,void* caller){
    LockScope _ts(*self.core);Queue& q=self.core->q;
    if((!lr&&!lb)||sub.mode!=SubRes::Free)return D3DERR_INVALIDCALL;
    UINT l=0,t=0,r=lw,b=lh,f=0,k=ld;
    if(rect){
        if(rect->left<0||rect->top<0||UINT(rect->right)>lw||UINT(rect->bottom)>lh||rect->left>=rect->right||rect->top>=rect->bottom)return D3DERR_INVALIDCALL;
        l=UINT(rect->left);t=UINT(rect->top);r=UINT(rect->right);b=UINT(rect->bottom);
    }
    if(box){
        if(box->Right>lw||box->Bottom>lh||box->Back>ld||box->Left>=box->Right||box->Top>=box->Bottom||box->Front>=box->Back)return D3DERR_INVALIDCALL;
        l=box->Left;t=box->Top;r=box->Right;b=box->Bottom;f=box->Front;k=box->Back;
    }
    // Use the same actual Lock caller/metadata as TexReadbackDiag. The interval
    // after a successful return belongs to the caller, not LockRect's API cost.
    struct ImageProbe {
        StreamCore& core;SubRes& sub;std::uint64_t id=0;
        ImageProbe(StreamCore& c,SubRes& s,UINT route,UINT level,UINT face,UINT w,UINT h,UINT d,const Info& info,DWORD flags,void* caller):core(c),sub(s){
            if(!core.idleImages.enabled())return;
            IdleImageLock p;p.key=std::uint64_t(reinterpret_cast<std::uintptr_t>(&sub));p.entryNs=nowNs();p.caller=std::uint64_t(reinterpret_cast<std::uintptr_t>(caller));p.frame=core.frameNo;
            p.fmt=info.fmt;p.w=w;p.h=h;p.d=d;p.level=level;p.face=face;p.pool=info.pool;p.usage=info.usage;p.flags=flags;p.route=route;p.epoch=core.queryEpoch.load(std::memory_order_acquire);id=core.idleImages.begin(p);
        }
        ~ImageProbe(){if(id)core.idleImages.returned(id,nowNs(),unsigned(sub.mode));}
    } probe(*self.core,sub,route,level,face,lw,lh,ld,root.info,flags,caller);
    const DWORD eff=effectiveLockFlags(flags,root.info.pool);
    const auto fi=D3::formatInfo(root.info.fmt);
    const bool rtLike=(root.info.usage&(D3::kUsageRT|D3::kUsageDS))!=0,volume=route==RouteVolume||route==RouteVolumeTexture;
    const UINT rows=fi.ok?(b-t+fi.bh-1)/fi.bh:0,rowBytes=fi.ok?((r-l+fi.bw-1)/fi.bw)*fi.bytes:0,slices=k-f;
    // 0.3.206 (task 31): Diagnostics only (core.timing): the lock's record, completed by the readback / pass-through below
    StreamCore& core=*self.core;const bool tm=core.timing;TexReadbackDiag::Hit hit;
    if(tm){
        if(!sub.diagId)sub.diagId=core.texDiag.nextId++;
        TexReadbackDiag::Meta m;m.fmt=root.info.fmt;m.w=lw;m.h=lh;m.d=ld;m.level=level;m.face=face;m.pool=root.info.pool;m.usage=root.info.usage;m.baseW=root.info.w;m.baseH=root.info.h;
        if(fi.ok)m.levelBytes=std::uint64_t((lh+fi.bh-1)/fi.bh)*(((lw+fi.bw-1)/fi.bw)*fi.bytes)*ld;
        const bool whole=l==0&&t==0&&r==lw&&b==lh&&f==0&&k==ld;
        const double cover=double(r-l)*double(b-t)*double(k-f)/(double(lw)*double(lh)*double(ld));
        hit=core.texDiag.lock(sub.diagId,m,whole,cover,core.frameNo,caller);   // the history supplies m.caller = the first lock's (caller=); `caller` is this lock's (rbCaller= if it reads back)
    }
    auto describe=[&]{
        sub.flags=flags;sub.hasRect=rect!=nullptr||box!=nullptr;
        sub.rect.left=LONG(l);sub.rect.top=LONG(t);sub.rect.right=LONG(r);sub.rect.bottom=LONG(b);
        if(box)sub.box=*box;
        sub.rows=rows;sub.slices=slices;sub.rowBytes=rowBytes;sub.pitch=rowBytes;sub.slicePitch=rowBytes*rows;
    };
    // ---- a level shadow: served from CPU memory, Unlock records the locked rows ----
    if(!rtLike&&fi.ok){
        const UINT lrows=(lh+fi.bh-1)/fi.bh,lrowBytes=((lw+fi.bw-1)/fi.bw)*fi.bytes;
        const std::size_t levelBytes=std::size_t(lrows)*lrowBytes*ld;
        bool justMade=false;
        if(!sub.shadowOn&&!sub.shadowDead){
            const bool undefined=!sub.written||(eff&D3::kLockDiscard)!=0;   // nothing to preserve: a zero shadow is as good as the real bytes
            const bool fresh=undefined&&levelBytes<=kFreshShadowMax,readback=!undefined&&!(flags&D3::kLockReadOnly)&&levelBytes<=kMaxStagedImage;
            if(fresh||readback){
                // 0.3.196 (task 12): a FRESH keep (a level WoW writes once and may never lock again) takes free room, or evicts never-re-locked keeps that are at least kFreshEvictAgeFrames
                // old (never a re-locked shadow); otherwise it is skipped: not made, and the lock takes the staged path below (a Block recorded with the same bytes, no sync).
                // A readback may still evict (fresh keeps first, then re-locked ones).
                const bool room=readback?makeRoomForShadow(*self.core,levelBytes,true,&sub):makeRoomForFreshKeep(*self.core,levelBytes,&sub);
                if(!room){
                    if(readback){add(q.stats.texShadowRefused);add(q.stats.texShadowRefusedBytes,levelBytes);}
                    else{add(q.stats.texShadowFreshSkipped);sub.gone=SubRes::GoneSkipped;}
                }
                else{
                    bool ok=true;
                    takeTexSpare(*self.core,sub.shadow,levelBytes+kLockSlack);   // 0.3.200 (pipeline)
                    try{sub.shadow.assign(levelBytes+kLockSlack,0);}catch(...){ok=false;}
                    if(ok&&readback){   // ONE synchronous readback: the replay thread copies the whole real level out under a READONLY lock
                        ok=false;
                        std::uint64_t lockT=0,copyT=0;bool innerGone=false,isExpress=false;const std::uint64_t t0=tm?nowNs():0;   // 0.3.206 (task 31): `tm` was read on the game thread; the task only reads this copy
                        auto body=[&](bool isExpress){
                            if(!self.inner||self.dead.load()){if(isExpress)innerGone=true;return;}
                            D3DLOCKED_RECT r2{};D3DLOCKED_BOX b2{};
                            const std::uint64_t a0=tm?nowNs():0;
                            if(FAILED(callLock(route,self.inner,level,face,&r2,&b2,nullptr,nullptr,D3::kLockReadOnly)))return;
                            const std::uint64_t a1=tm?nowNs():0;
                            const auto* src=static_cast<const unsigned char*>(volume?b2.pBits:r2.pBits);const INT rp=volume?b2.RowPitch:r2.Pitch,sp=volume?b2.SlicePitch:0;
                            if(src){for(UINT z=0;z<ld;++z)for(UINT y=0;y<lrows;++y)std::memcpy(sub.shadow.data()+(std::size_t(z)*lrows+y)*lrowBytes,src+std::ptrdiff_t(z)*sp+std::ptrdiff_t(y)*rp,lrowBytes);ok=true;}
                            const std::uint64_t a2=tm?nowNs():0;
                            callUnlock(route,self.inner,level,face);
                            if(tm){const std::uint64_t a3=nowNs();lockT=(a1-a0)+(a3-a2);copyT=a2-a1;}};
                        // 0.3.206 (task 31): EXPRESS when every command this readback depends on (the owner's and the root's create / derive, the level's last content write) has already been replayed:
                        // the replay thread runs the same body at its next command boundary instead of after the whole queue. Exact: no recorded command can change the bytes any more (the game thread is here).
                        bool ran=false;
                        const std::uint64_t need=std::max(std::max(self.readySeq,root.readySeq),sub.contentSeq);
                        const bool express=!inPumpedWait&&self.readySeq!=0&&root.readySeq!=0&&sub.contentSeq!=0&&need<=q.replayedSeq();
                        ExpressTimes xt;std::uint64_t e0=0;
                        if(express){
                            if(tm)e0=nowNs();
                            q.runExpress([](void* a){(*static_cast<decltype(body)*>(a))(true);},&body,tm?&xt:nullptr);
                            // innerGone (should not happen): the drain cannot help. The express body only saw inner null / dead because every command it depends on (incl. the owner's create) had already
                            // replayed, so runTask's body would find the same and end with ok=false: skip it and take that outcome (ran=true, ok=false -> shadowDead below, no readback counters).
                            ran=true;isExpress=!innerGone;
                        }
                        if(!ran)ran=runTask(*self.core,[&](StreamCore&){body(false);},Cmd::SyncLock);
                        if(!ran)return D3DERR_INVALIDCALL;
                        if(ok&&tm){const std::uint64_t total=nowNs()-t0,busy=lockT+copyT;
                            core.texDiag.readback(hit,levelBytes,std::uint64_t(rows)*rowBytes*slices,unsigned(sub.gone),total>busy?total-busy:0,lockT,copyT,isExpress,xt.behindCmd,isExpress&&xt.startNs>e0?xt.startNs-e0:0);}   // sub.gone is still the cause here
                        if(ok){add(q.stats.texShadowReadbacks);
                            add(sub.gone==SubRes::GoneFresh?q.stats.readbackAfterFreshDrop:sub.gone==SubRes::GoneRelocked?q.stats.readbackAfterRelockedEvict:sub.gone==SubRes::GoneSkipped?q.stats.readbackAfterFreshSkip:q.stats.readbackNeverShadowed);}
                    }else if(ok)add(q.stats.texShadowFresh);
                    if(ok){sub.gone=SubRes::GoneNone;
                        sub.shadowOn=true;sub.levelRowBytes=lrowBytes;sub.levelRows=lrows;sub.levelSlices=ld;q.addTexShadowBytes(std::int64_t(levelBytes));
                        sub.relocked=readback;sub.fromFresh=fresh;listShadow(*self.core,sub);justMade=true;   // a readback is a re-lock by definition: evicted last
                    }
                    else{std::vector<unsigned char>().swap(sub.shadow);sub.shadowDead=true;}   // the real level cannot be read: pass-through as before
                }
            }
        }
        if(sub.shadowOn){
            if(!justMade){const bool first=!sub.relocked;touchTexShadow(*self.core,sub,first);if(first&&sub.fromFresh)add(q.stats.texShadowFreshUseful);}   // a kept first write that was locked again was worth keeping
            describe();sub.mode=SubRes::Shadowed;
            sub.shadowOffset=std::size_t(f)*lrows*lrowBytes+std::size_t(t/fi.bh)*lrowBytes+std::size_t(l/fi.bw)*fi.bytes;
            unsigned char* at=sub.shadow.data()+sub.shadowOffset;
            if(lr){lr->Pitch=INT(lrowBytes);lr->pBits=at;}
            if(lb){lb->RowPitch=INT(lrowBytes);lb->SlicePitch=INT(lrows*lrowBytes);lb->pBits=at;}
            add(q.stats.texShadowHits);own(q.stats.lockAsync);return D3D_OK;
        }
    }
    PassReason why=PassReason::Other;bool candidate=false;
    if(flags&D3::kLockReadOnly)why=PassReason::ReadOnly;
    else if(rtLike)why=PassReason::RenderTarget;
    else if(sub.written&&!(eff&D3::kLockDiscard))why=PassReason::Written;
    else if(!fi.ok)why=PassReason::Format;
    else candidate=true;
    if(candidate){
        const std::size_t total=std::size_t(rows)*rowBytes*slices;
        if(total>kMaxStagedImage)why=PassReason::Size;
        else if(Block* blk=q.tryAllocBlock(total+kLockSlack)){
            describe();sub.mode=SubRes::Staged;sub.stage=blk;
            blk->used=std::uint32_t(total);std::memset(blk->data(),0,total+kLockSlack);   // deterministic bytes for what the game leaves unwritten
            if(lr){lr->Pitch=INT(rowBytes);lr->pBits=blk->data();}
            if(lb){lb->RowPitch=INT(rowBytes);lb->SlicePitch=INT(rowBytes*rows);lb->pBits=blk->data();}
            own(q.stats.lockAsync);return D3D_OK;
        }else why=PassReason::Budget;
    }
    countPass(self,why);
    if(tm)core.texDiag.passSync(hit);
    HRESULT hr=D3DERR_INVALIDCALL;
    const bool ran=runTask(*self.core,[&](StreamCore&){if(self.inner&&!self.dead.load())hr=callLock(route,self.inner,level,face,lr,lb,rect,box,flags);},Cmd::SyncLock);
    if(!ran)return D3DERR_INVALIDCALL;
    if(SUCCEEDED(hr)){sub.mode=SubRes::Pass;sub.flags=flags;}
    return hr;
}
inline HRESULT unlockImage(ProxyBase& self,SubRes& sub,UINT route,UINT level,UINT face){
    LockScope _ts(*self.core);Queue& q=self.core->q;
    if(sub.mode==SubRes::Free)return D3DERR_INVALIDCALL;
    if(self.core->idleImages.enabled())self.core->reportIdleImage(self.core->idleImages.unlock(std::uint64_t(reinterpret_cast<std::uintptr_t>(&sub)),nowNs()));
    const bool volume=route==RouteVolume||route==RouteVolumeTexture;
    const std::uint16_t cmd=(std::uint16_t)(volume?Cmd::UnlockBox:Cmd::UnlockRect);
    if(sub.mode==SubRes::Staged){
        auto* a=static_cast<UnlockImageArgs*>(q.reserveWithBlock(cmd,sizeof(UnlockImageArgs),sub.stage));
        *a=makeUnlockArgs(self,sub,route,level,face);
        q.commit();sub.stage=nullptr;sub.mode=SubRes::Free;sub.written=true;sub.contentSeq=q.recordedSeq();q.publish();return D3D_OK;
    }
    if(sub.mode==SubRes::Shadowed){
        sub.mode=SubRes::Free;
        if(sub.flags&D3::kLockReadOnly)return D3D_OK;
        const std::size_t total=std::size_t(sub.rows)*sub.rowBytes*sub.slices,sliceBytes=std::size_t(sub.levelRows)*sub.levelRowBytes;
        auto gather=[&](unsigned char* out){
            for(UINT z=0;z<sub.slices;++z)for(UINT y=0;y<sub.rows;++y)
                std::memcpy(out+(std::size_t(z)*sub.rows+y)*sub.rowBytes,sub.shadow.data()+sub.shadowOffset+z*sliceBytes+std::size_t(y)*sub.levelRowBytes,sub.rowBytes);};
        sub.pitch=sub.rowBytes;sub.slicePitch=sub.rowBytes*sub.rows;
        if(Block* blk=q.tryAllocBlock(total?total:1)){
            blk->used=std::uint32_t(total);gather(blk->data());
            auto* a=static_cast<UnlockImageArgs*>(q.reserveWithBlock(cmd,sizeof(UnlockImageArgs),blk));*a=makeUnlockArgs(self,sub,route,level,face);q.commit();
        }else{   // no Block: the same rows written synchronously
            std::vector<unsigned char> tmp(total?total:1);gather(tmp.data());const UnlockImageArgs a=makeUnlockArgs(self,sub,route,level,face);
            add(self.core->q.stats.passThrough[unsigned(PassReason::Budget)]);
            if(!runTask(*self.core,[&](StreamCore& c){applyImageUnlock(c,a,tmp.data());},Cmd::SyncUnlock))return D3DERR_INVALIDCALL;
        }
        sub.written=true;sub.contentSeq=q.recordedSeq();q.publish();return D3D_OK;   // Ready uploads can run while the caller decodes the next image.
    }
    HRESULT hr=D3DERR_INVALIDCALL;
    if(!runTask(*self.core,[&](StreamCore&){if(!(sub.flags&D3::kLockReadOnly)){invalidateParticleTexture(&self);if(NorthlightAtlasProof::enabled.load(std::memory_order_relaxed))NorthlightAtlasProof::cache().invalidate((self.parent?self.parent:&self)->raw);}if(self.inner)hr=callUnlock(route,self.inner,level,face);},Cmd::SyncUnlock))return D3DERR_INVALIDCALL;
    sub.mode=SubRes::Free;if(!(sub.flags&D3::kLockReadOnly)){sub.written=true;sub.contentSeq=q.recordedSeq();}
    return hr;
}
