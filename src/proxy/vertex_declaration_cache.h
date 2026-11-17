#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include "intrusive_lru.h"

// Declarations are immutable COM objects. Hold a reference while caching the
// decoded elements so pointer reuse can never associate a new layout with an
// old one. Render-thread only; bounded and explicitly reset with the device.
namespace NorthlightVertexDeclarations {
class Cache {
    struct Entry {
        std::array<D3DVERTEXELEMENT9,MAXD3DDECLLENGTH+1> elements{};
        UINT count=0;
        std::array<UINT,4> captureExtent{};bool captureChecked=false,captureValid=false;
    };
    // Lookup and LRU never need the decoded layouts. Keep their keys together
    // instead of walking a cache line from every large declaration record.
    struct Key {IDirect3DVertexDeclaration9* object=nullptr;NorthlightIntrusiveLRU::Links<Key> lru;};
    std::array<Entry,128> entries_{};
    std::array<Key,128> keys_{};
    unsigned used_=0;
    NorthlightIntrusiveLRU::List<Key> lru_;unsigned recent_=128;
    Entry* acquire(IDirect3DVertexDeclaration9* object){
        if(!object)return nullptr;
        if(recent_<used_&&keys_[recent_].object==object){lru_.touch(keys_[recent_]);return &entries_[recent_];}
        for(unsigned i=0;i<used_;++i){auto& key=keys_[i];
            if(key.object==object){lru_.touch(key);recent_=i;return &entries_[i];}
        }
        std::array<D3DVERTEXELEMENT9,MAXD3DDECLLENGTH+1> decoded{};UINT n=UINT(decoded.size());
        if(FAILED(object->GetDeclaration(decoded.data(),&n))||!n||n>decoded.size()||decoded[n-1].Stream!=0xff)return nullptr;
        unsigned slot=0;
        if(used_<entries_.size())slot=used_++;
        else {slot=unsigned(lru_.oldest()-keys_.data());lru_.remove(keys_[slot]);}
        auto& key=keys_[slot];auto& entry=entries_[slot];object->AddRef();if(key.object)key.object->Release();
        key.object=object;lru_.append(key);std::copy_n(decoded.begin(),n,entry.elements.begin());entry.count=n;
        entry.captureExtent={};entry.captureChecked=entry.captureValid=false;recent_=slot;return &entry;
    }
public:
    Cache()=default;
    Cache(const Cache&)=delete;Cache& operator=(const Cache&)=delete;
    ~Cache(){clear();}
    void clear(){recent_=128;lru_.clear();for(unsigned i=0;i<used_;++i){auto& key=keys_[i];if(key.object)key.object->Release();key=Key{};entries_[i]=Entry{};}used_=0;}
    bool get(IDirect3DVertexDeclaration9* object,const D3DVERTEXELEMENT9*& elements,UINT& count){
        elements=nullptr;count=0;auto* entry=acquire(object);if(!entry)return false;
        elements=entry->elements.data();count=entry->count;return true;
    }
    bool captureLayout(IDirect3DVertexDeclaration9* object,UINT extent[4]){
        if(!extent)return false;for(unsigned s=0;s<4;++s)extent[s]=0;
        auto* entry=acquire(object);if(!entry)return false;
        if(!entry->captureChecked){
            // Same stream/type/method/end validation as Frame::layout. Keep
            // its result separate: get() also serves non-capture consumers.
            constexpr unsigned sizes[]={4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};
            bool any=false,end=false,valid=true;
            for(UINT i=0;i<entry->count;++i){const auto& e=entry->elements[i];if(e.Stream==0xff){end=true;break;}
                const unsigned size=e.Type<sizeof(sizes)/sizeof(*sizes)?sizes[e.Type]:0;
                if(e.Stream>=4||!size||e.Method!=D3DDECLMETHOD_DEFAULT){valid=false;break;}
                const UINT finish=UINT(e.Offset)+size;auto& value=entry->captureExtent[e.Stream];if(finish>value)value=finish;any=true;
            }
            entry->captureValid=valid&&any&&end;entry->captureChecked=true;
        }
        for(unsigned s=0;s<4;++s)extent[s]=entry->captureExtent[s];return entry->captureValid;
    }
};
}
