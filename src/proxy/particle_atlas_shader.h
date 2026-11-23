#pragma once
#include "shader_read_set.h"
#include <array>

namespace NorthlightParticleAtlas {
// Only change a declaration input when its sole consumer is a straight
// move to the output read by one ordinary, implicit-LOD texture sample.
namespace Proof {
using Word=std::uint32_t;
using namespace NorthlightShaderReads::Detail;
inline unsigned reg(Word token){return token&0x70001fffu;}
inline unsigned key(unsigned type,unsigned index){return reg((type&7)<<28|(type&24)<<8|index);}

template<class Visit> bool visit(const Word* words,unsigned size,Visit op){
    if(!words||size<2)return false;
    const unsigned major=(words[0]>>8)&255;
    for(unsigned i=1;i<size;){
        const Word token=words[i++];
        const unsigned code=token&0xffff;
        if(code==0xffff)return token==0xffff&&i==size;
        if(code==0xfffe){
            const unsigned length=(token>>16)&0x7fff;
            if(length>size-i)return false;
            i+=length;
            continue;
        }
        const int length=major>=2?(knownOpcode(code)?int((token>>24)&15):-1):vs1Length(code);
        if(length<0||unsigned(length)>size-i||(token&0xf0000000u)||
           !op(token,words+i,unsigned(length)))return false;
        i+=length;
    }
    return false;
}

inline int pixelUv(const Word* code,unsigned size,unsigned stage){
    if(!size||(code[0]!=0xffff0200u&&code[0]!=0xffff0300u))return -1;
    const bool ps3=code[0]==0xffff0300u;
    const unsigned inputType=ps3?1:3;
    int coord=-1;
    std::array<unsigned,16> use{},semantic{};
    semantic.fill(~0u);
    if(!visit(code,size,[&](Word token,const Word* a,unsigned n){
        const unsigned op=token&0xffff;
        if(op==31){
            if(n!=2)return false;
            if(regType(a[1])==inputType&&regNum(a[1])<16&&(!ps3||(a[0]&31)==5))
                semantic[regNum(a[1])]=ps3?(a[0]>>16)&15:regNum(a[1]);
            return true;
        }
        if(op==48||op==47||op==81)return true;
        for(unsigned i=!noDestination(op);i<n;++i){
            if(!(a[i]&0x80000000u))return false;
            if(regType(a[i])==inputType){
                if((a[i]&0x2000)||regNum(a[i])>=16)return false;
                ++use[regNum(a[i])];
            }
        }
        if(n<1||regType(a[n-1])!=10||regNum(a[n-1])!=stage)return true;
        if(op!=66||n!=3||coord>=0||(token&0x00ff0000u)||regType(a[1])!=inputType||
           (a[1]&0x0fff2000u)!=0x00e40000u)return false;
        coord=int(regNum(a[1]));
        return true;
    })||coord<0||coord>=16||use[unsigned(coord)]!=1||semantic[unsigned(coord)]>=16)return -1;
    return int(semantic[unsigned(coord)]);
}

inline int vertexUv(const Word* code,unsigned size,unsigned uv){
    if(!size||(code[0]>>16)!=0xfffe)return -1;
    const unsigned major=(code[0]>>8)&255;
    if(major<1||major>3)return -1;
    unsigned output=major==3?~0u:key(6,uv);
    std::array<unsigned,16> inputSemantic{},use{};
    inputSemantic.fill(~0u);
    unsigned source=~0u,writes=0;
    if(!visit(code,size,[&](Word token,const Word* a,unsigned n){
        const unsigned op=token&0xffff;
        if(op==31){
            if(n!=2)return false;
            if((a[0]&31)!=5)return true;
            const unsigned semantic=(a[0]>>16)&15,index=regNum(a[1]);
            if(regType(a[1])==1&&index<16)inputSemantic[index]=semantic;
            if(major==3&&regType(a[1])==6&&semantic==uv)output=reg(a[1]);
            return true;
        }
        if(op==48||op==47||op==81)return true;
        for(unsigned i=!noDestination(op);i<n;++i){
            if(!(a[i]&0x80000000u))return false;
            if(regType(a[i])==1){
                if(regNum(a[i])>=16||(a[i]&0x2000))return false;
                ++use[regNum(a[i])];
            }
        }
        if(n&&reg(a[0])==output){
            if(++writes!=1||op!=1||n!=2||(a[0]&0x0ff30000u)!=0x00030000u||
               regType(a[1])!=1||(a[1]&0x0fff2000u)!=0x00e40000u)return false;
            source=regNum(a[1]);
        }
        return true;
    })||writes!=1||source>=16||use[source]!=1||inputSemantic[source]>=16)return -1;
    return int(inputSemantic[source]);
}

// Stock 12340 particles form (u,v,1) in a temporary and write the
// sampled coordinate with two dot products against c6/c7.
inline int affineVertexUv(const Word* code,unsigned size,unsigned uv){
    if(!size||code[0]!=0xfffe0300u)return -1;
    unsigned output=~0u,input=~0u,phase=0;
    std::array<unsigned,16> semantic{},uses{};
    semantic.fill(~0u);
    bool unit=false;
    if(!visit(code,size,[&](Word token,const Word* a,unsigned n){
        const auto op=token&0xffff;
        if(op==31){
            if(n!=2)return false;
            if((a[0]&31)!=5)return true;
            if(regType(a[1])==1&&regNum(a[1])<16)semantic[regNum(a[1])]=(a[0]>>16)&15;
            if(regType(a[1])==6&&((a[0]>>16)&15)==uv)output=a[1]&~0x000f0000u;
            return true;
        }
        if(op==48||op==47||op==81){
            if(op!=81||n!=5)return true;
            if(regNum(a[0])==6||regNum(a[0])==7)return false;
            if(a[0]==0xa00f0000u&&a[1]==0x3f800000u&&!a[2])unit=true;
            return true;
        }
        const bool writesUv=n&&(a[0]&~0x000f0000u)==output;
        for(unsigned i=!noDestination(op);i<n;++i){
            if(regType(a[i])==1){
                if(regNum(a[i])>=16||(a[i]&0x2000u))return false;
                ++uses[regNum(a[i])];
            }
            if(phase==3&&reg(a[i])==key(0,0))return false;
        }
        if(phase==3)return !writesUv;
        if(!phase){
            if(op==4&&n==4&&a[0]==0x80070000u&&(a[1]&~0x7ffu)==0x90c40000u&&
               a[2]==0xa0d00000u&&a[3]==0xa0c50000u){
                input=regNum(a[1]);phase=1;
            }
            return !writesUv;
        }
        if(!writesUv||op!=8||n!=3||a[0]!=(output|(phase<<16))||
           a[1]!=(0xa0f40005u+phase)||a[2]!=0x80e40000u)return false;
        ++phase;
        return true;
    })||!unit||phase!=3||input>=16||uses[input]!=1||semantic[input]>=16)return -1;
    return int(semantic[input]);
}
}
}
