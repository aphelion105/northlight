#pragma once
#include <atomic>
#include <istream>
#include <sstream>
#include <string>
#include <vector>
#include "quality_settings.h"
// task 42: MemMap (northlight-quality.ini). Cached relaxed bools so a disabled feature costs one load.
// Some MEMMAP points (backend-loaded, pre-create) run before WorldRenderer::loadQuality, so ensureLoaded() parses the same
// file on first use; loadQuality() stores the final values again. Until set, MemMap is off (no walk can run unconfigured).
namespace NorthlightMemorySwitches {
inline std::atomic<bool> memMapFlag{false},loadedFlag{false};
inline bool memMap(){return memMapFlag.load(std::memory_order_relaxed);}
inline void configure(bool memMapOn){memMapFlag.store(memMapOn,std::memory_order_relaxed);loadedFlag.store(true,std::memory_order_relaxed);}
// Text of northlight-quality.ini (hasFile=false: absent). Unreadable or throwing: MemMap off.
inline void configureFromText(const std::string& own,bool hasFile){
    try{std::istringstream in(NorthlightQuality::narrow(own));std::vector<std::string> problems;
        const NorthlightQuality::Settings s=NorthlightQuality::load(hasFile?&in:nullptr,nullptr,problems);
        configure(s.memMap!=0);}
    catch(...){configure(false);}
}
inline bool loaded(){return loadedFlag.load(std::memory_order_relaxed);}
}
