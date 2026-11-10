#pragma once
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cassert>
namespace UpstreamStageProbe {
struct Row{char name[40];double ms;};
inline Row rows[64];inline unsigned count=0;
inline std::chrono::steady_clock::time_point previous;
inline void mark(const char* name){const auto now=std::chrono::steady_clock::now();if(!name){count=0;previous=now;return;}assert(count<64);auto& row=rows[count++];std::snprintf(row.name,sizeof row.name,"%s",name);row.ms=std::chrono::duration<double,std::milli>(now-previous).count();previous=now;}
inline void print(unsigned frame){for(unsigned i=0;i<count;++i)std::printf("COMMON_STAGE_CPU frame=%u stage=%s ms=%.9f\n",frame,rows[i].name,rows[i].ms);}
}
inline void northlightCompareStage(const char* name){UpstreamStageProbe::mark(name);}
