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
// DXVK_CONFIG is process-local. Explicit inline overrides follow these defaults.
inline std::string config(const std::string& existing,bool disableFse=false,bool latencyX86=false){
    constexpr char legacy[]="d3d9.customVendorId = 1002; d3d9.cachedDynamicBuffers = True; d3d9.cachedWriteOnlyBuffers = True; dxvk.enableDescriptorHeap = False; ";
    // The packaged DXVK reads DXVK_CONFIG into a MAX_PATH buffer. Use the
    // current buffer option and compact assignments on the audited x86 build.
    constexpr char paced[]="d3d9.customVendorId=1002;d3d9.cachedWriteOnlyBuffers=True;dxvk.enableDescriptorHeap=False;"
        "dxvk.enableGraphicsPipelineLibrary=True;d3d9.maxFrameLatency=1;dxvk.latencySleep=True;dxvk.disableNvLowLatency2=True;";
    constexpr char forceFseOff[]="; dxvk.allowFse = False; ";
    constexpr size_t suffixLength=sizeof(forceFseOff)-1;
    // Keep the compatibility options when explicit overrides leave too little room for pacing defaults.
    const bool havePaced=existing.compare(0,sizeof(paced)-1,paced)==0;
    const bool fitsPaced=existing.size()+(sizeof(paced)-1)+(disableFse?suffixLength:0)<260;
    const std::string defaults=latencyX86&&(havePaced||fitsPaced)?paced:legacy;
    // The process environment survives a proxy reload.
    std::string result=existing.compare(0,defaults.size(),defaults)==0?existing:defaults+existing;
    if(disableFse&&result.compare(result.size()-suffixLength,suffixLength,forceFseOff)!=0)
        result+=forceFseOff;
    return result;
}
}
