#include "static_m2_dedup.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace StaticM2Dedup;
struct P {float x,y,z;};
// A unit quad (two opaque triangles) plus one alpha-tested card, placed at (8,10,2).
static StaticShadow::Model model(){
    StaticShadow::Model m;m.key="doodad";m.contentRevision=3;
    m.vertices={{0,0,0,0,0},{1,0,0,1,0},{1,1,0,1,1},{0,1,0,0,1},{5,5,5,0,0},{6,5,5,0,0},{6,6,5,0,0}};
    m.indices={0,1,2,0,2,3,4,5,6};m.batches={{0,6,0,{0,0,0},{1,1,0}},{6,3,1,{5,5,5},{6,6,5}}};
    m.materials.resize(2);m.materials[0].alphaCutoff=0;m.materials[1].alphaCutoff=.5f;return m;
}
static StaticShadow::Placement placement(){StaticShadow::Placement p;p.uid=9;p.category=1;p.modelKey="doodad";p.translation={8,10,2};p.low={8,10,2};p.high={14,16,7};return p;}
static std::vector<P> quad(){return {{8,10,2},{9,10,2},{9,11,2},{8,11,2}};}
static Facts good(){Facts f;f.rigid=f.placed=f.modelMatches=f.cacheValid=f.boundsInside=true;f.stableFrames=StableFrames;return f;}
int main(){
    // decide: the happy path and each way out, in report order.
    assert(decide(good())==Verdict::Skip);
    {auto f=good();f.rigid=false;assert(decide(f)==Verdict::Skinned);}
    {auto f=good();f.placed=false;assert(decide(f)==Verdict::Unplaced);}
    {auto f=good();f.modelMatches=false;assert(decide(f)==Verdict::ModelMismatch);}
    {auto f=good();f.stableFrames=StableFrames-1;assert(decide(f)==Verdict::Animated);}
    {auto f=good();f.cacheValid=false;assert(decide(f)==Verdict::CacheInvalid);}
    {auto f=good();f.boundsInside=false;assert(decide(f)==Verdict::BoundsOutside);}
    // covers: the model identity proof.
    const auto m=model();const auto place=placement();auto w=quad();
    assert(covers(m,place,w,2));
    w[1].x+=.015f;assert(covers(m,place,w,2));                       // float noise within tolerance
    w=quad();w[1].x+=.1f;assert(!covers(m,place,w,2));                 // a vertex off the model: another model at this origin
    w=quad();assert(!covers(m,place,w,3));                             // more triangles than the opaque batches hold
    std::vector<P> card={{13,15,7},{14,15,7},{14,16,7}};assert(!covers(m,place,card,1)); // the alpha-tested card is not opaque coverage
    w=quad();auto moved=place;moved.translation={20,10,2};assert(!covers(m,moved,w,2));
    w=quad();w[2].z=std::numeric_limits<float>::quiet_NaN();assert(!covers(m,place,w,2));
    assert(!covers(m,place,std::vector<P>{},2)&&!covers(m,place,quad(),0));
    auto broken=m;broken.indices[1]=99;assert(!covers(broken,place,quad(),2));
    assert(!covers(m,place,quad(),2,VertexTolerance,3));              // model vertex cap
    // bounds against the placement's box.
    Vec3 low,high;w=quad();assert(boundsOf(w,low,high)&&inside(place,low,high)&&low.x==8&&high.y==11);
    assert(!inside(place,Vec3(2,10,2),Vec3(9,11,2))&&inside(place,Vec3(7.9f,10,2),Vec3(9,11,2)));
    // Memory: stability counts consecutive frames of one matrix; a move starts a new identity.
    Memory memory;auto mesh=std::make_shared<int>(1);std::shared_ptr<const void> owner=mesh;int shader=0,decl=0;
    float world[12]={1,0,0,0,1,0,0,0,1,8,10,2};
    for(unsigned long long frame=1;frame<=StableFrames+1;++frame){auto& e=memory.touch(owner,&shader,&decl,0,2,world,frame);assert(e.stable==frame-1);}
    assert(memory.size()==1);
    {auto& e=memory.touch(owner,&shader,&decl,0,2,world,StableFrames+1);assert(e.stable==StableFrames);} // once per frame
    world[9]+=.5f;{auto& e=memory.touch(owner,&shader,&decl,0,2,world,StableFrames+2);assert(e.stable==0);} // animated or moved
    assert(memory.size()==2);
    world[9]-=.5f;{auto& e=memory.touch(owner,&shader,&decl,0,2,world,StableFrames+9);assert(e.stable==0);} // a gap in capture frames restarts
    {auto& e=memory.touch(owner,&shader,&decl,3,2,world,StableFrames+9);assert(e.stable==0);}             // another range of the mesh
    // A mesh that died and whose address is reused is a different identity.
    {auto& e=memory.touch(owner,&shader,&decl,0,2,world,StableFrames+10);e.state=Entry::Matched;}
    owner.reset();mesh.reset();auto other=std::make_shared<int>(1);std::shared_ptr<const void> again=other;
    assert(memory.touch(again,&shader,&decl,0,2,world,StableFrames+11).state==Entry::Unverified);
    for(unsigned k=0;k<Memory::Capacity+40;++k){world[9]=float(k)*3;memory.touch(again,&shader,&decl,0,2,world,100+k);}
    assert(memory.size()==Memory::Capacity);
    std::puts("PASS static M2 dedup: decision table, opaque vertex coverage, model mismatch, alpha exclusion, stability memory and capacity");
}
