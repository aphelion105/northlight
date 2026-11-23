#pragma once
#include <cstdint>

namespace NorthlightGx12340 {
struct Range {
    std::int32_t base=0;
    std::uint32_t minimum=0,vertices=0,start=0,triangles=0;
    bool operator==(const Range& other)const{
        return base==other.base&&minimum==other.minimum&&vertices==other.vertices&&start==other.start&&triangles==other.triangles;
    }
};

template<class Word>
bool indexPoolRange(Word word,unsigned first,unsigned& base,std::uint64_t& start){
    const auto device=word(0xc5df88);
    if(!device)return false;
    const auto ib=word(device+0x28bc);
    if(!ib)return false;
    base=0;
    if(!word(device+0x224)){
        const auto vb=word(device+0x2870);
        if(!vb)return false;
        const auto stride=word(vb+12);
        if(!stride)return false;
        base=word(vb+24)/stride;
    }
    start=std::uint64_t(word(ib+24)>>1)+first;
    return true;
}

}
