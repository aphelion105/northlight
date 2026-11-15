#pragma once
#include "actor_deformation.h"
#include "name_stencil.h"
#include "shader_tags.h"

namespace NorthlightUnlitReceiver {
constexpr unsigned long Bit=0x40;
constexpr unsigned long PrelitBit=0x20;
constexpr unsigned long AuthoredBit=0x10;
// Prove the live COLOR0 lighting equation independently of shader identity.
// Material-modified WMO programs retain c10 + dot(normal,-c12)*c11 even when
// their hash no longer belongs to the stock catalog. Alpha/fog do not qualify.
inline bool nativeLightingShader(const uint32_t* words,size_t count){
    using namespace NorthlightActorDeformation;
    Program color;if(!compile(words,count,color,true,2,10,0,7)||!color.kernel.valid)return false;
    unsigned values[33][4]={},found=0;
    const auto component=[&](const Kernel::Src& s,unsigned c){
        if(s.kind==2&&!s.relative&&!s.modifier&&s.definition<0&&(s.index==10||s.index==11))return 10u*(s.index-9)+s.swizzle[c];
        return s.kind==0&&!s.relative&&!s.modifier&&s.index<33?values[s.index][s.swizzle[c]]:0u;
    };
    const auto constant=[](const Kernel::Src& s,unsigned index,unsigned c,unsigned modifier=0){
        return s.kind==2&&s.index==index&&s.definition<0&&!s.relative&&s.modifier==modifier&&s.swizzle[c]==c;
    };
    for(const auto& op:color.kernel.ops){
        if(op.address)continue;
        unsigned result[4]={};
        if(op.code==1&&!op.saturate)for(unsigned c=0;c<4;++c)result[c]=component(op.source[0],c);
        else if(op.code==36&&!op.saturate)for(unsigned c=0;c<3;++c)result[c]=c+1;
        else if(op.code==8){
            for(unsigned side=0;side<2;++side){bool dot=true;
                for(unsigned c=0;c<3;++c)dot&=constant(op.source[side],12,c,1)&&component(op.source[1-side],c)==c+1;
                if(dot)std::fill_n(result,4,4u);
            }
        }else if(op.code==4){
            for(unsigned c=0;c<3;++c)if((op.mask&(1u<<c))&&component(op.source[0],c)==4&&
                component(op.source[1],c)==20+c&&component(op.source[2],c)==10+c)found|=1u<<c;
        }
        for(unsigned c=0;c<4;++c)if(op.mask&(1u<<c))values[op.target][c]=result[c];
    }
    return found==7;
}
// A lit shader may receive a room/group override instead of world lighting.
// Those native colours are already in the framebuffer; outdoor subtraction
// cannot recover them. Compare only RGB, never the packed neighbouring W lane.
inline bool lightingOverride(const float* native,const float* ambient,const float* direct){
    bool different=false;
    for(unsigned i=0;i<3;++i){
        for(unsigned row=0;row<2;++row){
            const float a=native[row*4+i],b=(row?direct:ambient)[i];
            if(!std::isfinite(a)||!std::isfinite(b)||a<0||a>8||b<0||b>8)return false;
            different|=std::fabs(a-b)>.02f;
        }
    }
    return different;
}
// D24X4S4 and D15S1 cannot represent bit 6.
inline bool stencilFormat(unsigned long format,unsigned long intz){return format==75||format==81||format==intz;}
// The caller proves the complete shader identity belongs to an M2 model.
// Slice RGB only: fog, alpha and environment UVs may still read a normal.
inline bool shader(const uint32_t* words,size_t count,bool model){
    if(!model)return false;
    NorthlightActorDeformation::Program color;
    if(!NorthlightActorDeformation::compile(words,count,color,true,2,10,0,7))return false;
    for(const auto& input:color.inputs)if(input.usage==3)return false;
    return true;
}
// WMO identity alone does not imply prelit: failed outdoor-light decoding is
// also represented by lighting=false in the context catalog. Prove the RGB
// dependency slice contains vertex colours and no normal/global-light inputs.
inline bool prelitShader(const uint32_t* words,size_t count,bool wmo){
    if(!wmo)return false;
    NorthlightActorDeformation::Program color;
    if(!NorthlightActorDeformation::compile(words,count,color,true,2,10,0,7))return false;
    bool vertexColor=false;
    for(const auto& input:color.inputs){if(input.usage==3)return false;vertexColor|=input.usage==10;}
    for(const auto& op:color.kernel.ops)for(int i=0;i<op.operands;++i){const auto& s=op.source[i];
        if(s.kind==2&&(s.relative||(s.index>=10&&s.index<=12&&s.definition<0)))return false;}
    return vertexColor;
}
// Material edits may change an interior shader's identity without changing
// its coordinates or baked RGB. Use the same admission as world capture, then
// prove RGB separately; coordinate proof alone never grants prelit lighting.
inline bool prelitWorldShader(const uint32_t* words,size_t count,uint64_t hash){
    const bool world=(NorthlightShaderTags::triggerTags(hash,words,count*sizeof(*words))&NorthlightShaderTags::kWmo)!=0;
    return prelitShader(words,count,world);
}
using namespace NorthlightNameStencil;
inline constexpr Set Mark[]={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,Bit},{StencilWriteMask,Bit|PrelitBit|AuthoredBit|kBit},{TwoSidedStencilMode,0}};
inline constexpr Set MarkPrelit[]={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,PrelitBit},{StencilWriteMask,Bit|PrelitBit|AuthoredBit|kBit},{TwoSidedStencilMode,0}};
inline constexpr Set MarkAuthored[]={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,AuthoredBit},{StencilWriteMask,Bit|PrelitBit|AuthoredBit|kBit},{TwoSidedStencilMode,0}};
inline constexpr Set Unmark[]={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,0},{StencilWriteMask,Bit|PrelitBit|AuthoredBit|kBit},{TwoSidedStencilMode,0}};
}
