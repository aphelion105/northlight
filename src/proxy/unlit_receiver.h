#pragma once
#include <algorithm>
#include "actor_deformation.h"
#include "name_stencil.h"
#include "shader_tags.h"

namespace NorthlightUnlitReceiver {
constexpr unsigned long Bit=0x40;
constexpr unsigned long PrelitBit=0x20;
constexpr unsigned long AuthoredBit=0x10;
// Existing stencil bits encode the part of a room bank that probes may
// attenuate. Round down when the directional and ambient colours differ.
inline constexpr unsigned RoomAmbientQuarter=4;
inline constexpr unsigned RoomSharedTint=5;
inline constexpr unsigned long RoomRef[]={AuthoredBit,AuthoredBit|PrelitBit,AuthoredBit|Bit,PrelitBit,
                                         AuthoredBit|PrelitBit|Bit,PrelitBit|Bit};
inline constexpr float RoomAmbientShare[]={0.f,1.f/3.f,2.f/3.f,1.f,.25f,.8f};
inline constexpr unsigned RoomLevelCount=sizeof(RoomRef)/sizeof(RoomRef[0]);
// Posed (skinned) M2s let measured probe occlusion attenuate their native room
// bank. Rigid doodads keep it, like the authored WMO room around them: probes
// cannot measure authored room fill and read nearly black inside, which left
// props at a fraction of the walls' brightness.
inline constexpr bool modelRoomBank(bool menuScene,unsigned family,bool skinned){
    return !menuScene&&family==3&&skinned;
}
inline constexpr unsigned receiverMaskKind(bool prelit,bool authored,unsigned roomLevel){
    return prelit||(authored&&roomLevel==3)?4u:authored?5u+roomLevel:3u;
}
inline unsigned long roomStencilMask(unsigned levels){
    unsigned long mask=0;
    for(unsigned level=0;level<RoomLevelCount;++level)if(levels&(1u<<level))mask|=RoomRef[level];
    return mask;
}
inline unsigned roomAmbientLevel(const float* bank){
    const float ambient=bank[4]+bank[5]+bank[6];
    if(ambient==0)return 0;
    const float direct=bank[0]+bank[1]+bank[2];
    if(direct>0){
        // The indoor M2 bank can split one authored room tint across its
        // directional and omnidirectional terms. Both belong to the baked
        // receiver. Keep a small share of the native tint so dark probes
        // cannot erase the material's shape in rooms with no measured bounce.
        const float scale=ambient/direct;
        constexpr float colorStep=1.f/255.f;
        if(std::fabs(bank[4]-scale*bank[0])<=colorStep&&
           std::fabs(bank[5]-scale*bank[1])<=colorStep&&
           std::fabs(bank[6]-scale*bank[2])<=colorStep)return RoomSharedTint;
    }
    unsigned level=3;
    bool quarter=true;
    for(unsigned c=0;c<3;++c){
        const float direct=bank[c],ambient=bank[4+c];
        if(direct>0){
            const unsigned channel=ambient>=2*direct?2u:2*ambient>=direct?1u:0u;
            level=std::min(level,channel);
            quarter&=3*ambient>=direct;
        }
    }
    return !level&&quarter?RoomAmbientQuarter:level;
}
// Prove the live COLOR0 lighting equation independently of shader identity.
// Material-modified WMO programs retain c10 + dot(normal,-c12)*c11 even when
// their hash no longer belongs to the stock catalog. Alpha/fog do not qualify.
inline bool nativeLightingShader(const uint32_t* words,size_t count,bool model=false){
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
                component(op.source[1],c)==(model?10u:20u)+c&&component(op.source[2],c)==(model?20u:10u)+c)found|=1u<<c;
        }
        for(unsigned c=0;c<4;++c)if(op.mask&(1u<<c))values[op.target][c]=result[c];
    }
    return found==7;
}
// A lit shader may receive a room/group override instead of world lighting.
// Those native colours are already in the framebuffer; outdoor subtraction
// cannot recover them. Compare only RGB, never the packed neighbouring W lane.
inline bool lightingOverride(const float* native,const float* ambient,const float* direct,bool model=false){
    // M2 uses c10=direct,c11=ambient; WMO uses the opposite order.
    const float* rows[]={native+4*unsigned(model),native+4*unsigned(!model)};
    const float* scene[]={ambient,direct};
    float differences[2]={};
    for(unsigned row=0;row<2;++row)for(unsigned i=0;i<3;++i){
        const float a=rows[row][i],b=scene[row][i];
        if(!std::isfinite(a)||!std::isfinite(b)||a<0||a>8||b<0||b>8)return false;
        differences[row]=std::max(differences[row],std::fabs(a-b));
    }
    if(std::max(differences[0],differences[1])<=.02f)return false;
    if(model){
        // Outdoor M2s can scale one light term without replacing its RGB tint.
        // Require the other term to be present and unchanged at byte precision.
        constexpr float colorStep=1.f/255.f;
        const unsigned scaled=differences[0]>differences[1]?0:1;
        const unsigned anchor=1-scaled;
        const auto peak=[](const float* rgb){return *std::max_element(rgb,rgb+3);};
        if(differences[anchor]<=colorStep&&peak(scene[anchor])>.02f&&
           peak(scene[scaled])>.02f&&peak(rows[scaled])>.02f){
            const float scale=peak(rows[scaled])/peak(scene[scaled]);
            const float tolerance=colorStep*(1.f+scale);
            if(std::fabs(rows[scaled][0]-scene[scaled][0]*scale)<=tolerance&&
               std::fabs(rows[scaled][1]-scene[scaled][1]*scale)<=tolerance&&
               std::fabs(rows[scaled][2]-scene[scaled][2]*scale)<=tolerance)return false;
        }
    }
    return true;
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
using StencilMark=Set[sizeof(Mark)/sizeof(Set)];
template<unsigned Level>
inline constexpr StencilMark RoomMark={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,RoomRef[Level]},{StencilWriteMask,Bit|PrelitBit|AuthoredBit|kBit},{TwoSidedStencilMode,0}};
inline constexpr auto& MarkAuthored=RoomMark<0>;
inline const StencilMark& receiverMark(unsigned kind){
    if(kind==3)return Mark;
    if(kind==4)return MarkPrelit;
    if(kind==6)return RoomMark<1>;
    if(kind==7)return RoomMark<2>;
    if(kind==5+RoomAmbientQuarter)return RoomMark<RoomAmbientQuarter>;
    if(kind==5+RoomSharedTint)return RoomMark<RoomSharedTint>;
    return MarkAuthored;
}
inline constexpr Set Unmark[]={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,0},{StencilWriteMask,Bit|PrelitBit|AuthoredBit|kBit},{TwoSidedStencilMode,0}};
}
