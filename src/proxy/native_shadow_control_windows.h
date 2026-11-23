#pragma once
#include "native_shadow_control.h"
#include "blp_mip_guard.h"
#include "client_code_observation.h"
#include "world_context.h"
#include <algorithm>
#include <atomic>

namespace NorthlightNativeShadowControl {
template<class Read> bool verifyCode(Read read){
    struct Proof {std::uintptr_t address;unsigned size;const char* sha;};
    const Proof proof[]={
        {0x873f80,18,"6ba473f22d8c83766c32d4ac280cf382eabc76e0bfb7bd7d00b3df4bf4b1c016"},
        {0x874210,44,"9aed82fe89b840c5f5319bf4b497815a1defdfebccbc62a0c36e8ef09dc4c2f8"},
        {0x875f80,983,"904fd1e43a4b9b4db3c44b4afe6bf0f6e7ca52bc6a4ba04de00142de4e329606"},
        {0x78e85a,50,"afe91101ea58a2f456e115fabbc3e7991c458e415510abdd34c735a6962bfb64"}};
    constexpr unsigned chunk=NorthlightStream::ClientCodeObservation::MaxSpanBytes;
    unsigned char bytes[983];
    for(const auto& p:proof){
        for(unsigned offset=0;offset<p.size;offset+=chunk)
            if(!read(p.address+offset,bytes+offset,std::min(chunk,p.size-offset)))return false;
        if(!NorthlightBlpMipGuard::digest(bytes,p.size,p.sha))return false;
    }
    return true;
}
inline bool verified(){
#ifdef NORTHLIGHT_REPLAY_STAND
    auto* context=NorthlightStream::StandInputs::currentContext();
    if(!context||context->targetBase()!=0x400000)return false;
    static thread_local std::uint64_t identity=0;
    static thread_local bool supported=false;
    if(identity!=context->identity()){
        identity=context->identity();
        supported=verifyCode([&](auto address,auto output,auto size){return context->readObservedCode(address,output,size);});
    }
    return supported;
#else
    static const bool supported=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))==0x400000&&
        verifyCode(NorthlightWorldContext::readSelfLive);
    return supported;
#endif
}
inline std::atomic<std::uintptr_t> owner{0};
inline std::atomic<DWORD> requestedAt{0};
inline std::atomic<bool> wanted{false};
inline Controller controller; // Only producerPresent() on the game thread accesses it.
inline void publish(const void* device,bool replace){
    const auto id=reinterpret_cast<std::uintptr_t>(device);auto current=owner.load(std::memory_order_acquire);
    if(replace&&current==0){owner.compare_exchange_strong(current,id,std::memory_order_acq_rel);current=owner.load(std::memory_order_acquire);}
    if(current!=id)return;
    requestedAt.store(GetTickCount(),std::memory_order_relaxed);wanted.store(replace,std::memory_order_release);
}
inline void release(const void* device){
    auto id=reinterpret_cast<std::uintptr_t>(device);
    if(owner.load(std::memory_order_acquire)!=id)return;
    wanted.store(false,std::memory_order_release);owner.compare_exchange_strong(id,0,std::memory_order_acq_rel);
}
// No callbacks or instruction patches in the client; its own dirty path frees
// and recreates the maps on a provider change. The saved CVar is never written.
inline void producerPresent(){
#ifndef NORTHLIGHT_REPLAY_STAND
    const bool replace=owner.load(std::memory_order_acquire)&&wanted.load(std::memory_order_acquire)&&DWORD(GetTickCount()-requestedAt.load(std::memory_order_relaxed))<1000;
    if((!replace&&!controller.leased())||!verified())return;
    const State state=sample(NorthlightBlpMipGuard::read);
    controller.update(state,replace,[](std::uintptr_t address,unsigned value){SIZE_T written=0;return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,4,&written)&&written==4;});
#endif
}
} // namespace NorthlightNativeShadowControl
