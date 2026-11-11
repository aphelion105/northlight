#pragma once
#include "aurora_native_reader.h"
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace NorthlightAuroraNative {
// One entry per independently proven emitter family. An unsupported/ambiguous
// family never invalidates a different known emitter in the same sky blend.
class Registry {
    struct Group {std::uint64_t drawGroup=0;std::vector<Packet> packets;};
    struct Job {std::uint64_t selection=0,revision=0,epoch=0,drawGroup=0;unsigned family=0;std::vector<Packet> packets;};
    struct Field {NorthlightAuroraSource::Snapshot native,twilight;};
    struct Result {unsigned family=0;std::uint64_t epoch=0,drawGroup=0;Field source;};
    std::vector<Group> frameGroups_;
    struct Source {std::uint64_t drawGroup=0;Field value;};
    std::map<unsigned,Source> sources_;
    std::array<std::uint64_t,sizeof(AssetFamilies)/sizeof(AssetFamilies[0])> rejectedThrough_{};
    std::shared_ptr<const NorthlightAuroraSource::Snapshot> published_;
    std::uint64_t selection_=0,revision_=0,publishedStyle_=0,epoch_=0;
    unsigned frame_=~0u,triangles_=0;bool collecting_=false;
    std::mutex mutex_;std::condition_variable condition_;bool stopped_=false;
    std::vector<Job> jobs_;std::vector<Result> results_;std::thread worker_;
    void run(){
        for(;;){std::vector<Job> jobs;
            {std::unique_lock<std::mutex> lock(mutex_);condition_.wait(lock,[&]{return stopped_||!jobs_.empty();});if(stopped_)return;jobs.swap(jobs_);}
            std::vector<Result> done;
            for(auto& job:jobs)try{
                std::vector<NorthlightAuroraSource::NativeTriangle> triangles;
                std::vector<Material> materials;
                bool complete=true;
                for(auto& packet:job.packets){if(!Reader::expand(packet)){complete=false;break;}const unsigned material=unsigned(materials.size());materials.push_back(std::move(packet.material));
                    for(auto& triangle:packet.triangles){triangle.material=material;triangles.push_back(std::move(triangle));}}
                std::array<std::array<float,6>,NorthlightAuroraSource::SectorCount> field{};
                const bool ok=complete&&NorthlightAuroraSource::projectChannels<6>(triangles,job.selection,job.revision,
                    [&](unsigned material,const NorthlightAuroraSource::NativeVertex& vertex,std::array<float,6>& out){return material<materials.size()&&materials[material].emission(vertex,out);},field);
                if(ok){Field source;for(auto* snapshot:{&source.native,&source.twilight}){snapshot->selection=job.selection;snapshot->revision=job.revision;snapshot->complete=true;}
                    for(unsigned sector=0;sector<NorthlightAuroraSource::SectorCount;++sector)for(unsigned c=0;c<3;++c){source.native.radiance[sector][c]=field[sector][c];source.twilight.radiance[sector][c]=field[sector][c+3];}
                    done.push_back({job.family,job.epoch,job.drawGroup,source});}
            }catch(const std::bad_alloc&){}
            try{std::lock_guard<std::mutex> lock(mutex_);if(stopped_)return;results_.insert(results_.end(),done.begin(),done.end());if(results_.size()>32)results_.erase(results_.begin(),results_.end()-32);}
            catch(const std::bad_alloc&){} // Retain the last valid field under memory pressure.
        }
    }
    static int family(const Group& group){
        std::vector<unsigned> observed;for(const auto& packet:group.packets)observed.push_back(packet.assetBatch);std::sort(observed.begin(),observed.end());
        for(unsigned i=0;i<sizeof(AssetFamilies)/sizeof(AssetFamilies[0]);++i){const auto& candidate=AssetFamilies[i];
            if(candidate.count==observed.size()&&std::equal(observed.begin(),observed.end(),AssetFamilyBatches+candidate.offset))return int(i);}
        return -1;
    }
public:
    Registry()=default;
    ~Registry(){
        {std::lock_guard<std::mutex> lock(mutex_);stopped_=true;jobs_.clear();}condition_.notify_one();if(worker_.joinable())worker_.join();
    }
    Registry(const Registry&)=delete;Registry& operator=(const Registry&)=delete;
    void reset(){++epoch_;selection_=0;triangles_=0;frame_=~0u;collecting_=false;frameGroups_.clear();sources_.clear();rejectedThrough_.fill(0);published_.reset();publishedStyle_=0;
        std::lock_guard<std::mutex> lock(mutex_);jobs_.clear();results_.clear();}
    void begin(unsigned frame,std::uint64_t selection,bool collect){
        if(selection_!=selection){reset();selection_=selection;}
        frame_=frame;triangles_=0;collecting_=collect&&selection;frameGroups_.clear();
        std::vector<Result> done;{std::lock_guard<std::mutex> lock(mutex_);done.swap(results_);}
        for(auto& result:done)if(result.epoch==epoch_&&result.source.native.selection==selection_&&result.source.native.revision>rejectedThrough_[result.family]){auto old=sources_.find(result.family);
            if(old==sources_.end()||old->second.value.native.revision<result.source.native.revision){sources_[result.family]={result.drawGroup,result.source};published_.reset();}}
    }
    bool collecting()const{return collecting_;}
    void observe(std::uint64_t drawGroup,Packet packet){
        if(!collecting_||packet.assetBatch==~0u)return;
        if(packet.assetBatch>=sizeof(AssetBatches)/sizeof(AssetBatches[0]))return;
        triangles_+=AssetBatches[packet.assetBatch].primitives;if(triangles_>16384){collecting_=false;frameGroups_.clear();return;}
        for(auto& group:frameGroups_)if(group.drawGroup==drawGroup){if(group.packets.size()<128)group.packets.push_back(std::move(packet));return;}
        if(frameGroups_.size()<8)frameGroups_.push_back({drawGroup,{}}),frameGroups_.back().packets.push_back(std::move(packet));
    }
    void end(){
        if(!collecting_)return;collecting_=false;
        std::map<unsigned,unsigned> counts;for(const auto& group:frameGroups_){const int f=family(group);if(f>=0)++counts[unsigned(f)];}
        std::vector<Job> next;
        for(auto& group:frameGroups_){const int f=family(group);if(f<0)continue;
            // Two indistinguishable instances of one family need a native
            // instance key. Fail open for that family until it is available.
            if(counts[unsigned(f)]!=1){rejectedThrough_[unsigned(f)]=++revision_;sources_.erase(unsigned(f));published_.reset();continue;}
            next.push_back({selection_,++revision_,epoch_,group.drawGroup,unsigned(f),std::move(group.packets)});
        }
        frameGroups_.clear();if(next.empty())return;
        if(!worker_.joinable())worker_=std::thread([this]{run();});
        {std::lock_guard<std::mutex> lock(mutex_);jobs_=std::move(next);}condition_.notify_one();
    }
    bool owns(unsigned assetBatch,std::uint64_t drawGroup)const{
        for(const auto& entry:sources_){if(entry.second.drawGroup!=drawGroup)continue;const auto& family=AssetFamilies[entry.first];
            if(std::find(AssetFamilyBatches+family.offset,AssetFamilyBatches+family.offset+family.count,assetBatch)!=AssetFamilyBatches+family.offset+family.count)return true;}
        return false;
    }
    std::shared_ptr<const NorthlightAuroraSource::Snapshot> source(const NorthlightAurora::Style& style){
        if(!selection_||sources_.empty()||!style.enabled||!style.revision)return {};
        if(published_&&publishedStyle_==style.revision)return published_;
        auto next=std::make_shared<NorthlightAuroraSource::Snapshot>();next->selection=selection_;next->revision=++revision_;next->complete=true;
        const auto scale=NorthlightAuroraSource::emissionScale(style);
        for(const auto& entry:sources_)for(unsigned sector=0;sector<NorthlightAuroraSource::SectorCount;++sector)
            for(unsigned c=0;c<3;++c)next->radiance[sector][c]+=entry.second.value.native.radiance[sector][c]*style.intensity+entry.second.value.twilight.radiance[sector][c]*scale[c];
        if(!NorthlightAuroraSource::valid(*next))return {};publishedStyle_=style.revision;published_=std::move(next);return published_;
    }
};
}
