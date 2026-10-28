#include <cstdlib>
#include <new>
#include <cstdio>
static unsigned allocations=0;
void* operator new(size_t n){++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
#include "actor_deformation.h"
#include "actor_reference.h"
#include <cassert>
#include <chrono>
#include <atomic>
using namespace NorthlightActorDeformation;
#include "actor_probe_program.h"
static volatile float sink=0;
__attribute__((noinline)) static void maskTiming(const Program& p,const ActorReference::Program& old,unsigned major,unsigned uv,unsigned split){
    double maskNs[2]={};unsigned maskSums[2]={};
    for(unsigned round=0;round<12;++round)for(unsigned turn=0;turn<2;++turn){unsigned mode=(round+turn)&1;auto start=std::chrono::steady_clock::now();unsigned sum=0;
        for(unsigned mask=0;mask<65536;++mask){std::atomic_signal_fence(std::memory_order_seq_cst);sum+=mode?clearVertexInputsMask(p,mask):ActorReference::clearVertexInputsMask(old,mask);}
        maskSums[mode]+=sum;maskNs[mode]+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/65536;}
    assert(maskSums[0]==maskSums[1]);std::printf("inputMask major=%u uv=%u split=%u oldNs=%.3f newNs=%.3f ratio=%.4f\n",major,uv,split,maskNs[0]/12,maskNs[1]/12,maskNs[1]/maskNs[0]);
}
int main(){
    unsigned cases=0,oldAllocations=0,newAllocations=0;
    for(unsigned major=1;major<=3;++major)for(unsigned uv=0;uv<2;++uv)for(unsigned split=0;split<2;++split){
        auto words=code(major);Program p;ActorReference::Program old;
        assert(compile(words.data(),words.size(),p,uv)&&ActorReference::compile(words.data(),words.size(),old,uv));
        for(unsigned mask=0;mask<65536;++mask)assert(clearVertexInputsMask(p,mask)==ActorReference::clearVertexInputsMask(old,mask));
        maskTiming(p,old,major,uv,split);
        NorthlightDrawSnapshot::Mesh mesh;mesh.vertexCount=24;mesh.streams[0].stride=32;mesh.streams[0].bytes.resize(32*24);
        D3DVERTEXELEMENT9 decl[]={{0,0,2,0,0,0},{0,16,5,0,2,0},{0,24,1,0,5,0},{0xff,0,17,0,0,0}};
        if(split){mesh.streams[1].stride=12;mesh.streams[1].bytes.resize(12*24);decl[1].Stream=decl[2].Stream=1;decl[1].Offset=0;decl[2].Offset=4;}
        for(unsigned v=0;v<24;++v){float xyz[]={v*.23f,2,-3};std::memcpy(mesh.streams[0].bytes.data()+32*v,xyz,12);
            auto* bytes=mesh.streams[split].bytes.data()+v*mesh.streams[split].stride;bytes[split?0:16]=v%4;
            float tex[]={v*.011f,v*.07f};std::memcpy(bytes+(split?4:24),tex,8);}
        float bank[1024]={},inverse[16]={};for(unsigned i=0;i<4;++i)inverse[i*5]=1;inverse[12]=1000;
        for(unsigned bone=0;bone<4;++bone){unsigned r=31+3*bone;bank[4*r]=bank[4*(r+1)+1]=bank[4*(r+2)+2]=1;}
        bank[24]=2;bank[25]=3;bank[28]=.25f;bank[29]=-.5f;
        std::vector<Position> result;std::vector<ActorReference::Position> expected;
        std::vector<std::array<float,2>> tex,expectedTex;
        auto run=[&](bool candidate){return uv?(candidate?textureUVs(p,mesh,decl,4,bank,tex):ActorReference::textureUVs(old,mesh,decl,4,bank,expectedTex)):
            (candidate?worldPositions(p,mesh,decl,4,bank,inverse,result):ActorReference::worldPositions(old,mesh,decl,4,bank,inverse,expected));};
        auto equal=[&]{if(uv)return tex==expectedTex;return result.size()==expected.size()&&(result.empty()||!std::memcmp(result.data(),expected.data(),result.size()*sizeof(Position)));};
        assert(run(false)&&run(true)&&equal());
        for(unsigned pose=0;pose<200;++pose){bank[31*4+3]=pose*.17f;bank[28]=pose*.003f;
            unsigned before=allocations;bool a=run(false);oldAllocations+=allocations-before;before=allocations;bool b=run(true);newAllocations+=allocations-before;assert(a==b&&equal());++cases;}
        double elapsed[2]={};
        for(unsigned round=0;round<12;++round)for(unsigned turn=0;turn<2;++turn){unsigned mode=(round+turn)&1;auto begin=std::chrono::steady_clock::now();
            for(unsigned n=0;n<1000;++n){std::atomic_signal_fence(std::memory_order_seq_cst);assert(run(mode));sink=uv?tex[0][0]:result[0].x;}
            elapsed[mode]+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count()/1000;}
        std::printf("transient major=%u uv=%u split=%u oldNs=%.2f newNs=%.2f ratio=%.4f\n",major,uv,split,elapsed[0]/12,elapsed[1]/12,elapsed[1]/elapsed[0]);
        for(unsigned fault=0;fault<7;++fault){auto saved=decl[uv?2:0];const auto bytes=mesh.streams[split].bytes.size();
            switch(fault){case 0:decl[uv?2:0].Method=1;break;case 1:decl[uv?2:0].Stream=4;break;case 2:decl[uv?2:0].Offset=65535;break;case 3:decl[uv?2:0].Type=17;break;case 4:decl[uv?2:0].Usage=15;break;case 5:mesh.streams[split].bytes.resize(1);break;case 6:bank[31*4]=NAN;bank[24]=NAN;break;}
            bool a=run(false),b=run(true);assert(a==b&&equal());++cases;decl[uv?2:0]=saved;mesh.streams[split].bytes.resize(bytes);}
    }
    assert(!newAllocations);std::printf("PASS transient bindings: cases=%u oldAllocations=%u newAllocations=%u exactPositionUV=1 failures=1 allInputMasks=1\n",cases,oldAllocations,newAllocations);
}
