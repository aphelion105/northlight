#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>
#include <algorithm>

// Replay-position upload bytes only. This never starts a buffer readback.
namespace NorthlightParticleUploads {
inline std::atomic<bool> enabled{false};
inline constexpr unsigned Entries=64,MaxChunk=256u<<10,ByteBudget=2u<<20;
struct Stats {std::uint64_t writes=0,bytes=0,hits=0,misses=0,oversized=0,evictions=0;};
class Cache {
    struct Entry {const void* key=nullptr;std::uint64_t life=0,used=0;unsigned offset=0;std::vector<unsigned char> bytes;};
    std::array<Entry,Entries> entries_{};
    std::array<const void*,Entries> wanted_{};
    unsigned wantedCount_=0,used_=0,copied_=0;std::uint64_t clock_=0;
    std::mutex mutex_;Stats stats_;
    void drop(Entry& e){used_-=unsigned(e.bytes.size());std::vector<unsigned char>().swap(e.bytes);e.key=nullptr;}
    void erase(const void* key){if(used_)for(auto& e:entries_)if(e.key==key)drop(e);}
public:
    void clear(){std::lock_guard<std::mutex> lock(mutex_);for(auto& e:entries_)if(e.key)drop(e);wantedCount_=copied_=0;}
    void nextFrame(){std::lock_guard<std::mutex> lock(mutex_);copied_=0;}
    void forget(const void* key){
        std::lock_guard<std::mutex> lock(mutex_);erase(key);
        for(unsigned i=0;i<wantedCount_;)if(wanted_[i]==key)wanted_[i]=wanted_[--wantedCount_];else ++i;
    }
    void invalidate(const void* key,unsigned offset,unsigned size,bool discard=false){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!used_)return;
        const std::uint64_t end=std::uint64_t(offset)+size;
        for(auto& e:entries_)if(e.key==key&&(discard||!size||(offset<std::uint64_t(e.offset)+e.bytes.size()&&e.offset<end)))drop(e);
    }
    void write(const void* key,std::uint64_t life,unsigned offset,const void* data,unsigned size,bool dynamic){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!key||!life||!data||!size||std::uint64_t(offset)+size>UINT32_MAX)return;
        if(used_){
            const std::uint64_t end=std::uint64_t(offset)+size;
            for(auto& e:entries_)if(e.key==key&&(e.life!=life||(offset<std::uint64_t(e.offset)+e.bytes.size()&&e.offset<end)))drop(e);
        }
        // Dynamic buffers are copied only after a proven particle draw asks
        // for them. Small immutable uploads can be retained before first use.
        if(dynamic&&std::find(wanted_.begin(),wanted_.begin()+wantedCount_,key)==wanted_.begin()+wantedCount_)return;
        if(size>MaxChunk||copied_>ByteBudget-size){++stats_.oversized;return;}
        Entry* slot=nullptr;
        while(!slot||used_+size>ByteBudget){
            slot=nullptr;for(auto& e:entries_)if(!e.key){slot=&e;break;}
            if(slot&&used_+size<=ByteBudget)break;
            auto victim=std::min_element(entries_.begin(),entries_.end(),[](const Entry& a,const Entry& b){return a.key&&(!b.key||a.used<b.used);});
            if(!victim->key)return;drop(*victim);++stats_.evictions;
        }
        try{slot->bytes.assign(static_cast<const unsigned char*>(data),static_cast<const unsigned char*>(data)+size);}
        catch(...){slot->key=nullptr;std::vector<unsigned char>().swap(slot->bytes);return;}
        slot->key=key;slot->life=life;slot->offset=offset;slot->used=++clock_;
        used_+=size;copied_+=size;++stats_.writes;stats_.bytes+=size;
    }
    bool read(const void* key,std::uint64_t offset,unsigned size,void* out,bool* hasBuffer=nullptr){
        std::lock_guard<std::mutex> lock(mutex_);
        if(hasBuffer)*hasBuffer=false;
        if(!key||!out||!size||offset>UINT32_MAX-size){++stats_.misses;return false;}
        if(std::find(wanted_.begin(),wanted_.begin()+wantedCount_,key)==wanted_.begin()+wantedCount_){
            if(wantedCount_==Entries)wantedCount_=0;
            wanted_[wantedCount_++]=key;
        }
        if(!used_){++stats_.misses;return false;}
        unsigned copied=0;
        while(copied<size){
            Entry* found=nullptr;
            for(auto& e:entries_)if(e.key==key){
                if(hasBuffer)*hasBuffer=true;
                if(e.offset<=offset+copied&&offset+copied<std::uint64_t(e.offset)+e.bytes.size()){found=&e;break;}
            }
            if(!found){++stats_.misses;return false;}
            const auto at=unsigned(offset+copied-found->offset),n=std::min(size-copied,unsigned(found->bytes.size())-at);
            std::memcpy(static_cast<unsigned char*>(out)+copied,found->bytes.data()+at,n);copied+=n;found->used=++clock_;
        }
        ++stats_.hits;return true;
    }
    Stats stats(){std::lock_guard<std::mutex> lock(mutex_);return stats_;}
    unsigned bytes(){std::lock_guard<std::mutex> lock(mutex_);return used_;}
};
inline Cache& cache(){static Cache value;return value;}
// The atlas needs the most recent emitter uploads even after the particle
// light cache has exhausted its frame budget. Keep only its active stream.
class AtlasUploads {
    struct Range {unsigned offset=0;std::vector<unsigned char> bytes;};
    std::array<Range,3> ranges_;
    std::atomic<const void*> watched_{nullptr};
    std::mutex mutex_;
    unsigned next_=0;
public:
    bool watching(const void* key)const{return key&&watched_.load(std::memory_order_relaxed)==key;}
    void watch(const void* key){
        if(watching(key))return;
        std::lock_guard<std::mutex> guard(mutex_);
        if(watching(key))return;
        for(auto& range:ranges_)range.bytes.clear();
        watched_.store(key,std::memory_order_relaxed);
    }
    void forget(const void* key){
        if(!watching(key))return;
        std::lock_guard<std::mutex> guard(mutex_);
        if(!watching(key))return;
        for(auto& range:ranges_)range.bytes.clear();
        watched_.store(nullptr,std::memory_order_relaxed);
    }
    void invalidate(const void* key,unsigned offset,unsigned size,bool discard){
        if(!watching(key))return;
        std::lock_guard<std::mutex> guard(mutex_);
        if(!watching(key))return;
        const auto end=std::uint64_t(offset)+size;
        for(auto& r:ranges_)if(discard||!size||(r.offset<end&&offset<std::uint64_t(r.offset)+r.bytes.size()))r.bytes.clear();
    }
    void write(const void* key,unsigned offset,unsigned size,const unsigned char* bytes){
        if(!watching(key)||!bytes||!size||size>64u*1024)return;
        std::lock_guard<std::mutex> guard(mutex_);
        if(!watching(key))return;
        auto& range=ranges_[next_++%ranges_.size()];
        try{range.bytes.assign(bytes,bytes+size);range.offset=offset;}
        catch(...){range.bytes.clear();}
    }
    template<class Read>
    bool readRange(const void* key,std::uint64_t offset,unsigned size,Read consume){
        if(!watching(key))return false;
        std::lock_guard<std::mutex> guard(mutex_);
        if(!watching(key))return false;
        for(const auto& r:ranges_)if(offset>=r.offset&&offset-r.offset+size<=r.bytes.size()){
            consume(r.bytes.data()+std::size_t(offset-r.offset));
            return true;
        }
        return false;
    }
    bool read(const void* key,std::uint64_t offset,unsigned size,void* out){
        return readRange(key,offset,size,[&](const unsigned char* data){std::memcpy(out,data,size);});
    }
};
inline AtlasUploads& atlasUploads(){static AtlasUploads value;return value;}
struct Pending {unsigned offset=0,size=0;bool active=false;};
}
