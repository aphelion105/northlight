#define NOMINMAX
#include <d3d9.h>
#include "legacy_actor_deformation.h"
#include "actor_deformation.h"
#include "actor_client_programs.h"
#include <cassert>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <random>
namespace N=NorthlightActorDeformation;
namespace O=LegacyActorDeformation;
using Mesh=NorthlightDrawSnapshot::Mesh;
static std::mt19937 random(41377);
static float number(){return float(int(random()%20001)-10000)*.0031f;}
static void palette(float* bank){for(unsigned i=0;i<1024;++i)bank[i]=number();bank[0]=3;bank[1]=1;}
__attribute__((noinline)) static bool before(const O::Program& p,const Mesh& mesh,const O::PreparedBindings& bindings,const float* bank,float* lo,float* hi,const float* basis){return O::singleSkinBoundsPrepared(p,mesh,bindings,bank,lo,hi,basis);}
__attribute__((noinline)) static bool after(const N::Program& p,const Mesh& mesh,const N::PreparedBindings& bindings,const float* bank,float* lo,float* hi,const float* basis){return N::singleSkinBoundsPrepared(p,mesh,bindings,bank,lo,hi,basis);}
template<class Words> static void check(const Words& words,const char* name,bool timing){
    N::Program p;O::Program old;assert(N::compile(words,std::size(words),p)&&O::compile(words,std::size(words),old));
    assert(p.kernel.singleSkin());unsigned checks=0;
    for(unsigned count:{0u,1u,71u,256u,8192u}){
        Mesh mesh;mesh.vertexCount=count;mesh.streams[0].stride=32;mesh.streams[1].stride=8;
        mesh.streams[0].bytes.resize(size_t(count)*32);mesh.streams[1].bytes.resize(size_t(count)*8);
        const D3DVERTEXELEMENT9 declaration[]={{0,4,D3DDECLTYPE_FLOAT3,0,0,0},{1,2,D3DDECLTYPE_UBYTE4,0,2,0},D3DDECL_END()};
        N::PreparedBindings bindings;O::PreparedBindings oldBindings;
        assert(N::prepareBindings(p,mesh,declaration,3,bindings)&&O::prepareBindings(old,mesh,declaration,3,oldBindings));
        float bank[1024],basis[16];
        for(unsigned pose=0;pose<36;++pose){palette(bank);for(auto& x:basis)x=number();if(pose==30)basis[8]=INFINITY;if(pose==31)basis[0]=NAN;
            for(unsigned i=0;i<count;++i){const float xyz[]={number(),number(),number()};std::memcpy(mesh.streams[0].bytes.data()+i*32+4,xyz,12);mesh.streams[1].bytes[i*8+2]=uint8_t(pose%3==0?0:pose%3==1?74:random()%75);}
            if(count&&pose>=24){const unsigned vertex=count/2;
                if(pose<28)mesh.streams[1].bytes[vertex*8+2]=uint8_t(75+pose);
                else {const uint32_t special[]={0x7fc12345,0x7f800000,0x80000000,1,0x7f7fffff,0xff7fffff,0xff800000,0x80000001};
                    if(pose%2)std::memcpy(bank+31*4+pose%12,&special[pose-28],4);
                    else std::memcpy(mesh.streams[0].bytes.data()+vertex*32+4,&special[pose-28],4);}}
            for(const float* axes:{static_cast<const float*>(nullptr),static_cast<const float*>(basis)}){
                float lo[3]={FLT_MAX,FLT_MAX,FLT_MAX},hi[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX},expectedLo[3],expectedHi[3];std::memcpy(expectedLo,lo,12);std::memcpy(expectedHi,hi,12);
                const bool a=before(old,mesh,oldBindings,bank,expectedLo,expectedHi,axes),b=after(p,mesh,bindings,bank,lo,hi,axes);
                if(a!=b||std::memcmp(lo,expectedLo,12)||std::memcmp(hi,expectedHi,12))std::printf("BOUNDS_MISMATCH %s count=%u pose=%u basis=%u\n",name,count,pose,unsigned(axes!=nullptr));
                assert(a==b&&!std::memcmp(lo,expectedLo,12)&&!std::memcmp(hi,expectedHi,12));++checks;
            }
        }
        if(timing&&count){
            palette(bank);for(unsigned i=0;i<16;++i)basis[i]=i%5==0?1.f:0.f;basis[0]=basis[5]=.8f;basis[1]=.6f;basis[4]=-.6f;
            for(unsigned i=0;i<count;++i){const float xyz[]={number(),number(),number()};std::memcpy(mesh.streams[0].bytes.data()+i*32+4,xyz,12);mesh.streams[1].bytes[i*8+2]=uint8_t(i%75);}
            for(unsigned world=0;world<2;++world){std::vector<double> samples[2];volatile float checksum=0;const unsigned draws=count<1024?1024:32;
                for(unsigned round=0;round<10;++round)for(unsigned order=0;order<2;++order){const unsigned optimized=(round+order)%2;const auto start=std::chrono::steady_clock::now();
                    for(unsigned draw=0;draw<draws;++draw){bank[31*4+3]=float(draw)*.1f;float lo[3]={FLT_MAX,FLT_MAX,FLT_MAX},hi[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX};
                        assert(optimized?after(p,mesh,bindings,bank,lo,hi,world?basis:nullptr):before(old,mesh,oldBindings,bank,lo,hi,world?basis:nullptr));checksum=checksum+lo[0]+hi[1];}
                    if(round)samples[optimized].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/draws);
                }
                for(auto& s:samples)std::sort(s.begin(),s.end());assert(std::isfinite(checksum));
                std::printf("SINGLE_BOUNDS_TIMING name=%s vertices=%u world=%u oldMs=%.8f newMs=%.8f speedup=%.4f\n",name,count,world,samples[0][4],samples[1][4],samples[0][4]/samples[1][4]);
            }
        }
    }
    std::printf("SINGLE_BOUNDS_PASS name=%s checks=%u bitExact=1 changingPalettes=1 failureBounds=exact\n",name,checks);
}
int main(int argc,char**){std::setvbuf(stdout,nullptr,_IONBF,0);check(ClientShaders::OneBoneVs3,"sm3",argc>1);check(ClientShaders::OneBoneVs3WLast,"w-last",false);check(ClientShaders::OneBoneVs3WMid,"w-middle",false);check(ClientShaders::OneBoneVs2,"sm2",false);}
