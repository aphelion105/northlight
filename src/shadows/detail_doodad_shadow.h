#pragma once
#include <cstddef>
#include <cstdint>

// Stock 3.3.5a DetailDoodad VS2/VS3, six lighting permutations each.
// The complete bytecode identities are distinct from Terrain. Their c4..7
// projection layout alone does not make their alpha-cut geometry terrain.
namespace NorthlightDetailShadow {
inline bool shader(std::uint64_t hash){
    constexpr std::uint64_t hashes[]={
        0x7f2707e33c6c97beULL,0x6bee6c0fc22ac5b2ULL,0x279074f2d7f6ea1eULL,
        0x87aacbe7c33bb84fULL,0x8e0b9bf5d6eff0ffULL,0xb4cb4906153b6683ULL,
        0xd8f18d8080e6bb3aULL,0x4963565c635e7d6dULL,0x28d133339d7839aeULL,
        0x723e54bfc91c3bffULL,0xe2e26d9aa7c0d59cULL,0xc8e41181431a84c7ULL};
    for(auto value:hashes)if(value==hash)return true;
    return false;
}
struct Budget {
    static constexpr unsigned MaxDraws=32,MaxVertices=8192;
    static constexpr std::size_t MaxBytes=512u*1024u;
    unsigned attempts=0,readVertices=0,draws=0,vertices=0;std::size_t bytes=0,readBytes=0;
    bool room(unsigned count)const{return count&&attempts<MaxDraws&&count<=MaxVertices-readVertices&&readBytes<MaxBytes;}
    bool reserve(unsigned count,std::size_t size){if(!room(count)||size>MaxBytes-readBytes)return false;++attempts;readVertices+=count;readBytes+=size;return true;}
    bool take(unsigned count,std::size_t size){
        if(count>MaxVertices-vertices||size>MaxBytes-bytes)return false;
        ++draws;vertices+=count;bytes+=size;return true;
    }
};
}
