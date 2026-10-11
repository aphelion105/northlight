#pragma once
// 0.3.210 (task 31): capture admission. The capture budget (CaptureBudgetMiB) was spent in the game's
// draw order, so characters far beyond ActorShadowRadius used it up (and the selection then threw them
// away) while nearby ones got nothing. Two decisions per skinned draw, taken from its palette root
// (Replay::captureRoot) BEFORE the snapshot read:
//  - Radius gate (task 1): a draw whose identity {shader, primitive count, root} matches a body the
//    selection (or this gate last frame) classified as outside the radius, and whose root is beyond
//    gateYards = yards + 2*band + ClusterExtent + 4, is not captured at all. The list sustains itself
//    (a gated draw refreshes its own entry on an exact identity match; there is no proximity
//    spreading, so a rigid prop or a new NPC next to a gated body is never gated). Only with the
//    self established (centre.self) and ActorShadowRadius > 0.
//  - Distance cut (task 2): the budget is spent nearest-first. Every rooted skinned draw that is not
//    radius-gated leaves a record {distance from the centre, bytes} (admitted: the real charged bytes;
//    refused: an estimate from the shape's earlier captures). At the end of the frame the records are
//    sorted by distance and the cut D is where the cumulative bytes reach
//    (limit - non-priority bytes) * 0.92. D shrinks only after real exhaustion on 2 capture frames in a
//    row (at most band(D) per frame), grows only after GrowFrames frames with room (at most band(D) per
//    step), is infinite when everything fits and never goes below MinCut. A root in (D, D+band(D)]
//    admitted last frame stays admitted (per-actor deadband).
// Near-reserve hits always pass both. A draw without a root is never gated or cut. Portable: no D3D calls.
#include "actor_shadow_selection.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace NorthlightCaptureAdmission {
constexpr float CellYards=4.f,MinCut=12.f,Margin=.92f,MatchYards=1.5f,MatchCap=CellYards,GateExtra=4.f,GIYards=100.f,Teleport=100.f,CountCell=2.f;
constexpr unsigned GrowFrames=10,ShrinkFrames=2;
constexpr std::size_t ShapeCap=8192;
inline float gateYards(float yards){return yards+2*NorthlightActorShadowSelection::band(yards)+NorthlightActorShadowSelection::ClusterExtent+GateExtra;}
struct Item {std::uintptr_t shader=0;unsigned count=0;float at[3]={};};
// Items of one identity found by cell: 4 yd cells (x, y), nine probed per query (tolerance <= CellYards).
class Grid {
    std::vector<Item> items_;std::vector<std::uint32_t> head_,next_;std::size_t mask_=0;
    static std::int32_t cell(float v){return std::int32_t(std::floor(v/CellYards));}
    static std::size_t slot(std::uintptr_t shader,unsigned count,std::int32_t x,std::int32_t y){
        std::uint64_t h=std::uint64_t(shader)*0x9e3779b97f4a7c15ULL^std::uint64_t(count)*0xc2b2ae3d27d4eb4fULL^std::uint64_t(std::uint32_t(x))*0x165667b19e3779f9ULL^std::uint64_t(std::uint32_t(y))*0x27d4eb2f165667c5ULL;
        h^=h>>29;return std::size_t(h);}
public:
    void clear(){items_.clear();head_.clear();next_.clear();mask_=0;}
    std::size_t size()const{return items_.size();}
    const std::vector<Item>& items()const{return items_;}
    void add(const Item& i){if(std::isfinite(i.at[0])&&std::isfinite(i.at[1])&&std::isfinite(i.at[2]))items_.push_back(i);}
    void build(){
        std::size_t n=16;while(n<items_.size()*2)n<<=1;
        head_.assign(n,~0u);next_.assign(items_.size(),~0u);mask_=n-1;
        for(std::size_t k=0;k<items_.size();++k){const auto& i=items_[k];const std::size_t s=slot(i.shader,i.count,cell(i.at[0]),cell(i.at[1]))&mask_;next_[k]=head_[s];head_[s]=std::uint32_t(k);}
    }
    // The nearest item of the identity within tolerance (3-D), or null.
    const Item* find(std::uintptr_t shader,unsigned count,const float* at,float tolerance)const{
        if(items_.empty())return nullptr;
        const Item* best=nullptr;float bestSquared=tolerance*tolerance;
        const std::int32_t cx=cell(at[0]),cy=cell(at[1]);
        for(std::int32_t y=cy-1;y<=cy+1;++y)for(std::int32_t x=cx-1;x<=cx+1;++x)
            for(std::uint32_t k=head_[slot(shader,count,x,y)&mask_];k!=~0u;k=next_[k]){const auto& i=items_[k];
                if(i.shader!=shader||i.count!=count)continue;float q=0;for(unsigned a=0;a<3;++a){const float t=i.at[a]-at[a];q+=t*t;}
                if(q<=bestSquared){bestSquared=q;best=&i;}}
        return best;
    }
};
enum class Verdict : unsigned char {Admit,RadiusGate,DistanceCut};
struct Decision {Verdict verdict=Verdict::Admit;float distance=0;bool rooted=false;std::uint64_t shape=0;};
struct Counters {unsigned radiusGated=0,cutRejected=0,insideRefusedDraws=0,insideRefusedActors=0,noRoot=0;std::size_t radiusGatedBytes=0;};
// The frame's inputs, at its first ask: centre (the self's position, else the pivot) and its kind.
struct Context {const float* centre=nullptr;bool self=false;const float* camera=nullptr;float yards=0;bool gi=false;};
class Admission {
    struct Record {float distance;std::size_t bytes;};
    // Frame
    bool armed_=false,radiusOn_=false,self_=false,gi_=false,exhausted_=false;
    float centre_[3]={},camera_[3]={},yards_=0,gate_=0,tolerance_=MatchYards;
    std::vector<Record> records_;std::size_t admittedBytes_=0;Counters counters_;
    std::unordered_set<std::uint64_t> insideCells_;
    // Persistent
    float cut_=INFINITY;unsigned exhaustStreak_=0,room_=0;
    float lastCentre_[3]={},lastCamera_[3]={};bool lastValid_=false;
    Grid gateGrid_,lastAdmitted_,admittedNext_;std::vector<Item> gatedLast_,gatedNext_,far_;
    std::unordered_map<std::uint64_t,std::size_t> shapes_;
    static bool finite(const float* v){return v&&std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]);}
    static float length(const float* a,const float* b){float q=0;for(unsigned k=0;k<3;++k){const float t=a[k]-b[k];q+=t*t;}return std::sqrt(q);}
    static std::uint64_t shapeKey(std::uintptr_t shader,unsigned count,unsigned vertexTotal){
        std::uint64_t h=14695981039346656037ull;auto mix=[&](std::uint64_t n){h=(h^n)*1099511628211ull;};mix(shader);mix(count);mix(vertexTotal);return h;}
    // Bytes of a refused draw: the shape's last real capture; a cold shape errs high so the error tightens the cut.
    std::size_t estimate(std::uint64_t shape,unsigned vertexTotal,unsigned count)const{
        const auto it=shapes_.find(shape);return it!=shapes_.end()?it->second:std::size_t(vertexTotal)*32+std::size_t(count)*12;}
    void refuse(const Decision& d,const float* root,std::size_t bytes){
        records_.push_back({d.distance,bytes});
        if(radiusOn_&&d.distance<=yards_){++counters_.insideRefusedDraws;
            const std::uint64_t x=std::uint32_t(std::int32_t(std::floor(root[0]/CountCell))),y=std::uint32_t(std::int32_t(std::floor(root[1]/CountCell)));
            if(insideCells_.insert(x<<32|y).second)++counters_.insideRefusedActors;}
    }
