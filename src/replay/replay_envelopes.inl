// Immutable source bounds, reevaluated with this draw's current pose and camera.
// Cheap and heavy evaluation have independent budgets. Cold construction never
// evaluates a pose and cannot spend either evaluation budget.
class EnvelopeCache {
    const bool alignedSkin_;
    using Key=std::shared_ptr<const std::vector<std::uint32_t>>;
    struct Binding {unsigned reg;D3DVERTEXELEMENT9 element;};
    using Bindings=std::shared_ptr<const std::vector<Binding>>;
public:
    class Prepared {
        friend class EnvelopeCache;
        NorthlightActorDeformation::Program program_;
        std::vector<D3DVERTEXELEMENT9> declaration_;
        // Source-independent layout; entries keep it alive after metadata eviction.
        Key key_;Bindings bindings_;
        std::uint64_t hash_=0;
        Prepared()=default;
    public:
        Prepared(const Prepared&)=delete;Prepared& operator=(const Prepared&)=delete;
        size_t bytes()const{return sizeof(Prepared)+declaration_.capacity()*sizeof(D3DVERTEXELEMENT9)+(key_?sizeof(*key_)+key_->capacity()*4+32:0)+
            (bindings_?sizeof(*bindings_)+bindings_->capacity()*sizeof(Binding)+32:0)+program_.operations.capacity()*sizeof(NorthlightActorDeformation::Operation)+program_.inputs.capacity()*sizeof(NorthlightActorDeformation::Input)+program_.definitions.capacity()*sizeof(NorthlightActorDeformation::Definition);}
    };
    enum class Stage {All,Ready,Build,Cheap,Heavy};
    enum class Kind {Unknown,Cold,Cheap,Heavy,Unsupported};
    struct WorkInfo {
        Kind kind=Kind::Unknown;std::uint64_t programHash=0;size_t groups=0,operations=0;double elapsedUs=0;bool skin=false;
    };
    struct Expensive {
        bool valid=false;std::uint64_t frame=0,programHash=0;size_t groups=0,operations=0;double elapsedUs=0;
        Stage stage=Stage::Heavy;Status status=Status::Budget;bool skin=false;
    };
    struct Stats {
        size_t ready=0,skinValid=0,cold=0,buildVertices=0,buildIndices=0,completed=0,buildSkippedReady=0;
        size_t skinBuildVertices=0,typedSkinBuildVertices=0;
        size_t terminalBuildVertices=0;std::uint64_t terminalBuildNanoseconds=0;
        size_t timeDeferred=0,vertexDeferred=0,operationDeferred=0,unsupported=0,tupleLimit=0,invalid=0;
        size_t evictions=0,readyEvictions=0,expiredEvictions=0,releasedGroupBytes=0;
        size_t cheapValid=0,heavyCandidates=0,heavyAttempts=0,heavyValid=0,cheapSliceDeferred=0;
        size_t pendingResumes=0,pendingDeferred=0,pendingOwnerExpired=0,pendingAbandoned=0;
        size_t memoTests=0,memoHits=0,memoMisses=0,memoStored=0,memoEvictions=0,memoRejected=0,memoComparedBytes=0,memoAvoidedOperations=0,memoBudgetDeferred=0;
        size_t memoColdMisses=0,memoReplacedMisses=0,memoCameraMisses=0,memoConstantMisses=0;
        size_t memoBypassed=0,memoStoreBypassed=0,memoProtected=0,memoCopiedBytes=0;
        size_t effectiveCopiedBytes=0,fastValid=0;
        size_t sourceIdentityHits=0,sourceReconstructedHits=0,sharedKeyHits=0,keyComparedBytes=0;
        size_t rigidPeerHits=0;
    };
    struct MemoTotals {
        std::uint64_t tests=0,hits=0,cold=0,replaced=0,camera=0,constants=0;
        std::uint64_t bypassed=0,storeBypassed=0,protectedStores=0,copiedBytes=0;
        void add(const Stats& s){
            tests+=s.memoTests;hits+=s.memoHits;cold+=s.memoColdMisses;replaced+=s.memoReplacedMisses;
            camera+=s.memoCameraMisses;constants+=s.memoConstantMisses;bypassed+=s.memoBypassed;
            storeBypassed+=s.memoStoreBypassed;protectedStores+=s.memoProtected;copiedBytes+=s.memoCopiedBytes;
        }
    };
private:
    struct Group {std::vector<float> limits;}; // low/high for USED input registers only
    struct Entry {
        std::weak_ptr<const NorthlightDrawSnapshot::Mesh> owner;
        const NorthlightDrawSnapshot::Mesh* sourceAddress=nullptr;
        NorthlightSourceIdentity::Handle identity;
        Key key;
        Bindings bindings;
        std::unordered_map<std::string,Group> groups;
        std::unique_ptr<SkinEnvelope> skin;
        size_t vertex=0,index=0,indexCount=0;bool unsupported=false,ready=false;
        std::uint64_t touched=0,programHash=0,lastEvaluationNs=0,serial=0;size_t bytes=0,baseBytes=0;
        unsigned constantFirst=0,constantCount=256,memoSlot=64;
        bool copyDefinitions=true,singleBone=false,rigidPositions=false; /* singleBone resolves its control DEF directly */
        unsigned rigidStride=0;bool rigidPeerTried=false;
    };
    // Rotation filled 2048 entries at only 3.3 MiB. Keep more small immutable
    // sources instead of rebuilding them on return; the byte cap still wins.
    static constexpr size_t MaxGroups=512,Limit=8u*1024u*1024u,MaxEntries=4096;
    static constexpr size_t MemoSlots=64,MemoLimit=256u*1024u;
    struct Memo {std::uint64_t serial=0,lastUsedFrame=0;std::vector<float> key;Bounds bounds;size_t bytes=0;};
    std::vector<Memo> memo_;
    NorthlightReplayMemo::Policy memoPolicy_;
    MemoTotals memoTotals_;
    bool memoProtect_=true;
    size_t memoBytes_=0,memoTableBytes_=0,memoNext_=0,memoEntries_=0;
    std::uint64_t entrySerial_=0;
    static constexpr std::uint64_t HeavyNanoseconds=AdaptiveTimeBudget::Heavy,CheapSliceNanoseconds=40000;
    AdaptiveTimeBudget timeBudget_;bool diagnostics_=true,terminalBuild_=false;
    size_t bytes_=0;
    std::unordered_multimap<std::uint64_t,Entry> entries_;
    std::uint64_t clock_=0,frame_=0,readyNanoseconds_=0,heavyNanoseconds_=0,buildNanoseconds_=0,classifyNanoseconds_=0;
    unsigned readyCalls_=0,heavyCalls_=0,buildCalls_=0;Stats stats_;Expensive expensive_;
    std::vector<std::uint32_t> scratch_;
    // One admitted immutable source build owns the cold turn until completion.
    // Map rehash preserves Entry addresses; erase/clear always release this
    // non-owning pointer first. No geometry owner or evaluated pose is retained.
    Entry* pending_=nullptr;
    std::uint64_t pendingProgressFrame_=0;
    /* FastReady clock batch: the interval since fastMark_ (warm cheap draws and
       the schedule between them) is charged to the ready bank every
       ReadyClockStride draws and before any Timer/lending starts. Gaps are
       charged, never dropped, so the batch only over-counts, by construction. */
    BoundsClock::time_point fastMark_;bool fastOpen_=false;unsigned fastCount_=0;
    static std::uint64_t elapsedNs(BoundsClock::time_point a,BoundsClock::time_point b){return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(a-b).count());}
    void flushFast(BoundsClock::time_point now){if(fastOpen_){readyNanoseconds_+=elapsedNs(now,fastMark_);fastOpen_=false;}}
    /* Owner equivalence without lock(): same control block and a live argument
       proves the same unexpired snapshot; an ABA address has another block. */
    static bool sameOwner(const std::weak_ptr<const NorthlightDrawSnapshot::Mesh>& a,const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& b){return !a.owner_before(b)&&!b.owner_before(a);}
    static const auto& identity(const NorthlightDrawSnapshot::Mesh& mesh){return mesh.replayIdentity?mesh.replayIdentity:mesh.sourceIdentity;}
    static std::uint64_t sourceHash(const NorthlightDrawSnapshot::Mesh& mesh){const auto& id=identity(mesh);return id?id->hash:std::uint64_t(reinterpret_cast<std::uintptr_t>(&mesh));}
    auto findSource(const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const std::vector<std::uint32_t>& key,const std::vector<std::uint32_t>* sharedKey=nullptr){
        const auto& id=identity(*owner);auto range=entries_.equal_range(sourceHash(*owner));
        for(auto it=range.first;it!=range.second;++it){auto& e=it->second;
            // Prepared metadata and every entry retain the same immutable key.
            // Independently prepared/generic calls still compare every word.
            if(e.key.get()==sharedKey)++stats_.sharedKeyHits;
            else {stats_.keyComparedBytes+=key.size()*4;if(*e.key!=key)continue;}
            const bool same=e.sourceAddress==owner.get()&&sameOwner(e.owner,owner);
            if(id?!NorthlightSourceIdentity::same(id,e.identity):e.identity||!same)continue;
            if(id){++stats_.sourceIdentityHits;if(!same){++stats_.sourceReconstructedHits;
                // Only immutable source envelopes survive reconstruction. Pose/
                // camera results must be evaluated again for this live draw.
                releaseMemo(e);e.owner=owner;e.sourceAddress=owner.get();}}
            return it;
        }return entries_.end();
    }
    static constexpr std::uint64_t MaxIdleBuildFrames=8;
    static bool makeKey(const NorthlightActorDeformation::Program& p,const D3DVERTEXELEMENT9* elements,size_t count,std::vector<std::uint32_t>& key){
        if(!detail::linear(p)||!elements||!count||count>MAXD3DDECLLENGTH+1||p.inputs.size()>16||p.definitions.size()>256)return false;
        key.clear();auto word=[&](std::uint32_t n){key.push_back(n);};
        word(p.major);word(p.positionRegister);word(p.textureCoordinates);word(p.skinned);word(unsigned(p.operations.size()));
        for(const auto& op:p.operations){word(op.code);word(op.destination);for(const auto& s:op.source){word(s.token);word(s.address);}}
        word(unsigned(p.inputs.size()));for(const auto& i:p.inputs){word(i.reg);word(i.usage);word(i.index);}
        word(unsigned(p.definitions.size()));for(const auto& d:p.definitions){word(d.reg);for(float f:d.value){std::uint32_t bits;std::memcpy(&bits,&f,4);word(bits);}}
        word(unsigned(count));for(size_t j=0;j<count;++j){const auto& e=elements[j];word(e.Stream);word(e.Offset);word(e.Type);word(e.Method);word(e.Usage);word(e.UsageIndex);}
        return true;
    }
    static std::uint64_t programHash(const NorthlightActorDeformation::Program& p){
        // Diagnostic identity of the sliced program, not a replacement for the
        // exact full program/declaration key and immutable mesh ownership proof.
        std::uint64_t hash=1469598103934665603ull;auto word=[&](std::uint32_t n){hash^=n;hash*=1099511628211ull;};
        word(p.major);word(p.positionRegister);word(p.textureCoordinates);word(p.skinned);word(unsigned(p.operations.size()));
        for(const auto& o:p.operations){word(o.code);word(o.destination);for(const auto& src:o.source){word(src.token);word(src.address);}}
        word(unsigned(p.inputs.size()));for(const auto& i:p.inputs){word(i.reg);word(i.usage);word(i.index);}
        word(unsigned(p.definitions.size()));for(const auto& d:p.definitions){word(d.reg);for(float f:d.value){std::uint32_t bits;std::memcpy(&bits,&f,4);word(bits);}}
        return hash;
    }
    static Bindings prepareBindings(const NorthlightActorDeformation::Program& p,const D3DVERTEXELEMENT9* elements,size_t count){
        auto bindings=std::make_shared<std::vector<Binding>>();bindings->reserve(p.inputs.size());unsigned used=0;
        for(const auto& input:p.inputs){if(input.reg>=16||(used&(1u<<input.reg)))return {};used|=1u<<input.reg;bool found=false;
            for(size_t j=0;j<count;++j){const auto& element=elements[j];if(element.Stream==0xff)break;
                if(element.Usage!=input.usage||element.UsageIndex!=input.index)continue;
                if(found||element.Stream>=4||element.Method!=D3DDECLMETHOD_DEFAULT||!NorthlightDrawSnapshot::declarationBytes(element.Type))return {};
                bindings->push_back({input.reg,element});found=true;
            }if(!found)return {};
        }return bindings;
    }
    static bool prepare(const NorthlightActorDeformation::Program& p,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,size_t count,Entry& e,const Prepared* prepared){
        if(!mesh.vertexCount||mesh.vertexCount>262144||!mesh.primitiveCount||mesh.primitiveCount>87380||
           (mesh.topology!=D3DPT_TRIANGLELIST&&mesh.topology!=D3DPT_TRIANGLESTRIP))return false;
        e.indexCount=mesh.topology==D3DPT_TRIANGLELIST?size_t(mesh.primitiveCount)*3:size_t(mesh.primitiveCount)+2;
        if(mesh.indexed){if(mesh.indices.size()<e.indexCount)return false;}else{if(e.indexCount>mesh.vertexCount)return false;e.index=e.indexCount;}
        e.bindings=prepared?prepared->bindings_:prepareBindings(p,elements,count);
        if(!e.bindings)return false;
        for(const auto& b:*e.bindings){const auto& s=mesh.streams[b.element.Stream];
            if(UINT(b.element.Offset)+NorthlightDrawSnapshot::declarationBytes(b.element.Type)>s.stride||uint64_t(mesh.vertexCount)*s.stride>s.bytes.size())return false;}
        if(SkinEnvelope::supports(p))e.skin=std::make_unique<SkinEnvelope>();
        /* The skin template's own DEF is c0; its evaluator reads only palette
           rows c31..c255. Copying the 3.6 KiB bank then changes no value read. */
        e.copyDefinitions=!p.definitions.empty();
        if(e.skin){e.copyDefinitions=false;for(const auto& d:p.definitions)e.copyDefinitions|=d.reg>=31;}
        // The single-bone evaluator resolves its c0 literal directly; an ordinary
        // palette needs no 4 KiB copy just to replace that control register.
        e.singleBone=p.kernel.valid&&p.kernel.ops.size()==p.operations.size()&&p.kernel.singleSkin();
        if(e.singleBone){
            e.copyDefinitions=false;for(const auto& d:p.definitions)e.copyDefinitions|=d.reg!=0;
        }
        // A superset of EVERY original API constant read by this evaluator.
        // The exact skin template reads palette rows c31..c255. Generic relative
        // addressing retains the full bank; no observed vertex narrows it.
        if(e.skin){e.constantFirst=31;e.constantCount=225;}
        else {
            unsigned first=256,end=0;bool relative=false;
            for(const auto& op:p.operations){const unsigned count=unsigned(NorthlightShadowShader::arithmeticOperands(op.code,p.major)-1);
                for(unsigned j=0;j<count;++j){const auto& src=op.source[j];if(NorthlightShadowShader::regType(src.token)!=2)continue;
                    if(src.token&0x2000){relative=true;continue;}
                    const unsigned reg=NorthlightShadowShader::regIndex(src.token);if(reg>=256)return false;
                    bool defined=false;for(const auto& def:p.definitions)if(def.reg==reg){defined=true;break;}
                    if(!defined){first=std::min(first,reg);end=std::max(end,reg+1);}
                }
            }
            e.constantFirst=relative?0:end?first:0;e.constantCount=relative?256:end?end-first:0;
            // This is exactly the generic builder's one unpartitioned group:
            // FLOAT3 POSITION supplies XYZ and implicit W=1, no palette input
            // or relative address can split it into multiple groups.
            e.rigidPositions=!p.skinned&&!relative&&e.bindings->size()==1&&(*e.bindings)[0].element.Usage==D3DDECLUSAGE_POSITION&&
                (*e.bindings)[0].element.Type==D3DDECLTYPE_FLOAT3;
            if(e.rigidPositions)e.rigidStride=mesh.streams[(*e.bindings)[0].element.Stream].stride;
        }
        return true;
    }
    void releaseMemo(size_t slot){
        auto& memo=memo_[slot];if(!memo.serial)return;
        bytes_-=memo.bytes;memoBytes_-=memo.bytes;--memoEntries_;++stats_.memoEvictions;
        memo.serial=0;memo.lastUsedFrame=0;memo.bytes=0;memo.bounds={};std::vector<float>().swap(memo.key);
    }
    void releaseMemo(const Entry& entry){
        if(entry.memoSlot<memo_.size()&&memo_[entry.memoSlot].serial==entry.serial)releaseMemo(entry.memoSlot);
    }
    void trimMemos(size_t totalExtra,size_t memoExtra=0){
        for(size_t n=0;n<memo_.size()&&(totalExtra>Limit-bytes_||memoExtra>MemoLimit-memoBytes_);++n)
            releaseMemo((memoNext_+n)%memo_.size());
        // Empty optional tables must not displace a source envelope either.
        if(!memoEntries_&&memoTableBytes_&&(totalExtra>Limit-bytes_||memoExtra>MemoLimit-memoBytes_)){
            bytes_-=memoTableBytes_;memoBytes_-=memoTableBytes_;memoTableBytes_=0;std::vector<Memo>().swap(memo_);memoNext_=0;
        }
    }
    bool memoHit(const Entry& entry,const float* constants,const float* inverse,Bounds& output){
        if(!ResultMemo||!memoPolicy_.enabled()){++stats_.memoBypassed;return false;}
        ++stats_.memoTests;
        if(entry.memoSlot<memo_.size()){
            auto& memo=memo_[entry.memoSlot];const size_t values=size_t(entry.constantCount)*4;
            if(memo.serial==entry.serial&&memo.key.size()==values+16){
                memo.lastUsedFrame=frame_;
                // Reject a moving camera before comparing up to 4 KiB of pose.
                // Report the first failed proof; equality still requires BOTH.
                stats_.memoComparedBytes+=16*sizeof(float);
                if(std::memcmp(memo.key.data()+values,inverse,16*sizeof(float)))++stats_.memoCameraMisses;
                else {
                    stats_.memoComparedBytes+=values*sizeof(float);
                    if(std::memcmp(memo.key.data(),constants+4*entry.constantFirst,values*sizeof(float)))++stats_.memoConstantMisses;
                    else {output=memo.bounds;return true;}
                }
            }else ++stats_.memoReplacedMisses;
        }else ++stats_.memoColdMisses;
        ++stats_.memoMisses;return false;
    }
    void remember(Entry& entry,const float* constants,const float* inverse,const Bounds& output){
        if(!output.valid||!entry.serial)return;
        if(!ResultMemo||!memoPolicy_.enabled()){++stats_.memoStoreBypassed;return;}
        // Optional memo failure cannot discard a successfully computed bound.
        try {
            if(memo_.empty()){
                const size_t table=MemoSlots*sizeof(Memo)+32;
                if(table>Limit-bytes_){++stats_.memoRejected;return;}
                memo_.resize(MemoSlots);memoTableBytes_=memo_.capacity()*sizeof(Memo)+32;
                bytes_+=memoTableBytes_;memoBytes_+=memoTableBytes_;
            }
            const size_t values=size_t(entry.constantCount)*4,keyBytes=(values+16)*sizeof(float)+32;
            size_t slot=entry.memoSlot<memo_.size()&&memo_[entry.memoSlot].serial==entry.serial?entry.memoSlot:memoNext_++%memo_.size();
            // A long sequential scan must not evict the useful working set on
            // every frame. Probe ONE rotating victim, in constant time. A new
            // source may replace it only after an entire frame without use.
            // Capacity trimming may still reclaim any optional memo first.
            if(memoProtect_&&memo_[slot].serial&&memo_[slot].serial!=entry.serial&&frame_-memo_[slot].lastUsedFrame<=1){
                ++stats_.memoProtected;return;
            }
            // A miss usually changes the pose, not key size. Reuse the bounded
            // slot allocation instead of allocating/freeing a bank per draw.
            if(memo_[slot].serial&&memo_[slot].key.capacity()>=values+16){
                auto& memo=memo_[slot];if(memo.serial!=entry.serial)++stats_.memoEvictions;
                memo.key.resize(values+16);
                if(values)std::memcpy(memo.key.data(),constants+4*entry.constantFirst,values*sizeof(float));
                std::memcpy(memo.key.data()+values,inverse,16*sizeof(float));
                memo.bounds=output;memo.serial=entry.serial;memo.lastUsedFrame=frame_;entry.memoSlot=unsigned(slot);
                ++stats_.memoStored;stats_.memoCopiedBytes+=(values+16)*sizeof(float);return;
            }
            releaseMemo(slot);trimMemos(keyBytes,keyBytes);
            // trimMemos may free an empty table under severe source pressure.
            if(memo_.empty()||keyBytes>Limit-bytes_||keyBytes>MemoLimit-memoBytes_){++stats_.memoRejected;return;}
            std::vector<float> key(values+16);
            if(values)std::memcpy(key.data(),constants+4*entry.constantFirst,values*sizeof(float));
            std::memcpy(key.data()+values,inverse,16*sizeof(float));
            const size_t actual=key.capacity()*sizeof(float)+32;
            if(actual>Limit-bytes_||actual>MemoLimit-memoBytes_){++stats_.memoRejected;return;}
            auto& memo=memo_[slot];memo.key=std::move(key);memo.bounds=output;memo.serial=entry.serial;memo.bytes=actual;
            memo.lastUsedFrame=frame_;stats_.memoCopiedBytes+=(values+16)*sizeof(float);
            bytes_+=actual;memoBytes_+=actual;++memoEntries_;entry.memoSlot=unsigned(slot);++stats_.memoStored;
        }catch(...){++stats_.memoRejected;}
    }
    void discardGroups(Entry& e){
        if(pending_==&e)pending_=nullptr;releaseMemo(e);
        const size_t released=e.bytes-e.baseBytes;std::unordered_map<std::string,Group>().swap(e.groups);e.skin.reset();
        e.bytes=e.baseBytes;bytes_-=released;stats_.releasedGroupBytes+=released;e.unsupported=true;e.ready=false;
    }
    bool evict(const Entry* protectedEntry=nullptr){
        auto oldest=entries_.end();int best=99;
        for(auto it=entries_.begin();it!=entries_.end();++it){auto& e=it->second;if(&e==protectedEntry||&e==pending_)continue;
            const int priority=!e.identity&&e.owner.expired()?0:e.unsupported?1:!e.ready?2:3;
            if(oldest==entries_.end()||priority<best||(priority==best&&e.touched<oldest->second.touched)){oldest=it;best=priority;if(priority==0)break;}
        }
        if(oldest!=entries_.end()){++stats_.evictions;stats_.readyEvictions+=oldest->second.ready;stats_.expiredEvictions+=oldest->second.owner.expired();releaseMemo(oldest->second);bytes_-=oldest->second.bytes;entries_.erase(oldest);return true;}
        return false;
    }
    struct Timer {
        using Clock=BoundsClock;
        EnvelopeCache& cache;bool building;bool heavy=false,sliced=false;
        Clock::time_point start=Clock::now(),checked=start,sliceStart=start,evaluationStart=start;
        Timer(EnvelopeCache& owner,bool build,bool expensive=false):cache(owner),building(build),heavy(expensive){cache.flushFast(start);}
        Entry* evaluated=nullptr;WorkInfo* info=nullptr;Stage stage=Stage::All;Status* result=nullptr;size_t groups=0,operations=0;
        static std::uint64_t delta(Clock::time_point a,Clock::time_point b){return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(a-b).count());}
        std::uint64_t& bank(){return building?cache.buildNanoseconds_:heavy?cache.heavyNanoseconds_:cache.readyNanoseconds_;}
        void charge(Clock::time_point now){bank()+=delta(now,start);start=checked=now;}
        void mode(bool next){if(next!=building){charge(Clock::now());building=next;}}
        void slice(){sliced=true;sliceStart=checked;}
        bool sliceExpired()const{return sliced&&delta(checked,sliceStart)>=CheapSliceNanoseconds;}
        bool expired(){checked=Clock::now();return sliceExpired()||bank()+delta(checked,start)>=(building?cache.timeBudget_.buildLimit:heavy?HeavyNanoseconds:cache.timeBudget_.cheapLimit);}
        void evaluation(Entry& e,WorkInfo* metadata,Stage phase,Status& status,size_t groupCount,size_t operationCount){
            evaluated=&e;info=metadata;stage=phase;result=&status;groups=groupCount;operations=operationCount;evaluationStart=checked;
        }
        ~Timer(){
            const auto now=Clock::now();
            if(evaluated){const auto ns=delta(now,evaluationStart);evaluated->lastEvaluationNs=ns;
                if(cache.diagnostics_){if(info)info->elapsedUs=double(ns)/1000;
                    if(stage==Stage::Heavy||(stage==Stage::Cheap&&*result==Status::Budget))
                        cache.expensive_={true,cache.frame_,evaluated->programHash,groups,operations,double(ns)/1000,stage,*result,bool(evaluated->skin)};
                }
            }
            charge(now);
        }
    };
    bool reuseRigidPeer(Entry& e,const NorthlightDrawSnapshot::Mesh& mesh,Budget& budget,Timer& timer){
        if(!e.rigidPositions||e.vertex||!e.groups.empty()||e.rigidPeerTried)return false;
        e.rigidPeerTried=true;
        // A missed optimization must leave source construction able to progress.
        // Try once, visit at most eight peers, and reserve the copy separately.
        if(budget.maxOperations-budget.operations<16)return false;
        const auto range=entries_.equal_range(sourceHash(mesh));const auto& a=(*e.bindings)[0].element;
        unsigned visited=0;
        for(auto it=range.first;it!=range.second&&visited<8;++it,++visited){
            if(timer.expired()||budget.operations>=budget.maxOperations)return false;
            ++budget.operations;const auto& peer=it->second;
            if(&peer==&e||!peer.ready||!peer.rigidPositions||peer.groups.size()!=1||peer.rigidStride!=e.rigidStride||peer.vertex!=mesh.vertexCount||peer.indexCount!=e.indexCount)continue;
            const auto& b=(*peer.bindings)[0].element;
            if(a.Stream!=b.Stream||a.Offset!=b.Offset||a.Type!=b.Type||a.Method!=b.Method||a.Usage!=b.Usage||a.UsageIndex!=b.UsageIndex)continue;
            const bool same=e.sourceAddress==peer.sourceAddress&&!e.owner.owner_before(peer.owner)&&!peer.owner.owner_before(e.owner);
            if(e.identity?!NorthlightSourceIdentity::same(e.identity,peer.identity):peer.identity||!same)continue;
            if(budget.maxOperations-budget.operations<8)return false;
            // Only immutable input XYZ limits are shared. Each draw still runs
            // its own position program with its current constants and camera.
            Group group;group.limits=peer.groups.begin()->second.limits;const std::string key;
            const size_t added=sizeof(Group)+group.limits.capacity()*4+key.capacity()+1+192;
            trimMemos(added);while(entries_.size()>1&&bytes_+added>Limit){if(!evict(&e))break;}if(bytes_+added>Limit)return false;
            e.groups.emplace(key,std::move(group));e.bytes+=added;bytes_+=added;budget.operations+=8;
            e.vertex=mesh.vertexCount;e.index=e.indexCount;e.ready=true;pending_=nullptr;++stats_.completed;++stats_.rigidPeerHits;return true;
        }
        return false;
    }
    Status buildEntry(Entry& e,const NorthlightDrawSnapshot::Mesh& mesh,Budget& budget,Timer& timer){
        if(reuseRigidPeer(e,mesh,budget,timer))return Status::Valid;
        // Budget interruptions retain only partial SOURCE envelopes. They can
        // never set a draw's valid flag; evaluation still uses its current pose.
        struct Progress {
            EnvelopeCache& cache;Entry& entry;size_t index,vertex;bool skin;
            ~Progress(){if(skin)cache.stats_.skinBuildVertices+=entry.vertex-vertex;
                if(cache.pending_==&entry&&(entry.index!=index||entry.vertex!=vertex))cache.pendingProgressFrame_=cache.frame_;}
        } progress{*this,e,e.index,e.vertex,bool(e.skin)};
        while(e.index<e.indexCount){if(timer.expired())return timeBudget();
            if(budget.operations>=budget.maxOperations){++stats_.operationDeferred;return Status::Budget;}
            const size_t end=std::min(e.indexCount,e.index+64);
#if defined(__SSE2__) && !defined(NORTHLIGHT_REPLAY_BOUNDS_SCALAR_INDICES)
            // Four unsigned indices share one vector range predicate. Charge
            // predicates, not lanes; time/vertex limits remain independent.
            const size_t vectors=(end-e.index)/4,tail=(end-e.index)%4;
            const size_t operations=vectors+tail;
            if(operations>budget.maxOperations-budget.operations){++stats_.operationDeferred;return Status::Budget;}
            budget.operations+=operations;stats_.buildIndices+=end-e.index;
            const auto bias=_mm_set1_epi32(int(0x80000000u));
            const auto maximum=_mm_set1_epi32(int((mesh.vertexCount-1)^0x80000000u));
            for(size_t vector=0;vector<vectors;++vector,e.index+=4){
                const auto values=_mm_loadu_si128(reinterpret_cast<const __m128i*>(mesh.indices.data()+e.index));
                if(_mm_movemask_epi8(_mm_cmpgt_epi32(_mm_xor_si128(values,bias),maximum))){discardGroups(e);++stats_.invalid;return Status::Invalid;}
            }
#else
            if(end-e.index>budget.maxOperations-budget.operations){++stats_.operationDeferred;return Status::Budget;}
            budget.operations+=end-e.index;stats_.buildIndices+=end-e.index;
#endif
            for(;e.index<end;++e.index)if(mesh.indices[e.index]>=mesh.vertexCount){discardGroups(e);++stats_.invalid;return Status::Invalid;}
        }
        if(e.rigidPositions){
            // Scan the same immutable positions in bounded chunks. The shared
            // scanner preserves scalar min/max ties and rejects non-finite
            // values; only the generic per-vertex decode/hash work is removed.
            const auto& binding=(*e.bindings)[0];const auto& source=mesh.streams[binding.element.Stream];
            while(e.vertex<mesh.vertexCount){
                if(budget.vertices>=budget.maxVertices){++stats_.vertexDeferred;return Status::Budget;}
                if(timer.expired())return timeBudget();
                const size_t count=std::min({size_t(BuildClockStride),size_t(mesh.vertexCount)-e.vertex,budget.maxVertices-budget.vertices});
                float low[3],high[3];
                if(!NorthlightPositionBounds::scanPositions(source.bytes.data()+e.vertex*source.stride+binding.element.Offset,count,source.stride,low,high)){
                    discardGroups(e);++stats_.invalid;return Status::Invalid;}
                if(e.groups.empty()){
                    Group g;g.limits.resize(8);const std::string key;
                    const size_t added=sizeof(Group)+g.limits.capacity()*4+key.capacity()+1+192;
                    trimMemos(added);while(entries_.size()>1&&bytes_+added>Limit){if(!evict(&e))break;}if(bytes_+added>Limit)return Status::Budget;
                    for(unsigned j=0;j<3;++j){g.limits[j]=low[j];g.limits[4+j]=high[j];}g.limits[3]=g.limits[7]=1;
                    e.groups.emplace(key,std::move(g));e.bytes+=added;bytes_+=added;
                }else{auto& limits=e.groups.begin()->second.limits;
                    for(unsigned j=0;j<3;++j){limits[j]=std::min(limits[j],low[j]);limits[4+j]=std::max(limits[4+j],high[j]);}}
                e.vertex+=count;budget.vertices+=count;stats_.buildVertices+=count;
            }
        }
        if(e.skin){
            // The proven skin template reads only v0/v1/v2. Resolve its layout
            // once per source chunk; other declarations retain the decoder.
            const Binding* binding[3]={};
            for(const auto& b:*e.bindings)binding[b.reg]=&b;
            if(binding[0]->element.Type==D3DDECLTYPE_FLOAT3&&binding[2]->element.Type==D3DDECLTYPE_UBYTE4&&
               (binding[1]->element.Type==D3DDECLTYPE_FLOAT4||binding[1]->element.Type==D3DDECLTYPE_UBYTE4N)){
                const std::uint8_t* first[3];size_t stride[3];
                for(unsigned i=0;i<3;++i){const auto& b=binding[i]->element;const auto& s=mesh.streams[b.Stream];first[i]=s.bytes.data()+b.Offset;stride[i]=s.stride;}
                const auto scan=[&](auto readWeights,auto normalizedWeights){
                    for(unsigned sinceCheck=0;e.vertex<mesh.vertexCount;++sinceCheck){
                        if(budget.vertices>=budget.maxVertices){++stats_.vertexDeferred;return Status::Budget;}
                        if((sinceCheck%BuildClockStride)==0&&timer.expired())return timeBudget();
                        NorthlightActorDeformation::Four inputs[3];
                        std::memcpy(inputs[0].data(),first[0]+e.vertex*stride[0],12);inputs[0][3]=1;
                        readWeights(first[1]+e.vertex*stride[1],inputs[1]);
                        const auto* indices=first[2]+e.vertex*stride[2];for(unsigned j=0;j<4;++j)inputs[2][j]=float(indices[j]);
                        // Preserve Invalid (nonfinite decode) vs Unsupported
                        // (skin constraints), including the generic fallback.
                        for(unsigned i=0;i<(decltype(normalizedWeights)::value?1u:2u);++i)for(float f:inputs[i])if(!std::isfinite(f)){discardGroups(e);++stats_.invalid;return Status::Invalid;}
                        if(!e.skin->template add<true,decltype(normalizedWeights)::value>(inputs)){discardGroups(e);++stats_.unsupported;return Status::Unsupported;}
                        ++e.vertex;++budget.vertices;++stats_.buildVertices;
                    }
                    return Status::Valid;
                };
                const auto firstVertex=e.vertex;
                const auto status=binding[1]->element.Type==D3DDECLTYPE_FLOAT4?
                    scan([](const std::uint8_t* p,NorthlightActorDeformation::Four& v){std::memcpy(v.data(),p,16);},std::false_type{}):
                    scan([](const std::uint8_t* p,NorthlightActorDeformation::Four& v){for(unsigned j=0;j<4;++j)v[j]=p[j]*(1.f/255.f);},std::true_type{});
                stats_.typedSkinBuildVertices+=e.vertex-firstVertex;
                if(status!=Status::Valid)return status;
            }
        }
        const Binding* singleBindings[2]={};
        if(e.singleBone&&e.bindings->size()==2)for(const auto& b:*e.bindings)if(b.reg<2)singleBindings[b.reg]=&b;
        const bool typedSingle=singleBindings[0]&&singleBindings[1]&&singleBindings[0]->element.Type==D3DDECLTYPE_FLOAT3&&
            singleBindings[0]->element.Usage==0&&singleBindings[1]->element.Type==D3DDECLTYPE_UBYTE4&&singleBindings[1]->element.Usage==2;
        const std::uint8_t* singleSource[2]={};size_t singleStride[2]={};
        if(typedSingle)for(unsigned i=0;i<2;++i){const auto& b=singleBindings[i]->element;const auto& s=mesh.streams[b.Stream];singleSource[i]=s.bytes.data()+b.Offset;singleStride[i]=s.stride;}
        Group* singleGroups[256];if(typedSingle)std::fill_n(singleGroups,256,nullptr);
        for(unsigned sinceCheck=0;e.vertex<mesh.vertexCount;++sinceCheck){
            if(budget.vertices>=budget.maxVertices){++stats_.vertexDeferred;return Status::Budget;}
            if((sinceCheck%BuildClockStride)==0&&timer.expired())return timeBudget();NorthlightActorDeformation::Four inputs[16];std::string groupKey;unsigned singleIndex=0;
            if(typedSingle){
                std::memcpy(inputs[0].data(),singleSource[0]+e.vertex*singleStride[0],12);inputs[0][3]=1;
                for(float f:inputs[0])if(!std::isfinite(f)){discardGroups(e);++stats_.invalid;return Status::Invalid;}
                const auto* indices=singleSource[1]+e.vertex*singleStride[1];for(unsigned j=0;j<4;++j)inputs[1][j]=float(indices[j]);
                singleIndex=indices[0];
            }else for(const auto& binding:*e.bindings){const auto& s=mesh.streams[binding.element.Stream];auto& input=inputs[binding.reg];
                if(!NorthlightActorDeformation::decodeElement(s.bytes.data()+e.vertex*s.stride+binding.element.Offset,binding.element.Type,input)){discardGroups(e);++stats_.invalid;return Status::Invalid;}
                for(float f:input)if(!std::isfinite(f)){discardGroups(e);++stats_.invalid;return Status::Invalid;}
                // Audited single-bone programs address only v1.x. Unused YZW
                // must not split one bone into hundreds of cold groups.
                if(!e.skin&&binding.element.Usage==2)groupKey.append(reinterpret_cast<const char*>(input.data()),
                    e.singleBone&&binding.reg==1?4:16);
            }
            if(e.skin){if(!e.skin->add(inputs)){discardGroups(e);++stats_.unsupported;return Status::Unsupported;}}
            else {
                Group* group=typedSingle?singleGroups[singleIndex]:nullptr;bool addedGroup=false;
                if(!group){
                    if(typedSingle)groupKey.append(reinterpret_cast<const char*>(inputs[1].data()),4);
#ifdef NORTHLIGHT_TEST_ENVELOPE_GROUP_LOOKUP
                    NORTHLIGHT_TEST_ENVELOPE_GROUP_LOOKUP();
#endif
                    auto found=e.groups.find(groupKey);
                    if(found==e.groups.end()){
                        if(e.groups.size()>=MaxGroups){discardGroups(e);++stats_.tupleLimit;++stats_.unsupported;return Status::Unsupported;}
                        Group g;g.limits.resize(e.bindings->size()*8);const size_t added=sizeof(Group)+g.limits.capacity()*4+groupKey.capacity()+1+192;
                        trimMemos(added);while(entries_.size()>1&&bytes_+added>Limit){if(!evict(&e))break;}if(bytes_+added>Limit)return Status::Budget;
                        for(size_t i=0;i<e.bindings->size();++i)for(unsigned j=0;j<4;++j)g.limits[i*8+j]=g.limits[i*8+4+j]=inputs[(*e.bindings)[i].reg][j];
                        found=e.groups.emplace(std::move(groupKey),std::move(g)).first;e.bytes+=added;bytes_+=added;addedGroup=true;
                    }
                    group=&found->second;
                }
                if(!addedGroup)for(size_t i=0;i<e.bindings->size();++i)for(unsigned j=0;j<4;++j){auto& g=*group;const float f=inputs[(*e.bindings)[i].reg][j];g.limits[i*8+j]=std::min(g.limits[i*8+j],f);g.limits[i*8+4+j]=std::max(g.limits[i*8+4+j],f);}
                // Node references survive rehash; this local proof ends with
                // the source chunk, before any source can change or expire.
                if(typedSingle)singleGroups[singleIndex]=group;
            }
            ++e.vertex;++budget.vertices;++stats_.buildVertices;
        }
        e.ready=true;pending_=nullptr;++stats_.completed;
        return Status::Valid;
    }
    Status timeBudget(){++stats_.timeDeferred;return Status::Budget;}
    void resumePendingBuild(Budget& budget){
        ++buildCalls_;Timer timer{*this,true};
        auto owner=pending_->owner.lock();
        if(!owner){pending_=nullptr;++stats_.pendingOwnerExpired;return;}
        ++stats_.pendingResumes;
        try{pending_->touched=++clock_;(void)buildEntry(*pending_,*owner,budget,timer);}
        catch(...){pending_=nullptr;++stats_.pendingAbandoned;}
    }
