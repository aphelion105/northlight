#ifdef NDEBUG
#undef NDEBUG
#endif
#include "replay_constant_ranges.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

using namespace NorthlightReplayConstants;
using NorthlightShaderConstants::Usage;
static uint32_t randomBits(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static uint32_t source(unsigned type,unsigned index,bool relative=false){return NorthlightShadowShader::replaceReg(0x80e40000u,type,index)|(relative?0x2000u:0);}
static uint32_t dest(unsigned type,unsigned index){return NorthlightShadowShader::replaceReg(0x800f0000u,type,index);}
static Usage metadata(std::vector<uint32_t> code,unsigned kind=2){code.push_back(0xffff);return NorthlightShaderConstants::analyze(code.data(),code.size(),kind);}

template<class T,std::size_t Count,std::size_t Width>
static void randomSequence(const char* name){
    static_assert(sizeof(T)==4,"Test patterns are DWORDs");
    std::array<T,Count*Width> desired{},backend{};
    std::array<bool,Count> known{};
    BoundedDirtyRegisters<T,Count,Width> cache,packedCache;
    uint32_t rng=0x91fa79e3;
    uint64_t compared=0,full=0;
    for(unsigned iteration=0;iteration<20000;++iteration){
        if(iteration%307==0){cache.reset();packedCache.reset();known.fill(false);std::memset(backend.data(),0xa5,sizeof backend);}
        for(unsigned edit=0;edit<iteration%9;++edit){auto at=randomBits(rng)%desired.size();auto bits=randomBits(rng);std::memcpy(&desired[at],&bits,4);}
        unsigned first=randomBits(rng)%Count,count=randomBits(rng)%(Count-first+1);
        if(iteration%29==0){first=0;count=Count;} // Relative/fallback full-bank path.
        std::size_t expectedFirst=Count,expectedLast=0;
        for(unsigned i=first;i<first+count;++i)if(!known[i]||std::memcmp(backend.data()+i*Width,desired.data()+i*Width,Width*sizeof(T))){
            if(expectedFirst==Count)expectedFirst=i;expectedLast=i+1;
        }
        auto span=cache.update(desired.data(),{first,count});
        const std::vector<T> packed(desired.begin()+first*Width,desired.begin()+(first+count)*Width);
        const auto packedSpan=packedCache.updateSpan(packed.data(),{first,count});
        assert(span.count==packedSpan.count&&span.first==packedSpan.first);
        if(expectedFirst==Count)assert(span.count==0);
        else assert(span.first==expectedFirst&&span.count==expectedLast-expectedFirst);
        if(span.count){
            assert(span.first>=first&&span.first+span.count<=first+count);
            std::memcpy(backend.data()+span.first*Width,desired.data()+span.first*Width,span.count*Width*sizeof(T));
            for(unsigned i=span.first;i<span.first+span.count;++i)known[i]=true;
        }
        for(unsigned i=first;i<first+count;++i){
            assert(std::memcmp(backend.data()+i*Width,desired.data()+i*Width,Width*sizeof(T))==0);
            assert(std::memcmp(backend.data()+i*Width,packedCache.data()+i*Width,Width*sizeof(T))==0);
        }
        compared+=count*Width*sizeof(T);full+=sizeof desired;
    }
    assert(cache.update(nullptr,{0,0}).count==0);
    std::printf("PASS %s: 20000 range-switch/reset/mutation draws; examined span bytes %.2f%% of full-bank baseline\n",name,double(compared)*100/double(full));
}
static void projectionPreparation(){
    float captured[1024],desired[1024],fullReference[1024],rows[16];
    for(unsigned i=0;i<1024;++i)captured[i]=float(i*3+7);
    for(unsigned i=0;i<16;++i)rows[i]=float(i+10000);
    for(unsigned kind:{1u,2u}){
        auto usage=metadata({0xfffe0200u,0x02000001u,dest(0,0),source(2,12)},kind);
        assert(usage.analyzed);
        for(float& value:desired)value=-991.f;
        std::memcpy(fullReference,captured,sizeof captured);
        if(kind==1){for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)fullReference[16+a*4+b]=rows[b*4+a];}
        else std::memcpy(fullReference+8,rows,sizeof rows);
        assert(prepareFloatConstants(desired,captured,usage,kind,rows));
        for(unsigned i=0;i<1024;++i){
            bool used=i>=usage.floats.first*4&&i<(usage.floats.first+usage.floats.count)*4;
            assert(desired[i]==(used?fullReference[i]:-991.f));
        }
        BoundedDirtyRegisters<float,256,4> cache;
        auto span=cache.update(desired,usage.floats);
        assert(span.first==usage.floats.first&&span.count==usage.floats.count);
        assert(cache.update(desired,usage.floats).count==0);
    }
    Usage invalid;invalid.floats={8,1};assert(!prepareFloatConstants(desired,captured,invalid,2,rows));
    invalid.floats={0,257};assert(!prepareFloatConstants(desired,captured,invalid,2,rows));
    assert(!prepareFloatConstants(desired,captured,Usage{},3,rows));
    std::puts("PASS bounded preparation, mandatory replacement projection, both matrix layouts, untouched unused slots");
}
static void localDefinitionsAndSwitches(){
    auto external=metadata({0xfffe0200u,0x02000001u,dest(0,0),source(2,20),0x02000001u,dest(0,1),source(7,3),0x02000001u,dest(0,2),source(14,4)});
    auto local=metadata({0xfffe0200u,
        0x05000051u,dest(2,20),0x3f800000,0,0,0,
        0x05000030u,dest(7,3),1,2,3,4,
        0x0200002fu,dest(14,4),1,
        0x02000001u,dest(0,0),source(2,20),0x02000001u,dest(0,1),source(7,3),0x02000001u,dest(0,2),source(14,4)});
    assert(external.analyzed&&local.analyzed);
    assert(local.floats.first==2&&local.floats.count==4&&local.booleans.count==0&&local.integers.count==0);
    float values[1024]={};int32_t integers[64]={},booleans[16]={};
    BoundedDirtyRegisters<float,256,4> f;
    BoundedDirtyRegisters<int32_t,16,4> i;
    BoundedDirtyRegisters<int32_t,16,1> b;
    // A uploads its API state. B uses shader-local DEF values, which must NOT
    // change the API-bank cache. A again needs no upload for unchanged values.
    assert(f.update(values,external.floats).count);assert(i.update(integers,external.integers).count);assert(b.update(booleans,external.booleans).count);
    assert(!f.update(values,local.floats).count);assert(!i.update(integers,local.integers).count);assert(!b.update(booleans,local.booleans).count);
    assert(!f.update(values,external.floats).count);assert(!i.update(integers,external.integers).count);assert(!b.update(booleans,external.booleans).count);
    // Changes to captured API constants that B does not use remain deferred
    // until A reads them again, then each bank uploads exactly that register.
    values[80]=9.f;integers[12]=17;booleans[4]=1;
    assert(!f.update(values,local.floats).count);assert(!i.update(integers,local.integers).count);assert(!b.update(booleans,local.booleans).count);
    auto fs=f.update(values,external.floats);auto is=i.update(integers,external.integers);auto bs=b.update(booleans,external.booleans);
    assert(fs.first==20&&fs.count==1&&is.first==3&&is.count==1&&bs.first==4&&bs.count==1);
    // Switching to an uncaptured disjoint range initializes it even when its
    // values happen to match stale stack bytes; switching back preserves A.
    fs=f.update(values,{100,4});assert(fs.first==100&&fs.count==4);
    assert(!f.update(values,external.floats).count);
    std::puts("PASS A->DEF/DEFI/DEFB shader->A, deferred changes in all banks, disjoint newly used ranges");
}
static void relativeAndBitPatterns(){
    auto usage=metadata({0xfffe0200u,0x03000001u,dest(0,0),source(2,20,true),source(3,0)});
    assert(usage.analyzed&&usage.relativeFloat&&usage.floats.first==0&&usage.floats.count==256);
    float values[1024]={};BoundedDirtyRegisters<float,256,4> cache;
    auto span=cache.update(values,usage.floats);assert(span.first==0&&span.count==256);
    uint32_t negativeZero=0x80000000,nan=0x7fc01001;
    std::memcpy(&values[1023],&negativeZero,4);span=cache.update(values,usage.floats);assert(span.first==255&&span.count==1);
    std::memcpy(&values[0],&nan,4);span=cache.update(values,usage.floats);assert(span.first==0&&span.count==1);
    assert(!cache.update(values,usage.floats).count);
    ++nan;std::memcpy(&values[0],&nan,4);span=cache.update(values,usage.floats);assert(span.first==0&&span.count==1);
    std::puts("PASS conservative relative 256-register palette, signed zero, NaN payloads and last bone register");
}
static volatile uint64_t benchmarkSink;
static void benchmark(){
    constexpr unsigned draws=400000;
    float captured[1024]={},desired[1024],rows[16]={};
    Usage usage;usage.analyzed=true;usage.floats={2,26};usage.booleans={};usage.integers={};
    uint64_t checksum=0;
    auto start=std::chrono::steady_clock::now();
    {
        DirtyRegisters<float,256,4> f;DirtyRegisters<int32_t,16,1>b;DirtyRegisters<int32_t,16,4>i;
        int32_t bools[16]={},ints[64]={};
        for(unsigned draw=0;draw<draws;++draw){captured[32]=float(draw/4);std::memcpy(desired,captured,sizeof desired);std::memcpy(desired+8,rows,sizeof rows);checksum+=f.update(desired).count+b.update(bools).count+i.update(ints).count;}
    }
    benchmarkSink=checksum;
    double oldSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    start=std::chrono::steady_clock::now();checksum=0;
    {
        BoundedDirtyRegisters<float,256,4>f;BoundedDirtyRegisters<int32_t,16,1>b;BoundedDirtyRegisters<int32_t,16,4>i;
        int32_t bools[16]={},ints[64]={};
        for(unsigned draw=0;draw<draws;++draw){captured[32]=float(draw/4);assert(prepareFloatConstants(desired,captured,usage,2,rows));checksum+=f.update(desired,usage.floats).count+b.update(bools,usage.booleans).count+i.update(ints,usage.integers).count;}
    }
    benchmarkSink=checksum;
    double boundedSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::printf("CPU BENCH %u representative replay draws (native helper only, not game FPS): full %.6fs, bounded %.6fs; copied float bytes/draw 4096 -> 416\n",draws,oldSeconds,boundedSeconds);
}
int main(){
    projectionPreparation();localDefinitionsAndSwitches();relativeAndBitPatterns();
    randomSequence<float,256,4>("VS float");randomSequence<int32_t,16,1>("VS bool");randomSequence<int32_t,16,4>("VS int");
    randomSequence<uint32_t,65,1>("64-bit validity mask boundary");benchmark();
    std::puts("All bounded replay constant tests passed");
}
