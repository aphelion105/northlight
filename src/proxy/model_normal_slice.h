#pragma once
#include "water_shader_patch.h"
#include <array>
#include <new>

namespace NorthlightModelNormalSlice {
using namespace NorthlightWaterShaderPatch;
struct Op {size_t at;unsigned code,count;};
inline bool arithmetic(unsigned op){
    return (op>=1&&op<=24)||(op>=32&&op<=36)||op==46;
}
inline unsigned channels(unsigned op,unsigned observed){
    if(op==8||op==33||op==36)return 7;
    if(op==9||op==16||op==17)return 15;
    if(op==6||op==7||op==14||op==15||op==32)return 1;
    if(op>=20&&op<=24)return op==20||op==21?15:7;
    return observed;
}
template<class Read> bool sources(const Word* code,const Op& op,unsigned observed,Read read){
    const bool kill=op.code==65;const unsigned formal=unsigned(arity(op.code,3));
    size_t at=op.at+(kill?1:2),end=op.at+op.count+1;
    for(unsigned i=kill?0:1;i<formal;++i){
        if(at>=end)return false;const Word token=code[at++];if(!(token&0x80000000u))return false;
        const unsigned type=regType(token),reg=regIndex(token),modifier=(token>>24)&15;
        if(modifier>13||!((type==0&&reg<32)||(type==1&&reg<16)||(type==2&&reg<256)||
            (type==3&&reg==0)||((type==7||type==14)&&reg<16)))return false;
        unsigned mask=kill?15:channels(op.code,observed);
        if(op.code>=20&&op.code<=24&&i==2){if(type!=2)return false;mask=15;}
        if(modifier==9)mask|=4;if(modifier==10)mask|=8;
        unsigned swizzled=0;for(unsigned j=0;j<4;++j)if(mask&(1u<<j))swizzled|=1u<<((token>>(16+j*2))&3);
        if(!read(type,reg,swizzled))return false;
        if(token&0x2000u){
            if(type!=2||at>=end)return false;const Word address=code[at++];
            if(!(address&0x80000000u)||(address&0x2000u)||regType(address)!=3||regIndex(address)!=0)return false;
            if(!read(3,0,1u<<((address>>16)&3)))return false;
        }
    }
    return at==end;
}
inline bool parse(const Word* code,size_t count,bool vertex,std::vector<Op>& ops){
    if(!code||count<2||count>65536||code[0]!=(vertex?0xfffe0300u:0xffff0300u))return false;
    for(size_t at=1;at<count;){
        const unsigned op=code[at]&65535;
        if(op==65535)return code[at]==65535&&at+1==count;
        const size_t n=op==65534?(code[at]>>16)&32767:(code[at]>>24)&15;
        if(n>=count-at)return false;
        if(op==65534){at+=n+1;continue;}
        if(code[at]&0xf0000000u)return false;
        if(op==31){if(n!=2||!(code[at+2]&0x80000000u)||(code[at+2]&0x2000u)||!((code[at+2]>>16)&15))return false;}
        else if(op==81||op==48||op==47){
            if(n!=(op==47?2u:5u)||regType(code[at+1])!=(op==81?2u:op==48?7u:14u)||(code[at+1]&0x2000u))return false;
            if(regIndex(code[at+1])>=(op==81?256u:16u))return false;
        }else if(op){
            if(!arithmetic(op)&&!(op==65&&!vertex))return false;
            if(!n)return false;
            if(op!=65){const Word destination=code[at+1];const unsigned type=regType(destination),reg=regIndex(destination);
                if(!(destination&0x80000000u)||(destination&0x2000u)||!((type==0&&reg<32)||(vertex&&type==6&&reg<12)||(vertex&&type==3&&reg==0)||(!vertex&&type==8&&reg<4)))return false;}
            if(!sources(code,{at,op,unsigned(n)},15,[](unsigned,unsigned,unsigned){return true;}))return false;
        }else if(n)return false;
        ops.push_back({at,op,unsigned(n)});at+=n+1;
    }
    return false;
}
// Only the capture's two known inputs may survive. Coverage using an authored
// input, texture or unsupported instruction retains the complete original VS.
inline bool pixel(const Word* code,size_t count,std::vector<Word>& out)try{
    std::vector<Op> ops;if(!parse(code,count,false,ops))return false;
    unsigned declared[16]={},used[16]={};int normal=-1,depth=-1;
    for(const auto& op:ops)if(op.code==31&&regType(code[op.at+2])==1){
        const unsigned reg=regIndex(code[op.at+2]);if(reg>=16||declared[reg])return false;declared[reg]=(code[op.at+2]>>16)&15;
        const unsigned semantic=code[op.at+1]&0xfffff;
        if(semantic==0x50005){if(normal>=0)return false;normal=int(reg);}
        if(semantic==0x70005){if(depth>=0)return false;depth=int(reg);}
    }
    if(normal<0||depth<0)return false;
    for(const auto& op:ops)if(arithmetic(op.code)||op.code==65){
        const unsigned observed=op.code==65?15:(code[op.at+1]>>16)&15;
        if(!sources(code,op,observed,[&](unsigned type,unsigned reg,unsigned mask){
            if(type==3)return false;
            if(type==1){if(reg!=unsigned(normal)&&reg!=unsigned(depth))return false;used[reg]|=mask;}
            return true;
        }))return false;
    }
    if(used[normal]!=7||used[depth]!=8)return false;
    std::vector<Word> result{code[0]};
    for(const auto& op:ops){
        if(op.code==31&&regType(code[op.at+2])==1){const unsigned reg=regIndex(code[op.at+2]);if(!used[reg])continue;
            if(used[reg]&~declared[reg])return false;result.insert(result.end(),code+op.at,code+op.at+3);
            // Keep the native depth interpolator's lanes. DXVK2 drops its W
            // value when both stages declare only W, producing zero depth.
            const unsigned mask=reg==unsigned(depth)?declared[reg]:used[reg];result.back()=(result.back()&~0xf0000u)|(mask<<16);
        }else result.insert(result.end(),code+op.at,code+op.at+op.count+1);
    }
    result.push_back(65535);out.swap(result);return true;
}catch(const std::bad_alloc&){return false;}
inline bool vertex(const Word* code,size_t count,std::vector<Word>& out,bool shadow=false)try{
    std::vector<Op> ops;if(!parse(code,count,true,ops))return false;
    unsigned outputs[12]={},needed[12]={},temps[32]={},address=0,inputs[16]={},declaredInputs[16]={};
    bool position=false,normal=shadow,depth=false,uv=false,alpha=false;
    for(const auto& op:ops)if(op.code==31){const Word destination=code[op.at+2];const unsigned type=regType(destination),reg=regIndex(destination),mask=(destination>>16)&15;
        if(type==1){if(reg>=16||declaredInputs[reg])return false;declaredInputs[reg]=mask;}
        else if(type==6){if(reg>=12||outputs[reg])return false;const unsigned semantic=code[op.at+1]&0xfffff;
            unsigned live=mask;
            if((semantic&15)==5||(semantic&15)==10)live=0;
            if(!shadow&&semantic==0x50005){live=7;normal=true;}
            if(shadow&&semantic==5){live=mask;uv=(mask&3)==3;}
            if(shadow&&semantic==10){live=8;alpha=true;}
            if(semantic==0x70005){live=mask;depth=true;}
            if(semantic==0)position=true;
            if(live&~mask)return false;outputs[reg]=mask;needed[reg]=live;
        }else return false;
    }
    if(!position||!normal||!depth||(shadow&&(!uv||!alpha)))return false;
    const auto liveOutputs=std::array<unsigned,12>{needed[0],needed[1],needed[2],needed[3],needed[4],needed[5],needed[6],needed[7],needed[8],needed[9],needed[10],needed[11]};
    std::vector<unsigned> masks(ops.size());
    for(size_t index=ops.size();index--;){const auto& op=ops[index];if(!arithmetic(op.code))continue;
        const Word destination=code[op.at+1];const unsigned type=regType(destination),reg=regIndex(destination),written=(destination>>16)&15;
        unsigned& required=type==0?temps[reg]:type==3?address:needed[reg];const unsigned observed=required&written;if(!observed)continue;required&=~written;
        masks[index]=(op.code>=20&&op.code<=24)||op.code==16||op.code==17||op.code==36?written:observed;
        if(!sources(code,op,masks[index],[&](unsigned sourceType,unsigned source,unsigned mask){
            if(sourceType==0)temps[source]|=mask;else if(sourceType==1)inputs[source]|=mask;else if(sourceType==3)address|=mask;
            return true;
        }))return false;
    }
    for(unsigned need:temps)if(need)return false;for(unsigned need:needed)if(need)return false;if(address)return false;
    for(unsigned i=0;i<16;++i)if(inputs[i]&~declaredInputs[i])return false;
    std::vector<Word> result{code[0]};
    for(size_t index=0;index<ops.size();++index){const auto& op=ops[index];
        if(op.code==31){const Word destination=code[op.at+2];const unsigned type=regType(destination),reg=regIndex(destination),mask=type==1?inputs[reg]:(shadow&&(code[op.at+1]&0xfffff)==10?outputs[reg]:liveOutputs[reg]);if(!mask)continue;
            result.insert(result.end(),code+op.at,code+op.at+3);result.back()=(result.back()&~0xf0000u)|(mask<<16);
        }else if(arithmetic(op.code)){if(!masks[index])continue;const auto at=result.size();result.insert(result.end(),code+op.at,code+op.at+op.count+1);result[at+1]=(result[at+1]&~0xf0000u)|(masks[index]<<16);
        }else result.insert(result.end(),code+op.at,code+op.at+op.count+1);
    }
    result.push_back(65535);out.swap(result);return true;
}catch(const std::bad_alloc&){return false;}
// All shadow replay consumers read UV0, clip7 and diffuse alpha only. Keep the
// original COLOR0 declaration lanes for native/DXVK interpolation linkage.
inline bool shadowVertex(const Word* code,size_t count,std::vector<Word>& out){return vertex(code,count,out,true);}
}