public:
    explicit EnvelopeCache(bool alignedSkin=false):alignedSkin_(alignedSkin){}
    EnvelopeCache(const EnvelopeCache&)=delete;EnvelopeCache& operator=(const EnvelopeCache&)=delete;
    EnvelopeCache(EnvelopeCache&&)=delete;EnvelopeCache& operator=(EnvelopeCache&&)=delete;
    void clear(){pending_=nullptr;pendingProgressFrame_=0;std::vector<Memo>().swap(memo_);memoBytes_=memoTableBytes_=memoNext_=memoEntries_=0;memoPolicy_={};memoTotals_={};entrySerial_=0;entries_.clear();scratch_.clear();bytes_=0;clock_=frame_=0;expensive_={};stats_={};beginFrame();}
    void beginFrame(bool diagnostics=true,size_t packets=0){
        fastOpen_=false;fastCount_=0;
        memoTotals_.add(stats_);
        memoPolicy_.beginFrame(stats_.memoTests,stats_.memoHits);
        memoProtect_=true;
        diagnostics_=diagnostics;terminalBuild_=false;timeBudget_.reset(packets);++frame_;readyNanoseconds_=heavyNanoseconds_=buildNanoseconds_=classifyNanoseconds_=0;readyCalls_=heavyCalls_=buildCalls_=0;stats_={};
        if(pending_&&pending_->owner.expired()){pending_=nullptr;++stats_.pendingOwnerExpired;}
        else if(pending_&&frame_-pendingProgressFrame_>MaxIdleBuildFrames){pending_=nullptr;++stats_.pendingAbandoned;}
    }
    // Call before the ordinary cold scan, using the SAME frame build budget.
    // Continuing is independent of packet cursor/classification and never reads
    // a pose, evaluates bounds, or consumes cheap/heavy evaluation allowances.
    void continuePendingBuild(Budget& budget){
        if(!pending_||!canBuild()||budget.vertices>=budget.maxVertices||budget.operations>=budget.maxOperations)return;
        resumePendingBuild(budget);
    }
    // Evaluation has finished. Warm only the already admitted immutable source
    // with the actual unspent total; never publish a pose or admit another one.
    void continuePendingAfterReady(Budget& budget){
        if(!timeBudget_.reservedClosed||!pending_||buildCalls_>=2048||budget.vertices>=budget.maxVertices||budget.operations>=budget.maxOperations)return;
        settleClock();
        const auto limit=readyNanoseconds_>=timeBudget_.totalLimit||heavyNanoseconds_>=timeBudget_.totalLimit-readyNanoseconds_?
            0:timeBudget_.totalLimit-readyNanoseconds_-heavyNanoseconds_;
        if(buildNanoseconds_>=limit)return;
        timeBudget_.cheapLimit=readyNanoseconds_;timeBudget_.buildLimit=limit;
        const auto vertices=budget.vertices;const auto ns=buildNanoseconds_;
        resumePendingBuild(budget);
        stats_.terminalBuildVertices+=budget.vertices-vertices;stats_.terminalBuildNanoseconds+=buildNanoseconds_-ns;
    }
    void terminalCheap(){
        settleClock();timeBudget_.cheapLimit=heavyNanoseconds_>=timeBudget_.totalLimit||buildNanoseconds_>=timeBudget_.totalLimit-heavyNanoseconds_?
            0:timeBudget_.totalLimit-heavyNanoseconds_-buildNanoseconds_;
    }
    template<class Work> bool terminalBuild(Budget& budget,Work work){
        if(!timeBudget_.reservedClosed||terminalBuild_||buildCalls_>=2048||budget.vertices>=budget.maxVertices||budget.operations>=budget.maxOperations)return false;
        settleClock();const auto limit=readyNanoseconds_>=timeBudget_.totalLimit||heavyNanoseconds_>=timeBudget_.totalLimit-readyNanoseconds_?
            0:timeBudget_.totalLimit-readyNanoseconds_-heavyNanoseconds_;
        if(buildNanoseconds_>=limit)return false;
        timeBudget_.cheapLimit=readyNanoseconds_;timeBudget_.buildLimit=limit;terminalBuild_=true;
        struct Finish {EnvelopeCache& cache;Budget& budget;size_t vertices;std::uint64_t ns;
            ~Finish(){cache.settleClock();cache.terminalBuild_=false;cache.stats_.terminalBuildVertices+=budget.vertices-vertices;
                cache.stats_.terminalBuildNanoseconds+=cache.buildNanoseconds_-ns;cache.terminalCheap();}
        } finish{*this,budget,budget.vertices,buildNanoseconds_};
        work();return true;
    }
    void closeTerminalWork(){settleClock();timeBudget_.cheapLimit=readyNanoseconds_;}
    bool hasPendingBuild()const{return pending_!=nullptr;}
    size_t pendingBuildVertices()const{return pending_?pending_->vertex:0;}
    size_t pendingBuildIndices()const{return pending_?pending_->index:0;}
    void settleClock(){flushFast(BoundsClock::now());}
    void lendToBuild(Budget& budget){
        settleClock();timeBudget_.lendToBuild(readyNanoseconds_,heavyNanoseconds_);
        budget.maxVertices=std::max(budget.maxVertices,timeBudget_.buildVertices());
    }
    void finishReservedTurnsAndLend(){
        settleClock();
        timeBudget_.finishReservedTurns(heavyNanoseconds_,buildNanoseconds_);
        // The completion pass skips packets whose bounds are already valid.
        // Let its remaining sources replace earlier results: retaining them
        // would pin slots for packets that this pass no longer visits.
        memoProtect_=false;
    }
    void chargeCheapPreparation(std::uint64_t ns)noexcept{readyNanoseconds_+=std::min(ns,std::numeric_limits<std::uint64_t>::max()-readyNanoseconds_);}
    double borrowedMicroseconds()const{return double(timeBudget_.lent())/1000.;}
    bool canReady()const{return readyCalls_<4096&&readyNanoseconds_<timeBudget_.cheapLimit;}
    bool canHeavy()const{return !timeBudget_.reservedClosed&&heavyCalls_<2048&&heavyNanoseconds_<HeavyNanoseconds;}
    bool canBuild()const{return (!timeBudget_.reservedClosed||terminalBuild_)&&buildCalls_<2048&&buildNanoseconds_<timeBudget_.buildLimit;}
    bool canWork()const{return canReady()||canBuild();}
    // Time of a phase (0 cheap, 1 heavy, 2 build) spent so far this frame and its limit: the near walk of the bounds job takes half of what is left.
    std::uint64_t phaseNanoseconds(unsigned phase)const{return phase==0?readyNanoseconds_:phase==1?heavyNanoseconds_:buildNanoseconds_;}
    std::uint64_t phaseLimit(unsigned phase)const{return phase==0?timeBudget_.cheapLimit:phase==1?HeavyNanoseconds:timeBudget_.buildLimit;}
    double readyMilliseconds()const{return double(readyNanoseconds_)/1000000.;}
    double heavyMilliseconds()const{return double(heavyNanoseconds_)/1000000.;}
    double classifyMilliseconds()const{return double(classifyNanoseconds_)/1000000.;}
    const Expensive& lastExpensive()const{return expensive_;}
    double buildMilliseconds()const{return double(buildNanoseconds_)/1000000.;}
    double workMilliseconds()const{return readyMilliseconds()+heavyMilliseconds()+buildMilliseconds();}
    const Stats& stats()const{return stats_;}
    size_t entries()const{return entries_.size();}
    size_t reservedBytes()const{return bytes_;}
    size_t memoBytes()const{return memoBytes_;}
    size_t memoEntries()const{return memoEntries_;}
    const NorthlightReplayMemo::Policy& memoPolicy()const{return memoPolicy_;}
    MemoTotals memoTotals()const{auto total=memoTotals_;total.add(stats_);return total;}
    static std::shared_ptr<const Prepared> prepareProgram(const NorthlightActorDeformation::Program& program,const D3DVERTEXELEMENT9* elements,size_t count){
        try {std::shared_ptr<Prepared> prepared(new Prepared);
            std::vector<std::uint32_t> key;if(!makeKey(program,elements,count,key))return {};
            // Copy once here so entries need not retain the builder's spare capacity.
            prepared->key_=std::make_shared<const std::vector<std::uint32_t>>(key);
            prepared->bindings_=prepareBindings(program,elements,count);if(!prepared->bindings_)return {};
            prepared->program_=program;prepared->declaration_.assign(elements,elements+count);prepared->hash_=programHash(program);
            return prepared;
        }catch(...){return {};}
    }
    Status calculatePrepared(const Prepared& prepared,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const float* constants,const float* inverse,
        Budget& budget,Bounds& output,Stage stage=Stage::All,WorkInfo* info=nullptr){
        return calculateImpl(prepared.program_,mesh,owner,prepared.declaration_.data(),prepared.declaration_.size(),constants,inverse,budget,output,stage,info,&prepared);
    }
    Status calculate(const NorthlightActorDeformation::Program& p,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const D3DVERTEXELEMENT9* elements,size_t count,
        const float* constants,const float* inverse,Budget& budget,Bounds& output,Stage stage=Stage::All,WorkInfo* info=nullptr){
        return calculateImpl(p,mesh,owner,elements,count,constants,inverse,budget,output,stage,info,nullptr);
    }
