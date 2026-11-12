#pragma once
#include <array>
#include <cmath>
#include <cstdint>

// World-oriented, equal-solid-angle transport basis. This is the diffuse
// projection of an emitter, not a replacement for its detailed visible image.
namespace NorthlightAuroraSource {
inline constexpr unsigned AzimuthCount=8,HeightCount=4,SectorCount=AzimuthCount*HeightCount;
inline constexpr float Pi=3.14159265358979323846f;
using RGB=std::array<float,3>;
struct Snapshot {
    std::array<RGB,SectorCount> radiance{};
    std::uint64_t revision=0,selection=0;
    bool complete=false;
};
// Accept arbitrary finite directions; normalize before assigning height.
// Azimuth follows atan2(worldX,worldY), matching the native-source atlas.
inline int sector(float x,float y,float z){
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||z<=0)return -1;
    const double length=std::sqrt(double(x)*x+double(y)*y+double(z)*z);
    if(!(length>0))return -1;
    unsigned height=unsigned(double(z)/length*HeightCount);
    if(height>=HeightCount)height=HeightCount-1;
    const double angle=std::atan2(double(x),double(y));
    unsigned azimuth=unsigned((angle/(2*double(Pi))+.5)*AzimuthCount);
    if(azimuth>=AzimuthCount)azimuth=0;
    return int(height*AzimuthCount+azimuth);
}
inline bool valid(const Snapshot& source){
    if(!source.complete||!source.revision||!source.selection)return false;
    for(const auto& color:source.radiance)for(float value:color)
        if(!std::isfinite(value)||value<0)return false;
    return true;
}
inline RGB evaluate(const Snapshot& source,float x,float y,float z){
    const int index=sector(x,y,z);
    return index<0||!valid(source)?RGB{}:source.radiance[unsigned(index)];
}
}
