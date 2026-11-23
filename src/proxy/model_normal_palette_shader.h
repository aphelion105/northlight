#pragma once
#include <d3d9.h>
#include "model_normal_slice.h"
#include "patch_terrain_shadow_coverage.h"

namespace NorthlightModelNormalPaletteShader {
constexpr unsigned Base=31, RowsPerBone=3, MaxBones=(256-Base)/RowsPerBone;
constexpr unsigned MaxInstanceConstants=21;

struct ConstantRun {unsigned first,count;};
struct Program {
    unsigned semantic=0, limit=256, sourceLimit=256;
    std::vector<std::uint32_t> words;
    std::vector<ConstantRun> fixedReads;
    std::vector<ConstantRun> instanceReads;
    unsigned instanceRows=0, biasConstant=256;
};
enum class Bias {Folded, Separate};

struct MaterialVaryings {
    unsigned texcoords=0, colors=0;
    bool fog=false;
    bool reads(unsigned semantic)const{
        const unsigned usage=semantic&15, index=semantic>>16;
        if(usage==D3DDECLUSAGE_TEXCOORD)return texcoords&(1u<<index);
        if(usage==D3DDECLUSAGE_COLOR)return colors&(1u<<index);
        // Material replay disables fixed-function fog. Preserve position and
        // every other output that can affect rasterization.
        return usage!=D3DDECLUSAGE_FOG||fog;
    }
};

inline bool materialVaryings(const std::uint32_t* code,size_t count,MaterialVaryings& output){
    using namespace NorthlightModelNormalSlice;
    NorthlightTerrainCoverage::Program program;
    if(!NorthlightTerrainCoverage::analyse(code,count,program)||program.vertex)return false;
    std::array<unsigned,10> semantics;
    semantics.fill(~0u);
    unsigned read=0;
    for(const auto& op:program.ops){
        if(op.code==31){
            const Word target=code[op.at+2];
            if(regType(target)!=1)continue;
            const unsigned reg=regIndex(target);
            if(semantics[reg]!=~0u)return false;
            semantics[reg]=code[op.at+1]&0xfffffu;
        }else if(op.code!=81&&op.code!=48&&op.code!=47){
            for(unsigned i=1;i<=op.size;++i){
                const Word token=code[op.at+i];
                if((token&0x80000000u)&&regType(token)==1)read|=1u<<regIndex(token);
            }
        }
    }
    MaterialVaryings result;
    for(unsigned i=0;i<semantics.size();++i)if(read&(1u<<i)){
        if(semantics[i]==~0u)return false;
        const unsigned usage=semantics[i]&15, index=semantics[i]>>16;
        if(usage==D3DDECLUSAGE_TEXCOORD)result.texcoords|=1u<<index;
        if(usage==D3DDECLUSAGE_COLOR)result.colors|=1u<<index;
        if(usage==D3DDECLUSAGE_FOG)result.fog=true;
    }
    output=result;
    return true;
}

inline void materialFixedReads(const std::uint32_t* code,const std::vector<NorthlightModelNormalSlice::Op>& ops,
                               const MaterialVaryings& varyings,std::array<bool,256>& fixedReads){
    using namespace NorthlightModelNormalSlice;
    std::array<unsigned,12> outputs;
    outputs.fill(15);
    unsigned declarations=0;
    for(const auto& op:ops)if(op.code==31){
        const Word target=code[op.at+2];
        if(regType(target)!=6)continue;
        const unsigned index=regIndex(target);
        if(index>=outputs.size()||(declarations&(1u<<index)))return;
        declarations|=1u<<index;
        if(!varyings.reads(code[op.at+1]&0xfffffu))outputs[index]=0;
    }
    std::array<unsigned,32> temps{};
    unsigned address=0;
    std::array<bool,256> live{};
    for(auto it=ops.rbegin();it!=ops.rend();++it){
        if(!arithmetic(it->code))continue;
        const Word target=code[it->at+1];
        const unsigned type=regType(target), reg=regIndex(target), written=(target>>16)&15;
        unsigned& need=type==0?temps[reg]:type==3?address:outputs[reg];
        if(!(need&written))continue;
        need&=~written;
        // Keep all source components of a contributing instruction. In
        // particular, coupled arithmetic can read a lane it does not write.
        if(!sources(code,*it,15,[&](unsigned kind,unsigned index,unsigned){
            if(kind==0)temps[index]=15;
            if(kind==3)address=15;
            if(kind==2)live[index]=fixedReads[index];
            return true;
        }))return;
    }
    fixedReads=live;
}

// Relocate only a straight-line VS3 palette with an exact integer address
// calculation. The caller must prove that BLENDINDICES0 supplies UBYTE4.
inline bool build(const std::uint32_t* code,size_t count,Program& output,Bias bias=Bias::Folded,
                  const MaterialVaryings* varyings=nullptr,bool gameConstants=false)try{
    using namespace NorthlightModelNormalSlice;
    std::vector<Op> ops;
    if(!parse(code,count,true,ops))return false;

    Program result;
    std::array<const Word*,256> literals{};
    std::array<bool,256> fixedReads{};
    unsigned inputs=0,texcoords=0,blendIndices=16;
    std::uint32_t usedTemps=0;
    for(const auto& op:ops){
        if(op.code==81){
            const unsigned reg=regIndex(code[op.at+1]);
            if(literals[reg])return false;
            literals[reg]=code+op.at+2;
            if(reg>=Base)result.limit=std::min(result.limit,reg);
        }else if(op.code==31&&regType(code[op.at+2])==1){
            const unsigned reg=regIndex(code[op.at+2]);
            if(reg>=16||(inputs&(1u<<reg)))return false;
            inputs|=1u<<reg;
            const unsigned semantic=code[op.at+1]&0xfffffu;
            if((semantic&15)==5)texcoords|=1u<<(semantic>>16);
            if(semantic==2){
                if(blendIndices<16)return false;
                blendIndices=reg;
            }
        }
    }
    const unsigned freeInputs=(~inputs)&0xffffu, freeTexcoords=(~texcoords)&0xffu;
    if(blendIndices==16||!freeInputs||!freeTexcoords)return false;
    const unsigned instanceInput=__builtin_ctz(freeInputs);
    result.semantic=__builtin_ctz(freeTexcoords);

    size_t multiply=0;
    unsigned addressLanes=0, temporary=0, modifiedLanes=0;
    bool hasPaletteReads=false;
    for(size_t i=0;i<ops.size();++i){
        const auto& op=ops[i];
        if(!arithmetic(op.code))continue;
        // The last matrix row can be implicit in bytecode. Keep that case
        // outside this proof, as well as reads of an unproven address lane.
        if(op.code>=20&&op.code<=24)return false;
        const Word target=code[op.at+1];
        const unsigned type=regType(target), reg=regIndex(target), lanes=(target>>16)&15;
        if(type==0)usedTemps|=1u<<reg;
        for(unsigned j=2;j<=op.count;++j){
            const Word operand=code[op.at+j];
            const unsigned kind=regType(operand),source=regIndex(operand);
            if(kind==0)usedTemps|=1u<<source;
            if(kind==3||(kind==1&&!(inputs&(1u<<source))))return false;
            if(operand&0x2000u){
                if(kind!=2||source<Base||source>=Base+RowsPerBone||j==op.count)return false;
                const Word address=code[op.at+(++j)];
                const unsigned lane=(address>>16)&3;
                if(address!=src(3,0,lane*0x55u)||!(addressLanes&(1u<<lane)))return false;
                hasPaletteReads=true;
            }else if(kind==2){
                if(source>=Base)result.limit=std::min(result.limit,source);
                fixedReads[source]=!literals[source];
            }
        }
        if(op.code==46){
            if(multiply||!i||type!=3||!lanes||code[op.at]!=0x0200002eu||(target&0x0ff00000u))return false;
            const auto& product=ops[i-1];
            const Word source=code[op.at+2];
            if(product.code!=5||code[product.at]!=0x03000005u||regType(source)!=0||(source&0x0f002000u))return false;
            const Word productTarget=code[product.at+1];
            temporary=regIndex(productTarget);
            if(regType(productTarget)!=0||temporary!=regIndex(source)||(productTarget&0x0ff00000u))return false;
            Word indices=code[product.at+2],constant=code[product.at+3];
            if(regType(indices)==2)std::swap(indices,constant);
            if(regType(indices)!=1||regIndex(indices)!=blendIndices||regType(constant)!=2||
               (indices&0x0f002000u)||(constant&0x0f002000u))return false;
            const auto* literal=literals[regIndex(constant)];
            if(!literal)return false;
            modifiedLanes=(productTarget>>16)&15;
            for(unsigned lane=0;lane<4;++lane)if(lanes&(1u<<lane)){
                const unsigned read=(source>>(16+lane*2))&3;
                if(!(modifiedLanes&(1u<<read))||literal[(constant>>(16+read*2))&3]!=0x40400000u)return false;
            }
            multiply=product.at;
            addressLanes=lanes;
        }else{
            if(type==3)return false;
            // Folding the bias must not affect another use of the MUL result.
            if(modifiedLanes&&!sources(code,op,lanes,[&](unsigned kind,unsigned index,unsigned read){
                return kind!=0||index!=temporary||!(read&modifiedLanes);
            }))return false;
            if(type==0&&reg==temporary)modifiedLanes&=~lanes;
        }
    }
    if(!hasPaletteReads||result.limit<Base+2*RowsPerBone)return false;
    result.sourceLimit=result.limit;

    if(varyings)materialFixedReads(code,ops,*varyings,fixedReads);
    std::array<unsigned,256> instanceTemp;
    instanceTemp.fill(32);
    if(gameConstants){
        // GameScene's varying constants on the indexed M2 paths. Shader
        // literals and registers without an absolute read stay shared.
        unsigned rows=0;
        for(unsigned c=0;c<256;++c){
            const bool varying=(c>=10&&c<=12)||(c>=17&&c<=24)||c==29||(c>=236&&c<=243);
            if(!fixedReads[c]||!varying)continue;
            if(usedTemps==UINT32_MAX||rows==MaxInstanceConstants)return false;
            const unsigned temp=__builtin_ctz(~usedTemps);
            usedTemps|=1u<<temp;instanceTemp[c]=temp;fixedReads[c]=false;++rows;
            if(!result.instanceReads.empty()&&result.instanceReads.back().first+result.instanceReads.back().count==c)
                ++result.instanceReads.back().count;
            else result.instanceReads.push_back({c,1});
        }
        if(rows){
            // The relocated shader reads the lifted rows through its instance
            // prefix. Only its literals and remaining absolute reads occupy
            // their original slots in the packed bank.
            result.limit=Base;
            while(result.limit<256&&!literals[result.limit]&&!fixedReads[result.limit])++result.limit;
            // Reserve only an original, unread register below the bone bank.
            for(unsigned c=Base;c-->0;)
                if(!literals[c]&&!fixedReads[c]&&instanceTemp[c]==32){result.biasConstant=c;break;}
            if(result.biasConstant==256)return false;
            result.instanceRows=(rows+RowsPerBone-1)/RowsPerBone*RowsPerBone;
        }
    }
    for(unsigned i=0;i<fixedReads.size();){
        if(!fixedReads[i]){++i;continue;}
        const unsigned first=i++;
        while(i<fixedReads.size()&&fixedReads[i])++i;
        result.fixedReads.push_back({first,i-first});
    }
    auto& words=result.words;
    words.reserve(count+4);
    words.push_back(code[0]);
    bool declared=false;
    for(const auto& op:ops){
        if(!declared&&arithmetic(op.code)){
            words.insert(words.end(),{0x0200001fu,0x80000005u|(result.semantic<<16),dst(1,instanceInput,1)});
            if(result.instanceRows){
                const unsigned scratch=instanceTemp[result.instanceReads.front().first];
                // The instance bias includes the native minimum bone. Undo that
                // correction to read this instance's fixed-register prefix.
                words.insert(words.end(),{0x03000002u,dst(0,scratch,1),src(1,instanceInput,0),src(2,result.biasConstant,0),
                    0x0200002eu,dst(3,0,1),src(0,scratch,0)});
                unsigned row=0;
                for(const auto& run:result.instanceReads)for(unsigned c=run.first;c<run.first+run.count;++c)
                    words.insert(words.end(),{0x03000001u,dst(0,instanceTemp[c]),src(2,Base+row++)|0x2000u,src(3,0,0)});
            }
            declared=true;
        }
        if(op.at==multiply&&bias==Bias::Folded){
            words.insert(words.end(),{0x04000004u,code[op.at+1],code[op.at+2],code[op.at+3],src(1,instanceInput,0)});
        }else{
            const auto first=words.size();
            words.insert(words.end(),code+op.at,code+op.at+op.count+1);
            if(result.instanceRows&&arithmetic(op.code))for(unsigned i=2;i<=op.count;++i){
                auto& token=words[first+i];
                if(token&0x2000u){++i;continue;}
                if(regType(token)==2&&instanceTemp[regIndex(token)]<32)
                    token=(token&~0x70001fffu)|instanceTemp[regIndex(token)];
            }
            if(op.at==multiply){
                const Word target=code[op.at+1];
                words.insert(words.end(),{0x03000002u,target,src(0,regIndex(target)),src(1,instanceInput,0)});
            }
        }
    }
    words.push_back(65535);
    output=std::move(result);
    return true;
}catch(const std::bad_alloc&){return false;}
}
