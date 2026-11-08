// Exact typed cold-source decoding against the generic FLOAT4-position path.
#undef NDEBUG
#include <cstdint>
#include <chrono>
struct SourceClock {
    using duration=std::chrono::nanoseconds;using time_point=std::chrono::time_point<SourceClock,duration>;
    static constexpr bool is_steady=true;static inline std::int64_t ticks=0;
    static time_point now(){return time_point(duration(++ticks));}
};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK SourceClock
#include "replay_bounds_job.h"
#include "point_light_shadow.h"
#include "skin_fixture.inc"
#include <array>
#include <algorithm>
#include <limits>
#include <cfenv>

static void weightRounding(){
    const float edge[]={0.f,-0.f,std::numeric_limits<float>::min(),std::numeric_limits<float>::denorm_min(),
        .125f,.5f,1.f,4.f,std::nextafter(16.f,0.f),16.f,std::numeric_limits<float>::max(),INFINITY,NAN,-1.f};
    std::mt19937 rng(2307);size_t checks=0;
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
        assert(std::fesetround(mode)==0);
        for(unsigned n=0;n<120000;++n){Four input[3]={{1,-2,3,1},{},{0,1,2,3}};
            for(unsigned k=0;k<4;++k){
                if(n<38416){unsigned at=n;for(unsigned j=0;j<k;++j)at/=14;input[1][k]=edge[at%14];}
                else if(n<103952)input[1][k]=float((n>>(k%2*8))&255)*(1.f/255.f);
                else {std::uint32_t bits=rng()&0x7fffffffu;std::memcpy(&input[1][k],&bits,4);}
            }
            // Independent original nextafter loop, including its validation
            // order and outward step after a zero weight following a positive.
            double lo=0,hi=0;bool accepted=true;
            for(float w:input[1]){if(!std::isfinite(w)||w<0||(w!=0&&w<std::numeric_limits<float>::min())){accepted=false;break;}
                if(hi+w>0){lo=std::nextafter(lo+double(w),-INFINITY);hi=std::nextafter(hi+double(w),INFINITY);}}
            accepted=accepted&&std::isfinite(hi)&&hi<=16;
            SkinEnvelope actual;assert(actual.add(input)==accepted);
            if(accepted){const double low=std::max(0.,lo);assert(!std::memcmp(&actual.sumLow,&low,8)&&!std::memcmp(&actual.sumHigh,&hi,8));}
            else assert(!actual.usedBones);
            ++checks;
        }
    }
    assert(std::fesetround(FE_TONEAREST)==0);
    std::printf("SKIN_WEIGHT_ROUND_PASS checks=%zu roundingModes=4 signedZero=1 floatExtremes=1 invalid=1 nextafterBits=exact\n",checks);
}

