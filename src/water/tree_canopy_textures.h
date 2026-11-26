#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>

namespace NorthlightTreeCanopy {
struct CanopyMip {std::uint64_t fingerprint,hash;unsigned format,width,height;};
#include "tree_canopy_catalog.h"

inline std::uint64_t hashBytes(const unsigned char* bytes,std::size_t count,std::uint64_t hash=14695981039346656037ull){
    for(std::size_t i=0;i<count;++i)hash=(hash^bytes[i])*1099511628211ull;
    return hash;
}

// The stream already owns each complete CPU upload. Match two short samples
// first and verify the full mip only on a hit. Never read a texture back.
inline bool matches(unsigned format,unsigned w,unsigned h,const unsigned char* bytes,unsigned uploadRows,std::size_t pitch){
    if(!bytes||w<16||h<16||w>1024||h>1024)return false;
    const bool bc1=format==0x31545844u,bc=bc1||format==0x33545844u||format==0x35545844u;
    if(!bc&&format!=21)return false;
    const unsigned rows=bc?(h+3)/4:h,rowBytes=bc?((w+3)/4)*(bc1?8:16):w*4;
    if(uploadRows!=rows||pitch<rowBytes)return false;
    const auto fingerprint=hashBytes(bytes+(2*rows/3)*pitch+rowBytes/3,8,
        hashBytes(bytes+(rows/3)*pitch+rowBytes/3,8));
    auto mip=std::lower_bound(std::begin(canopyMips),std::end(canopyMips),fingerprint,
        [](const CanopyMip& item,std::uint64_t key){return item.fingerprint<key;});
    std::uint64_t hash=0;
    for(;mip!=std::end(canopyMips)&&mip->fingerprint==fingerprint;++mip){
        if(mip->format!=format||mip->width!=w||mip->height!=h)continue;
        if(!hash){hash=14695981039346656037ull;for(unsigned y=0;y<rows;++y)hash=hashBytes(bytes+y*pitch,rowBytes,hash);}
        if(hash==mip->hash)return true;
    }
    return false;
}

// The upload and draw both run on the replay thread. Only confirmed resources
// are stored, and overwrites and destruction remove their proof.
inline thread_local std::unordered_map<const void*,unsigned> identified;
inline void forget(const void* texture){identified.erase(texture);}
inline bool contains(const void* texture){return identified.find(texture)!=identified.end();}
inline void upload(const void* texture,unsigned level,unsigned format,unsigned w,unsigned h,
                   const unsigned char* bytes,unsigned uploadRows,std::size_t pitch,bool whole){
    if(!texture)return;
    if(!whole){forget(texture);return;}
    if(matches(format,w,h,bytes,uploadRows,pitch)){
        try{const auto entry=identified.try_emplace(texture,level).first;entry->second=std::min(entry->second,level);}catch(...){}
    }else if(auto entry=identified.find(texture);entry!=identified.end()&&level<=entry->second)identified.erase(entry);
}
} // namespace NorthlightTreeCanopy
