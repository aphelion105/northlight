// 0.3.172 rigid memory (rigid_memory.h): settling (time AND capture frames), sticky mobile/held/static,
// identity without the snapshot pointer, absence (off screen and shortfall frames draw, the in-view
// despawn test, range, teleport, clear), seen-again changes, caps with farthest eviction, the rebase
// round trip; the real client one-influence program (from the retired test_persistent_casters: the exact
// template, a Stormwind sign end to end, the static-doodad flood with real world-cache placements).
// Built by test_rigid_memory.py.
#include "rigid_memory.h"
#include "sampled_vertex_cache.h"
#include "replay_bounds.h"
#include "actor_client_programs.h" /* written by test_rigid_memory.py from the tester's client (client_fixtures.py) */
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <random>
using namespace NorthlightRigidMemory;
using NorthlightRigidGeometry::oneBoneTemplate;using NorthlightRigidGeometry::worldBone;using NorthlightRigidGeometry::staticPlacement;using NorthlightRigidGeometry::PlacementIndex;
using NorthlightActorDeformation::Program;
// 0.3.176 (S1): the 0.3.175 index (a hash map of per-cell vectors), verbatim: the reference of the flat one.
class MapPlacementIndex {
    struct Item {float x=0,y=0,z=0;std::uint32_t index=0;};
    std::unordered_map<std::uint64_t,std::vector<Item>> cells_;std::size_t items_=0;
    static std::uint64_t key(long x,long y){return (std::uint64_t(std::uint32_t(x))<<32)|std::uint32_t(y);}
    static long cell(float v){return long(std::floor(v/16.f));}
public:
    const void* scene=nullptr;std::uint64_t revision=0;std::size_t next=0;bool complete=false;
    void reset(const void* s,std::uint64_t r){cells_.clear();items_=0;scene=s;revision=r;next=0;complete=false;}
    void add(float x,float y,float z,std::uint32_t index){if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return;cells_[key(cell(x),cell(y))].push_back({x,y,z,index});++items_;}
    std::size_t size()const{return items_;}
    template<class Match> bool find(const float* root,float tolerance,Match match)const{
        if(!std::isfinite(root[0])||!std::isfinite(root[1]))return false;const long cx=cell(root[0]),cy=cell(root[1]);
        for(long dx=-1;dx<=1;++dx)for(long dy=-1;dy<=1;++dy){auto it=cells_.find(key(cx+dx,cy+dy));if(it==cells_.end())continue;
            for(const auto& i:it->second)if(std::fabs(i.x-root[0])<=tolerance&&std::fabs(i.y-root[1])<=tolerance&&std::fabs(i.z-root[2])<=tolerance&&match(i.index))return true;}
        return false;
    }
};
static std::vector<NorthlightActorDeformation::Word> load(const char* path){
    FILE* f=std::fopen(path,"rb");assert(f);std::fseek(f,0,SEEK_END);long size=std::ftell(f);std::rewind(f);
    std::vector<NorthlightActorDeformation::Word> w(size_t(size)/4);assert(std::fread(w.data(),1,size_t(size),f)==size_t(size));std::fclose(f);return w;}
// The renderer's copy, reduced: a token (released on clear) and the constant bank.
struct Payload {std::shared_ptr<int> token;std::vector<float> constants;unsigned bone=0;};
using Reg=Registry<Payload>;
struct Camera {float inverse[16]={};float projection[3]={1.5f,2.f,-1.f};};
// World camera: rows right, up, back (view z = -forward) and the eye (the renderer's inverse view).
static Camera looking(const double* eye,const double* at){double f[3]={at[0]-eye[0],at[1]-eye[1],at[2]-eye[2]};double n=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);for(double& x:f)x/=n;
    double r[3]={f[1],-f[0],0};n=std::sqrt(r[0]*r[0]+r[1]*r[1]);for(double& x:r)x/=n;const double u[3]={r[1]*f[2]-r[2]*f[1],r[2]*f[0]-r[0]*f[2],r[0]*f[1]-r[1]*f[0]};
    Camera c;for(unsigned k=0;k<3;++k){c.inverse[k]=float(r[k]);c.inverse[4+k]=float(u[k]);c.inverse[8+k]=float(-f[k]);c.inverse[12+k]=float(eye[k]);}c.inverse[15]=1;return c;}
static Camera facing(double x,double y,double z,double dx,double dy){const double eye[3]={x,y,z},at[3]={x+dx,y+dy,z};return looking(eye,at);}
static void rotZ(float angle,float scale,float* M){const float c=std::cos(angle)*scale,s=std::sin(angle)*scale;const float m[9]={c,-s,0,s,c,0,0,0,scale};std::memcpy(M,m,36);}
// W of a model placed with row-major M (column vector) at t: axis a -> column a of M.
static void placed(const float* M,float x,float y,float z,float* W){for(unsigned a=0;a<3;++a)for(unsigned w=0;w<3;++w)W[a*3+w]=M[w*3+a];W[9]=x;W[10]=y;W[11]=z;}
static Observation sign(std::uint64_t shape,float x,float y,float z,float yaw=.3f,std::size_t bytes=4000,unsigned draws=1,unsigned triangles=150){
    Observation o;o.shape=shape;float M[9];rotZ(yaw,1,M);placed(M,x,y,z,o.world);std::memcpy(o.world0,o.world,48);o.hasWorld0=true;o.draws=draws;o.triangles=triangles;o.bytes=bytes;return o;}
