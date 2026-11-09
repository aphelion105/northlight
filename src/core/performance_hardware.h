#pragma once
#include "performance_monitor.h"
#include <windows.h>
#include <d3d9.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <intrin.h>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace NorthlightPerformance {
struct Adapter {
    uintptr_t owner=0;uint64_t luid=0;unsigned vendor=0,device=0,interval=0;HWND window=nullptr;char name[160]={};
};
struct Hardware {
    uint64_t at=0,ramUsed=0,ramTotal=0,gameRam=0,vramTotal=0,vramUsed=0,gameVram=0,gameVramBudget=0;
    double cpu=-1,gameCpu=-1,gpu=-1,temperature=-1,clockMHz=-1,powerW=-1;
    bool boardMemory=false,gameMemory=false,gameRamKnown=false,matched=false;unsigned windowWidth=0,windowHeight=0;
    char gpuName[160]={},cpuName[64]={},gpuSource[40]={};
};
template<class T> inline T symbol(HMODULE module,const char* name){return module?reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module,name))):nullptr;}
inline HMODULE systemModule(const wchar_t* name){wchar_t path[MAX_PATH]={};const auto n=GetSystemDirectoryW(path,MAX_PATH);if(!n||n>=MAX_PATH||n+1+wcslen(name)>=MAX_PATH)return nullptr;path[n]=L'\\';wcscpy(path+n+1,name);return LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);}
inline std::string utf8(const wchar_t* value){char out[256]={};WideCharToMultiByte(CP_UTF8,0,value,-1,out,sizeof(out)-1,nullptr,nullptr);return out;}

