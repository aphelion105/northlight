#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace NorthlightStream {
// Sparse exact ranges avoid mirroring an oversized buffer. Uploads retain
// their own staging bytes until replay completes.
constexpr std::size_t RangeShadowBudgetBytes=std::size_t(8)<<20;
constexpr std::size_t RangeShadowItemBytes=std::size_t(4)<<20;
constexpr std::size_t RangeShadowMinAllocation=4096;
// A join copies the range on each growth; capped so one long run cannot take the budget.
constexpr std::size_t RangeShadowJoinBytes=std::size_t(32)<<10;
class BufferRangeShadow {
    struct Span {
        std::unique_ptr<unsigned char[]> bytes;
        std::size_t capacity=0;
        std::uint32_t begin=0,end=0;
        std::uint64_t used=0;
    };
    // Disjoint, sorted spans. The byte budget also bounds their count, so
    // scattered slots can use the full budget without scanning it per lock.
    std::vector<Span> spans_;
    std::size_t capacity_=0;
    std::uint64_t clock_=0;
    bool discarded_=false;
    // Every byte written since creation or the last DISCARD lies in one of these; bytes outside are undefined.
    // Kept apart from the cached bytes: clear() and invalidate() keep it. The client's slot pool needs 2.
    std::array<std::uint32_t,8> written_{};unsigned writtenN_=0;
    auto firstOverlap(std::uint32_t off) const {
        return std::upper_bound(spans_.begin(),spans_.end(),off,
            [](std::uint32_t value,const Span& span){return value<span.end;});
    }
    void wrote(std::uint32_t a,std::uint64_t end){
        std::uint32_t b=std::uint32_t(std::min<std::uint64_t>(end,UINT32_MAX));unsigned n=0;
        for(unsigned i=0;i<writtenN_;i+=2){   // fold every touching interval into [a,b)
            const auto x=written_[i],y=written_[i+1];
            if(x<=b&&a<=y){a=std::min(a,x);b=std::max(b,y);}else{written_[n]=x;written_[n+1]=y;n+=2;}
        }
        if(n==written_.size()){   // full: widen the nearest interval instead; the set only grows, so the proof stays sound
            unsigned best=0;std::uint32_t bestGap=UINT32_MAX;
            for(unsigned i=0;i<n;i+=2){const auto g=written_[i+1]<a?a-written_[i+1]:b<written_[i]?written_[i]-b:0;if(g<bestGap){bestGap=g;best=i;}}
            written_[best]=std::min(a,written_[best]);written_[best+1]=std::max(b,written_[best+1]);writtenN_=n;return;
        }
        written_[n]=a;written_[n+1]=b;writtenN_=n+2;
    }
    bool neverWritten(std::uint32_t off,std::uint64_t end)const{
        for(unsigned i=0;i<writtenN_;i+=2)if(written_[i]<end&&off<written_[i+1])return false;
        return true;
    }
public:
    std::size_t capacity()const{return capacity_;}
    bool covers(std::uint32_t off,std::uint32_t size)const{
        const std::uint64_t end=std::uint64_t(off)+size;
        auto cursor=off;
        for(auto s=firstOverlap(off);s!=spans_.end()&&s->begin<=cursor;++s){
            if(s->end>=end)return true;
            cursor=s->end;
        }
        return false;
    }
    bool canRead(std::uint32_t off,std::uint32_t size)const{
        if(std::uint64_t(off)+size>UINT32_MAX)return false;
        // DISCARD's unknown bytes remain undefined only while every subsequent
        // write is retained. Dropping any written range revokes this proof.
        // Bytes never written are undefined whatever was dropped; read() zero-fills them.
        return covers(off,size)||(discarded_&&size<=RangeShadowItemBytes)||neverWritten(off,std::uint64_t(off)+size);
    }
    void read(void* dst,std::uint32_t off,std::uint32_t size)const{
        const std::uint64_t end=std::uint64_t(off)+size;
        auto s=firstOverlap(off);
        if(s!=spans_.end()&&s->begin<=off&&s->end>=end){std::memcpy(dst,s->bytes.get()+off-s->begin,size);return;}
        std::memset(dst,0,size);
        for(;s!=spans_.end()&&s->begin<end;++s){
            const auto first=std::max(off,s->begin),last=std::uint32_t(std::min(end,std::uint64_t(s->end)));
            std::memcpy(static_cast<unsigned char*>(dst)+first-off,s->bytes.get()+first-s->begin,last-first);
        }
    }
    void clear(){std::vector<Span>().swap(spans_);capacity_=clock_=0;discarded_=false;}
    void invalidate(){spans_.clear();capacity_=clock_=0;discarded_=false;}
    void discard(){invalidate();discarded_=true;writtenN_=0;}
    // Called for every write to the buffer, cached or not.
    bool remember(std::uint32_t off,std::uint32_t size,const void* src,std::size_t freeBudget){
        if(!size)return false;
        wrote(off,std::uint64_t(off)+size);
        if(size>RangeShadowItemBytes||std::uint64_t(off)+size>UINT32_MAX){clear();return false;}
        const std::uint32_t end=off+size;
        const auto overlapping=std::size_t(firstOverlap(off)-spans_.begin());
        if(overlapping<spans_.size()){
            auto& span=spans_[overlapping];
            if(span.begin<=off&&span.end>=end){
                std::memcpy(span.bytes.get()+off-span.begin,src,size);span.used=++clock_;return true;
            }
        }
        // Small sequential appends can fill the last allocation without
        // copying its existing bytes or making another span.
        if(overlapping&&(overlapping==spans_.size()||end<=spans_[overlapping].begin)){
            auto& previous=spans_[overlapping-1];
            if(previous.end==off&&end-previous.begin<=previous.capacity){
                std::memcpy(previous.bytes.get()+off-previous.begin,src,size);
                previous.end=end;previous.used=++clock_;return true;
            }
        }
        // Merge only overlapping spans. Adjacent spans can already answer one
        // lock together, without copying existing bytes as the upload grows.
        const auto mergeEnd=std::lower_bound(spans_.begin()+overlapping,spans_.end(),end,
            [](const Span& span,std::uint32_t value){return span.begin<value;});
        const bool overlap=mergeEnd!=spans_.begin()+overlapping;
        const auto first=overlap?std::min(off,spans_[overlapping].begin):off;
        const auto last=overlap?std::max(end,(mergeEnd-1)->end):end;
        std::size_t cap=RangeShadowMinAllocation;while(cap<last-first)cap*=2;
        if(overlap&&last-first<=spans_[overlapping].capacity&&mergeEnd==spans_.begin()+overlapping+1){
            auto& span=spans_[overlapping];
            if(first<span.begin)std::memmove(span.bytes.get()+span.begin-first,span.bytes.get(),span.end-span.begin);
            std::memcpy(span.bytes.get()+off-first,src,size);span.begin=first;span.end=last;span.used=++clock_;return true;
        }
        // Large overlaps can retain the new bytes alone. Drop the old proof
        // in that case so subsequent locks read any missing tails from D3D9.
        const bool merge=overlap&&last-first<=RangeShadowJoinBytes&&cap<=freeBudget&&capacity_+cap<=RangeShadowItemBytes;
        Span next;
        next.begin=merge?first:off;next.end=merge?last:end;next.used=++clock_;
        if(!merge){cap=RangeShadowMinAllocation;while(cap<size)cap*=2;}
        if(overlap&&!merge){
            for(auto s=spans_.begin()+overlapping;s!=mergeEnd;++s){capacity_-=s->capacity;freeBudget+=s->capacity;}
            spans_.erase(spans_.begin()+overlapping,mergeEnd);discarded_=false;
        }
        while(capacity_+cap>RangeShadowItemBytes||cap>freeBudget){
            if(spans_.empty()){clear();return false;}
            auto old=std::min_element(spans_.begin(),spans_.end(),[](const Span& a,const Span& b){return a.used<b.used;});
            capacity_-=old->capacity;freeBudget+=old->capacity;spans_.erase(old);discarded_=false;
        }
        try{
            next.bytes.reset(new unsigned char[cap]);next.capacity=cap;
            if(merge){
                for(auto s=spans_.begin()+overlapping;s!=mergeEnd;++s){
                    std::memcpy(next.bytes.get()+s->begin-first,s->bytes.get(),s->end-s->begin);capacity_-=s->capacity;
                }
                spans_.erase(spans_.begin()+overlapping,mergeEnd);
            }
            std::memcpy(next.bytes.get()+off-next.begin,src,size);
            const auto at=firstOverlap(next.begin)-spans_.begin();
            spans_.insert(spans_.begin()+at,std::move(next));capacity_+=cap;return true;
        }catch(...){clear();return false;}
    }
};
}
