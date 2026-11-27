#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace NorthlightX64World {
inline constexpr uintptr_t CameraField=0x8008, CameraPose=0x10, CameraTransform=0xb8;
inline constexpr uintptr_t ViewSlot=0x1b98, ViewStack=0x1ba0;
inline constexpr unsigned SkyDirection=0x1c0, SkyDirect=0x1cc, SkyAmbient=0x1d0, SkySize=0x1d4;

struct Layout {
    uintptr_t worldFrame, mapName, mapLoaded, gxDevice, sky;
    uintptr_t worldFrameVtable, cameraVtable, d3dVtable;
};

struct ImageRange {
    uintptr_t rva=0, size=0;
    bool contains(uintptr_t at, uintptr_t count) const {
        return count<=size&&at>=rva&&at-rva<=size-count;
    }
};

template<size_t N>
size_t uniqueCode(std::string_view code,const char (&pattern)[N]) {
    // Each '?' masks a relocation operand. All opcodes and field offsets match exactly.
    const auto end=code.size()>=N-1?code.end()-(N-2):code.begin();
    auto at=code.begin();
    size_t found=code.size();
    while((at=std::find(at,end,pattern[0]))!=end){
        if(std::equal(pattern,pattern+N-1,at,[](char expected,char actual){
            return expected=='?'||expected==actual;
        })){
            if(found!=code.size())return code.size();
            found=at-code.begin();
        }
        ++at;
    }
    return found;
}

inline bool resolveLayout(std::string_view code,uintptr_t textRva,
                          ImageRange data,ImageRange readOnly,Layout& out) {
    const auto rip=[&](size_t at,unsigned displacement,unsigned length)->uintptr_t{
        if(at>=code.size()||length>code.size()-at)return 0;
        int32_t delta;
        std::memcpy(&delta,code.data()+at+displacement,sizeof delta);
        const int64_t target=int64_t(textRva)+int64_t(at)+length+delta;
        return target>0?uintptr_t(target):0;
    };

    // Map loading writes the name and sets the loaded flag in the same function.
    const auto map=uniqueCode(code,"\x4c\x8d\x3d????\x41\xb8\xff\xff\xff\x7f\x49\x8b\xcf\x48\x8b\xd3\xe8????");
    if(map>=code.size())return false;
    const auto mapFunction=code.substr(map,256);
    const auto loaded=uniqueCode(mapFunction,"\xc7\x05????\x01\x00\x00\x00\x89\x2d????\xc7\x05????\x01\x00\x00\x00");
    if(loaded>=mapFunction.size())return false;

    // The world-frame constructor publishes the same object the camera accessor reads.
    const auto frame=uniqueCode(code,"\x48\x8d\x05????\x48\x89\x07\x48\x8d\x05????\x48\x89\x47\x38\x48\x8d\x8f\x60\x04\x00\x00");
    if(frame>=code.size())return false;
    const auto frameConstructor=code.substr(frame,512);
    const auto publish=uniqueCode(frameConstructor,"\x48\x89\x3d????\x33\xd2\x48\x8b\xcf\xe8????\xba\x01\x00\x00\x00\x41\xb8\xff\xff\xff\xff\x48\x8b\xcf\xe8????");
    if(publish>=frameConstructor.size())return false;
    const auto pose=uniqueCode(code,"\x48\x8b\x05????\x48\x8b\x80\x08\x80\x00\x00\xf2\x0f\x10\x40\x10\xf2\x0f\x11\x01\x8b\x40\x18\x89\x41\x08\xc3");
    const auto camera=uniqueCode(code,"\x48\x8d\x05????\x48\x89\x03\x33\xed\x48\x89\xab\x58\x01\x00\x00\x89\xab\x60\x01\x00\x00");
    const auto forward=uniqueCode(code,"\x48\x83\xb9\xb8\x00\x00\x00\x00\x48\x8b\xda\x44\x0f\x29\x84\x24\x80\x00\x00\x00\xf3\x44\x0f\x10\x41\x1c\x44\x0f\x29\x4c\x24\x70\xf3\x44\x0f\x10\x49\x20\x44\x0f\x29\x5c\x24\x50\xf3\x44\x0f\x10\x59\x24\x75");
    const auto up=uniqueCode(code,"\x48\x83\xb9\xb8\x00\x00\x00\x00\x48\x8b\xda\x44\x0f\x29\x84\x24\x80\x00\x00\x00\xf3\x44\x0f\x10\x41\x34\x44\x0f\x29\x4c\x24\x70\xf3\x44\x0f\x10\x49\x38\x44\x0f\x29\x5c\x24\x50\xf3\x44\x0f\x10\x59\x3c\x75");
    if(forward>=code.size()||up>=code.size())return false;

    const auto view=uniqueCode(code,"\x48\x8b\x15????\x8b\x82\x98\x1b\x00\x00\x4c\x8d\x82\xa0\x1b\x00\x00\x48\xc1\xe0\x06\x4c\x03\xc0\x49\x3b\xc8\x74\x2b");
    const auto d3d=uniqueCode(code,"\x48\x8d\x05????\x48\x8b\xd9\x48\x89\x01\x48\x81\xc1\x28\x3d\x00\x00\xe8????\x48\x8b\xcb\x48\x83\xc4\x20");

    // The two sky references must agree, and the light update must still use this layout.
    const auto sky=uniqueCode(code,"\x48\x8d\x3d????\x90\x48\x63\xd3\xf3\x0f\x10\x84\x97\x38\x02\x00\x00\xf3\x0f\x11\x44\x24\x20\x0f\x28");
    const auto skyBanks=uniqueCode(code,"\x4c\x8d\x3d????\x41\x8b\xde\x4c\x8d\x25????\x0f\x1f\x84\x00\x00\x00\x00\x00\x83\xfb\x03");
    const auto skyLight=uniqueCode(code,"\x48\x8d\x8f\xec\x01\x00\x00\xe8????\xf3\x0f\x10\x0d????\x48\x8d\x97\xc0\x01\x00\x00\xf3\x0f\x5c\xce\xf3\x0f\x59\x0d????\xf3\x0f\x2d\xc1\x0f\xb6\xc8\x0f\xb6\x87\xd2\x01\x00\x00");
    if(skyLight>=code.size())return false;

    const Layout next{rip(pose,3,7),rip(map,3,7),rip(map+loaded,2,10),rip(view,3,7),rip(sky,3,7),
                      rip(frame,3,7),rip(camera,3,7),rip(d3d,3,7)};
    if(next.worldFrame!=rip(frame+publish,3,7)||next.sky!=rip(skyBanks,3,7)||
       !data.contains(next.worldFrame,8)||!data.contains(next.mapName,64)||!data.contains(next.mapLoaded,4)||
       !data.contains(next.gxDevice,8)||!data.contains(next.sky,SkySize)||
       !readOnly.contains(next.worldFrameVtable,8)||!readOnly.contains(next.cameraVtable,8)||
       !readOnly.contains(next.d3dVtable,8))return false;
    out=next;
    return true;
}
} // namespace NorthlightX64World