static Observation unselected(Observation o){o.selected=false;return o;}
struct Scene {
    Reg reg;unsigned now=1000;std::vector<Observation> obs;std::vector<float> bodies;int covered=0;unsigned injected=0,stored=0,refreshed=0,screens=0;std::shared_ptr<int> token=std::make_shared<int>(0);
    float pivot[3]={0,0,0};Camera away=facing(0,0,2,-1,0),toward=facing(0,0,2,1,0);std::vector<std::array<float,3>> doodads; /* static-cache placement origins (staticBody) */
    explicit Scene(Tuning t=Tuning{}):reg(t){}
    // drawn: bone-0 matrices of game draws this capture frame (markDrawn, the renderer's test at capture).
    void step(std::vector<Observation> list,const Camera& c,bool complete=true,unsigned dt=16,std::vector<std::array<float,12>> drawn={}){now+=dt;obs=std::move(list);
        for(const auto& w:drawn)reg.markDrawn(w.data(),[](const Reg::Entry&){return true;});
        reg.frame(obs,bodies.data(),bodies.size()/3,now,pivot,c.inverse,c.projection,complete,[&](std::size_t){++screens;return covered;},
            [&](const float* r){for(const auto& d:doodads)if(std::fabs(d[0]-r[0])<=.25f&&std::fabs(d[1]-r[1])<=.25f&&std::fabs(d[2]-r[2])<=.25f)return true;return false;});
        for(const auto& o:obs)if(o.action!=Observation::None&&reg.store(o,Payload{token,{},0},now)){if(o.action==Observation::Remember)++stored;else ++refreshed;}
        injected=0;reg.forAbsent([&](Reg::Entry&){++injected;});}
    // Frames at dt until remembered (or `limit` frames): the count.
    unsigned settle(const Observation& o,const Camera& c,unsigned dt=16,unsigned limit=1000){const auto before=reg.stats().remembered;
        for(unsigned k=1;k<=limit;++k){step({o},c,true,dt);if(reg.stats().remembered>before)return k;}return 0;}
};
static std::array<float,12> w0(const Observation& o){std::array<float,12> w;std::memcpy(w.data(),o.world0,48);return w;}
// Frames at 16 ms until a new track is remembered: the first, then 125 more (2000 ms).
constexpr unsigned Settle=126;
static void settle(){
    // Time AND capture frames AND 8 complete observed frames: 16 ms frames need the whole 2 s; 1 s frames need an 8th frame.
    {Scene s;const auto o=sign(1,20,0,3);assert(s.settle(o,s.away)==Settle&&s.stored==1&&s.reg.stats().remembered==1);}
    {Scene s;const auto o=sign(1,20,0,3);for(unsigned k=0;k<7;++k){s.step({o},s.away,true,1000);assert(s.reg.entries().empty());} /* 7 s, 7 frames: not yet */
     s.step({o},s.away,true,1000);assert(s.reg.entries().size()==1);}
    {Scene s;const auto o=sign(1,20,0,3);for(unsigned k=0;k<40;++k){s.step({o},s.away,true,16);}assert(s.reg.entries().empty());} /* 40 frames, 640 ms: not yet */
    // A creeping object (0.03 yd per frame, below the travel limit) restarts the settle every frame.
    {Scene s;for(unsigned k=0;k<16;++k){s.step({sign(1,20+.03f*k,0,3)},s.away);}for(unsigned k=0;k<16;++k)s.step({sign(1,20.45f,0,3)},s.away);
     assert(s.reg.entries().empty());assert(s.settle(sign(1,20.45f,0,3),s.away)==Settle-17);} /* settles from its stop: the anchor is the 16th frame */
    std::puts("PASS settle: 2000 ms AND 4 capture frames AND 8 complete frames (none alone is enough), motion restarts it");
}
static void sticky(){
    // Mobile: travelled 0.6 yd, then still for 10 s: never remembered. Turned 0.06: likewise.
    {Scene s;for(unsigned k=0;k<=12;++k)s.step({sign(1,20+.05f*k,0,3)},s.away);for(unsigned k=0;k<625;++k)s.step({sign(1,20.6f,0,3)},s.away);
     assert(s.reg.entries().empty()&&s.reg.stats().mobile==1);}
    {Scene s;for(unsigned k=0;k<=6;++k)s.step({sign(1,20,0,3,.3f+.01f*k)},s.away);for(unsigned k=0;k<625;++k)s.step({sign(1,20,0,3,.36f)},s.away);
     assert(s.reg.entries().empty()&&s.reg.stats().mobile==1);}
    // Static: covered once -> never; not covered -> remembered; unknown -> asked again every frame.
    {Scene s;s.covered=1;for(unsigned k=0;k<300;++k)s.step({sign(1,20,0,3)},s.away);assert(s.reg.entries().empty()&&s.reg.stats().statics==1&&s.screens==1);}
    {Scene s;s.covered=-1;for(unsigned k=0;k<200;++k)s.step({sign(1,20,0,3)},s.away);assert(s.reg.entries().empty()&&s.reg.screening()&&s.screens==200-Settle+1);
     s.covered=0;s.step({sign(1,20,0,3)},s.away);assert(s.reg.entries().size()==1&&!s.reg.screening());}
    // Too large: more than 4 draws, 4096 triangles or 256 KiB of mesh.
    for(const auto& o:{sign(1,20,0,3,.3f,4000,5),sign(1,20,0,3,.3f,4000,1,4097),sign(1,20,0,3,.3f,(256u<<10)+1)}){Scene s;assert(!s.settle(o,s.away,16,300));}
    std::puts("PASS sticky: mobile (travel 0.5 yd, turn 0.05), static screen once; small only");
}
// F1: remembered only within 72 yd of the pivot, dropped beyond 80 (hysteresis), no track beyond 80.
static void ranges(){
    {Scene s;for(unsigned k=0;k<500;++k)s.step({sign(1,76,0,0)},s.away);const auto& st=s.reg.stats();
     assert(st.remembered==0&&st.droppedRange==0&&st.rememberGateFar>0&&st.tracks==1&&s.stored==0);} /* no churn */
    {Scene s;assert(s.settle(sign(1,70,0,0),s.away)==Settle);
     for(unsigned k=1;k<=9;++k){s.pivot[0]=-float(k);s.step({sign(1,70,0,0)},s.away);assert(s.reg.entries().size()==1);} /* 79 yd: kept */
     s.pivot[0]=-11;s.step({sign(1,70,0,0)},s.away);assert(s.reg.entries().empty()&&s.reg.stats().droppedRange==1);}
    {Scene s;for(unsigned k=0;k<300;++k)s.step({sign(1,85,0,0),sign(2,0,90,0)},s.away);assert(s.reg.stats().tracks==0&&s.reg.stats().remembered==0);}
    std::puts("PASS ranges: remembered within 72 yd only (76 yd: no churn over 500 frames), kept to 80 yd, no track beyond 80 yd");
}
// F2: held is the share of complete observed frames with a body root within 4 yd (a state), doodad bodies ignored.
static void held(){
    const auto o=sign(1,20,0,3);
    // A still guard's polearm: a body within 3 yd on every frame -> held, never remembered.
    {Scene s;s.bodies={20,3,3};for(unsigned k=0;k<625;++k)s.step({o},s.away);assert(s.reg.stats().remembered==0&&s.reg.stats().held==1);}
    // The same with every other frame a shortfall frame on which the body is not captured: still held.
    // (Counterfactual, must fail: counting the shortfall frames would give a ratio of 0.5 and remember it.)
    {Scene s;for(unsigned k=0;k<625;++k){const bool complete=k%2==0;s.bodies=complete?std::vector<float>{20,3,3}:std::vector<float>{};s.step({o},s.away,complete);}
     assert(s.reg.stats().remembered==0&&s.reg.stats().held==1);}
    // A body passing within 3 yd for 1 s (1.5-2.5 s) during a 4 s observation: remembered.
    {Scene s;for(unsigned k=0;k<250;++k){s.bodies=k>=94&&k<156?std::vector<float>{20,3,3}:std::vector<float>{};s.step({o},s.away);}
     assert(s.reg.stats().remembered==1&&s.reg.entries().size()==1);}
    // An entry with a body parked 2 yd away for 10 s: kept (held never drops), then drawn by us off screen.
    {Scene s;s.settle(o,s.away);s.bodies={20,2,3};for(unsigned k=0;k<625;++k){s.step({o},s.away);assert(s.reg.entries().size()==1);}
     assert(s.reg.stats().held==1);s.bodies.clear();s.step({},s.away);assert(s.injected==1&&s.reg.entries().size()==1);}
    // A body root on a static-cache placement origin (a lantern) holds nothing.
    {Scene s;s.bodies={20,3,3};s.doodads={{20.1f,3,3}};assert(s.settle(o,s.away)==Settle&&s.reg.stats().held==0);}
    // Until the placement index is complete (a static-scene revision change) the doodad counts as a body: held;
    // once the index is complete again (the renderer steps it while tracks exist) the hold decays and it is remembered.
    {Scene s;s.bodies={20,3,3};for(unsigned k=0;k<100;++k)s.step({o},s.away);assert(s.reg.stats().held==1&&s.reg.stats().remembered==0);
     s.doodads={{20.1f,3,3}};unsigned k=0;for(;k<300&&!s.reg.stats().remembered;++k)s.step({o},s.away);assert(s.reg.stats().remembered==1&&s.reg.stats().held==0&&k<60);}
    std::puts("PASS held: >= 80% of >= 8 complete frames (shortfall frames excluded), a passer-by does not hold, an entry is never dropped by a body, doodad bodies ignored");
}
// F3: unsettled, held or mobile tracks are forgotten after 2 s unseen; settled free ones after 60 s.
static void forgetting(){
    {Scene s;unsigned peak=0;for(unsigned f=0;f<625;++f){std::vector<Observation> riders;for(unsigned r=0;r<4;++r){const float t=.05f*float(f)+1.57f*float(r); /* 1.5 yd per frame on a rising circle: never the same spot twice */
             riders.push_back(sign(10+r,30*std::cos(t),30*std::sin(t),.01f*float(f)+2*float(r)));}
         s.step(riders,s.away);peak=std::max<unsigned>(peak,unsigned(s.reg.stats().tracks));}
     assert(peak<=4*(2000/16+2)&&s.reg.stats().tracksForgotten>=1500);std::printf("  riders: 2500 moving observations, peak %u tracks, %llu forgotten\n",peak,(unsigned long long)s.reg.stats().tracksForgotten);}
    {Scene s;s.covered=-1;for(unsigned k=0;k<200;++k)s.step({sign(1,20,0,3)},s.away);assert(s.reg.stats().tracks==1);
     s.step({},s.away,true,30000);assert(s.reg.stats().tracks==1); /* 30 s unseen, settled and free: kept */
     s.covered=0;s.step({sign(1,20,0,3)},s.away);assert(s.reg.entries().size()==1);}
    std::puts("PASS forgetting: moving rigid parts leave the table after 2 s, a settled free track waiting for the screen survives 30 s unseen");
}
static void identity(){
    // The renderer's identity: draw shapes, never the snapshot pointer. A snapshot re-created every 10 frames keeps the entry.
    const int shader=0,decl=0;std::vector<int> snapshots(400);
    auto shape=[&](unsigned frame,bool withPointer){std::uint64_t h=ShapeSeed;mixShape(h,&shader,&decl,268,150,6432);
        if(withPointer){const auto* p=&snapshots[frame/10];h=(h^std::uint64_t(reinterpret_cast<std::uintptr_t>(p)))*1099511628211ull;}return h;};
    {Scene s;unsigned frame=0;for(;frame<400&&s.reg.entries().empty();++frame)s.step({sign(shape(frame,false),20,0,3)},s.away);
     assert(frame==Settle);for(;frame<400;++frame){s.step({sign(shape(frame,false),20,0,3)},s.away);assert(s.reg.entries().size()==1&&s.injected==0);}
     assert(s.stored==1&&s.refreshed==400-Settle);}
    // Counterfactual (must fail): the snapshot pointer in the identity rekeys every 160 ms and never settles.
    {Scene s;for(unsigned frame=0;frame<400;++frame)s.step({sign(shape(frame,true),20,0,3)},s.away);assert(s.reg.entries().empty()&&s.reg.stats().remembered==0&&s.reg.stats().tracks<=14);}
    std::puts("PASS identity: shapes + origin; a re-created snapshot keeps the entry (counterfactual with the pointer: a new track every 160 ms, never remembered)");
}
static void absence(){
    const auto o=sign(1,20,0,3); /* 20 yd in front of the toward camera */
    // Off screen (camera away): drawn for 120 s. In view on shortfall frames: drawn, the clock held.
    {Scene s;s.settle(o,s.away);for(unsigned k=0;k<120;++k){s.step({},s.away,true,1000);assert(s.injected==1);}
     unsigned drawn=0,gated=0;for(unsigned k=0;k<200;++k){const bool complete=false;s.step({},s.toward,complete);drawn+=s.injected;gated+=complete?s.injected:0;}
     assert(drawn==200&&gated==0&&s.reg.entries().size()==1); /* counterfactual (must fail): drawing only on complete frames draws nothing here */
     // NotDrawn in view within 25 yd on complete frames: dropped after 1000 ms AND 6 frames; a shortfall frame in between holds.
     unsigned k=0;for(;k<200&&s.reg.entries().size();++k)s.step({},s.toward,k!=3);
     assert(s.reg.stats().droppedInView==1&&k==64); /* dropped 1008 ms after the first in-view frame (62 complete frames; the shortfall frame holds the count, not the time) */
     // Then a fresh settle: not remembered again before another 2000 ms of observation.
     const unsigned again=s.settle(o,s.toward);assert(again>=Settle-2&&again<=Settle);}
    // NotDrawn in view at 30 yd (beyond 25 from the eye): kept.
    {Scene s;const auto far=sign(1,30,0,3);s.settle(far,s.away);for(unsigned k=0;k<313;++k){s.step({},s.toward);assert(s.injected==1);}assert(s.reg.stats().droppedInView==0);}
    // Out of view on a complete frame resets the clock.
    {Scene s;s.settle(o,s.away);for(unsigned k=0;k<300;++k){s.step({},k%10==9?s.away:s.toward);assert(s.injected==1);}}
    // Range: walking away 1 yd per frame; dropped beyond 80 yd from the pivot.
    {Scene s;s.settle(o,s.away);unsigned k=0;for(;k<200&&s.reg.entries().size();++k){s.pivot[0]=-float(k);s.step({},s.away);}
     assert(s.reg.stats().droppedRange==1&&k==61);} /* at pivot x=-60: |(80,0,3)| = 80.06 > 80 */
    // Teleport: the pivot jumps 150 yd -> everything cleared. Map change: clear() releases the payloads.
    {Scene s;s.settle(o,s.away);assert(s.token.use_count()==2);s.pivot[0]=150;s.step({},s.away);assert(s.reg.entries().empty()&&s.reg.stats().droppedTeleport==1&&s.reg.stats().tracks==0&&s.token.use_count()==1);}
    {Scene s;s.settle(o,s.away);for(unsigned k=0;k<Settle;++k)s.step({sign(2,-20,5,3)},s.away);assert(s.reg.entries().size()==2&&s.token.use_count()==3);s.reg.clear();assert(s.reg.entries().empty()&&s.token.use_count()==1&&s.reg.stats().cleared==2);}
    // Safety timeout: 10 min unseen.
    {Scene s;s.settle(o,s.away);s.step({},s.away,true,599999);assert(s.reg.entries().size()==1);s.step({},s.away,true,2);assert(s.reg.stats().droppedUnseen==1);}
    // Seen again with other axes (a door that opened) or moved within the identity: dropped and mobile for good.
    for(const auto& moved:{sign(1,20,0,3,.3f+.02f),sign(1,20.1f,0,3)}){Scene s;s.settle(o,s.away);s.step({moved},s.away);
        assert(s.reg.entries().empty()&&s.reg.stats().droppedMoved==1&&s.reg.stats().mobile==1);for(unsigned k=0;k<300;++k)s.step({moved},s.away);assert(s.reg.entries().empty());}
    std::puts("PASS absence: off screen and shortfall frames draw, NotDrawn in view within 25 yd dropped after 1000 ms and 6 complete frames then a fresh settle, 30 yd kept, range 80, teleport and clear release everything, 10 min timeout, seen changed -> dropped and mobile");
}
// F4 and the drawn-by-the-game states (markDrawn at capture).
static void states(){
    const auto o=sign(1,10,0,3); /* 10 yd in view of the toward camera */
    // LiveUnselected: selection kept none of its draws for 20 frames in view: neither drawn by us nor dropped.
    {Scene s;s.settle(o,s.away);for(unsigned k=0;k<80;++k){s.step({unselected(o)},s.toward);assert(s.injected==0&&s.reg.entries().size()==1&&s.reg.stats().liveUnselected==1);}
     assert(s.stored==1&&s.refreshed==0);} /* no copy for an unselected group */
    // DrawnNotCaptured: the game drew it (blend fade-in on complete frames, or the budget on shortfall frames) for 5 s in view: drawn by us, never dropped.
    for(bool complete:{true,false}){Scene s;s.settle(o,s.away);
        for(unsigned k=0;k<313;++k){s.step({},s.toward,complete,16,{w0(o)});assert(s.injected==1&&s.reg.stats().drawnNotCaptured==1);}assert(s.reg.stats().droppedInView==0);}
    // DrawnMoved: drawn with bone 0 turned 30 degrees (a door opened): dropped at once, mobile.
    {Scene s;s.settle(o,s.away);const auto opened=sign(1,10,0,3,.3f+.5236f);s.step({},s.toward,true,16,{w0(opened)});
     assert(s.reg.entries().empty()&&s.reg.stats().droppedDrawnMoved==1&&s.reg.stats().mobile==1&&s.injected==0);}
    // Drawn elsewhere (another sign of the same shape 2 yd away): not this entry.
    {Scene s;s.settle(o,s.away);s.step({},s.toward,true,16,{w0(sign(1,12,0,3))});assert(s.reg.stats().drawnNotCaptured==0&&s.reg.stats().notDrawn==1);}
    // Without a finite W0 at record time there is no drawn test: NotDrawn, the despawn clock runs.
    {Scene s;auto bad=o;bad.hasWorld0=false;s.settle(bad,s.away);unsigned k=0;for(;k<200&&s.reg.entries().size();++k)s.step({},s.toward,true,16,{w0(o)});
     assert(s.reg.stats().droppedInView==1&&k==64);}
    // The key set: (shader, primitive count) pairs, one probe per draw.
    {DrawKeySet keys;int a=0,b=0;assert(keys.empty()&&!keys.contains(&a,150));keys.add(&a,150);keys.add(&a,150);keys.add(&b,32);
     assert(keys.size()==2&&keys.contains(&a,150)&&keys.contains(&b,32)&&!keys.contains(&a,32)&&!keys.contains(&b,150));
     std::vector<int> many(600);for(auto& x:many)keys.add(&x,1);assert(keys.size()==512);keys.clear();assert(keys.empty()&&!keys.contains(&a,150));}
    std::puts("PASS states: LiveUnselected kept and not drawn, DrawnNotCaptured (complete or shortfall) drawn and never dropped, DrawnMoved dropped at once, W0 missing -> NotDrawn; key set");
}
// D1 (0.3.174): new tracks are sorted and merged into the ordered survivors; only moved positions are
// reindexed. Random sessions (new tracks, forgetting, the track cap, entries, settles, teleports): after
// every frame the table is in strict (shape, serial) order - with unique keys exactly the full sort -, the
// serial index is exact and every entry's track points back at it.
static void trackMerge(){
    unsigned frames=0,checks=0;
    for(unsigned seed=1;seed<=12;++seed){std::mt19937 rng(seed);Tuning t;t.maxTracks=seed%3==0?150:2048;Scene s(t);
        // Enter with a real remembered object; random captures alone need not settle a track on every standard library.
        assert(s.settle(sign(1000,20,0,3),s.away)&&s.reg.stats().remembered>0);
        std::uniform_int_distribution<unsigned> coin(0,99),shape(1,40);std::uniform_real_distribution<float> at(-60,60);
        std::vector<Observation> still;for(unsigned i=0;i<30;++i)still.push_back(sign(shape(rng),at(rng),at(rng),float(i%5)));
        for(unsigned f=0;f<700;++f){std::vector<Observation> list;
            for(const auto& o:still)if(coin(rng)<85)list.push_back(o);
            for(unsigned r=coin(rng)%12;r-->0;)list.push_back(sign(shape(rng),at(rng),at(rng),at(rng)*.1f));
            if(coin(rng)<2){s.pivot[0]+=coin(rng)<50?150.f:-150.f;}
            s.step(list,coin(rng)<50?s.away:s.toward,coin(rng)<80,coin(rng)<5?2100u:16u);
            assert(s.reg.consistent());++checks;}
        frames+=700;}
    std::printf("PASS track merge: %u random frames, seeded remembered entry, order and serial links consistent (%u checks)\n",frames,checks);
}