public:
    bool armed()const{return armed_;}
    float cut()const{return cut_;}
    bool cutActive()const{return std::isfinite(cut_);}
    const Counters& counters()const{return counters_;}
    bool centreSelf()const{return armed_&&self_;}
    std::size_t gateSize()const{return gateGrid_.size();}
    std::size_t shapeCount()const{return shapes_.size();}
    // Forget everything (map change, device reset, a lost selection).
    void reset(){armed_=false;cut_=INFINITY;exhaustStreak_=room_=0;exhausted_=false;lastValid_=false;records_.clear();admittedBytes_=0;counters_={};insideCells_.clear();
        gateGrid_.clear();lastAdmitted_.clear();admittedNext_.clear();gatedLast_.clear();gatedNext_.clear();far_.clear();shapes_.clear();}
    // The previous selection's bodies outside the radius, after selectShadowReplays: {shader, count, root}
    // of the bodies it classified as outside (not inside, not companion, not self, filtered, root known).
    void importFar(const std::vector<NorthlightActorShadowSelection::RadiusEntry>& entries){
        far_.clear();
        for(const auto& e:entries){if(e.inside||e.companion||e.self||!e.filtered||!e.rootValid||!e.shader||!finite(e.rootAt))continue;
            Item i;i.shader=e.shader;i.count=e.count;for(unsigned k=0;k<3;++k)i.at[k]=e.rootAt[k];far_.push_back(i);}
    }
    // The first ask of a capture frame: the centre is fixed and the gate list built.
    void arm(const Context& c){
        armed_=true;exhausted_=false;records_.clear();admittedBytes_=0;counters_={};insideCells_.clear();gatedNext_.clear();admittedNext_.clear();
        const bool ok=finite(c.centre);
        for(unsigned k=0;k<3;++k){centre_[k]=ok?c.centre[k]:0.f;camera_[k]=finite(c.camera)?c.camera[k]:centre_[k];}
        self_=c.self&&ok;gi_=c.gi&&finite(c.camera);yards_=c.yards;radiusOn_=NorthlightActorShadowSelection::Enabled&&c.yards>0;gate_=gateYards(c.yards);
        float move=0;
        if(lastValid_){const float centreMove=length(centre_,lastCentre_),cameraMove=length(camera_,lastCamera_);move=std::max(centreMove,cameraMove);
            if(move>Teleport){gatedLast_.clear();far_.clear();lastAdmitted_.clear();cut_=INFINITY;exhaustStreak_=room_=0;move=0;}}
        tolerance_=std::min(MatchYards+move,MatchCap);
        for(unsigned k=0;k<3;++k){lastCentre_[k]=centre_[k];lastCamera_[k]=camera_[k];}lastValid_=true;
        gateGrid_.clear();
        if(radiusOn_&&self_){
            for(const auto& i:gatedLast_)gateGrid_.add(i);
            gateGrid_.build();
            std::vector<Item> extra;for(const auto& i:far_)if(!gateGrid_.find(i.shader,i.count,i.at,.5f))extra.push_back(i);
            for(const auto& i:extra)gateGrid_.add(i);
            gateGrid_.build();}
    }
    // A skinned draw about to be captured. root: its palette root (null: none). nearHit: a near-reserve hit (not "near": a Windows macro).
    Decision decide(std::uintptr_t shader,unsigned count,unsigned vertexTotal,const float* root,bool nearHit){
        Decision d;
        if(!finite(root)){++counters_.noRoot;return d;}
        d.rooted=true;d.shape=shapeKey(shader,count,vertexTotal);d.distance=length(root,centre_);
        if(nearHit)return d;
        if(radiusOn_&&self_&&d.distance>gate_&&gateGrid_.find(shader,count,root,tolerance_)){
            // GI packets need skinned draws up to 100 yd from the camera: admitted unless the budget cut is on.
            if(!(gi_&&length(root,camera_)<=GIYards&&!cutActive())){
                Item i;i.shader=shader;i.count=count;for(unsigned k=0;k<3;++k)i.at[k]=root[k];gatedNext_.push_back(i);
                ++counters_.radiusGated;counters_.radiusGatedBytes+=estimate(d.shape,vertexTotal,count);d.verdict=Verdict::RadiusGate;return d;}}
        if(cutActive()&&d.distance>cut_){
            const bool held=d.distance<=cut_+NorthlightActorShadowSelection::band(cut_)&&lastAdmitted_.find(shader,count,root,tolerance_);
            if(!held){d.verdict=Verdict::DistanceCut;++counters_.cutRejected;refuse(d,root,estimate(d.shape,vertexTotal,count));}}
        return d;
    }
    // The draw was read: its real charged bytes.
    void admitted(const Decision& d,std::uintptr_t shader,unsigned count,const float* root,std::size_t bytes){
        if(!d.rooted)return;
        records_.push_back({d.distance,bytes});admittedBytes_+=bytes;
        if(shapes_.size()>=ShapeCap&&!shapes_.count(d.shape))shapes_.clear();
        shapes_[d.shape]=bytes;
        Item i;i.shader=shader;i.count=count;for(unsigned k=0;k<3;++k)i.at[k]=root[k];admittedNext_.add(i);
    }
    // The budget turned an admitted draw away (real exhaustion).
    void budgetRefused(const Decision& d,const float* root,unsigned vertexTotal,unsigned count){
        exhausted_=true;if(!d.rooted)return;
        refuse(d,root,estimate(d.shape,vertexTotal,count));
    }
    // End of a capture frame: the next cut. limit: the capture budget; totalRead: all bytes the frame read.
    void endFrame(std::size_t limit,std::size_t totalRead){
        if(!armed_)return;
        const std::size_t nonPriority=totalRead>admittedBytes_?totalRead-admittedBytes_:0;
        const double target=limit>nonPriority?double(limit-nonPriority)*Margin:0.0;
        float calc=INFINITY,farthest=0;double total=0;
        for(const auto& r:records_){total+=double(r.bytes);farthest=std::max(farthest,r.distance);}
        if(total>target){
            std::sort(records_.begin(),records_.end(),[](const Record& a,const Record& b){return a.distance<b.distance;});
            double sum=0;std::size_t k=0;for(;k<records_.size();++k){sum+=double(records_[k].bytes);if(sum>target)break;}
            calc=k==0?MinCut:.5f*(records_[k-1].distance+records_[k].distance);calc=std::max(calc,MinCut);}
        exhaustStreak_=exhausted_?exhaustStreak_+1:0;
        const float previousCut=cut_;
        const auto band=[](float d){return NorthlightActorShadowSelection::band(d);};
        if(exhaustStreak_>=ShrinkFrames){
            if(!cutActive())cut_=std::isfinite(calc)?calc:std::max(MinCut,farthest-band(farthest));
            else{const float step=cut_-band(cut_);cut_=calc>=step&&calc<cut_?calc:step;}
            cut_=std::max(cut_,MinCut);room_=0;}
        else if(cutActive()&&calc>cut_&&!exhausted_){
            if(++room_>=GrowFrames){const float step=cut_+band(cut_);
                cut_=std::isfinite(calc)?std::min(calc,step):step>=farthest?float(INFINITY):step;room_=0;}}
        else room_=0;
        /* the deadband bridges jitter at a settled cut; a cut that moved starts without one (what was admitted before it is not a reason) */
        if(cut_==previousCut||(!std::isfinite(cut_)&&!std::isfinite(previousCut))){lastAdmitted_=std::move(admittedNext_);lastAdmitted_.build();}else lastAdmitted_.clear();
        admittedNext_=Grid{};
        gatedLast_=std::move(gatedNext_);gatedNext_.clear();
        armed_=false;exhausted_=false;records_.clear();admittedBytes_=0;counters_={};insideCells_.clear();
    }
};
}