struct SkinSource {
    std::shared_ptr<Mesh> mesh;
    std::array<D3DVERTEXELEMENT9,4> decl;
};
static SkinSource source(unsigned vertices,unsigned weightType,bool generic,bool split,bool shuffled=false){
    SkinSource s;s.mesh=std::make_shared<Mesh>();auto& m=*s.mesh;m.vertexCount=vertices;m.primitiveCount=vertices/3;m.indexed=true;
    m.indices.resize(size_t(m.primitiveCount)*3);for(unsigned i=0;i<m.indices.size();++i)m.indices[i]=i;
    s.decl={{{0,4,std::uint8_t(generic?D3DDECLTYPE_FLOAT4:D3DDECLTYPE_FLOAT3),0,0,0},
             {std::uint16_t(split?1:0),20,std::uint8_t(weightType),0,1,0},{std::uint16_t(split?2:0),36,D3DDECLTYPE_UBYTE4,0,2,0},{0xff,0,17,0,0,0}}};
    for(unsigned stream=0;stream<(split?3u:1u);++stream){m.streams[stream].stride=44+stream*4;m.streams[stream].bytes.resize(size_t(vertices)*m.streams[stream].stride);}
    for(unsigned i=0;i<vertices;++i){
        const float position[]={std::sin(float(i)*.13f),std::cos(float(i)*.07f),float(int(i%19)-9)*.1f,1};
        auto at=[&](unsigned element){const auto& d=s.decl[element];return m.streams[d.Stream].bytes.data()+size_t(i)*m.streams[d.Stream].stride+d.Offset;};
        std::memcpy(at(0),position,16);
        const unsigned char bytes[]={0,63,127,65};
        if(weightType==D3DDECLTYPE_FLOAT4){float weights[4];for(unsigned j=0;j<4;++j)weights[j]=bytes[j]*(1.f/255.f);std::memcpy(at(1),weights,16);}
        else std::memcpy(at(1),bytes,4);
        auto* indices=at(2);for(unsigned j=0;j<4;++j)indices[j]=std::uint8_t((i*(j+1)+j*7)%75);
    }
    if(shuffled)std::swap(s.decl[0],s.decl[2]);return s;
}
static bool equal(const Bounds& a,const Bounds& b){return a.valid==b.valid&&(!a.valid||(!std::memcmp(a.low,b.low,12)&&!std::memcmp(a.high,b.high,12)));}
static Status finishSource(EnvelopeCache& cache,const Program& p,const SkinSource& s,const float* c,const float* inv,Bounds& b,size_t quota=37){
    std::shared_ptr<const Mesh> owner=s.mesh;
    for(unsigned frame=0;frame<20000;++frame){cache.beginFrame(false);Budget work;work.maxVertices=quota;
        const auto status=cache.calculate(p,*owner,owner,s.decl.data(),s.decl.size(),c,inv,work,b);
        assert(work.vertices<=quota&&work.operations<=work.maxOperations);
        if(status!=Status::Budget)return status;assert(!b.valid);
    }assert(false);return Status::Budget;
}
static void verifyVertices(const Program& p,const SkinSource& s,const float* c,const float* inv,const Bounds& b){
    std::vector<Position> vertices;assert(b.valid&&worldPositions(p,*s.mesh,s.decl.data(),s.decl.size(),c,inv,vertices));
    for(const auto& v:vertices){const float at[]={v.x,v.y,v.z};for(unsigned axis=0;axis<3;++axis)assert(at[axis]>=b.low[axis]&&at[axis]<=b.high[axis]);}
}
int main(int argc,char** argv){
    assert(argc>=2);auto p=load(argv[1]);assert(SkinEnvelope::supports(p));
    float c[1024],inv[16]={};for(unsigned i=0;i<4;++i)inv[i*5]=1;palette(c,0);
    if(argc==3){
        // Deterministic internal clock removes scheduler luck from a decoder
        // microbenchmark; only the outer wall clock is measured.
        for(unsigned block=0;block<16;++block){const bool generic=block%4==0||block%4==3;
            auto s=source(32766,D3DDECLTYPE_UBYTE4N,generic,false);const auto prepared=EnvelopeCache::prepareProgram(p,s.decl.data(),4);assert(prepared);
            std::shared_ptr<const Mesh> owner=s.mesh;double total=0;size_t vertices=0;
            for(unsigned repeat=0;repeat<12;++repeat){EnvelopeCache cache;cache.beginFrame(false);Budget work;work.maxVertices=131072;Bounds b;
                const auto start=std::chrono::steady_clock::now();const auto status=cache.calculatePrepared(*prepared,*owner,owner,c,inv,work,b,EnvelopeCache::Stage::Build);
                total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                assert(status==Status::Budget&&!b.valid&&cache.stats().completed==1&&work.vertices==32766);vertices+=work.vertices;
#ifdef NORTHLIGHT_TEST_SKIN_SOURCE_COUNTERS
                assert(cache.stats().skinBuildVertices==32766&&cache.stats().typedSkinBuildVertices==(generic?0u:32766u));
#endif
            }
            std::printf("SKIN_SOURCE_TIMING block=%u generic=%u ms=%.6f vertices=%zu\n",block,unsigned(generic),total,vertices);
        }return 0;
    }
    weightRounding();
    unsigned exact=0;size_t checkedVertices=0;
    for(unsigned weight:{unsigned(D3DDECLTYPE_FLOAT4),unsigned(D3DDECLTYPE_UBYTE4N)})for(bool split:{false,true})for(bool shuffled:{false,true})for(unsigned n:{3u,33u,513u}){
        auto typed=source(n,weight,false,split,shuffled),generic=source(n,weight,true,split,shuffled);EnvelopeCache a,b;
        auto program=p;if(shuffled)std::reverse(program.inputs.begin(),program.inputs.end());assert(SkinEnvelope::supports(program));
        for(unsigned pose=0;pose<6;++pose){palette(c,pose);const float angle=pose*.77f;inv[0]=inv[5]=std::cos(angle);inv[1]=std::sin(angle);inv[4]=-inv[1];inv[12]=10000+pose;inv[13]=-900;
            Bounds x,y;assert(finishSource(a,program,typed,c,inv,x,pose%2?1:37)==Status::Valid);assert(finishSource(b,program,generic,c,inv,y,pose%2?1:37)==Status::Valid);
            assert(equal(x,y));verifyVertices(program,typed,c,inv,x);checkedVertices+=n;++exact;
        }
    }
    for(unsigned i=0;i<16;++i)inv[i]=i%5==0?1.f:0.f;palette(c,0);
    for(unsigned bad=0;bad<11;++bad){
        auto typed=source(33,D3DDECLTYPE_FLOAT4,false,true),generic=source(33,D3DDECLTYPE_FLOAT4,true,true);
        const auto corrupt=[&](SkinSource& s){auto& m=*s.mesh;
            if(bad<4){const float v=bad==0?NAN:bad==1?INFINITY:bad==2?std::numeric_limits<float>::denorm_min():-std::numeric_limits<float>::denorm_min();std::memcpy(m.streams[0].bytes.data()+4,&v,4);}
            else if(bad<8){const float v=bad==4?NAN:bad==5?INFINITY:bad==6?-.1f:std::numeric_limits<float>::denorm_min();std::memcpy(m.streams[1].bytes.data()+20,&v,4);}
            else if(bad==8)m.streams[2].bytes[36]=75;
            else if(bad==9)m.indices.back()=m.vertexCount;
            else m.streams[1].bytes.pop_back();};
        corrupt(typed);corrupt(generic);EnvelopeCache a,b;Bounds x,y;const auto sa=finishSource(a,p,typed,c,inv,x),sb=finishSource(b,p,generic,c,inv,y);
        assert(sa==sb&&(sa==Status::Invalid||sa==Status::Unsupported)&&!x.valid&&!y.valid);
    }
    // Skin-template source errors remain conservative even for packed weights.
    for(unsigned bad:{75u,255u}){auto s=source(33,D3DDECLTYPE_UBYTE4N,false,false);s.mesh->streams[0].bytes[36]=std::uint8_t(bad);EnvelopeCache cache;Bounds b;assert(finishSource(cache,p,s,c,inv,b)==Status::Unsupported&&!b.valid);}
    // The production schedule is exercised at the two live cold-wave sizes,
    // with multiple packets sharing a source and current-pose banks.
    for(unsigned count:{816u,969u}){
        Cache typedCache,genericCache;std::vector<NorthlightReplayBoundsJob::Item> a(count),b(count);
        std::vector<SkinSource> typed,generic;for(unsigned i=0;i<(count+2)/3;++i){typed.push_back(source(33+i%3*32,D3DDECLTYPE_UBYTE4N,false,i%2));generic.push_back(source(33+i%3*32,D3DDECLTYPE_UBYTE4N,true,i%2));}
        for(unsigned i=0;i<count;++i){a[i].owner=typed[i/3].mesh;b[i].owner=generic[i/3].mesh;a[i].prepared=EnvelopeCache::prepareProgram(p,typed[i/3].decl.data(),4);b[i].prepared=EnvelopeCache::prepareProgram(p,generic[i/3].decl.data(),4);a[i].constants=b[i].constants=c;}
        typedCache.beginFrame(false,count);genericCache.beginFrame(false,count);
        const auto x=NorthlightReplayBoundsJob::schedule(typedCache,a,inv),y=NorthlightReplayBoundsJob::schedule(genericCache,b,inv);
        assert(x.valid>0&&x.valid==y.valid&&x.boundOperations<=262144&&y.boundOperations<=262144);
#ifdef NORTHLIGHT_TEST_SKIN_SOURCE_COUNTERS
        assert(typedCache.envelopeStats().typedSkinBuildVertices==x.boundVertices&&typedCache.envelopeStats().skinBuildVertices==x.boundVertices);
        assert(!genericCache.envelopeStats().typedSkinBuildVertices&&genericCache.envelopeStats().skinBuildVertices==y.boundVertices);
#endif
        for(unsigned i=0;i<count;++i)assert(equal(a[i].bounds,b[i].bounds));
        std::printf("SKIN_SOURCE_WAVE_PASS packets=%u valid=%u vertices=%zu operations=%zu\n",count,x.valid,x.boundVertices,x.boundOperations);
    }
    std::printf("SKIN_SOURCE_PASS exactBounds=%u emulatedVertices=%zu malformed=13 splitStreams=1 shuffledInputs=1 partialResume=1\n",exact,checkedVertices);
}
