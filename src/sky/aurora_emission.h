#pragma once
#include "aurora_illumination.h"
#include "aurora_source.h"
#include <algorithm>
namespace NorthlightAuroraSource {
inline RGB emissionScale(const NorthlightAurora::Style& style){
    if(!style.enabled||!std::isfinite(style.gain)||style.gain<=0||!std::isfinite(style.phase))return {};
    const float phase=std::clamp(style.phase,0.f,1.f);
    constexpr RGB green{.07f,1.f,.32f},purple{.9f,.09f,1.f};
    RGB scale{};for(unsigned c=0;c<3;++c)scale[c]=style.gain*(green[c]+phase*(purple[c]-green[c]));return scale;
}
inline float decode(float encoded){encoded=std::clamp(encoded,0.f,1.f);return encoded<=.04045f?encoded/12.92f:std::pow((encoded+.055f)/1.055f,2.4f);}
// Only additional emission enters GI; the native baseline stays in its existing
// light budget. Authored additive masks specify emissive intensity; the visible
// HDR addition and GI share that basis. Alpha follows the native PS/blend.
inline RGB additional(const std::array<float,4>& native,const RGB& scale,bool sourceAlpha){
    for(float value:native)if(!std::isfinite(value))return {};
    const float luminance=std::clamp(native[0],0.f,1.f)*.2126f+std::clamp(native[1],0.f,1.f)*.7152f+std::clamp(native[2],0.f,1.f)*.0722f;
    const float factor=luminance*(sourceAlpha?std::clamp(native[3],0.f,1.f):1.f);
    return {scale[0]*factor,scale[1]*factor,scale[2]*factor};
}
}
