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
    void erase(const void* key){for(auto& e:entries_)if(e.key==key)drop(e);}
public:
    void clear(){std::lock_guard<std::mutex> lock(mutex_);for(auto& e:entries_)if(e.key)drop(e);wantedCount_=copied_=0;}
    void nextFrame(){std::lock_guard<std::mutex> lock(mutex_);copied_=0;}
    void forget(const void* key){
        std::lock_guard<std::mutex> lock(mutex_);erase(key);
        for(unsigned i=0;i<wantedCount_;)if(wanted_[i]==key)wanted_[i]=wanted_[--wantedCount_];else ++i;
    }
    void invalidate(const void* key,unsigned offset,unsigned size,bool discard=false){
        std::lock_guard<std::mutex> lock(mutex_);
        const std::uint64_t end=std::uint64_t(offset)+size;
        for(auto& e:entries_)if(e.key==key&&(discard||!size||(offset<std::uint64_t(e.offset)+e.bytes.size()&&e.offset<end)))drop(e);
    }
    void write(const void* key,std::uint64_t life,unsigned offset,const void* data,unsigned size,bool dynamic){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!key||!life||!data||!size||std::uint64_t(offset)+size>UINT32_MAX)return;
        for(auto& e:entries_)if(e.key==key&&e.life!=life)drop(e);
        for(auto& e:entries_)if(e.key==key&&offset<std::uint64_t(e.offset)+e.bytes.size()&&e.offset<std::uint64_t(offset)+size)drop(e);
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
    bool read(const void* key,std::uint64_t offset,unsigned size,void* out){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!key||!out||!size||offset+size>UINT32_MAX){++stats_.misses;return false;}
        if(std::find(wanted_.begin(),wanted_.begin()+wantedCount_,key)==wanted_.begin()+wantedCount_){
            if(wantedCount_==Entries)wantedCount_=0;
            wanted_[wantedCount_++]=key;
        }
        unsigned copied=0;
        while(copied<size){
            Entry* found=nullptr;
            for(auto& e:entries_)if(e.key==key&&e.offset<=offset+copied&&offset+copied<std::uint64_t(e.offset)+e.bytes.size()){found=&e;break;}
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
struct Pending {unsigned offset=0,size=0;bool active=false;};
}
