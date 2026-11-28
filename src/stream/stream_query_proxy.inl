// Query recording, ordered GetData, and generation-scoped completion.
inline unsigned queryDataSize(unsigned type){
    switch(type){case D3::kQEvent:case D3::kQOcclusion:case D3::kQDisjoint:return 4;case D3::kQTimestamp:case D3::kQFreq:return 8;case D3::kQVcache:return 16;default:return 0;}
}

struct StreamQuery final:IDirect3DQuery9,ProxyBase {
    unsigned type;unsigned dataSize;
    // EVENT completion is exact-generation only. Poll requests own no caller buffer;
    // ordered Destroy removes the proxy only after its last queued poll has run.
    struct PollRequest {StreamQuery* query;std::uint32_t generation;};
    std::atomic<bool> eventPollQueued{false};
    std::atomic<std::uint32_t> eventDoneGen{0};
    std::atomic<HRESULT> eventStatus{S_FALSE};
    std::atomic<BOOL> eventValue{FALSE};
    bool eventWaitExpired=false; // replay-thread only, reset at each replayed END
    EventSpinHistory eventSpinHistory; // replay-thread only
    std::uint32_t eventEpoch=0;std::uint64_t eventPendingSince=0,eventEndSequence=0;bool eventCallerLogged=false,eventStagePending=false; // game thread only
    std::uint32_t eventThreadProbeGen=0,eventThreadProbeEpoch=0; // one diagnostic attempt per exact generation
    void observeBackendThreads(std::uint32_t issued,std::uint32_t epoch,HRESULT hr,EventPendingKind kind){
        if(!core->timing||core->queryEpoch.load()!=epoch||gen.load()!=issued)return;
        const bool first=eventThreadProbeGen!=issued||eventThreadProbeEpoch!=epoch;
        if(core->queryBackendThreads.observe(reinterpret_cast<std::uintptr_t>(this),issued,epoch,first,hr,kind,core->frameNo,core->queryThreadLog)){
            eventThreadProbeGen=issued;eventThreadProbeEpoch=epoch;
        }
    }
    void completeEvent(HRESULT hr,const unsigned char* buf){
        // Replay thread only. Publish a generation once: pollQueries polls a query PollEventQuery already completed, and a
        // later error must not replace the published status or value (two relaxed stores a reader could pair across publications).
        if(hr==S_FALSE||eventDoneGen.load(std::memory_order_relaxed)==issueSeen)return;
        BOOL value=FALSE;if(hr==S_OK)std::memcpy(&value,buf,sizeof value);
        eventValue.store(value,std::memory_order_relaxed);eventStatus.store(hr,std::memory_order_relaxed);
        eventDoneGen.store(issueSeen,std::memory_order_release);
    }
    std::atomic<std::uint32_t> gen{0},readyGen{0};   // gen: END issues by the game; readyGen: the replay's polled result belongs to it
    std::atomic<std::uint32_t> failedGen{0};   // non-EVENT: the replay's poll of this generation failed and stopped
    unsigned char result[64]{};DWORD resultSize=0;std::uint32_t issueSeen=0,issueSeenEpoch=0;   // replay thread: END generation and device-reset epoch
    std::atomic<std::uint32_t> resultSeq{0};   // seqlock around result/resultSize: odd while the replay thread writes, so a stale read (FLUSH, below) never sees a torn result
    void writeResult(const unsigned char* buf,DWORD n){const std::uint32_t s=resultSeq.load(std::memory_order_relaxed);resultSeq.store(s+1,std::memory_order_relaxed);std::atomic_thread_fence(std::memory_order_release);
        n=std::min<DWORD>(n,sizeof result);std::memcpy(result,buf,n);resultSize=n;resultSeq.store(s+2,std::memory_order_release);}
    DWORD readResult(void* data,DWORD size){   // the latest polled result into the game's buffer, consistent
        for(;;){const std::uint32_t s1=resultSeq.load(std::memory_order_acquire);if(s1&1)continue;
            const DWORD n=resultSize,copy=std::min(size,n);unsigned char tmp[sizeof result];if(data&&copy)std::memcpy(tmp,result,copy);std::atomic_thread_fence(std::memory_order_acquire);
            if(resultSeq.load(std::memory_order_relaxed)!=s1)continue;
            if(data&&copy)std::memcpy(data,tmp,copy);return n;}}
    StreamQuery(StreamCore* c,unsigned t):ProxyBase(c,Kind::Query),type(t),dataSize(queryDataSize(t)){info.type=t;ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DQuery9*>(this));}
    ~StreamQuery(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_QUERY_METHODS
    NL_PROXY_COM(IDirect3DQuery9)
    HRESULT local(CmdTag<Cmd::Query_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    D3DQUERYTYPE local(CmdTag<Cmd::Query_GetType>){return (D3DQUERYTYPE)type;}
    DWORD local(CmdTag<Cmd::Query_GetDataSize>){return dataSize;}
    HRESULT STDMETHODCALLTYPE Issue(DWORD flags) override{
        [[maybe_unused]] auto scope=callScope(static_cast<std::uint16_t>(Cmd::Query_Issue));
        auto& q=streamQueue();
        auto* args=static_cast<Args_Query_Issue*>(q.reserve(static_cast<std::uint16_t>(Cmd::Query_Issue),sizeof(Args_Query_Issue)));
        *args={this,flags};
        if(type!=8)core->frameHazards|=StreamCore::kHazardQuery;
        if(flags&D3::kIssueEnd){
            eventEpoch=core->queryEpoch.load();eventPendingSince=0;eventStagePending=false;
            // Reserve can pump calls. The sequence becomes exact after reserve.
            eventEndSequence=type==8?q.recordedSeq()+1:0;
            gen.fetch_add(1);
        }
        q.commit();return D3D_OK;
    }
    template<class Poll>
    HRESULT waitFlushedEvent(Poll& poll,EventPendingKind& backend,EventPauseProfile* pauses,
                             std::uint64_t& cycles,std::uint64_t& cycleSamples,bool& shortGpuTrip,bool sustained){
        const bool legacyGpuSpin=legacyEventGpuSpin()&&!sustained;
        // Driver-wide submission locks have no deadline. Only real GetData
        // results can complete this generation, including after the timer wake.
        core->queryLongPause.begin();QueryPollPause shortPause;
        auto pause=[&](std::uint64_t left,std::uint64_t elapsed){
            if(pauses&&sustained&&!eventWaitExpired&&backend==EventPendingKind::FalsePayload&&elapsed>=200000&&elapsed<2000000)
                shortGpuTrip=true;
            pauseEventQuery(backend,eventWaitExpired,left,elapsed,shortPause,core->queryLongPause,legacyGpuSpin);
        };
#ifdef _WIN32
        ULONG64 before=0,after=0;
        const bool measuring=pauses&&QueryThreadCycleTime(GetCurrentThread(),&before);
#else
        (void)cycles;(void)cycleSamples;
#endif
        unsigned calls=0;std::uint64_t pendingNs=0;
        const HRESULT hr=waitEventQuery(poll,nowNs,pause,calls,kEventFlushWaitNs,&pendingNs,pauses);
#ifdef _WIN32
        if(measuring&&QueryThreadCycleTime(GetCurrentThread(),&after)&&after>=before){
            cycles=after-before;
            cycleSamples=1;
        }
#endif
        if(hr==S_FALSE)eventWaitExpired=true;
        eventSpinHistory.observePending(pendingNs);
        own(core->q.stats.eventGpuWaitNs,pendingNs);own(core->q.stats.eventPollCalls,calls);own(core->q.stats.eventWaitTrips);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetData(void* data,DWORD size,DWORD flags) override{
        Queue& q=core->q;
        if(dead.load())return D3DERR_INVALIDCALL;   // the real create failed: an error, not S_FALSE forever
        if(type==8&&flags==D3::kGetDataFlush&&!eventCallerLogged&&core->timing&&core->queryCaller){
            eventCallerLogged=true;core->queryCaller(core->frameNo,this,__builtin_return_address(0));
        }
        const std::uint32_t issued=gen.load();
        if(type==8&&issued&&eventEpoch==core->queryEpoch.load()&&core->coopState.load()==D3D_OK&&(flags==0||flags==D3::kGetDataFlush)&&
           ((!data&&!size)||(data&&size==dataSize))){
            const bool done=eventDoneGen.load(std::memory_order_acquire)==issued;
            if(!done&&flags==D3::kGetDataFlush){
                // Returning pending here enters the client's Sleep(0) retry path.
                // Wait with message pumping immediately; completed generations stay local.
                if(inPumpedWait)return D3DERR_INVALIDCALL;
                if(core->timing&&!eventPendingSince)eventPendingSince=nowNs();
                goto pumped;
            }
            if(done){
                const HRESULT hr=eventStatus.load(std::memory_order_relaxed);
                if(hr==S_OK&&data){const BOOL value=eventValue.load(std::memory_order_relaxed);std::memcpy(data,&value,sizeof value);}
                if(eventPendingSince){own(q.stats.eventPendingNs,nowNs()-eventPendingSince);own(q.stats.eventCompleted);eventPendingSince=0;}
                own(q.stats.eventLocal);observeBackendThreads(issued,eventEpoch,hr,EventPendingKind::Ready);return hr;
            }
            if(flags==D3::kGetDataFlush){
                if(inPumpedWait)return D3DERR_INVALIDCALL;
                // At most one bounded FLUSH poll per query is in flight. Repeated
                // S_FALSE probes publish progress without a stack task or sync wait.
                if(!eventPollQueued.load(std::memory_order_acquire)){
                    eventPollQueued.store(true,std::memory_order_relaxed);
                    auto* request=static_cast<PollRequest*>(q.reserve((std::uint16_t)Cmd::PollEventQuery,sizeof(PollRequest)));
                    *request={this,issued};q.commit();own(q.stats.eventRequests);
                }
            }
            if(core->timing&&!eventPendingSince)eventPendingSince=nowNs();
            q.publish();own(q.stats.eventPending);
#if defined(__i386__)||defined(__x86_64__)
            __builtin_ia32_pause();
#endif
            return S_FALSE;
        }
        // A completed current generation needs no backend round trip. An older
        // occlusion result can describe a different object or alpha threshold.
        {
            const bool localFlush=flags==D3::kGetDataFlush&&type==9&&core->occlusionLocal;
            if((flags==0||localFlush)&&issued&&readyGen.load(std::memory_order_acquire)==issued&&
               eventEpoch==core->queryEpoch.load()&&core->coopState.load()==D3D_OK&&
               ((!data&&!size)||(data&&size==dataSize))){
                if(flags==0)q.publish();else own(q.stats.queryFlushLocal);
                readResult(data,size);return D3D_OK;
            }
        }
        pumped:
        if(flags||!issued||eventEpoch!=core->queryEpoch.load()||core->coopState.load()!=D3D_OK||
           failedGen.load(std::memory_order_acquire)==issued||
           (data?size!=dataSize:size!=0)){   // Invalidated/uncached/failed requests use the real query after all recorded work.
            // Sent-message handlers may release the last external reference
            // while this call pumps. Keep both the query and its device alive.
            struct Hold {StreamQuery* query;explicit Hold(StreamQuery* p):query(p){query->comAddRef();}~Hold(){query->comRelease();}} hold(this);
            if(flags&D3::kGetDataFlush)own(core->q.stats.queryFlushSynced);
            HRESULT hr=D3DERR_INVALIDCALL;
            const bool measure=core->timing;const auto waitBefore=measure?get(q.stats.syncNs):0;
            const bool unissued=measure&&!gen.load();
            // Keep an ordinary GPU frame inside the pumped wait. The deadline
            // bounds an unresponsive query; completion returns immediately.
            const bool event=type==8&&flags==D3::kGetDataFlush&&gen.load();
            const auto tripStart=measure||event?nowNs():0;std::uint64_t replayStart=0,replayEnd=0;
            EventPauseProfile eventPauses;bool eventLongPhase=false,shortGpuTrip=false;EventPendingKind backendPending=EventPendingKind::None;
            std::uint64_t eventCpuCycles=0,eventCpuSamples=0;
            std::uint64_t fenceCalls=0,fenceNs=0,fenceTimeouts=0;
            std::uint64_t firstEventPoll=0,lastNotEncoded=0,firstGpuPending=0,lastGpuPending=0;
            const auto epoch=eventEpoch;
            auto body=[&](bool cached){if(measure||event)replayStart=nowNs();if(event)eventLongPhase=eventWaitExpired;if(inner){
                if(cached&&eventDoneGen.load(std::memory_order_acquire)==issued){
                    hr=eventStatus.load(std::memory_order_relaxed);
                    if(hr==S_OK&&data){const BOOL value=eventValue.load(std::memory_order_relaxed);std::memcpy(data,&value,sizeof value);}
                }else{
                    BOOL eventPayload=0;bool eventPayloadKnown=false;
                    const bool auditedPayload=event&&dxvk311EventPayload.load(std::memory_order_relaxed)&&
                        ((!data&&!size)||(data&&size==sizeof(BOOL)));
                    EventFlushCadence cadence;
                    auto poll=[&]{
                        const auto pollAt=event?nowNs():0;
                        const auto nativeFlags=event?cadence.flags(pollAt):flags;
                        if(!auditedPayload)return static_cast<IDirect3DQuery9*>(inner)->GetData(data,size,nativeFlags);
                        // Reuse the real poll. Zero upper bytes also handle
                        // DXVK's cached path, which writes a C++ bool, not BOOL.
                        BOOL payload=EventPayloadMarker;
                        const auto result=static_cast<IDirect3DQuery9*>(inner)->GetData(&payload,sizeof payload,nativeFlags);
                        backendPending=eventPendingKind(result,payload);
                        if(measure){
                            if(!firstEventPoll)firstEventPoll=pollAt;
                            if(backendPending==EventPendingKind::Untouched)lastNotEncoded=pollAt;
                            if(backendPending==EventPendingKind::FalsePayload){
                                if(!firstGpuPending)firstGpuPending=pollAt;
                                lastGpuPending=pollAt;
                            }
                        }
                        if(result==S_OK){eventPayload=payload;eventPayloadKnown=true;if(data)std::memcpy(data,&payload,sizeof payload);}
                        return result;
                    };
#ifdef NORTHLIGHT_REPLAY_STAND
                    // Preserve the stand's explicit A/B between fixed spin windows.
                    const bool sustained=false;
#else
                    const bool sustained=auditedPayload&&eventSpinHistory.sustained()&&adaptiveEventSpin();
#endif
                    if(event)hr=waitFlushedEvent(poll,backendPending,measure?&eventPauses:nullptr,eventCpuCycles,eventCpuSamples,shortGpuTrip,sustained);
                    else hr=poll();
                    // A completed pumped wait is also terminal for the background poller.
                    // NULL callers publish only when the audited native poll supplied a known full BOOL.
                    if(event&&hr!=S_FALSE&&issueSeen==issued&&issueSeenEpoch==epoch&&core->queryEpoch.load()==epoch&&
                       ((data&&size==sizeof(BOOL))||(!data&&!size&&(hr<0||eventPayloadKnown))))
                        completeEvent(hr,eventPayloadKnown?reinterpret_cast<const unsigned char*>(&eventPayload):static_cast<const unsigned char*>(data));
                    if(!event&&hr==S_OK&&issued&&data&&size==dataSize&&size<=sizeof result&&issueSeen==issued&&
                       issueSeenEpoch==epoch&&core->queryEpoch.load()==epoch&&gen.load()==issued&&core->coopState.load()==D3D_OK){
                        writeResult(static_cast<const unsigned char*>(data),size);readyGen.store(issued,std::memory_order_release);
                    }
                }
            }if(measure)replayEnd=nowNs();};
            bool ran=false;
            if(event&&!inPumpedWait&&epoch==core->queryEpoch.load()&&core->coopState.load()==D3D_OK&&
               ((!data&&!size)||(data&&size==dataSize))){
                // EVENT END fences only earlier work. After that END replayed,
                // a boundary task need not drain unrelated later draws to poll it.
                auto express=[&]{if(issueSeen==issued&&issueSeenEpoch==epoch&&core->queryEpoch.load()==epoch&&core->coopState.load()==D3D_OK&&!dead.load()&&inner){body(true);ran=true;}};
                auto ready=[&]{
                    const auto ahead=std::uint32_t(issued-issueSeen);
                    // Only an unreplayed captured END may defer the task. The
                    // half-range guard falls back for an ambiguous generation.
                    return core->queryEpoch.load()!=epoch||core->coopState.load()!=D3D_OK||dead.load()||
                           !ahead||ahead>=0x80000000u||q.recordedSeq()<=q.replayedSeq();
                };
                struct Work {decltype(express)* run;decltype(ready)* canRun;};Work work{&express,&ready};
                ExpressTimes queueTimes;
                const auto replayed=measure?q.replayedSeq():0;
                const auto queued=measure?q.recordedSeq()-replayed:0;
                q.publish();   // Issue returned: its END is committed; a deferred task can always advance to it.
                q.runExpress([](void* p){(*static_cast<Work*>(p)->run)();},&work,measure?&queueTimes:nullptr,ExpressKind::Query,
                             [](void* p){return (*static_cast<Work*>(p)->canRun)();},eventEndSequence);
                if(ran&&measure&&queueTimes.serveSeq>=queueTimes.seenSeq)
                    core->eventQueueFrame.record(tripStart,queueTimes.postedNs,queueTimes.seenNs,queueTimes.startNs,replayStart,
                                                 queued,queueTimes.serveSeq-queueTimes.seenSeq,queueTimes.seenBehindCmd);
                if(ran){own(q.stats.expressQueryHits);own(q.stats.census[(std::size_t)Cmd::SyncGetData]);own(q.stats.syncCalls);}
                else own(q.stats.expressQueryFallbacks);
            }
            if(!ran)ran=runTask(*core,[&](StreamCore&){body(false);},Cmd::SyncGetData);
            if(event&&ran&&hr>=0&&replayStart>=tripStart)own(q.stats.eventReplayWaitNs,replayStart-tripStart);
            const auto tripEnd=measure?nowNs():0;
            if(event&&ran&&hr!=S_FALSE&&eventPendingSince){own(q.stats.eventPendingNs,nowNs()-eventPendingSince);own(q.stats.eventCompleted);eventPendingSince=0;}   // a wait that ended on this pumped path
            if(measure){const auto waitNs=get(q.stats.syncNs)-waitBefore;
                const auto record=[&](StreamCore::QueryWait& s){s.record(flags,unissued,ran,hr,waitNs,tripStart,replayStart,replayEnd,tripEnd,event?&eventPauses:nullptr,eventLongPhase,backendPending);s.fenceCalls+=fenceCalls;s.fenceNs+=fenceNs;s.fenceTimeouts+=fenceTimeouts;};
                const unsigned index=type<20?type:20;
                auto& frame=core->queryFrame[index];
                record(core->queryWait[index]);record(frame);
                frame.eventCpuCycles+=eventCpuCycles;frame.eventCpuSamples+=eventCpuSamples;frame.eventShortGpuTrips+=shortGpuTrip;
            }
            if(!ran)return D3DERR_INVALIDCALL;
            // A DXVK pending payload identifies observed progress, including
            // queue submission and scheduling after the command is recorded.
            if(firstEventPoll&&replayEnd>=firstEventPoll&&core->queryThreadLog&&
               gen.load()==issued&&core->queryEpoch.load()==epoch&&
               (eventStagePending||replayEnd-firstEventPoll>=8000000)){
                // Keep the completion of a logged wait even if its last poll trip was short.
                eventStagePending=hr==S_FALSE;
                const auto sinceFirst=[&](std::uint64_t at){return at?double(at-firstEventPoll)/1e6:-1.;};
                char line[512];
                std::snprintf(line,sizeof line,
                    "CSTREAM EVENT_STAGE frame=%llu generation=%u epoch=%u startNs=%llu endNs=%llu lastNotEncodedMs=%.3f firstGpuPendingMs=%.3f lastGpuPendingMs=%.3f result=0x%08x",
                    (unsigned long long)core->frameNo,issued,epoch,(unsigned long long)firstEventPoll,(unsigned long long)replayEnd,
                    sinceFirst(lastNotEncoded),sinceFirst(firstGpuPending),sinceFirst(lastGpuPending),unsigned(hr));
                core->queryThreadLog(line);
            }
            if(measure&&event)observeBackendThreads(issued,epoch,hr,backendPending);
            return hr;
        }
        q.publish();   // the replay thread must see the Issue this GetData polls for, or a spin on S_FALSE would never end
        return S_FALSE;}
};
