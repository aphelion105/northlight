#include "replay_constants.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

using NorthlightReplayConstants::DirtyRegisters;
using NorthlightReplayConstants::DirtySpan;

static uint32_t randomBits(uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
template<class T,std::size_t Count,std::size_t Width>
static void backendSequence(const char* name){
    static_assert(sizeof(T)==4,"Test mutations use 32-bit words");
    std::array<T,Count*Width> desired{},backend{};
    DirtyRegisters<T,Count,Width> cache;
    std::memset(backend.data(),0xaa,sizeof backend);
    DirtySpan first=cache.update(desired.data());assert(first.first==0&&first.count==Count);
    std::memcpy(backend.data(),desired.data(),sizeof backend);
    assert(cache.update(desired.data()).count==0);
    uint32_t rng=0x71a9682d;
    uint64_t transferred=0,fullBytes=0;
    for(unsigned iteration=0;iteration<20000;++iteration){
        bool reset=iteration%317==0;
        if(reset){cache.reset();std::memset(backend.data(),0x5a,sizeof backend);}
        unsigned edits=iteration%7==0?0:1+randomBits(rng)%9;
        for(unsigned j=0;j<edits;++j){std::size_t index=randomBits(rng)%desired.size();uint32_t bits=randomBits(rng);std::memcpy(&desired[index],&bits,4);}
        // Independently inspect the emulated GPU bank before applying the
        // helper's transfer. This checks exact coherency across arbitrary
        // payloads, disjoint edits, untouched banks and external invalidation.
        std::size_t expectedFirst=Count,expectedLast=0;
        for(std::size_t reg=0;reg<Count;++reg)if(std::memcmp(backend.data()+reg*Width,desired.data()+reg*Width,Width*sizeof(T))){
            if(expectedFirst==Count)expectedFirst=reg;expectedLast=reg+1;
        }
        DirtySpan span=cache.update(desired.data());
        if(reset)assert(span.first==0&&span.count==Count);
        else if(expectedFirst==Count)assert(span.count==0);
        else assert(span.first==expectedFirst&&span.count==expectedLast-expectedFirst);
        assert(std::size_t(span.first)+span.count<=Count);
        if(span.count)std::memcpy(backend.data()+span.first*Width,desired.data()+span.first*Width,span.count*Width*sizeof(T));
        assert(std::memcmp(backend.data(),desired.data(),sizeof backend)==0);
        transferred+=span.count*Width*sizeof(T);fullBytes+=sizeof backend;
    }
    std::printf("PASS %s: 20000 backend-coherent random draws, reset/full-bank restoration, %.2f%% bytes of unconditional upload\n",name,double(transferred)*100/double(fullBytes));
}
static void specialFloats(){
    DirtyRegisters<float,256,4> cache;float values[1024]={};cache.update(values);
    uint32_t minusZero=0x80000000;std::memcpy(&values[4*17+2],&minusZero,4);
    auto span=cache.update(values);assert(span.first==17&&span.count==1);assert(cache.update(values).count==0);
    uint32_t nan=0x7fc01001;std::memcpy(&values[4*7],&nan,4);
    span=cache.update(values);assert(span.first==7&&span.count==1);assert(cache.update(values).count==0);
    nan=0x7fc01002;std::memcpy(&values[4*7],&nan,4);span=cache.update(values);assert(span.first==7&&span.count==1);
    values[0]=1;values[1023]=2;span=cache.update(values);assert(span.first==0&&span.count==256);
    values[4*20]=3;values[4*23+3]=4;span=cache.update(values);assert(span.first==20&&span.count==4);
    cache.reset();span=cache.update(values);assert(span.first==0&&span.count==256);
    std::puts("PASS NaN payloads, signed zero, unchanged NaNs, edge registers and minimal bounding span");
}
static void directSpans(){
    using NorthlightReplayConstants::Detail::knownFloatSpan;
    assert(knownFloatSpan(nullptr,nullptr,0).count==0);
    unsigned cases=0;
    for(unsigned offset=0;offset<4;++offset)for(unsigned count=1;count<=256;++count){
        std::vector<float> first(count*4+offset),second(count*4+offset);
        float* a=first.data()+offset;float* b=second.data()+offset;
        assert(knownFloatSpan(a,b,count).count==0);
        for(unsigned byte=0;byte<count*16;++byte){
            auto* bytes=reinterpret_cast<unsigned char*>(b);bytes[byte]^=0x80;
            const auto span=knownFloatSpan(a,b,count);
            assert(span.first==byte/16&&span.count==1);
            bytes[byte]^=0x80;++cases;
        }
        for(unsigned reg=0;reg<count;++reg){
            auto* bytes=reinterpret_cast<unsigned char*>(b);bytes[reg*16]^=0x5a;bytes[(count-1)*16+15]^=0xa5;
            const auto span=knownFloatSpan(a,b,count);
            assert(span.first==reg&&span.count==count-reg);
            bytes[reg*16]^=0x5a;bytes[(count-1)*16+15]^=0xa5;++cases;
        }
    }
    std::printf("PASS %u exact-span cases, every byte/register, counts1..256 and four SSE alignments\n",cases);
}
static void staticMatrixBenchmark(){
    constexpr unsigned Draws=200000;
    DirtyRegisters<float,256,4> cache;float desired[1024]={};
    uint64_t actualBytes=0,updates=0;
    auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<Draws;++i){
        // Four world-matrix rows change for one draw in four; the other draws
        // use identical complete banks. Projection has already been injected.
        if(i%4==0)for(unsigned component=0;component<16;++component)desired[32+component]=float(i+component);
        auto span=cache.update(desired);actualBytes+=uint64_t(span.count)*4*sizeof(float);updates+=span.count!=0;
    }
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    uint64_t fullBytes=uint64_t(Draws)*sizeof desired;
    uint64_t expected=sizeof desired+uint64_t(Draws/4-1)*16*sizeof(float);
    assert(actualBytes==expected&&updates==Draws/4);
    std::printf("BENCH %u static-matrix replay draws: %.3f MiB dirty uploads vs %.3f MiB full uploads (%.3f%%), %llu upload calls, native helper %.6fs\n",
        Draws,double(actualBytes)/(1024*1024),double(fullBytes)/(1024*1024),double(actualBytes)*100/double(fullBytes),static_cast<unsigned long long>(updates),seconds);
}
int main(){specialFloats();directSpans();backendSequence<float,256,4>("VS float256 vec4");backendSequence<int32_t,16,1>("VS bool16 DWORD");backendSequence<int32_t,16,4>("VS int16 vec4");staticMatrixBenchmark();std::puts("All replay constant tests passed");}
