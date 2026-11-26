#pragma once
#include "hdr_ui_patch.h"
#include "tree_canopy_textures.h"
#include "auto_bump_compiled_shaders.h"
#include "water_shader_patch.h"
#include "auto_bump_compiled_shaders.h"
#include <array>
#include <d3d9.h>
#include <unordered_map>

namespace NorthlightTreeCanopy {
constexpr unsigned canopyConstants=3;

inline bool patchCanopyPixel(const std::uint32_t* source,std::size_t words,std::vector<std::uint32_t>& output,unsigned& constant){
    using namespace NorthlightHdrUi;
    std::vector<Instruction> original,tail;
    unsigned originalCost=0,tailCost=0;
    const auto* epilogue=reinterpret_cast<const Word*>(kCanopyResponseShader);
    if(!parse(source,words,original,originalCost)||source[0]!=0xffff0300||
       !parse(epilogue,sizeof(kCanopyResponseShader)/sizeof(Word),tail,tailCost)||originalCost+tailCost>512)return false;

    std::array<bool,32> temps{};
    std::array<bool,224> constants{};
    std::array<bool,10> inputs{};
    unsigned colorMask=0;
    for(const auto& instruction:original){
        const unsigned first=instruction.op==31?2:1;
        const unsigned last=instruction.op==47||instruction.op==48||instruction.op==81?1:unsigned(instruction.count-1);
        if(instruction.op==31&&(source[instruction.at+1]&0x000f000fu)==0x00070005u)return false;
        for(unsigned i=first;i<=last;++i){
            const auto value=source[instruction.at+i];
            const unsigned kind=type(value),number=index(value);
            if(kind==0){if(number>=temps.size())return false;temps[number]=true;}
            if(kind==1){if(number>=inputs.size())return false;inputs[number]=true;}
            if(kind==2){if(number>=constants.size())return false;constants[number]=true;}
            if(kind==9)return false;
            if(kind==8&&number==0){if(i!=1)return false;colorMask|=(value>>16)&15;}
        }
    }
    if(colorMask!=15)return false;

    const auto free=[](auto& registers){
        const auto it=std::find(registers.begin(),registers.end(),false);
        if(it!=registers.end())*it=true;
        return unsigned(it-registers.begin());
    };
    const unsigned color=free(temps),clip=free(inputs);
    if(color==temps.size()||clip==inputs.size())return false;
    const auto bank=std::search_n(constants.begin(),constants.end(),canopyConstants,false);
    if(bank==constants.end())return false;
    constant=unsigned(bank-constants.begin());
    std::fill_n(bank,canopyConstants,true);
    std::array<int,32> tempMap;tempMap.fill(-1);
    std::array<int,224> constantMap;constantMap.fill(-1);
    for(unsigned i=0;i<canopyConstants;++i)constantMap[40+i]=constant+i;

    unsigned colorInput=inputs.size(),clipInput=inputs.size();
    std::vector<Word> definitions,body;
    for(const auto& instruction:tail){
        const auto at=instruction.at;
        if(instruction.op==31){
            const auto semantic=epilogue[at+1]&0x000f000fu;
            if(type(epilogue[at+2])!=1)return false;
            if(semantic==10)colorInput=index(epilogue[at+2]);
            else if(semantic==0x00070005u)clipInput=index(epilogue[at+2]);
            else return false;
            continue;
        }
        if(instruction.op==47||instruction.op==48)return false;
        const bool def=instruction.op==81;
        auto& result=def?definitions:body;
        result.push_back(epilogue[at]);
        for(unsigned i=1;i<instruction.count;++i){
            const auto value=epilogue[at+i];
            if(def&&i>1){result.push_back(value);continue;}
            const auto number=index(value);
            switch(type(value)){
            case 0:
                if(number>=tempMap.size())return false;
                if(tempMap[number]<0)tempMap[number]=int(free(temps));
                if(unsigned(tempMap[number])>=temps.size())return false;
                result.push_back(reg(value,0,unsigned(tempMap[number])));break;
            case 1:
                if(number==colorInput)result.push_back(reg(value,0,color));
                else if(number==clipInput)result.push_back(reg(value,1,clip));
                else return false;
                break;
            case 2:
                if(number>=constantMap.size())return false;
                if(def&&constantMap[number]<0)constantMap[number]=int(free(constants));
                if(constantMap[number]<0||unsigned(constantMap[number])>=constants.size())return false;
                result.push_back(reg(value,2,unsigned(constantMap[number])));break;
            case 8:
                if(number||i!=1)return false;
                result.push_back(value);break;
            default:return false;
            }
        }
    }
    if(colorInput>=inputs.size()||clipInput>=inputs.size())return false;
    output.assign(source,source+words-1);
    for(const auto& instruction:original){
        if(instruction.count<=1)continue;
        auto& destination=output[instruction.at+1];
        if(type(destination)==8&&index(destination)==0)destination=reg(destination,0,color);
    }
    output.insert(output.begin()+1,{0x0200001fu,0x80070005u,reg(0x800f0000u,1,clip)});
    output.insert(output.begin()+1,definitions.begin(),definitions.end());
    output.insert(output.end(),body.begin(),body.end());
    output.push_back(65535);
    return true;
}

class NativeLighting {
    struct Program {IDirect3DPixelShader9* shader=nullptr;unsigned constant=0;};
    std::unordered_map<IDirect3DPixelShader9*,Program> pixels;
    std::unordered_map<IDirect3DVertexShader9*,IDirect3DVertexShader9*> vertices;

