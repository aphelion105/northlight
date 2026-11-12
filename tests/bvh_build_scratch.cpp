#include "world_gi.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <new>
#include <string>
using namespace NorthlightGI;
static size_t failBytes=0;static unsigned allocationFailures=0;
void* operator new(size_t n){if(n==failBytes){failBytes=0;++allocationFailures;throw std::bad_alloc();}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,size_t)noexcept{std::free(p);}
static uint64_t digest=1469598103934665603ull;
static void bytes(const void* p,size_t n){for(size_t i=0;i<n;++i){digest^=static_cast<const unsigned char*>(p)[i];digest*=1099511628211ull;}}
template<class T> static void add(const T& v){bytes(&v,sizeof v);}
namespace NorthlightGI {
struct BvhBuildOracle {
    static uint64_t tree(const BVH& b){digest=1469598103934665603ull;add(b.root_);add(b.rootLow_);add(b.rootHigh_);
        for(auto i:b.order_)add(i);for(const auto& pair:b.pairs_)add(pair);return digest;}
};
}
static WorldScene synthetic(unsigned kind){WorldScene s;s.materials.push_back({});WorldMaterial cut;cut.width=2;cut.height=1;cut.rgba={255,255,255,0,255,255,255,255};s.materials.push_back(cut);
    unsigned random=31;auto next=[&]{random=random*1664525u+1013904223u;return float(random&65535)/256.f-128.f;};
    const unsigned count=kind==3?37:kind==4?350000:12000;
    for(unsigned i=0;i<count;++i){Vec3 a{next(),next(),next()*.2f};if(kind==1)a.z=0;if(kind==2)a={0,0,0};
        const Vec3 b=a+Vec3(.1f+std::fabs(next())*.02f,0,0),c=a+Vec3(0,.1f+std::fabs(next())*.02f,kind==1?0:.2f);
        const auto base=uint32_t(s.vertices.size());for(auto p:{a,b,c})s.vertices.push_back({p,{0,0,1},.25f,.5f});s.triangles.push_back({base,base+1,base+2,i%2});
        if(i%401==0)s.triangles.push_back({base,base,base,0});
    }return s;}
static WorldScene real(const char* client){WorldScene s;std::string error;std::vector<std::string> tiles;Vec3 center{1328.78f,-3192.43f,167.61f};
    const int x=int(std::floor((17066.6666667-center.y)/533.3333333)),y=int(std::floor((17066.6666667-center.x)/533.3333333));
    for(int j=y-1;j<=y+1;++j)for(int i=x-1;i<=x+1;++i)tiles.push_back(std::string(client)+"/world-cache/Northrend/"+std::to_string(i)+"_"+std::to_string(j)+".fg3");
    assert(loadInstancedScenes(tiles,std::string(client)+"/world-cache/models",center-Vec3(288,288,320),center+Vec3(288,288,320),s,error));return s;}
int main(int argc,char** argv){assert(argc>=3);const unsigned kind=unsigned(std::strtoul(argv[1],nullptr,10));const std::string mode=argv[2];WorldScene s=kind==5?real(argv[3]):synthetic(kind);
    const size_t triangles=s.triangles.size();unsigned refused=0;const auto scratchBytes=triangles*24;
    AllocationAdmission admit=[&](uint64_t n){if(mode=="deny"&&n==scratchBytes){++refused;return false;}return true;};
    if(mode=="oom")failBytes=scratchBytes;
    BVH b;std::string error;const auto start=std::chrono::steady_clock::now();assert(b.build(std::move(s),error,mode!="median",admit));
    const double buildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();assert(error.empty());failBytes=0;
    const auto tree=BvhBuildOracle::tree(b);digest=1469598103934665603ull;unsigned random=197;
    for(unsigned i=0;i<12000;++i){random=random*1664525u+1013904223u;const auto a=b.scene().vertices[random%b.scene().vertices.size()].position;
        const Vec3 from=a+Vec3(0,0,2),direction={.01f,.02f,-1};const auto hit=b.trace(from,direction,.001f,10.f);
        add(hit.hit);add(hit.backFace);add(hit.distance);add(hit.position);add(hit.normal);add(hit.geometricNormal);add(hit.albedo);add(hit.triangle);const bool blocked=b.occluded(from,direction,10.f);add(blocked);
    }
    std::printf("BVH_BUILD_PASS kind=%u mode=%s triangles=%zu tree=%016llx rays=%016llx buildMs=%.6f ownBytes=%zu refused=%u oom=%u\n",kind,mode.c_str(),triangles,(unsigned long long)tree,(unsigned long long)digest,buildMs,b.ownBytes(),refused,allocationFailures);
}
