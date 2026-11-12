#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Neutralise the game's own terrain shadow term inside a terrain pixel shader.
//
// The client's ps_2_0/ps_3_0 terrain shaders scale their diffuse result by
//     mad rY, S, 0.3, 0.7          (0.3 and 0.7 are `def` literals)
// where S is the baked ADT shadow (MCSH, alpha channel of the packed alpha-map
// texture) or min(S, dynamic shadow map) in the Terrain3 variants, and the
// specular term is multiplied by the same S. The shadow bit shares its texture
// with the layer blend weights, so texture substitution is impossible; the
// only clean route is replacing every read of S after its definition with a
// literal 1.0 that the shader already defines. Instruction lengths are
// unchanged, so no offsets move and the fog epilogue proof stays valid.
//
// Fail closed: any shader without exactly this shape, with predication,
// relative addressing on the affected operands, ps_1_x encoding, or without a
// safe literal substitution or identity fold is left untouched (returns false, output empty).
namespace NorthlightTerrainShadow {
using Word=std::uint32_t;
struct PatchInfo {
    unsigned major=0,minor=0,replaced=0,instructions=0;
    unsigned shadowRegister=0,shadowComponent=0;
    int oneConstant=-1;unsigned oneComponent=0;
    unsigned madIndex=0,definitionIndex=0;
};
inline unsigned sourceType(Word token){return ((token>>28)&7)|((token>>8)&24);}
inline unsigned sourceSwizzle(Word token){return (token>>16)&255;}
inline unsigned sourceModifier(Word token){return (token>>24)&15;}
inline bool sourceRelative(Word token){return (token&0x2000)!=0;}
inline unsigned replicatedSwizzle(unsigned component){return component*0x55u;}
inline bool replicated(Word token,unsigned& component){
    const unsigned swizzle=sourceSwizzle(token);
    for(unsigned c=0;c<4;++c)if(swizzle==replicatedSwizzle(c)){component=c;return true;}
    return false;
}
inline bool firstOperandIsSource(unsigned op){
    switch(op){case 25:case 26:case 27:case 28:case 38:case 40:case 41:case 44:case 45:case 65:case 96:return true;default:return false;}
}
inline float wordFloat(Word w){float f;static_assert(sizeof f==sizeof w,"float width");std::memcpy(&f,&w,sizeof f);return f;}

inline bool patch(const Word* code,std::size_t count,std::vector<Word>& output,PatchInfo* info=nullptr){
    output.clear();PatchInfo local;
    if(!code||count<3||(code[0]>>16)!=0xffff||code[count-1]!=0xffff)return false;
    local.major=(code[0]>>8)&255;local.minor=code[0]&255;
    if(local.major<2||local.major>3)return false;
    struct Instruction {std::size_t at;unsigned op,size;};
    std::vector<Instruction> instructions;
    float defs[224][4];bool defined[224]={};
    for(std::size_t at=1;at<count;){
        const Word token=code[at];const unsigned op=token&65535;
        if(op==65535){if(at+1!=count)return false;break;}
        if(op==65534){const std::size_t words=1+((token>>16)&32767);if(words>count-at)return false;at+=words;continue;}
        if(token&0x10000000u)return false; // predicated instruction
        const unsigned size=(token>>24)&15;
        if(size+1>count-at)return false;
        if(op==81){ // def cN, x, y, z, w
            if(size!=5||sourceType(code[at+1])!=2)return false;
            const unsigned index=code[at+1]&2047;if(index>=224)return false;
            for(unsigned k=0;k<4;++k)defs[index][k]=wordFloat(code[at+2+k]);defined[index]=true;
        }
        if(op!=81&&op!=82&&op!=83&&op!=31)instructions.push_back({at,op,size});
        at+=1+size;
    }
    if(instructions.empty())return false;
    local.instructions=unsigned(instructions.size());
    // Locate the unique "mad dst, S, 0.3, 0.7" with literal constant sources.
    auto literal=[&](Word token,float value){
        if(sourceType(token)!=2||sourceModifier(token)||sourceRelative(token))return false;
        const unsigned index=token&2047;unsigned component;
        if(index>=224||!defined[index]||!replicated(token,component))return false;
        return std::fabs(defs[index][component]-value)<.001f;
    };
    int mad=-1;
    for(std::size_t i=0;i<instructions.size();++i){
        const auto& in=instructions[i];if(in.op!=4||in.size!=4)continue;
        const Word* a=code+in.at+1;
        if(!literal(a[2],.3f)||!literal(a[3],.7f))continue;
        unsigned component;
        if(sourceType(a[1])!=0||sourceModifier(a[1])||sourceRelative(a[1])||!replicated(a[1],component))return false;
        if(mad>=0)return false; // two shadow scalings: unknown shape
        mad=int(i);local.shadowRegister=a[1]&2047;local.shadowComponent=component;
    }
    if(mad<0)return false;
    // A literal 1.0 the shader already defines replaces S. Prefer a lane of
    // the register holding 0.3/0.7: ps_2_0/ps_3_0 allow one constant register
    // per instruction, so the scale then stays a single-c# mad. Otherwise the
    // scale is rewritten as add(0.3, 0.7) + nop (same word count) and the
    // literal serves only instructions that read no other constant.
    const unsigned scaleConstant=code[instructions[mad].at+3]&2047;
    for(unsigned k=0;k<4;++k)if(defs[scaleConstant][k]==1.f){local.oneConstant=int(scaleConstant);local.oneComponent=k;break;}
    for(unsigned index=0;index<224&&local.oneConstant<0;++index)if(defined[index])
        for(unsigned k=0;k<4;++k)if(defs[index][k]==1.f){local.oneConstant=int(index);local.oneComponent=k;break;}
    const Word one=local.oneConstant<0?0:0x80000000u|(2u<<28)|(replicatedSwizzle(local.oneComponent)<<16)|Word(local.oneConstant);
    // S is live from its last write before the mad until (and including the
    // source reads of) the next write to its lane, the mad itself when it
    // writes that lane in place.
    auto writesShadow=[&](const Instruction& in){
        if(firstOperandIsSource(in.op)||in.size<1)return false;
        const Word dst=code[in.at+1];
        return sourceType(dst)==0&&(dst&2047)==local.shadowRegister&&(((dst>>16)&15)>>local.shadowComponent&1)!=0;
    };
    int definition=-1;for(int i=mad-1;i>=0;--i)if(writesShadow(instructions[i])){definition=i;break;}
    if(definition<0)return false;
    std::size_t end=instructions.size()-1;
    if(writesShadow(instructions[mad]))end=std::size_t(mad);
    else for(std::size_t i=std::size_t(mad)+1;i<instructions.size();++i)if(writesShadow(instructions[i])){end=i;break;}
    local.madIndex=unsigned(mad);local.definitionIndex=unsigned(definition);
    output.assign(code,code+count);
    auto isShadowRead=[&](Word token){
        unsigned component;
        return (token&0x80000000u)&&sourceType(token)==0&&(token&2047)==local.shadowRegister&&replicated(token,component)&&component==local.shadowComponent;
    };
    for(std::size_t i=std::size_t(definition)+1;i<=end;++i){
        const auto& in=instructions[i];
        const unsigned first=firstOperandIsSource(in.op)?0:1;
        unsigned reads=0,otherConstant=0;std::size_t readAt=0;
        for(unsigned k=first;k<in.size;++k){
            const Word token=code[in.at+1+k];
            if(isShadowRead(token)){
                if(sourceRelative(token)||sourceModifier(token)){output.clear();return false;}
                ++reads;readAt=k;
            } else if((token&0x80000000u)&&sourceType(token)==2&&(local.oneConstant<0||(token&2047)!=unsigned(local.oneConstant)))++otherConstant;
        }
        if(!reads)continue;
        // Native D3D9 rejects the all-constant MAD produced by substituting
        // S=1 into its literal 0.3/0.7 scale. Fold that scale to ADD + NOP.
        if(local.oneConstant>=0&&!otherConstant&&i!=std::size_t(mad)){
            for(unsigned k=first;k<in.size;++k)if(isShadowRead(code[in.at+1+k])){output[in.at+1+k]=one;++local.replaced;}
            continue;
        }
        // Single-c# rule: fold S=1 into the instruction and pad with nop.
        Word* w=output.data()+in.at;
        if(in.op==4&&in.size==4&&reads==1&&(readAt==1||readAt==2)){w[0]=2u|(3u<<24);w[2]=w[readAt==1?3:2];w[3]=w[4];w[4]=0;++local.replaced;continue;}
        if(in.op==5&&in.size==3&&reads==1){w[0]=1u|(2u<<24);w[2]=w[readAt==1?3:2];w[3]=0;++local.replaced;continue;}
        output.clear();return false;
    }
    if(!local.replaced){output.clear();return false;}
    if(info)*info=local;
    return true;
}
} // namespace NorthlightTerrainShadow
