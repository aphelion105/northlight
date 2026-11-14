#include <chrono>
#include <cstdint>
struct BoundsProbeClock {
    using Clock=std::chrono::steady_clock;using time_point=Clock::time_point;
    static inline bool frozen=true;
    static time_point now(){return frozen?time_point{}:Clock::now();}
};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK BoundsProbeClock
#define main materialEnvelopeFixtureMain
#include "material_envelope_fast.cpp"
#undef main
#include <cfenv>
namespace NorthlightReplayBounds {
namespace detail {
#include "BeforeKernel.inl"
}
#define EnvelopeCache BeforeEnvelopeCache
#define singleSkinIntervals beforeSingleSkinIntervals
#include "Before.inl"
#undef singleSkinIntervals
#undef EnvelopeCache
}
using Before=B::BeforeEnvelopeCache;
struct Inputs {float bank[1024],inverse[16];};
static Inputs inputs(unsigned frame){
    Inputs in{};pose(in.bank,frame);
    const float angle=frame*.019f;
    in.inverse[0]=in.inverse[5]=std::cos(angle);in.inverse[1]=std::sin(angle);in.inverse[4]=-in.inverse[1];
    in.inverse[10]=in.inverse[15]=1;in.inverse[12]=float(frame%11)*17.f;return in;
}
template<class Cache,class Prepared> static void prepare(Cache& cache,const Prepared& p,const std::shared_ptr<const Mesh>& m,Inputs& in){
    for(unsigned i=0;i<1000;++i){cache.beginFrame(false);B::Budget budget;B::Bounds b;
        if(cache.calculatePrepared(p,*m,m,in.bank,in.inverse,budget,b)==B::Status::Valid)return;}
    assert(false);
}
struct Sample {double ms;unsigned valid=0;std::uint64_t operations=0;};
static volatile float sink;
template<class Cache,class Prepared> static Sample sample(Cache& cache,const Prepared& p,const std::vector<std::shared_ptr<const Mesh>>& sources,const std::vector<Inputs>& poses){
    Sample result{};const auto start=std::chrono::steady_clock::now();
    for(unsigned frame=0;frame<poses.size();++frame){cache.beginFrame(false);B::Budget budget;
        for(unsigned draw=0;draw<16;++draw){const auto& source=sources[(frame*16+draw)%sources.size()];const auto& in=poses[frame];B::Bounds b;
            const auto status=cache.calculatePrepared(p,*source,source,in.bank,in.inverse,budget,b);
            if(status==B::Status::Valid){assert(b.valid);++result.valid;sink=b.low[draw%3];}else assert(!b.valid);
        }result.operations+=budget.operations;
    }
    result.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/poses.size();return result;
}
int main(int argc,char**){
    const bool timing=argc>1;std::vector<Inputs> poses;for(unsigned i=0;i<96;++i)poses.push_back(inputs(i));
    unsigned checked=0;
    for(unsigned mode=0;mode<5;++mode){
        const unsigned groups=mode==0?1:mode==1||mode==4?4:mode==2?32:63;
        const unsigned count=mode==0?2048:mode==1?512:mode==2?128:64;
        const auto p=mode==4?program(ClientShaders::FourBoneVs3):program(ClientShaders::OneBoneVs3);
        auto a=E::prepareProgram(p,decl,4);auto b=Before::prepareProgram(p,decl,4);assert(a&&b);
        E current(true);Before before(true);std::vector<std::shared_ptr<const Mesh>> sources;
        BoundsProbeClock::frozen=true;
        for(unsigned i=0;i<count;++i){sources.push_back(mesh(groups));prepare(current,*a,sources.back(),poses[0]);prepare(before,*b,sources.back(),poses[0]);}
        assert(current.entries()==count&&before.entries()==count);
        // Current-pose values and bounds, including invalid inputs, fallback,
        // palette overrides, all rounding modes and exhausted operation budget.
        for(int rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
            assert(std::fesetround(rounding)==0);
            for(unsigned frame=0;frame<96;++frame){auto in=poses[frame];
                if(frame%17==0)in.bank[124]=std::numeric_limits<float>::denorm_min();
                if(frame%23==0)in.bank[124]=std::numeric_limits<float>::quiet_NaN();
                if(frame%29==0)in.inverse[15]=2;
                current.beginFrame(false);before.beginFrame(false);B::Budget x,y;B::Bounds u,v;
                if(frame%31==0)x.maxOperations=y.maxOperations=0;
                const auto& m=sources[frame%sources.size()];
                const auto sx=current.calculatePrepared(*a,*m,m,in.bank,in.inverse,x,u);
                const auto sy=before.calculatePrepared(*b,*m,m,in.bank,in.inverse,y,v);
                assert(sx==sy&&u.valid==v.valid&&x.operations==y.operations&&x.vertices==y.vertices);
                assert(!std::memcmp(u.low,v.low,12)&&!std::memcmp(u.high,v.high,12));++checked;
            }
        }
        assert(std::fesetround(FE_TONEAREST)==0);
        const auto x=sample(current,*a,sources,poses),y=sample(before,*b,sources,poses);
        assert(x.valid==1536&&y.valid==x.valid&&x.operations==y.operations);
        // Real clocks retain production timing overhead. Never compare a batch
        // that performed less work because its wall-clock budget expired.
        BoundsProbeClock::frozen=false;
        if(timing)for(unsigned block=0;block<24;++block){const bool old=block%4==0||block%4==3;
            const auto r=old?sample(before,*b,sources,poses):sample(current,*a,sources,poses);
            std::printf("BOUNDS_CPU mode=%u groups=%u sources=%u old=%u warm=%u ms=%.9f valid=%u operations=%llu expectedOperations=%llu\n",mode,groups,count,unsigned(old),unsigned(block<4),r.ms,r.valid,(unsigned long long)r.operations,(unsigned long long)x.operations);
        }
    }
    std::printf("BOUNDS_CPU_PASS cases=%u bounds=bitexact work=equal varied_pose=1 rounding=4\n",checked);
}
