#pragma once
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace NorthlightPositionBounds {
namespace detail {
inline bool scanPositionsScalar(const void* data,std::size_t count,std::size_t stride,float* low,float* high){
    for(unsigned k=0;k<3;++k){low[k]=FLT_MAX;high[k]=-FLT_MAX;}
    for(std::size_t i=0;i<count;++i){
        float p[3];std::memcpy(p,static_cast<const unsigned char*>(data)+i*stride,sizeof p);
        for(unsigned k=0;k<3;++k){
            if(!std::isfinite(p[k]))return false;
            low[k]=std::min(low[k],p[k]);high[k]=std::max(high[k],p[k]);
        }
    }
    return true;
}
}
// Caller validates the complete strided byte range; only XYZ is read.
inline bool scanPositions(const void* data,std::size_t count,std::size_t stride,float* low,float* high){
#if defined(__SSE2__)
    __m128 lo=_mm_set1_ps(FLT_MAX),hi=_mm_set1_ps(-FLT_MAX);
    const auto magnitude=_mm_set1_epi32(0x7fffffff),zero=_mm_setzero_si128();
    const auto bias=_mm_set1_epi32(0x7f800000),lastNormal=_mm_set1_epi32(-0x01000001);
    const auto* bytes=static_cast<const unsigned char*>(data);
    for(std::size_t i=0;i<count;++i){
        const auto* source=bytes+i*stride;
        const auto xy=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(source));
        std::int32_t z;std::memcpy(&z,source+8,sizeof(z));
        const auto bits=_mm_unpacklo_epi64(xy,_mm_cvtsi32_si128(z));
        const auto abs=_mm_and_si128(bits,magnitude);
        // Translate the positive normal encoding range to a single signed
        // comparison, then allow either sign of zero. Subnormals keep the
        // scalar path so DAZ/FTZ and the original zero sign stay authoritative.
        const auto exceptional=_mm_andnot_si128(_mm_cmpeq_epi32(abs,zero),
            _mm_cmpgt_epi32(_mm_add_epi32(abs,bias),lastNormal));
        if(_mm_movemask_ps(_mm_castsi128_ps(exceptional)))return detail::scanPositionsScalar(data,count,stride,low,high);
        const auto p=_mm_castsi128_ps(bits);
        // MINPS/MAXPS select the second operand on equality. Keep the prior
        // bound second to preserve std::min/max's signed-zero tie behavior.
        lo=_mm_min_ps(p,lo);hi=_mm_max_ps(p,hi);
    }
    float a[4],b[4];_mm_storeu_ps(a,lo);_mm_storeu_ps(b,hi);
    std::memcpy(low,a,3*sizeof(float));std::memcpy(high,b,3*sizeof(float));
    return true;
#else
    return detail::scanPositionsScalar(data,count,stride,low,high);
#endif
}
}
