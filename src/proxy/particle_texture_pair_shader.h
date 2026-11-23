#pragma once
#include <algorithm>
#include <d3d9.h>
#include "hdr_ui_patch.h"
#include "particle_atlas_shader.h"

namespace NorthlightParticleTexturePair {
using NorthlightHdrUi::Word;
using NorthlightHdrUi::reg;
using NorthlightHdrUi::type;
using NorthlightHdrUi::index;
struct Registers {
    unsigned input=0,output=0,selector=0,semantic=0,sampler=0;
};
inline unsigned freeRegister(std::uint32_t used,unsigned count){
    unsigned i=0;
    while(i<count&&(used&(1u<<i)))++i;
    return i;
}
inline constexpr unsigned MaxConstantRows=32;

// Select the original constant rows for the second emitter and their saved
// values for the first. Rewrite source registers only, preserving swizzles
// and modifiers. Relative and implicit matrix reads stay on the serial path.
inline bool patchVertexConstants(const std::vector<Word>& code,unsigned selector,
                                 const unsigned* rows,unsigned count,std::vector<Word>& output,unsigned& bank){
    using namespace NorthlightParticleAtlas::Proof;
    if(code.empty()||code[0]!=0xfffe0300u||selector>=16||!count||count>MaxConstantRows)return false;
    std::array<bool,256> constants{};
    unsigned temps=0;
    const Word* body=nullptr;
    if(!visit(code.data(),unsigned(code.size()),[&](Word token,const Word* args,unsigned n){
        const unsigned op=token&65535;
        if(noDestination(op)||(op>=20&&op<=24))return false;
        const bool declaration=op==31||op==47||op==48||op==81;
        if(declaration&&body)return false;
        if(!body&&!declaration)body=args-1;
        const unsigned first=op==31?1:0,last=op==47||op==48||op==81?1:n;
        if(first>last)return false;
        for(unsigned i=first;i<last;++i){
            const auto t=type(args[i]),r=index(args[i]);
            if(!(args[i]&0x80000000u)||(args[i]&0x2000u)||(t>=11&&t<=13))return false;
            if(t==0){if(r>=32)return false;temps|=1u<<r;}
            if(t==2){if(r>=constants.size())return false;constants[r]=true;}
        }
        return true;
    })||!body)return false;
    std::array<unsigned,MaxConstantRows> registers{};
    for(unsigned i=0;i<count;++i){
        if(rows[i]>=constants.size()||!constants[rows[i]])return false;
        registers[i]=freeRegister(temps,32);
        if(registers[i]==32)return false;
        temps|=1u<<registers[i];
    }
    bool found=false;
    for(unsigned end=unsigned(constants.size());end>count;--end){
        const auto first=end-count-1;
        if(std::none_of(constants.begin()+first,constants.begin()+end,[](bool used){return used;})){
            bank=first;found=true;break;
        }
    }
    if(!found)return false;
    output.assign(code.data(),body);
    output.insert(output.end(),{0x05000051u,reg(0x800f0000u,2,bank+count),0,0,0,0,
        0x02000029u|(D3DSPC_LT<<16),reg(0x80000000u,1,selector),reg(0x80000000u,2,bank+count)});
    for(unsigned i=0;i<count;++i)
        output.insert(output.end(),{0x02000001u,reg(0x800f0000u,0,registers[i]),reg(0x80e40000u,2,bank+i)});
    output.push_back(0x0000002au);
    for(unsigned i=0;i<count;++i)
        output.insert(output.end(),{0x02000001u,reg(0x800f0000u,0,registers[i]),reg(0x80e40000u,2,rows[i])});
    output.push_back(0x0000002bu);
    const auto rewritten=output.size();
    output.insert(output.end(),body,code.data()+code.size());
    return visit(code.data(),unsigned(code.size()),[&](Word,const Word* args,unsigned n){
        if(args<=body)return true;
        for(unsigned i=1;i<n;++i)if(type(args[i])==2)
            for(unsigned j=0;j<count;++j)if(index(args[i])==rows[j])
                output[rewritten+std::size_t(args+i-body)]=reg(args[i],0,registers[j]);
        return true;
    });
}

inline bool patch(const std::vector<Word>& vs,const std::vector<Word>& ps,unsigned declarationTexcoords,
                  std::vector<Word>& vertex,std::vector<Word>& pixel,Registers& route){
    using namespace NorthlightParticleAtlas::Proof;
    if(vs.empty()||ps.empty()||vs[0]!=0xfffe0300u||ps[0]!=0xffff0300u||
       pixelUv(ps.data(),unsigned(ps.size()),0)<0)return false;
    unsigned inputs=0,outputs=0,semantics=declarationTexcoords;
    if(!visit(vs.data(),unsigned(vs.size()),[&](Word token,const Word* args,unsigned n){
        const unsigned op=token&65535;
        if(noDestination(op))return false; // The added output must run for every vertex.
        const unsigned first=op==31?1:0,last=op==81?1:n;
        if(op==31){
            if(n!=2)return false;
            if((args[0]&31)==D3DDECLUSAGE_TEXCOORD)semantics|=1u<<((args[0]>>16)&15);
        }
        for(unsigned i=first;i<last;++i){
            const auto t=type(args[i]),r=index(args[i]);
            if(t==1){if(r>=16||(args[i]&0x2000))return false;inputs|=1u<<r;}
            if(t==6){if(r>=12)return false;outputs|=1u<<r;}
        }
        return true;
    }))return false;
    std::vector<NorthlightHdrUi::Instruction> instructions;
    unsigned cost=0,psInputs=0,samplers=0,temps=0;
    std::array<bool,224> constants{};
    std::size_t sample=0;
    if(!NorthlightHdrUi::parse(ps.data(),ps.size(),instructions,cost)||cost>496)return false;
    for(const auto& ins:instructions){
        if(noDestination(ins.op)||ins.op==D3DSIO_TEXKILL)return false;
        const Word* args=ps.data()+ins.at+1;
        const unsigned first=ins.op==31?1:0,last=(ins.op==81||ins.op==47||ins.op==48)?1:unsigned(ins.count-1);
        if(ins.op==31&&type(args[1])==1&&(args[0]&31)==D3DDECLUSAGE_TEXCOORD)semantics|=1u<<((args[0]>>16)&15);
        for(unsigned i=first;i<last;++i){
            const auto t=type(args[i]),r=index(args[i]);
            if(t==0){if(r>=32)return false;temps|=1u<<r;}
            if(t==1){if(r>=10)return false;psInputs|=1u<<r;}
            if(t==2){if(r>=constants.size())return false;constants[r]=true;}
            if(t==10){if(r>=16)return false;samplers|=1u<<r;}
        }
        if(ins.op==66&&args[2]==0xa0e40800u){
            if(sample||type(args[0])!=0||(args[0]&0x0fff0000u)!=0x000f0000u)return false;
            sample=ins.at;
        }
    }
    route={freeRegister(inputs,16),freeRegister(outputs,12),freeRegister(psInputs,10),
           freeRegister(semantics,8),freeRegister(samplers,16)};
    const unsigned dx=freeRegister(temps,32);
    if(!sample||route.input==16||route.output==12||route.selector==10||route.semantic==8||
       !route.sampler||route.sampler==16||dx==32)return false;
    const unsigned dy=freeRegister(temps|(1u<<dx),32);
    const auto local=unsigned(std::find(constants.begin(),constants.end(),false)-constants.begin());
    if(dy==32||local==constants.size())return false;
    const Word semantic=0x80000005u|(route.semantic<<16);
    vertex.assign(vs.begin(),vs.end()-1);
    vertex.insert(vertex.begin()+1,{0x0200001fu,semantic,reg(0x80010000u,1,route.input),
                                  0x0200001fu,semantic,reg(0x80010000u,6,route.output)});
    vertex.insert(vertex.end(),{0x02000001u,reg(0x80010000u,6,route.output),
                               reg(0x80000000u,1,route.input),0x0000ffffu});
    const Word target=ps[sample+1],uv=ps[sample+2],stage0=ps[sample+3];
    const Word stage1=reg(stage0,10,route.sampler);
    const Word gradX=reg(0x80440000u,0,dx),gradY=reg(0x80440000u,0,dy);
    pixel.assign(ps.begin(),ps.begin()+sample);
    pixel.insert(pixel.begin()+1,{0x05000051u,reg(0x800f0000u,2,local),0,0,0,0,
        0x0200001fu,semantic,reg(0x80010000u,1,route.selector),
        0x0200001fu,0x90000000u,reg(0x800f0000u,10,route.sampler)});
    // Calculate gradients before divergent flow so each primitive keeps the
    // same anisotropic/mip sampling on its original texture.
    pixel.insert(pixel.end(),{
        0x0200005bu,reg(0x80030000u,0,dx),uv,0x0200005cu,reg(0x80030000u,0,dy),uv,
        0x02000029u|(D3DSPC_LT<<16),reg(0x80000000u,1,route.selector),reg(0x80000000u,2,local),
        0x0500005du,target,uv,stage1,gradX,gradY,0x0000002au,
        0x0500005du,target,uv,stage0,gradX,gradY,0x0000002bu});
    pixel.insert(pixel.end(),ps.begin()+sample+4,ps.end());
    return true;
}
}
