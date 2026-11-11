#pragma once
#include "hdr_ui_patch.h"
#include "aurora_compiled_shaders.h"
#include <array>
namespace NorthlightAuroraNative {
struct EmissionPatch {unsigned constant=0,originalSlots=0,addedSlots=0;};
// Retain the original material program verbatim apart from its final output
// destination. No texture reads, alpha test, VS animation or draw ownership is
// inferred here. The caller proves the native emitter and its CPU PS evaluator.
inline bool patchEmission(const std::uint32_t* original,std::size_t count,bool linear,
                          std::vector<std::uint32_t>& output,EmissionPatch& info){
    using namespace NorthlightHdrUi;
    output.clear();info={};
    if(!original||count<3)return false;
    const bool sm2=original[0]==0xffff0200u;
    const auto* transfer=linear?(sm2?kAuroraLinearPs2:kAuroraLinearPs3):(sm2?kAuroraLegacyPs2:kAuroraLegacyPs3);
    const auto length=linear?(sm2?sizeof(kAuroraLinearPs2)/4:sizeof(kAuroraLinearPs3)/4):(sm2?sizeof(kAuroraLegacyPs2)/4:sizeof(kAuroraLegacyPs3)/4);
    std::vector<Instruction> old,tail;unsigned oldCost=0,newCost=0;
    if(original[0]!=transfer[0]||!parse(original,count,old,oldCost)||!parse(transfer,length,tail,newCost))return false;
    const unsigned temps=sm2?12:32,constants=sm2?32:224;
    std::array<bool,32> usedTemps{};std::array<bool,224> usedConstants{};unsigned mask=0;
    for(const auto& instruction:old){
        const unsigned first=instruction.op==31?2:1,last=(instruction.op==47||instruction.op==48||instruction.op==81)?1:unsigned(instruction.count)-1;
        for(unsigned j=first;j<=last;++j){const auto token=original[instruction.at+j];const auto t=type(token),r=index(token);
            if(t==0){if(r>=temps)return false;usedTemps[r]=true;}
            if(t==2){if(r>=constants)return false;usedConstants[r]=true;}
            if(t==8){if(j!=1||r)return false;mask|=(token>>16)&15;}
            if(t==9)return false;
        }
    }
    if(linear)++newCost; // Explicit native alpha saturation below (also SM2).
    if(mask!=15||oldCost+newCost>(sm2?64u:512u))return false;
    int capture=-1;for(unsigned i=temps;i-->0;)if(!usedTemps[i]){capture=int(i);break;}if(capture<0)return false;
    std::array<int,224> constantMap;constantMap.fill(-1);std::array<int,32> tempMap;tempMap.fill(-1);
    int control=-1;for(unsigned i=constants-1;i>0;--i)if(!usedConstants[i]&&!usedConstants[i-1]){control=int(i-1);break;}
    if(control<0)return false;
    constantMap[0]=control;constantMap[1]=control+1;usedConstants[control]=usedConstants[control+1]=true;
    const auto constant=[&](unsigned r)->int{
        if(r>=constants)return -1;if(constantMap[r]>=0)return constantMap[r];
        for(unsigned i=constants;i-->0;)if(!usedConstants[i]){usedConstants[i]=true;return constantMap[r]=int(i);}return -1;
    };
    unsigned nextTemp=0;
    const auto remap=[&](Word token,Word& result)->bool{
        const unsigned t=type(token),r=index(token);int mapped=-1;
        if(t==1||(linear&&t==3)){if(r)return false;result=reg(token,0,unsigned(capture));return true;}
        if(t==2){mapped=constant(r);if(mapped<0)return false;result=reg(token,2,unsigned(mapped));return true;}
        if(t==0){if(r>=32)return false;if(tempMap[r]<0){while(nextTemp<temps&&int(nextTemp)==capture)++nextTemp;if(nextTemp>=temps)return false;tempMap[r]=int(nextTemp++);}result=reg(token,0,unsigned(tempMap[r]));return true;}
        if(t==8&&r==0){result=token;return true;}return false;
    };
    std::vector<Word> definitions,body;
    for(const auto& instruction:tail){
        if(instruction.op==31)continue;if(instruction.op==47||instruction.op==48)return false;
        if(instruction.op==81&&index(transfer[instruction.at+1])<2)return false; // c0/c1 are runtime controls.
        auto& destination=instruction.op==81?definitions:body;destination.push_back(transfer[instruction.at]);
        for(unsigned j=1;j<instruction.count;++j){const Word token=transfer[instruction.at+j];Word mapped=0;
            if(instruction.op==81&&j>1){destination.push_back(token);continue;}
            if(!remap(token,mapped))return false;destination.push_back(mapped);}
    }
    std::vector<Word> result{original[0]};result.insert(result.end(),definitions.begin(),definitions.end());
    for(const auto& instruction:old){result.push_back(original[instruction.at]);
        for(unsigned j=1;j<instruction.count;++j){auto token=original[instruction.at+j];
            if(j==1&&type(token)==8)token=reg(linear?token:token|0x00100000u,0,unsigned(capture));result.push_back(token);}}
    if(linear)result.insert(result.end(),{0x02000001u,reg(0x80180000u,0,unsigned(capture)),reg(0x80ff0000u,0,unsigned(capture))});
    result.insert(result.end(),body.begin(),body.end());result.push_back(65535);
    output=std::move(result);info={unsigned(control),oldCost,newCost};return true;
}
}
