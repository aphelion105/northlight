#pragma once
#include "blp_mip_guard.h"
#include "native_light_stay.h"

#include "native_more_lights.h"
#include "glue_lighting_selection_code.h"
#include "menu_scene.h"

// Keep the menu backdrop's authored sources on every receiver in that scene.
// The native world selection is untouched. The producer validates the published
// menu identity before copying scene values into the command stream.
namespace NorthlightGlueLighting {
inline constexpr uintptr_t SetupRva=0x431af0,CallOffset=0x68;
inline constexpr size_t SetupSize=0xb3;

inline const char* install(HMODULE image=GetModuleHandleW(nullptr)){
#if defined(__i386__) || defined(_M_IX86)
    using namespace NorthlightBlpMipGuard;
    const auto base=reinterpret_cast<uintptr_t>(image);
    if(base!=0x400000)return "unsupported-image-base";
    unsigned char setup[SetupSize],select[400],layout[166];
    // Prove the world-scene global and intrusive scene/model list before the thunk uses them.
    const unsigned char worldProof[]={0x8b,0x35,0x4c,0x75,0xcd,0x00};
    const unsigned char listProof[]={0x8d,0x42,0x08,0x89,0x46,0x08,0x89,0x56,0x28,0x8b,0x38,0x8d,0x4e,0x0c};
    unsigned char worldBytes[sizeof worldProof],listBytes[sizeof listProof];
    if(!read(0x79abdf,worldBytes,sizeof worldBytes)||std::memcmp(worldBytes,worldProof,sizeof worldBytes)||
       !read(0x834550,listBytes,sizeof listBytes)||std::memcmp(listBytes,listProof,sizeof listBytes))return "scene-layout-mismatch";
    // M2 retains its camera at scene+0x84/+0xc4 but submits identity to Gx while
    // drawing. The menu snapshot must not confuse that temporary view with UI.
    unsigned char cameraSetup[115],drawView[130];
    if(!read(0x821a4d,cameraSetup,sizeof cameraSetup)||!digest(cameraSetup,sizeof cameraSetup,"94b3c1517468fef88745efe33cd49e077a8d6d9d334ce7ae45d85956ee65dca6")||
       !read(0x8236c1,drawView,sizeof drawView)||!digest(drawView,sizeof drawView,"54592adeacd233ad42abd05e49684c92ac33d1464cfff4fec9f0621f90f6ef03"))return "menu-camera-layout-mismatch";
    unsigned char program[sizeof Selection];std::memcpy(program,Selection,sizeof program);
    const auto observer=std::uint32_t(reinterpret_cast<std::uintptr_t>(&NorthlightMenu::observation));
    std::memcpy(program+ObserverEntryOffset,&observer,4);std::memcpy(program+ObserverClearOffset,&observer,4);
    std::uintptr_t extra=0;
    if(NorthlightNativeMoreLights::existing(extra)){
        if(!NorthlightNativeMoreLights::verified(true))return "extra-light-layout-mismatch";
        const auto entry=std::uint32_t(extra);std::memcpy(program+ExtraEntryOffset,&entry,4);
    }
    if(!read(base+SetupRva,setup,sizeof setup)||!read(base+0x41e400,select,sizeof select)||
       !read(base+0x4333d0,layout,sizeof layout))return "unsupported-client";
    if(!NorthlightNativeLightStay::knownQuery(select)||
       !digest(layout,sizeof layout,"b910edc9143293226bc9ca203642a7f7cb85f4645e07f6842fa9e14d17b4fded"))return "client-layout-mismatch";
    auto* call=reinterpret_cast<unsigned char*>(base+SetupRva+CallOffset);
    int32_t original=int32_t((base+0x41e400)-(reinterpret_cast<uintptr_t>(call)+5));
    unsigned char expected[5]={0xe8};std::memcpy(expected+1,&original,4);
    const bool changed=std::memcmp(setup+CallOffset,expected,5)!=0;
    if(changed){
        if(setup[CallOffset]!=0xe8)return "call-site-mismatch";
        int32_t relative;std::memcpy(&relative,setup+CallOffset+1,4);
        unsigned char current[sizeof Selection];
        if(!read(reinterpret_cast<uintptr_t>(call)+5+relative,current,sizeof current)||
           std::memcmp(current,program,sizeof current))return "call-site-mismatch";
        std::memcpy(setup+CallOffset,expected,5);
    }
    if(!digest(setup,sizeof setup,"50a8b1fd89da572ac8d871c185509e28da71da92488363ea2027b404392878f5"))return "setup-signature-mismatch";
    if(changed)return "already-active";
    auto* thunk=static_cast<unsigned char*>(VirtualAlloc(nullptr,sizeof Selection,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!thunk)return "allocation-failed";
    std::memcpy(thunk,program,sizeof program);
    DWORD ignored=0;
    if(!VirtualProtect(thunk,sizeof Selection,PAGE_EXECUTE_READ,&ignored)||!FlushInstructionCache(GetCurrentProcess(),thunk,sizeof Selection)){
        VirtualFree(thunk,0,MEM_RELEASE);return "thunk-publication-failed";
    }
    PausedThreads threads;
    if(const auto* error=threads.pause(base+SetupRva,SetupSize)){VirtualFree(thunk,0,MEM_RELEASE);return error;}
    unsigned char current[SetupSize];
    if(!read(base+SetupRva,current,sizeof current)||std::memcmp(current,setup,sizeof current)){
        VirtualFree(thunk,0,MEM_RELEASE);return "setup-changed-concurrently";
    }
    DWORD old=0;
    if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old)){VirtualFree(thunk,0,MEM_RELEASE);return "code-protection-failed";}
    unsigned char replacement[5]={0xe8};
    const int32_t relative=int32_t(reinterpret_cast<uintptr_t>(thunk)-(reinterpret_cast<uintptr_t>(call)+5));
    std::memcpy(replacement+1,&relative,4);std::memcpy(call,replacement,5);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),call,5)!=FALSE;
    if(flushed&&VirtualProtect(call,5,old,&ignored))return "active";
    std::memcpy(call,expected,5);
    const bool restored=FlushInstructionCache(GetCurrentProcess(),call,5)!=FALSE;
    const bool protectedAgain=VirtualProtect(call,5,old,&ignored)!=FALSE;
    VirtualFree(thunk,0,MEM_RELEASE);
    return !restored?"rollback-cache-failed":!protectedAgain?"rollback-protection-failed":"call-publication-failed";
#else
    (void)image;return "requires-x86";
#endif
}
} // namespace NorthlightGlueLighting
