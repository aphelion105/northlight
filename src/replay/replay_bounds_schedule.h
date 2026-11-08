#pragma once
#include <algorithm>
#include <cstddef>
namespace NorthlightReplaySchedule {
// A pending packet may be cold or merely too expensive for the remaining
// budget. Keep visiting affordable packets until the phase's actual budget is
// exhausted. Rotate past a first packet that consumes an entire phase so it
// cannot pin every following draw indefinitely.
template<class CanWork,class Visit>
size_t pass(size_t count,size_t start,CanWork canWork,Visit visit){
    if(!count)return 0;
    start%=count;
    size_t index=start;
    for(size_t offset=0;offset<count;++offset){
        const size_t next=index+1==count?0:index+1;
        if(!canWork())return index==start?next:index;
        const bool pending=visit(index);
        if(!canWork())return pending&&index!=start?index:next;
        index=next;
    }
    return start+1==count?0:start+1;
}
// Spend only the real remaining total after reserved turns. A finished source
// can serve this frame immediately; unfinished sources still draw. Bonus walks
// never advance the regular rotating cursors or reopen the heavy allowance.
template<class Cache,class Budget,class Live,class Visit,class Cold>
void finishCold(size_t count,size_t start,Cache& cache,Budget& cheap,Budget& heavy,Budget& build,Live live,Visit visit,Cold cold){
    if(!live())return;
    const size_t buildCap=build.maxOperations;
    cache.continuePendingAfterReady(build);cache.terminalCheap();
    if(count)for(size_t offset=0,index=start%count;offset<count;++offset,index=index+1==count?0:index+1){
        if(!live()||cache.hasPendingBuild())break;
        if(!cold(index))continue;
        const size_t evaluated=cheap.operations+heavy.operations;
        build.maxOperations=std::min(buildCap,evaluated>=262144?size_t(0):262144-evaluated);
        if(!cache.terminalBuild(build,[&]{visit(index,2);}))break;
        cheap.maxOperations=262144-heavy.operations-build.operations;
        if(live()&&cache.canCheap()&&cheap.operations<cheap.maxOperations)visit(index,3);
    }
    cheap.maxOperations=262144-heavy.operations-build.operations;
    pass(count,start,[&]{return live()&&cache.canCheap()&&cheap.operations<cheap.maxOperations;},[&](size_t index){return visit(index,3);});
    cache.closeTerminalWork();
}
}
