#pragma once
#include "client_image.h"
#include "snapshot_store.h"

// Explicit offline host only. The shared capability owns immutable PE bytes;
// the producer import cursor and queued playback snapshots remain separate.
namespace NorthlightStream::StandInputs {
class Context {
    std::vector<unsigned char> bytes_;ClientImage::View image_;
    std::atomic<bool> incomplete_{false};
    static inline std::atomic<std::uint64_t> serial_{0};
    const std::uint64_t identity_=serial_.fetch_add(1,std::memory_order_relaxed)+1;
public:
    std::atomic<std::uint64_t> staticReads{0},dynamicReads{0},recordedFailures{0},missingReads{0};
    std::atomic<std::uint64_t> workerStarts{0},workerStops{0};
    Context(const void* bytes,std::size_t size){
        if(!bytes||size<64||size>64u*1024u*1024u)return;
        const auto* first=static_cast<const unsigned char*>(bytes);bytes_.assign(first,first+size);
        ClientImage::Error error;image_.open(bytes_.data(),bytes_.size(),error);
    }
    Context(const Context&)=delete;Context& operator=(const Context&)=delete;
    bool valid()const{return identity_&&image_.valid()&&!incomplete_.load(std::memory_order_acquire);}
    void reject(){incomplete_.store(true,std::memory_order_release);}
    std::uint64_t identity()const{return identity_;}
    std::uint32_t targetBase()const{return valid()?image_.imageBase():0;}
    bool missing(){missingReads.fetch_add(1,std::memory_order_relaxed);reject();return false;}
    ClientImage::Region region(std::uintptr_t address,std::size_t bytes)const{return image_.region(address,bytes);}
    bool read(GameSnapshot* snapshot,std::uintptr_t address,void* output,std::size_t bytes){
        if(!valid())return false;
        if(!output||!bytes)return missing();
        const auto kind=region(address,bytes);
        if(kind==ClientImage::Region::Static){
            if(!image_.readStatic(address,output,bytes))return missing();
            staticReads.fetch_add(1,std::memory_order_relaxed);return true;
        }
        if(kind==ClientImage::Region::Missing||!snapshot)return missing();
        // The cursor belongs to the calling thread: either the imported owner
        // packet or the worker's existing queue-owned GameSnapshot.
        switch(snapshot->serve(address,output,bytes)){
        case GameSnapshot::Serve::Ok:dynamicReads.fetch_add(1,std::memory_order_relaxed);return true;
        case GameSnapshot::Serve::Failed:recordedFailures.fetch_add(1,std::memory_order_relaxed);return false;
        default:return missing();}
    }
};
class Session {
    std::shared_ptr<Context> context_;GameSnapshot* snapshot_=nullptr;
    bool bound_=false,captured_=false,capturing_=false;std::uint64_t previousDraw_=0;
public:
    Session(const void* bytes,std::size_t size):context_(std::make_shared<Context>(bytes,size)){}
    Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    bool valid()const{return context_->valid();}
    void reject(){context_->reject();}
    std::uint64_t identity()const{return context_->identity();}
    std::uint32_t targetBase()const{return context_->targetBase();}
    const std::shared_ptr<Context>& context()const{return context_;}
    // The caller must separately prove device/epoch/trace-call identity. A draw
    // number in an index is not sufficient evidence of that join.
    bool bind(GameSnapshot& snapshot,std::uint64_t draw,unsigned trigger){
        if(!valid()||capturing_||!snapshot.usable()||trigger<1||trigger>3||snapshot.triggerKind!=trigger||snapshot.triggerDraw!=draw||
           (bound_&&draw<=previousDraw_)){reject();return false;}
        snapshot_=&snapshot;snapshot_->rewind();previousDraw_=draw;bound_=true;captured_=false;return true;
    }
    bool beginCapture(unsigned trigger,std::uint64_t draw){
        if(!valid()||!bound_||captured_||capturing_||snapshot_->triggerKind!=trigger||snapshot_->triggerDraw!=draw){reject();return false;}
        snapshot_->rewind();captured_=capturing_=true;return true;
    }
    void endCapture(){capturing_=false;}
    bool read(std::uintptr_t address,void* output,std::size_t bytes){
        return context_->read(snapshot_,address,output,bytes);
    }
    bool record(GameSnapshot& destination,std::uintptr_t address,void* output,std::size_t bytes){
        // ScopedRecording clears destination. Never use it as the import source.
        if(!capturing_||snapshot_==&destination){reject();return false;}
        const auto kind=context_->region(address,bytes);
        const bool ok=read(address,output,bytes);
        if(valid()&&kind!=ClientImage::Region::Static&&!destination.record(address,output,bytes,ok))reject();
        return ok;
    }
};
inline thread_local Session* active=nullptr;
inline thread_local Context* playbackContext=nullptr;
class CaptureScope {
    Session* owner_;bool entered_;
public:
    CaptureScope(unsigned trigger,std::uint64_t draw):owner_(active),entered_(owner_&&owner_->beginCapture(trigger,draw)){}
    ~CaptureScope(){if(entered_)owner_->endCapture();}
    bool entered()const{return entered_;}
    CaptureScope(const CaptureScope&)=delete;CaptureScope& operator=(const CaptureScope&)=delete;
};
class Scope {
    static inline std::atomic<std::uint64_t> frozenIdentity_{0};
    static inline std::atomic<bool> occupied_{false};
    bool entered_=false;
public:
    explicit Scope(Session& session){
        if(!session.valid()||active||playbackContext)return;
        bool expected=false;if(!occupied_.compare_exchange_strong(expected,true))return;
        std::uint64_t identity=0;
        if(!frozenIdentity_.compare_exchange_strong(identity,session.identity())&&identity!=session.identity()){
            occupied_.store(false);return;
        }
        active=&session;entered_=true;
    }
    ~Scope(){if(entered_){active=nullptr;occupied_.store(false);}}
    static bool admitted(const Context& context){return frozenIdentity_.load(std::memory_order_acquire)==context.identity();}
    bool entered()const{return entered_;}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
class PlaybackScope {
    std::shared_ptr<Context> pin_;bool entered_=false;
public:
    explicit PlaybackScope(std::shared_ptr<Context> pin)noexcept:pin_(std::move(pin)){
        if(!pin_||!pin_->valid()||active||playbackContext||!Scope::admitted(*pin_))return;
        playbackContext=pin_.get();pin_->workerStarts.fetch_add(1,std::memory_order_relaxed);entered_=true;
    }
    ~PlaybackScope(){if(entered_){playbackContext=nullptr;pin_->workerStops.fetch_add(1,std::memory_order_relaxed);}}
    bool entered()const{return entered_;}
    PlaybackScope(const PlaybackScope&)=delete;PlaybackScope& operator=(const PlaybackScope&)=delete;
};
inline std::shared_ptr<Context> shareForPlayback(){return active&&active->valid()?active->context():nullptr;}
inline Context* currentContext(){return active?active->context().get():playbackContext;}
inline bool read(std::uintptr_t address,void* output,std::size_t bytes){
    auto* context=currentContext();if(!context||!context->valid())return false;
    if(activeSnapshot&&activeSnapshot->mode==GameSnapshot::Mode::Playing){
        const bool ok=context->read(activeSnapshot,address,output,bytes);
        if(context->region(address,bytes)!=ClientImage::Region::Static){
            if(context->valid())++activeSnapshot->hits;else ++activeSnapshot->misses;
        }
        return ok;
    }
    if(active){
        if(activeSnapshot&&activeSnapshot->mode==GameSnapshot::Mode::Recording)return active->record(*activeSnapshot,address,output,bytes);
        return active->read(address,output,bytes);
    }
    // Worker startup may verify immutable signatures before its first snapshot.
    // A runtime read without a queue-owned snapshot is an incomplete import.
    return context->read(nullptr,address,output,bytes);
}
}
