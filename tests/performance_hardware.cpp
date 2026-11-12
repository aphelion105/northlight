#include "performance_hardware.h"
#include <cassert>
#include <cstdio>
int main(){
    using namespace NorthlightPerformance;Adapter a;a.owner=0x1234;
    HMODULE module=systemModule(L"d3d9.dll");assert(module);auto create=symbol<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>(module,"Direct3DCreate9Ex");assert(create);IDirect3D9Ex* api=nullptr;assert(SUCCEEDED(create(D3D_SDK_VERSION,&api)));
    LUID luid{};assert(SUCCEEDED(api->GetAdapterLUID(0,&luid)));a.luid=uint64_t(uint32_t(luid.HighPart))<<32|luid.LowPart;api->Release();FreeLibrary(module);
    store().touch(a.owner);HardwareSampler sampler(a);Hardware h;
    for(unsigned i=0;i<280;++i){store().touch(a.owner);Sleep(10);sampler.read(h);if(h.at&&h.cpu>=0&&h.gameCpu>=0)break;}
    assert(h.at&&h.ramTotal&&h.cpu>=0&&h.cpu<=100&&h.gameCpu>=0&&h.gameCpu<=100&&h.matched&&h.gpuName[0]);
    std::printf("PERFORMANCE_HARDWARE_PASS cpu=%.2f gameCpu=%.2f gpu=%.2f temperature=%.1f clockMHz=%.0f power=%.1f ram=%llu/%llu gameRam=%llu vram=%llu/%llu gameVram=%llu boardMemory=%u gameMemory=%u gpuName=%s cpuName=%s source=%s\n",h.cpu,h.gameCpu,h.gpu,h.temperature,h.clockMHz,h.powerW,(unsigned long long)h.ramUsed,(unsigned long long)h.ramTotal,(unsigned long long)h.gameRam,(unsigned long long)h.vramUsed,(unsigned long long)h.vramTotal,(unsigned long long)h.gameVram,h.boardMemory,h.gameMemory,h.gpuName,h.cpuName,h.gpuSource);
}