private:
    const float* effectiveConstants(const NorthlightActorDeformation::Program& p,const Entry& e,const float* captured,float* storage){
        if(!e.copyDefinitions)return captured;
        const size_t copied=size_t(e.constantCount)*16;
        if(copied)std::memcpy(storage+4*e.constantFirst,captured+4*e.constantFirst,copied);
        stats_.effectiveCopiedBytes+=copied;
        for(const auto& def:p.definitions){if(def.reg>=256)return nullptr;std::memcpy(storage+4*def.reg,def.value.data(),16);}
        return storage;
    }
    template<bool CompactSingle,class Stop> static Status evaluateGroupsImpl(const NorthlightActorDeformation::Program& p,const Entry& e,
        const float* constants,const float* inverse,Budget& budget,Bounds& output,Stop&& stop){
        double low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
        for(const auto& pair:e.groups){
            if(stop())return Status::Budget;
            budget.operations+=p.operations.size();detail::Vector inputs[CompactSingle?2:16]={};
            const auto& limits=pair.second.limits;
            for(size_t i=0;i<e.bindings->size();++i)for(unsigned j=0;j<4;++j){const float lo=limits[i*8+j],hi=limits[i*8+4+j],mid=float((double(lo)+hi)*.5);inputs[(*e.bindings)[i].reg][j]={mid,std::max(double(mid)-lo,double(hi)-mid)};}
            detail::Vector position;
            if(!detail::singleSkinIntervals(p,inputs,constants,position,e.copyDefinitions)){
                if constexpr(CompactSingle){
                    detail::Vector fallback[16]={};for(unsigned i=0;i<2;++i)fallback[i]=inputs[i];
                    if(!detail::evaluateIntervals(p,fallback,constants,position,e.copyDefinitions))return Status::Unsupported;
                }else if(!detail::evaluateIntervals(p,inputs,constants,position,e.copyDefinitions))return Status::Unsupported;
            }
            for(unsigned axis=0;axis<3;++axis){detail::Value world=detail::mul(position[3],{inverse[12+axis],0});double magnitude=std::fabs(world.value)+world.error;
                for(unsigned k=0;k<3;++k){auto term=detail::mul(position[k],{inverse[4*k+axis],0});magnitude+=std::fabs(term.value)+term.error;world=detail::add(world,term);}
                world.error+=detail::roundError(magnitude)*8+.001;
                if(!std::isfinite(world.value)||!std::isfinite(world.error)||std::fabs(world.value)+world.error>1000000)return Status::Unsupported;
                low[axis]=std::min(low[axis],double(world.value)-world.error);high[axis]=std::max(high[axis],double(world.value)+world.error);
            }
        }
        for(unsigned axis=0;axis<3;++axis){output.low[axis]=std::nextafter(float(low[axis]),-std::numeric_limits<float>::infinity());output.high[axis]=std::nextafter(float(high[axis]),std::numeric_limits<float>::infinity());}
        output.valid=true;return Status::Valid;
    }
    template<class Stop> __attribute__((always_inline)) static inline Status evaluateGroups(const NorthlightActorDeformation::Program& p,const Entry& e,
        const float* constants,const float* inverse,Budget& budget,Bounds& output,Stop&& stop){
        // The audited single-bone kernel uses only v0/v1. All other programs
        // keep the complete register bank; a declined fast kernel does too.
        const bool compact=e.singleBone&&e.bindings->size()==2&&(((*e.bindings)[0].reg==0&&(*e.bindings)[1].reg==1)||((*e.bindings)[0].reg==1&&(*e.bindings)[1].reg==0));
        if(compact)return evaluateGroupsImpl<true>(p,e,constants,inverse,budget,output,std::forward<Stop>(stop));
        return evaluateGroupsImpl<false>(p,e,constants,inverse,budget,output,std::forward<Stop>(stop));
    }
    // Warm cheap skins and small generic envelopes share the batched clock.
    // Cold/heavy work retains its interruptible Timer path and reserved budget.
    bool fastReady(const Prepared& prepared,const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const float* constants,const float* inverse,
        Budget& budget,Bounds& output,WorkInfo* info,Status& status,Entry*& source,Timer* individual=nullptr){
        const bool batched=!individual;
        auto found=findSource(owner,*prepared.key_,prepared.key_.get());Entry* e=found==entries_.end()?nullptr:&found->second;
        source=e;
        if(!e||e->unsupported||!e->ready||(e->skin&&e->copyDefinitions)||e->lastEvaluationNs>=CheapSliceNanoseconds)return false;
        const auto& p=prepared.program_;
        const size_t groups=e->skin?e->skin->usedBones:e->groups.size();
        const size_t operations=e->skin?size_t(75)+groups*(alignedSkin_?88:16):groups*p.operations.size();
        if((!e->skin||!batched)&&((!e->singleBone&&groups>8)||operations>128))return false;
        if(budget.operations>budget.maxOperations||operations>budget.maxOperations-budget.operations)return false;
        for(unsigned j=0;j<16;++j)if(!std::isfinite(inverse[j]))return false;
        if(inverse[3]!=0||inverse[7]!=0||inverse[11]!=0||inverse[15]!=1)return false;
        if(batched&&!fastOpen_){fastMark_=BoundsClock::now();fastOpen_=true;fastCount_=0;}
        e->touched=++clock_;++stats_.memoBypassed;
        if(info){info->kind=Kind::Cheap;info->programHash=e->programHash;info->groups=groups;info->operations=operations;info->skin=bool(e->skin);}
        if(individual)individual->evaluation(*e,info,Stage::All,status,groups,operations);
        float storage[1024];const float* effective=effectiveConstants(p,*e,constants,storage);
        if(!effective){++stats_.invalid;status=Status::Invalid;}
        else if(e->skin){budget.operations+=operations;status=e->skin->evaluate(effective,inverse,output,nullptr,nullptr,alignedSkin_)?Status::Valid:Status::Unsupported;}
        else status=evaluateGroups(p,*e,effective,inverse,budget,output,[]{return false;});
        if(status==Status::Valid){++stats_.ready;stats_.skinValid+=bool(e->skin);stats_.cheapValid+=batched;++stats_.memoStoreBypassed;++stats_.fastValid;}
        else {output={};if(status==Status::Unsupported)++stats_.unsupported;}
        if(batched&&++fastCount_>=ReadyClockStride){const auto now=BoundsClock::now();readyNanoseconds_+=elapsedNs(now,fastMark_);fastMark_=now;fastCount_=0;}
        return true;
    }
    Status calculateImpl(const NorthlightActorDeformation::Program& p,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const D3DVERTEXELEMENT9* elements,size_t count,
        const float* constants,const float* inverse,Budget& budget,Bounds& output,Stage stage,WorkInfo* info,const Prepared* prepared){
        output={};if(info)*info={};
        if(!owner||owner.get()!=&mesh||!constants||!inverse){if(info)info->kind=Kind::Unsupported;return Status::Unsupported;}
        if(stage==Stage::Build?!canBuild():stage==Stage::Heavy?!canHeavy():!canReady())return timeBudget();
        if(stage==Stage::Build)++buildCalls_;else if(stage==Stage::Heavy)++heavyCalls_;else ++readyCalls_;
        Entry* source=nullptr;
        const bool sourceLookedUp=FastReady&&!ResultMemo&&prepared&&(stage==Stage::Cheap||stage==Stage::All);
        if(FastReady&&!ResultMemo&&stage==Stage::Cheap&&prepared){Status fast;if(fastReady(*prepared,owner,constants,inverse,budget,output,info,fast,source))return fast;}
        // Material draws interleave unrelated game work. Time each bounded
        // warm evaluation separately; the shadow pass's batched clock would
        // charge those gaps and exhaust the material budget prematurely.
        if(FastReady&&!ResultMemo&&stage==Stage::All&&prepared){
            {Status fast;Timer timer{*this,false};if(fastReady(*prepared,owner,constants,inverse,budget,output,info,fast,source,&timer))return fast;}
            if(!canReady())return timeBudget();
        }
        Status evaluationStatus=Status::Budget;
        Timer timer{*this,stage==Stage::Build,stage==Stage::Heavy};
        const auto lookupStart=timer.start;bool classified=false;
        const auto accountClassify=[&](){if(diagnostics_&&!classified){classifyNanoseconds_+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(BoundsClock::now()-lookupStart).count());classified=true;}};
        try {
            if(!prepared&&!makeKey(p,elements,count,scratch_)){accountClassify();if(info)info->kind=Kind::Unsupported;++stats_.unsupported;return Status::Unsupported;}
            const auto& exactKey=prepared?*prepared->key_:scratch_;
            // The failed fast path already proved this source hit or miss.
            // No cache mutation occurs before here; reuse it for cold/heavy work.
            if(!sourceLookedUp){auto found=findSource(owner,exactKey,prepared?prepared->key_.get():nullptr);source=found==entries_.end()?nullptr:&found->second;}
            accountClassify();
            if(!source){
                ++stats_.cold;if(info)info->kind=Kind::Cold;
                if(stage==Stage::Ready||stage==Stage::Cheap||stage==Stage::Heavy)return Status::Budget;
                // A material lookup can arrive after its shared build quota
                // ran out. Do not evict a ready source to admit an entry that
                // cannot make progress (or pin it as the pending builder).
                if(budget.vertices>=budget.maxVertices){++stats_.vertexDeferred;return Status::Budget;}
                if(budget.operations>=budget.maxOperations){++stats_.operationDeferred;return Status::Budget;}
                timer.mode(true);if(!canBuild()||timer.expired())return timeBudget();
                if(pending_){++stats_.pendingDeferred;return Status::Budget;}
                Entry e;e.owner=owner;e.sourceAddress=owner.get();e.identity=identity(mesh);e.key=prepared?prepared->key_:std::make_shared<const std::vector<std::uint32_t>>(exactKey);e.programHash=prepared?prepared->hash_:programHash(p);if(!prepare(p,mesh,elements,count,e,prepared)){++stats_.invalid;return Status::Invalid;}
                e.baseBytes=sizeof(Entry)+sizeof(*e.key)+e.key->capacity()*4+32+sizeof(*e.bindings)+e.bindings->capacity()*sizeof(Binding)+32+256+(e.identity?e.identity->bytes():0);
                e.bytes=e.baseBytes+(e.skin?sizeof(SkinEnvelope)+32:0);if(e.bytes>Limit)return Status::Unsupported;
                trimMemos(e.bytes);
                while(!entries_.empty()&&(bytes_+e.bytes>Limit||entries_.size()>=MaxEntries)){if(!evict())return Status::Budget;}
                if(entrySerial_!=std::numeric_limits<std::uint64_t>::max())e.serial=++entrySerial_;
                const auto added=e.bytes;source=&entries_.emplace(sourceHash(mesh),std::move(e))->second;bytes_+=added;
            }
            Entry& e=*source;
            // Negative entries remain small and do not displace useful ready
            // entries merely because the same unsupported draw repeats often.
            if(e.unsupported){if(info)info->kind=Kind::Unsupported;++stats_.unsupported;return Status::Unsupported;}
            e.touched=++clock_;
            if(!e.ready){
                if(stage==Stage::Ready||stage==Stage::Cheap||stage==Stage::Heavy){++stats_.cold;if(info)info->kind=Kind::Cold;return Status::Budget;}
                timer.mode(true);if(!canBuild()||timer.expired())return timeBudget();
                if(pending_&&pending_!=&e){++stats_.pendingDeferred;return Status::Budget;}
                if(!pending_){pending_=&e;pendingProgressFrame_=frame_;}
                const auto built=buildEntry(e,mesh,budget,timer);
                if(built!=Status::Valid)return built;
                if(stage==Stage::Build)return Status::Budget; // Classify/evaluate next frame; never bypass the heavy quota.
            }else if(stage==Stage::Build){++stats_.buildSkippedReady;return Status::Unsupported;} // No build needed; do not pin the build cursor.
            const size_t groups=e.skin?e.skin->usedBones:e.groups.size();
            const size_t operations=e.skin?size_t(75)+size_t(e.skin->usedBones)*(alignedSkin_?88:16):e.groups.size()*p.operations.size();
            // Audited single-bone groups have a fixed small evaluator. Their
            // existing operation/slice caps suffice even above eight bones.
            const bool cheap=(e.skin||((e.singleBone||e.groups.size()<=8)&&operations<=128))&&e.lastEvaluationNs<CheapSliceNanoseconds;
            if(info){info->kind=cheap?Kind::Cheap:Kind::Heavy;info->programHash=e.programHash;info->groups=groups;info->operations=operations;info->skin=bool(e.skin);}
            if(memoHit(e,constants,inverse,output)){
                if(timer.expired()){output={};++stats_.memoBudgetDeferred;return timeBudget();}
                ++stats_.memoHits;stats_.memoAvoidedOperations+=operations;
                if(info)info->kind=Kind::Cheap;
                ++stats_.ready;stats_.skinValid+=bool(e.skin);
                if(stage==Stage::Cheap)++stats_.cheapValid;if(stage==Stage::Heavy)++stats_.heavyValid;
                return Status::Valid;
            }
            if(stage==Stage::Cheap&&!cheap){++stats_.heavyCandidates;return Status::Budget;}
            // Queued categories are hints only: exact current key/owner proof was
            // repeated above. A stale heavy hint never bypasses those checks.
            timer.mode(false);if((stage==Stage::Heavy?!canHeavy():!canReady())||timer.expired())return timeBudget();
            if(stage==Stage::Cheap)timer.slice();
            if(stage==Stage::Heavy)++stats_.heavyAttempts;
            timer.evaluation(e,info,stage,evaluationStatus,groups,operations);
            if(budget.operations>budget.maxOperations||operations>budget.maxOperations-budget.operations){++stats_.operationDeferred;return Status::Budget;}
            for(unsigned j=0;j<16;++j)if(!std::isfinite(inverse[j])){++stats_.invalid;evaluationStatus=Status::Invalid;return evaluationStatus;}
            if(inverse[3]!=0||inverse[7]!=0||inverse[11]!=0||inverse[15]!=1){++stats_.invalid;evaluationStatus=Status::Invalid;return evaluationStatus;}
            // Apply shader-local DEF overrides once, instead of searching their
            // list at every source fetch in every group. Captured banks untouched.
            float storage[1024];const float* effective=effectiveConstants(p,e,constants,storage);
            if(!effective){++stats_.invalid;evaluationStatus=Status::Invalid;return evaluationStatus;}
            if(e.skin){budget.operations+=operations;
                bool interrupted=false;
                struct StopContext {Timer* timer;bool* interrupted;};StopContext stop{&timer,&interrupted};
                const auto shouldStop=[](void* context){auto& s=*static_cast<StopContext*>(context);if(s.timer->expired()){*s.interrupted=true;return true;}return false;};
                if(!e.skin->evaluate(effective,inverse,output,shouldStop,&stop,alignedSkin_)){output={};
                    if(interrupted){if(stage==Stage::Cheap&&timer.sliceExpired()){++stats_.cheapSliceDeferred;if(info)info->kind=Kind::Heavy;}return timeBudget();}
                    ++stats_.unsupported;evaluationStatus=Status::Unsupported;return evaluationStatus;}
                ++stats_.ready;++stats_.skinValid;if(stage==Stage::Cheap)++stats_.cheapValid;if(stage==Stage::Heavy)++stats_.heavyValid;
                remember(e,constants,inverse,output);evaluationStatus=Status::Valid;return evaluationStatus;
            }
            evaluationStatus=evaluateGroups(p,e,effective,inverse,budget,output,[&]{return timer.expired();});
            if(evaluationStatus==Status::Budget){if(stage==Stage::Cheap&&timer.sliceExpired()){++stats_.cheapSliceDeferred;if(info)info->kind=Kind::Heavy;}return timeBudget();}
            if(evaluationStatus!=Status::Valid){++stats_.unsupported;return evaluationStatus;}
            ++stats_.ready;if(stage==Stage::Cheap)++stats_.cheapValid;if(stage==Stage::Heavy)++stats_.heavyValid;remember(e,constants,inverse,output);return evaluationStatus;
        }catch(...){evaluationStatus=Status::Unsupported;accountClassify();if(info)info->kind=Kind::Unsupported;++stats_.unsupported;return Status::Unsupported;}
    }
};
