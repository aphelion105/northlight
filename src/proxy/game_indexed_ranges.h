#pragma once
#include <algorithm>
#include <array>
#include "capture_buffer_metadata.h"
#include "draw_snapshot.h"
#include "replay_copies.h"
#include "upload_lock.h"

// Reuse ordered index sequences from the replay-position CPU copy. No readback
// or mutable auxiliary buffer is needed on the render thread.
class NorthlightGameIndexedRanges {
public:
    struct Range {
        UINT minimum=0,vertices=0,start=0,primitives=0;
        bool operator==(const Range& b)const{
            return minimum==b.minimum&&vertices==b.vertices&&start==b.start&&primitives==b.primitives;
        }
    };
    static constexpr unsigned MaxDraws=8,MaxPrimitives=4096;
    using Ranges=std::array<Range,MaxDraws>;
private:
    static constexpr unsigned Ways=8,BytesLimit=6u<<20;
    struct Entry {
        NorthlightDrawSnapshot::Ref<IDirect3DIndexBuffer9> buffer;
        Ranges ranges{};
        std::uint64_t identity=0,revision=0,used=0;
        unsigned count=0,bytes=0;
    };
    std::array<Entry,1024> cache{};
    std::uint64_t clock=0;
    unsigned resident=0,uploads=0;
    IDirect3DDevice9* d;

    template<class Index> static bool copy(IDirect3DIndexBuffer9* source,const Ranges& ranges,unsigned count,
                                            UINT sourceBytes,unsigned char* out){
        constexpr unsigned width=sizeof(Index);
        UINT first=sourceBytes/width,end=0;
        for(unsigned i=0;i<count;++i){
            const auto& range=ranges[i];const UINT length=3*range.primitives;
            if(range.start>=sourceBytes/width||length>sourceBytes/width-range.start)return false;
            first=std::min(first,range.start);end=std::max(end,range.start+length);
        }
        NorthlightReplayCopies::Slot* slot=nullptr;
        const auto* bytes=NorthlightReplayCopies::read(source,first*width,(end-first)*width,slot,true);
        if(!bytes)return false;
        bool valid=true;
        for(unsigned i=0;i<count&&valid;++i){
            const auto& range=ranges[i];const UINT length=3*range.primitives;
            const auto* indices=bytes+(range.start-first)*width;
            for(UINT j=0;j<length;++j){
                Index value;std::memcpy(&value,indices+j*width,width);
                if(value<range.minimum||value-range.minimum>=range.vertices){valid=false;break;}
            }
            if(valid)std::memcpy(out,indices,length*width);
            out+=length*width;
        }
        NorthlightReplayCopies::unpin(*slot);
        return valid;
    }

public:
    unsigned hits=0,built=0,unavailable=0;
    explicit NorthlightGameIndexedRanges(IDirect3DDevice9* device):d(device){}
    void clearCounters(){hits=built=unavailable=uploads=0;}
    unsigned bytes()const{return resident;}
    unsigned buffers()const{
        return unsigned(std::count_if(cache.begin(),cache.end(),[](const Entry& entry){return entry.buffer.p!=nullptr;}));
    }
    void reset(){
        for(auto& entry:cache)if(entry.buffer.p){entry.buffer.p->Release();entry.buffer.p=nullptr;entry.used=0;}
        resident=0;clock=0;clearCounters();
    }
    IDirect3DIndexBuffer9* buffer(IDirect3DIndexBuffer9* source,const NorthlightCaptureMetadata::Info& info,
                                  const Ranges& ranges,unsigned count,unsigned primitives){
        const unsigned width=info.format==D3DFMT_INDEX16?2:info.format==D3DFMT_INDEX32?4:0;
        if(!info.identity||!info.revision||!width||(info.usage&D3DUSAGE_DYNAMIC)){++unavailable;return nullptr;}
        std::uint64_t hash=info.identity;
        for(unsigned i=0;i<count;++i){
            hash=(hash^ranges[i].start)*1099511628211ull;
            hash=(hash^ranges[i].primitives)*1099511628211ull;
        }
        auto* bucket=cache.data()+((hash^(hash>>32))%(cache.size()/Ways))*Ways;
        auto* oldest=bucket;
        for(unsigned i=0;i<Ways;++i){
            auto& entry=bucket[i];
            if(!entry.buffer.p||entry.used<oldest->used)oldest=&entry;
            if(entry.buffer.p&&entry.identity==info.identity&&entry.revision==info.revision&&entry.count==count&&
               std::equal(ranges.begin(),ranges.begin()+count,entry.ranges.begin())){
                entry.used=++clock;++hits;return entry.buffer.p;
            }
        }
        const UINT size=primitives*3*width;
        const unsigned replaced=oldest->buffer.p?oldest->bytes:0;
        if(uploads>=12||size>BytesLimit-resident+replaced){++unavailable;return nullptr;}
        std::array<unsigned char,3*MaxPrimitives*4> data;
        if(!(width==2?copy<WORD>(source,ranges,count,info.size,data.data()):
                      copy<DWORD>(source,ranges,count,info.size,data.data()))){++unavailable;return nullptr;}
        ++uploads;
        NorthlightDrawSnapshot::Ref<IDirect3DIndexBuffer9> created;
        if(FAILED(d->CreateIndexBuffer(size,D3DUSAGE_WRITEONLY,info.format,D3DPOOL_DEFAULT,created.out(),nullptr))||!created.p){++unavailable;return nullptr;}
        void* target=nullptr;
        if(FAILED(created->Lock(0,size,&target,NorthlightUpload::FreshBufferLock))){++unavailable;return nullptr;}
        if(target)std::memcpy(target,data.data(),size);
        if(FAILED(created->Unlock())||!target){++unavailable;return nullptr;}
        std::swap(created.p,oldest->buffer.p);
        oldest->ranges=ranges;oldest->identity=info.identity;oldest->revision=info.revision;
        oldest->count=count;oldest->bytes=size;oldest->used=++clock;
        resident=resident-replaced+size;++built;
        return oldest->buffer.p;
    }
};
