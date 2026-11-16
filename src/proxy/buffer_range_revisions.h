#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// Conservative page generations for immutable capture ranges. Owners serialize
// publication with the tracked-buffer registry; no driver call holds that lock.
namespace NorthlightBufferRanges {
inline std::atomic<size_t> residentBytes{0};
inline constexpr size_t Budget=2u<<20,PageBytes=4096;
class Revisions {
    std::vector<uint64_t> pages_;
    uint64_t base_,observed_;
    uint32_t size_;
    size_t charged_=0;
    Revisions(uint32_t size,uint64_t revision):pages_((uint64_t(size)+PageBytes-1)/PageBytes),base_(revision),observed_(revision),size_(size){}
public:
    ~Revisions(){residentBytes.fetch_sub(charged_,std::memory_order_relaxed);}
    static std::unique_ptr<Revisions> create(uint32_t size,uint64_t revision)noexcept{
        const uint64_t charge=sizeof(Revisions)+((uint64_t(size)+PageBytes-1)/PageBytes)*sizeof(uint64_t);
        if(!size||!revision||charge>Budget)return {};
        auto used=residentBytes.load(std::memory_order_relaxed);
        do{if(used>Budget-charge)return {};}while(!residentBytes.compare_exchange_weak(used,used+size_t(charge),std::memory_order_relaxed));
        try{auto out=std::unique_ptr<Revisions>(new Revisions(size,revision));out->charged_=size_t(charge);return out;}
        catch(...){residentBytes.fetch_sub(size_t(charge),std::memory_order_relaxed);return {};}
    }
    void invalidate(uint64_t revision){
        // Clock rewind/wrap cannot expose stamps from the previous epoch.
        if(revision<=observed_)std::fill(pages_.begin(),pages_.end(),0);
        base_=observed_=revision;
    }
    void publish(uint64_t previous,uint64_t current,uint64_t begin,uint64_t bytes,bool whole){
        if(!current||!previous||observed_!=previous||current<=previous||whole||!bytes||begin>=size_||bytes>size_-begin){invalidate(current);return;}
        const size_t last=size_t((begin+bytes-1)/PageBytes);
        for(size_t page=size_t(begin/PageBytes);page<=last;++page)pages_[page]=current;
        observed_=current;
    }
    uint64_t revision(uint64_t current,uint64_t begin,uint64_t bytes){
        if(!current||!bytes||begin>=size_||bytes>size_-begin)return 0;
        if(observed_!=current)invalidate(current);
        if(base_==current)return current;
        auto result=base_;
        const size_t last=size_t((begin+bytes-1)/PageBytes);
        for(size_t page=size_t(begin/PageBytes);page<=last;++page)result=std::max(result,pages_[page]);
        return result;
    }
};
}