static void caps(){
    // 70 signs at 2..71 yd (both orders): the 64 nearest stay, the farthest go.
    for(bool reversed:{false,true}){Scene s;std::vector<Observation> all;for(unsigned i=0;i<70;++i){const unsigned k=reversed?69-i:i;all.push_back(sign(100+k,-2.f-k,0,2));}
        for(unsigned f=0;f<130;++f)s.step(all,s.toward);
        assert(s.reg.entries().size()==64);float far=0;for(const auto& e:s.reg.entries())far=std::max(far,-e.world[9]);assert(far==65);
        assert(s.reg.stats().evicted==(reversed?6u:0u));for(unsigned f=0;f<30;++f)s.step(all,s.toward);assert(s.reg.stats().remembered==(reversed?70u:64u));}
    // 2 MiB of mesh: eight 256 KiB entries; a nearer ninth evicts the farthest.
    {Scene s;std::vector<Observation> all;for(unsigned i=0;i<8;++i)all.push_back(sign(200+i,-10.f-i,0,3,.3f,256u<<10));for(unsigned f=0;f<130;++f)s.step(all,s.toward);
     assert(s.reg.entries().size()==8&&s.reg.stats().bytes==(2u<<20));all.push_back(sign(300,-5,0,3,.3f,256u<<10));for(unsigned f=0;f<130;++f)s.step(all,s.toward);
     assert(s.reg.entries().size()==8&&s.reg.stats().evicted==1&&s.reg.stats().bytes==(2u<<20));for(const auto& e:s.reg.entries())assert(e.world[9]!=-17);}
    // Tracks: at most 2048 (the oldest without an entry go); unsettled ones forgotten after 2 s unseen.
    {Scene s;std::vector<Observation> crowd;for(unsigned i=0;i<2100;++i)crowd.push_back(sign(1000+i,float(i%50),float(i/50),0));s.step(crowd,s.away);assert(s.reg.stats().tracks==2048);
     s.step({},s.away,true,2000);assert(s.reg.stats().tracks==0);}
    std::puts("PASS caps: 64 entries and 2 MiB with farthest eviction (either arrival order), 2048 tracks");
}
static void rebaseRoundTrip(){
    std::mt19937 rng(172);std::uniform_real_distribution<float> angle(-3.1f,3.1f),pos(-9000,9000),near(-60,60),scale(.4f,2.2f);double worst=0;
    for(unsigned i=0;i<1000;++i){float yaw=angle(rng),pitch=angle(rng)*.45f,roll=angle(rng)*.1f;
        const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
        float iv[16]={};const float f[3]={cy*cp,sy*cp,sp},r0[3]={sy,-cy,0},u0[3]={r0[1]*f[2]-r0[2]*f[1],r0[2]*f[0]-r0[0]*f[2],r0[0]*f[1]-r0[1]*f[0]};
        for(unsigned k=0;k<3;++k){iv[k]=cr*r0[k]+sr*u0[k];iv[4+k]=-sr*r0[k]+cr*u0[k];iv[8+k]=-f[k];}const float eye[3]={pos(rng),pos(rng),pos(rng)*.02f};
        for(unsigned k=0;k<3;++k)iv[12+k]=eye[k];iv[15]=1;
        float M[9],W[12];rotZ(angle(rng),scale(rng),M);placed(M,eye[0]+near(rng),eye[1]+near(rng),eye[2]+near(rng),W);
        float rows[12],back[12];assert(rebase(W,iv,rows)&&worldBone(rows,iv,back));
        for(unsigned k=0;k<12;++k){const double tolerance=1e-5*(k<9?1.0:std::max(1.0,std::fabs(double(W[k]))));const double d=std::fabs(double(back[k])-W[k]);worst=std::max(worst,d/tolerance);assert(d<=tolerance);}}
    float singular[16]={};assert(!rebase(singular+0,singular,singular+4));
    std::printf("PASS rebase round trip: worldBone(rebase(W, view), view) == W for 1000 random views and placements up to 9000 yd (worst %.2f of 1e-5 relative)\n",worst);
}
// ---- real client one-influence program (from the retired test_persistent_casters) ----------
static Program compiled(const std::uint32_t* words,std::size_t n){Program p;assert(NorthlightActorDeformation::compile(words,n,p));return p;}
#define CLIENT_PROGRAM(name) compiled(ClientShaders::name,sizeof ClientShaders::name/4)
static void oneBoneProgram(const char* fourBone){
    const Program variants[]={CLIENT_PROGRAM(OneBoneVs3),CLIENT_PROGRAM(OneBoneVs3WLast),CLIENT_PROGRAM(OneBoneVs3WMid),CLIENT_PROGRAM(OneBoneVs2)};
    for(const auto& p:variants)assert(oneBoneTemplate(p)&&!NorthlightReplayBounds::SkinEnvelope::supports(p)&&p.skinned&&p.paletteBase==31);
    const Program none=CLIENT_PROGRAM(NoBoneVs3);assert(!oneBoneTemplate(none)&&!none.skinned);
    {auto words=load(fourBone);Program four;assert(NorthlightActorDeformation::compile(words.data(),words.size(),four)&&!oneBoneTemplate(four)&&NorthlightReplayBounds::SkinEnvelope::supports(four));}
    // Every altered program is refused: another palette row, register, constant, input, order or an extra move.
    const Program& base=variants[0];unsigned refused=0;
    auto refuse=[&](auto change){Program p=base;change(p);assert(!oneBoneTemplate(p));++refused;};
    refuse([](Program& p){p.operations[5].source[0].token+=1;}); /* dp4 z reads c34 */
    refuse([](Program& p){std::swap(p.operations[4],p.operations[5]);}); /* z before y */
    refuse([](Program& p){p.operations[3].source[0].address=0;}); /* no a0 addressing */
    refuse([](Program& p){p.operations[2].source[0].token=0x80000000u;}); /* mova from r0, mul wrote r1 */
    refuse([](Program& p){p.operations[1].source[0].token=0xa0550000u;}); /* mul by c0.y */
    refuse([](Program& p){for(auto& d:p.definitions)if(d.reg==0)d.value[0]=4;}); /* stride 4 */
    refuse([](Program& p){p.operations.erase(p.operations.begin());}); /* no w */
    refuse([](Program& p){p.operations.push_back(p.operations[0]);}); /* two w moves */
    refuse([](Program& p){p.inputs.push_back({3,1,0});}); /* a BLENDWEIGHT input */
    refuse([](Program& p){p.paletteBase=34;});refuse([](Program& p){p.positionRegister=1;});refuse([](Program& p){p.major=1;});
    std::printf("PASS one-influence program: 4 real client variants (w move first/last/middle, t=r0/r1, vs_2_0 swapped mul) are the exact template, four-weight/zero-influence are not, %u altered programs refused\n",refused);
}
// The game's palette slot: view * world (model->world M row-major column vector, translation t), as float rows.
static void slot(const Camera& c,const double* eye,const float* M,const float* t,float* rows){
    for(unsigned row=0;row<3;++row){const float* v=c.inverse+4*row;double w=0;
        for(unsigned j=0;j<3;++j){double s=0;for(unsigned k=0;k<3;++k)s+=double(v[k])*M[k*3+j];rows[4*row+j]=float(s);}
        for(unsigned k=0;k<3;++k)w+=double(v[k])*(double(t[k])-eye[k]);rows[4*row+3]=float(w);}}
