#pragma once
#include "blp_mip_guard.h"
#include <array>
#include <atomic>
#include <cmath>

namespace NorthlightNativeViewDistance {
inline std::atomic<bool> requested{false};
inline void request(bool enabled){requested.store(enabled,std::memory_order_release);}
inline bool enabled(){return requested.load(std::memory_order_acquire);}

#if !defined(NORTHLIGHT_NATIVE_GX) && !defined(NORTHLIGHT_REPLAY_STAND)
struct Proof {uintptr_t rva;size_t size;const char* hash;};
struct Client {
    std::array<Proof,3> proofs;
    uintptr_t current,cvar,limits;
};
inline const Client* verifiedClient(){
    static const Client* const verified=[]()->const Client*{
        static constexpr Client clients[]={
#ifdef _WIN64
            {{{{0x4e9140,0x56,"04e188ecfe845931a7a7a6ed1aaa74fbe9a60f161c321a39f8bcc9269e029125"},
               {0x4eb8b0,0x90,"d04c995b5ee25e9fac2a3f31515b84b2db3b3adec4b7c23d9aa80de62704d06e"},
               {0x4e2e4f,0x28,"f628f43ce22bacd7073b2b289b8b4021fc32768bf58f0fab4c3b06383f84d19c"}}},
               0xac9a78,0xad0660,0x77c430},
            {{{{0x6b4840,0x56,"ff43abe438b7611faf8b2dee022a97c1dca36f35b322f2b7f64d8e798cb1fd59"},
               {0x6b7670,0x1e,"af5fe0b432a2de5c3af65d4230eec0e8334996ea330b6506a0c3dc71a3bc36cd"},
               {0x6acaef,0x27,"13ae3ceb77e85e2a7fdc59aaf64706c3d45aef7c4cef054af13551f6a94a3dac"}}},
               0x1b6a748,0x1b733a0,0},
#else
            {{{{0x380800,0x5e,"6a162feea3e44e8d8dc471a87d298d195f01bd909ea08f08b88996f56cca83f3"},
               {0x380770,0x88,"f13484676028c5bd5a54fc0f62725a71787314c6085ce85afe38275d73f96341"},
               {0x381430,0x24,"1b88d111cde25983013c32dcb647a08653c7979c3c1e1da89b2954447e714ffb"}}},
               0x8d7748,0x8d8580,0x63e70c},
#endif
        };
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
#ifndef _WIN64
        if(base!=0x400000)return nullptr; // Absolute addresses are part of the x86 proofs.
#endif
        for(const auto& client:clients){
            bool valid=true;
            for(const auto& proof:client.proofs){
                unsigned char bytes[0x90];
                if(!NorthlightBlpMipGuard::read(base+proof.rva,bytes,proof.size)||
                   !NorthlightBlpMipGuard::digest(bytes,proof.size,proof.hash)){
                    valid=false;break;
                }
            }
            if(valid)return &client;
        }
        return nullptr;
    }();
    return verified;
}

using Limits=std::array<LONG,2>;
inline constexpr Limits Stock{{0x4445eaab,0x44c5eaab}},Raised{{0x461c4000,0x461c4000}};
inline bool replaceLimits(uintptr_t address,const Limits& expected,const Limits& next){
    Limits current{};
    if(!NorthlightBlpMipGuard::read(address,current.data(),sizeof current)||current!=expected)return false;
    auto* target=reinterpret_cast<volatile LONG*>(address);
    auto* page=reinterpret_cast<void*>(address);
    DWORD protection=0,ignored=0;
    if(!VirtualProtect(page,sizeof current,PAGE_READWRITE,&protection))return false;
    for(size_t i=0;i<next.size();++i)InterlockedExchange(target+i,next[i]);
    if(VirtualProtect(page,sizeof current,protection,&ignored))return true;
    for(size_t i=0;i<current.size();++i)InterlockedExchange(target+i,current[i]);
    VirtualProtect(page,sizeof current,protection,&ignored);
    return false;
}
#endif

inline bool available(){
#if defined(NORTHLIGHT_NATIVE_GX)
    return true;
#elif defined(NORTHLIGHT_REPLAY_STAND)
    return false;
#else
    return verifiedClient()!=nullptr;
#endif
}

// Present is consumed on the game thread. Use the native setter so the engine
// updates its streaming, camera and dirty state together.
inline void producerPresent(){
#if !defined(NORTHLIGHT_NATIVE_GX) && !defined(NORTHLIGHT_REPLAY_STAND)
    static bool active=false,ownedLimits=false;
    const bool extend=enabled();
    if(!extend&&!active)return;
    const auto* client=verifiedClient();
    if(!client)return;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    constexpr float Distance=10000.0f;
    if(extend&&active){
        float current=0;
        if(!NorthlightBlpMipGuard::read(base+client->current,&current,sizeof current)||current==Distance)return;
    }
    constexpr uintptr_t FloatValue=sizeof(uintptr_t)==8?0x50:0x2c;
    uintptr_t cvar=0;
    float configured=0;
    if(!NorthlightBlpMipGuard::read(base+client->cvar,&cvar,sizeof cvar)||cvar<0x10000||cvar>UINTPTR_MAX-FloatValue-sizeof configured||
       !NorthlightBlpMipGuard::read(cvar+FloatValue,&configured,sizeof configured)||!std::isfinite(configured))return;
    if(client->limits&&extend!=active){
        const auto address=base+client->limits;
        Limits limits{};
        if(!NorthlightBlpMipGuard::read(address,limits.data(),sizeof limits))return;
        if(extend&&limits!=Raised){
            if(!replaceLimits(address,Stock,Raised))return;
            ownedLimits=true;
        }
        if(!extend&&ownedLimits){
            if(limits==Raised&&!replaceLimits(address,Raised,Stock))return;
            ownedLimits=false;
        }
    }
    using SetFarClip=void(__cdecl*)(float);
    reinterpret_cast<SetFarClip>(base+client->proofs[0].rva)(extend?Distance:configured);
    active=extend;
#endif
}
}
