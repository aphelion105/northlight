#include "world_mesh_pages.h"
#include "point_light_shadow.h"
#include <cassert>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <random>

namespace P=NorthlightWorldMeshPages;
using NorthlightGI::Vec3;
static uint64_t compared=0,linearTests=0,indexedTests=0,checksum=0;
static void compare(const std::vector<P::Batch>& batches,const P::PointTree& tree,NorthlightLocalLights::Light light,unsigned requested){
    float matrices[6][16];for(unsigned face=0;face<6;++face)assert(NorthlightPointShadow::faceMatrix(light.position,.1f,light.attenuationEnd,face,matrices[face],256));
    NorthlightPointShadow::FaceClip clip(matrices,light,requested);
    std::vector<unsigned char> nearby;
    assert(tree.nearby(batches.size(),[&](Vec3 lo,Vec3 hi){return NorthlightPointShadow::recordOutsideSphere(lo,hi,light);},nearby));
    for(size_t i=0;i<batches.size();++i){const auto& b=batches[i];
        const auto expected=clip.mask(&b.boundsLow.x,&b.boundsHigh.x,true,requested);
        const auto actual=nearby[i]?clip.mask(&b.boundsLow.x,&b.boundsHigh.x,true,requested):0;
        assert(actual==expected);++compared;++linearTests;indexedTests+=nearby[i]!=0;
    }
    using Group=NorthlightPointShadow::ReplayGroup;
    for(size_t first=0;first<batches.size();first+=Group::Width){
        const size_t end=std::min(first+Group::Width,batches.size());Group group;
        for(size_t i=first;i<end;++i)group.include(&batches[i].boundsLow.x,&batches[i].boundsHigh.x,i%127!=0,i==first);
        const unsigned faces=clip.mask(group.low,group.high,group.valid,requested);
        for(size_t i=first;i<end;++i){const auto& b=batches[i];const bool known=i%127!=0;
            assert(clip.mask(&b.boundsLow.x,&b.boundsHigh.x,known,faces)==clip.mask(&b.boundsLow.x,&b.boundsHigh.x,known,requested));}
    }
    assert(!tree.nearby(batches.size()+1,[](Vec3,Vec3){return false;},nearby));
}
// Sixteen lights share the same per-frame replay groups. Include rebuilding
// those groups in every measured frame and check each ordered face mask.
static void replayBenchmark(bool shuffled,bool dense){
    using namespace NorthlightPointShadow;
    struct Box {float low[3],high[3];bool valid=true;};std::vector<Box> boxes(1155);
    std::mt19937 rng(1017);
    for(size_t i=0;i<boxes.size();++i){const float a=float(i/16)*.18f;
        const float x=80.f*std::cos(a)+float(i%4)*.6f,y=80.f*std::sin(a)+float(i%3);
        boxes[i]={{x,y,0},{x+1,y+1,3}};
    }
    if(shuffled)std::shuffle(boxes.begin(),boxes.end(),rng);
    std::vector<FaceClip> clips;
    for(unsigned i=0;i<16;++i){NorthlightLocalLights::Light l{};l.position[0]=80.f*std::cos(float(i)*.4f);
        l.position[1]=80.f*std::sin(float(i)*.4f);l.position[2]=2;l.attenuationEnd=dense?240.f:16.f;
        float matrix[6][16];for(unsigned f=0;f<6;++f)assert(faceMatrix(l.position,.1f,l.attenuationEnd,f,matrix[f],256));clips.emplace_back(matrix,l);
    }
    std::vector<ReplayGroup> groups((boxes.size()+ReplayGroup::Width-1)/ReplayGroup::Width);
    std::vector<unsigned char> masks[2];for(auto& m:masks)m.resize(boxes.size()*clips.size());
    std::vector<double> times[2];
    for(unsigned block=0;block<14;++block)for(unsigned turn=0;turn<2;++turn){
        const auto grouped=(block+turn)%2;auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<4;++frame){
            const bool indexed=grouped&&ReplayGroup::useful(boxes.size(),[&](size_t n)->const auto& {return boxes[n];});
            if(indexed){std::fill(groups.begin(),groups.end(),ReplayGroup{});
                for(size_t n=0;n<boxes.size();++n)groups[n/ReplayGroup::Width].include(boxes[n].low,boxes[n].high,true,n%ReplayGroup::Width==0);}
            size_t dst=0;
            for(const auto& clip:clips)for(size_t first=0;first<boxes.size();first+=ReplayGroup::Width){
                const auto& group=groups[first/ReplayGroup::Width];
                const unsigned face=indexed?clip.mask(group.low,group.high,group.valid):63;
                for(size_t n=first;n<std::min(first+ReplayGroup::Width,boxes.size());++n)
                    masks[grouped][dst++]=face?static_cast<unsigned char>(clip.mask(boxes[n].low,boxes[n].high,true,face)):0;
            }
            checksum+=masks[grouped][frame*31];
        }
        if(block>=2)times[grouped].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/4);
        if(turn)assert(masks[0]==masks[1]);
    }
    for(auto& t:times)std::sort(t.begin(),t.end());
    std::printf("POINT_REPLAY_GROUPS shuffled=%u dense=%u packets=%zu lights=%zu directMs=%.6f groupedMs=%.6f\n",
        unsigned(shuffled),unsigned(dense),boxes.size(),clips.size(),(times[0][5]+times[0][6])/2,(times[1][5]+times[1][6])/2);
}
int main(){
    replayBenchmark(false,false);replayBenchmark(false,true);replayBenchmark(true,false);
    std::mt19937 random(12340);std::uniform_real_distribution<float> coordinate(-512,512),extent(0,40);
    std::vector<P::Batch> batches(7657);
    for(auto& b:batches){b.boundsLow={coordinate(random),coordinate(random),coordinate(random)};b.boundsHigh={b.boundsLow.x+extent(random),b.boundsLow.y+extent(random),b.boundsLow.z+extent(random)};}
    for(unsigned i=0;i<12;++i){const float v=i<4?std::numeric_limits<float>::quiet_NaN():i<8?INFINITY:-INFINITY;
        (&batches[i].boundsLow.x)[i%3]=v;}
    batches[12].boundsHigh.x=batches[12].boundsLow.x-1;
    batches[13].boundsLow=Vec3(-FLT_MAX,-FLT_MAX,-FLT_MAX);batches[13].boundsHigh=Vec3(FLT_MAX,FLT_MAX,FLT_MAX);
    batches[14].boundsLow=Vec3(0,0,0);batches[14].boundsHigh=Vec3(12,12,12);
    assert(!P::PointTree::build(batches,[](uint64_t){return false;}));
    size_t admittedBytes=0;
    const auto started=std::chrono::steady_clock::now();const auto tree=P::PointTree::build(batches,[&](uint64_t bytes){admittedBytes=bytes;return bytes<=128*1024;});assert(tree);
    assert(tree->bytes()<=admittedBytes);
    const auto buildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    for(unsigned n=0;n<240;++n){NorthlightLocalLights::Light l{};for(auto& p:l.position)p=coordinate(random);l.attenuationEnd=n%4?12:128;
        compare(batches,*tree,l,n%63+1);}
    for(float x:{-12.f,std::nextafter(-12.f,-INFINITY),std::nextafter(-12.f,INFINITY),0.f,12.f,24.f}){
        NorthlightLocalLights::Light l{};l.position[0]=x;l.attenuationEnd=12;for(unsigned mask=1;mask<=63;++mask)compare(batches,*tree,l,mask);}
    std::vector<P::Batch> unknown(16,batches[0]);const auto unknownTree=P::PointTree::build(unknown);assert(unknownTree);
    std::vector<unsigned char> hits;assert(unknownTree->nearby(unknown.size(),[](Vec3,Vec3){return true;},hits));
    for(auto h:hits)assert(h==1);
    // Invalid queries must keep every leaf; the production sphere predicate
    // has the same fail-open contract as the final FaceClip test.
    for(float radius:{-1.f,INFINITY,std::numeric_limits<float>::quiet_NaN()}){
        NorthlightLocalLights::Light l{};l.attenuationEnd=radius;
        assert(tree->nearby(batches.size(),[&](Vec3 lo,Vec3 hi){return NorthlightPointShadow::recordOutsideSphere(lo,hi,l);},hits));for(auto h:hits)assert(h==1);}
    for(size_t n:{size_t(0),size_t(15),size_t(65537)})assert(!P::PointTree::build(std::vector<P::Batch>(n)));
    // Alternate the same moving-lamp workload; include tree traversal, mask
    // clearing and the unchanged ordered scan in the measured indexed path.
    std::vector<double> samples[2];
    for(unsigned block=0;block<32;++block){const unsigned variant=(block/2)%2?1-block%2:block%2;
        const auto begin=std::chrono::steady_clock::now();
        for(unsigned n=0;n<64;++n){NorthlightLocalLights::Light l{};l.position[0]=float(n)-32;l.position[1]=float(n%7);l.attenuationEnd=12;
            float matrices[6][16];for(unsigned f=0;f<6;++f)assert(NorthlightPointShadow::faceMatrix(l.position,.1f,12,f,matrices[f],256));
            NorthlightPointShadow::FaceClip clip(matrices,l);
            if(variant)assert(tree->nearby(batches.size(),[&](Vec3 lo,Vec3 hi){return NorthlightPointShadow::recordOutsideSphere(lo,hi,l);},hits));
            for(size_t i=0;i<batches.size();++i)if(!variant||hits[i])checksum+=clip.mask(&batches[i].boundsLow.x,&batches[i].boundsHigh.x,true);
        }
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/64;
        if(block>=8)samples[variant].push_back(ms);
    }
    double ms[2];for(unsigned v=0;v<2;++v){std::sort(samples[v].begin(),samples[v].end());ms[v]=(samples[v][5]+samples[v][6])*.5;}
    std::printf("POINT_BATCH_TREE_PASS compared=%llu linearTests=%llu indexedTests=%llu bytes=%zu buildMs=%.6f linearMs=%.6f indexedMs=%.6f checksum=%llu\n",
        (unsigned long long)compared,(unsigned long long)linearTests,(unsigned long long)indexedTests,tree->bytes(),buildMs,ms[0],ms[1],(unsigned long long)checksum);
}