// A board: one row of `quads` quads, game layout: float3 POSITION, UBYTE4N BLENDWEIGHT (255,0,0,0),
// UBYTE4 BLENDINDICES (0 and junk in the unread lanes).
static std::shared_ptr<NorthlightDrawSnapshot::Mesh> board(unsigned quads,float width,float height){auto m=std::make_shared<NorthlightDrawSnapshot::Mesh>();
    const unsigned nx=quads+1,ny=2;m->vertexCount=nx*ny;m->streams[0].stride=20;m->streams[0].bytes.resize(size_t(m->vertexCount)*20);
    for(unsigned y=0;y<ny;++y)for(unsigned x=0;x<nx;++x){auto* out=m->streams[0].bytes.data()+size_t(y*nx+x)*20;const float p[3]={width*(float(x)/quads-.5f),.05f*(y&1),-height*float(y)/(ny-1)};
        std::memcpy(out,p,12);out[12]=255;out[13]=out[14]=out[15]=0;out[16]=0;out[17]=7;out[18]=3;out[19]=9;}
    m->indexed=true;m->topology=D3DPT_TRIANGLELIST;
    for(unsigned y=0;y+1<ny;++y)for(unsigned x=0;x<quads;++x){const std::uint32_t a=y*nx+x,b=a+1,c=a+nx,d=c+1;for(auto i:{a,b,c,b,d,c})m->indices.push_back(i);}
    m->primitiveCount=unsigned(m->indices.size()/3);return m;}
