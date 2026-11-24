#pragma once
#include <string>

namespace NorthlightDxvkCompatibility {
// RESZ is exposed AND executed only with the AMD D3D9 identity (still true in DXVK 3.1.1).
// Captures read game buffers on the CPU; their mappings must be CPU-cached on
// discrete GPUs. 0.3.189: DXVK 2.x reads d3d9.cachedDynamicBuffers, >=3.0 renamed it
// d3d9.cachedWriteOnlyBuffers (it applies to DYNAMIC directly-mapped buffers, which captures
// read on the CPU); each version ignores the other's unknown key, so both are set.
// DXVK 3.1.1's descriptor heap produces blank autogen mips on NVIDIA 621.1.0;
// the descriptor-buffer path preserves fresh and restored material maps.
// DXVK_CONFIG is process-local and overrides only these file options. All other
// file options and explicit inline overrides survive.
inline std::string config(const std::string& existing,bool disableFse=false){
    constexpr char defaults[]="d3d9.customVendorId = 1002; d3d9.cachedDynamicBuffers = True; d3d9.cachedWriteOnlyBuffers = True; dxvk.enableDescriptorHeap = False; ";
    constexpr char forceFseOff[]="; dxvk.allowFse = False; ";
    // The process environment survives a proxy reload. DXVK 2.7.1 ignores
    // DXVK_CONFIG at MAX_PATH, which two copies of these defaults can exceed.
    std::string result=existing.compare(0,sizeof(defaults)-1,defaults)==0?existing:defaults+existing;
    constexpr size_t suffixLength=sizeof(forceFseOff)-1;
    if(disableFse&&result.compare(result.size()-suffixLength,suffixLength,forceFseOff)!=0)
        result+=forceFseOff;
    return result;
}
}