    template<class Shader> static std::vector<std::uint32_t> bytecode(Shader* source){
        UINT bytes=0;
        if(FAILED(source->GetFunction(nullptr,&bytes))||bytes<12||bytes>65536||bytes%4)return {};
        std::vector<std::uint32_t> original(bytes/4);
        if(FAILED(source->GetFunction(original.data(),&bytes))||bytes!=original.size()*4)return {};
        return original;
    }
    Program* pixelVariant(IDirect3DDevice9* d,IDirect3DPixelShader9* source){
        const auto cached=pixels.find(source);
        if(cached!=pixels.end())return &cached->second;
        if(pixels.size()>=128)return nullptr;
        auto& result=pixels.try_emplace(source).first->second;
        source->AddRef();
        const auto original=bytecode(source);
        std::vector<std::uint32_t> patched;
        if(patchCanopyPixel(original.data(),original.size(),patched,result.constant))
            d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&result.shader);
        return &result;
    }
    IDirect3DVertexShader9* vertexVariant(IDirect3DDevice9* d,IDirect3DVertexShader9* source){
        const auto cached=vertices.find(source);
        if(cached!=vertices.end())return cached->second;
        if(vertices.size()>=128)return nullptr;
        auto& result=vertices.try_emplace(source,nullptr).first->second;
        source->AddRef();
        const auto original=bytecode(source);
        std::vector<std::uint32_t> patched;
        if(!original.empty()&&original[0]==0xfffe0300u&&
           !NorthlightWaterShaderPatch::patch(original.data(),original.size(),patched))
            d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&result);
        return result;
    }
