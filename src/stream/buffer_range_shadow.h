#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace NorthlightStream {
// Sparse exact ranges avoid mirroring an oversized buffer. Uploads retain
// their own staging bytes until replay completes.
constexpr std::size_t RangeShadowBudgetBytes=std::size_t(8)<<20;
constexpr std::size_t RangeShadowItemBytes=std::size_t(4)<<20;
class BufferRangeShadow {
    struct Span {
        std::unique_ptr<unsigned char[]> bytes;
        std::size_t capacity=0;
        std::uint32_t begin=0,end=0;
        std::uint64_t used=0;
    };
    // Only oversized, written buffers allocate this cold metadata.
    std::unique_ptr<std::array<Span,8>> spans_;
    std::size_t capacity_=0;
    std::uint64_t clock_=0;
    bool discarded_=false;
    static bool contains(const Span& s,std::uint32_t off,std::uint64_t end){return s.begin<s.end&&off>=s.begin&&end<=s.end;}
    void release(Span& s){capacity_-=s.capacity;if(s.begin<s.end)discarded_=false;s=Span{};}
public:
    std::size_t capacity()const{return capacity_;}
    bool covers(std::uint32_t off,std::uint32_t size)const{
        if(!spans_)return false;
        const std::uint64_t end=std::uint64_t(off)+size;
        std::uint64_t cursor=off;
        // Adjoining intervals can cover a lock; unknown gaps cannot.
        for(unsigned pass=0;pass<spans_->size();++pass){
            auto next=cursor;
            for(const auto& s:*spans_)if(s.begin<=cursor&&cursor<s.end)next=std::max(next,std::uint64_t(s.end));
            if(next==cursor)return false;
            if(next>=end)return true;
            cursor=next;
        }
        return false;
    }
    bool canRead(std::uint32_t off,std::uint32_t size)const{
        if(std::uint64_t(off)+size>UINT32_MAX)return false;
        // DISCARD's unknown bytes remain undefined only while every subsequent
        // write is retained. Dropping any written range revokes this proof.
        return covers(off,size)||(discarded_&&size<=RangeShadowItemBytes);
    }
    void read(void* dst,std::uint32_t off,std::uint32_t size)const{
        const std::uint64_t end=std::uint64_t(off)+size;
        if(spans_)for(const auto& s:*spans_)if(contains(s,off,end)){std::memcpy(dst,s.bytes.get()+off-s.begin,size);return;}
        std::memset(dst,0,size);
        if(spans_)for(const auto& s:*spans_){
            const auto first=std::max(off,s.begin),last=std::uint32_t(std::min(end,std::uint64_t(s.end)));
            if(first<last)std::memcpy(static_cast<unsigned char*>(dst)+first-off,s.bytes.get()+first-s.begin,last-first);
        }
    }
    void clear(){spans_.reset();capacity_=clock_=0;discarded_=false;}
    void invalidate(){if(spans_)for(auto& s:*spans_)s.begin=s.end=0;discarded_=false;}
    void discard(){invalidate();discarded_=true;}
    void remember(std::uint32_t off,std::uint32_t size,const void* src,std::size_t freeBudget){
        if(!size)return;
        if(size>RangeShadowItemBytes||std::uint64_t(off)+size>UINT32_MAX){clear();return;}
        if(!spans_)try{spans_=std::make_unique<std::array<Span,8>>();}catch(...){clear();return;}
        const std::uint32_t end=off+size;
        Span* selected=nullptr;bool join=false;
        for(auto& s:*spans_)if(s.begin<s.end&&off<=s.end&&s.begin<=end&&
            std::uint64_t(std::max(end,s.end))-std::min(off,s.begin)<=RangeShadowItemBytes){
            if(!selected||s.used>selected->used){selected=&s;join=true;}
        }
        if(!selected)for(auto& s:*spans_)if(s.begin==s.end&&(!selected||s.capacity>selected->capacity))selected=&s;
        if(!selected)selected=&*std::min_element(spans_->begin(),spans_->end(),[](const Span& a,const Span& b){return a.used<b.used;});
        auto& s=*selected;
        if(!join&&s.begin<s.end)discarded_=false;
        const auto first=join?std::min(off,s.begin):off,last=join?std::max(end,s.end):end;
        const std::size_t need=last-first;
        if(need>s.capacity){
            std::size_t cap=4096;while(cap<need)cap*=2;
            // Include old and new allocations in the global peak budget.
            while(capacity_-s.capacity+cap>RangeShadowItemBytes||cap>freeBudget){
                Span* old=nullptr;
                for(auto& other:*spans_)if(&other!=selected&&other.capacity&&(!old||other.used<old->used))old=&other;
                if(!old){clear();return;}
                freeBudget+=old->capacity;release(*old);
            }
            std::unique_ptr<unsigned char[]> next;
            try{next.reset(new unsigned char[cap]);}catch(...){clear();return;}
            if(join)std::memcpy(next.get()+s.begin-first,s.bytes.get(),s.end-s.begin);
            capacity_=capacity_-s.capacity+cap;s.bytes=std::move(next);s.capacity=cap;
        }else if(join&&first<s.begin)std::memmove(s.bytes.get()+s.begin-first,s.bytes.get(),s.end-s.begin);
        std::memcpy(s.bytes.get()+off-first,src,size);s.begin=first;s.end=last;s.used=++clock_;
        // Update every overlapping copy so subset reads cannot see stale data.
        for(auto& other:*spans_)if(&other!=selected){
            const auto a=std::max(off,other.begin),b=std::min(end,other.end);
            if(a<b)std::memcpy(other.bytes.get()+a-other.begin,static_cast<const unsigned char*>(src)+a-off,b-a);
        }
    }
};
}
