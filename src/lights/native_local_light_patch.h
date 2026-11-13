#pragma once
#include "equipment_light_proof.h"
#include "actor_deformation.h"
#include "hdr_ui_patch.h"
#include "native_material_capture_patch.h"

// Exact native endpoint programs, not an estimated framebuffer subtraction.
// One owned slot produces two native COLOR0 results, including the native
// saturation. A pixel helper mixes their full material/fog results by visibility.
namespace NorthlightNativeLocalPatch {
using namespace NorthlightWaterShaderPatch;
struct VertexInfo {unsigned count=0,slots[3]{},varying[8]{},addedSlots=0;};
struct PixelInfo {unsigned control=0,samplers[3]{},addedSlots=0,sharedInstructions=0,sharedTextureReads=0,savedSlots=0;};
inline bool vertexLegacy(const Word* code,size_t size,const unsigned* slots,unsigned count,std::vector<Word>& out,VertexInfo& info,unsigned reservedSemantics=0){
    if(!code||!slots||count<1||count>2||code[0]!=0xfffe0300u)return false;
    const unsigned lights=NorthlightEquipmentLightProof::analyze(code,size);
    if(!lights||slots[0]>=lights||(count==2&&(slots[1]>=lights||slots[0]>=slots[1])))return false;
    NorthlightActorDeformation::Program color,alpha,position;
    if(!NorthlightActorDeformation::compile(code,size,color,true,2,10,0,15)||
       !NorthlightActorDeformation::compile(code,size,alpha,true,2,10,0,8)||
       !NorthlightActorDeformation::compile(code,size,position)||position.positionRegister>=32)return false;
    // Changing a light's RGB must not alter coverage/alpha.
    for(const auto& op:alpha.operations)for(const auto& s:op.source)if(regType(s.token)==2&&regIndex(s.token)>=17&&regIndex(s.token)<21)return false;
    bool outputs[12]{},semantics[8]{},constants[256]{};unsigned cost=0;for(unsigned i=0;i<8;++i)semantics[i]=(reservedSemantics&(1u<<i))!=0;
    for(size_t at=1;at<size-1;){unsigned op=code[at]&65535,n=op==65534?(code[at]>>16)&32767:(code[at]>>24)&15;
        if(n>=size-at)return false;
        if(op==31&&n==2){const auto d=code[at+2];if(regType(d)==6){if(regIndex(d)>=12)return false;outputs[regIndex(d)]=true;if((code[at+1]&15)==5){unsigned semantic=(code[at+1]>>16)&15;if(semantic>=8)return false;semantics[semantic]=true;}}}
        if(op!=31&&op!=81&&op!=65534)cost+=NorthlightShadowShader::instructionSlots(op);
        if(op!=65534)for(unsigned k=op==31?2:1;k<=n;++k){if(op==81&&k>1)break;const Word t=code[at+k];if(regType(t)==2&&regIndex(t)<256)constants[regIndex(t)]=true;}
        at+=n+1;
    }
    unsigned zero=0;while(zero<31&&constants[zero])++zero;if(zero==31)return false;
    unsigned registers[4]{};const unsigned variants=1u<<count;
    for(unsigned i=0;i<variants;++i){unsigned r=0,s=0;while(r<12&&outputs[r])++r;while(s<8&&semantics[s])++s;if(r==12||s==8)return false;outputs[r]=semantics[s]=true;registers[i]=r;info.varying[i]=s;}
    std::vector<Word> result(code,code+size-1),declarations;
    for(unsigned i=0;i<variants;++i)declarations.insert(declarations.end(),{0x0200001fu,0x80000005u|(info.varying[i]<<16),dst(6,registers[i],i?15:7)});
    result.insert(result.begin()+1,declarations.begin(),declarations.end());
    result.insert(result.begin()+1,{0x05000051u,dst(2,zero),0,0,0,0});
    auto append=[&](const NorthlightActorDeformation::Program& program,unsigned subset,unsigned output){
        for(const auto& op:program.operations){const int operands=NorthlightShadowShader::arithmeticOperands(op.code,3)-1;if(operands<0)return false;
            std::vector<Word> instruction;Word d=op.destination;if(regType(d)==0&&regIndex(d)==32)d=replaceReg(d,6,output);instruction.push_back(d);
            for(int k=0;k<operands;++k){Word s=op.source[k].token;
                for(unsigned i=0;i<count;++i)if((subset&(1u<<i))&&regType(s)==2&&regIndex(s)==17+slots[i])s=replaceReg(s,2,zero);
                instruction.push_back(s);if(s&0x2000u)instruction.push_back(op.source[k].address);}
            result.push_back(op.code|(unsigned(instruction.size())<<24));result.insert(result.end(),instruction.begin(),instruction.end());cost+=NorthlightShadowShader::instructionSlots(op.code);++info.addedSlots;
        }return true;
    };
    if(!append(position,0,0))return false;
    result.insert(result.end(),{0x02000001u,dst(6,registers[0],7),src(0,position.positionRegister)});++cost;
    for(unsigned subset=1;subset<variants;++subset)if(!append(color,subset,registers[subset]))return false;
    if(cost>512)return false;result.push_back(65535);info.count=count;std::copy_n(slots,count,info.slots);out.swap(result);return true;
}

// Pack receiver XYZ and seven RGB alternatives into six TEXCOORD registers.
// Native alpha is independent of the removed lights and stays unchanged.
inline bool vertex(const Word* code,size_t size,const unsigned* slots,unsigned count,std::vector<Word>& out,VertexInfo& info,unsigned reservedSemantics=0){
 if(count!=3)return vertexLegacy(code,size,slots,count,out,info,reservedSemantics);
 if(!code||!slots||code[0]!=0xfffe0300u)return false;
 const unsigned lights=NorthlightEquipmentLightProof::analyze(code,size);
 for(unsigned i=0;i<3;++i)if(slots[i]>=lights||(i&&slots[i-1]>=slots[i]))return false;
 NorthlightActorDeformation::Program color,alpha,position;
 if(!NorthlightActorDeformation::compile(code,size,color,true,2,10,0,15)||!NorthlightActorDeformation::compile(code,size,alpha,true,2,10,0,8)||!NorthlightActorDeformation::compile(code,size,position)||position.positionRegister>=32)return false;
 for(const auto& op:alpha.operations)for(const auto& t:op.source)if(regType(t.token)==2&&regIndex(t.token)>=17&&regIndex(t.token)<21)return false;
 bool outputs[12]{},semantics[8]{},constants[256]{},temps[32]{};unsigned cost=0;
 for(unsigned i=0;i<8;++i)semantics[i]=(reservedSemantics&(1u<<i))!=0;
 for(size_t at=1;at<size-1;){unsigned op=code[at]&65535,n=op==65534?(code[at]>>16)&32767:(code[at]>>24)&15;if(n>=size-at)return false;
  if(op==31&&n==2&&regType(code[at+2])==6){unsigned r=regIndex(code[at+2]);if(r>=12)return false;outputs[r]=true;if((code[at+1]&15)==5){unsigned t=(code[at+1]>>16)&15;if(t>=8)return false;semantics[t]=true;}}
  if(op!=31&&op!=81&&op!=65534)cost+=NorthlightShadowShader::instructionSlots(op);
  if(op!=65534)for(unsigned k=op==31?2:1;k<=n;++k){if(op==81&&k>1)break;const auto t=code[at+k];if(regType(t)==2&&regIndex(t)<256)constants[regIndex(t)]=true;if(regType(t)==0&&regIndex(t)<32)temps[regIndex(t)]=true;}at+=n+1;
 }
 unsigned zero=0;while(zero<31&&constants[zero])++zero;if(zero==31)return false;
 unsigned scratch=0;while(scratch<32&&temps[scratch])++scratch;if(scratch==32)return false;
 unsigned registers[6]{};for(unsigned i=0;i<6;++i){unsigned r=0,t=0;while(r<12&&outputs[r])++r;while(t<8&&semantics[t])++t;if(r==12||t==8)return false;outputs[r]=semantics[t]=true;registers[i]=r;info.varying[i]=t;}
 std::vector<Word> result(code,code+size-1),decl;
 for(unsigned i=0;i<6;++i)decl.insert(decl.end(),{0x0200001fu,0x80000005u|(info.varying[i]<<16),dst(6,registers[i],15)});
 result.insert(result.begin()+1,decl.begin(),decl.end());result.insert(result.begin()+1,{0x05000051u,dst(2,zero),0,0,0,0});
 auto append=[&](const NorthlightActorDeformation::Program& program,unsigned subset){for(const auto& op:program.operations){int operands=NorthlightShadowShader::arithmeticOperands(op.code,3)-1;if(operands<0)return false;std::vector<Word> instruction;Word d=op.destination;if(regType(d)==0&&regIndex(d)==32)d=replaceReg(d,0,scratch);instruction.push_back(d);
 for(int k=0;k<operands;++k){Word t=op.source[k].token;for(unsigned i=0;i<3;++i)if((subset&(1u<<i))&&regType(t)==2&&regIndex(t)==17+slots[i])t=replaceReg(t,2,zero);instruction.push_back(t);if(t&0x2000u)instruction.push_back(op.source[k].address);}result.push_back(op.code|(unsigned(instruction.size())<<24));result.insert(result.end(),instruction.begin(),instruction.end());cost+=NorthlightShadowShader::instructionSlots(op.code);++info.addedSlots;}return true;};
 auto pack=[&](unsigned offset,unsigned input){for(unsigned lane=0;lane<3;++lane){unsigned index=offset+lane;Word t=src(0,input);t=(t&~0x00ff0000u)|((lane*0x55u)<<16);result.insert(result.end(),{0x02000001u,dst(6,registers[index/4],1u<<(index%4)),t});++cost;}};
 if(!append(position,0))return false;pack(0,position.positionRegister);
 for(unsigned subset=1;subset<8;++subset){if(!append(color,subset))return false;pack(subset*3,scratch);}
 if(cost>512)return false;result.push_back(65535);info.count=3;std::copy_n(slots,3,info.slots);out.swap(result);return true;
}
inline bool alphaIndependent(const Word* code,size_t size,unsigned diffuse){
    NorthlightTerrainCoverage::Program program;if(!NorthlightTerrainCoverage::analyse(code,size,program))return false;
    std::vector<Word> alpha;if(!NorthlightNativeMaterialCapture::alphaSlice(code,program,true,alpha))return false;
    std::vector<NorthlightHdrUi::Instruction> instructions;unsigned slots=0;if(!NorthlightHdrUi::parse(alpha.data(),alpha.size(),instructions,slots))return false;
    for(const auto& op:instructions){if(op.op==31||op.op==81||op.op==48||op.op==47)continue;
        unsigned channels=op.op==65?15u:(alpha[op.at+1]>>16)&15;
        if(op.op==8||op.op==33||op.op==36)channels=7;else if(op.op==9||op.op==16||op.op==17)channels=15;
        else if(op.op==6||op.op==7||op.op==14||op.op==15||op.op==32)channels=1;
        for(unsigned k=op.op==65?1:2;k<op.count;++k){Word s=alpha[op.at+k];if(regType(s)!=1||regIndex(s)!=diffuse)continue;
            for(unsigned lane=0;lane<4;++lane)if((channels&(1u<<lane))&&((s>>(16+2*lane))&3)<3)return false;}
    }return true;
}
inline bool pointOwnerPixel(const Word* code,size_t size){
    NorthlightTerrainCoverage::Program program;if(!NorthlightTerrainCoverage::analyse(code,size,program)||program.vertex||program.major!=3)return false;
    for(const auto& op:program.ops)if(op.code==31&&regType(code[op.at+2])==1&&(code[op.at+1]&0xf000fu)==10)
        return alphaIndependent(code,size,regIndex(code[op.at+2]));
    return false;
}
// Share only pure, straight-line values that cannot depend on native diffuse.
// First-endpoint order is retained. Persistent registers are never native temps;
// the helper may reuse them only after every endpoint has consumed its values.
inline unsigned materialSourceMask(unsigned op,unsigned operand,unsigned destination){
    switch(op){
    case 6:case 7:case 14:case 15:case 32:case 35:return 1;
    case 8:case 33:case 36:return 7;
    case 9:return 15;
    case 90:return operand==2?1:3;
    default:return destination;
    }
}
inline bool sharedEndpoints(const Word* code,const std::vector<NorthlightHdrUi::Instruction>& ops,unsigned diffuse,
    unsigned variants,const unsigned* endpoint,const unsigned* varying,const bool* occupied,std::vector<Word>& output,
    unsigned& cost,unsigned& shared,unsigned& textures){
    struct Value {bool independent=false,needed=false;int cache=-1;};std::vector<Value> values(ops.size());
    unsigned inputMask[10]{},samplerType[16]{};
    for(const auto& op:ops)if(op.op==31){const Word token=code[op.at+2];if(regType(token)==1)inputMask[regIndex(token)]=(token>>16)&15;
        else if(regType(token)==10)samplerType[regIndex(token)]=(code[op.at+1]>>27)&15;}
    unsigned taint[32];std::fill_n(taint,32,15u);int producer[32][4];for(auto& row:producer)std::fill_n(row,4,-1);
    auto reads=[&](const NorthlightHdrUi::Instruction& op,unsigned k){
        unsigned lanes=materialSourceMask(op.op,k-2,(code[op.at+1]>>16)&15);
        if(op.op==66||op.op==93||op.op==95)lanes=3|((op.op==95||(op.op==66&&(code[op.at]&0x00ff0000u)))?8:0);
        const Word token=code[op.at+k];unsigned mask=0;
        for(unsigned lane=0;lane<4;++lane)if(lanes&(1u<<lane))mask|=1u<<((token>>(16+2*lane))&3);return mask;
    };
    for(unsigned i=0;i<ops.size();++i){const auto& op=ops[i];if(op.op==31||op.op==81||op.op==0)continue;
        switch(op.op){
        case 1:case 2:case 3:case 4:case 5:case 6:case 7:case 8:case 9:case 10:case 11:case 12:case 13:case 14:case 15:
        case 18:case 19:case 32:case 33:case 35:case 36:case 66:case 88:case 90:case 91:case 92:case 93:case 95:break;
        default:return false; // Includes texkill: never move a derivative across a discard epoch.
        }
        const Word destination=code[op.at+1];const unsigned type=regType(destination),r=regIndex(destination),mask=(destination>>16)&15;
        if((type!=0&&type!=8)||(destination&0x0f200000u)||((op.op==33||op.op==36)&&(mask&8)))return false; // no partial precision/undefined lanes
        if(op.op==66||op.op==93||op.op==95){const Word sampler=code[op.at+3];if(regType(sampler)!=10||samplerType[regIndex(sampler)]!=2)return false;}
        bool dependent=type!=0;
        for(unsigned k=2;k<op.count;++k){const Word token=code[op.at+k];const unsigned t=regType(token),n=regIndex(token),modifier=(token>>24)&15;
            if(modifier!=0&&modifier!=1&&modifier!=11&&modifier!=12)return false;
            if(t==0)dependent|=(taint[n]&reads(op,k))!=0;
            else if(t==1){if(n>=10)return false;dependent|=n==diffuse||(reads(op,k)&~inputMask[n])!=0;}
            else if(t!=2&&t!=10)return false;
        }
        values[i].independent=!dependent;
        if(dependent)for(unsigned k=2;k<op.count;++k){const Word token=code[op.at+k];if(regType(token)!=0)continue;const unsigned used=reads(op,k);
            for(unsigned lane=0;lane<4;++lane)if(used&(1u<<lane)){const int p=producer[regIndex(token)][lane];if(p>=0&&values[p].independent)values[p].needed=true;}}
        if(type==0){taint[r]=(taint[r]&~mask)|(dependent?mask:0);for(unsigned lane=0;lane<4;++lane)if(mask&(1u<<lane))producer[r][lane]=int(i);}
    }
    bool reserved[32];std::copy_n(occupied,32,reserved);
    for(auto& value:values)if(value.needed){unsigned r=0;while(r<32&&reserved[r])++r;if(r==32)return false;reserved[r]=true;value.cache=int(r);}
    std::vector<Word> result;unsigned slots=0,omitted=0,omittedTextures=0;
    for(unsigned subset=0;subset<variants;++subset){unsigned alias[32][4];for(unsigned r=0;r<32;++r)std::fill_n(alias[r],4,r);
        for(unsigned i=0;i<ops.size();++i){const auto& op=ops[i];if(op.op==31||op.op==81)continue;
            const bool skip=subset&&values[i].independent;const Word destination=op.count>1?code[op.at+1]:0;
            if(!skip){std::vector<Word> instruction(code+op.at,code+op.at+op.count);
                for(unsigned k=2;k<op.count;++k){Word& token=instruction[k];const unsigned type=regType(token),r=regIndex(token);
                    if(type==0){const unsigned used=reads(op,k);unsigned selected=32;bool mixed=false;
                        for(unsigned lane=0;lane<4;++lane)if(used&(1u<<lane)){if(selected==32)selected=alias[r][lane];else mixed|=selected!=alias[r][lane];}
                        if(mixed){for(unsigned lane=0;lane<4;++lane)if((used&(1u<<lane))&&alias[r][lane]!=r){const unsigned source=alias[r][lane];unsigned lanes=0;
                                for(unsigned other=0;other<4;++other)if((used&(1u<<other))&&alias[r][other]==source){lanes|=1u<<other;alias[r][other]=r;}
                                result.insert(result.end(),{0x02000001u,dst(0,r,lanes),src(0,source)});++slots;}selected=r;}
                        if(selected<32)token=replaceReg(token,0,selected);
                    }else if(subset&&type==1&&r==diffuse)token=replaceReg(token,1,varying[subset]);
                }
                if(op.count>1){if(regType(destination)==8)instruction[1]=replaceReg(destination,0,endpoint[subset]);
                    else if(values[i].cache>=0)instruction[1]=replaceReg(destination,0,unsigned(values[i].cache));}
                result.insert(result.end(),instruction.begin(),instruction.end());slots+=NorthlightHdrUi::slots(op.op);
            }else{++omitted;if(op.op==66||op.op==93||op.op==95)++omittedTextures;}
            if(op.count>1&&regType(destination)==0){const unsigned r=regIndex(destination),mask=(destination>>16)&15;
                for(unsigned lane=0;lane<4;++lane)if(mask&(1u<<lane))alias[r][lane]=values[i].cache>=0?unsigned(values[i].cache):r;}
        }
    }
    if(!omitted)return false;output.swap(result);cost=slots;shared=omitted;textures=omittedTextures;return true;
}
inline bool pixelLegacy(const Word* code,size_t size,const VertexInfo& vertex,const Word* helper,size_t helperSize,std::vector<Word>& out,PixelInfo& info,bool enableSharing=true){
    if(!code||code[0]!=0xffff0300u||vertex.count<1||vertex.count>2||!helper||helper[0]!=code[0])return false;
    using NorthlightHdrUi::Instruction;std::vector<Instruction> original,tail;unsigned originalCost=0,helperCost=0;
    if(!NorthlightHdrUi::parse(code,size,original,originalCost)||!NorthlightHdrUi::parse(helper,helperSize,tail,helperCost))return false;
    bool temps[32]{},constants[224]{},inputs[10]{},samplers[16]{};int diffuse=-1;unsigned outputMask=0;
    for(const auto& op:original){
        if(op.op==31){Word d=code[op.at+2];if(regType(d)==1){if(regIndex(d)>=10)return false;inputs[regIndex(d)]=true;if((code[op.at+1]&15)==10&&((code[op.at+1]>>16)&15)==0)diffuse=int(regIndex(d));}}
        else if(op.op!=81&&op.op!=65&&NorthlightWaterShaderPatch::arity(op.op,3)<0)return false; // straight-line native programs only
        for(unsigned k=op.op==31?2:1;k<op.count;++k){if(op.op==81&&k>1)break;Word t=code[op.at+k];unsigned r=regIndex(t),type=regType(t);
            if(type==0){if(r>=32)return false;temps[r]=true;}else if(type==2){if(r>=224)return false;constants[r]=true;}else if(type==10){if(r>=16)return false;samplers[r]=true;}
            else if(type==8){if(k!=1||r)return false;outputMask|=(t>>16)&15;}else if(type==9||type==7||type==14)return false;}
    }
    if(diffuse<0||outputMask!=15||!alphaIndependent(code,size,unsigned(diffuse)))return false;
    unsigned variantTemps[4]{},varyingInputs[4]{};const unsigned variants=1u<<vertex.count;
    for(unsigned i=0;i<variants;++i){unsigned r=32;while(r&&temps[r-1])--r;if(!r)return false;variantTemps[i]=r-1;temps[r-1]=true;
        unsigned input=0;while(input<10&&inputs[input])++input;if(input==10)return false;inputs[input]=true;varyingInputs[i]=input;}
    unsigned control=0;for(;control+14<=224;++control){bool free=true;for(unsigned k=0;k<14;++k)free&=!constants[control+k];if(free)break;}if(control+14>224)return false;
    for(unsigned k=0;k<14;++k)constants[control+k]=true;info.control=control;
    for(unsigned i=0;i<vertex.count;++i){unsigned s=0;while(s<16&&samplers[s])++s;if(s==16)return false;samplers[s]=true;info.samplers[i]=s;}
    std::vector<Word> result{code[0]},definitions,body;
    for(const auto& op:original)if(op.op==31||op.op==81)result.insert(result.end(),code+op.at,code+op.at+op.count);
    for(unsigned i=0;i<variants;++i)result.insert(result.end(),{0x0200001fu,0x80000005u|(vertex.varying[i]<<16),dst(1,varyingInputs[i],i?15:7)});
    unsigned nativeCost=originalCost*variants,shared=0,sharedTextures=0;std::vector<Word> endpoints;
    unsigned sharedCost=0;
    bool share=false;
    if(enableSharing)try{share=sharedEndpoints(code,original,unsigned(diffuse),variants,variantTemps,varyingInputs,temps,endpoints,sharedCost,shared,sharedTextures)&&sharedCost<nativeCost;}catch(const std::bad_alloc&){}
    if(share){
        result.insert(result.end(),endpoints.begin(),endpoints.end());nativeCost=sharedCost;
    }else{
        shared=sharedTextures=0;
    for(unsigned subset=0;subset<variants;++subset)for(const auto& op:original){if(op.op==31||op.op==81)continue;
        result.push_back(code[op.at]);for(unsigned k=1;k<op.count;++k){Word t=code[op.at+k];if(regType(t)==8)t=replaceReg(t,0,variantTemps[subset]);
            else if(subset&&regType(t)==1&&regIndex(t)==unsigned(diffuse))t=replaceReg(t,1,varyingInputs[subset]);result.push_back(t);}}
    }
    int constantMap[224],tempMap[32],inputMap[16];std::fill_n(constantMap,224,-1);std::fill_n(tempMap,32,-1);std::fill_n(inputMap,16,-1);bool helperDefined[224]{},helperInteger[16]{};
    for(const auto& op:tail)if(op.op==48){const auto r=regIndex(helper[op.at+1]);if(regType(helper[op.at+1])!=7||r>=16)return false;helperInteger[r]=true;}
    for(const auto& op:tail)if(op.op==81)helperDefined[regIndex(helper[op.at+1])]=true;
    for(const auto& op:tail)if(op.op==31&&regType(helper[op.at+2])==1){unsigned r=regIndex(helper[op.at+2]),semantic=(helper[op.at+1]>>16)&15;if(r>=16||semantic>4)return false;inputMap[r]=int(semantic);}
    auto mapped=[&](Word t,Word& value){const unsigned type=regType(t),r=regIndex(t);value=t;
        if(type==1){if(r>=16||inputMap[r]<0)return false;unsigned s=unsigned(inputMap[r]);if(s==4)value=replaceReg(t,1,varyingInputs[0]);else{if(s>=variants)return false;value=replaceReg(t,0,variantTemps[s]);}return true;}
        if(type==0){if(r>=32)return false;if(tempMap[r]<0){unsigned s=0;while(s<32&&std::find(variantTemps,variantTemps+variants,s)!=variantTemps+variants)++s;
                for(;s<32;++s)if(std::find(variantTemps,variantTemps+variants,s)==variantTemps+variants&&std::find(tempMap,tempMap+32,int(s))==tempMap+32)break;if(s==32)return false;tempMap[r]=int(s);}value=replaceReg(t,0,unsigned(tempMap[r]));return true;}
        if(type==2){if(r>=224)return false;if(constantMap[r]<0){if(!helperDefined[r]){if(r>=14)return false;constantMap[r]=int(control+r);}else{unsigned s=224;while(s&&constants[s-1])--s;if(!s)return false;constants[s-1]=true;constantMap[r]=int(s-1);}}value=replaceReg(t,2,unsigned(constantMap[r]));return true;}
        if(type==10){if(r<1||r>vertex.count)return false;value=replaceReg(t,10,info.samplers[r-1]);return true;}// Native integer operands are rejected above; only helper-owned DEFI registers survive.
        return (type==8&&r==0)||(type==7&&r<16&&helperInteger[r]);
    };
    for(const auto& op:tail){if(op.op==31){if(regType(helper[op.at+2])==10){Word s;if(!mapped(helper[op.at+2],s))return false;definitions.insert(definitions.end(),{helper[op.at],helper[op.at+1],s});}continue;}
        auto& destination=(op.op==81||op.op==48)?definitions:body;destination.push_back(helper[op.at]);for(unsigned k=1;k<op.count;++k){Word t;if((op.op==81||op.op==48)&&k>1)t=helper[op.at+k];else if(!mapped(helper[op.at+k],t))return false;destination.push_back(t);}}
    result.insert(result.begin()+1,definitions.begin(),definitions.end());result.insert(result.end(),body.begin(),body.end());result.push_back(65535);
    // Cube texldl costs 5 slots. The generic parser counts its 2D cost as 2.
    unsigned cubeReads=0;for(const auto& op:tail)if(op.op==95||op.op==66)++cubeReads;
    const unsigned cost=nativeCost+helperCost+3*cubeReads;if(cost>512)return false;info.addedSlots=cost-originalCost;info.sharedInstructions=shared;info.sharedTextureReads=sharedTextures;info.savedSlots=originalCost*variants-nativeCost;out.swap(result);return true;
}
inline bool pixelThree(const Word* code,size_t size,const VertexInfo& vertex,const Word* helper,size_t helperSize,std::vector<Word>& out,PixelInfo& info,bool enableSharing=true){
    if(!code||code[0]!=0xffff0300u||vertex.count!=3||!helper||helper[0]!=code[0])return false;
    using NorthlightHdrUi::Instruction;std::vector<Instruction> original,tail;unsigned originalCost=0,helperCost=0;
    if(!NorthlightHdrUi::parse(code,size,original,originalCost)||!NorthlightHdrUi::parse(helper,helperSize,tail,helperCost))return false;
    bool temps[32]{},constants[224]{},inputs[10]{},samplers[16]{};int diffuse=-1;unsigned outputMask=0;
    for(const auto& op:original){
        if(op.op==31){Word d=code[op.at+2];if(regType(d)==1){if(regIndex(d)>=10)return false;inputs[regIndex(d)]=true;if((code[op.at+1]&15)==10&&((code[op.at+1]>>16)&15)==0)diffuse=int(regIndex(d));}}
        else if(op.op!=81&&op.op!=65&&NorthlightWaterShaderPatch::arity(op.op,3)<0)return false; // straight-line native programs only
        for(unsigned k=op.op==31?2:1;k<op.count;++k){if(op.op==81&&k>1)break;Word t=code[op.at+k];unsigned r=regIndex(t),type=regType(t);
            if(type==0){if(r>=32)return false;temps[r]=true;}else if(type==2){if(r>=224)return false;constants[r]=true;}else if(type==10){if(r>=16)return false;samplers[r]=true;}
            else if(type==8){if(k!=1||r)return false;outputMask|=(t>>16)&15;}else if(type==9||type==7||type==14)return false;}
    }
    if(diffuse<0||outputMask!=15||!alphaIndependent(code,size,unsigned(diffuse)))return false;
    unsigned variantTemps[8]{},varyingInputs[8]{},packedInputs[6]{},unpacked[8]{};const unsigned variants=1u<<vertex.count;
    for(unsigned i=0;i<8;++i){unsigned r=32;while(r&&temps[r-1])--r;if(!r)return false;variantTemps[i]=r-1;temps[r-1]=true;varyingInputs[i]=10+i;}
    for(unsigned i=0;i<8;++i){unsigned r=32;while(r&&temps[r-1])--r;if(!r)return false;unpacked[i]=r-1;temps[r-1]=true;}
    for(unsigned i=0;i<6;++i){unsigned r=0;while(r<10&&inputs[r])++r;if(r==10)return false;packedInputs[i]=r;inputs[r]=true;}
    unsigned control=0;for(;control+14<=224;++control){bool free=true;for(unsigned k=0;k<14;++k)free&=!constants[control+k];if(free)break;}if(control+14>224)return false;
    for(unsigned k=0;k<14;++k)constants[control+k]=true;info.control=control;
    for(unsigned i=0;i<vertex.count;++i){unsigned s=0;while(s<16&&samplers[s])++s;if(s==16)return false;samplers[s]=true;info.samplers[i]=s;}
    std::vector<Word> result{code[0]},definitions,body;
    for(const auto& op:original)if(op.op==31||op.op==81)result.insert(result.end(),code+op.at,code+op.at+op.count);
    for(unsigned i=0;i<6;++i)result.insert(result.end(),{0x0200001fu,0x80000005u|(vertex.varying[i]<<16),dst(1,packedInputs[i],15)});
    for(unsigned subset=0;subset<8;++subset){for(unsigned lane=0;lane<3;++lane){unsigned index=subset*3+lane;Word t=src(1,packedInputs[index/4]);t=(t&~0x00ff0000u)|(((index%4)*0x55u)<<16);result.insert(result.end(),{0x02000001u,dst(0,unpacked[subset],1u<<lane),t});}if(subset)result.insert(result.end(),{0x02000001u,dst(0,unpacked[subset],8),(src(1,unsigned(diffuse))&~0x00ff0000u)|0x00ff0000u});}
    unsigned nativeCost=originalCost*variants,shared=0,sharedTextures=0;std::vector<Word> endpoints;
    unsigned sharedCost=0;
    bool share=false;
    if(enableSharing)try{share=sharedEndpoints(code,original,unsigned(diffuse),variants,variantTemps,varyingInputs,temps,endpoints,sharedCost,shared,sharedTextures)&&sharedCost<nativeCost;}catch(const std::bad_alloc&){}
    if(share){
        result.insert(result.end(),endpoints.begin(),endpoints.end());nativeCost=sharedCost;
    }else{
        shared=sharedTextures=0;
    for(unsigned subset=0;subset<variants;++subset)for(const auto& op:original){if(op.op==31||op.op==81)continue;
        result.push_back(code[op.at]);for(unsigned k=1;k<op.count;++k){Word t=code[op.at+k];if(regType(t)==8)t=replaceReg(t,0,variantTemps[subset]);
            else if(subset&&regType(t)==1&&regIndex(t)==unsigned(diffuse))t=replaceReg(t,1,varyingInputs[subset]);result.push_back(t);}}
    }
    // The material endpoints consume reconstructed RGB and unchanged native alpha.
    for(size_t at=1;at+1<result.size();){unsigned op=result[at]&65535,count=(result[at]>>24)&15;for(unsigned k=2;k<=count;++k){Word& t=result[at+k];if(op!=31&&op!=81&&regType(t)==1&&regIndex(t)>=11&&regIndex(t)<18)t=replaceReg(t,0,unpacked[regIndex(t)-10]);}at+=count+1;}
    int constantMap[224],tempMap[32],inputMap[16];std::fill_n(constantMap,224,-1);std::fill_n(tempMap,32,-1);std::fill_n(inputMap,16,-1);bool helperDefined[224]{},helperInteger[16]{};
    for(const auto& op:tail)if(op.op==48){const auto r=regIndex(helper[op.at+1]);if(regType(helper[op.at+1])!=7||r>=16)return false;helperInteger[r]=true;}
    for(const auto& op:tail)if(op.op==81)helperDefined[regIndex(helper[op.at+1])]=true;
    for(const auto& op:tail)if(op.op==31&&regType(helper[op.at+2])==1){unsigned r=regIndex(helper[op.at+2]),semantic=(helper[op.at+1]>>16)&15;if(r>=16||semantic>7)return false;inputMap[r]=(helper[op.at+1]&15)==10?8:int(semantic);}
    auto mapped=[&](Word t,Word& value){const unsigned type=regType(t),r=regIndex(t);value=t;
        if(type==1){if(r>=16||inputMap[r]<0)return false;unsigned s=unsigned(inputMap[r]);if(s==7)value=replaceReg(t,0,unpacked[0]);else value=replaceReg(t,0,variantTemps[s==8?0:s+1]);return true;}
        if(type==0){if(r>=32)return false;if(tempMap[r]<0){unsigned s=0;while(s<32&&(s==unpacked[0]||std::find(variantTemps,variantTemps+variants,s)!=variantTemps+variants))++s;
                for(;s<32;++s)if(s!=unpacked[0]&&std::find(variantTemps,variantTemps+variants,s)==variantTemps+variants&&std::find(tempMap,tempMap+32,int(s))==tempMap+32)break;if(s==32)return false;tempMap[r]=int(s);}value=replaceReg(t,0,unsigned(tempMap[r]));return true;}
        if(type==2){if(r>=224)return false;if(constantMap[r]<0){if(!helperDefined[r]){if(r>=14)return false;constantMap[r]=int(control+r);}else{unsigned s=224;while(s&&constants[s-1])--s;if(!s)return false;constants[s-1]=true;constantMap[r]=int(s-1);}}value=replaceReg(t,2,unsigned(constantMap[r]));return true;}
        if(type==10){if(r<1||r>vertex.count)return false;value=replaceReg(t,10,info.samplers[r-1]);return true;}// Native integer operands are rejected above; only helper-owned DEFI registers survive.
        return (type==8&&r==0)||(type==7&&r<16&&helperInteger[r]);
    };
    for(const auto& op:tail){if(op.op==31){if(regType(helper[op.at+2])==10){Word s;if(!mapped(helper[op.at+2],s))return false;definitions.insert(definitions.end(),{helper[op.at],helper[op.at+1],s});}continue;}
        auto& destination=(op.op==81||op.op==48)?definitions:body;destination.push_back(helper[op.at]);for(unsigned k=1;k<op.count;++k){Word t;if((op.op==81||op.op==48)&&k>1)t=helper[op.at+k];else if(!mapped(helper[op.at+k],t))return false;destination.push_back(t);}}
    result.insert(result.begin()+1,definitions.begin(),definitions.end());result.insert(result.end(),body.begin(),body.end());result.push_back(65535);
    // Cube texldl costs 5 slots. The generic parser counts its 2D cost as 2.
    unsigned cubeReads=0;for(const auto& op:tail)if(op.op==95||op.op==66)++cubeReads;
    const unsigned cost=31+nativeCost+helperCost+3*cubeReads;if(cost>512)return false;info.addedSlots=cost-originalCost;info.sharedInstructions=shared;info.sharedTextureReads=sharedTextures;info.savedSlots=originalCost*variants-nativeCost;out.swap(result);return true;
}
inline bool pixel(const Word* code,size_t size,const VertexInfo& vertex,const Word* helper,size_t helperSize,std::vector<Word>& out,PixelInfo& info,bool enableSharing=true){return vertex.count==3?pixelThree(code,size,vertex,helper,helperSize,out,info,enableSharing):pixelLegacy(code,size,vertex,helper,helperSize,out,info,enableSharing);}
} // namespace NorthlightNativeLocalPatch
