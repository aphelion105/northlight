#pragma once
#include "native_shadow_control.h"
#include "blp_mip_guard.h"
#include "client_code_observation.h"
#include "world_context.h"
#include <algorithm>
#include <atomic>

namespace NorthlightNativeShadowControl {
#if defined(_WIN64) && !defined(NORTHLIGHT_REPLAY_STAND)
inline constexpr std::uintptr_t X64Quality=0x1926bcc,X64Dirty=0xe22474,X64CVar=0x1b73450;
#endif
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
#elif defined(_WIN64)
    // The extShadowQuality callback writes the runtime level and dirty bit.
    // The producer consumes the dirty bit and skips its maps at runtime level 0.
    static const bool supported=[]{
        constexpr NorthlightX64World::CodeProof proof[]={
            {0x6bc148,72,UINT64_C(0x7ba88348f7a86cf1)}, {0x2675c7,21,UINT64_C(0x8b87c2c14c525390)},
            {0x267638,39,UINT64_C(0x75413e981bf90592)}, {0x26769b,24,UINT64_C(0x91a001b43a5c8ebf)},
        };
        return NorthlightX64World::verifyCode(NorthlightX64World::legacyImageBase(),proof);
    }();
    return supported;
#else
    static const bool supported=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))==0x400000&&
        verifyCode(NorthlightWorldContext::readSelfLive);
    return supported;
#endif
}
#if defined(_WIN64) && !defined(NORTHLIGHT_REPLAY_STAND)
template<class Read> State sampleX64(Read read){
    const auto base=NorthlightX64World::legacyImageBase();
    State result;
    uintptr_t cvar=0;
    constexpr uintptr_t NumericValue=0x54;
    if(!verified()||!read(base+X64CVar,&cvar,sizeof cvar)||cvar<0x10000||cvar>UINTPTR_MAX-NumericValue-4||(cvar&7)||
       !read(cvar+NumericValue,&result.configured,sizeof result.configured)||result.configured>5||
       !read(base+X64Quality,&result.runtime,sizeof result.runtime)||result.runtime>5||
       !read(base+X64Dirty,&result.pending,sizeof result.pending)||result.pending>1)return {};
    result.readable=true;return result;
}
#endif
template<class Snapshot> State sampleRendererFrame(Snapshot* snapshot,bool streamed){
#if defined(_WIN64) && !defined(NORTHLIGHT_REPLAY_STAND)
    if(streamed&&!snapshot)return {};
    return sampleX64(static_cast<bool(*)(uintptr_t,void*,size_t)>(NorthlightX64World::readFrame));
#else
    return sampleFrame(snapshot,streamed,NorthlightWorldContext::readSelf);
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
#ifdef _WIN64
    const State state=sampleX64(static_cast<bool(*)(uintptr_t,void*,size_t)>(NorthlightX64World::read));
    const auto base=NorthlightX64World::legacyImageBase();
    controller.update(state,replace,[&](std::uintptr_t slot,unsigned value){
        const auto address=base+(slot==Quality?X64Quality:X64Dirty);
        SIZE_T written=0;
        return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof value,&written)&&written==sizeof value;
    });
#else
    const State state=sample(NorthlightBlpMipGuard::read);
    controller.update(state,replace,[](std::uintptr_t address,unsigned value){SIZE_T written=0;return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,4,&written)&&written==4;});
#endif
#endif
}
} // namespace NorthlightNativeShadowControl
