#pragma once
#include "equipment_light_proof.h"
#include <vector>
#include <array>
#include <algorithm>
#include <cstring>

// Runtime SM2/3 transformation; no original game shader is embedded or stored.
// Formula reference: St0ny's lights8/terrain8 (WoW-lights-patch-shader).
namespace NorthlightNativeMoreLightShader {
using Word=std::uint32_t;
enum class Kind {None,Model,Terrain};
inline unsigned type(Word p){return ((p>>28)&7)|((p>>8)&24);}
inline unsigned index(Word p){return p&2047;}
inline Word dst(unsigned t,unsigned n,unsigned mask=15,bool sat=false){return 0x80000000u|((t&7)<<28)|((t&24)<<8)|n|(mask<<16)|(unsigned(sat)<<20);}
inline Word src(unsigned t,unsigned n,unsigned swizzle=0xe4,bool neg=false){return 0x80000000u|((t&7)<<28)|((t&24)<<8)|n|(swizzle<<16)|(unsigned(neg)<<24);}
inline Word r(unsigned n,unsigned swizzle=0xe4,bool neg=false){return src(0,n,swizzle,neg);}
inline Word c(unsigned n,unsigned swizzle=0xe4){return src(2,n,swizzle);}
struct Instruction {Word token=0;std::vector<Word> p;unsigned op()const{return token&65535;}};
inline bool reads(const Instruction& i,unsigned constant){
    if(i.op()==31||i.op()==81||i.op()==65534)return false;
    for(unsigned k=1;k<i.p.size();++k)if(type(i.p[k])==2&&index(i.p[k])==constant&&!(i.p[k]&0x2000))return true;return false;
}
inline void emit(std::vector<Word>& out,unsigned op,std::initializer_list<Word> p){out.push_back(op|(unsigned(p.size())<<24));out.insert(out.end(),p.begin(),p.end());}
inline bool transform(const Word* code,size_t count,bool terrain,std::vector<Word>& out,Kind& kind,bool& already){
    kind=Kind::None;already=false;
    if(!code||count<2||count>4096||(code[0]!=0xfffe0200u&&code[0]!=0xfffe0300u)||code[count-1]!=0xffff)return false;
    std::vector<Instruction> instructions;std::array<bool,256> constants{};std::array<bool,32> used{};
    for(size_t at=1;at<count-1;){const unsigned op=code[at]&65535,n=op==65534?(code[at]>>16)&32767:(code[at]>>24)&15;
        if(n>=count-at)return false;Instruction i;i.token=code[at];i.p.assign(code+at+1,code+at+n+1);
        if(op!=65534&&op!=81&&op!=31)for(Word p:i.p){if(type(p)==0&&index(p)<32)used[index(p)]=true;}
        if(op!=65534&&op!=81&&op!=31)for(unsigned k=1;k<i.p.size();++k){const Word p=i.p[k];
            if(type(p)==2&&index(p)<256&&!(p&0x2000))constants[index(p)]=true;}
        instructions.push_back(std::move(i));at+=n+1;}
    if(terrain){for(unsigned reg:{29u,32u,35u})if(!constants[reg])return false;}
    else for(unsigned reg=17;reg<=27;++reg)if(!constants[reg])return false;
    bool rgbOnly=false;const unsigned lights=NorthlightEquipmentLightProof::analyze(code,count,&rgbOnly,terrain);
    if(!rgbOnly||lights!=(terrain?3u:4u))return false;
    kind=terrain?Kind::Terrain:Kind::Model;
    if(constants[236]){for(unsigned reg=236;reg<=(terrain?250u:246u);++reg)if(!constants[reg])return false;
        const unsigned epsilon=terrain?251:247,component=terrain?0:3;bool found=false;
        for(const auto& i:instructions)if(i.op()==81&&i.p.size()==5&&type(i.p[0])==2&&index(i.p[0])==epsilon&&i.p[1+component]==0x358637bdu)found=true;
        if(!found)return false;already=true;out.assign(code,code+count);return true;}
    for(unsigned reg=236;reg<256;++reg)if(constants[reg])return false;
    const auto unique=[&](auto pred){int result=-1;for(unsigned i=0;i<instructions.size();++i)if(pred(instructions[i])){if(result>=0)return -1;result=int(i);}return result;};
    int savePosition=unique([&](const Instruction& i){if(i.op()!=2||!reads(i,terrain?3:21))return false;
        if(terrain)return !i.p.empty()&&type(i.p[0])==0;
        for(unsigned k=1;k<i.p.size();++k)if(type(i.p[k])==0&&((i.p[k]>>24)&15)==1)return true;return false;});
    const int saveNormal=unique([&](const Instruction& i){return i.op()==8&&reads(i,terrain?24:12);});
    int end=-1;
    if(terrain){for(unsigned k=0;k<instructions.size();++k)if(reads(instructions[k],36))end=int(k);}
    else end=unique([&](const Instruction& i){return i.op()==4&&reads(i,20);});
    if(savePosition<0||saveNormal<0||end<0)return false;
    Word terrainPosition=0;
    if(terrain){end+=2;if(unsigned(end)>=instructions.size()||instructions[end-1].op()!=6||instructions[end].op()!=4)return false;
        // The position after c3 must remain live until the first native light.
        int first=-1;for(unsigned k=0;k<instructions.size()&&first<0;++k)for(unsigned reg=28;reg<=36;++reg)if(reads(instructions[k],reg)){first=int(k);break;}
        if(first<=savePosition||first>=end)return false;
        const unsigned p=index(instructions[savePosition].p[0]);
        terrainPosition=r(p);
        for(int k=savePosition+1;k<first;++k){const auto& i=instructions[k];if(i.op()!=31&&i.op()!=81&&i.op()!=65534&&!i.p.empty()&&type(i.p[0])==0&&index(i.p[0])==p&&((i.p[0]>>16)&7))return false;}
        savePosition=first;
    }
    if(savePosition>=end||saveNormal>=end)return false;
    const auto& last=instructions[end];if(last.p.empty()||type(last.p[0])!=0||((last.p[0]>>16)&15)!=7||((last.p[0]>>20)&14))return false;
    const unsigned acc=index(last.p[0]);const bool saturated=(last.p[0]&(1u<<20))!=0;
    if(terrain&&saturated)return false;
    const unsigned maxTemps=code[0]==0xfffe0200u?12:32;unsigned free[2]{},freeCount=0;
    for(unsigned n=0;n<maxTemps&&freeCount<2;++n)if(!used[n])free[freeCount++]=n;if(freeCount!=2)return false;
    std::array<bool,32> live{};live[acc]=true;
    // Whole-register liveness is conservative; insufficient room retains native code.
    for(unsigned k=end+1;k<instructions.size();++k){const auto& i=instructions[k];if(i.op()==31||i.op()==81||i.op()==65534)continue;
        for(unsigned n=1;n<i.p.size();++n)if(type(i.p[n])==0&&index(i.p[n])<32)live[index(i.p[n])]=true;}
    unsigned work[4]{},workCount=0;for(unsigned n=0;n<maxTemps&&workCount<4;++n)if(!live[n]&&n!=free[0]&&n!=free[1])work[workCount++]=n;if(workCount!=4)return false;
    Word position=0,normal=0;for(unsigned k=1;k<instructions[saveNormal].p.size();++k)if(type(instructions[saveNormal].p[k])==0){normal=instructions[saveNormal].p[k];break;}
    if(terrain)position=terrainPosition;
    else for(unsigned k=1;k<instructions[savePosition].p.size();++k)if(type(instructions[savePosition].p[k])==0){position=instructions[savePosition].p[k];break;}
    if(!position||!normal)return false;position&=~(15u<<24);normal&=~(15u<<24);if(terrain)normal=(normal&~(255u<<16))|(0xe4u<<16);
    unsigned zeroReg=0,zeroSwizzle=0,oneReg=0,oneSwizzle=0;bool zero=false,one=false;
    for(const auto& i:instructions)if(i.op()==81&&i.p.size()==5){for(unsigned k=0;k<4;++k){if(i.p[k+1]==0&&!zero){zero=true;zeroReg=index(i.p[0]);zeroSwizzle=0x55*k;}if(i.p[k+1]==0x3f800000u&&!one){one=true;oneReg=index(i.p[0]);oneSwizzle=0x55*k;}}}
    if(!zero||(!one&&terrain&&maxTemps==12))return false;
    const unsigned p=free[0],n=free[1],L=work[0],D=work[1],K=work[2],T=work[3];std::vector<Word> block;
    if(!terrain){for(unsigned j=0;j<4;++j){emit(block,2,{dst(0,L,7),r(p,0xe4,true),c(240+j)});emit(block,8,{dst(0,D,1u<<j),r(L),r(L)});emit(block,8,{dst(0,K,1u<<j),r(L),r(n)});}
        for(unsigned j=0;j<4;++j)emit(block,7,{dst(0,T,1u<<j),r(D,0x55*j)});
        emit(block,5,{dst(0,K),r(K),r(T)});emit(block,11,{dst(0,K),r(K),c(zeroReg,zeroSwizzle)});emit(block,5,{dst(0,T),r(D),r(T)});
        emit(block,5,{dst(0,T),r(T),c(245)});emit(block,4,{dst(0,T),r(D),c(246),r(T)});emit(block,2,{dst(0,T),r(T),c(244)});emit(block,11,{dst(0,T),r(T),c(247,0xff)});
        for(unsigned j=0;j<4;++j)emit(block,6,{dst(0,L,1u<<j),r(T,0x55*j)});
        emit(block,5,{dst(0,K),r(K),r(L)});for(unsigned j=0;j<4;++j)emit(block,4,{dst(0,acc,7,saturated&&j==3),c(236+j),r(K,0x55*j),r(acc)});
    }else{emit(block,1,{dst(0,D,7),c(0)});emit(block,1,{dst(0,K,7),c(1)});emit(block,1,{dst(0,T,7),c(2)});
        for(unsigned j=0;j<5;++j){const unsigned b=236+3*j;
            emit(block,5,{dst(0,L,7),r(K),c(b,0x55)});emit(block,4,{dst(0,L,7),c(b,0),r(D),r(L)});emit(block,4,{dst(0,L,7),c(b,0xaa),r(T),r(L)});emit(block,2,{dst(0,L,7),r(p,0xe4,true),r(L)});
            emit(block,8,{dst(0,L,8),r(L),r(L)});emit(block,7,{dst(0,L,8),r(L,0xff)});emit(block,6,{dst(0,p,8),r(L,0xff)});emit(block,5,{dst(0,L,7),r(L),r(L,0xff)});
            emit(block,8,{dst(0,L,8,maxTemps==32),r(L),r(n)});if(maxTemps==12){emit(block,11,{dst(0,L,8),r(L,0xff),c(zeroReg,zeroSwizzle)});emit(block,10,{dst(0,L,8),r(L,0xff),c(oneReg,oneSwizzle)});}
            emit(block,5,{dst(0,L,7),r(L,0xff),c(b+1)});emit(block,4,{dst(0,L,8),c(b+2,0x55),r(p,0xff),c(b+2,0)});emit(block,5,{dst(0,p,8),r(p,0xff),r(p,0xff)});
            emit(block,4,{dst(0,L,8),r(p,0xff),c(b+2,0xaa),r(L,0xff)});emit(block,11,{dst(0,L,8),r(L,0xff),c(251,0)});emit(block,6,{dst(0,L,8),r(L,0xff)});emit(block,4,{dst(0,acc,7),r(L),r(L,0xff),r(acc)});
        }}
    std::vector<Word> result{code[0]};emit(result,81,{dst(2,terrain?251:247),terrain?0x358637bdu:0x3f800000u,0,terrain?0u:0x3f800000u,terrain?0u:0x358637bdu});
    unsigned slots=0;
    for(unsigned k=0;k<instructions.size();++k){auto i=instructions[k];if(int(k)==savePosition)emit(result,1,{dst(0,p,7),position});if(int(k)==saveNormal)emit(result,1,{dst(0,n,7),normal});
        if(!terrain&&int(k)==end)i.p[0]&=~(1u<<20);result.push_back(i.token);result.insert(result.end(),i.p.begin(),i.p.end());if(int(k)==end)result.insert(result.end(),block.begin(),block.end());}
    result.push_back(0xffff);
    for(size_t at=1;at<result.size()-1;){const unsigned op=result[at]&65535,n=op==65534?(result[at]>>16)&32767:(result[at]>>24)&15;
        if(op!=31&&op!=81&&op!=65534)slots+=op==32||op==36?3:op==20||op==22?4:op==21||op==23?3:op==24?2:op==37&&maxTemps==12?8:1;at+=n+1;}
    if(result.size()>4096||slots>(maxTemps==12?256u:512u))return false;
    out=std::move(result);return true;
}
} // namespace NorthlightNativeMoreLightShader
