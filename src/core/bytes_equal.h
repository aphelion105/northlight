#pragma once
#include <cstddef>
#include <cstring>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

// Equality only: complete ranges, unaligned input, no read beyond either end.
// The renderer CRT's dynamic-size memcmp scans large equal buffers byte by byte.
namespace NorthlightBytes {
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline bool equal(const void* left,const void* right,std::size_t bytes){
    if(!bytes||left==right)return true;
    const auto* a=static_cast<const unsigned char*>(left);
    const auto* b=static_cast<const unsigned char*>(right);
#if defined(__SSE2__)
    while(bytes>=16){
        const auto x=_mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
        const auto y=_mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
        if(_mm_movemask_epi8(_mm_cmpeq_epi8(x,y))!=0xffff)return false;
        a+=16;b+=16;bytes-=16;
    }
#endif
    return !std::memcmp(a,b,bytes);
}
}
