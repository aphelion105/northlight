#pragma once
#include "stream_wait.h"
#include "query_poll.h"
#include <cstdio>
#include <cwchar>
#include <functional>

namespace NorthlightStream {
// RenderProfile-only own-process observations. Thread CPU deltas cannot name a
// draw or prove what a waiting CS thread depends on. Never suspend a thread.
class QueryBackendThreads {
#if defined(_WIN32) && !defined(NL_STUB)
    struct Item {HANDLE handle=nullptr;unsigned group=0;std::uint64_t time=0,cycles=0;};
    Item items_[32]{};unsigned count_=0,attempts_=0,scanned_=0;
    std::uintptr_t key_=0;std::uint32_t generation_=0,epoch_=0;
    std::uint64_t start_=0,setupNs_=0;bool active_=false;
    static bool read(HANDLE h,std::uint64_t& time,std::uint64_t& cycles){
        FILETIME c,e,k,u;ULONG64 value=0;if(!GetThreadTimes(h,&c,&e,&k,&u)||!QueryThreadCycleTime(h,&value))return false;
        time=((std::uint64_t(k.dwHighDateTime)<<32)|k.dwLowDateTime)+((std::uint64_t(u.dwHighDateTime)<<32)|u.dwLowDateTime);cycles=value;return true;
    }
    void begin(){
        const auto before=nowNs();scanned_=0;
        using Description=HRESULT(WINAPI*)(HANDLE,PWSTR*);
        using NextThread=LONG(WINAPI*)(HANDLE,HANDLE,ACCESS_MASK,ULONG,ULONG,PHANDLE);
        const auto describe=reinterpret_cast<Description>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetThreadDescription")));
        const auto next=reinterpret_cast<NextThread>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtGetNextThread")));
        // Both are optional. No global Toolhelp fallback: its system-wide
        // snapshot took 75-108 ms in the offline control on this machine.
        HANDLE previous=nullptr;bool kept=false;
        if(describe&&next)for(;scanned_<128&&count_<32;++scanned_){
            HANDLE handle=nullptr;const LONG status=next(GetCurrentProcess(),previous,THREAD_QUERY_LIMITED_INFORMATION,0,0,&handle);
            if(previous&&!kept)CloseHandle(previous);previous=nullptr;kept=false;
            if(status<0||!handle)break;previous=handle;
            PWSTR name=nullptr;unsigned group=0;
            if(SUCCEEDED(describe(handle,&name))&&name){
                if(!std::wcscmp(name,L"dxvk-cs"))group=1;
                else if(!std::wcsncmp(name,L"dxvk-shader-",12))group=2;
                else if(!std::wcsncmp(name,L"dxvk-",5))group=3;
                LocalFree(name);
            }
            Item item;item.handle=handle;item.group=group;
            if(group&&read(handle,item.time,item.cycles)){items_[count_++]=item;kept=true;}
        }
        if(previous&&!kept)CloseHandle(previous);
        start_=nowNs();setupNs_=start_-before;
    }
    void finish(std::uint64_t frame,HRESULT result,const std::function<void(const char*)>& sink){
        const auto before=nowNs();std::uint64_t times[4]{},cycles[4]{};unsigned counts[4]{};
        for(unsigned i=0;i<count_;++i){std::uint64_t time=0,cycle=0;const auto& item=items_[i];
            if(read(item.handle,time,cycle)&&time>=item.time&&cycle>=item.cycles){times[item.group]+=time-item.time;cycles[item.group]+=cycle-item.cycles;++counts[item.group];}}
        const unsigned retained=count_;clear();const auto sampleNs=nowNs()-before;char line[800];
        std::snprintf(line,sizeof line,"CSTREAM EVENT_THREADS gameFrame=%llu generation=%u epoch=%u result=0x%08x intervalMs=%.4f setupMs=%.4f sampleMs=%.4f scanned=%u retained=%u truncated=%u csThreads=%u csCpuMs=%.4f csCycles=%llu compilerThreads=%u compilerCpuMs=%.4f compilerCycles=%llu otherThreads=%u otherCpuMs=%.4f scope=own-thread-deltas-after-first-expired-trip-not-draw-or-gpu-time",
            (unsigned long long)frame,generation_,epoch_,unsigned(result),double(before-start_)/1e6,double(setupNs_)/1e6,double(sampleNs)/1e6,scanned_,retained,unsigned(scanned_==128||retained==32),counts[1],double(times[1])/1e4,(unsigned long long)cycles[1],counts[2],double(times[2])/1e4,(unsigned long long)cycles[2],counts[3],double(times[3])/1e4);
        sink(line);
    }
#endif
public:
    QueryBackendThreads()=default;
    QueryBackendThreads(const QueryBackendThreads&)=delete;QueryBackendThreads& operator=(const QueryBackendThreads&)=delete;
    ~QueryBackendThreads(){clear();}
    void clear(){
#if defined(_WIN32) && !defined(NL_STUB)
        for(unsigned i=0;i<count_;++i)CloseHandle(items_[i].handle);
        count_=0;active_=false;
#endif
    }
    // Called on the game thread AFTER its trip/profile clocks are finalized.
    // True consumes this generation's only start attempt, even if APIs fail.
    bool observe(std::uintptr_t key,std::uint32_t generation,std::uint32_t epoch,bool first,HRESULT result,EventPendingKind kind,std::uint64_t frame,const std::function<void(const char*)>& sink){
#if defined(_WIN32) && !defined(NL_STUB)
        if(active_&&(first||key!=key_||generation!=generation_||epoch!=epoch_))clear();
        if(!sink){clear();return false;}
        if(active_){if(result!=S_FALSE)finish(frame,result,sink);return false;}
        if(!first||result!=S_FALSE||kind!=EventPendingKind::Untouched||attempts_>=16)return false;
        ++attempts_;key_=key;generation_=generation;epoch_=epoch;begin();active_=true;return true;
#else
        (void)key;(void)generation;(void)epoch;(void)first;(void)result;(void)kind;(void)frame;(void)sink;return false;
#endif
    }
};
}
