// Extracted renderer static-screen methods plus the production registry.
#include "rigid_memory.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
using DWORD=unsigned long;
using namespace NorthlightRigidMemory;
struct Placement {unsigned category=1;std::string modelKey="tree";NorthlightGI::Vec3 translation;float matrix[9]={1,0,0,0,1,0,0,0,1};};
struct Model {};
struct Snapshot {std::string map="test";unsigned long long revision=1,mapGeneration=1;std::vector<Placement> placements;std::map<std::string,std::shared_ptr<Model>> models;};
struct Renderer {
    std::shared_ptr<Snapshot> staticScene=std::make_shared<Snapshot>();
    struct {std::string map="test";} lastRequest;
    NorthlightRigidGeometry::PlacementIndex rigidIndex;
    Registry<int> rigidMemory;
    DWORD rigidMapMs=0;size_t rigidIndexItems=0;double rigidIndexMs=0;
    static constexpr size_t RigidIndexStep=2048;
    bool profileSampled()const{return false;}
    static unsigned long long rigidSceneRevision(const Snapshot& s){return s.revision*1000003ull^s.mapGeneration;}
    /*SCREEN_METHODS*/
};
static Observation observation(){Observation o;o.shape=1;o.draws=1;o.triangles=1;o.bytes=12;o.world[0]=o.world[4]=o.world[8]=1;return o;}
static void step(Registry<int>& r,int covered,unsigned now){
    std::vector<Observation> obs{observation()};float inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},projection[3]={1,1,-1},pivot[3]={};
    r.frame(obs,nullptr,0,now,pivot,inverse,projection,true,[&](size_t){return covered;},[](const float*){return false;});
    for(const auto& o:obs)if(o.action==Observation::Remember)assert(r.store(o,7,now));
}
int main(){
    Renderer r;r.staticScene->placements.push_back({});r.rigidIndexStep();auto o=observation();
    assert(r.rigidStaticCovered(o,20000)==-1); // Metadata is not a loaded model.
    r.staticScene->models["tree"]={};assert(r.rigidStaticCovered(o,20000)==-1);
    r.staticScene->models["tree"]=std::make_shared<Model>();assert(r.rigidStaticCovered(o,20000)==1);
    o.world[9]=1;assert(r.rigidStaticCovered(o,20000)==0);o=observation();
    // A pending decision retries; a previous static decision must retry after revision.
    Tuning t;t.settleMs=0;t.settleFrames=1;t.heldMinFrames=1;r.rigidMemory=Registry<int>(t);
    step(r.rigidMemory,-1,1);assert(r.rigidMemory.screening());
    step(r.rigidMemory,1,2);assert(r.rigidMemory.stats().statics==1&&r.rigidMemory.entries().empty());
    r.staticScene->placements.clear();++r.staticScene->revision;r.rigidIndexStep();
    step(r.rigidMemory,r.rigidStaticCovered(o,20000),3);
    assert(r.rigidMemory.stats().statics==0&&r.rigidMemory.entries().size()==1);
    // Index work on the same revision must not forget a retained payload.
    r.rigidIndexStep();assert(r.rigidMemory.entries().size()==1&&r.rigidMemory.entries()[0].payload==7);
    assert(r.rigidMemory.consistent());
    // Model replacement must wait while the new revision has metadata only.
    r.rigidMemory=Registry<int>(t);r.staticScene->placements.push_back({});++r.staticScene->revision;r.rigidIndexStep();
    step(r.rigidMemory,1,4);assert(r.rigidMemory.stats().statics==1);
    r.staticScene->models.clear();++r.staticScene->revision;r.rigidIndexStep();
    step(r.rigidMemory,r.rigidStaticCovered(o,20000),5);
    assert(r.rigidMemory.stats().statics==0&&r.rigidMemory.screening()&&r.rigidMemory.entries().empty());
    r.staticScene->models["tree"]=std::make_shared<Model>();
    step(r.rigidMemory,r.rigidStaticCovered(o,20000),6);assert(r.rigidMemory.stats().statics==1);
    r.staticScene.reset();r.rigidIndexStep();step(r.rigidMemory,r.rigidStaticCovered(o,20000),7);
    assert(r.rigidMemory.entries().size()==1&&r.rigidMemory.stats().statics==0);
    // Metadata-only matches must not hide another ready placement at this origin.
    r.staticScene=std::make_shared<Snapshot>();r.staticScene->placements.resize(2);
    r.staticScene->placements[1].modelKey="ready";r.staticScene->models["ready"]=std::make_shared<Model>();r.rigidIndexStep();
    assert(r.rigidStaticCovered(o,20000)==1);
    std::puts("PASS pending model, transform mismatch, static revision invalidation, retained payload");
}
