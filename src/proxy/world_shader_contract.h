#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Registration-time proof of the rigid world coordinate contract. Shader
// identities remain useful for old programs; an edited material must not lose
// its world camera, geometry capture and native-light handoff just because its
// lighting instructions changed. This proves only coordinates, not lighting,
// fog, alpha coverage or a material family. Those retain their own proofs.
namespace NorthlightWorldShaderContract {
inline unsigned type(std::uint32_t t){return ((t>>28)&7)|((t>>8)&24);}
inline unsigned reg(std::uint32_t t){return t&2047;}
inline int sources(unsigned op){switch(op){
    case 1:case 6:case 7:case 14:case 15:case 16:case 19:case 35:case 36:case 46:return 1;
    case 2:case 3:case 5:case 8:case 9:case 10:case 11:case 12:case 13:case 17:case 32:case 33:return 2;
    case 4:case 18:return 3;default:return -1;}}
inline bool rigidWmo(const std::uint32_t* code,std::size_t count){
    if(!code||count<4||count>4096||code[0]!=0xfffe0300u)return false;
    // Distinct provenance for v0, the three model-view dot products, one,
    // projection outputs, and unmodified external constants.
    enum:unsigned {Unknown=0,Input=1,View=5,One=8,Clip=9,Constant=32};
    using Value=std::array<unsigned,4>;
    Value temps[32]{},outputs[16]{},definitions[256]{};
    bool defined[256]{},input=false,executable=false;int position=-1;
    auto read=[&](std::uint32_t t,Value& value){
        const unsigned k=type(t),r=reg(t);
        if(!(t&0x80000000u)||(t&0x2000u)||!((k==0&&r<32)||(k==1&&r<16)||(k==2&&r<256)))return false;
        for(unsigned c=0;c<4;++c){const unsigned lane=(t>>(16+2*c))&3;
            value[c]=k==0?temps[r][lane]:k==1?(r==0?Input+lane:Unknown):defined[r]?definitions[r][lane]:Constant+4*r+lane;
            if((t>>24)&15)value[c]=Unknown;
        }return true;
    };
    auto constant=[](const Value& v,unsigned r){for(unsigned c=0;c<4;++c)if(v[c]!=Constant+4*r+c)return false;return true;};
    const Value vertex{Input,Input+1,Input+2,Input+3},view{View,View+1,View+2,One};
    for(std::size_t at=1;at<count;){
        const auto token=code[at++];const unsigned op=token&65535;
        if(op==65535)return token==65535&&at==count&&input&&position>=0&&outputs[position]==Value{Clip,Clip+1,Clip+2,Clip+3};
        if(op==65534){const unsigned n=(token>>16)&32767;if(n>count-at)return false;at+=n;continue;}
        if(token&0xf0000000u)return false; // predication/coissue
        const unsigned n=(token>>24)&15;if(n>count-at)return false;
        const auto* a=code+at;at+=n;
        if(op==0){if(n)return false;continue;}
        if(op==31){
            if(n!=2||executable)return false;
            if((a[0]&0xf001fu)==0){
                if(type(a[1])==6){if(position>=0||reg(a[1])>=16)return false;position=int(reg(a[1]));}
                if(type(a[1])==1){if(input||reg(a[1])!=0)return false;input=true;}
            }continue;
        }
        if(op==81){
            if(n!=5||executable||type(a[0])!=2||reg(a[0])>=256)return false;
            const unsigned r=reg(a[0]);if((r>=2&&r<=5)||(r>=31&&r<=33)||defined[r])return false;
            defined[r]=true;for(unsigned c=0;c<4;++c)definitions[r][c]=a[c+1]==0x3f800000u?One:Unknown;
            continue;
        }
        const int ns=sources(op);if(ns<0||n!=unsigned(ns+1))return false;executable=true;
        const unsigned k=type(a[0]),r=reg(a[0]),mask=(a[0]>>16)&15;
        if(!(a[0]&0x80000000u)||(a[0]&0x2000u)||!mask||!((k==0&&r<32)||(k==6&&r<16)))return false;
        Value operands[3]{},result{};for(int i=0;i<ns;++i)if(!read(a[i+1],operands[i]))return false;
        if(op==1)result=operands[0];
        if(op==9)for(unsigned i=0;i<2;++i){
            for(unsigned row=0;row<3;++row)if(constant(operands[i],31+row)&&operands[1-i]==vertex)result.fill(View+row);
            for(unsigned row=0;row<4;++row)if(constant(operands[i],2+row)&&operands[1-i]==view)result.fill(Clip+row);
        }
        // Saturation/scaling of a position invalidates the coordinate proof.
        if(a[0]&0x0ff00000u)result={};
        auto& dest=k==0?temps[r]:outputs[r];for(unsigned c=0;c<4;++c)if(mask&(1u<<c))dest[c]=result[c];
    }
    return false;
}
}
