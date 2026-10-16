#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

// 0.3.216 (issue #66): content identity of one world-mesh page. The key is a 128-bit word-wise hash of the page's
// vertex bytes then index bytes plus both byte sizes. Two pages with an equal key are treated as byte-identical
// (collision odds ~2^-128 per pair; sizes are compared exactly). Word-wise: ~6-8 GB/s per core, a city plan of
// ~120 MiB hashes in ~20-30 ms on the worker. Portable (no D3D); also used for the content-defined page anchors.
namespace NorthlightMeshPageKey {
struct Key {
    uint64_t lo=0,hi=0;uint32_t vertexBytes=0,indexBytes=0;
    bool operator==(const Key& o)const{return lo==o.lo&&hi==o.hi&&vertexBytes==o.vertexBytes&&indexBytes==o.indexBytes;}
    bool operator!=(const Key& o)const{return !(*this==o);}
};
struct KeyHash {size_t operator()(const Key& k)const{return size_t(k.lo^(k.hi*0x9E3779B97F4A7C15ull));}};
inline uint64_t rotl(uint64_t v,unsigned r){return (v<<r)|(v>>(64-r));}
inline uint64_t avalanche(uint64_t h){h^=h>>33;h*=0xff51afd7ed558ccdull;h^=h>>33;h*=0xc4ceb9fe1a85ec53ull;h^=h>>33;return h;}
// Two independent multiply-rotate lanes over the same 8-byte words (xxHash64-style rounds with different primes).
struct Hasher {
    uint64_t a=0x243F6A8885A308D3ull,b=0x13198A2E03707344ull;
    void word(uint64_t w){
        a=rotl(a^(w*0xC2B2AE3D27D4EB4Full),31)*0x9E3779B185EBCA87ull;
        b=rotl(b+(w^0x165667B19E3779F9ull)*0x85EBCA77C2B2AE63ull,27)*0xD6E8FEB86659FD93ull;
    }
    // Bytes must be a multiple of 8 (vertices are 32 bytes, indices a multiple of 12: the tail is zero-padded).
    void bytes(const void* data,size_t size){
        const unsigned char* p=static_cast<const unsigned char*>(data);size_t i=0;
        for(;i+8<=size;i+=8){uint64_t w;std::memcpy(&w,p+i,8);word(w);}
        if(i<size){uint64_t w=0;std::memcpy(&w,p+i,size-i);word(w);}
    }
    Key finish(uint32_t vertexBytes,uint32_t indexBytes)const{
        Key k;k.vertexBytes=vertexBytes;k.indexBytes=indexBytes;
        const uint64_t sizes=(uint64_t(vertexBytes)<<32)|indexBytes;
        k.lo=avalanche(a^rotl(b,17)^sizes);k.hi=avalanche(b+rotl(a,41)+sizes*0x9E3779B97F4A7C15ull);
        return k;
    }
};
inline Key keyOf(const void* vertices,size_t vertexBytes,const void* indices,size_t indexBytes){
    Hasher h;h.bytes(vertices,vertexBytes);h.bytes(indices,indexBytes);return h.finish(uint32_t(vertexBytes),uint32_t(indexBytes));
}
// Content anchor of one triangle: its three source positions as bit patterns (never the source indices, which
// differ between builds). Order-sensitive (winding).
inline uint64_t triangleHash(const float* p0,const float* p1,const float* p2){
    uint32_t w[9];std::memcpy(w,p0,12);std::memcpy(w+3,p1,12);std::memcpy(w+6,p2,12);
    uint64_t h=0x9E3779B97F4A7C15ull;
    for(unsigned i=0;i<9;++i){h=(h^w[i])*0xff51afd7ed558ccdull;h^=h>>32;}
    return avalanche(h);
}
}
