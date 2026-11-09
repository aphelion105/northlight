#pragma once
#include "model_normal_patch.h"
#include "patch_terrain_shadow_coverage.h"

// The native pair has already been selected by the material/light factories.
// Preserve its outputs and depth. One extra varying carries normal.xyz/clip.w.
namespace NorthlightModelNormalMrt {
using namespace NorthlightWaterShaderPatch;
using NorthlightTerrainCoverage::Program;
inline bool terminalFlow(const Program& p){
    for(const auto& op:p.ops)if(op.code==25||op.code==26||op.code==28||op.code==30)return false;
    return true;
}
inline unsigned outputMask(const Word* code,const Program& p){
    unsigned mask=0;for(const auto& op:p.ops){if(op.code==31||op.code==81||op.code==48||op.code==47||!op.size||op.code==65)continue;
        const Word d=code[op.at+1];if(regType(d)==8&&regIndex(d)<4)mask|=1u<<regIndex(d);}
    return mask;
}
inline bool pixel(const Word* code,size_t count,unsigned slot,bool receiver,unsigned semantic,std::vector<Word>& out){
    out.clear();Program p;if(slot<1||slot>3||!NorthlightTerrainCoverage::analyse(code,count,p)||p.vertex||!terminalFlow(p)||
        (outputMask(code,p)&(1u<<slot))||p.slots+(receiver?7:1)>512)return false;
    unsigned input=0;while(input<10&&p.inputs[input])++input;
    if(receiver){if(input==10||semantic>7)return false;
        for(const auto& op:p.ops){if(op.code==31&&(code[op.at+1]&15)==5&&((code[op.at+1]>>16)&15)==semantic)return false;
            if(op.size&&op.code!=31&&op.code!=81&&op.code!=48&&op.code!=47&&regType(code[op.at+1])==9)return false;}}
    unsigned constant=0;while(constant<224&&p.constants[constant])++constant;if(constant==224)return false;
    out.assign(code,code+count-1);
    if(receiver)out.insert(out.begin()+p.declEnd,{0x0200001fu,0x80000005u|(semantic<<16),dst(1,input)});
    // Zero RGB is invalid regardless of W; alpha one overwrites it under blending.
    out.insert(out.begin()+1,{0x05000051u,dst(2,constant),receiver?0x42400000u:0u,0,0,receiver?0u:0x3f800000u});
    // All native outputs are complete here. Reuse dead native temporaries,
    // rather than increasing register pressure in the final material shader.
    if(receiver)out.insert(out.end(),{0x02000024u,dst(0,0,7),src(1,input),
        0x02000001u,dst(0,0,8),src(1,input,0xff),
        0x03000002u,dst(0,1,1),src(2,constant,0),src(1,input,0xff)|0x01000000u,
        0x04000058u,dst(8,slot),src(0,1,0),src(0,0),src(2,constant,0x55)});
    else out.insert(out.end(),{0x02000001u,dst(8,slot),src(2,constant)});
    out.push_back(65535);return true;
}
inline bool vertex(const Word* code,size_t count,const Word* pixel,size_t pixelCount,std::vector<Word>& out,unsigned& semantic){
    out.clear();Program v,p;if(!NorthlightTerrainCoverage::analyse(code,count,v,true)||!v.vertex||!terminalFlow(v)||
        !NorthlightTerrainCoverage::analyse(pixel,pixelCount,p)||p.vertex)return false;
    bool used[8]={};unsigned position=12,output=0;
    for(const auto* pair:{&v,&p})for(const auto& op:pair->ops)if(op.code==31){const Word* words=pair==&v?code:pixel;
        const unsigned kind=words[op.at+1]&15,index=(words[op.at+1]>>16)&15;
        if(kind==5&&index<8&&regType(words[op.at+2])==(pair==&v?6u:1u))used[index]=true;
        if(pair==&v&&kind==0&&regType(words[op.at+2])==6)position=regIndex(words[op.at+2]);}
    semantic=0;while(semantic<8&&used[semantic])++semantic;
    while(output<12&&v.outputs[output])++output;if(semantic==8||output==12||position==12)return false;
    bool normalized[32]={};size_t normalAt=0;unsigned normal=32,writes=0;
    for(const auto& op:v.ops){
        if(op.code==31||op.code==81||op.code==48||op.code==47)continue;
        if(op.code==27||op.code==29||(op.code>=38&&op.code<=45)||op.code==96)return false;
        if(op.code==8&&op.size==3&&!normalAt)for(unsigned i=2;i<=3;++i){const auto light=code[op.at+i],value=code[op.at+5-i];
            if(regType(light)==2&&regIndex(light)==12&&((light>>24)&15)==1&&((light>>16)&255)==0xe4&&
               regType(value)==0&&regIndex(value)<32&&!(value&0x0f002000u)&&((value>>16)&255)==0xe4&&normalized[regIndex(value)]){
                normalAt=op.at;normal=regIndex(value);}}
        if(!op.size)continue;const Word d=code[op.at+1];const unsigned type=regType(d),r=regIndex(d),mask=(d>>16)&15;
        if(type==6&&r==position&&(mask&8)){
            if(op.code!=1&&op.code!=2&&op.code!=4&&op.code!=5&&op.code!=9)return false;++writes;}
        if(type==0&&r<32&&(mask&7))normalized[r]=op.code==36&&(mask&7)==7;
    }
    if(!normalAt||!writes||v.slots+writes+1>512)return false;
    out.assign(code,code+v.declEnd);out.insert(out.end(),{0x0200001fu,0x80000005u|(semantic<<16),dst(6,output)});
    size_t cursor=v.declEnd;
    for(const auto& op:v.ops){if(op.at<v.declEnd)continue;
        out.insert(out.end(),code+cursor,code+op.at);cursor=op.at+op.size+1;
        if(op.at==normalAt)out.insert(out.end(),{0x02000001u,dst(6,output,7),src(0,normal)});
        out.insert(out.end(),code+op.at,code+cursor);
        if(op.size&&op.code!=31&&op.code!=81&&op.code!=48&&op.code!=47){const auto d=code[op.at+1];
            if(regType(d)==6&&regIndex(d)==position&&(d&0x80000u)){
                out.push_back(code[op.at]);out.push_back((replaceReg(d,6,output)&~0xf0000u)|0x80000u);
                out.insert(out.end(),code+op.at+2,code+cursor);}}
    }
    out.insert(out.end(),code+cursor,code+count);return true;
}
}
