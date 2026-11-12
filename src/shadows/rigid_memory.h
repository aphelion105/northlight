#pragma once
// 0.3.172 rigid memory. A server-placed rigid prop (a shop sign, an event fence) is a per-frame
// replay: it casts only in frames in which the game draws it, so its shadow vanished when the
// camera turned away from it (or when the capture budget turned its draw away). This registry
// remembers the last captured replay of a settled, free-standing, small rigid group and has the
// renderer draw a copy of it (a replay draw only: no cache signature, no conversion) while the
// game does not draw it.
// Remembered: every draw of the group is driven by ONE palette bone (the renderer's audited
// programs and rigidBones), at most maxDraws/maxTriangles/maxMeshBytes, its bone's world matrix
// W (worldBone) has stayed within stillTolerance/axisTolerance for settleMs AND settleFrames
// capture frames, it lies within rememberRange of the pivot (0.3.173; entries drop beyond
// range: a hysteresis band), and it was observed on heldMinFrames complete frames. Never
// remembered: mobile (sticky: W travelled more than travelTolerance or turned more than
// turnTolerance since the track began: lifts, boats, a door opened while seen), held (0.3.173,
// a state: a non-rigid group's palette root within attachRadius on at least heldRatio of the
// complete capture frames it was observed on, a rolling window of heldWindow; bodies on a
// static-cache placement are doodads and hold nothing: weapons, shields), static (until scene revision: the
// static cache draws it: staticPlacement at W). Held never drops an entry: only motion does.
// Identity: a hash of the draws' shapes and W's origin within identityTolerance, never the
// snapshot pointer (a re-created snapshot keeps the entry; the renderer refreshes its copy on
// every capture frame in which it is seen). Observations beyond range make no track.
// Each capture frame an entry is (State): LiveSelected (observed with selected draws: the live
// replay casts), LiveUnselected (observed, but selection kept none of its draws: respected, not
// drawn), DrawnNotCaptured (the game drew it - markDrawn(), the renderer's test at capture, on
// bone 0's matrix W0 - but it has no replay: budget, 4096 cap, blend fade-in, projection: drawn
// by us), DrawnMoved (drawn with W0 turned beyond turnTolerance: dropped at once, mobile) or
// NotDrawn (drawn by us). A NotDrawn entry whose origin is in view within despawnRange of the
// eye on complete frames (no capture shortfall) for despawnMs AND despawnFrames uninterrupted
// is a despawn: dropped, and its track must settle afresh. Also dropped: beyond range from the
// pivot, after unseenMs, seen again with another W (moved, turned). A pivot jump beyond
// teleportDistance clears everything; so do the renderer's map change, device loss and trim.
// Caps: maxEntries and maxBytes of mesh (the farthest from the pivot goes), maxTracks. A track
// without an entry is forgotten after shortForgetMs unseen when unsettled, held or mobile, else
// after forgetMs. Portable: the Payload (the renderer's replay copies) is opaque here; no D3D.
#include "rigid_geometry.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <unordered_map>
#include <utility>
#include <vector>
namespace NorthlightRigidMemory {
using NorthlightRigidGeometry::Vec3;
struct Tuning {
    unsigned settleMs=2000,settleFrames=4;          /* time AND capture frames: cadence and frame rate do not rescale it */
    float stillTolerance=.02f,axisTolerance=.01f;    /* W origin (yd) and basis (times its scale) */
    float travelTolerance=.5f,turnTolerance=.05f;    /* since the track began: mobile for good */
    float attachRadius=4.f,heldRatio=.8f,staticBodyTolerance=.25f; /* held: a body root this near on this share of complete frames */
    unsigned heldMinFrames=8,heldWindow=64;          /* complete observed frames before held/remember is judged; halved at the window */
    float identityTolerance=.25f;                    /* same shapes, W origin this near: the same object */
    unsigned maxDraws=4,maxTriangles=4096;std::size_t maxMeshBytes=256u<<10;
    float despawnRange=25.f;unsigned despawnMs=1000,despawnFrames=6; /* NotDrawn in view this near the eye: a despawn */
    float range=80.f,rememberRange=72.f,teleportDistance=100.f;unsigned unseenMs=600000;
    std::size_t maxEntries=64,maxBytes=2u<<20,maxTracks=2048;unsigned forgetMs=60000,shortForgetMs=2000;
};
// One captured rigid group of this capture frame, built by the renderer.
struct Observation {
    std::uint64_t shape=0;float world[12]={}; /* W: world images of the bone's model axes (9), then of its origin (3) (worldBone) */
    float world0[12]={};bool hasWorld0=false; /* W0: palette bone 0 (c31..c33), the renderer's drawn test at capture */
    bool selected=true; /* false: LiveUnselected (captured, selection kept none of its draws): matched only */
    unsigned draws=0,triangles=0;std::size_t bytes=0; /* bytes: mesh bytes of its draws */
    enum Action : unsigned char {None,Refresh,Remember};
    Action action=None;std::uint32_t track=0;float distanceSquared=0; /* out: store() a fresh copy for Refresh/Remember */
};
// Identity of a group's shapes: each draw's original shader, declaration, vertex and primitive
// counts and mesh bytes, in draw order (start with ShapeSeed). Never the snapshot pointer.
constexpr std::uint64_t ShapeSeed=14695981039346656037ull;
inline void mixShape(std::uint64_t& h,const void* shader,const void* declaration,unsigned vertices,unsigned primitives,std::size_t bytes){
    for(std::uint64_t n:{std::uint64_t(reinterpret_cast<std::uintptr_t>(shader)),std::uint64_t(reinterpret_cast<std::uintptr_t>(declaration)),std::uint64_t(vertices),std::uint64_t(primitives),std::uint64_t(bytes)})h=(h^n)*1099511628211ull;}
enum class State : unsigned char {NotDrawn,LiveSelected,LiveUnselected,DrawnNotCaptured,DrawnMoved};
inline const char* stateName(State s){static const char* n[]={"notDrawn","liveSelected","liveUnselected","drawnNotCaptured","drawnMoved"};return n[unsigned(s)];}
enum class Reason : unsigned char {None,Range,InView,Moved,DrawnMoved,Unseen,Evicted,Teleport};
inline const char* reasonName(Reason r){static const char* n[]={"none","range","inView","moved","drawnMoved","unseen","evicted","teleport"};return n[unsigned(r)];}
// F6 diagnostics (recorded only while events(true)): remember, refresh-skip, drop, held on/off, static.
struct Event {enum Kind : unsigned char {Remember,RefreshSkip,Drop,HeldOn,HeldOff,Static} kind=Remember;Reason reason=Reason::None;State state=State::NotDrawn;
    std::uint64_t shape=0;float at[3]={},body[3]={},bodyDistance=0,ratio=0;bool hasBody=false;};
inline const char* eventName(Event::Kind k){static const char* n[]={"remember","refreshSkip","drop","heldOn","heldOff","static"};return n[unsigned(k)];}
struct Stats {std::size_t tracks=0,entries=0,seen=0,held=0,statics=0,mobile=0,bytes=0;
    std::size_t liveSelected=0,liveUnselected=0,drawn=0,drawnNotCaptured=0,notDrawn=0; /* this capture frame */
    std::uint64_t remembered=0,droppedInView=0,droppedRange=0,droppedTeleport=0,droppedMoved=0,droppedDrawnMoved=0,droppedUnseen=0,evicted=0,cleared=0,
        rememberGateFar=0,tracksForgotten=0,refreshSkipped=0;double sortMs=0;};
// (original shader, primitive count) of every remembered draw: the renderer's cheap first test
// of a game draw at capture (one probe; a miss costs nothing more). Open addressing, no allocation.
class DrawKeySet {
    struct Slot {const void* shader=nullptr;unsigned count=0;};
    static constexpr std::size_t Size=1024; /* > 4 x maxEntries: the load stays low */
    Slot slots_[Size];std::size_t used_=0;
    static std::size_t hash(const void* shader,unsigned count){std::uint64_t h=std::uint64_t(reinterpret_cast<std::uintptr_t>(shader))*0x9e3779b97f4a7c15ull^(std::uint64_t(count)*0xc2b2ae3d27d4eb4full);return std::size_t(h^(h>>29))&(Size-1);}
public:
    void clear(){if(used_)for(auto& s:slots_)s=Slot{};used_=0;}
    bool empty()const{return !used_;}
    std::size_t size()const{return used_;}
    bool add(const void* shader,unsigned count){if(!shader||used_>=Size/2)return false;
        for(std::size_t i=hash(shader,count);;i=(i+1)&(Size-1)){auto& s=slots_[i];if(s.shader==shader&&s.count==count)return true;if(!s.shader){s={shader,count};++used_;return true;}}}
    bool contains(const void* shader,unsigned count)const{if(!used_)return false;
        for(std::size_t i=hash(shader,count);;i=(i+1)&(Size-1)){const auto& s=slots_[i];if(!s.shader)return false;if(s.shader==shader&&s.count==count)return true;}}
};
// Palette rows (3 x 4 floats, the c31+3b layout) that put W in the view of `inverseView`
// (view -> world; its 3x3 part is inverted, not transposed): worldBone(rows, inverseView) == W.
inline bool rebase(const float* world,const float* inverseView,float* rows){
    double m[3][3],inv[3][3];for(unsigned r=0;r<3;++r)for(unsigned w=0;w<3;++w)m[r][w]=inverseView[4*r+w];
    const double det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    if(!std::isfinite(det)||std::fabs(det)<1e-12)return false;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c){const unsigned r1=(c+1)%3,r2=(c+2)%3,c1=(r+1)%3,c2=(r+2)%3;
        inv[r][c]=(m[r1][c1]*m[r2][c2]-m[r1][c2]*m[r2][c1])/det;} /* inverse = adjugate / det */
    for(unsigned a=0;a<4;++a){double x[3];for(unsigned w=0;w<3;++w)x[w]=a<3?double(world[a*3+w]):double(world[9+w])-double(inverseView[12+w]);
        for(unsigned r=0;r<3;++r){double v=0;for(unsigned w=0;w<3;++w)v+=x[w]*inv[w][r];rows[4*r+a]=float(v);}}
    for(unsigned k=0;k<12;++k)if(!std::isfinite(rows[k]))return false;
    return true;
}
template<class Payload> class Registry {
public:
    struct Entry {std::uint32_t track=0;std::uint64_t shape=0;float world[12]={},world0[12]={};bool hasWorld0=false;Payload payload{};std::size_t bytes=0;
        unsigned lastSeenMs=0,absentFrames=0,absentSinceMs=0;State state=State::NotDrawn;bool drawn=false,drawnMoved=false;float distanceSquared=0;};
private:
    struct Track {std::uint64_t shape=0;std::uint32_t serial=0;float anchor[12]={},first[12]={},at[3]={};unsigned sinceMs=0,frames=0,lastSeenMs=0,completeFrames=0,nearFrames=0;
        int entry=-1; /* index in entries_ */bool used=false,mobile=false,held=false,statics=false;signed char placed=0; /* static screen: -1 not static, 0 not asked */};
    Tuning t_;std::vector<Track> tracks_;std::vector<Entry> entries_;Stats stats_;std::vector<Event> events_;bool recordEvents_=false;
    std::unordered_map<std::uint32_t,std::size_t> trackIndex_; /* serial -> index, for the sorted tracks (rebuilt on a change) */
    std::vector<signed char> bodyStatic_;
    float lastPivot_[3]={};bool hasPivot_=false,screening_=false;std::uint32_t nextSerial_=1;
    static float distance2(const float* a,const float* b){float q=0;for(unsigned k=0;k<3;++k){const float d=a[k]-b[k];q+=d*d;}return q;}
    static float axesDifference(const float* a,const float* b){float m=0;for(unsigned k=0;k<9;++k)m=std::max(m,std::fabs(a[k]-b[k]));return m;}
    static float axesScale(const float* a){float m=1;for(unsigned k=0;k<9;++k)m=std::max(m,std::fabs(a[k]));return m;}
    bool still(const float* a,const float* b)const{return distance2(a+9,b+9)<=t_.stillTolerance*t_.stillTolerance&&axesDifference(a,b)<=t_.axisTolerance*axesScale(b);}
    bool settled(const Track& t,unsigned now)const{return t.frames>=t_.settleFrames&&unsigned(now-t.sinceMs)>=t_.settleMs;}
    // Tracks are ordered by shape (then serial) up to `sorted`; later ones are this frame's new tracks.
    std::size_t find(std::uint64_t shape,const float* at,std::size_t sorted)const{
        std::size_t best=SIZE_MAX;float bestSquared=t_.identityTolerance*t_.identityTolerance;
        auto visit=[&](std::size_t i){const auto& t=tracks_[i];if(t.used||t.shape!=shape)return;const float d=distance2(t.at,at);if(d<=bestSquared){bestSquared=d;best=i;}};
        auto it=std::lower_bound(tracks_.begin(),tracks_.begin()+std::ptrdiff_t(sorted),shape,[](const Track& t,std::uint64_t s){return t.shape<s;});
        for(std::size_t i=std::size_t(it-tracks_.begin());i<sorted&&tracks_[i].shape==shape;++i)visit(i);
        for(std::size_t i=sorted;i<tracks_.size();++i)visit(i);
        return best;
    }
    Track* track(std::uint32_t serial){auto it=trackIndex_.find(serial);if(it!=trackIndex_.end()&&it->second<tracks_.size()&&tracks_[it->second].serial==serial)return &tracks_[it->second];
        for(auto& t:tracks_)if(t.serial==serial)return &t; /* appended this frame */return nullptr;}
    static bool trackBefore(const Track& a,const Track& b){return a.shape!=b.shape?a.shape<b.shape:a.serial<b.serial;}
    void note(Event::Kind kind,std::uint64_t shape,const float* at,State state=State::NotDrawn,Reason reason=Reason::None){
        if(!recordEvents_||events_.size()>=256)return;Event e;e.kind=kind;e.shape=shape;std::memcpy(e.at,at,12);e.state=state;e.reason=reason;events_.push_back(e);}
    void drop(std::size_t i,std::uint64_t& counter,Reason why){++counter;auto& e=entries_[i];stats_.bytes-=std::min(stats_.bytes,e.bytes);note(Event::Drop,e.shape,e.world+9,e.state,why);
        if(auto* t=track(e.track))t->entry=-1;entries_.erase(entries_.begin()+std::ptrdiff_t(i));
        for(std::size_t j=i;j<entries_.size();++j)if(auto* t=track(entries_[j].track))t->entry=int(j);}
    std::size_t farthest()const{std::size_t f=SIZE_MAX;for(std::size_t i=0;i<entries_.size();++i)if(f==SIZE_MAX||entries_[i].distanceSquared>entries_[f].distanceSquared)f=i;return f;}
    // Room for `o` within the caps, evicting only entries farther from the pivot than it.
    bool room(const Observation& o)const{
        if(o.bytes>t_.maxBytes||!t_.maxEntries)return false;
        std::vector<char> gone;std::size_t count=entries_.size(),bytes=stats_.bytes;
        while(count+1>t_.maxEntries||bytes+o.bytes>t_.maxBytes){std::size_t f=SIZE_MAX;
            for(std::size_t i=0;i<entries_.size();++i){if(gone.size()>i&&gone[i])continue;if(f==SIZE_MAX||entries_[i].distanceSquared>entries_[f].distanceSquared)f=i;}
            if(f==SIZE_MAX||!(entries_[f].distanceSquared>o.distanceSquared))return false;
            gone.resize(entries_.size(),0);gone[f]=1;--count;bytes-=std::min(bytes,entries_[f].bytes);}
        return true;
    }
public:
    explicit Registry(Tuning t=Tuning{}):t_(t){}
    const Tuning& tuning()const{return t_;}
    const Stats& stats()const{return stats_;}
    const std::vector<Entry>& entries()const{return entries_;}
    bool small(const Observation& o)const{return o.draws&&o.draws<=t_.maxDraws&&o.triangles<=t_.maxTriangles&&o.bytes<=t_.maxMeshBytes;}
    // A settled track waits for the static-cache screen (the renderer builds its index meanwhile).
    bool screening()const{return screening_;}
    // Placement decisions belong to one static scene revision. Keep settled poses,
    // motion/held classification and retained payloads when that scene changes.
    void invalidateStaticCoverage(){for(auto& t:tracks_)if(t.entry<0){t.statics=false;t.placed=0;}}
    // Map change, device loss, trim, shadows off: everything is forgotten (payloads released).
    void clear(){stats_.cleared+=entries_.size();entries_.clear();tracks_.clear();trackIndex_.clear();stats_.bytes=0;hasPivot_=false;screening_=false;}
    // Tests: tracks strictly in (shape, serial) order (= a full sort), the serial index exact, every
    // entry's track pointing back at it.
    bool consistent()const{
        for(std::size_t i=1;i<tracks_.size();++i)if(!trackBefore(tracks_[i-1],tracks_[i]))return false;
        if(trackIndex_.size()!=tracks_.size())return false;
        for(std::size_t i=0;i<tracks_.size();++i){auto it=trackIndex_.find(tracks_[i].serial);if(it==trackIndex_.end()||it->second!=i)return false;}
        std::size_t linked=0;for(const auto& t:tracks_)if(t.entry>=0){++linked;if(std::size_t(t.entry)>=entries_.size()||entries_[std::size_t(t.entry)].track!=t.serial)return false;}
        return linked==entries_.size();
    }
    // The cumulative counters (the renderer: on a map change).
    void resetStats(){const auto bytes=stats_.bytes;stats_=Stats{};stats_.bytes=bytes;stats_.entries=entries_.size();stats_.tracks=tracks_.size();}
    void events(bool on){recordEvents_=on;if(!on)events_.clear();}
    // Frame boundary: drawn marks belong to the capture frame they were made in.
    void clearDrawn(){for(auto& e:entries_)e.drawn=e.drawnMoved=false;}
    void takeEvents(std::vector<Event>& out){out.swap(events_);events_.clear();}
    // The game drew a draw whose (shader, primitive count) is in an entry (match(entry) true)
    // with bone 0's world matrix `w0`, this capture frame (before any capture rejection).
    template<class Match> void markDrawn(const float* w0,Match match){
        for(auto& e:entries_){if(!e.hasWorld0||e.drawn||distance2(w0+9,e.world0+9)>t_.identityTolerance*t_.identityTolerance||!match(e))continue;
            const float turn=axesDifference(w0,e.world0),scale=axesScale(e.world0);
            if(turn<=t_.turnTolerance*scale){e.drawn=true;e.drawnMoved=false;}else e.drawnMoved=true;}
    }
    // One capture frame. bodies: palette roots (3 floats each) of this frame's non-rigid groups.
    // complete: no draw was lost to capture limits. covered(n): 1 the static cache draws obs[n],
    // 0 it does not, -1 not known yet (asked again next frame). staticBody(root): the root is a
    // static-cache placement origin (a doodad: it holds nothing); asked only for near bodies.
    template<class Covered,class StaticBody> void frame(std::vector<Observation>& obs,const float* bodies,std::size_t bodyCount,unsigned now,const float* pivot,
                                       const float* inverseView,const float* projection,bool complete,Covered covered,StaticBody staticBody){
        screening_=false;
        if(pivot){if(hasPivot_&&distance2(pivot,lastPivot_)>t_.teleportDistance*t_.teleportDistance){
                for(const auto& e:entries_)note(Event::Drop,e.shape,e.world+9,e.state,Reason::Teleport);
                stats_.droppedTeleport+=entries_.size();entries_.clear();tracks_.clear();trackIndex_.clear();stats_.bytes=0;}
            std::memcpy(lastPivot_,pivot,12);hasPivot_=true;}
        for(auto& t:tracks_)t.used=false;for(auto& e:entries_)e.state=State::NotDrawn;
        bodyStatic_.assign(bodyCount,-1);
        const std::size_t sorted=tracks_.size();const float attach2=t_.attachRadius*t_.attachRadius,range2=t_.range*t_.range;bool changed=false;
        for(std::size_t n=0;n<obs.size();++n){auto& o=obs[n];o.action=Observation::None;o.track=0;o.distanceSquared=pivot?distance2(o.world+9,pivot):0;
            if(o.distanceSquared>range2)continue; /* no track beyond range */
            std::size_t k=find(o.shape,o.world+9,sorted);
            if(!o.selected){ /* LiveUnselected: its track and entry only */
                if(k==SIZE_MAX)continue;auto& t=tracks_[k];t.used=true;t.lastSeenMs=now;o.track=t.serial;
                if(t.entry>=0){auto& e=entries_[std::size_t(t.entry)];e.state=State::LiveUnselected;e.lastSeenMs=now;e.absentFrames=0;e.distanceSquared=o.distanceSquared;
                    if(!still(o.world,e.world)){t.mobile=true;drop(std::size_t(t.entry),stats_.droppedMoved,Reason::Moved);}}
                continue;}
            if(k==SIZE_MAX){Track f;f.shape=o.shape;f.serial=nextSerial_++;if(!nextSerial_)nextSerial_=1;std::memcpy(f.anchor,o.world,48);std::memcpy(f.first,o.world,48);
                f.sinceMs=f.lastSeenMs=now;f.frames=1;tracks_.push_back(f);k=tracks_.size()-1;changed=true;}
            else{auto& t=tracks_[k];t.lastSeenMs=now;
                if(still(o.world,t.anchor))++t.frames;else{std::memcpy(t.anchor,o.world,48);t.sinceMs=now;t.frames=1;t.placed=0;}
                if(distance2(o.world+9,t.first+9)>t_.travelTolerance*t_.travelTolerance||axesDifference(o.world,t.first)>t_.turnTolerance*axesScale(t.first))t.mobile=true;}
            auto& t=tracks_[k];t.used=true;std::memcpy(t.at,o.world+9,12);o.track=t.serial;
            // Held: the share of complete observed frames with a (non-doodad) body root within attachRadius.
            if(complete){std::size_t nearest=SIZE_MAX;float best=attach2;
                for(std::size_t b=0;b<bodyCount;++b){const float d=distance2(bodies+3*b,o.world+9);if(!(d<=best))continue;
                    if(bodyStatic_[b]<0)bodyStatic_[b]=staticBody(bodies+3*b)?1:0;if(bodyStatic_[b])continue;
                    best=d;nearest=b;if(!recordEvents_)break;}
                ++t.completeFrames;t.nearFrames+=nearest!=SIZE_MAX;
                if(t.completeFrames>=t_.heldWindow){t.completeFrames/=2;t.nearFrames/=2;}
                const bool held=t.completeFrames>=t_.heldMinFrames&&float(t.nearFrames)>=t_.heldRatio*float(t.completeFrames);
                if(held!=t.held){t.held=held;
                    if(recordEvents_&&events_.size()<256){Event e;e.kind=held?Event::HeldOn:Event::HeldOff;e.shape=t.shape;std::memcpy(e.at,o.world+9,12);e.ratio=float(t.nearFrames)/float(t.completeFrames);
                        if(nearest!=SIZE_MAX){e.hasBody=true;std::memcpy(e.body,bodies+3*nearest,12);e.bodyDistance=std::sqrt(best);}events_.push_back(e);}}}
            if(t.entry>=0){auto& e=entries_[std::size_t(t.entry)];e.state=State::LiveSelected;e.lastSeenMs=now;e.absentFrames=0;e.distanceSquared=o.distanceSquared;
                if(!still(o.world,e.world)||t.mobile){t.mobile=true;drop(std::size_t(t.entry),stats_.droppedMoved,Reason::Moved);} /* a door opened: held never drops */
                else{o.action=Observation::Refresh;continue;}}
            if(t.mobile||t.held||t.statics||!small(o)||!settled(t,now)||t.completeFrames<t_.heldMinFrames)continue;
            if(o.distanceSquared>t_.rememberRange*t_.rememberRange){++stats_.rememberGateFar;continue;}
            if(!t.placed){const int c=covered(n);if(c>0){t.statics=true;note(Event::Static,t.shape,o.world+9);}else if(c==0)t.placed=-1;else screening_=true;}
            if(t.placed<0&&!t.statics&&room(o))o.action=Observation::Remember;
        }
        // Entries not observed this frame: drawn by the game (markDrawn), moved, or not drawn.
        stats_.liveSelected=stats_.liveUnselected=stats_.drawn=stats_.drawnNotCaptured=stats_.notDrawn=0;
        for(std::size_t i=0;i<entries_.size();){auto& e=entries_[i];stats_.drawn+=e.drawn;
            e.distanceSquared=pivot?distance2(e.world+9,pivot):e.distanceSquared;
            const bool drawn=e.drawn,moved=e.drawnMoved;e.drawn=e.drawnMoved=false;
            if(e.distanceSquared>range2){drop(i,stats_.droppedRange,Reason::Range);continue;}
            if(e.state==State::LiveSelected){++stats_.liveSelected;++i;continue;}
            if(e.state==State::LiveUnselected){++stats_.liveUnselected;++i;continue;}
            if(drawn){e.state=State::DrawnNotCaptured;e.lastSeenMs=now;e.absentFrames=0;++stats_.drawnNotCaptured;++i;continue;}
            if(moved){e.state=State::DrawnMoved;if(auto* t=track(e.track))t->mobile=true;drop(i,stats_.droppedDrawnMoved,Reason::DrawnMoved);continue;}
            ++stats_.notDrawn;
            if(unsigned(now-e.lastSeenMs)>=t_.unseenMs){drop(i,stats_.droppedUnseen,Reason::Unseen);continue;}
            if(complete){const Vec3 at(e.world[9],e.world[10],e.world[11]);
                if(NorthlightRigidGeometry::centreInView(inverseView,projection,at,at,t_.despawnRange)){if(!e.absentFrames++)e.absentSinceMs=now;
                    if(e.absentFrames>=t_.despawnFrames&&unsigned(now-e.absentSinceMs)>=t_.despawnMs){
                        if(auto* t=track(e.track)){t->frames=0;t->sinceMs=now;} /* a fresh settle before it is remembered again */
                        drop(i,stats_.droppedInView,Reason::InView);continue;}}
                else e.absentFrames=0;}
            ++i;}
        // Forget tracks (never one with an entry): unsettled, held or mobile after shortForgetMs
        // unseen, settled free ones after forgetMs; then the oldest above maxTracks.
        // Order-preserving: `prefix` counts the survivors of the ordered part [0,sorted) (this frame's
        // new tracks follow them); positions before `first` keep their index.
        const std::size_t before=tracks_.size();std::size_t prefix=sorted,first=tracks_.size();
        auto compact=[&](auto gone){std::size_t w=0,kept=0;
            for(std::size_t i=0;i<tracks_.size();++i){auto& t=tracks_[i];if(gone(t)){trackIndex_.erase(t.serial);first=std::min(first,w);continue;}
                kept+=i<prefix;if(w!=i)tracks_[w]=std::move(t);++w;}
            tracks_.resize(w);prefix=kept;};
        compact([&](const Track& t){
            const bool free=!t.mobile&&!t.held&&t.frames>=t_.settleFrames&&unsigned(t.lastSeenMs-t.sinceMs)>=t_.settleMs;
            return t.entry<0&&unsigned(now-t.lastSeenMs)>=(free?t_.forgetMs:t_.shortForgetMs);});
        if(tracks_.size()>t_.maxTracks){std::vector<std::pair<unsigned,std::uint32_t>> age;for(const auto& t:tracks_)if(t.entry<0)age.push_back({unsigned(now-t.lastSeenMs),t.serial});
            std::sort(age.begin(),age.end(),[](const auto& a,const auto& b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
            std::vector<std::uint32_t> gone;for(std::size_t i=0;i<age.size()&&tracks_.size()-gone.size()>t_.maxTracks;++i)gone.push_back(age[i].second);std::sort(gone.begin(),gone.end());
            compact([&](const Track& t){return std::binary_search(gone.begin(),gone.end(),t.serial);});}
        stats_.tracksForgotten+=before-tracks_.size();changed=changed||tracks_.size()!=before;
        // (shape, serial) order: the new tracks sorted and merged into the ordered survivors (the keys are
        // unique, so this is exactly the full sort); only positions from the first change are reindexed.
        if(changed){const auto started=std::chrono::steady_clock::now();
            const auto middle=tracks_.begin()+std::ptrdiff_t(prefix);std::size_t start=first;
            if(middle!=tracks_.end()){std::sort(middle,tracks_.end(),trackBefore);
                start=std::min(start,std::size_t(std::lower_bound(tracks_.begin(),middle,*middle,trackBefore)-tracks_.begin()));
                std::inplace_merge(tracks_.begin(),middle,tracks_.end(),trackBefore);}
            for(std::size_t i=start;i<tracks_.size();++i)trackIndex_[tracks_[i].serial]=i;
            for(std::size_t j=0;j<entries_.size();++j)if(auto* t=track(entries_[j].track))t->entry=int(j);
            stats_.sortMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();}
        else stats_.sortMs=0;
        stats_.tracks=tracks_.size();stats_.held=stats_.statics=stats_.mobile=0;
        for(const auto& t:tracks_){stats_.held+=t.held;stats_.statics+=t.statics;stats_.mobile+=t.mobile;}
        stats_.seen=stats_.liveSelected+stats_.liveUnselected;stats_.entries=entries_.size();
    }
    // 0.3.176 (S3'): an observation frame() marked Refresh updates its entry's payload in place:
    // fill(payload) first (a throw leaves the entry as it was), then the byte accounting. False: no
    // entry (refreshSkipped), fill not called.
    template<class Fill> bool refresh(const Observation& o,Fill fill){
        if(o.action!=Observation::Refresh)return false;
        Track* t=track(o.track);if(!t||t->entry<0){++stats_.refreshSkipped;note(Event::RefreshSkip,o.shape,o.world+9);return false;}
        auto& e=entries_[std::size_t(t->entry)];fill(e.payload);stats_.bytes=stats_.bytes-std::min(stats_.bytes,e.bytes)+o.bytes;e.bytes=o.bytes;return true;
    }
    // A fresh copy for an observation frame() marked Refresh (replaces the entry's) or Remember
    // (a new entry; the farthest entries beyond the caps go). False: nothing stored.
    bool store(const Observation& o,Payload&& payload,unsigned now){
        if(o.action==Observation::Refresh)return refresh(o,[&](Payload& p){p=std::move(payload);});
        if(o.action!=Observation::Remember)return false;Track* t=track(o.track);if(!t||t->entry>=0||!room(o))return false;
        while(entries_.size()+1>t_.maxEntries||stats_.bytes+o.bytes>t_.maxBytes){const std::size_t f=farthest();if(f==SIZE_MAX)return false;drop(f,stats_.evicted,Reason::Evicted);}
        Entry e;e.track=o.track;e.shape=o.shape;std::memcpy(e.world,o.world,48);std::memcpy(e.world0,o.world0,48);e.hasWorld0=o.hasWorld0;e.payload=std::move(payload);e.bytes=o.bytes;
        e.lastSeenMs=now;e.state=State::LiveSelected;e.distanceSquared=o.distanceSquared;
        entries_.push_back(std::move(e));if((t=track(o.track)))t->entry=int(entries_.size()-1);stats_.bytes+=o.bytes;++stats_.remembered;stats_.entries=entries_.size();
        note(Event::Remember,o.shape,o.world+9,State::LiveSelected);return true;
    }
    // Entries the game did not draw (NotDrawn) or drew without a replay (DrawnNotCaptured) this
    // capture frame: the renderer draws their copies.
    template<class F> void forAbsent(F f){for(auto& e:entries_)if(e.state==State::NotDrawn||e.state==State::DrawnNotCaptured)f(e);}
};
}
