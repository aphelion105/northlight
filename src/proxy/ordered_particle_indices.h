#pragma once
#include <cstdint>
#include <cstring>

namespace NorthlightOrderedParticles {
struct Draw {
    int base;
    unsigned vertices,primitives;

    bool operator==(const Draw& other)const{
        return base==other.base&&vertices==other.vertices&&primitives==other.primitives;
    }
    bool quadList()const{
        return base>=0&&primitives&&!(primitives&1)&&primitives<=8192&&vertices==2*primitives;
    }
    bool follows(const Draw& next)const{
        return quadList()&&next.quadList()&&std::uint64_t(base)+vertices==std::uint64_t(next.base);
    }
};

struct Window {
    Draw first,second;
    const void* uploadBuffer=nullptr;
    unsigned uploadOffset=0,uploadBytes=0;
    unsigned textureStage=~0u;
    unsigned firstConstant=0,constantCount=0;
    const void* nextTexture=nullptr;
    const float* nextConstants=nullptr; // Queue payload, borrowed before the setter is played.
};

// The game can reuse the same prefix with a different base for each emitter.
// A single draw is equivalent only when the expanded prefix has exactly the
// same index order, including each triangle's winding and provoking vertex.
inline unsigned quadPrefix(const unsigned char* data,unsigned quads){
    if(!quads)return 0;
    std::uint16_t first[6];
    std::memcpy(first,data,sizeof first);
    for(auto i:first)if(i>=4)return 0;
    for(unsigned quad=1;quad<quads;++quad){
        std::uint16_t next[6];
        std::memcpy(next,data+quad*sizeof first,sizeof next);
        for(unsigned i=0;i<6;++i)if(next[i]!=first[i]+4*quad)return quad;
    }
    return quads;
}

inline bool disjointUpload(const Draw& draw,unsigned offset,unsigned stride,unsigned begin,unsigned size){
    const auto first=offset+std::uint64_t(draw.base)*stride;
    const auto end=first+std::uint64_t(draw.vertices)*stride;
    return size&&(begin>=end||std::uint64_t(begin)+size<=first);
}
}
