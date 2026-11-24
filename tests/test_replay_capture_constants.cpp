#include "replay_capture_constants.h"
#include "point_rigid_pose_rows.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <type_traits>
#include <vector>
using namespace NorthlightReplayCaptureConstants;
struct Packet {
    float constantStorage[1024]={};int boolStorage[16]={},intStorage[64]={};
    const float* constants=constantStorage;const int* bools=boolStorage;const int* ints=intStorage;
    NorthlightConstantEpoch::Stamp constantStamp;
    NorthlightShaderConstants::Usage constantUsage;
    unsigned projectionKind=2,constantGroup=0;
    Packet()=default;Packet(const Packet&)=delete;Packet& operator=(const Packet&)=delete;
};
struct Source {
    NorthlightConstantEpoch::Clock clock;
    float floats[1024]={};int booleans[16]={},integers[64]={};
    bool enabled=true,fail=false,mutateWhileReading=false;size_t reads=0,bytes=0;
    static bool reader(void* context,NorthlightConstantEpoch::Stamp& stamp){
        auto& s=*static_cast<Source*>(context);stamp=s.clock.stamp(s.enabled);return stamp.valid;
    }
    bool fetch(Packet& p){
        ++reads;if(fail)return false;
        const auto& u=p.constantUsage;
        std::memcpy(p.constantStorage+4*u.floats.first,floats+4*u.floats.first,u.floats.count*16);
        std::memcpy(p.boolStorage+u.booleans.first,booleans+u.booleans.first,u.booleans.count*4);
        std::memcpy(p.intStorage+4*u.integers.first,integers+4*u.integers.first,u.integers.count*16);
        bytes+=u.floats.count*16+u.booleans.count*4+u.integers.count*16;
        if(mutateWhileReading)clock.invalidate();return true;
    }
};
static void equal(const Packet& a,const Packet& b){
    const auto& u=a.constantUsage;
    assert(!std::memcmp(a.constants+4*u.floats.first,b.constants+4*u.floats.first,u.floats.count*16));
    assert(!std::memcmp(a.bools+u.booleans.first,b.bools+u.booleans.first,u.booleans.count*4));
    assert(!std::memcmp(a.ints+4*u.integers.first,b.ints+4*u.integers.first,u.integers.count*16));
}
static bool get(Packet& p,const Packet* previous,Source& source,Stats* stats=nullptr){
    return capture(p,previous,&source,Source::reader,[&]{return source.fetch(p);},stats);
}
static void rigidPointPose(){
    using NorthlightPointShadow::RigidPoseRows;
    // mov o0, c6; mov o1, c2. Cube replay replaces c2.
    const std::uint32_t vertex[]={0xfffe0300,0x02000001,0xe00f0000,0xa0e40006,
        0x02000001,0xe00f0001,0xa0e40002,0x0000ffff};
    const auto rows=RigidPoseRows::fromProgram(vertex,std::size(vertex));
    Packet a,b;
    a.constantUsage.floats=b.constantUsage.floats={0,8};
    assert(rows.sameBanks(a,b));
    b.constantStorage[8]=2;
    b.constantStorage[28]=3;
    assert(rows.sameBanks(a,b));
    b.constantStorage[24]=-0.0f;
    assert(!rows.sameBanks(a,b));
    b.constantStorage[24]=0.0f;
    b.constantUsage.floats={2,4};
    assert(!rows.sameBanks(a,b));
    b.constantUsage.floats={2,5};
    assert(rows.sameBanks(a,b));
    assert(!RigidPoseRows::fromProgram(vertex,std::size(vertex)-1).sameBanks(a,b));
    std::puts("POINT_RIGID_POSE_PASS overwritten and unread rows, signed zero, short banks and unknown bytecode");
}
static void edges(){
    Source s;Packet first,next,rejected;Stats stats;
    assert(get(first,nullptr,s,&stats));assert(s.reads==1);
    assert(get(next,&first,s,&stats));assert(s.reads==1&&next.constants==first.constants&&stats.hits==1);
    assert(get(rejected,&next,s,&stats));assert(rejected.constants==first.constants);
    reset(rejected);assert(rejected.constants==rejected.constantStorage&&!rejected.constantStamp.valid);
    s.floats[100]=2;s.clock.writeFloat(25,1);assert(get(rejected,&next,s));
    assert(first.constants[100]==0&&next.constants[100]==0&&rejected.constants[100]==2);
    assert(!samePose(next,rejected));
    // A projection-only write must still copy its new bytes, while sharing pose identity.
    s.floats[8]=7;s.clock.writeFloat(2,1);Packet projection;
    assert(get(projection,&rejected,s));assert(projection.constants!=rejected.constants);
    assert(samePose(rejected,projection,&stats)&&projection.constants[8]==7);
    s.clock.writeFloat(2,1);Packet redundant;
    assert(get(redundant,&projection,s)&&samePose(projection,redundant));
    // A -> B -> A between draws is a conservative epoch miss, then exact-byte pose hit.
    s.floats[100]=3;s.clock.writeFloat(25,1);s.floats[100]=2;s.clock.writeFloat(25,1);
    Packet changedBack;auto reads=s.reads;
    assert(get(changedBack,&redundant,s)&&s.reads==reads+1&&samePose(redundant,changedBack));
    // Uncaptured/failed/recorded/disabled state cannot acquire reusable authority.
    s.clock.invalidate();s.fail=true;Packet failed;assert(!get(failed,&changedBack,s)&&!failed.constantStamp.valid);
    s.fail=false;s.enabled=false;assert(get(failed,&changedBack,s)&&!failed.constantStamp.valid);
    s.enabled=true;s.mutateWhileReading=true;Packet raced;assert(get(raced,&failed,s)&&!raced.constantStamp.valid);
    s.mutateWhileReading=false;Packet fallback;reads=s.reads;
    assert(capture(fallback,&changedBack,nullptr,nullptr,[&]{return s.fetch(fallback);})&&s.reads==reads+1&&!fallback.constantStamp.valid);
    // A successful getter for one layout is not proof for other register ranges.
    Packet narrow;narrow.constantUsage.floats={2,4};narrow.constantUsage.booleans={};narrow.constantUsage.integers={};
    assert(get(narrow,&changedBack,s));Packet wide;reads=s.reads;
    assert(get(wide,&narrow,s)&&s.reads==reads+1&&!compatible(narrow,wide));
    Packet kind1;kind1.projectionKind=1;reads=s.reads;assert(get(kind1,&wide,s)&&s.reads==reads+1);
    Packet unsupported;unsupported.projectionKind=3;assert(!compatible(unsupported,unsupported));
    Packet invalid;invalid.constantUsage.floats={250,10};assert(!compatible(invalid,invalid));
    // Signed zero and NaN payloads are compared as bits on fallback.
    Packet bits1,bits2;uint32_t nan=0x7fc01234;std::memcpy(s.floats+100,&nan,4);s.clock.writeFloat(25,1);assert(get(bits1,nullptr,s));
    assert(get(bits2,&bits1,s)&&samePose(bits1,bits2));nan++;std::memcpy(s.floats+100,&nan,4);s.clock.writeFloat(25,1);assert(get(bits2,&bits1,s)&&!samePose(bits1,bits2));
    s.floats[100]=0;s.clock.writeFloat(25,1);assert(get(bits1,nullptr,s));s.floats[100]=-0.f;s.clock.writeFloat(25,1);assert(get(bits2,&bits1,s)&&!samePose(bits1,bits2));
}
// 0.3.180 (C0-C2): the templated captureBlocks() through ReaderSource and ClockSource against the
// 0.3.179 Reader overload (unchanged, the reference) on one randomized mirror.
struct Mirror {
    NorthlightConstantEpoch::Clock constantEpoch;
    bool enabled=true,recording=false,held=true;std::size_t rawDepth=0;
    float floats[1024]={};int booleans[16]={},integers[64]={};
    bool active()const{return enabled&&!recording&&!rawDepth;}
    bool heldByThisThread()const{return held;}
    static bool reader(void* context,NorthlightConstantEpoch::Stamp& out){
        auto& m=*static_cast<Mirror*>(context);out=m.constantEpoch.stamp(m.active());return out.valid;
    }
    // MirrorDevice: an accepted write advances the serials only while active; every change of the
    // active state (Begin/EndStateBlock, RawScope, disable) and every Apply/Reset invalidates.
    void writeFloats(unsigned first,unsigned count,const float* data){
        for(unsigned r=0;r<count;++r)if(first+r<256)std::memcpy(floats+4*(first+r),data+4*r,16);
        if(active())constantEpoch.writeFloat(first,count);
    }
    void writeBool(unsigned reg,int value){booleans[reg]=value;if(active())constantEpoch.writeBool(1);}
    void writeInt(unsigned reg,int value){integers[4*reg]=value;if(active())constantEpoch.writeInt(1);}
    void inactive(unsigned kind,bool on){
        constantEpoch.invalidate();
        if(kind==0)recording=on;else if(kind==1)rawDepth=on;else enabled=!on;
        constantEpoch.invalidate();
    }
    // Test hook: the Clock's serials near the top (overflow); Clock is standard-layout, value_ first.
    void nearOverflow(){
        static_assert(std::is_standard_layout<NorthlightConstantEpoch::Clock>::value,"Clock layout");
        NorthlightConstantEpoch::Stamp words=constantEpoch.live();words.reset=std::numeric_limits<std::uint64_t>::max()-1;
        std::memcpy(static_cast<void*>(&constantEpoch),&words,sizeof words);
    }
    bool fetch(Packet& p)const{
        const auto& u=p.constantUsage;
        std::memcpy(p.constantStorage+4*u.floats.first,floats+4*u.floats.first,u.floats.count*16);
        std::memcpy(p.boolStorage+u.booleans.first,booleans+u.booleans.first,u.booleans.count*4);
        std::memcpy(p.intStorage+4*u.integers.first,integers+4*u.integers.first,u.integers.count*16);
        return true;
    }
    bool fetchFloats(unsigned first,unsigned count,float* out)const{std::memcpy(out,floats+4*first,count*16);return true;}
};
// Counterfactual sources (each must change some result): an in-place compare that ignores the reset
// serial, and one that certifies while the mirror is inactive.
struct IgnoreResetSource {
    const Mirror* mirror;std::uint64_t pinned=0;mutable NorthlightConstantEpoch::Stamp words;
    bool attached()const{return true;}
    bool valid()const{return mirror->active()&&!mirror->constantEpoch.overflow();}
    const NorthlightConstantEpoch::Stamp& live()const{words=mirror->constantEpoch.live();words.reset=pinned;return words;}
    NorthlightConstantEpoch::Stamp snapshot()const{auto s=mirror->constantEpoch.stamp(mirror->active());s.reset=pinned;return s;}
};
struct CertifyInactiveSource {
    const Mirror* mirror;
    bool attached()const{return true;}
    bool valid()const{return !mirror->constantEpoch.overflow();}
    const NorthlightConstantEpoch::Stamp& live()const{return mirror->constantEpoch.live();}
    NorthlightConstantEpoch::Stamp snapshot()const{return mirror->constantEpoch.stamp(true);}
};
static bool sameStamp(const NorthlightConstantEpoch::Stamp& a,const NorthlightConstantEpoch::Stamp& b){
    return a.valid==b.valid&&a.reset==b.reset&&a.floats==b.floats&&a.floatWithout2==b.floatWithout2&&a.floatWithout4==b.floatWithout4&&
        a.booleans==b.booleans&&a.integers==b.integers&&!std::memcmp(a.floatBlock,b.floatBlock,sizeof a.floatBlock);
}
static bool sameStats(const Stats& a,const Stats& b){
    return a.tests==b.tests&&a.hits==b.hits&&a.bytesAvoided==b.bytesAvoided&&a.poseFastHits==b.poseFastHits&&a.blockTests==b.blockTests&&
        a.blockShared==b.blockShared&&a.blockRewrites==b.blockRewrites&&a.blockCopies==b.blockCopies&&a.registersReused==b.registersReused&&
        a.registersFetched==b.registersFetched&&a.poseBlockHits==b.poseBlockHits&&a.poseBytesAvoided==b.poseBytesAvoided&&a.selfChecks==b.selfChecks&&
        a.selfCheckMismatches==b.selfCheckMismatches&&!std::memcmp(a.dirtyBlocks,b.dirtyBlocks,sizeof a.dirtyBlocks);
}
// Which storage a packet's bank pointer names: its own (-1) or the packet (index) that owns it.
template<class Storage> static long owner(const std::vector<std::unique_ptr<Packet>>& run,size_t index,const void* pointer,Storage storage){
    for(size_t i=0;i<=index;++i)if(pointer==storage(*run[i]))return i==index?-1:long(i);
    return -2;
}
static unsigned samePackets(const std::vector<std::unique_ptr<Packet>>& a,const std::vector<std::unique_ptr<Packet>>& b,size_t i){
    const Packet& x=*a[i];const Packet& y=*b[i];const auto& u=x.constantUsage;
    auto floats=[](const Packet& p)->const void*{return p.constantStorage;};
    auto bools=[](const Packet& p)->const void*{return p.boolStorage;};
    auto ints=[](const Packet& p)->const void*{return p.intStorage;};
    if(owner(a,i,x.constants,floats)!=owner(b,i,y.constants,floats)||owner(a,i,x.bools,bools)!=owner(b,i,y.bools,bools)||
       owner(a,i,x.ints,ints)!=owner(b,i,y.ints,ints))return 1;
    if(std::memcmp(x.constants+4*u.floats.first,y.constants+4*u.floats.first,u.floats.count*16)||
       std::memcmp(x.bools+u.booleans.first,y.bools+u.booleans.first,u.booleans.count*4)||
       std::memcmp(x.ints+4*u.integers.first,y.ints+4*u.integers.first,u.integers.count*16))return 2;
    if(!sameStamp(x.constantStamp,y.constantStamp))return 3;
    return x.constantGroup==y.constantGroup?0:4;
}
struct Verified {size_t draw;unsigned first,count;bool operator==(const Verified& o)const{return draw==o.draw&&first==o.first&&count==o.count;}};
// One capture path over the sequence. Kind 0: the 0.3.179 Reader overload (stats on self-check frames
// only, as the renderer passed them). Templated kinds keep the profile Stats (stats) apart from the
// self-check object (check.stats): 1 ReaderSource, 2 ClockSource, 3 ClockSource with profile stats on
// every frame (C0: must not move a verify()), 4/5 the counterfactual sources.
struct Path {
    unsigned kind;std::vector<std::unique_ptr<Packet>> packets;Stats stats,profile,pose;SelfCheck check;std::vector<Verified> verified;
    unsigned differences=0;
    template<class Src> bool run(Packet& p,const Packet* previous,const Src& source,Mirror& m,bool selfCheck,bool profiled){
        check.stats=selfCheck?&stats:nullptr;
        return captureBlocks(p,previous,source,[&]{return m.fetch(p);},[&](unsigned first,unsigned count,float* out){return m.fetchFloats(first,count,out);},
            profiled?&profile:selfCheck?&stats:nullptr,selfCheck,check);
    }
    bool capture(Packet& p,Mirror& m,bool selfCheck){
        const Packet* previous=packets.empty()?nullptr:packets.back().get();
        const auto checks=stats.selfChecks;bool ok=false;
        if(kind==0)ok=captureBlocks(p,previous,&m,Mirror::reader,[&]{return m.fetch(p);},[&](unsigned first,unsigned count,float* out){return m.fetchFloats(first,count,out);},selfCheck?&stats:nullptr);
        else if(kind==1)ok=run(p,previous,ReaderSource{&m,Mirror::reader,{}},m,selfCheck,false);
        else if(kind==2)ok=run(p,previous,ClockSource<Mirror>{&m.constantEpoch,&m},m,selfCheck,false);
        else if(kind==3)ok=run(p,previous,ClockSource<Mirror>{&m.constantEpoch,&m},m,selfCheck,true);
        else if(kind==4)ok=run(p,previous,IgnoreResetSource{&m,7,{}},m,selfCheck,false);
        else ok=run(p,previous,CertifyInactiveSource{&m},m,selfCheck,false);
        if(stats.selfChecks!=checks)verified.push_back({packets.size(),p.constantUsage.floats.first,p.constantUsage.floats.count});
        if(ok)p.constantGroup=previous?previous->constantGroup+!samePose(*previous,p,&pose):0;
        return ok;
    }
    void frame(){stats={};profile={};check.serial=0;for(auto& p:packets)reset(*p);packets.clear();}
};
static void sources(){
    std::mt19937 random(180);Mirror m;
    Path paths[6];for(unsigned k=0;k<6;++k)paths[k].kind=k;
    const NorthlightShaderConstants::Range layouts[]={{0,256},{0,256},{0,256},{2,32},{0,40},{2,120}};
    size_t draws=0,verified=0,inactiveDraws=0,invalidStamps=0,overflows=0;
    for(unsigned frame=0;frame<24;++frame){
        const bool selfCheck=frame%3!=1; // the renderer's frame%120==0 frames
        unsigned layout=0,kind=2;
        for(unsigned draw=0;draw<1600;++draw,++draws){
            const unsigned action=random()%100;
            float data[64*4];for(auto& v:data){uint32_t bits=random()%4?random()%64:random();std::memcpy(&v,&bits,4);}
            if(action<30){unsigned first=random()%260,count=1+random()%(random()%4?4:64);m.writeFloats(first,count,data);} // spans across block edges
            else if(action<42){unsigned first=random()%256,count=1+random()%24;if(first+count>256)count=256-first;float same[24*4];std::memcpy(same,m.floats+4*first,count*16);m.writeFloats(first,count,same);}
            else if(action<45){m.floats[4*(random()%256)]=float(random());m.constantEpoch.invalidate();} // state-block Apply
            else if(action<47)m.writeBool(random()%16,int(random()%2));
            else if(action<49)m.writeInt(random()%16,int(random()));
            else if(action<50)m.constantEpoch.invalidate(); // Reset
            else if(action<52){const unsigned which=random()%3;m.inactive(which,true);m.writeFloats(random()%256,1,data);m.inactive(which,false);}
            const bool inactive=random()%31==0;const unsigned which=random()%3;
            if(inactive){m.inactive(which,true);m.writeFloats(random()%250,2,data);++inactiveDraws;}
            if(frame==20&&draw==800){m.nearOverflow();m.constantEpoch.invalidate();m.constantEpoch.invalidate();assert(m.constantEpoch.overflow());++overflows;}
            if(draw%37==0)layout=random()%6;
            if(draw%61==0)kind=random()%2?1:2;
            for(auto& path:paths){
                auto p=std::make_unique<Packet>();p->constantUsage.floats=layouts[layout];p->projectionKind=kind;
                if(layout==3){p->constantUsage.booleans={2,4};p->constantUsage.integers={3,7};}
                if(layout==4){p->constantUsage.booleans={};p->constantUsage.integers={};}
                assert(path.capture(*p,m,selfCheck));path.packets.push_back(std::move(p));
            }
            const size_t i=paths[0].packets.size()-1;
            invalidStamps+=!paths[0].packets[i]->constantStamp.valid;
            {Packet reference;reference.constantUsage=paths[0].packets[i]->constantUsage;m.fetch(reference);equal(*paths[0].packets[i],reference);}
            for(unsigned k=1;k<4;++k)assert(!samePackets(paths[0].packets,paths[k].packets,i));
            for(unsigned k=4;k<6;++k)paths[k].differences+=samePackets(paths[0].packets,paths[k].packets,i)!=0;
            if(inactive)m.inactive(which,false);
            // Reference-equal Stats: the templated paths with the reference's single object (kinds 1, 2).
            for(unsigned k=1;k<3;++k)assert(sameStats(paths[0].stats,paths[k].stats)&&sameStats(paths[0].pose,paths[k].pose));
            // C0: profile counters apart; the self-check object keeps exactly the verify() counts.
            assert(paths[3].stats.selfChecks==paths[0].stats.selfChecks&&paths[3].stats.selfCheckMismatches==paths[0].stats.selfCheckMismatches);
        }
        for(unsigned k=0;k<4;++k)assert(paths[k].verified==paths[0].verified);
        // C0: the profile object counts every frame; the 0.3.179 object only self-check frames.
        assert(paths[3].profile.tests==1600&&(paths[3].profile.hits+paths[3].profile.blockTests>0)==(frame<=20));
        assert(selfCheck||(!paths[0].stats.tests&&!paths[2].stats.tests));
        verified+=paths[0].verified.size();
        for(auto& path:paths){path.verified.clear();path.frame();}
    }
    // The cadence was exercised (every 512th block hit of a self-check frame) and the counterfactuals bite.
    assert(verified>=10&&inactiveDraws>100&&invalidStamps>100&&overflows==1);
    assert(paths[4].differences>0&&paths[5].differences>0);
    std::printf("{\"sourceDraws\":%zu,\"verified\":%zu,\"inactiveDraws\":%zu,\"invalidStamps\":%zu,\"ignoreResetDifferences\":%u,\"certifyInactiveDifferences\":%u}\n",
        draws,verified,inactiveDraws,invalidStamps,paths[4].differences,paths[5].differences);
}
// C2 counterfactual (validation build only): a fetch that writes the bank (SetVertexShaderConstantF
// mid-fetch) breaks "after==before" and must trip the assertion. Block path, then the full-fetch path.
static void race(bool block){
    Mirror m;SelfCheck check;Packet first,second;
    auto run=[&](Packet& p,const Packet* previous,bool mutate){
        float row[4]={1,2,3,4};
        return captureBlocks(p,previous,ClockSource<Mirror>{&m.constantEpoch,&m},[&]{if(mutate)m.writeFloats(40,1,row);return m.fetch(p);},
            [&](unsigned f,unsigned c,float* out){if(mutate)m.writeFloats(40,1,row);return m.fetchFloats(f,c,out);},nullptr,false,check);
    };
    assert(run(first,nullptr,false));
    float row[4]={5,6,7,8};m.writeFloats(40,1,row);
    if(!block)m.constantEpoch.invalidate();
    run(second,&first,true);
}
// A detached ClockSource (no clock yet): never valid, nothing bound through null (UBSan), a plain fetch
// with no certificate and no epoch test counted, as the 0.3.179 path with a null Reader.
static void detached(){
    Mirror m;Stats stats;SelfCheck check;Packet p,q;
    assert(captureBlocks(p,static_cast<const Packet*>(nullptr),ClockSource<Mirror>{},[&]{return m.fetch(p);},[&](unsigned f,unsigned c,float* out){return m.fetchFloats(f,c,out);},&stats,true,check));
    assert(captureBlocks(q,&p,ClockSource<Mirror>{},[&]{return m.fetch(q);},[&](unsigned f,unsigned c,float* out){return m.fetchFloats(f,c,out);},&stats,true,check));
    assert(!p.constantStamp.valid&&!q.constantStamp.valid&&q.constants==q.constantStorage&&!stats.tests&&!stats.hits&&!stats.blockTests);
}
static void bankLoans(){
    Mirror m;
    ClockSource<Mirror> source{&m.constantEpoch,&m};
    const auto stamp=source.snapshot();
    const NorthlightConstantEpoch::BankHandoff bank{m.floats,stamp.reset,stamp.floats,stamp.booleans,stamp.integers,2,6};
    assert(source.borrow(&bank,2,4)==m.floats+8&&source.borrow(&bank,4,4)==m.floats+16);
    assert(!source.borrow(&bank,0,4)&&!source.borrow(&bank,5,4)&&!source.borrow(nullptr,2,4));
    assert(!ClockSource<Mirror>{}.borrow(&bank,2,4));
    m.held=false;assert(!source.borrow(&bank,2,4));
    m.held=true;m.recording=true;assert(!source.borrow(&bank,2,4));
    m.recording=false;m.constantEpoch.writeFloat(2,1);assert(!source.borrow(&bank,2,4));
    auto current=bank;current.floats=source.live().floats;
    assert(source.borrow(&current,2,4));
    m.constantEpoch.invalidate();assert(!source.borrow(&current,2,4));
}
static void packetBankLoans(){
    namespace E=NorthlightConstantEpoch;
    Mirror m;ClockSource<Mirror> source{&m.constantEpoch,&m};
    float material[1024]={};
    const auto certify=[&]{
        const auto s=source.snapshot();
        return E::BankHandoff{material,s.reset,s.floats,s.booleans,s.integers,2,32};
    };
    auto bank=certify();
    unsigned loans=0,gets=0,checks=0;
    Stats stats;SelfCheck check{0,&stats};
    const auto getter=[&](unsigned first,unsigned count,float* out){
        ++checks;return m.fetchFloats(first,count,out);
    };
    auto capturePacket=[&](Packet& p,const Packet* previous){
        p.constantUsage.floats={2,32};
        const auto captureRows=[&](unsigned first,unsigned count,float* out){
            if(source.copy(&bank,first,count,out)){++loans;return true;}
            ++gets;return m.fetchFloats(first,count,out);
        };
        return captureBlocks(p,previous,source,[&]{
            return captureRows(2,32,p.constantStorage+8);
        },captureRows,&stats,true,check,getter);
    };
    Packet first,dirty,stale;
    assert(capturePacket(first,nullptr)&&loans==1&&gets==0&&first.constants==first.constantStorage);
    const float newRows[4]={5,6,7,8};
    m.writeFloats(20,1,newRows);
    bank=certify(); // Negative control: a falsely certified scratch lane.
    material[80]=-5;
    check.serial=SelfCheckInterval-1;
    assert(capturePacket(dirty,&first)&&loans==2&&gets==0&&checks==1);
    assert(stats.selfCheckMismatches==1&&!dirty.constantStamp.valid);
    assert(!std::memcmp(dirty.constants+80,newRows,16)&&first.constants[80]==0);
    m.constantEpoch.invalidate();
    assert(capturePacket(stale,&dirty)&&loans==2&&gets==1);
    assert(!std::memcmp(stale.constants+80,newRows,16));
    std::puts("SHADOW_PACKET_BANK_PASS owned_copy=1 stale_fallback=1 independent_repair=1");
}
int main(int argc,char** argv){
    if(argc>1){const std::string mode=argv[1];race(mode=="c2-race-block");std::puts("race not detected");return 0;}
    rigidPointPose();detached();sources();bankLoans();packetBankLoans();
    edges();std::mt19937 random(127);Source source;Stats stats;
    size_t frames=100,draws=300,legacyBytes=0,accepted=0;
    for(size_t frame=0;frame<frames;++frame){
        source.clock.invalidate();std::vector<std::unique_ptr<Packet>> actual,reference;
        for(size_t draw=0;draw<draws;++draw){
            if(draw%7==0){unsigned bank=random()%3,reg=random()%(bank?16:256);uint32_t bits=random();
                if(bank==0){std::memcpy(source.floats+reg*4,&bits,4);source.clock.writeFloat(reg,1);}
                if(bank==1){std::memcpy(source.booleans+reg,&bits,4);source.clock.writeBool(1);}
                if(bank==2){std::memcpy(source.integers+reg*4,&bits,4);source.clock.writeInt(1);}}
            if(draw%29==0){source.clock.invalidate();}
            source.enabled=draw%41>1;
            auto p=std::make_unique<Packet>(),r=std::make_unique<Packet>();
            if((draw/47)%2){p->constantUsage.floats={2,32};p->constantUsage.booleans={2,4};p->constantUsage.integers={3,7};}
            p->projectionKind=(draw/79)%2?1:2;r->projectionKind=p->projectionKind;r->constantUsage=p->constantUsage;
            const auto* previous=actual.empty()?nullptr:actual.back().get();
            assert(get(*p,previous,source,&stats));
            const auto before=source.bytes;assert(source.fetch(*r));legacyBytes+=source.bytes-before;
            equal(*p,*r);
            p->constantGroup=previous?previous->constantGroup+!samePose(*previous,*p,&stats):0;
            r->constantGroup=reference.empty()?0:reference.back()->constantGroup+!NorthlightReplayPoses::sameCaptured(*reference.back(),*r);
            assert(p->constantGroup==r->constantGroup);
            if(draw%19==0){reset(*p);assert(p->constants==p->constantStorage);continue;}
            ++accepted;actual.push_back(std::move(p));reference.push_back(std::move(r));
        }
        // Old captures must remain immutable through subsequent writes and every cascade.
        for(unsigned pass=0;pass<6;++pass){
            NorthlightReplayPoses::Pass<int> a,b;float projection[16];for(auto& value:projection)value=float(random());
            for(size_t i=0;i<actual.size();++i){equal(*actual[i],*reference[i]);
                assert(a.prepare(*actual[i],projection)&&b.prepare(*reference[i],projection));
                assert(a.floats.first==b.floats.first&&a.floats.count==b.floats.count);
                assert(a.booleans.first==b.booleans.first&&a.booleans.count==b.booleans.count);
                assert(a.integers.first==b.integers.first&&a.integers.count==b.integers.count);
                const auto u=actual[i]->constantUsage.floats;assert(!std::memcmp(a.desired()+4*u.first,b.desired()+4*u.first,u.count*16));
            }
            assert(a.prepared==b.prepared&&a.reused==b.reused);
        }
        // Destruction/reset may happen in any order after all consumers finish.
        for(auto& p:actual)reset(*p);
    }
    assert(stats.hits>10000&&stats.poseFastHits>10000&&stats.bytesAvoided>10000000);
    std::printf("{\"draws\":%zu,\"accepted\":%zu,\"snapshotHits\":%llu,\"poseFastHits\":%llu,\"bytesAvoided\":%llu,\"legacyBytes\":%zu}\n",frames*draws,accepted,(unsigned long long)stats.hits,(unsigned long long)stats.poseFastHits,(unsigned long long)stats.bytesAvoided,legacyBytes);
}
