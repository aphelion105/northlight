#include "mesh_pool_trim.h"
#include <cassert>
#include <iostream>
using namespace NorthlightMeshPoolTrim;
constexpr uint64_t MiB=1ull<<20;
// Owner model: the pool grows to twice the committed mesh, then releases what the state asks for.
struct Sim {
    State s;uint64_t pool=0,maxStep=0;unsigned trims=0;
    uint64_t commit(uint64_t mesh){
        if(pool<2*mesh)pool=2*mesh;
        const uint64_t r=s.onCommit(pool,mesh);
        if(r){assert(r<=MaxReleaseBytes&&r<=pool);pool-=r;++trims;if(r>maxStep)maxStep=r;}
        return r;
    }
};
int main(){
    assert(target(35*MiB)==70*MiB&&surplus(60*MiB,35*MiB)==0&&surplus(159*MiB,35*MiB)==89*MiB);
    // Logged case: ~35 MiB meshes in a 159 MiB pool: armed on the third commit, then 32 MiB per commit.
    {State s;assert(!s.onCommit(159*MiB,35*MiB)&&!s.onCommit(159*MiB,35*MiB));
     assert(s.onCommit(159*MiB,35*MiB)==MaxReleaseBytes);                       // 89 MiB surplus, capped
     assert(s.onCommit(127*MiB,35*MiB)==MaxReleaseBytes);                       // streak kept between partial trims
     assert(s.onCommit(95*MiB,35*MiB)==9*MiB+16*MiB);                           // 25 MiB surplus left: released whole
     assert(s.onCommit(70*MiB,35*MiB)==0);
     for(int i=0;i<20;++i)assert(s.onCommit(70*MiB,35*MiB)==0);}
    // Exactly at the threshold is not a surplus.
    {State s;for(int i=0;i<10;++i)assert(!s.onCommit(70*MiB+SurplusBytes,35*MiB));}
    // A stable mesh in a right-sized pool never trims.
    {State s;for(int i=0;i<100;++i)assert(!s.onCommit(84*MiB,42*MiB));}
    // Alternating every commit never trims (the large mesh stays in the window).
    {Sim m;for(int i=0;i<400;++i)m.commit((i%2?84:35)*MiB);assert(m.trims==0);}
    // Block-alternating sizes: no trim after warm-up (the pool already holds 2x the large mesh).
    {for(const auto& p:{std::pair<unsigned,unsigned>{40,70},std::pair<unsigned,unsigned>{35,80}}){
         const unsigned run=p.first==40?4:3;Sim m;
         for(int i=0;i<400;++i)m.commit(((i/run)%2?p.second:p.first)*MiB);
         assert(m.trims==0);
         assert(m.pool==2*uint64_t(p.second)*MiB);}}
    // One spike, then steady 35 MiB: trims to ~2x35 within a bounded number of commits, never above 32 MiB a step.
    {Sim m;m.commit(84*MiB);assert(m.pool==168*MiB);
     unsigned n=0;uint64_t total=0;
     for(;n<400&&m.pool>2*35*MiB+SurplusBytes;++n){const uint64_t before=m.pool;m.commit(35*MiB);total+=before-m.pool;}
     assert(m.pool<=2*35*MiB+SurplusBytes&&m.pool>=2*35*MiB);
     assert(n>=WindowCommits&&n<=WindowCommits+8);                              // waits for the spike to leave the window
     assert(m.maxStep<=MaxReleaseBytes&&m.maxStep>0&&total<=168*MiB-70*MiB);
     const unsigned trims=m.trims;for(int i=0;i<100;++i)m.commit(35*MiB);assert(m.trims==trims); // settled for good
    }
    // Sustained smaller mesh from the start trims after the streak and the window, not before.
    {Sim m;for(int i=0;i<10;++i)m.commit(70*MiB);for(int i=0;i<int(WindowCommits)-1;++i)m.commit(30*MiB);assert(m.trims==0);
     for(int i=0;i<10;++i)m.commit(30*MiB);assert(m.trims>0&&m.pool<=2*30*MiB+SurplusBytes);}
    // The streak must be consecutive; a commit at or below the threshold disarms it; reset() clears everything.
    {State s;assert(!s.onCommit(159*MiB,35*MiB)&&!s.onCommit(159*MiB,35*MiB));assert(!s.onCommit(70*MiB,35*MiB));
     assert(!s.onCommit(159*MiB,35*MiB)&&!s.onCommit(159*MiB,35*MiB)&&s.onCommit(159*MiB,35*MiB));
     s.reset();assert(!s.onCommit(159*MiB,35*MiB)&&!s.onCommit(159*MiB,35*MiB));
     State t;t.onCommit(10*MiB,84*MiB);t.reset();assert(t.windowMax()==0&&t.count==0);}
    // Pool smaller than the target (growing mesh) is no surplus and does not underflow.
    {State s;assert(!s.onCommit(10*MiB,35*MiB)&&surplus(10*MiB,35*MiB)==0);}
    std::cout<<"ok\n";
}
