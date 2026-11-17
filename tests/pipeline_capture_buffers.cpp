#include "pipeline_capture_fixture.h"
#include <chrono>
#include <cstdlib>
#include <new>
#include <string>

static long failAfter=-1;
static size_t allocations=0;
void* operator new(size_t bytes){
    ++allocations;if(failAfter==0){assert(activeLocks==0);throw std::bad_alloc();}if(failAfter>0)--failAfter;
    auto* p=std::malloc(bytes?bytes:1);if(!p)throw std::bad_alloc();return p;
}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}

static void setup(Device& d,unsigned count=12288){
    for(unsigned s=0;s<2;++s){d.stride[s]=s?12:64;d.vb[s].bytes.resize(d.offset[s]+size_t(count)*d.stride[s]);
        for(size_t i=0;i<d.vb[s].bytes.size();++i)d.vb[s].bytes[i]=uint8_t(i*37+i/17);}
}
static void verify(const Device& d,const Mesh& m,unsigned count,bool up=false){
    assert(m.vertexCount==count);
    for(unsigned s=0;s<(up?1u:2u);++s){const auto& bytes=m.streams[s].bytes;const unsigned extent=s?4:16;
        assert(bytes.size()==size_t(count)*d.stride[s]);const auto copied=bytes.size()-d.stride[s]+extent;
        assert(!std::memcmp(bytes.data(),d.vb[s].bytes.data()+d.offset[s],copied));
        for(size_t i=copied;i<bytes.size();++i)assert(bytes[i]==0);}
}
static void faults(){
    unsigned failures=0;
    for(bool indexed:{false,true})for(long point=0;point<32;++point){
        Device d;setup(d,12);d.indices({0,1,2,3,4,5,6,7,8,9,10,11});Frame f;Mesh m;
        Draw draw{D3DPT_TRIANGLELIST,0,0,12,0,4,indexed};failAfter=point;
        try{assert(f.read(&d,&d.decl,draw,m));}catch(const std::bad_alloc&){++failures;}
        failAfter=-1;assert(activeLocks==0);f.clearFrame();assert(f.read(&d,&d.decl,draw,m));verify(d,m,12);
        assert(d.ib.refs==1&&d.ib.locks==d.ib.unlocks);for(auto& vb:d.vb)assert(vb.refs==1&&vb.locks==vb.unlocks);
    }
    assert(failures>=4);std::printf("PIPELINE_ALLOCATION_PASS failures=%u checkpoints=64 locks=balanced tail=zero\n",failures);
}
static void growth(){
    for(bool up:{false,true}){Device d;setup(d);if(up)d.decl.e.erase(d.decl.e.begin()+1);
        Frame f;Mesh m;Draw draw{D3DPT_TRIANGLELIST,0,0,12288,0,32,false};const auto before=allocations;
        for(unsigned i=0;i<128;++i){draw.primitives=(i+1)*32;f.clearFrame();
            if(up)assert(f.readUP(&d.decl,draw,nullptr,D3DFMT_INDEX16,d.vb[0].bytes.data()+d.offset[0],d.stride[0],m));else assert(f.read(&d,&d.decl,draw,m));
            verify(d,m,draw.primitives*3,up);}
        assert(allocations-before<=32);std::printf("PIPELINE_GROWTH_PASS up=%u captures=128 allocations=%zu exact=1\n",unsigned(up),allocations-before);
    }
}
static void benchmark(const std::string& mode){
    const bool up=mode.find("up-")==0,growing=mode.find("growing")!=std::string::npos,warm=growing||mode.find("warm")!=std::string::npos,indexed=mode=="indexed-growing";
    Device d;setup(d);if(up)d.decl.e.erase(d.decl.e.begin()+1);
    if(indexed){d.ib.desc.Format=D3DFMT_INDEX16;d.ib.bytes.resize(12288*2);for(unsigned i=0;i<12288;++i){uint16_t value=uint16_t(i);std::memcpy(d.ib.bytes.data()+i*2,&value,2);}}
    Frame f;Draw draw{D3DPT_TRIANGLELIST,0,0,12288,0,4096,indexed};Mesh retained;
    uint64_t digest=0;const auto before=allocations;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<128;++i){Mesh fresh;auto& m=warm?retained:fresh;f.clearFrame();
        if(growing)draw.primitives=(i+1)*32;
        if(up)assert(f.readUP(&d.decl,draw,nullptr,D3DFMT_INDEX16,d.vb[0].bytes.data()+d.offset[0],d.stride[0],m));
        else assert(f.read(&d,&d.decl,draw,m));
        digest=digest*33+m.streams[0].bytes[(i*7919)%m.streams[0].bytes.size()]+m.vertexCount;
    }
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("PIPELINE_TIMING stage=%s cpuMs=%.6f digest=%llu allocations=%zu\n",mode.c_str(),ms,(unsigned long long)digest,allocations-before);
}
int main(int argc,char** argv){if(argc>1)benchmark(argv[1]);else{originalSnapshotTests();faults();growth();}}