static const std::vector<D3DVERTEXELEMENT9> gameLayout={{0,0,2,0,0,0},{0,12,8,0,1,0},{0,16,5,0,2,0},{0xff,0,17,0,0,0}};
struct Prop {std::uint64_t key=0;float M[9]={},t[3]={};std::shared_ptr<NorthlightDrawSnapshot::Mesh> mesh;bool placed=false;unsigned appear=0,screens=0;bool remembered=false;};
// Palette slot 0 = view * world, every other slot stale data (another model's bones).
static void propConstants(const Prop& p,const Camera& c,const double* eye,std::mt19937& rng,std::vector<float>& constants){
    constants.assign(1024,0.f);std::uniform_real_distribution<float> junk(-50,50);for(unsigned r=34;r<256;++r)for(unsigned k=0;k<4;++k)constants[4*r+k]=junk(rng);
    constants[0]=3;constants[1]=1;slot(c,eye,p.M,p.t,constants.data()+4*31);}
// rigidMemoryObserve for props: the bone (RigidBoneCache, the renderer's rigid bone cache), W (worldBone), the
// shape (mixShape), the static screen (the index), then store() of a copy (the constant bank).
struct PropScene {
    const Program& program;Reg reg;const PlacementIndex& index;const std::vector<std::array<float,12>>& placements;std::vector<Prop>& props;
    std::vector<Observation> obs;std::vector<size_t> who;std::mt19937 rng{172};NorthlightActorDeformation::RigidBoneCache bones;unsigned now=1000,screens=0;
    PropScene(const Program& p,const PlacementIndex& x,const std::vector<std::array<float,12>>& pl,std::vector<Prop>& v):program(p),index(x),placements(pl),props(v){}
    int covered(const Observation& o){if(!index.complete)return -1;return index.find(o.world+9,.05f,[&](std::uint32_t i){return staticPlacement(o.world+9,o.world,placements[i].data()+9,placements[i].data());})?1:0;}
    void frame(const double* eye,const Camera& c,const float* pivot,bool visible=true,unsigned serial=0,unsigned dt=16){
        now+=dt;obs.clear();who.clear();std::vector<std::vector<float>> banks;
        if(visible)for(size_t n=0;n<props.size();++n){auto& p=props[n];if(p.appear>serial)continue;banks.emplace_back();propConstants(p,c,eye,rng,banks.back());
            const float bone=bones.scanMesh(*p.mesh,gameLayout.data(),gameLayout.size());assert(bone==0);
            Observation o;o.shape=ShapeSeed;mixShape(o.shape,reinterpret_cast<const void*>(p.key),&gameLayout,p.mesh->vertexCount,p.mesh->primitiveCount,p.mesh->byteSize());
            o.draws=1;o.triangles=p.mesh->primitiveCount;o.bytes=p.mesh->byteSize();assert(worldBone(banks.back().data()+4*31,c.inverse,o.world));obs.push_back(o);who.push_back(n);}
        reg.frame(obs,nullptr,0,now,pivot,c.inverse,c.projection,true,[&](size_t k){++screens;++props[who[k]].screens;return covered(obs[k]);},[](const float*){return false;});
        for(size_t k=0;k<obs.size();++k)if(obs[k].action!=Observation::None){Payload x;x.constants=banks[k];x.bone=0;if(reg.store(obs[k],std::move(x),now))props[who[k]].remembered=true;}
    }
};
// A Stormwind shop sign drawn with the real one-influence program, the camera orbiting the player the whole
// time (every palette row changes each frame): remembered after 2 s, then drawn 120 s while the camera looks
// away; the copy rebased to each new view places every vertex exactly (the program evaluated on the CPU).
static void realSign(){
    const Program program=CLIENT_PROGRAM(OneBoneVs3);PlacementIndex index;std::vector<std::array<float,12>> placements;int token=0;index.reset(&token,1);index.complete=true;
    std::vector<Prop> props(1);auto& s=props[0];s.key=0x5167;rotZ(.7f,1,s.M);s.t[0]=-8850.31f;s.t[1]=612.74f;s.t[2]=104.2f;s.mesh=board(75,1.6f,1.1f);
    assert(s.mesh->primitiveCount==150); /* the HD sign board: 150 triangles */
    PropScene scene{program,index,placements,props};const float pivot[3]={-8856.f,610.f,98.5f};const double player[3]={-8856,610,99.5};unsigned frames=0;
    auto orbit=[&](double yaw,double* eye){eye[0]=player[0]-12*std::cos(yaw);eye[1]=player[1]-12*std::sin(yaw);eye[2]=player[2]+5;return looking(eye,player);};
    for(double yaw=0;frames<400&&!s.remembered;++frames,yaw+=.02){double eye[3];const Camera c=orbit(yaw,eye);scene.frame(eye,c,pivot);}
    assert(s.remembered&&frames==Settle&&s.screens==1&&scene.reg.stats().remembered==1);
    double worst=0;unsigned drawn=0;
    for(unsigned i=0;i<120;++i){double eye[3];orbit(.5*i,eye);const double back[3]={eye[0]+(eye[0]-player[0]),eye[1]+(eye[1]-player[1]),eye[2]};const Camera c=looking(eye,back);
        scene.frame(eye,c,pivot,false,0,1000); /* 120 s turned away: the game draws nothing */
        scene.reg.forAbsent([&](Reg::Entry& e){++drawn;auto constants=e.payload.constants;float rows[12];assert(rebase(e.world,c.inverse,rows));std::memcpy(constants.data()+4*(31+3*e.payload.bone),rows,48);
            std::vector<NorthlightActorDeformation::Position> world;assert(NorthlightActorDeformation::worldPositions(program,*s.mesh,gameLayout.data(),gameLayout.size(),constants.data(),c.inverse,world));
            for(size_t v=0;v<s.mesh->vertexCount;++v){float x[3];std::memcpy(x,s.mesh->streams[0].bytes.data()+v*20,12);
                const double e3[3]={s.M[0]*double(x[0])+s.M[1]*double(x[1])+s.M[2]*double(x[2])+s.t[0],s.M[3]*double(x[0])+s.M[4]*double(x[1])+s.M[5]*double(x[2])+s.t[1],s.M[6]*double(x[0])+s.M[7]*double(x[1])+s.M[8]*double(x[2])+s.t[2]};
                worst=std::max({worst,std::fabs(world[v].x-e3[0]),std::fabs(world[v].y-e3[1]),std::fabs(world[v].z-e3[2])});}});}
    assert(drawn==120&&worst<5e-3&&scene.reg.entries().size()==1);
    std::printf("PASS real sign: one-influence program, camera orbiting, remembered after %u frames (%u ms), drawn 120 s turned away, rebased vertices within %.4f yd of its placement\n",frames,frames*16,worst);
}
// Static-doodad flood: real Stormwind trade-district / Goldshire placements from the world cache (WMO
// doodads scaled .4-2.2 and ADT doodads) plus rotated/scaled synthetic doodads, all drawn at rest with the
// one-influence program (palette = view * placement): each is screened once and never remembered; the
// server-spawned signs among them, and a spawned crate beside its static twin, are all remembered.
struct RealPlacement {unsigned category;const char* model;float m[9],t[3];};
static const RealPlacement tradeDistrict[]={
#include "stormwind_rigid_placements.inc" /* written by test_rigid_memory.py from the tester's world cache */
};
static void staticFlood(){
    const Program program=CLIENT_PROGRAM(OneBoneVs3);PlacementIndex index;std::vector<std::array<float,12>> placements;int token=0;
    std::vector<Prop> props;std::mt19937 rng(1461);std::uniform_real_distribution<float> spread(-90,90),angle(-3.1f,3.1f),scale(.4f,2.2f),height(90,110);
    const float centre[2]={-8850,620};auto base=board(20,1,1);
    auto add=[&](const float* M,const float* t,bool placed){Prop p;p.key=0x1000+props.size()%37;std::memcpy(p.M,M,36);std::memcpy(p.t,t,12);p.mesh=base;p.placed=placed;props.push_back(p);
        if(placed){std::array<float,12> x;std::memcpy(x.data(),M,36);std::memcpy(x.data()+9,t,12);placements.push_back(x);}};
    for(const auto& r:tradeDistrict){const bool goldshire=r.t[0]<-9000;const float t[3]={goldshire?r.t[0]+610.f:r.t[0],goldshire?r.t[1]+560.f:r.t[1],r.t[2]};add(r.m,t,true);}
    auto flood=[&](unsigned doodads,unsigned signs,unsigned appear){
        for(unsigned i=0;i<doodads;++i){float M[9];rotZ(angle(rng),scale(rng),M);const float t[3]={centre[0]+spread(rng),centre[1]+spread(rng),height(rng)};add(M,t,true);props.back().appear=appear;}
        for(unsigned i=0;i<signs;++i){float M[9];rotZ(angle(rng),1,M);const float t[3]={centre[0]+spread(rng)*.3f,centre[1]+spread(rng)*.3f,104};add(M,t,false);props.back().key=0x9000+props.size();props.back().appear=appear;}};
    flood(180,6,0);flood(200,5,300);
    {float M[9];rotZ(.3f,1.1f,M);const float a[3]={centre[0]+4,centre[1]+3,97},b[3]={a[0]+.8f,a[1]+.6f,97};add(M,a,true);props.back().key=0x7777;props.back().appear=300;
        add(M,b,false);props.back().key=0x7777;props.back().appear=300;}
    index.reset(&token,1);for(size_t i=0;i<placements.size();++i)index.add(placements[i][9],placements[i][10],placements[i][11],std::uint32_t(i));
    PropScene scene{program,index,placements,props};const float pivot[3]={centre[0],centre[1],98};const double player[3]={centre[0],centre[1],99};
    auto orbit=[&](double yaw,double* eye){eye[0]=player[0]-12*std::cos(yaw);eye[1]=player[1]-12*std::sin(yaw);eye[2]=player[2]+5;return looking(eye,player);};
    unsigned frame=0;for(;frame<1500;++frame){index.complete=frame>=150;double eye[3];const Camera c=orbit(.01*frame,eye);scene.frame(eye,c,pivot,true,frame);
        bool all=frame>=300;for(const auto& p:props)all=all&&(p.placed||p.remembered);if(all)break;}
    size_t statics=0,signs=0,far=0; /* placed props beyond 72 yd of the pivot are never screened (0.3.173: the remember gate; no track beyond 80) */
    for(const auto& p:props){if(p.placed){float q=0;for(unsigned a=0;a<3;++a)q+=(p.t[a]-pivot[a])*(p.t[a]-pivot[a]);const bool inRange=q<=72.f*72.f;
            assert(!p.remembered&&(p.screens>=1)==inRange);if(inRange)++statics;else ++far;}else{assert(p.remembered);++signs;}}
    const auto& st=scene.reg.stats();assert(signs==12&&st.statics==statics&&st.remembered==12&&scene.reg.entries().size()==12);
    const unsigned screens=scene.screens;
    for(unsigned i=0;i<600;++i){double eye[3];const Camera c=orbit(.01*(frame+i),eye);scene.frame(eye,c,pivot,true,frame+i);}
    assert(scene.screens==screens&&scene.reg.entries().size()==12&&scene.reg.stats().statics==statics); /* screened once: never asked again */
    std::printf("PASS static-doodad flood: %zu doodads in range (%zu real world-cache placements, scale .4-2.2) screened and never remembered, %zu beyond 72 yd never screened, 11 signs and a spawned crate beside its static twin remembered (%u frames), %u screen calls\n",statics,sizeof tradeDistrict/sizeof*tradeDistrict,far,frame,screens);
}
// 0.3.176 (S1): the flat placement index against the 0.3.175 map index. Real world-cache placements
// (the trade district and Goldshire fixture) among 30000 synthetic ones (all categories, shared cells,
// negative coordinates across the whole map, non-finite origins), stepped RigidIndexStep (2048) at a time with the
// renderer's category filter; after every step the same size, next and complete, and the same find()
// answer for exact, near, far and non-finite roots with both match users (the static placement and
// "any", the doodad-body test) and a selective one.
static void flatIndex(){
    struct Place {unsigned category;float m[9],t[3];};std::vector<Place> all;std::mt19937 rng(1176);
    for(const auto& r:tradeDistrict){Place p{r.category,{},{}};std::memcpy(p.m,r.m,36);std::memcpy(p.t,r.t,12);all.push_back(p);}
    std::uniform_real_distribution<float> near(-400,400),angle(-3.1f,3.1f),scale(.4f,2.2f);const float centre[2]={-8850,620};
    for(unsigned i=0;i<30000;++i){Place p{unsigned(rng()%5),{},{}};rotZ(angle(rng),scale(rng),p.m);
        const unsigned kind=rng()%100;
        if(kind<60){p.t[0]=centre[0]+near(rng);p.t[1]=centre[1]+near(rng);}
        else if(kind<80){const auto& o=all[rng()%all.size()];p.t[0]=o.t[0]+float(int(rng()%5)-2)*.01f;p.t[1]=o.t[1]+float(int(rng()%5)-2)*16.f;} /* shared and adjacent cells */
        else if(kind<95){p.t[0]=float(int(rng()%34000)-17000);p.t[1]=float(int(rng()%34000)-17000);}
        else{const float bad[]={NAN,INFINITY,-INFINITY};p.t[0]=bad[rng()%3];p.t[1]=rng()%2?bad[rng()%3]:centre[1];}
        p.t[2]=rng()%50?90.f+float(rng()%40):NAN;all.push_back(p);}
    std::shuffle(all.begin()+1,all.end(),rng);
    MapPlacementIndex old;PlacementIndex flat;int token=0;
    old.reset(&token,1);flat.reset(&token,1,all.size());
    auto step=[&](auto& x){for(const size_t end=std::min(all.size(),x.next+2048);x.next<end;++x.next){const auto& place=all[x.next];
        if(place.category==1||place.category==3)x.add(place.t[0],place.t[1],place.t[2],std::uint32_t(x.next));}x.complete=x.next==all.size();};
    size_t queries=0,hits=0,placedHits=0,steps=0;
    while(!old.complete){step(old);step(flat);++steps;
        assert(old.size()==flat.size()&&old.next==flat.next&&old.complete==flat.complete);
        for(unsigned q=0;q<3000;++q){
            float root[3],axes[9];const auto& o=all[rng()%all.size()];const unsigned kind=rng()%6;
            for(unsigned k=0;k<3;++k)root[k]=o.t[k];for(unsigned r=0;r<3;++r)for(unsigned w=0;w<3;++w)axes[r*3+w]=o.m[w*3+r]; /* model axes: matrix columns */
            if(kind==1)for(unsigned k=0;k<3;++k)root[k]+=float(int(rng()%21)-10)*.006f;          /* within / across the tolerance */
            else if(kind==2){root[0]=centre[0]+near(rng);root[1]=centre[1]+near(rng);root[2]=100;}
            else if(kind==3){root[rng()%3]=NAN;}
            else if(kind==4)for(unsigned k=0;k<2;++k)root[k]=std::floor(root[k]/16.f)*16.f+(rng()%2?-.01f:.01f); /* cell borders */
            const float tolerance=rng()%4?.05f:.5f;
            auto placed=[&](std::uint32_t i){return staticPlacement(root,axes,all[i].t,all[i].m);};
            auto any=[](std::uint32_t){return true;};
            const std::uint32_t pick=std::uint32_t(rng()%all.size());auto selective=[&](std::uint32_t i){return i%7==pick%7;};
            const bool a=old.find(root,tolerance,placed),b=old.find(root,tolerance,any),c=old.find(root,tolerance,selective);
            assert(a==flat.find(root,tolerance,placed)&&b==flat.find(root,tolerance,any)&&c==flat.find(root,tolerance,selective));
            ++queries;hits+=a+b+c;placedHits+=a;}
    }
    assert(steps==(all.size()+2047)/2048&&flat.complete&&placedHits>500&&hits>queries/20);
    flat.reset(nullptr,0);assert(flat.size()==0);float root[3]={all[0].t[0],all[0].t[1],all[0].t[2]};assert(!flat.find(root,.05f,[](std::uint32_t){return true;}));
    PlacementIndex grown;grown.reset(&token,2); /* no reservation: the table grows */
    for(size_t i=0;i<all.size();++i)grown.add(all[i].t[0],all[i].t[1],all[i].t[2],std::uint32_t(i));
    for(unsigned q=0;q<2000;++q){const auto& o=all[rng()%all.size()];assert(grown.find(o.t,.05f,[](std::uint32_t){return true;})==(std::isfinite(o.t[0])&&std::isfinite(o.t[1])&&std::isfinite(o.t[2])));}
    std::printf("PASS flat placement index == 0.3.175 map index: %zu placements (%zu real), %zu steps of 2048, %zu queries x 3 match users, %zu hits (%zu static placements)\n",all.size(),sizeof tradeDistrict/sizeof*tradeDistrict,steps,queries,hits,placedHits);
}
int main(int argc,char** argv){
    assert(argc>1);settle();sticky();ranges();held();forgetting();identity();absence();states();trackMerge();caps();rebaseRoundTrip();oneBoneProgram(argv[1]);realSign();staticFlood();flatIndex();
    std::puts("rigid memory: all passed");
}