public:
    ~NativeLighting(){
        for(auto& entry:pixels){entry.first->Release();if(entry.second.shader)entry.second.shader->Release();}
        for(auto& entry:vertices){entry.first->Release();if(entry.second)entry.second->Release();}
    }
    bool restoreFailed=false;
    template<class Draw,class Fallback> HRESULT draw(IDirect3DDevice9* d,IDirect3DVertexShader9* originalVertex,
                                      IDirect3DPixelShader9* originalPixel,const float* light,
                                      const Draw& submit,const Fallback& fallback,bool& used){
        Program* pixel=nullptr;
        IDirect3DVertexShader9* vertex=nullptr;
        try{
            pixel=pixelVariant(d,originalPixel);
            if(pixel&&pixel->shader)vertex=vertexVariant(d,originalVertex);
        }catch(...){return fallback();}
        if(!vertex)return fallback();

        float previous[canopyConstants*4]={};
        if(FAILED(d->GetPixelShaderConstantF(pixel->constant,previous,canopyConstants))||
           FAILED(d->SetVertexShader(vertex)))return fallback();
        const bool pixelBound=SUCCEEDED(d->SetPixelShader(pixel->shader));
        used=pixelBound&&SUCCEEDED(d->SetPixelShaderConstantF(pixel->constant,light,canopyConstants));
        const HRESULT result=used?submit():D3D_OK;
        const HRESULT vertexRestored=d->SetVertexShader(originalVertex);
        const HRESULT pixelRestored=pixelBound?d->SetPixelShader(originalPixel):D3D_OK;
        const HRESULT constants=pixelBound?d->SetPixelShaderConstantF(pixel->constant,previous,canopyConstants):D3D_OK;
        restoreFailed|=FAILED(vertexRestored)||FAILED(pixelRestored)||FAILED(constants);
        return used?result:fallback();
    }
};
inline bool patchCrownPixel(const std::uint32_t* source,std::size_t words,std::vector<std::uint32_t>& output,unsigned& constant){
    using namespace NorthlightHdrUi;
    std::vector<Instruction> instructions;
    unsigned cost=0;
    if(!parse(source,words,instructions,cost)||source[0]!=0xffff0300||cost>510)return false;

    bool temps[32]={},constants[224]={};
    unsigned colorMask=0;
    for(const auto& instruction:instructions){
        const unsigned first=instruction.op==31?2:1;
        const unsigned last=instruction.op==47||instruction.op==48||instruction.op==81?1:unsigned(instruction.count-1);
        for(unsigned i=first;i<=last;++i){
            const auto value=source[instruction.at+i];
            const unsigned kind=type(value),number=index(value);
            if(kind==0){if(number>=32)return false;temps[number]=true;}
            if(kind==2){if(number>=224)return false;constants[number]=true;}
            if(kind==9)return false;
            if(kind==8&&number==0){if(i!=1)return false;colorMask|=(value>>16)&15;}
        }
    }
    if(colorMask!=15)return false;
    unsigned temporary=0;while(temporary<32&&temps[temporary])++temporary;
    constant=0;while(constant<224&&constants[constant])++constant;
    if(temporary==32||constant==224)return false;

    output.assign(source,source+words-1);
    for(const auto& instruction:instructions){
        if(instruction.count<=1)continue;
        auto& destination=output[instruction.at+1];
        if(type(destination)==8&&index(destination)==0)destination=reg(destination,0,temporary);
    }
    // Keep the game shader's cutout, fog and alpha in the same draw.
    const auto color=reg(0x80e40000,0,temporary);
    output.insert(output.end(),{0x03000002u,reg(0x80070000,0,temporary),color,reg(0xa0e40000,2,constant),
        0x02000001u,0x800f0800u,color,65535});
    return true;
}

class CrownLighting {
    struct Program {IDirect3DPixelShader9* shader=nullptr;unsigned constant=0;};
    std::unordered_map<IDirect3DPixelShader9*,Program> shaders;
    Program* variant(IDirect3DDevice9* d,IDirect3DPixelShader9* source){
        const auto cached=shaders.find(source);
        if(cached!=shaders.end())return &cached->second;
        if(shaders.size()>=128)return nullptr;
        auto& result=shaders.try_emplace(source).first->second;
        source->AddRef();
        UINT bytes=0;
        if(FAILED(source->GetFunction(nullptr,&bytes))||bytes<12||bytes>65536||bytes%4)return &result;
        std::vector<std::uint32_t> original(bytes/4),patched;
        if(SUCCEEDED(source->GetFunction(original.data(),&bytes))&&bytes==original.size()*4&&
           patchCrownPixel(original.data(),original.size(),patched,result.constant))
            d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&result.shader);
        return &result;
    }
public:
    ~CrownLighting(){
        for(auto& entry:shaders){entry.first->Release();if(entry.second.shader)entry.second.shader->Release();}
    }
    bool restoreFailed=false;
    template<class Draw> HRESULT draw(IDirect3DDevice9* d,IDirect3DPixelShader9* source,const float* tint,
                                      const Draw& submit,bool& used){
        Program* program=nullptr;
        try{program=variant(d,source);}catch(...){return submit();}
        if(!program||!program->shader)return submit();
        float previous[4]={};
        if(FAILED(d->GetPixelShaderConstantF(program->constant,previous,1))||
           FAILED(d->SetPixelShader(program->shader)))return submit();
        const HRESULT changed=d->SetPixelShaderConstantF(program->constant,tint,1);
        used=SUCCEEDED(changed);
        const HRESULT result=used?submit():D3D_OK;
        const HRESULT restored=d->SetPixelShader(source);
        const HRESULT constants=d->SetPixelShaderConstantF(program->constant,previous,1);
        restoreFailed|=FAILED(restored)||FAILED(constants);
        return used?result:submit();
    }
};
} // namespace NorthlightTreeCanopy
