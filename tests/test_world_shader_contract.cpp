#include "shader_tags.h"
#include <cassert>
#include <cstdio>
#include <vector>
using Words=std::vector<std::uint32_t>;
static std::uint32_t dst(unsigned t,unsigned r,unsigned mask=15){return 0x80000000u|(t<<28)|(mask<<16)|r;}
static std::uint32_t src(unsigned t,unsigned r,unsigned swizzle=0xe4){return 0x80000000u|(t<<28)|(swizzle<<16)|r;}
static Words program(){
    Words w{0xfffe0300u,0x05000051u,src(2,0),0x3f800000u,0,0,0,
        0x0200001fu,0x80000000u,dst(1,0),0x0200001fu,0x80000000u,dst(6,0),
        0x02000001u,dst(0,7,8),src(2,0,0)};
    for(unsigned i=0;i<3;++i)w.insert(w.end(),{0x03000009u,dst(0,7,1u<<i),src(2,31+i),src(1,0)});
    for(unsigned i=0;i<4;++i)w.insert(w.end(),{0x03000009u,dst(6,0,1u<<i),src(2,2+i),src(0,7)});
    w.push_back(65535);return w;
}
static bool proof(const Words& w){return NorthlightWorldShaderContract::rigidWmo(w.data(),w.size());}
int main(){
    const Words original=program();assert(proof(original));
    const auto hash=NorthlightShaderTags::fnv1a(original.data(),original.size()*4);
    assert(!NorthlightShaderTags::triggerTags(hash));
    assert(NorthlightShaderTags::triggerTagsOfBytecode(original.data(),original.size()*4)==(NorthlightShaderTags::kWorld|NorthlightShaderTags::kWmo));
    // A different material and register allocation retain the same coordinates.
    auto w=original;w.insert(w.end()-1,{0x03000005u,dst(6,1),src(2,236),src(1,2)});assert(proof(w));
    w=original;for(auto& t:w)if((t&0x80000000u)&&NorthlightWorldShaderContract::type(t)==0&&(t&2047)==7)t=(t&~2047u)|19;assert(proof(w));
    w=original;for(std::size_t at=16;at+3<w.size();at+=4)std::swap(w[at+2],w[at+3]);assert(proof(w));
    // Full-program final outputs, not an accepted prefix followed by a rewrite.
    w=original;w.insert(w.end()-1,{0x02000001u,dst(6,0,1),src(2,17,0)});assert(!proof(w));
    w=original;w.insert(w.end()-1,{0x00000028u});assert(!proof(w));
    w=original;w.insert(w.begin()+13,{0x05000051u,src(2,31),0,0,0,0});assert(!proof(w));
    for(std::size_t i=0;i<original.size();++i){w.assign(original.begin(),original.begin()+i);assert(!proof(w));}
    w=original;w.push_back(0);assert(!proof(w));
    for(unsigned r:{2u,3u,4u,5u,31u,32u,33u}){w=original;for(auto& t:w)if((t&0x80000000u)&&NorthlightWorldShaderContract::type(t)==2&&(t&2047)==r)t=(t&~2047u)|80;assert(!proof(w));}
    w=original;w[15]=src(2,0,0x55);assert(!proof(w)); // homogeneous W must be one
    w=original;w[19]=src(1,0,0x00);assert(!proof(w)); // altered position input
    w=original;w[17]|=0x100000u;assert(!proof(w)); // saturated model-view X
    w=original;w[16]|=0x10000000u;assert(!proof(w)); // predication
    w=original;w[18]|=0x2000u;assert(!proof(w)); // relative matrix
    std::puts("world shader contract: material edits/renaming accepted, full coordinate mutations/truncation/flow rejected PASS");
}
