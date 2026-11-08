#pragma once
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <cstdint>
#include "stream_wait.h"
struct ThreadProbe {
    struct Item {HANDLE handle=nullptr;DWORD id=0;unsigned group=0;std::uint64_t time=0,cycles=0;};
    Item items[32]{};unsigned count=0;std::uint64_t start=0,setupNs=0;
    static bool read(HANDLE h,std::uint64_t& time,std::uint64_t& cycles){
        FILETIME c,e,k,u;ULONG64 value=0;if(!GetThreadTimes(h,&c,&e,&k,&u)||!QueryThreadCycleTime(h,&value))return false;
        time=((std::uint64_t(k.dwHighDateTime)<<32)|k.dwLowDateTime)+((std::uint64_t(u.dwHighDateTime)<<32)|u.dwLowDateTime);cycles=value;return true;
    }
    ~ThreadProbe(){clear();}
    void clear(){for(unsigned i=0;i<count;++i)CloseHandle(items[i].handle);count=0;}
    void begin(){
        clear();const auto before=NorthlightStream::nowNs();
        using Description=HRESULT(WINAPI*)(HANDLE,PWSTR*);
        const auto describe=reinterpret_cast<Description>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetThreadDescription")));
        if(!describe)return;
        using NextThread=LONG(WINAPI*)(HANDLE,HANDLE,ACCESS_MASK,ULONG,ULONG,PHANDLE);
        const auto next=reinterpret_cast<NextThread>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtGetNextThread")));
        if(!next)return;
        HANDLE previous=nullptr;bool kept=false;
        for(unsigned scanned=0;scanned<128&&count<32;++scanned){
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
            Item item;item.handle=handle;item.id=GetThreadId(handle);item.group=group;
            if(group&&read(handle,item.time,item.cycles)){items[count++]=item;kept=true;}
        }
        if(previous&&!kept)CloseHandle(previous);start=NorthlightStream::nowNs();setupNs=start-before;
    }
    void finish(const char* label,unsigned index){
        const auto end=NorthlightStream::nowNs();std::uint64_t times[4]{},cycles[4]{};unsigned counts[4]{};
        for(unsigned i=0;i<count;++i){std::uint64_t time=0,cycle=0;const auto& item=items[i];if(read(item.handle,time,cycle)&&time>=item.time&&cycle>=item.cycles){times[item.group]+=time-item.time;cycles[item.group]+=cycle-item.cycles;++counts[item.group];}}
        std::printf("THREAD_PROBE label=%s index=%u intervalMs=%.4f setupMs=%.4f csThreads=%u csCpuMs=%.4f csCycles=%llu compilerThreads=%u compilerCpuMs=%.4f compilerCycles=%llu otherThreads=%u otherCpuMs=%.4f\n",label,index,double(end-start)/1e6,double(setupNs)/1e6,counts[1],times[1]/1e4,(unsigned long long)cycles[1],counts[2],times[2]/1e4,(unsigned long long)cycles[2],counts[3],times[3]/1e4);
        clear();
    }
};
