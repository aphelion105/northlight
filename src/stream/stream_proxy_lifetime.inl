// Parent and device pins, ordered destruction, and child creation.
inline void ProxyBase::pinDevice(){if(core->game)core->game->AddRef();}
inline ULONG ProxyBase::comRelease(){
    const LONG n=refs.fetch_sub(1)-1;
    auto* parentToRelease=n==0?parent:nullptr;
    auto* deviceToRelease=!parent&&n==baseline?core->game:nullptr;
    // Publishing Destroy allows the replay thread to delete this proxy immediately.
    useDec();
    if(parentToRelease)parentToRelease->comRelease();
    if(deviceToRelease)deviceToRelease->Release();
    return n<0?0:ULONG(n);
}
inline ProxyBase::~ProxyBase(){dropPrivate();}
inline void ProxyBase::useDec(){
    if(use.fetch_sub(1)!=1||binds.load(std::memory_order_relaxed)!=0)return;
    gone();
}
inline void ProxyBase::gone(){
    if(parent){parent->useDec();return;}
    pendingDestroy.fetch_add(1);
    Queue& q=core->q;
    auto* p=static_cast<ProxyBase**>(q.reserve((std::uint16_t)Cmd::Destroy,sizeof(ProxyBase*)));*p=this;q.commit();
}

// The derive command: the replay thread asks `parent.inner` for a child and stores it in `child.inner`.
enum DeriveOp:UINT{DeriveSurfaceLevel=0,DeriveCubeFace=1,DeriveVolumeLevel=2,DeriveBackBuffer=3};
struct DeriveArgs {ProxyBase* parent;ProxyBase* child;UINT op,a,b;};
inline void recordDerive(StreamCore& c,ProxyBase* parent,ProxyBase* child,UINT op,UINT a,UINT b){
    auto* d=static_cast<DeriveArgs*>(c.q.reserve((std::uint16_t)Cmd::Derive,sizeof(DeriveArgs)));*d=DeriveArgs{parent,child,op,a,b};c.q.commit();child->readySeq=c.q.recordedSeq();
}
