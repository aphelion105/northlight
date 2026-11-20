#ifdef NDEBUG
#undef NDEBUG
#endif
#include "shader_constant_usage.h"
#include "world_shader_signatures.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <array>

using namespace NorthlightShaderConstants;
using Word=uint32_t;
static Word reg(unsigned type,unsigned index,bool relative=false){return NorthlightShadowShader::replaceReg(0x80e40000u, type,index)|(relative?0x2000u:0);}
static Word dest(unsigned type,unsigned index){return NorthlightShadowShader::replaceReg(0x800f0000u,type,index);}
static std::vector<Word> header(){return {0xfffe0200u};}
static void mov(std::vector<Word>& w,Word source){w.insert(w.end(),{0x02000001u,dest(0,0),source});}
static Usage finish(std::vector<Word> w,unsigned kind=2){w.push_back(0xffff);return analyze(w.data(),w.size(),kind);}
static void full(const Usage& u){assert(!u.analyzed&&u.floats.first==0&&u.floats.count==256&&u.booleans.count==16&&u.integers.count==16);}
static void weighted(){
    const std::vector<Word> shader={0xfffe0300u,
        0x0200001fu,0x80000002u,0x900f0002u,
        0x05000051u,0xa00f0000u,0x40400000u,0x3f800000u,0,0,
        0x03000005u,0x800f0000u,0xa0000000u,0x90e40002u,
        0x0200002eu,0xb00f0000u,0x80e40000u,
        0x03000001u,0x800f0000u,0xa0e42021u,0xb0ff0000u};
    auto withFixed=shader;mov(withFixed,reg(2,67));
    const auto u=finish(withFixed);
    assert(u.relativeFloat&&u.floats.count==256&&u.weightedFloats.first==2&&u.weightedFloats.count==66);
    for(auto change:std::array<std::pair<size_t,Word>,5>{{{2,1},{6,0x40800000u},
             {14,0xb0010000u},{18,0xa0e42022u},{19,0xf0ff0800u}}}){
        auto invalid=shader;invalid[change.first]=change.second;
        assert(!finish(invalid).weightedFloats.count);
    }
    auto secondWriter=shader;
    secondWriter.insert(secondWriter.end(),{0x0200002eu,0xb00f0000u,0x80e40000u});
    assert(!finish(secondWriter).weightedFloats.count);
    std::puts("PASS weighted relative reads certified only for the single audited address writer");
}
static void tests(){
    auto w=header();mov(w,reg(2,8));mov(w,reg(2,11));auto u=finish(w);
    assert(u.analyzed&&!u.relativeFloat&&u.floats.first==2&&u.floats.count==10&&u.booleans.count==0&&u.integers.count==0);
    // Projection rows remain mandatory even when original shader has no API
    // constant sources. The replay camera gate needs their original values.
    w=header();mov(w,reg(1,0));u=finish(w,1);assert(u.floats.first==4&&u.floats.count==4);
    u=finish(w,2);assert(u.floats.first==2&&u.floats.count==4);
    // Literal bit patterns deliberately resemble constant/address source tokens.
    w=header();w.insert(w.end(),{0x05000051u,dest(2,255),reg(2,250),reg(7,15),reg(14,15),0xffffffffu});
    w.insert(w.end(),{0x05000030u,dest(7,15),reg(2,250),0xffffffffu,0xa0e42000u,0x80000000u});
    w.insert(w.end(),{0x0200002fu,dest(14,15),reg(2,250)});
    mov(w,reg(2,255));mov(w,reg(7,15));mov(w,reg(14,15));u=finish(w);
    assert(u.analyzed&&u.floats.first==2&&u.floats.count==4&&u.booleans.count==0&&u.integers.count==0);
    // External integer/boolean registers are counted separately, not as float.
    w=header();mov(w,reg(7,4));mov(w,reg(7,7));mov(w,reg(14,3));u=finish(w);
    assert(u.analyzed&&u.integers.first==4&&u.integers.count==4&&u.booleans.first==3&&u.booleans.count==1);
    // Matrix macros read registers implicit in their last source operand.
    for(unsigned op=20;op<=24;++op){
        w=header();w.insert(w.end(),{(3u<<24)|op,dest(0,0),reg(1,0),reg(2,40)});u=finish(w);
        unsigned rows=(op==20||op==22)?4u:op==24?2u:3u;
        assert(u.analyzed&&u.floats.first==2&&u.floats.count==40+rows-2);
    }
    // Relative a0 access can reach every float register even when its base is
    // small, negative or a DEF register. BOOL/INT still require no readback.
    w=header();w.insert(w.end(),{0x03000001u,dest(0,0),reg(2,20,true),reg(3,0)});u=finish(w);
    assert(u.analyzed&&u.relativeFloat&&u.floats.first==0&&u.floats.count==256&&u.booleans.count==0&&u.integers.count==0);
    for(unsigned bank=11;bank<=13;++bank){w=header();mov(w,reg(bank,0));full(finish(w));}
    w=header();w.insert(w.end(),{0x03000014u,dest(0,0),reg(1,0),reg(2,254)});full(finish(w));
    w=header();mov(w,reg(2,256));full(finish(w));
    w=header();w.insert(w.end(),{0x01000028u,reg(14,2)});full(finish(w)); // IF unsupported flow.
    w=header();w.insert(w.end(),{0x03000001u,dest(0,0),reg(2,2,true)});full(finish(w)); // Missing relative address.
    w=header();w.insert(w.end(),{0x05000051u,dest(2,2),0,0,0,0});full(finish(w)); // Literal projection.
    w=header();mov(w,reg(2,10));w[1]|=0x10000000u;full(finish(w));
    w={0xfffe0101u,1,dest(0,0),reg(2,33),0xffff};u=analyze(w.data(),w.size(),2);assert(u.analyzed&&!u.relativeFloat&&u.floats.first==2&&u.floats.count==32&&u.booleans.count==0&&u.integers.count==0);
    w={0xfffe0101u,1,dest(0,0),reg(2,31,true),0xffff};u=analyze(w.data(),w.size(),2);assert(u.analyzed&&u.relativeFloat&&u.floats.count==256&&u.booleans.count==0&&u.integers.count==0);
    w={0xfffe0100u,0xffff};full(analyze(w.data(),w.size(),2));full(analyze(nullptr,0,2));
    w=header();mov(w,reg(2,10));w.push_back(0xffff);w.push_back(0);full(analyze(w.data(),w.size(),2));
    // Comments carry arbitrary data including END and source-looking tokens.
    w=header();w.insert(w.end(),{0x0003fffeu,0xffffu,reg(2,255),reg(14,15)});mov(w,reg(2,8));u=finish(w);
    assert(u.analyzed&&u.floats.first==2&&u.floats.count==7&&u.booleans.count==0);
    // Emulate arbitrary bone indices against the full bank requirement.
    std::array<uint32_t,1024> desired{},captured{};for(unsigned i=0;i<1024;++i)desired[i]=i*0x71a79u;
    w=header();w.insert(w.end(),{0x03000001u,dest(0,0),reg(2,20,true),reg(3,0)});u=finish(w);
    for(unsigned i=u.floats.first*4;i<(u.floats.first+u.floats.count)*4;++i)captured[i]=desired[i];
    for(int offset=-20;offset<236;++offset)for(unsigned component=0;component<4;++component)
        assert(captured[unsigned(20+offset)*4+component]==desired[unsigned(20+offset)*4+component]);
    std::puts("PASS explicit/relative/matrix float reads, mandatory projection, local DEF literals, bool/int banks and unknown-format full fallbacks");
}
static void corpus(const char* path){
    FILE* f=std::fopen(path,"rb");assert(f);uint32_t bytes;
    unsigned total=0,eligible=0,optimized=0,relative=0,fallback=0,boolBanks=0,intBanks=0,terrainSkipped=0,modelPatchRejected=0,modelSm1Rejected=0;
    uint64_t floatCount=0,boolCount=0,intCount=0;
    while(std::fread(&bytes,4,1,f)==1){
        assert(bytes%4==0&&bytes<1024*1024);std::vector<Word> w(bytes/4),patched;assert(std::fread(w.data(),1,bytes,f)==bytes);++total;
        uint64_t hash=14695981039346656037ull;for(unsigned i=0;i<bytes;++i)hash=(hash^reinterpret_cast<const uint8_t*>(w.data())[i])*1099511628211ull;
        auto first=std::begin(kWorldShaderSignatures),last=std::end(kWorldShaderSignatures);
        auto signature=std::lower_bound(first,last,hash,[](const WorldShaderSignature& a,uint64_t h){return a.hash<h;});
        assert(signature!=last&&signature->hash==hash);
        if(signature->projectionKind==1){++terrainSkipped;continue;} // Runtime captures terrain geometry directly.
        NorthlightShadowShader::PatchInfo info;
        if(!NorthlightShadowShader::patch(w.data(),w.size(),patched,&info)||!info.texcoord0XY){++modelPatchRejected;modelSm1Rejected+=((w[0]>>8)&255)==1;continue;}
        ++eligible;auto u=analyze(w.data(),w.size(),2),p=analyze(patched.data(),patched.size(),2);
        assert(u.floats.first==p.floats.first&&u.floats.count==p.floats.count&&u.booleans.first==p.booleans.first&&u.booleans.count==p.booleans.count&&u.integers.first==p.integers.first&&u.integers.count==p.integers.count);
        optimized+=u.analyzed;relative+=u.relativeFloat;fallback+=!u.analyzed;boolBanks+=u.booleans.count>0;intBanks+=u.integers.count>0;
        floatCount+=u.floats.count;boolCount+=u.booleans.count;intCount+=u.integers.count;
    }
    std::fclose(f);assert(eligible>0&&total==terrainSkipped+modelPatchRejected+eligible);
    std::printf("CORPUS total=%u terrainSkipped=%u modelPatchRejected=%u modelSm1Rejected=%u modelEligible=%u analyzed=%u relative=%u fallback=%u BOOLbanks=%u INTbanks=%u avgF=%.3f avgB=%.3f avgI=%.3f captureBytesPercent=%.3f\n",total,terrainSkipped,modelPatchRejected,modelSm1Rejected,eligible,optimized,relative,fallback,boolBanks,intBanks,double(floatCount)/eligible,double(boolCount)/eligible,double(intCount)/eligible,double(floatCount*16+boolCount*4+intCount*16)*100/(eligible*(256.0*16+16*4+16*16)));
    std::puts("PASS original/patched shader constant metadata equivalence for complete eligible corpus");
}
int main(int argc,char**argv){tests();weighted();if(argc>1)corpus(argv[1]);std::puts("All shader constant usage tests passed");}
