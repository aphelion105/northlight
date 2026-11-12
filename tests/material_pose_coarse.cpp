#include <chrono>
struct CoarseClock {
    using duration=std::chrono::nanoseconds;using time_point=std::chrono::time_point<CoarseClock,duration>;
    inline static bool live=true;
    static time_point now(){return time_point(live?std::chrono::duration_cast<duration>(std::chrono::steady_clock::now().time_since_epoch()):duration(1000000));}
};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK CoarseClock
// Reuse the real-client shader/layout/source fixtures, without their test main.
#define main material_envelope_fixture_main
#include "material_envelope_fast.cpp"
#undef main
#include <random>
#include <cfenv>
static std::shared_ptr<const Mesh> weighted(unsigned groups,bool randomized=false){
    auto m=std::make_shared<Mesh>(*mesh(groups));std::mt19937 rng(734);
    for(unsigned i=0;i<m->vertexCount;++i){
        float weights[]={.4f,.3f,.2f,.1f};
        if(randomized){float p[3];for(auto& v:p)v=float(int(rng()%20001)-10000)*.002f;std::memcpy(m->streams[0].bytes.data()+32*i,p,12);for(auto& v:weights)v=float(rng()%1001)*.002f;}
        std::memcpy(m->streams[0].bytes.data()+32*i+16,weights,16);
        for(unsigned k=0;k<4;++k)m->streams[0].bytes[32*i+12+k]=(i+k)%groups;
    }return m;
}
static void assertContains(const B::Bounds& coarse,const B::Bounds& tight){
    assert(coarse.valid&&tight.valid);
    for(unsigned k=0;k<3;++k)assert(coarse.low[k]<=tight.low[k]&&coarse.high[k]>=tight.high[k]);
    const float origin[3]={};
    for(float range:{.01f,1.f,8.f,16.f,48.f,100.f})
        if(B::outsideSphere(coarse,origin,range))assert(B::outsideSphere(tight,origin,range));
}
int main(){
    auto p=program(ClientShaders::FourBoneVs3);assert(B::SkinEnvelope::supports(p));
    auto prepared=E::prepareProgram(p,decl,4);assert(prepared);
    float c[1024],inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};pose(c,100);
    unsigned checks=0;
    for(unsigned groups:{1u,4u,16u,75u}){
        auto m=weighted(groups,true);E cache(true);warm(cache,*prepared,m,c,inverse);
        std::mt19937 random(713);
        for(unsigned frame=0;frame<128;++frame)for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
            const int saved=std::fegetround();assert(std::fesetround(mode)==0);
            pose(c,frame+20);const float a=float(frame)*.13f;
            const float cy=std::cos(a),sy=std::sin(a),cz=std::cos(a*.7f),sz=std::sin(a*.7f);
            inverse[0]=cz*cy;inverse[4]=-sz;inverse[8]=cz*sy;inverse[1]=sz*cy;inverse[5]=cz;inverse[9]=sz*sy;inverse[2]=-sy;inverse[6]=0;inverse[10]=cy;
            for(unsigned bone=0;bone<groups;++bone){auto* r=c+4*(31+3*bone);
                r[3]=float(int(random()%200)-100)*.1f;r[7]=float(int(random()%200)-100)*.1f;
                r[11]=20+float(random()%80);}
            cache.beginFrame(false);B::Bounds coarse,tight;B::Budget budget;
            assert(cache.coarsePrepared(*prepared,m,c,inverse,8,coarse));
            assert(cache.phaseNanoseconds(0)==0&&cache.phaseNanoseconds(1)==0&&cache.phaseNanoseconds(2)==0);
            assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,budget,tight)==B::Status::Valid);
            assertContains(coarse,tight);
            std::vector<NorthlightActorDeformation::Position> positions;
            assert(NorthlightActorDeformation::worldPositions(p,*m,decl,4,c,inverse,positions));
            for(const auto& v:positions){const float xyz[]={v.x,v.y,v.z};for(unsigned k=0;k<3;++k)assert(xyz[k]>=coarse.low[k]&&xyz[k]<=coarse.high[k]);}
            assert(std::fesetround(saved)==0);++checks;
        }
        // Source ownership, program changes, unknown/near/invalid palettes fail open.
        cache.beginFrame(false);B::Bounds b;auto other=std::make_shared<Mesh>(*m);
        assert(!cache.coarsePrepared(*prepared,other,c,inverse,8,b)&&!b.valid);
        auto changed=p;changed.definitions.push_back({31,{1,0,0,99}});
        auto different=E::prepareProgram(changed,decl,4);assert(different);
        assert(!cache.coarsePrepared(*different,m,c,inverse,8,b)&&!b.valid);
        warm(cache,*different,m,c,inverse);cache.beginFrame(false);
        assert(!cache.coarsePrepared(*different,m,c,inverse,8,b)&&!b.valid);
        pose(c,0);assert(!cache.coarsePrepared(*prepared,m,c,inverse,8,b)&&!b.valid);
        pose(c,100);c[4*31]=INFINITY;assert(!cache.coarsePrepared(*prepared,m,c,inverse,8,b)&&!b.valid);
        pose(c,100);inverse[15]=2;assert(!cache.coarsePrepared(*prepared,m,c,inverse,8,b)&&!b.valid);inverse[15]=1;
        cache.beginFrame(false);B::Budget budget;
        for(unsigned i=0;i<5000;++i)cache.coarsePrepared(*prepared,m,c,inverse,8,b);
        assert(cache.stats().coarseAttempts<=4096&&cache.stats().coarseNanoseconds>=100000);
        // Optional work cannot exhaust the original evaluation or build allowance.
        assert(cache.canReady()&&cache.canBuild()&&budget.operations==0&&budget.vertices==0);
        assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,budget,b)==B::Status::Valid);
        cache.clear();assert(!cache.coarsePrepared(*prepared,m,c,inverse,8,b)&&!b.valid);
    }
    std::printf("MATERIAL_POSE_COARSE_PASS comparisons=%u rounding_modes=4 current_pose immutable_owner fail_open independent_budget\n",checks);
    // Equal work: all old evaluations have enough operation budget. Early
    // rejects must agree with the old decision; surviving bounds stay bit exact.
