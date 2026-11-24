#pragma once

#ifdef NORTHLIGHT_REPLAY_STAND
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace NorthlightCaptureWork {
struct Work {
    size_t chargedBytes=0,hits=0,misses=0,packets=0,geometryReuse=0,bankReuse=0,boundsTests=0;
    size_t floatGets=0,floatHandoff=0;
    size_t rangeReads=0,rangeHandoff=0;

    template<class Frame> void addSnapshots(const Frame& frame){
        chargedBytes+=frame.bytesRead();
        hits+=frame.snapshotCacheHits();
        misses+=frame.snapshotCacheMisses();
    }
};

struct Route {
    enum Lane : unsigned {Material=1,Normals=2,World=4,TerrainFirst=8};
    enum class ReadPath : char {Buffer='B', Fill='F', Copy='C'};
    using ReportRead=void(*)(void*,Lane,const void*,bool,ReadPath,unsigned,unsigned,bool);
    unsigned lanes=0;
    uint64_t source=0;
    size_t sourceBytes=0;
    bool pose=false,bank=false;
    ReportRead reportRead=nullptr;
    void* context=nullptr;
};

// The consumer scope excludes the water and draw submission stages. Retain the
// enclosing scope on reentry. A disabled nested route must not charge its caller.
class BufferReads {
    static inline thread_local BufferReads* active_=nullptr;
    BufferReads* previous_;
    const Route* route_;
    Route::Lane lane_;
public:
    BufferReads(const Route* route,Route::Lane lane):previous_(active_),route_(route),lane_(lane){active_=this;}
    ~BufferReads(){active_=previous_;}
    BufferReads(const BufferReads&)=delete;
    BufferReads& operator=(const BufferReads&)=delete;
    static void record(const void* buffer,bool index,Route::ReadPath path,unsigned offset,unsigned size,bool ok){
        if(active_&&active_->route_&&active_->route_->reportRead)
            active_->route_->reportRead(active_->route_->context,active_->lane_,buffer,index,path,offset,size,ok);
    }
};

// One renderer frame per diagnostic run. No production draw-path overhead.
inline bool selected(unsigned frame){
    static const unsigned wanted=[] {
        const char* value=std::getenv("NORTHLIGHT_RETRACE_CAPTURE_WORK_FRAME");
        if(!value||!*value)return 0u;
        char* end=nullptr;
        const auto parsed=std::strtoull(value,&end,10);
        return *end||parsed>0xffffffffUL?0u:unsigned(parsed);
    }();
    return wanted&&frame==wanted;
}
}
#endif
