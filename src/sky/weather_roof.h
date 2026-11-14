#pragma once
#include <array>
#include <cmath>

namespace NorthlightWeatherShelter {
// World-aligned precipitation ceiling. Immutable once published; no D3D or
// scene ownership. One R32F upload per publication, sampled in the weather pass.
struct Roof {
    static constexpr unsigned Size=128;
    static constexpr float Span=96.f,Step=Span/Size;
    float x=0,y=0,z=0;
    std::array<float,Size*Size> height{};
    bool contains(float px,float py,float margin=0)const{
        return px>=x+margin&&py>=y+margin&&px<x+Span-margin&&py<y+Span-margin;
    }
};
}
