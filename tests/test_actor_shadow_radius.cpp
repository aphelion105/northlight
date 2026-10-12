// ActorShadowRadius (actor_shadow_selection.h Radius): off = the 0.3.144
// selection, distance filter with hysteresis, the player's own group (self
// candidates on the centre ray, mount by bounds, attachments) at radius 1 with
// a stale pivot, unknown distances, the quota interplay and the main-thread cost. 0.3.144 itself is compared
// decision for decision by test_actor_shadow_selection.py (decision digest).
#include "actor_shadow_selection.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
using namespace NorthlightActorShadowSelection;
namespace {
// A scene thing: a body (multi-bone, `parts` draws sampled around it, palette
// root at its position) or a rigid single-bone attachment riding on `parent`.
struct Thing {float x=0,y=0,z=0,vx=0,vy=0,ox=0,oy=0,oz=0;unsigned parts=1,model=0;std::size_t bytes=20000;bool rigid=false,known=true;int parent=-1;unsigned tag=0;float spread=.35f;};
enum Tag {Npc=0,Player=1,Mount=2,PlayerPart=3,NpcPart=4,Fence=5};
struct Frame {std::vector<Draw> draws;std::vector<unsigned> owner;};
// Sampled vertex of part k: a fixed spot on the model (per part) plus animation noise.
Frame build(const std::vector<Thing>& things,const float* eye,std::mt19937& random,float noise,float dropout=0){
    std::uniform_real_distribution<float> u(-.5f,.5f),p(0,1);Frame f;unsigned group=0;
    for(unsigned id=0;id<things.size();++id){const auto& t=things[id];if(dropout>0&&p(random)<dropout)continue;
        for(unsigned k=0;k<t.parts;++k){Draw w;w.index=f.draws.size();w.group=group;w.bytes=t.bytes/t.parts;w.known=t.known;
            const float a=1.7f*float(k+t.model),r=t.rigid?0.f:t.spread,n=t.rigid&&t.parent<0?.002f:noise; /* free-standing props: float noise only */
            w.at[0]=t.x+r*std::cos(a)+u(random)*n;w.at[1]=t.y+r*std::sin(a)+u(random)*n;w.at[2]=t.z+(t.rigid?0.f:.25f*float(k%8))+u(random)*n;
            float s=0;for(unsigned c=0;c<3;++c){const float q=w.at[c]-eye[c];s+=q*q;}w.distanceSquared=t.known?s:0; /* the renderer's camera distance */
            w.rigid=t.rigid;w.bone=t.rigid?5.f:NAN;w.key=1000+std::uint64_t(t.model)*16+k; /* instances of one model share keys */
            if(!t.rigid&&k==0){w.hasRoot=true;w.root[0]=t.x;w.root[1]=t.y;w.root[2]=t.z;}
            f.draws.push_back(w);f.owner.push_back(id);}
        ++group;}
    return f;
}
void ride(std::vector<Thing>& things){for(auto& t:things)if(t.parent>=0){const auto& b=things[std::size_t(t.parent)];t.x=b.x+t.ox;t.y=b.y+t.oy;t.z=b.z+t.oz;}}
std::uint64_t digest(const std::vector<Draw>& d,const Scratch& sc,const Result& r,const History& h){
    std::uint64_t x=1469598103934665603ull;auto mix=[&](std::uint64_t v){x=(x^v)*1099511628211ull;};
    for(const auto& w:d)mix(w.keep);
    for(const auto& a:sc.actors)for(std::uint64_t v:{std::uint64_t(a.keep),std::uint64_t(a.rigid),std::uint64_t(a.body),std::uint64_t(a.orphan),std::uint64_t(a.exempt),std::uint64_t(a.waiting),std::uint64_t(a.matched)})mix(v);
    for(std::size_t v:{r.kept,r.dropped,r.keptBytes,r.droppedBytes,r.unknown,r.actors,r.actorsKept,r.rigidActors,r.rigidDraws,r.rigidBytes,r.toggles,r.matched,r.retained,r.admitted,r.waiting,r.reserved,
        r.attached,r.attachedBytes,r.orphans,r.exemptActors,r.exemptBytes,r.locked,h.size()})mix(v);
    return x;
}
std::vector<Thing> crowd(std::mt19937& random,unsigned npcs,float extent,unsigned fences=0){
    std::uniform_real_distribution<float> u(0,1);std::vector<Thing> things;
    for(unsigned i=0;i<npcs;++i){Thing t;const float a=u(random)*6.2832f,r=2+u(random)*extent;t.x=r*std::cos(a);t.y=r*std::sin(a);t.parts=3+random()%6;t.model=random()%9;t.bytes=40000+random()%120000;
        if(i%5==0){t.vx=(u(random)-.5f)*.3f;t.vy=(u(random)-.5f)*.3f;}things.push_back(t);}
    for(unsigned i=0;i<npcs;i+=2){Thing w;w.rigid=true;w.parent=int(i);w.ox=.5f;w.oy=.3f;w.oz=1.1f;w.model=40+random()%3;w.bytes=5000;w.tag=NpcPart;things.push_back(w);}
    for(unsigned i=0;i<fences;++i){Thing t;t.rigid=true;t.tag=Fence;const float a=u(random)*6.2832f,r=2+u(random)*extent;t.x=r*std::cos(a);t.y=r*std::sin(a);t.model=60+random()%4;t.bytes=15000;things.push_back(t);}
    return things;
}
// (a) off: no Radius, yards 0, or a radius that contains everything without an
// eye (no self) give the 0.3.144 decisions frame for frame (ranked frames).
void off(){
    std::size_t frames=0;
    for(unsigned seed=0;seed<12;++seed){std::mt19937 r0(seed),r1(seed),r2(seed);std::mt19937 scene(500+seed);auto things=crowd(scene,120,70,150);
        History h0,h1,h2;Scratch s0,s1,s2;const std::size_t budget=3000000+seed*400000;float pivot[3]={0,0,1.7f},eye[3]={-15,0,8};
        for(unsigned f=0;f<200;++f){for(auto& t:things)if(t.parent<0){t.x+=t.vx;t.y+=t.vy;}ride(things);pivot[0]+=.1f;eye[0]+=.1f;
            auto a=build(things,eye,r0,.4f,seed%3?0:.05f),b=build(things,eye,r1,.4f,seed%3?0:.05f),c=build(things,eye,r2,.4f,seed%3?0:.05f);
            const Radius zero{0,eye,true},all{1e6f,nullptr,true};
            const auto ra=choose(a.draws,s0,budget,h0,f%4==0,pivot,Tuning{});
            const auto rb=choose(b.draws,s1,budget,h1,f%4==0,pivot,Tuning{},&zero);
            const auto rc=choose(c.draws,s2,budget,h2,f%4==0,pivot,Tuning{},&all);
            assert(digest(a.draws,s0,ra,h0)==digest(b.draws,s1,rb,h1)&&digest(a.draws,s0,ra,h0)==digest(c.draws,s2,rc,h2));
            assert(!rb.radiusDropped&&!rc.radiusDropped&&!rc.radiusSelf&&h1.radiusSize()==0);++frames;}}
    std::printf("PASS radius off: no Radius == yards 0 == all-inside radius without self, %zu ranked crowd frames identical\n",frames);
}
// (b) Walking across the edge: hysteresis keyed by identity, no per-frame toggling.
void hysteresis(){
    for(int ranked=0;ranked<2;++ranked)for(float dropout:{0.f,.05f}){
        const float R=20;std::mt19937 random(9);std::uniform_real_distribution<float> u(-.5f,.5f);
        std::vector<Thing> things;
        // Loiterers sway +-1 yd around the edge (inside the band); walkers go 10 -> 40 -> 10 yd.
        for(unsigned i=0;i<24;++i){Thing t;const float a=.26f*float(i);t.x=R*std::cos(a);t.y=R*std::sin(a);t.parts=4;t.model=i%3;t.bytes=60000;things.push_back(t);}
        for(unsigned i=0;i<6;++i){Thing t;t.parts=5;t.model=5+i%2;t.bytes=80000;t.tag=100+i;things.push_back(t);}
        for(unsigned i=0;i<30;i+=2){Thing w;w.rigid=true;w.parent=int(i);w.ox=.4f;w.oz=1;w.model=40;w.bytes=4000;w.tag=NpcPart;things.push_back(w);}
        {Thing player;player.parts=8;player.model=1;player.tag=Player;things.push_back(player);} /* at the pivot: the centre */
        History h;Scratch sc;std::vector<int> last(things.size(),-1),changes(things.size(),0),naive(things.size(),0),naiveLast(things.size(),-1);
        const float pivot0[3]={0,0,0};
        for(unsigned f=0;f<1200;++f){
            for(unsigned i=0;i<24;++i){const float a=.26f*float(i),d=R+std::sin(.05f*float(f)+float(i))+.3f*u(random);things[i].x=d*std::cos(a);things[i].y=d*std::sin(a);}
            for(unsigned i=0;i<6;++i){const float phase=float((f+100*i)%600)/600,d=phase<.5f?10+60*phase:70-60*phase,a=1.1f*float(i);things[24+i].x=d*std::cos(a);things[24+i].y=d*std::sin(a);}
            ride(things);float pivot[3]={pivot0[0]+.5f*u(random),pivot0[1]+.5f*u(random),0};const float eye[3]={-8,0,10}; /* the camera is still; the pivot estimate jitters +-.25 yd */
            const Radius radius{R,eye,ranked==1};auto fr=build(things,eye,random,.4f,dropout);
            const auto r=choose(fr.draws,sc,ranked?2000000:0,h,false,pivot,Tuning{},&radius);if(r.radiusUnreferenced)continue; /* no self yet */
            std::vector<int> now(things.size(),-1),nearest(things.size(),-1);
            for(const auto& a:sc.actors)if(!a.rigid&&a.body==SIZE_MAX&&!a.orphan){const unsigned id=fr.owner[a.first];now[id]=!a.outside;float q=INFINITY;for(std::size_t i=a.first;i<a.first+a.count;++i)q=std::min(q,fr.draws[i].at[0]*fr.draws[i].at[0]+fr.draws[i].at[1]*fr.draws[i].at[1]);nearest[id]=q<=R*R;}
            for(unsigned id=0;id<things.size();++id){if(now[id]<0||things[id].rigid||things[id].tag==Player)continue;
                if(last[id]>=0&&now[id]!=last[id])++changes[id];last[id]=now[id];
                if(naiveLast[id]>=0&&nearest[id]!=naiveLast[id])++naive[id];naiveLast[id]=nearest[id];}
            for(const auto& a:sc.actors)if(a.body!=SIZE_MAX&&sc.actors[a.body].outside)assert(!a.keep);} /* attachments follow their body */
        int loiter=0,loiterNaive=0,walk=0;for(unsigned i=0;i<24;++i){loiter+=changes[i];loiterNaive+=naive[i];assert(changes[i]<=1);} /* at most the first entry */
        for(unsigned i=24;i<30;++i){walk+=changes[i];assert(changes[i]>=2&&changes[i]<=4);} /* out and back in, twice in 1200 frames */
        std::printf("hysteresis ranked=%d dropout=%.2f: edge loiterers radius changes=%d (a plain d<=R test: %d), walkers 10->40->10 yd changes=%d\n",ranked,double(dropout),loiter,loiterNaive,walk);
        assert(loiterNaive>20*loiter+20);}
    // The player walks 120 yd through a still crowd (idle sway .4 yd) with the
    // camera following and a wobbling pivot: every NPC is passed once, so its
    // radius state may change at most twice (in, out). With 2% capture dropout
    // the player itself is sometimes missing: another candidate is the centre
    // for that frame, which may add (never drop) a shadow near it briefly.
    for(int ranked=0;ranked<2;++ranked)for(float dropout:{0.f,.02f}){std::mt19937 scene(4),random(8);std::uniform_real_distribution<float> u(0,1);
        std::vector<Thing> things;Thing player;player.parts=10;player.model=1;player.tag=Player;player.x=-60;player.z=1;things.push_back(player);
        for(unsigned i=0;i<120;++i){Thing t;t.x=-70+140*u(scene);t.y=-30+60*u(scene);t.parts=4+i%3;t.model=3+i%5;t.bytes=90000;things.push_back(t);}
        for(unsigned i=1;i<121;i+=3){Thing w;w.rigid=true;w.parent=int(i);w.ox=.4f;w.oz=1;w.model=40;w.bytes=4000;w.tag=NpcPart;things.push_back(w);}
        const Radius radius{15,nullptr,ranked==1,false};History h;Scratch sc;std::vector<int> last(things.size(),-1),changes(things.size(),0),plain(things.size(),0),plainLast(things.size(),-1);
        std::size_t playerDropped=0;
        for(unsigned f=0;f<1200;++f){things[0].x=-60+.1f*float(f);ride(things);
            const float focus[3]={things[0].x,0,2},eye[3]={focus[0]-12,4,8},error=1.5f*std::sin(.07f*float(f));
            float dir[3],len=0;for(unsigned c=0;c<3;++c){dir[c]=focus[c]-eye[c];len+=dir[c]*dir[c];}len=std::sqrt(len);
            float pivot[3];for(unsigned c=0;c<3;++c)pivot[c]=focus[c]+dir[c]/len*error;
            Radius r=radius;r.eye=eye;auto fr=build(things,eye,random,.4f,dropout);
            const auto res=choose(fr.draws,sc,ranked?4000000:0,h,false,pivot,Tuning{},&r);
            for(std::size_t i=0;i<fr.draws.size();++i)playerDropped+=fr.owner[i]==0&&!fr.draws[i].keep;
            if(res.radiusUnreferenced)continue; /* the first admitFrames selections: no self yet, nothing dropped */
            for(const auto& a:sc.actors)if(!a.rigid&&a.body==SIZE_MAX&&!a.orphan){const unsigned id=fr.owner[a.first];if(!id)continue;const int now=!a.outside;
                if(last[id]>=0&&now!=last[id])++changes[id];last[id]=now;
                float q=INFINITY;for(std::size_t i=a.first;i<a.first+a.count;++i){float s=0;for(unsigned c=0;c<3;++c){const float x=fr.draws[i].at[c]-focus[c];s+=x*x;}q=std::min(q,s);}
                const int p=q<=225;if(plainLast[id]>=0&&p!=plainLast[id])++plain[id];plainLast[id]=p;}}
        int most=0,total=0,plainTotal=0;for(unsigned id=1;id<121;++id){most=std::max(most,changes[id]);total+=changes[id];plainTotal+=plain[id];}
        std::printf("walk through a crowd ranked=%d dropout=%.2f radius 15: max radius changes per NPC=%d, total=%d (a plain d<=15 test: %d), player draws dropped=%zu\n",ranked,double(dropout),most,total,plainTotal,playerDropped);
        assert(most<=(dropout>0?4:2)&&plainTotal>total+50&&!playerDropped);}
    std::puts("PASS hysteresis: band max(2,10%) keyed by identity (roots), capture gaps, pivot jitter, walking player, both quota modes");
}
// (c) Radius 1 (effective MinYards): the player's whole group (body incl. cape
// parts, a big mount sampled only at its extremities, weapon, shield, mount
// armour) casts on every frame the rider is captured. The pivot estimate is
// wrong along the ray as after mouse-wheel zooms (-18..+9 yd), the camera
// orbits (neighbours at 5-9.5 yd sweep across the centre ray), the player
// walks and flies fast (identities match only through the camera's move).
// Until a self is established nothing is dropped; after that no neighbour and
// no neighbour's weapon casts.
// gaps: 5% capture dropout of every thing (the player included) and a 20 yd blink.
void self(){
    for(int ranked=0;ranked<2;++ranked)for(int fast=0;fast<2;++fast)for(int gaps=0;gaps<2;++gaps){
        std::mt19937 random(31+fast);std::vector<Thing> things;
        Thing player;player.parts=12;player.model=1;player.bytes=240000;player.tag=Player;player.z=2.2f;things.push_back(player);
        Thing mount;mount.parts=3;mount.model=2;mount.spread=3.5f;mount.bytes=180000;mount.tag=Mount;mount.parent=0;mount.oz=-2.2f;things.push_back(mount); /* nose/tail/flank samples 3.5 yd out */
        for(auto o:{std::array<float,3>{.5f,.3f,.6f},std::array<float,3>{-.4f,.4f,.7f}}){Thing w;w.rigid=true;w.parent=0;w.ox=o[0];w.oy=o[1];w.oz=o[2];w.model=30;w.bytes=6000;w.tag=PlayerPart;things.push_back(w);} /* weapon, shield */
        {Thing w;w.rigid=true;w.parent=1;w.ox=-2.5f;w.oz=1.2f;w.model=31;w.bytes=6000;w.tag=PlayerPart;things.push_back(w);} /* mount armour */
        const unsigned first=unsigned(things.size());
        for(unsigned i=0;i<10;++i){Thing t;t.parts=6;t.model=3+i%3;t.bytes=120000;t.tag=Npc;t.parent=0;const float a=.63f*float(i),d=5+.5f*float(i);t.ox=d*std::cos(a);t.oy=d*std::sin(a);t.oz=-2.2f;things.push_back(t);}
        for(unsigned i=first;i<first+10;i+=2){Thing w;w.rigid=true;w.parent=int(i);w.ox=.5f;w.oz=1;w.model=40;w.bytes=5000;w.tag=NpcPart;things.push_back(w);}
        for(unsigned i=0;i<8;++i){Thing t;t.parts=5;t.model=6+i%2;t.bytes=100000;t.tag=Npc;t.parent=0;const float a=.8f*float(i),d=12+4*float(i);t.ox=d*std::cos(a);t.oy=d*std::sin(a);t.oz=-2.2f;things.push_back(t);} /* farther ones the turning camera sweeps across */
        Thing fence;fence.rigid=true;fence.tag=Fence;fence.model=60;fence.x=30;things.push_back(fence);
        History h;Scratch sc;std::size_t frames=0,riderFrames=0,selfFrames=0,open=0,companion=0,groupGapDrops=0,stray=0;
        for(unsigned f=0;f<1200;++f){
            const float speed=fast?2.5f:.12f;things[0].x+=speed+(gaps&&f==450?20.f:0.f);things[0].y+=.3f*speed*std::sin(.01f*float(f));ride(things);
            // Camera focus on the player's head; eye orbits at 15-28 yd; the pivot estimate is off along the ray.
            const float focus[3]={things[0].x,things[0].y,things[0].z+.9f},t=.02f*float(f),zoom=f%400<200?15.f:28.f;
            float eye[3]={focus[0]+zoom*.9f*std::cos(t),focus[1]+zoom*.9f*std::sin(t),focus[2]+zoom*.43f};
            float dir[3],len=0;for(unsigned c=0;c<3;++c){dir[c]=focus[c]-eye[c];len+=dir[c]*dir[c];}len=std::sqrt(len);
            const unsigned phase=f%400;const float error=phase<100?2.f*std::sin(.1f*float(f)):phase<200?9.f:phase<300?-18.f:-3.f; /* wobble, stale zoom-in, stale zoom-out */
            float pivot[3];for(unsigned c=0;c<3;++c)pivot[c]=focus[c]+dir[c]/len*error;
            const Radius radius{1,eye,ranked==1,false};auto fr=build(things,eye,random,.05f,gaps?.05f:0.f);
            const auto r=choose(fr.draws,sc,ranked?100000000:0,h,false,pivot,Tuning{},&radius);
            std::vector<bool> present(things.size());for(auto id:fr.owner)present[id]=true;const bool rider=present[0];
            ++frames;riderFrames+=rider;companion+=r.radiusCompanions;open+=r.radiusUnreferenced;
            if(r.radiusUnreferenced){for(const auto& d:fr.draws)assert(d.keep);continue;} /* no self yet: nothing dropped */
            for(const auto& a:sc.actors)if(a.self){assert(fr.owner[a.first]<=1);++selfFrames;} /* one self: the rider or its mount (same place) */
            for(const auto& a:sc.actors){const auto& thing=things[fr.owner[a.first]];
                if(thing.tag==Npc)assert(!a.keep);
                if(thing.tag==NpcPart){assert(!a.keep);stray+=a.keep;}}
            for(std::size_t i=0;i<fr.draws.size();++i){const auto tag=things[fr.owner[i]].tag;
                if(tag==Fence)assert(fr.draws[i].keep); /* a free-standing prop, never affected */
                else if(tag==Player||tag==Mount||tag==PlayerPart){if(rider)assert(fr.draws[i].keep);else groupGapDrops+=!fr.draws[i].keep;}}}
        std::printf("radius 1 ranked=%d fast=%d gaps=%d: unfiltered until the self was established=%zu frames; a self (the rider, or its mount) on %zu of %zu filtered frames and the group cast on every frame with the rider captured; neighbours cast on 0 frames (nor their weapons: %zu); group draws dropped while the rider was not captured=%zu; companions/frame=%.2f\n",
            ranked,fast,gaps,open,selfFrames,frames-open,stray,groupGapDrops,double(companion)/double(frames));
        assert(open<=(gaps?2:1)*Tuning{}.admitFrames&&selfFrames+(riderFrames<frames?frames-riderFrames:0)>=frames-open);}
    std::puts("PASS radius 1: the player's full group on every frame (stale pivot -18..+9 yd, orbit sweeping neighbours at 5-40 yd across the centre, zoom, big mount, fast flight, capture gaps, blink); no neighbour or neighbour's weapon ever casts; unfiltered until the self is established");
}
// Toggles over the radius-0 baseline on the same scene (the self() formation,
// orbit, stale pivot): every keep change of a draw present on two consecutive
// frames, counted from admitFrames selections after the self is established
// (the login transition: what the unfiltered frames cast settles once; radius
// 0 = budget 0 today: no selection, nothing ever changes).
void toggles(){
    for(int variant=0;variant<3;++variant){const bool fast=variant==2,gaps=variant==1;std::size_t counts[3]={};unsigned slot=0;
        for(float yards:{0.f,1.f,30.f}){std::mt19937 random(77);std::vector<Thing> things;
            Thing player;player.parts=12;player.model=1;player.tag=Player;player.z=2.2f;things.push_back(player);
            Thing mount;mount.parts=3;mount.model=2;mount.spread=3.5f;mount.tag=Mount;mount.parent=0;mount.oz=-2.2f;things.push_back(mount);
            {Thing w;w.rigid=true;w.parent=0;w.ox=.5f;w.oy=.3f;w.oz=.6f;w.model=30;w.tag=PlayerPart;things.push_back(w);}
            for(unsigned i=0;i<24;++i){Thing t;t.parts=5;t.model=3+i%4;t.tag=Npc;t.parent=0;const float a=.61f*float(i),d=4+1.6f*float(i);t.ox=d*std::cos(a);t.oy=d*std::sin(a);t.oz=-2.2f;things.push_back(t);}
            for(unsigned i=3;i<27;i+=2){Thing w;w.rigid=true;w.parent=int(i);w.ox=.5f;w.oz=1;w.model=40;w.tag=NpcPart;things.push_back(w);}
            History h;Scratch sc;std::vector<int> last(things.size(),-1);unsigned established=0;
            for(unsigned f=0;f<1200;++f){things[0].x+=fast?2.5f:.12f;ride(things);
                const float focus[3]={things[0].x,things[0].y,things[0].z+.9f},t=.02f*float(f),zoom=f%400<200?15.f:28.f;
                float eye[3]={focus[0]+zoom*.9f*std::cos(t),focus[1]+zoom*.9f*std::sin(t),focus[2]+zoom*.43f},dir[3],len=0;
                for(unsigned c=0;c<3;++c){dir[c]=focus[c]-eye[c];len+=dir[c]*dir[c];}len=std::sqrt(len);
                const float error=f%400<200?9.f:-18.f;float pivot[3];for(unsigned c=0;c<3;++c)pivot[c]=focus[c]+dir[c]/len*error;
                auto fr=build(things,eye,random,.05f,gaps?.05f:0.f);
                if(yards>0){const Radius radius{yards,eye,false,false};const auto r=choose(fr.draws,sc,0,h,false,pivot,Tuning{},&radius);
                    if(established<=Tuning{}.admitFrames){established+=!r.radiusUnreferenced;std::fill(last.begin(),last.end(),-1);}}
                else for(auto& d:fr.draws)d.keep=true;
                std::vector<int> now(things.size(),-1);for(std::size_t i=0;i<fr.draws.size();++i)now[fr.owner[i]]=fr.draws[i].keep;
                for(std::size_t id=0;id<things.size();++id){if(now[id]>=0&&last[id]>=0&&now[id]!=last[id])++counts[slot];last[id]=now[id];}}
            ++slot;}
        std::printf("toggles %-12s radius 0: %zu  radius 1: %zu  radius 30: %zu\n",fast?"fast flight":gaps?"5% gaps":"no gaps",counts[0],counts[1],counts[2]);
        assert(counts[1]<=counts[0]&&counts[2]<=counts[0]);}
    std::puts("PASS toggles: the radius adds no keep changes over radius 0 on the formation scene (no gaps, 5% gaps, fast flight)");
}
// The player walks toward a still NPC standing on the centre ray in front of it,
// with the pivot estimate stuck at 12 yd while the camera zooms 12 -> 3 -> 30
// without orbiting (mouse wheel), then stands still while zooming again. The
// login window (no self yet) drops nothing; the player is the self throughout.
void zoomWalk(){
    for(int ranked=0;ranked<2;++ranked){std::mt19937 random(12);std::vector<Thing> things;
        Thing player;player.parts=10;player.model=1;player.tag=Player;things.push_back(player);
        Thing still;still.parts=6;still.model=4;still.tag=Npc;still.x=9;things.push_back(still); /* on the path, on the ray */
        Thing far;far.parts=6;far.model=5;far.tag=Npc;far.x=-4;far.y=9;things.push_back(far);
        History h;Scratch sc;std::size_t open=0,selfFrames=0,frames=0,npcSelf=0,onRay=0;float zoom=12;
        for(unsigned f=0;f<600;++f){
            const bool walking=f<200||(f>=400&&f<450);if(walking)things[0].x+=.1f;
            zoom=f<100?12.f:f<150?12-9*float(f-100)/50:f<250?3.f:f<300?3+27*float(f-250)/50:30.f; /* wheel, no orbit */
            const float focus[3]={things[0].x,0,1.8f},back[3]={-.9f,0,.43f};
            float eye[3];for(unsigned c=0;c<3;++c)eye[c]=focus[c]+zoom*back[c];
            float pivot[3];for(unsigned c=0;c<3;++c)pivot[c]=eye[c]-12*back[c]; /* the stale estimate: always 12 yd */
            const Radius radius{1,eye,ranked==1,false};auto fr=build(things,eye,random,.3f);
            const auto r=choose(fr.draws,sc,ranked?100000000:0,h,false,pivot,Tuning{},&radius);++frames;
            for(std::size_t i=0;i<fr.draws.size();++i)if(fr.owner[i]==0)assert(fr.draws[i].keep);
            if(r.radiusUnreferenced){++open;for(const auto& d:fr.draws)assert(d.keep);continue;}
            for(const auto& a:sc.actors){if(a.self){if(fr.owner[a.first]==0)++selfFrames;else ++npcSelf;}if(fr.owner[a.first]==1)onRay+=a.candidate;}}
        std::printf("zoom 12->3->30 without orbit, walking toward a still NPC on the ray ranked=%d: login window %zu frames (nothing dropped), player self %zu of %zu frames, NPC self %zu, NPC on the ray %zu frames\n",ranked,open,selfFrames,frames-open,npcSelf,onRay);
        assert(open<=Tuning{}.admitFrames&&selfFrames==frames-open&&!npcSelf&&onRay>80);} /* within ClusterGap of the player the NPC is part of its character, not a candidate */
    std::puts("PASS stale pivot + zoom + still NPC on the ray: the player stays the self; nothing dropped before it is established");
}
// da5: after a fast camera move a new fence must not take a same-model
// neighbour fence's settled entry through the camera-shifted lookup (it would
// then seem to move and attach to a dropped NPC beside it). A row of one fence
// model every 3 yd, a still NPC beside each, the player flying past at 3 yd per
// selection and a 500 yd teleport: every fence casts once past its probation
// (0.3.144: a rigid actor entering view beside a body follows it for
// ProbationStill selections, so a fence beside a dropped NPC appears that late).
void fences(){
    std::mt19937 random(5);std::vector<Thing> things;
    Thing player;player.parts=8;player.model=1;player.tag=Player;player.z=1;things.push_back(player);
    for(unsigned i=0;i<60;++i){Thing f;f.rigid=true;f.tag=Fence;f.model=60;f.x=3.f*float(i);f.y=8;things.push_back(f);
        Thing n;n.parts=4;n.model=3;n.tag=Npc;n.x=3.f*float(i)+.8f;n.y=9;things.push_back(n);}
    History h;Scratch sc;std::size_t fenceDrops=0,fenceFrames=0,late=0;std::vector<unsigned> since(things.size(),0);
    for(unsigned f=0;f<300;++f){things[0].x=-20+3.f*float(f%90)+(f>=150?500.f:0.f);
        const float focus[3]={things[0].x,0,1.9f},eye[3]={focus[0]-12,0,focus[2]+6};
        const float dir[3]={12,0,-6};float pivot[3];for(unsigned c=0;c<3;++c)pivot[c]=eye[c]+dir[c];
        std::vector<Thing> seen;std::vector<unsigned> id;
        for(unsigned i=0;i<things.size();++i){if(std::fabs(things[i].x-things[0].x)<=20){seen.push_back(things[i]);id.push_back(i);++since[i];}else since[i]=0;} /* view range: fences enter as the player flies */
        const Radius radius{1,eye,false,false};auto fr=build(seen,eye,random,.2f);
        const auto r=choose(fr.draws,sc,0,h,false,pivot,Tuning{},&radius);(void)r;
        for(std::size_t i=0;i<fr.draws.size();++i)if(seen[fr.owner[i]].tag==Fence){const bool settled=since[id[fr.owner[i]]]>FreeFrames;
            ++fenceFrames;if(settled)fenceDrops+=!fr.draws[i].keep;else late+=!fr.draws[i].keep;}}
    std::printf("fence row beside dropped NPCs, flying 3 yd/selection and a 500 yd teleport: fence draws dropped after probation %zu of %zu (during probation, 0.3.144 behaviour: %zu)\n",fenceDrops,fenceFrames,late);
    assert(!fenceDrops);
    // Direct case: fence A settles in view; the camera (with the player) jumps 10 yd,
    // A leaves view and fence B of the same model appears exactly 10 yd on, beside a
    // dropped NPC. B must not inherit A's settled entry (it would seem to move and
    // stay bound to the NPC); after its probation it casts.
    {std::mt19937 r(9);History hh;Scratch ss;bool last=false;
     for(unsigned f=0;f<40;++f){const float shift=f<20?0.f:10.f;std::vector<Thing> v;
        Thing pl;pl.parts=8;pl.model=1;pl.tag=Player;pl.x=shift;pl.y=-15;pl.z=1;v.push_back(pl);
        Thing fe;fe.rigid=true;fe.tag=Fence;fe.model=60;fe.x=shift;v.push_back(fe);
        if(f>=20){Thing n;n.parts=4;n.model=3;n.tag=Npc;n.x=shift+.8f;n.y=1;v.push_back(n);}
        const float eye[3]={shift-12,-15,8},pv[3]={shift,-15,1.9f};const Radius rad{1,eye,false,false};
        auto fr=build(v,eye,r,.002f);choose(fr.draws,ss,0,hh,false,pv,Tuning{},&rad);last=fr.draws[8].keep;}
     std::printf("fence B beside a dropped NPC after a 10 yd camera jump: casts after its probation=%d\n",int(last));assert(last);}
    std::puts("PASS fences: the camera-shifted identity lookup never reaches a settled prop");
}
// Crowd (Stormwind trade district, the 0.3.145 R=35 game test): 110 characters
// on 120x120 yd (20-30 within 35 yd of the player), 10% riding (mount + rider,
// 14 yd/s), 6 stacks of 6 AFK players of one model, 35% of the characters share
// 5 models, 35% idle, the rest walk 1-7 yd/s, half carry a weapon. As in the
// game every character is drawn as 6 constant groups (bone palette sections:
// feet, spine, head, a swinging hand, legs, cape; roots at those bones; 1-2
// draws each, odd draws reusing their predecessor's sample): 0.3.145 logs show
// companions=5 around a lone player and 1.3 draws per group. The player walks or
// rides, the camera orbits in phases, 50 selections per second; 10% of the
// characters are missing per selection, 3% of the sections are missing (the
// smallest key can change), capture order reshuffled. Per character: in (every
// section casts), out (none) or partial; changes in<->out, reversals within
// RadiusDwell selections, and changes its own bounds cannot explain.
struct CrowdStats {std::size_t changes=0,flicker=0,wrong=0,partial=0,frames=0,counted=0,countedFlicker=0,rekeyed=0,window=0,maxWindow=0,characters=0,actors=0;double us=0;std::vector<std::size_t> windows;};
CrowdStats crowdRun(float yards,float playerSpeed,std::size_t budget,unsigned seed){
    struct Body {float x,y,z=0,vx=0,vy=0,speed=0,phase=0,turn=0;unsigned model=0,sections=6;bool rooted=true,idle=false,weapon=false;int mount=-1;};
    static const float bone[6][3]={{0,0,0},{0,0,1},{0,0,1.7f},{0,.3f,1.1f},{0,0,.5f},{-.3f,0,1.2f}};
    std::mt19937 random(seed);std::uniform_real_distribution<float> u(0,1);std::vector<Body> b;
    {Body p;p.x=-40;p.y=0;b.push_back(p);} /* the player: model 0, one of the popular ones */
    for(unsigned i=0;i<110;++i){Body t;t.x=(2*u(random)-1)*60;t.y=(2*u(random)-1)*60;t.model=u(random)<.35f?unsigned(random()%5):5+unsigned(random()%40);
        t.rooted=u(random)<.8f;t.idle=u(random)<.35f;t.speed=t.idle?0:1+6*u(random);t.phase=6.28f*u(random);t.weapon=u(random)<.5f;const float a=6.28f*u(random);t.vx=std::cos(a);t.vy=std::sin(a);t.turn=5*u(random);
        if(u(random)<.1f){t.model=50+random()%3;t.sections=3;t.speed=14;t.idle=false;b.push_back(t);Body r=t;r.model=random()%5;r.sections=6;r.z=1.5f;r.mount=int(b.size()-1);b.push_back(r);}else b.push_back(t);}
    for(unsigned c=0;c<6;++c){const float cx=(2*u(random)-1)*60,cy=(2*u(random)-1)*60;const unsigned m=random()%5;
        for(unsigned i=0;i<6;++i){Body t;t.x=cx+1.5f*(u(random)-.5f);t.y=cy+1.5f*(u(random)-.5f);t.model=m;t.idle=true;t.phase=6.28f*u(random);t.weapon=true;b.push_back(t);}}
    std::vector<unsigned> order(b.size());for(unsigned i=0;i<order.size();++i)order[i]=i;
    std::vector<int> last(b.size(),-1),changed(b.size(),-1000);
    History h;Scratch sc;CrowdStats s;const float dt=.02f;float yaw=0,yawRate=0,heading=0,leg=0;bool walking=true;const unsigned dwell=RadiusDwell;
    for(unsigned f=0;f<2000;++f){const float t=float(f)*dt;
        if((leg-=dt)<=0){walking=u(random)<.7f;leg=1+3*u(random);heading+=2*(u(random)-.5f);}
        if(walking){b[0].x+=playerSpeed*dt*std::cos(heading);b[0].y+=playerSpeed*dt*std::sin(heading);if(std::fabs(b[0].x)>60||std::fabs(b[0].y)>60)heading+=3.14f;}
        if(f%100==0)yawRate=u(random)<.5f?0:6*(u(random)-.5f);yaw+=yawRate*dt;
        const float focus[3]={b[0].x,b[0].y,b[0].z+1.8f},eye[3]={focus[0]+13.5f*std::cos(yaw),focus[1]+13.5f*std::sin(yaw),focus[2]+6.5f};
        const float forward[2]={-std::cos(yaw),-std::sin(yaw)};
        for(std::size_t i=1;i<b.size();++i){auto& x=b[i];if(x.mount>=0){x.x=b[std::size_t(x.mount)].x;x.y=b[std::size_t(x.mount)].y;continue;}if(x.idle)continue;
            if((x.turn-=dt)<=0){const float a=6.28f*u(random);x.vx=std::cos(a);x.vy=std::sin(a);x.turn=1+5*u(random);}
            x.x+=x.vx*x.speed*dt;x.y+=x.vy*x.speed*dt;if(std::fabs(x.x)>60)x.vx=-x.vx;if(std::fabs(x.y)>60)x.vy=-x.vy;}
        if(f%7==0)std::shuffle(order.begin()+1,order.end(),random);
        std::vector<Draw> draws;std::vector<unsigned> owner;unsigned group=0;
        auto distance=[&](Draw& w){float q=0;for(unsigned c=0;c<3;++c){const float e=w.at[c]-eye[c];q+=e*e;}w.distanceSquared=q;};
        for(unsigned id:order){const auto& x=b[id];const float dx=x.x-eye[0],dy=x.y-eye[1],d=std::hypot(dx,dy);
            if(id&&((d>6&&dx*forward[0]+dy*forward[1]<.5f*d)||d>120||u(random)<.1f))continue; /* frustum, capture gap */
            const float swing=x.idle?.05f:.35f*std::sin(8*t+x.phase);
            for(unsigned sct=0;sct<x.sections;++sct){if(id&&u(random)<.03f)continue; /* a section missing */
                float root[3];for(unsigned c=0;c<3;++c)root[c]=bone[sct][c];if(sct==3)root[0]+=swing;if(sct==4)root[0]+=.4f*swing;root[0]+=x.x;root[1]+=x.y;root[2]+=x.z;
                for(unsigned j=0;j<1+sct%2;++j){Draw w;w.index=draws.size();w.group=group;w.bytes=60000/(x.sections*2);w.known=true;w.bone=NAN;w.key=1000+std::uint64_t(x.model)*16+sct*2+j;
                    const float a=1.7f*float(sct*2+j+x.model);w.at[0]=root[0]+.2f*std::cos(a);w.at[1]=root[1]+.2f*std::sin(a);w.at[2]=root[2]+.1f*float(j);distance(w);
                    if(j){std::memcpy(w.at,draws.back().at,sizeof w.at);w.distanceSquared=draws.back().distanceSquared;} /* reused sample */
                    if(!j&&x.rooted){w.hasRoot=true;std::memcpy(w.root,root,sizeof root);}
                    draws.push_back(w);owner.push_back(id);}
                ++group;}
            if(x.weapon){Draw w;w.index=draws.size();w.group=group++;w.bytes=5000;w.known=true;w.rigid=true;w.bone=5;w.key=5000+x.model%3;w.at[0]=x.x+.5f;w.at[1]=x.y+.3f;w.at[2]=x.z+1.1f;distance(w);draws.push_back(w);owner.push_back(~0u);}}
        const auto t0=std::chrono::steady_clock::now();Result r;
        if(yards>0){const Radius radius{yards,eye,true,true};r=choose(draws,sc,budget,h,false,focus,Tuning{},&radius);}
        else if(budget&&h.shouldRank([&]{std::size_t n=0;for(const auto& d:draws)n+=d.bytes;return n;}(),budget,Tuning{}))r=choose(draws,sc,budget,h,false,focus,Tuning{});
        else{if(budget)h.keptAll();for(auto& d:draws)d.keep=true;}
        s.us+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t0).count();
        s.counted+=r.radiusToggles;s.countedFlicker+=r.radiusFlicker;s.rekeyed+=r.radiusRekeyed;s.characters+=r.radiusCharacters;s.window+=r.radiusToggles;
        for(const auto& a:sc.actors)s.actors+=!a.rigid&&a.body==SIZE_MAX&&!a.orphan;
        if(f%120==119){s.windows.push_back(s.window);s.maxWindow=std::max(s.maxWindow,s.window);s.window=0;}
        if(yards>0&&r.radiusUnreferenced)continue;
        // Per character: every captured section cast (1), none (0), or partial (2); its bounds.
        std::vector<int> now(b.size(),-1);std::vector<detail::Box> box(b.size());
        for(std::size_t i=0;i<draws.size();++i){const unsigned id=owner[i];if(id==~0u)continue;const int k=draws[i].keep;now[id]=now[id]<0?k:now[id]==k?k:2;box[id].add(draws[i].at);if(draws[i].hasRoot)box[id].add(draws[i].root);}
        const float feet[3]={b[0].x,b[0].y,b[0].z},y=std::max(yards,MinYards),w=y+band(y);
        for(std::size_t id=1;id<b.size();++id){if(now[id]<0)continue;++s.frames;if(now[id]==2){++s.partial;continue;}
            if(last[id]>=0&&now[id]!=last[id]){++s.changes;s.flicker+=int(f)-changed[id]<=int(dwell);changed[id]=int(f);
                const float q=box[id].squared(feet),slack=1.5f; /* the centre is a section root of the player */
                s.wrong+=now[id]?q>(y+slack)*(y+slack):q<=(w-slack)*(w-slack);}
            last[id]=now[id];}}
    std::sort(s.windows.begin(),s.windows.end());return s;
}
void crowd(){
    for(float speed:{7.f,14.f}){std::size_t base[2]={};
        for(std::size_t budget:{std::size_t(0),std::size_t(1500000)})for(float yards:{0.f,10.f,35.f}){
            std::size_t flicker=0,wrong=0,changes=0,partial=0,frames=0,counted=0,countedFlicker=0,rekeyed=0,p90=0,most=0,characters=0,actors=0;double us=0;
            for(unsigned seed=1;seed<=2;++seed){const auto s=crowdRun(yards,speed,budget,seed);flicker+=s.flicker;wrong+=s.wrong;changes+=s.changes;partial+=s.partial;frames+=s.frames;counted+=s.counted;countedFlicker+=s.countedFlicker;
                rekeyed+=s.rekeyed;characters+=s.characters;actors+=s.actors;us+=s.us/4000;if(!s.windows.empty()){p90=std::max(p90,s.windows[s.windows.size()*9/10]);most=std::max(most,s.maxWindow);}}
            if(yards==0){base[budget>0]=flicker;std::printf("crowd %2.0f yd/s budget=%-7zu radius 0: character changes=%zu reversals within %u=%zu partial=%zu choose %.0f us\n",double(speed),budget,changes,RadiusDwell,flicker,partial,us);continue;}
            std::printf("crowd %2.0f yd/s budget=%-7zu radius %2.0f: radiusToggles=%zu (per 120 selections p90 %zu max %zu) characters/selection=%.0f of %.0f body actors; character in/out changes=%zu reversals within %u=%zu (radiusFlicker=%zu) unexplained=%zu partial=%zu of %zu; rekeyed=%zu choose %.0f us\n",
                double(speed),budget,double(yards),counted,p90,most,double(characters)/4000,double(actors)/4000,changes,RadiusDwell,flicker,countedFlicker,wrong,partial,frames,rekeyed,us);
            if(!budget)assert(flicker*25<=changes&&wrong*20<=changes&&partial*1000<=frames&&p90<=40); /* 0.3.145: partial 0.4-0.6%, toggles p90 92-165 */
            else assert(flicker<=base[1]);}} /* the quota (per section, as in 0.3.144) alone flickers more */
    std::puts("PASS crowd: characters decide as a whole; changes are single crossings through same-model stacks, key changes (missing sections), capture gaps and orbiting");
}
// Unknown distance: kept whatever the radius (fail-open).
void unknown(){
    for(float yards:{1.f,5.f,40.f}){std::mt19937 random(3);std::vector<Thing> things(3);things[0].parts=4;things[0].z=1; /* the player */
        things[1].y=12;things[1].parts=3;things[2].x=12;things[2].parts=2;things[2].known=false;
        History h;Scratch sc;const float pivot[3]={0,0,1.5f},eye[3]={-10,0,6};const Radius radius{yards,eye,false};
        for(unsigned f=0;f<=Tuning{}.admitFrames;++f){auto fr=build(things,eye,random,0);const auto r=choose(fr.draws,sc,0,h,false,pivot,Tuning{},&radius);
            if(f+1<Tuning{}.admitFrames){assert(r.radiusUnreferenced);for(const auto& d:fr.draws)assert(d.keep);continue;} /* no self yet */
            if(r.radiusUnreferenced)continue;
            for(std::size_t i=0;i<fr.draws.size();++i)assert(fr.draws[i].keep==(fr.owner[i]!=1||yards>=11.5f));
            assert(r.unknown==1&&r.radiusSelf==1);}}
    std::puts("PASS unknown distance: kept at every radius (fail-open)");
}
// Quota interplay: an actor outside the radius is never ranked, charged or
// reserved, and does not set the cut; with decide the quota ranks only when
// the bytes INSIDE the radius exceed it.
void quota(){
    Tuning t;t.stationary=false;History h;Scratch sc;const float pivot[3]={0,0,0},eye[3]={-6,0,4};
    auto frame=[&](float farX,float yards,std::size_t budget,bool decide){std::vector<Draw> d;
        auto add=[&](unsigned group,std::uint64_t key,float x,std::size_t bytes){Draw w;w.index=d.size();w.group=group;w.bytes=bytes;w.known=true;w.bone=NAN;w.key=key;w.at[0]=x;w.at[1]=5;w.distanceSquared=x*x+25;w.hasRoot=true;std::memcpy(w.root,w.at,sizeof w.root);d.push_back(w);};
        add(0,10,0,40);add(1,20,5,40);add(2,30,farX,40); /* off the ray */
        {Draw w;w.index=d.size();w.group=3;w.bytes=0;w.known=true;w.bone=NAN;w.key=40;w.distanceSquared=0;w.hasRoot=true;d.push_back(w);} /* the player: at the pivot, the centre */
        const Radius radius{yards,eye,true,decide};const auto r=choose(d,sc,budget,h,false,pivot,t,yards>0?&radius:nullptr);return std::make_pair(d,r);};
    for(unsigned f=0;f<5;++f){auto [d,r]=frame(15,0,100,false);assert(d[0].keep&&d[1].keep&&!d[2].keep);} /* quota: the third does not fit */
    for(unsigned f=0;f<Tuning{}.admitFrames;++f)frame(15,12,120,false); /* until the self is established: unfiltered */
    for(unsigned f=0;f<5;++f){auto [d,r]=frame(15,12,120,false);assert(d[0].keep&&d[1].keep&&!d[2].keep&&r.radiusDropped==1&&r.radiusSelf==1&&r.actors==3&&r.keptBytes==80&&r.droppedBytes==40&&r.reserved==0);}
    {auto [d,r]=frame(15,12,80,false);assert(d[0].keep&&d[1].keep&&r.reserved==0&&std::isinf(h.lastCut()));} /* no cut: the quota holds the two inside */
    {History fresh;h=fresh;for(unsigned f=0;f<Tuning{}.admitFrames+Tuning{}.stayFrames;++f)frame(15,12,100,true); /* establish; leave the ranking state */
     auto [d,r]=frame(15,12,100,true);assert(!r.ranked&&r.radiusInsideBytes==80&&d[0].keep&&d[1].keep&&!d[2].keep);} /* 120 captured, 80 inside: no ranking */
    {auto [d,r]=frame(15,12,60,true);assert(r.ranked&&r.radiusInsideBytes==80&&!d[2].keep);}
    std::puts("PASS quota interplay: outside actors are neither ranked, charged, reserved nor the cut; the quota ranks on the bytes inside the radius");
}
// Main-thread cost of the radius-only path (budget 0: the Quality preset) on a
// 1400-draw crowd, and the body draws a radius removes (synthetic town: uniform
// in distance up to ~70 yd, weapons, fences, the player at the pivot).
void cost(){
    std::mt19937 scene(77);auto things=crowd(scene,200,70,200);{Thing p;p.parts=10;p.model=1;p.z=0;p.tag=Player;things.push_back(p);}std::mt19937 random(5);
    const float pivot[3]={0,0,.9f},eye[3]={-15,0,8};std::size_t total=0,draws=0;for(const auto& t:things){draws+=t.parts;if(!t.rigid)total+=t.parts;}
    for(float yards:{0.f,15.f,30.f,50.f}){History h;Scratch sc;std::vector<double> us;std::size_t kept=0,frames=0;const Radius radius{yards,eye,false};
        for(unsigned f=0;f<300;++f){auto fr=build(things,eye,random,.3f);const auto t0=std::chrono::steady_clock::now();
            if(yards>0){choose(fr.draws,sc,0,h,false,pivot,Tuning{},&radius);}else for(auto& w:fr.draws)w.keep=true; /* budget 0 today: no selection at all */
            us.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t0).count());
            for(std::size_t i=0;i<fr.draws.size();++i)kept+=fr.draws[i].keep&&!things[fr.owner[i]].rigid;++frames;}
        std::sort(us.begin(),us.end());
        std::printf("crowd %zu draws radius=%2.0f: body draws cast %.0f of %zu, choose median %.1f us p95 %.1f us per selection (native build, budget 0)\n",draws,double(yards),double(kept)/double(frames),total,us[us.size()/2],us[us.size()*95/100]);}
}
}
// Crowd blink diagnostics: a far character's body is gated out of the capture but its weapon is
// not. The weapon has no body in reach, settles as a free rigid actor after FreeFrames and casts
// beyond radius+band (freeRigidBeyond); when the body is captured again the weapon, still moving
// with it, re-attaches (freeToAttach) and follows the body's drop.
void farWeapon(){
    std::mt19937 random(77);History h;Scratch sc;
    std::vector<Thing> things;
    Thing player;player.parts=8;player.model=1;player.tag=Player;player.z=1;things.push_back(player);
    Thing npc;npc.parts=3;npc.model=3;npc.tag=Npc;npc.x=60;npc.bytes=60000;things.push_back(npc);
    Thing weapon;weapon.rigid=true;weapon.parent=1;weapon.ox=.5f;weapon.oy=.3f;weapon.oz=1.1f;weapon.model=40;weapon.bytes=5000;weapon.tag=NpcPart;things.push_back(weapon);
    Thing prop;prop.rigid=true;prop.tag=Fence;prop.model=61;prop.x=70;prop.y=5;prop.bytes=7000;things.push_back(prop); /* a static lantern: never had a body */
    const float eye[3]={-12,0,7.9f},pivot[3]={0,0,1.9f};const Radius radius{40,eye,false,false};
    const unsigned gap=3*FreeFrames,total=gap+3*FreeFrames;
    std::size_t beyondFrames=0,toFree=0,toAttach=0,beyondAfterReturn=0;bool weaponDroppedAfter=false;
    for(unsigned f=0;f<total;++f){things[1].x+=.2f;ride(things);const bool bodyCaptured=f>=gap;
        std::vector<Thing> seen;seen.push_back(things[0]);seen.push_back(things[3]);if(bodyCaptured)seen.push_back(things[1]);seen.push_back(things[2]); /* the gated body is absent */
        auto fr=build(seen,eye,random,.002f);
        const auto r=choose(fr.draws,sc,0,h,true,pivot,Tuning{},&radius);
        toFree+=r.attachToFree;toAttach+=r.freeToAttach;
        const Draw& w=fr.draws.back();
        if(!bodyCaptured&&f>FreeFrames+1){assert(r.beyondKnown&&r.propRigidBeyond==2&&r.propRigidBeyondBytes==12000&&r.freeRigidBeyond==0&&r.freeRigidBeyondBytes==0&&w.keep);++beyondFrames;assert(!toAttach);}
        if(bodyCaptured&&f>gap+FreeFrames){beyondAfterReturn+=r.freeRigidBeyond;weaponDroppedAfter=!w.keep;}}
    std::printf("far weapon without its gated body: never-attached rigid props beyond radius+band on %zu selections, then the body returns: attachToFree=%zu freeToAttach=%zu, weapon dropped with its body=%d, still free beyond=%zu\n",beyondFrames,toFree,toAttach,int(weaponDroppedAfter),beyondAfterReturn);
    assert(beyondFrames>=gap-FreeFrames-2&&toAttach>=1&&toFree==0&&beyondAfterReturn==0&&weaponDroppedAfter);
    std::puts("PASS radius diagnostics: never-attached rigid things beyond radius+band count as props (not as former attachments) and a weapon attaches when its body appears");
}
// A weapon that starts attached to its body (beyond the radius), then the body is gated away: attached -> free
// (counted, also beyond the radius), after FreeFrames it is a free former attachment beyond (settled: stuck) and still casts;
// the body returns: free -> attached beyond.
void farWeaponLoses(){
    std::mt19937 random(78);History h;Scratch sc;
    std::vector<Thing> things;
    Thing player;player.parts=8;player.model=1;player.tag=Player;player.z=1;things.push_back(player);
    Thing npc;npc.parts=3;npc.model=3;npc.tag=Npc;npc.x=60;npc.bytes=60000;things.push_back(npc);
    Thing weapon;weapon.rigid=true;weapon.parent=1;weapon.ox=.5f;weapon.oy=.3f;weapon.oz=1.1f;weapon.model=40;weapon.bytes=5000;weapon.tag=NpcPart;things.push_back(weapon);
    const float eye[3]={-12,0,7.9f},pivot[3]={0,0,1.9f};const Radius radius{40,eye,false,false};
    const unsigned first=3*FreeFrames,second=first+3*FreeFrames,total=second+3*FreeFrames;
    std::size_t toFree=0,toFreeBeyond=0,toAttach=0,toAttachBeyond=0,beyond=0,stuck=0;bool keptFree=true;
    for(unsigned f=0;f<total;++f){things[1].x+=.2f;ride(things);const bool body=f<first||f>=second;
        std::vector<Thing> seen;seen.push_back(things[0]);if(body)seen.push_back(things[1]);seen.push_back(things[2]);
        auto fr=build(seen,eye,random,.002f);
        const auto r=choose(fr.draws,sc,0,h,true,pivot,Tuning{},&radius);
        toFree+=r.attachToFree;toFreeBeyond+=r.attachToFreeBeyond;toAttach+=r.freeToAttach;toAttachBeyond+=r.freeToAttachBeyond;
        if(f<first&&f>=FreeFrames+2)assert(!fr.draws.back().keep&&r.freeRigidBeyond==0); /* attached to a body that is outside */
        if(!body&&f>=first+FreeFrames+1){assert(r.freeRigidBeyond==1&&r.propRigidBeyond==0&&fr.draws.back().keep);beyond+=r.freeRigidBeyond;stuck+=r.stuckBeyond;keptFree=keptFree&&fr.draws.back().keep;}}
    std::printf("far weapon loses its body: attachToFree=%zu (beyond %zu), free beyond on %zu selections (stuck %zu), body returns: freeToAttach=%zu (beyond %zu)\n",toFree,toFreeBeyond,beyond,stuck,toAttach,toAttachBeyond);
    assert(toFree==1&&toFreeBeyond==1&&beyond>=second-first-FreeFrames-2&&stuck>=1&&keptFree&&toAttach>=1&&toAttachBeyond>=1);
    std::puts("PASS radius diagnostics: attached -> free beyond radius+band is localised, then counted as a former attachment, then attaches again");
}
int main(){off();hysteresis();self();toggles();zoomWalk();fences();unknown();quota();crowd();cost();farWeapon();farWeaponLoses();std::puts("PASS actor shadow radius");}
