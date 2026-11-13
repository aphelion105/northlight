#pragma once
#include "blp_mip_guard.h"

// Build 12340 QueryLights: a stale model draw stamp must not disable its light.
// The two-byte branch keeps its native target; no DLL callback or file write.
namespace NorthlightNativeLightStay {
inline constexpr uintptr_t QueryRva=0x41e400,BranchOffset=0x148;
inline constexpr size_t QuerySize=400;
inline bool knownQuery(const unsigned char (&code)[QuerySize]){
    if(code[BranchOffset]!=0x74&&code[BranchOffset]!=0xeb)return false;
    unsigned char original[QuerySize];std::memcpy(original,code,sizeof original);original[BranchOffset]=0x74;
    return NorthlightBlpMipGuard::digest(original,sizeof original,"0d28ec8c69bda0bb2e0a6ff8ef04d6271d898b5be15636a1d4ab70fdef71aa32");
}
inline const char* install(bool enabled=true,HMODULE image=GetModuleHandleW(nullptr)
#if defined(__i386__) || defined(_M_IX86)
    ,NorthlightBlpMipGuard::MemoryOps ops={}
#endif
){
    if(!enabled)return "disabled";
#if defined(__i386__) || defined(_M_IX86)
    using namespace NorthlightBlpMipGuard;
    const auto base=reinterpret_cast<uintptr_t>(image);
    if(base!=0x400000)return "unsupported-image-base";
    unsigned char before[QuerySize];
    if(!read(base+QueryRva,before,sizeof before))return "client-code-unreadable";
    if(!knownQuery(before))return "query-signature-mismatch";
    if(before[BranchOffset]==0xeb)return "already-active";
    auto* target=reinterpret_cast<unsigned char*>(base+QueryRva+BranchOffset);
    PausedThreads threads;
    if(const auto* error=threads.pause(reinterpret_cast<uintptr_t>(target),2))return error;
    unsigned char current[QuerySize];
    if(!read(base+QueryRva,current,sizeof current)||std::memcmp(current,before,sizeof current))return "query-changed-concurrently";
    DWORD old=0,ignored=0;
    if(!ops.protect(target,1,PAGE_EXECUTE_READWRITE,&old))return "code-protection-failed";
    *target=0xeb;
    const bool flushed=ops.flush(GetCurrentProcess(),target,1)!=FALSE;
    if(flushed&&ops.protect(target,1,old,&ignored))return "active";
    *target=before[BranchOffset];
    const bool restored=ops.flush(GetCurrentProcess(),target,1)!=FALSE;
    const bool protectedAgain=ops.protect(target,1,old,&ignored)!=FALSE;
    return !restored?"rollback-cache-failed":!protectedAgain?"rollback-protection-failed":
        flushed?"code-protection-restore-failed":"code-cache-flush-failed";
#else
    (void)image;return "requires-x86";
#endif
}
} // namespace NorthlightNativeLightStay