// Optional read-only NVAPI bindings. No driver controls, installation or static
// vendor dependency. ABI definitions follow NVIDIA's public MIT-licensed SDK.
class NvidiaSensors {
    HMODULE module_=nullptr;using Query=void*(__cdecl*)(unsigned);Query query_=nullptr;
    void* gpu_=nullptr;bool initialized_=false;
    template<class T> T api(unsigned id){return query_?reinterpret_cast<T>(query_(id)):nullptr;}
    using Call=int(__cdecl*)(void*,void*);
    Call utilization_=nullptr,clocks_=nullptr,memory_=nullptr;
    using Thermal=int(__cdecl*)(void*,unsigned,void*);Thermal thermal_=nullptr;
    struct Util {unsigned version,flags;struct{unsigned present,percent;} domains[8];};
    struct Clock {unsigned version,type;struct{unsigned present,kHz;} domains[32];};
    struct Temp {unsigned version,count;struct{int controller,low,high,current,target;} sensors[3];};
    struct alignas(8) Memory {unsigned version,padding;uint64_t dedicated,available,system,shared,currentAvailable,evicted,evictions,promoted,promotions;};
    static_assert(sizeof(Util)==72&&sizeof(Clock)==264&&sizeof(Temp)==68&&sizeof(Memory)==80);
public:
    NvidiaSensors(const char* selected,bool enabled){
        if(!enabled)return;
        module_=systemModule(sizeof(void*)==8?L"nvapi64.dll":L"nvapi.dll");query_=symbol<Query>(module_,"nvapi_QueryInterface");
        const auto init=api<int(__cdecl*)()>(0x0150e828);if(!init||init()!=0)return;initialized_=true;
        const auto enumerate=api<int(__cdecl*)(void**,unsigned*)>(0xe5ac921f);const auto name=api<int(__cdecl*)(void*,char*)>(0xceee8e9f);
        if(!enumerate||!name)return;void* devices[64]={};unsigned count=0;if(enumerate(devices,&count)!=0||count>64)return;
        unsigned matches=0;for(unsigned i=0;i<count;++i){char text[64]={};if(name(devices[i],text)==0&&text[0]&&std::strstr(selected,text)){gpu_=devices[i];++matches;}}
        if(matches!=1){gpu_=nullptr;return;}
        utilization_=api<Call>(0x60ded2ed);clocks_=api<Call>(0xdcb616c3);thermal_=api<Thermal>(0xe3640a56);memory_=api<Call>(0xc0599498);
    }
    ~NvidiaSensors(){if(initialized_)if(auto unload=api<int(__cdecl*)()>(0xd22bdd7e))unload();if(module_)FreeLibrary(module_);}
    void sample(Hardware& out){if(!gpu_)return;
        Util u{};u.version=sizeof(u)|(1u<<16);if(utilization_&&utilization_(gpu_,&u)==0&&(u.domains[0].present&1)&&u.domains[0].percent<=100){out.gpu=u.domains[0].percent;std::strcpy(out.gpuSource,"Driver graphics engine");}
        Clock c{};c.version=sizeof(c)|(3u<<16);if(clocks_&&clocks_(gpu_,&c)==0&&(c.domains[0].present&1))out.clockMHz=c.domains[0].kHz/1000.;
        Temp t{};t.version=sizeof(t)|(2u<<16);if(thermal_&&thermal_(gpu_,15,&t)==0)for(unsigned i=0;i<std::min(t.count,3u);++i)if(t.sensors[i].target==1&&t.sensors[i].current>=-40&&t.sensors[i].current<=150)out.temperature=t.sensors[i].current;
        Memory m{};m.version=sizeof(m)|(1u<<16);if(memory_&&memory_(gpu_,&m)==0&&m.dedicated&&m.currentAvailable<=m.dedicated){out.vramTotal=m.dedicated;out.vramUsed=m.dedicated-m.currentAvailable;out.boardMemory=true;}
    }
};
class GpuLoad {
    HMODULE module_=nullptr;PDH_HQUERY query_=nullptr;PDH_HCOUNTER counter_=nullptr;
    decltype(&PdhCollectQueryData) collect_=nullptr;decltype(&PdhGetFormattedCounterArrayW) array_=nullptr;decltype(&PdhCloseQuery) close_=nullptr;
public:
    GpuLoad(){module_=systemModule(L"pdh.dll");auto open=symbol<decltype(&PdhOpenQueryW)>(module_,"PdhOpenQueryW");auto add=symbol<decltype(&PdhAddEnglishCounterW)>(module_,"PdhAddEnglishCounterW");
        collect_=symbol<decltype(collect_)>(module_,"PdhCollectQueryData");array_=symbol<decltype(array_)>(module_,"PdhGetFormattedCounterArrayW");close_=symbol<decltype(close_)>(module_,"PdhCloseQuery");
        if(open&&add&&collect_&&array_&&close_&&open(nullptr,0,&query_)==ERROR_SUCCESS&&add(query_,L"\\GPU Engine(*)\\Utilization Percentage",0,&counter_)==ERROR_SUCCESS)collect_(query_);
    }
    ~GpuLoad(){if(query_&&close_)close_(query_);if(module_)FreeLibrary(module_);}
    void sample(LUID luid,Hardware& out){
        if(!counter_||collect_(query_)!=ERROR_SUCCESS)return;DWORD bytes=0,count=0;
        if(array_(counter_,PDH_FMT_DOUBLE,&bytes,&count,nullptr)!=static_cast<PDH_STATUS>(PDH_MORE_DATA)||bytes>4u*1024u*1024u)return;
        std::vector<unsigned char> buffer(bytes);auto* items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if(array_(counter_,PDH_FMT_DOUBLE,&bytes,&count,items)!=ERROR_SUCCESS)return;
        std::map<std::pair<unsigned,unsigned>,double> engines;
        for(unsigned i=0;i<count;++i){unsigned pid=0,high=0,low=0,physical=0,engine=0;
            if(items[i].FmtValue.CStatus>1||std::swscanf(items[i].szName,L"pid_%u_luid_0x%x_0x%x_phys_%u_eng_%u",&pid,&high,&low,&physical,&engine)!=5||high!=unsigned(luid.HighPart)||low!=luid.LowPart)continue;
            const double value=items[i].FmtValue.doubleValue;if(std::isfinite(value)&&value>=0)engines[{physical,engine}]+=value;
        }
        if(!engines.empty()){out.gpu=0;for(const auto& e:engines)out.gpu=std::max(out.gpu,std::min(e.second,100.));std::strcpy(out.gpuSource,"Busiest WDDM engine");}
    }
};
class HardwareSampler {
    Adapter adapter_;std::atomic<bool> stop_{false};std::mutex dataMutex_,waitMutex_;std::condition_variable wake_;Hardware latest_;std::thread worker_;
    static uint64_t ticks(FILETIME t){return uint64_t(t.dwHighDateTime)<<32|t.dwLowDateTime;}
    void run(){
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        struct Graphics {HMODULE module=nullptr;IDXGIAdapter1* adapter=nullptr;IDXGIAdapter3* memory=nullptr;~Graphics(){if(memory)memory->Release();if(adapter)adapter->Release();if(module)FreeLibrary(module);}} graphics;
        graphics.module=systemModule(L"dxgi.dll");auto& dxgi=graphics.module;auto& selected=graphics.adapter;auto& memory=graphics.memory;DXGI_ADAPTER_DESC1 description{};
        using Create=HRESULT(WINAPI*)(REFIID,void**);auto create=symbol<Create>(dxgi,"CreateDXGIFactory1");IDXGIFactory1* factory=nullptr;
        if(create&&SUCCEEDED(create(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory)))){
            unsigned candidates=0;for(unsigned i=0;;++i){IDXGIAdapter1* a=nullptr;if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;if(!a)break;DXGI_ADAPTER_DESC1 d{};
                if(SUCCEEDED(a->GetDesc1(&d))&&!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){
                    const uint64_t luid=uint64_t(uint32_t(d.AdapterLuid.HighPart))<<32|d.AdapterLuid.LowPart;
                    const auto name=utf8(d.Description);
                    const bool match=adapter_.luid?luid==adapter_.luid:(d.DeviceId==adapter_.device&&(d.VendorId==adapter_.vendor||(adapter_.name[0]&&name==adapter_.name)));
                    if(match){++candidates;if(selected)selected->Release();selected=a;description=d;a=nullptr;}}
                if(a)a->Release();}
            if(candidates!=1&&selected){selected->Release();selected=nullptr;}factory->Release();
        }
        if(selected)selected->QueryInterface(__uuidof(IDXGIAdapter3),reinterpret_cast<void**>(&memory));
        Hardware base;base.matched=selected!=nullptr;
        if(selected){const auto name=utf8(description.Description);std::snprintf(base.gpuName,sizeof base.gpuName,"%s",name.c_str());base.vramTotal=description.DedicatedVideoMemory;}
        int cpu[4]={};__cpuid(cpu,int(0x80000000u));if(unsigned(cpu[0])>=0x80000004u){for(unsigned i=0;i<3;++i){__cpuid(cpu,int(0x80000002u+i));std::memcpy(base.cpuName+i*16,cpu,16);}}
        NvidiaSensors nvidia(base.gpuName,selected&&description.VendorId==0x10de);GpuLoad gpuLoad;
        using ProcessMemory=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);auto processMemory=symbol<ProcessMemory>(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo");
        FILETIME oldIdle{},oldKernel{},oldUser{},oldProcessKernel{},oldProcessUser{};uint64_t previous=0;bool primed=false;
        while(!stop_.load(std::memory_order_relaxed)){
            if(store().expire()&&store().wanted(adapter_.owner)){
                Hardware h=base;h.at=nowMs();FILETIME idle{},kernel{},user{},created{},exited{},pk{},pu{};
                const bool cpuOk=GetSystemTimes(&idle,&kernel,&user)&&GetProcessTimes(GetCurrentProcess(),&created,&exited,&pk,&pu);
                if(cpuOk&&primed&&h.at>previous){const auto total=ticks(kernel)-ticks(oldKernel)+ticks(user)-ticks(oldUser),free=ticks(idle)-ticks(oldIdle);const unsigned processors=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
                    if(processors<=64&&total&&free<=total)h.cpu=100.*(total-free)/total;
                    if(processors)h.gameCpu=std::clamp(double(ticks(pk)-ticks(oldProcessKernel)+ticks(pu)-ticks(oldProcessUser))/double(h.at-previous)/100./processors,0.,100.);}
                primed=cpuOk;previous=h.at;oldIdle=idle;oldKernel=kernel;oldUser=user;oldProcessKernel=pk;oldProcessUser=pu;
                MEMORYSTATUSEX m{};m.dwLength=sizeof m;if(GlobalMemoryStatusEx(&m)){h.ramTotal=m.ullTotalPhys;h.ramUsed=m.ullTotalPhys-m.ullAvailPhys;}
                PROCESS_MEMORY_COUNTERS pm{};if(processMemory&&processMemory(GetCurrentProcess(),&pm,sizeof pm)){h.gameRam=pm.WorkingSetSize;h.gameRamKnown=true;}
                if(memory){DXGI_QUERY_VIDEO_MEMORY_INFO v{};if(SUCCEEDED(memory->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&v))){h.gameVram=v.CurrentUsage;h.gameVramBudget=v.Budget;h.gameMemory=true;}}
                if(selected){gpuLoad.sample(description.AdapterLuid,h);nvidia.sample(h);}
                RECT r{};if(adapter_.window&&GetClientRect(adapter_.window,&r)){h.windowWidth=unsigned(std::max(0L,r.right-r.left));h.windowHeight=unsigned(std::max(0L,r.bottom-r.top));}
                std::lock_guard<std::mutex> lock(dataMutex_);latest_=h;
            }else primed=false;
            std::unique_lock<std::mutex> lock(waitMutex_);wake_.wait_for(lock,std::chrono::seconds(1),[&]{return stop_.load(std::memory_order_relaxed);});
        }
    }
public:
    explicit HardwareSampler(Adapter adapter):adapter_(adapter),worker_([this]{try{run();}catch(...){}}){}
    ~HardwareSampler(){stop_.store(true,std::memory_order_relaxed);wake_.notify_one();if(worker_.joinable())worker_.join();store().detach(adapter_.owner);}
    bool read(Hardware& out){std::unique_lock<std::mutex> lock(dataMutex_,std::try_to_lock);if(!lock)return false;out=latest_;return true;}
};
}
