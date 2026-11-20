#pragma once
#include "patch_shadow_shader.h"
#include <cstddef>
#include <cstdint>

// Read-only metadata for the audited straight-line VS1/VS2/VS3 replay shaders.
// Compute once when registering the original shader, never per draw. This does
// not change shader bytecode and is not a replacement for shader identity gates.
namespace NorthlightShaderConstants {
struct Range {unsigned first=0,count=0;};
struct Usage {
    Range floats{0,256},booleans{0,16},integers{0,16};
    bool analyzed=false,relativeFloat=false;
    Range weightedFloats{}; // Fixed rows of an audited c[31..33 + 3 * BLENDINDICES0] shader.
};

namespace Detail {
template<std::size_t N> inline Range range(const bool (&used)[N]){
    std::size_t first=0,last=N;
    while(first<N&&!used[first])++first;
    if(first==N)return {};
    while(last>first&&!used[last-1])--last;
    return {unsigned(first),unsigned(last-first)};
}
inline bool constant(unsigned type){return type==2||type==7||type==14||(type>=11&&type<=13);}
}

// projectionKind1 requires c4..7; kind2 requires c2..5. These rows are included
// even if the shader does not read them because the caller validates/replaces
// the projection. Unknown kind/version/instruction/bank returns full capture.
// Relative constants conservatively require all 256 float registers. A valid
// relative shader can still omit BOOL/INT readbacks when neither bank is read.
inline Usage analyze(const uint32_t* words,std::size_t wordCount,unsigned projectionKind){
    using namespace NorthlightShadowShader;
    Usage fallback;
    if(!words||wordCount<2||wordCount>65536||(projectionKind!=1&&projectionKind!=2))return fallback;
    unsigned major=(words[0]>>8)&255;
    if(major<1||major>3||words[0]!=(major==1?0xfffe0101u:major==2?0xfffe0200u:0xfffe0300u))return fallback;
    bool floats[256]={},booleans[16]={},integers[16]={};
    bool defFloat[256]={},defBool[16]={},defInt[16]={};
    bool relative=false,executable=false,ended=false;
    bool palette=major>=2,blendIndices=false,scaleThree=false,hasAddress=false;
    uint32_t previousIndexMultiply=0;
    std::size_t cursor=1;
    while(cursor<wordCount){
        uint32_t instruction=words[cursor];unsigned op=instruction&65535;
        if(op==65535){if(instruction!=0xffff||cursor+1!=wordCount)return fallback;ended=true;break;}
        if(op==65534){
            std::size_t length=(instruction>>16)&32767;
            if(length>wordCount-cursor-1)return fallback;
            cursor+=length+1;continue;
        }
        // Predication and unknown flow are rejected by the existing patcher;
        // keep their capture conservative rather than trying to infer paths.
        if(instruction&0xf0000000u)return fallback;
        int implicit=op==0?0:op==31?2:op==81?5:arithmeticOperands(op,major);
        if(major==1&&implicit<0)return fallback;
        unsigned length=major==1?unsigned(implicit):(instruction>>24)&15;
        if(length>wordCount-cursor-1)return fallback;
        std::size_t next=cursor+length+1;
        const uint32_t indexMultiply=previousIndexMultiply;
        previousIndexMultiply=0;
        if(op==31){
            if(length!=2||executable)return fallback;
            uint32_t destination=words[cursor+2];
            if(!(destination&0x80000000u)||(destination&0x2000u)||Detail::constant(regType(destination)))return fallback;
            // Declaration usage bits are semantics, not register operands.
            if(regType(destination)==1&&regIndex(destination)==2)
                blendIndices=(words[cursor+1]&0x000f000fu)==2u&&destination==0x900f0002u;
        }else if(op==81||op==48||op==47){
            if(executable||length!=(op==47?2u:5u))return fallback;
            uint32_t destination=words[cursor+1];unsigned type=regType(destination),index=regIndex(destination);
            if(!(destination&0x80000000u)||(destination&0x2000u))return fallback;
            if(op==81){if(type!=2||index>=256)return fallback;defFloat[index]=true;if(!index)scaleThree=words[cursor+2]==0x40400000u;}
            else if(op==48){if(type!=7||index>=16)return fallback;defInt[index]=true;}
            else{if(type!=14||index>=16)return fallback;defBool[index]=true;}
            // DEF/DEFI/DEFB payloads are literal bit patterns. Never inspect
            // them as source tokens, even if their high bits resemble c/i/b.
        }else if(op==0){
            if(length)return fallback;executable=true;
        }else{
            executable=true;int formal=arithmeticOperands(op,major);
            if(formal<0)return fallback;
            // Match the stock full-vector MUL/MOVA sequence. Any other a0
            // writer or relative operand invalidates the narrow upload proof.
            if(op==5&&length==3&&(words[cursor+1]&~31u)==0x800f0000u&&
               ((words[cursor+2]==0xa0000000u&&words[cursor+3]==0x90e40002u)||
                (words[cursor+2]==0x90e40002u&&words[cursor+3]==0xa0000000u)))
                previousIndexMultiply=words[cursor+1];
            std::size_t parameter=cursor+1;
            for(int operand=0;operand<formal;++operand){
                if(parameter>=next)return fallback;
                uint32_t token=words[parameter++];unsigned type=regType(token),index=regIndex(token);
                if(!(token&0x80000000u))return fallback;
                bool indirect=(token&0x2000u)!=0;
                if(operand==0){
                    if(indirect||Detail::constant(type))return fallback;
                    if(type==3){
                        palette=palette&&!hasAddress&&op==46&&length==2&&token==0xb00f0000u&&
                            indexMultiply&&words[cursor+2]==(0x80e40000u|(indexMultiply&31u));
                        hasAddress=true;
                    }
                }else{
                    // Extended float banks encode c2048/c4096/c6144 rather than
                    // c0..255. They are outside this client's capture contract.
                    if(type>=11&&type<=13)return fallback;
                    unsigned rows=1;
                    if(operand==2&&op>=20&&op<=24)rows=(op==20||op==22)?4u:op==24?2u:3u;
                    if(type==2){
                        if(index>=256||rows>256-index)return fallback;
                        for(unsigned row=0;row<rows;++row)floats[index+row]=true;
                    }else if(type==7){
                        if(index>=16||rows>16-index)return fallback;
                        for(unsigned row=0;row<rows;++row)integers[index+row]=true;
                    }else if(type==14){
                        if(index>=16||rows>16-index)return fallback;
                        for(unsigned row=0;row<rows;++row)booleans[index+row]=true;
                    }else if(type>19)return fallback;
                    if(indirect){
                        // Any relative source makes float capture conservative;
                        // constants can be addressed through an arbitrary a0/aL.
                        // Relative bool/int operands are not within our proof.
                        if(type==7||type==14)return fallback;
                        relative=true;
                        // VS1.1 c[a0.x+n] has an implicit address and no extra
                        // token. Preserve the full float bank for that path.
                        if(major>=2){if(parameter>=next)return fallback;
                            uint32_t address=words[parameter++];unsigned addressType=regType(address);
                            if(!(address&0x80000000u)||(address&0x2000u)||(addressType!=3&&addressType!=15))return fallback;
                            palette=palette&&hasAddress&&type==2&&index>=31&&index<=33&&rows==1&&
                                (address==0xb0000000u||address==0xb0550000u||address==0xb0aa0000u||address==0xb0ff0000u);}
                    }
                }
            }
            if(parameter!=next)return fallback;
        }
        cursor=next;
    }
    if(!ended)return fallback;
    // Locally defined constants are shader-owned. Get*Constant reads only API
    // state, and replaying another value cannot override a shader DEF anyway.
    for(unsigned i=0;i<256;++i)if(defFloat[i])floats[i]=false;
    for(unsigned i=0;i<16;++i){if(defBool[i])booleans[i]=false;if(defInt[i])integers[i]=false;}
    unsigned projectionStart=projectionKind==1?4:2;
    for(unsigned i=projectionStart;i<projectionStart+4;++i){
        // A literal projection would defeat the parent's matrix replacement.
        // Such shaders are rejected by its identity/layout gate; retain full
        // capture defensively if this helper is called without that gate.
        if(defFloat[i])return fallback;
        floats[i]=true;
    }
    Usage result;result.analyzed=true;result.relativeFloat=relative;
    result.floats=relative?Range{0,256}:Detail::range(floats);
    if(relative&&palette&&blendIndices&&scaleThree&&hasAddress)result.weightedFloats=Detail::range(floats);
    result.booleans=Detail::range(booleans);result.integers=Detail::range(integers);
    return result;
}
} // namespace NorthlightShaderConstants
