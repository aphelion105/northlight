#include "mesh_pool_trim.h"
#include <cassert>
#include <iostream>
using namespace NorthlightMeshPoolTrim;
constexpr uint64_t MiB=1ull<<20;
int main(){
    assert(target(35*MiB)==70*MiB&&surplus(60*MiB,35*MiB)==0&&surplus(159*MiB,35*MiB)==89*MiB);
    // Logged case: ~35 MiB meshes in a 159.5 MiB pool trim on the third commit, then settle.
    {State s;assert(!s.onCommit(159*MiB,35*MiB)&&!s.onCommit(159*MiB,35*MiB)&&s.onCommit(159*MiB,35*MiB));
     for(int i=0;i<20;++i)assert(!s.onCommit(72*MiB,35*MiB));}
    // Exactly at the threshold is not a surplus.
    {State s;for(int i=0;i<10;++i)assert(!s.onCommit(70*MiB+SurplusBytes,35*MiB));}
    // A stable mesh in a right-sized pool never trims.
    {State s;for(int i=0;i<100;++i)assert(!s.onCommit(84*MiB,42*MiB));}
    // Oscillating sizes: the large commit has no surplus and restarts the streak.
    {State s;for(int i=0;i<50;++i){assert(!s.onCommit(170*MiB,35*MiB));assert(!s.onCommit(170*MiB,84*MiB));}}
    // Streak must be consecutive; reset() clears it.
    {State s;assert(!s.onCommit(159*MiB,35*MiB)&&!s.onCommit(159*MiB,35*MiB));assert(!s.onCommit(70*MiB,35*MiB));
     assert(!s.onCommit(159*MiB,35*MiB)&&!s.onCommit(159*MiB,35*MiB)&&s.onCommit(159*MiB,35*MiB));
     assert(!s.onCommit(159*MiB,35*MiB));s.reset();assert(!s.onCommit(159*MiB,35*MiB)&&!s.onCommit(159*MiB,35*MiB));}
    // Pool smaller than the target (growing mesh) is no surplus and does not underflow.
    {State s;assert(!s.onCommit(10*MiB,35*MiB)&&surplus(10*MiB,35*MiB)==0);}
    std::cout<<"ok\n";
}
