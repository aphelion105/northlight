#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace NorthlightParticleAtlas {
// Each source gets an integral block-aligned origin at every mip, even
// when the source is only one texel wide. U is scaled by the atlas width.
inline constexpr unsigned TileWidthScale=16;
inline constexpr unsigned WidthScale=2*TileWidthScale;
inline constexpr unsigned origin(unsigned tile,unsigned width){return (TileWidthScale/2+TileWidthScale*tile)*width;}

inline void copySelectors(const unsigned char* source,unsigned char* target,unsigned bits,
                          const unsigned (&columns)[4]){
    std::uint64_t in=0,out=0;
    const unsigned bytes=2*bits;
    std::memcpy(&in,source,bytes);
    const unsigned mask=(1u<<bits)-1;
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
        out|=((in>>((4*y+columns[x])*bits))&mask)<<((4*y+x)*bits);
    std::memcpy(target,&out,bytes);
}

// DXT blocks are copied verbatim for the interior. The gutters keep the
// original color/alpha endpoints and only replicate the selected texels.
inline void tileRows(const unsigned char* source,unsigned pitch,unsigned char* target,unsigned targetPitch,
                     unsigned width,unsigned height,unsigned tile,bool wrap,unsigned blockBytes,unsigned alphaBits){
    const auto address=[&](int x){
        return wrap?unsigned(x)&(width-1):unsigned(std::clamp(x,0,int(width)-1));
    };
    const unsigned rows=blockBytes?(height+3)/4:height;
    const int step=blockBytes?4:1;
    const int from=-int(8*width),to=int(8*width);
    for(unsigned y=0;y<rows;++y){
        const auto* row=source+y*pitch;
        auto* dst=target+y*targetPitch;
        if(!blockBytes){
            auto* middle=dst+4*origin(tile,width);
            std::memcpy(middle,row,4*width);
            for(int x=from;x<to;++x)
                if(x<0||x>=int(width))std::memcpy(middle+4*x,row+4*address(x),4);
            continue;
        }
        auto* middle=dst+(origin(tile,width)/4)*blockBytes;
        for(int x=from;x<to;x+=step){
            const unsigned first=address(x);
            const auto* in=row+(first/4)*blockBytes;
            auto* out=middle+(x/4)*int(blockBytes);
            std::memcpy(out,in,blockBytes);
            const unsigned columns[]={first%4,address(x+1)%4,address(x+2)%4,address(x+3)%4};
            if(columns[0]==0&&columns[1]==1&&columns[2]==2&&columns[3]==3)continue;
            copySelectors(in+blockBytes-4,out+blockBytes-4,2,columns);
            if(alphaBits==4)copySelectors(in,out,4,columns);
            if(alphaBits==3)copySelectors(in+2,out+2,3,columns);
        }
    }
}
}
