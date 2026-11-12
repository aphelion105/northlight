#include <windows.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include "query_backend_threads.h"
using namespace NorthlightStream;
static std::string report;
static void sink(const char* line){report=line;}
static std::uint64_t field(const char* name){auto at=report.find(name);assert(at!=std::string::npos);return std::strtoull(report.c_str()+at+std::strlen(name),nullptr,10);}
int main(){
    using Name=HRESULT(WINAPI*)(HANDLE,PCWSTR);
    auto name=reinterpret_cast<Name>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"SetThreadDescription")));assert(name);
    std::atomic<unsigned> ready{0},done{0};std::atomic<bool> go{false},stop{false};
    auto work=[&](const wchar_t* label){assert(SUCCEEDED(name(GetCurrentThread(),label)));++ready;while(!go.load())YieldProcessor();
        volatile unsigned value=1;for(unsigned i=0;i<600000;++i)value=value*1664525u+1013904223u;assert(value!=1);++done;while(!stop.load())Sleep(1);};
    std::thread cs(work,L"dxvk-cs"),compiler(work,L"dxvk-shader-n"),unrelated(work,L"test-unrelated");
    while(ready.load()!=3)YieldProcessor();DWORD before=0,after=0;assert(GetProcessHandleCount(GetCurrentProcess(),&before));
    {
        QueryBackendThreads probe;
        assert(!probe.observe(1,1,0,true,S_OK,EventPendingKind::Ready,1,sink));
        assert(!probe.observe(1,1,0,true,S_FALSE,EventPendingKind::FalsePayload,1,sink));
        assert(GetProcessHandleCount(GetCurrentProcess(),&after)&&after==before);
        assert(probe.observe(1,1,0,true,S_FALSE,EventPendingKind::Untouched,1,sink));
        assert(GetProcessHandleCount(GetCurrentProcess(),&after)&&after==before+2);
        go.store(true);while(done.load()!=3)std::this_thread::yield();
        assert(!probe.observe(1,1,0,false,S_FALSE,EventPendingKind::Untouched,1,sink)&&report.empty());
        assert(!probe.observe(1,1,0,false,S_OK,EventPendingKind::Ready,1,sink));
        assert(field("csThreads=")==1&&field("compilerThreads=")==1&&field("otherThreads=")==0);
        assert(field("csCycles=")>0&&field("compilerCycles=")>0);
        assert(report.find("setupMs=")!=std::string::npos&&report.find("sampleMs=")!=std::string::npos);
        assert(GetProcessHandleCount(GetCurrentProcess(),&after)&&after==before);
        puts(report.c_str());report.clear();
        assert(!probe.observe(1,1,0,false,S_FALSE,EventPendingKind::Untouched,1,sink));
        assert(probe.observe(1,2,0,true,S_FALSE,EventPendingKind::Untouched,2,sink));
        // Reused proxy address/new generation, reset epoch and disabled sink
        // discard stale baselines and close every retained query-only handle.
        assert(!probe.observe(1,1,0,true,S_OK,EventPendingKind::Ready,2,sink)&&report.empty());
        assert(GetProcessHandleCount(GetCurrentProcess(),&after)&&after==before);
        assert(probe.observe(1,3,0,true,S_FALSE,EventPendingKind::Untouched,3,sink));
        assert(!probe.observe(1,3,1,false,S_OK,EventPendingKind::Ready,3,sink)&&report.empty());
        assert(probe.observe(1,4,1,true,S_FALSE,EventPendingKind::Untouched,4,sink));
        assert(!probe.observe(1,4,1,false,S_FALSE,EventPendingKind::Untouched,4,nullptr));
        assert(GetProcessHandleCount(GetCurrentProcess(),&after)&&after==before);
    }
    {
        QueryBackendThreads probe;unsigned accepted=0;
        for(unsigned i=1;i<=20;++i){accepted+=probe.observe(1,i,0,true,S_FALSE,EventPendingKind::Untouched,i,sink);probe.clear();}
        assert(accepted==16);assert(GetProcessHandleCount(GetCurrentProcess(),&after)&&after==before);
    }
    {
        QueryBackendThreads probe;assert(probe.observe(2,1,0,true,S_FALSE,EventPendingKind::Untouched,1,sink));
    }
    assert(GetProcessHandleCount(GetCurrentProcess(),&after)&&after==before);
    stop.store(true);cs.join();compiler.join();unrelated.join();
    puts("QUERY_BACKEND_THREADS_PASS own_named_cpu_deltas ready_gpu_no_capture exact_generation_epoch cancel_profile_off cap16 destructor_handles_closed");
}