#ifdef NORTHLIGHT_COARSE_BENCH
    CoarseClock::live=false;
    std::fill(inverse,inverse+16,0.f);for(unsigned i=0;i<4;++i)inverse[i*5]=1;
    for(unsigned nearPercent:{0u,20u,100u}){
        E cache(true);std::vector<std::shared_ptr<const Mesh>> sources;
        pose(c,100);
        for(unsigned draw=0;draw<512;++draw){sources.push_back(weighted(1u<<(2*((draw/128)%4))));warm(cache,*prepared,sources.back(),c,inverse);}
        double times[2][9];unsigned rejected[2]={},valid[2]={};volatile float sum=0;
        for(unsigned round=0;round<9;++round)for(unsigned turn=0;turn<2;++turn){unsigned variant=(round+turn)%2;
            const auto start=std::chrono::steady_clock::now();
            for(unsigned frame=0;frame<50;++frame){cache.beginFrame(false);B::Budget budget;budget.maxOperations=1u<<30;
                const float a=float(frame)*.013f;inverse[0]=inverse[10]=std::cos(a);inverse[2]=std::sin(a);inverse[8]=-inverse[2];
                for(unsigned draw=0;draw<sources.size();++draw){
                    for(unsigned bone=0;bone<75;++bone)c[4*(31+3*bone)+11]=draw%100<nearPercent?1.f:40.f+float(draw%5);
                    const auto& m=sources[draw];B::Bounds b;const float origin[3]={};
                    if(variant&&cache.coarsePrepared(*prepared,m,c,inverse,8,b)&&B::outsideSphere(b,origin,8)){++rejected[variant];continue;}
                    if(cache.calculatePrepared(*prepared,*m,m,c,inverse,budget,b)==B::Status::Valid){++valid[variant];rejected[variant]+=B::outsideSphere(b,origin,8);sum=b.low[0];}
                }
            }
            times[variant][round]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/50;
        }
        assert(valid[0]==512*50*9&&rejected[0]==rejected[1]);
        for(auto& t:times)std::sort(t,t+9);
        std::printf("MATERIAL_POSE_COARSE_TIMING draws=512 nearPercent=%u beforeMs=%.6f afterMs=%.6f validBefore=%u validAfter=%u rejectedBefore=%u rejectedAfter=%u sum=%g\n",nearPercent,times[0][4],times[1][4],valid[0],valid[1],rejected[0],rejected[1],float(sum));
    }
#endif
}
