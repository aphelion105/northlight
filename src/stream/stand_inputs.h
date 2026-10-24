#pragma once
#include "client_image.h"
#include "snapshot_store.h"

// Explicit offline host only. The image is borrowed immutable file data;
// runtime values come exclusively from an admitted recorded snapshot.
namespace NorthlightStream::StandInputs {
class Session {
    ClientImage::View image_;GameSnapshot* snapshot_=nullptr;
    bool bound_=false,incomplete_=false;std::uint64_t previousDraw_=0;
    static inline std::atomic<std::uint64_t> serial_{0};
    const std::uint64_t identity_=serial_.fetch_add(1,std::memory_order_relaxed)+1;
public:
    std::uint64_t staticReads=0,dynamicReads=0,recordedFailures=0,missingReads=0;
    explicit Session(const ClientImage::View& image):image_(image){}
    Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    bool valid()const{return identity_&&image_.valid()&&!incomplete_;}
    void reject(){incomplete_=true;}
    std::uint64_t identity()const{return identity_;}
    std::uint32_t targetBase()const{return valid()?image_.imageBase():0;}
    // The caller must separately prove device/epoch/trace-call identity. A draw
    // number in an index is not sufficient evidence of that join.
    bool bind(GameSnapshot& snapshot,std::uint64_t draw,unsigned trigger){
        if(!valid()||!snapshot.usable()||trigger<1||trigger>3||snapshot.triggerKind!=trigger||snapshot.triggerDraw!=draw||
           (bound_&&draw<=previousDraw_)){incomplete_=true;return false;}
        snapshot_=&snapshot;snapshot_->rewind();previousDraw_=draw;bound_=true;return true;
    }
    bool read(std::uintptr_t address,void* output,std::size_t bytes){
        if(!valid())return false;
        auto missing=[&](){++missingReads;incomplete_=true;return false;};
        if(!output||!bytes)return missing();
        const auto region=image_.region(address,bytes);
        if(region==ClientImage::Region::Static){
            if(!image_.readStatic(address,output,bytes))return missing();
            ++staticReads;return true;
        }
        if(region==ClientImage::Region::Missing||!snapshot_)return missing();
        // Match production playback: per-key FIFO, then the freshest captured
        // copy for subsequent draws under the same trigger. Never host fallback.
        switch(snapshot_->serve(address,output,bytes)){
        case GameSnapshot::Serve::Ok:++dynamicReads;return true;
        case GameSnapshot::Serve::Failed:++recordedFailures;return false;
        default:return missing();}
    }
};
inline thread_local Session* active=nullptr;
class Scope {
    static inline std::atomic<std::uint64_t> frozenIdentity_{0};
    static inline std::atomic<bool> occupied_{false};
    bool entered_=false;
public:
    explicit Scope(Session& session){
        if(!session.valid()||active)return;
        bool expected=false;if(!occupied_.compare_exchange_strong(expected,true))return;
        std::uint64_t identity=0;
        if(!frozenIdentity_.compare_exchange_strong(identity,session.identity())&&identity!=session.identity()){
            occupied_.store(false);return;
        }
        active=&session;entered_=true;
    }
    ~Scope(){if(entered_){active=nullptr;occupied_.store(false);}}
    bool entered()const{return entered_;}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
}
