#pragma once
// Read-only 12340 scene inventory. No native calls and no draw/visibility stamp
// as an existence test. Unsupported moving/attached emitters stay explicit.
#include "native_light_snapshot.h"
#include "local_light_selection.h"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <vector>

namespace NorthlightNativeLights {
namespace SceneDetail {
using Bytes=std::vector<unsigned char>;
using Value=std::array<float,4>;
inline constexpr std::uint32_t ScenePointer=0xcd754c;
inline constexpr unsigned ModelBytes=0x1d4,BoneBytes=0xac,LightBytes=0xd4;
inline constexpr unsigned DiscoverySteps=128,ModelLimit=512,AssetLimit=128,AssetBytes=256*1024,CacheBytes=2*1024*1024;
inline constexpr unsigned MaxBones=512,MaxLights=64,MaxSequences=512,MaxKeys=16384;
inline bool pointer(std::uint32_t p){return p>=0x10000&&p<0xffff0000&&!(p&3);}
template<class T> T at(const unsigned char* p,unsigned offset){T v;std::memcpy(&v,p+offset,sizeof v);return v;}
template<class T,std::size_t N> T at(const std::array<unsigned char,N>& p,unsigned offset){return at<T>(p.data(),offset);}
inline float distance(const float* a,const float* b){float d=0;for(unsigned k=0;k<3;++k){const float x=a[k]-b[k];d+=x*x;}return d;}
struct Keys {std::vector<std::uint32_t> times;std::vector<Value> values;};
struct Clock {unsigned animation=0;std::uint32_t time=0;};
struct Track {
    unsigned interpolation=0;int loop=-1;std::vector<Keys> sequences;
    bool constant(Value& value)const{
        const Value fallback=value;bool found=false;
        const auto equal=[&](const Value& v){if(found&&v!=value)return false;value=v;found=true;return true;};
        for(const auto& s:sequences){if(s.values.empty()){if(!equal(fallback))return false;}else for(const auto& v:s.values)if(!equal(v))return false;}return true;
    }
    Value sample(Clock clock,std::uint32_t now,std::uint32_t origin,const std::vector<std::uint32_t>& loops,Value fallback)const{
        if(sequences.empty())return fallback;
        if(loop>=0){const auto duration=loops[unsigned(loop)];clock={0,duration?(now-origin)%duration:0};}
        const auto& s=sequences[clock.animation<sequences.size()?clock.animation:0];if(s.times.empty())return fallback;
        const auto upper=std::upper_bound(s.times.begin(),s.times.end(),clock.time);
        const unsigned first=upper==s.times.begin()?0:unsigned(upper-s.times.begin()-1);
        if(!interpolation||first+1==s.times.size()||clock.time<=s.times[first])return s.values[first];
        const float t=float(clock.time-s.times[first])/float(s.times[first+1]-s.times[first]);Value value=s.values[first];
        for(unsigned k=0;k<4;++k)value[k]+=(s.values[first+1][k]-value[k])*t;return value;
    }
};
struct Authored {unsigned index=0,bone=0;float point[3]{};Track color,intensity,enabled;std::vector<unsigned> chain;};
struct Asset {
    std::uint32_t shared=0,data=0,checked=0,used=0;std::size_t bytes=0;
    std::array<unsigned char,304> header{};
    std::vector<std::array<unsigned char,88>> bones;
    std::vector<std::array<unsigned char,64>> sequences;
    std::vector<std::uint32_t> loops;std::vector<Authored> lights;
};
template<class Read> bool track(Read& read,const unsigned char* record,unsigned fields,Track& out,Asset& asset,bool compressed=false){
    const unsigned interpolation=at<std::uint16_t>(record,0);const int loop=at<std::int16_t>(record,2);
    const unsigned count=at<std::uint32_t>(record,4),keys=at<std::uint32_t>(record,12);
    if(interpolation>1||count!=keys||count>MaxSequences||loop< -1||(loop>=0&&unsigned(loop)>=asset.loops.size()))return false;
    out.interpolation=fields==1?0:interpolation;out.loop=loop;
    // Native uint8 visibility never interpolates or blends.
    out.sequences.resize(count);asset.bytes+=count*sizeof(Keys);
    std::vector<std::array<std::uint32_t,2>> times(count),values(count);
    const auto span=[&](std::uint32_t address,void* dst,std::size_t bytes){if(!bytes)return true;
        if(!pointer(address)||bytes>AssetBytes-std::min(asset.bytes,std::size_t(AssetBytes)))return false;
        asset.bytes+=bytes;return read(address,dst,bytes);};
    if(!span(at<std::uint32_t>(record,8),times.data(),count*8)||!span(at<std::uint32_t>(record,16),values.data(),count*8))return false;
    for(unsigned i=0;i<count;++i){const unsigned n=times[i][0];if(n!=values[i][0]||n>MaxKeys)return false;
        auto& s=out.sequences[i];s.times.resize(n);s.values.resize(n);const unsigned width=compressed?8:fields==1?1:fields==0?4:fields*4;
        if(asset.bytes>AssetBytes||n*(sizeof(Value)+4)>AssetBytes-asset.bytes)return false;
        asset.bytes+=n*(sizeof(Value)-width);Bytes raw(std::size_t(n)*width);
        if(!span(times[i][1],s.times.data(),n*4)||!span(values[i][1],raw.data(),raw.size())||!std::is_sorted(s.times.begin(),s.times.end()))return false;
        for(unsigned j=0;j<n;++j)for(unsigned k=0;k<(compressed?4:fields<=1?1:fields);++k){float v;
            if(compressed){const int q=at<std::int16_t>(raw.data()+j*width,k*2);v=float(q>0?q-32767:q+32767)/32767.f;}
            else v=fields==1?float(raw[j]):at<float>(raw.data()+j*width,k*4);
            if(!std::isfinite(v)||std::fabs(v)>100000||(fields==1&&v>1)||(fields==0&&v<0))return false;s.values[j][k]=v;
        }
    }return true;
}
template<class Read> bool fixedPoint(Read& read,Asset& a,Authored& light){
    int bone=int(light.bone);std::array<bool,MaxBones> visited{};
    while(bone>=0){if(unsigned(bone)>=a.bones.size()||visited[unsigned(bone)])return false;visited[unsigned(bone)]=true;light.chain.push_back(unsigned(bone));a.bytes+=sizeof(unsigned);
        const auto& b=a.bones[unsigned(bone)];const auto flags=at<std::uint32_t>(b,4);if(flags&0x7f)return false;
        if(flags&0x280){Track translation,rotation,scale;
            if(!track(read,b.data()+16,3,translation,a)||!track(read,b.data()+36,4,rotation,a,true)||!track(read,b.data()+56,3,scale,a))return false;
            Value t{},r{0,0,0,1},s{1,1,1,0};if(!translation.constant(t))return false;
            bool pivot=true;for(unsigned k=0;k<3;++k)pivot=pivot&&light.point[k]==at<float>(b,76+4*k);
            if(!pivot){if(!rotation.constant(r)||!scale.constant(s)||std::fabs(r[0])>1e-4f||std::fabs(r[1])>1e-4f||std::fabs(r[2])>1e-4f||std::fabs(std::fabs(r[3])-1)>1e-4f||s!=Value{1,1,1,0})return false;}
            for(unsigned k=0;k<3;++k)light.point[k]+=t[k];
        }bone=at<std::int16_t>(b,8);
    }return true;
}
inline Clock clock(const unsigned char* runtime,unsigned offset,const Asset& asset,std::uint32_t now,bool secondary,bool& ok){
    Clock c;const unsigned index=at<std::uint16_t>(runtime,offset+8);if(index==0xffff)return {at<std::uint16_t>(runtime,offset+4),at<std::uint32_t>(runtime,offset)};if(index>=asset.sequences.size()){ok=false;return c;}
    const auto& sequence=asset.sequences[index];const auto duration=at<std::uint32_t>(sequence,4),flags=at<std::uint32_t>(sequence,12);
    const auto start=at<std::uint32_t>(runtime,offset+12),end=at<std::uint32_t>(runtime,offset+16),origin=at<std::uint32_t>(runtime,offset+28);
    const float speed=at<float>(runtime,offset+20);if(!std::isfinite(speed)||std::fabs(speed)>1000){ok=false;return c;}
    const bool clamped=flags&(secondary?0x80u:1u);const bool ended=clamped&&std::int32_t(end-now)<=0;
    if(clamped&&std::int32_t(start-now)>0)now=start;
    const double elapsed=double(std::int32_t((ended?end:now)-start))*speed;
    if(elapsed<std::numeric_limits<std::int32_t>::min()||elapsed>std::numeric_limits<std::int32_t>::max()){ok=false;return c;}
    const auto value=std::uint32_t(std::int32_t(elapsed))+origin;c.animation=index;
    c.time=ended?(std::int32_t(value)<0?0:std::min(value,duration)):(duration?value%duration:0);return c;
}
} // namespace SceneDetail

class Reader {
    using Asset=SceneDetail::Asset;
    struct Model {std::uint32_t shared=0,data=0,origin=0,visited=0;std::uint64_t firstId=0;float distance=0;};
    std::map<std::uint32_t,Model> models_;std::vector<Asset> assets_;
    std::uint32_t scene_=0,cursor_=0,frame_=0,lastHead_=0,lastScan_=0;std::uint64_t serial_=0;bool checked_=false,proven_=false;
    unsigned loads_=0;std::size_t readBytes_=0;unsigned readCalls_=0;
    std::array<std::array<unsigned char,SceneDetail::BoneBytes>,SceneDetail::MaxBones> boneScratch_;
    std::array<unsigned char,8192> runtimeScratch_;
    std::array<std::uint32_t,SceneDetail::MaxBones> boneKnown_{};std::uint32_t boneGeneration_=0;
    template<class Read> bool sharedData(Read& read,std::uint32_t shared,std::uint32_t& data){
        for(const auto& a:assets_)if(a.shared==shared&&a.checked==frame_){data=a.data;return true;}
        return read(shared+0x150,&data,4);
    }
    template<class Read> Asset* asset(Read& input,std::uint32_t shared,std::uint32_t data){
        using namespace SceneDetail;
        unsigned calls=0;std::size_t bytesRead=0;bool metadataLimit=false;
        const auto read=[&](std::uint32_t address,void* dst,std::size_t bytes){
            if(calls>=512||bytes>AssetBytes-bytesRead){metadataLimit=true;return false;}
            ++calls;bytesRead+=bytes;return input(address,dst,bytes);
        };
        auto found=std::find_if(assets_.begin(),assets_.end(),[&](const Asset& a){return a.shared==shared&&a.data==data;});
        if(found!=assets_.end()&&found->checked==frame_){found->used=frame_;return &*found;}
        std::array<unsigned char,304> header{};if(!read(data,header.data(),header.size()))return nullptr;
        if(found!=assets_.end()&&found->header==header){found->checked=found->used=frame_;return &*found;}
        if(found!=assets_.end())assets_.erase(found);
        if(loads_++>=4){++budgetLimited;return nullptr;}
        if(std::memcmp(header.data(),"MD20",4)||at<std::uint32_t>(header,4)!=264){++malformed;return nullptr;}
        Asset a;a.header=header;a.shared=shared;a.data=data;a.checked=a.used=frame_;a.bytes=sizeof a;
        const auto readFailures=failures,limited=budgetLimited;
        const unsigned lights=at<std::uint32_t>(header,264),bones=at<std::uint32_t>(header,44),sequences=at<std::uint32_t>(header,28),loops=at<std::uint32_t>(header,20);
        if(lights>MaxLights||bones>MaxBones||sequences>MaxSequences||loops>MaxSequences){++unsupported;return nullptr;}
        if(lights){a.lights.reserve(lights);a.bytes+=lights*sizeof(Authored);a.bones.resize(bones);a.sequences.resize(sequences);a.loops.resize(loops);
            if((bones&&!read(at<std::uint32_t>(header,48),a.bones.data(),bones*88))||(sequences&&!read(at<std::uint32_t>(header,32),a.sequences.data(),sequences*64))||(loops&&!read(at<std::uint32_t>(header,24),a.loops.data(),loops*4)))return nullptr;
            a.bytes+=bones*88+sequences*64+loops*4;std::vector<std::array<unsigned char,156>> records(lights);
            if(!read(at<std::uint32_t>(header,268),records.data(),lights*156))return nullptr;
            for(unsigned i=0;i<lights;++i){const auto& r=records[i];Authored l;l.index=i;l.bone=at<std::uint16_t>(r,2);
                for(unsigned k=0;k<3;++k)l.point[k]=at<float>(r,4+4*k);
                bool finite=true;for(float p:l.point)finite=finite&&std::isfinite(p)&&std::fabs(p)<100000;
                if(!finite||at<std::uint16_t>(r,0)!=1||!fixedPoint(read,a,l)||!track(read,r.data()+56,3,l.color,a)||!track(read,r.data()+76,0,l.intensity,a)||!track(read,r.data()+136,1,l.enabled,a)){++unsupported;continue;}
                a.lights.push_back(std::move(l));
            }
        }
        if(metadataLimit){++metadataLimited;a.lights.clear();} // Unsupported metadata size must not block later models.
        if(failures!=readFailures||budgetLimited!=limited)return nullptr; // A temporary read/budget failure is retried, never cached as unsupported.
        std::size_t bytes=a.bytes;for(const auto& cached:assets_)bytes+=cached.bytes;
        while(!assets_.empty()&&(assets_.size()>=AssetLimit||bytes>CacheBytes)){auto oldest=std::min_element(assets_.begin(),assets_.end(),[](const auto& x,const auto& y){return x.used<y.used;});bytes-=oldest->bytes;assets_.erase(oldest);}
        if(a.bytes>CacheBytes){++budgetLimited;return nullptr;}assets_.push_back(std::move(a));return &assets_.back();
    }
public:
    unsigned scanned=0,unsupported=0,malformed=0,failures=0,budgetLimited=0,metadataLimited=0;
    unsigned modelCount()const{return unsigned(models_.size());}
    unsigned assetCount()const{return unsigned(assets_.size());}
    unsigned readCalls()const{return readCalls_;}
    std::size_t readBytes()const{return readBytes_;}
    void reset(){models_.clear();assets_.clear();scene_=cursor_=lastHead_=lastScan_=0;}
    template<class Read> static bool verify(Read read){
        // Local 12340 PE: world scene, intrusive list, loaded data and light layout.
        static const unsigned char world[]={0x8b,0x35,0x4c,0x75,0xcd,0x00};
        static const unsigned char attach[]={0x8d,0x42,0x08,0x89,0x46,0x08,0x89,0x56,0x28,0x8b,0x38,0x8d,0x4e,0x0c};
        static const unsigned char data[]={0x8b,0x46,0x2c,0x8b,0x80,0x50,0x01,0x00,0x00};
        static const unsigned char light[]={0x8b,0x8e,0xd0,0x01,0x00,0x00,0x52,0x8d,0x4c,0x19,0x68};
        struct Proof {std::uint32_t address;const unsigned char* bytes;unsigned size;};
        const Proof proofs[]={{0x79abdf,world,sizeof world},{0x834550,attach,sizeof attach},{0x834575,data,sizeof data},{0x834593,light,sizeof light}};
        for(const auto& p:proofs){unsigned char bytes[32];if(!read(p.address,bytes,p.size)||std::memcmp(bytes,p.bytes,p.size))return false;}return true;
    }
    template<class Read> void update(Read input,const float* camera,Snapshot& output){
        using namespace SceneDetail;output.clear();loads_=0;readBytes_=0;readCalls_=0;++frame_;
        if(!camera)return;for(unsigned k=0;k<3;++k)if(!std::isfinite(camera[k])||std::fabs(camera[k])>=100000)return;
        if(!checked_){checked_=true;proven_=verify(input);}if(!proven_)return;
        const auto read=[&](std::uint32_t address,void* dst,std::size_t bytes){
            if(!pointer(address)||bytes>std::uint64_t(0xffff0000u)-address){++failures;++malformed;return false;}
            if(bytes>1024*1024-28-readBytes_||readCalls_>=4094){++budgetLimited;return false;}
            ++readCalls_;readBytes_+=bytes;if(input(address,dst,bytes))return true;++failures;return false;};
        std::uint32_t scene=0;if(!read(ScenePointer,&scene,4)||!pointer(scene)){reset();return;}
        std::array<std::uint32_t,6> header{};if(!read(scene,header.data(),sizeof header))return;
        if(scene_!=scene){reset();scene_=scene;}
        std::array<float,Capacity> distances{};
        const auto evaluate=[&](){for(auto it=models_.begin();it!=models_.end();){if(it->second.visited==frame_){++it;continue;}it->second.visited=frame_;const auto address=it->first;const auto model=it->second;std::array<unsigned char,ModelBytes> m{};std::uint32_t data=0;
            const auto limited=budgetLimited;
            if(!read(address,m.data(),m.size())||at<std::uint32_t>(m,40)!=scene||at<std::uint32_t>(m,44)!=model.shared||at<std::uint32_t>(m,0x74)!=model.origin||!(at<std::uint32_t>(m,16)&1)||!sharedData(read,model.shared,data)||data!=model.data){if(budgetLimited!=limited){++it;continue;}it=models_.erase(it);lastScan_=0;continue;}
            if(at<std::uint32_t>(m,72)){++unsupported;++it;continue;}
            Asset* a=asset(read,model.shared,data);if(!a){++it;continue;}
            float world[16];for(unsigned k=0;k<16;++k)world[k]=at<float>(m,0xb4+4*k);
            bool valid=true;for(float v:world)valid=valid&&std::isfinite(v)&&std::fabs(v)<100000;
            valid=valid&&std::fabs(world[3])<.001f&&std::fabs(world[7])<.001f&&std::fabs(world[11])<.001f&&std::fabs(world[15]-1)<.001f;
            const float alpha=at<float>(m,0x178);valid=valid&&std::isfinite(alpha)&&alpha>=0&&alpha<=1.001f;
            const auto bones=at<std::uint32_t>(m,0x94),lights=at<std::uint32_t>(m,0x1d0);if(!valid||!pointer(bones)||!pointer(lights)){++malformed;++it;continue;}
            it->second.distance=std::numeric_limits<float>::infinity();
            for(const auto& l:a->lights){float position[3];for(unsigned k=0;k<3;++k){position[k]=world[12+k];for(unsigned j=0;j<3;++j)position[k]+=l.point[j]*world[j*4+k];}it->second.distance=std::min(it->second.distance,distance(position,camera));}
            const float reach=NorthlightLocalLightSelection::VisibilityEnd+12.f;
            if(it->second.distance>reach*reach){it=models_.erase(it);continue;}
            // InitializeLoaded allocates bones, matrices and lights in one block.
            // Small models need one RPM for their live clock/flags/enable values.
            const std::uint64_t runtimeEnd=std::uint64_t(lights)+at<std::uint32_t>(a->header,264)*LightBytes;
            const bool span=lights>=bones&&runtimeEnd>=bones&&runtimeEnd-bones<=runtimeScratch_.size()&&read(bones,runtimeScratch_.data(),std::size_t(runtimeEnd-bones));
            if(!++boneGeneration_){boneKnown_.fill(0);++boneGeneration_;}
            const auto bone=[&](unsigned index)->const unsigned char*{
                if(index>=a->bones.size())return nullptr;
                if(span&&std::size_t(index+1)*BoneBytes<=runtimeEnd-bones)return runtimeScratch_.data()+index*BoneBytes;
                if(boneKnown_[index]!=boneGeneration_){if(!read(bones+index*BoneBytes+0x40,boneScratch_[index].data()+0x40,BoneBytes-0x40))return nullptr;boneKnown_[index]=boneGeneration_;}
                return boneScratch_[index].data();};
            const auto inherited=[&](unsigned index,unsigned offset)->const unsigned char*{
                for(unsigned depth=0;depth<MaxBones;++depth){if(index>=a->bones.size())return nullptr;
                    const auto* b=bone(index);if(!b)return nullptr;if(at<std::uint16_t>(b,offset+8)!=0xffff)return b;
                    const int parent=at<std::int16_t>(a->bones[index],8);if(parent>=0)index=unsigned(parent);else if(index)index=0;else return b;
                }return nullptr;};
            for(const auto& l:a->lights){Point p;p.id=0xfea0000000000000ull|(model.firstId+l.index);
                for(unsigned k=0;k<3;++k){p.position[k]=world[12+k];for(unsigned j=0;j<3;++j)p.position[k]+=l.point[j]*world[j*4+k];}
                bool positionValid=true;for(float v:p.position)positionValid=positionValid&&std::isfinite(v)&&std::fabs(v)<100000;if(!positionValid){++malformed;continue;}
                const float d=distance(p.position,camera);const float reach=NorthlightLocalLightSelection::VisibilityEnd+12.f;
                if(!std::isfinite(d)||d>reach*reach)continue;
                bool supported=true;for(unsigned b:l.chain){const auto* r=bone(b);if(!r||at<std::uint32_t>(r,0x88)||(at<std::uint32_t>(r,0x8c)&~0x280u)||((at<std::uint32_t>(r,0x8c)&0x280u)&&!(at<std::uint32_t>(a->bones[b],4)&0x280u))){supported=false;break;}}
                if(!supported){++unsupported;continue;}
                const auto* primary=inherited(l.bone,0x40);if(!primary){++unsupported;continue;}
                const auto paused=at<std::uint32_t>(m,0x64),now=paused?paused:header[3];bool ok=true;
                const Clock first=clock(primary,0x40,*a,now,false,ok);const auto* secondary=inherited(l.bone,0x64);
                Clock second=first;float blend=0;
                if(secondary&&at<std::uint16_t>(secondary,0x6c)!=0xffff){second=clock(secondary,0x64,*a,now,true,ok);const auto end=at<std::uint32_t>(secondary,0x9c);const float inverse=at<float>(secondary,0xa0),weight=at<float>(secondary,0xa4);
                    if(!std::isfinite(inverse)||!std::isfinite(weight)||inverse<0||weight<0||weight>1){ok=false;}
                    else if(std::int32_t(end-header[3])>0&&(first.time!=second.time||first.animation!=second.animation)){const float t=std::clamp(float(end-header[3])*inverse,0.f,1.f);blend=t*t*(3-2*t)*weight;}}
                if(!ok){++malformed;continue;}
                const auto value=[&](const Track& track,Value fallback){Value v=track.sample(first,header[3],model.origin,a->loops,fallback);
                    if(blend&&track.interpolation&&track.loop<0){const auto b=track.sample(second,header[3],model.origin,a->loops,fallback);for(unsigned k=0;k<4;++k)v[k]+=(b[k]-v[k])*blend;}return v;};
                std::array<unsigned char,LightBytes> liveStorage;const unsigned char* live=span?runtimeScratch_.data()+(lights-bones)+l.index*LightBytes:liveStorage.data();
                if(!span&&!read(lights+l.index*LightBytes,liveStorage.data(),liveStorage.size()))continue;
                const bool enabled=at<std::uint32_t>(live,0x64)!=0&&value(l.enabled,{1,0,0,0})[0]>=1.f;
                if(!enabled||alpha==0)continue; // Missing IDs retire immediately at publication; disabled lamps do not spend light slots.
                const auto rgb=value(l.color,{});const float intensity=value(l.intensity,{l.intensity.sequences.empty()?1.f:0.f,0,0,0})[0];
                for(unsigned k=0;k<3;++k){p.color[k]=enabled?rgb[k]*intensity*alpha:0;p.attenuation[k]=at<float>(live,0xbc+4*k);
                    ok=ok&&std::isfinite(p.color[k])&&p.color[k]>=0&&p.color[k]<=10000&&std::isfinite(p.attenuation[k])&&p.attenuation[k]>=0&&p.attenuation[k]<=10000;}
                if(!ok||p.attenuation[0]+p.attenuation[1]+p.attenuation[2]<.001f){++malformed;continue;}
                if(std::max({p.color[0],p.color[1],p.color[2]})<=0)continue;
                unsigned slot=output.count;if(slot==Capacity){slot=unsigned(std::max_element(distances.begin(),distances.end())-distances.begin());if(d>=distances[slot])continue;}else ++output.count;
                output.lights[slot]=p;distances[slot]=d;
            }++it;
        }};
        // Refresh known sources before spending the remaining budget on cold
        // metadata. New sources are evaluated once in the same update.
        evaluate();
        const auto discover=[&](){
        if(!cursor_&&(header[2]!=lastHead_||!lastScan_||std::uint32_t(header[3]-lastScan_)>=200)){cursor_=header[2];lastHead_=header[2];lastScan_=header[3];}
        std::array<std::uint32_t,DiscoverySteps> walked{};unsigned walkedCount=0;
        for(unsigned step=0;cursor_&&step<DiscoverySteps;++step){const auto address=cursor_;
            if(std::find(walked.begin(),walked.begin()+walkedCount,address)!=walked.begin()+walkedCount){cursor_=0;++malformed;break;}walked[walkedCount++]=address;
            std::array<unsigned char,ModelBytes> m{};
            if(!read(address,m.data(),m.size())||at<std::uint32_t>(m,40)!=scene){cursor_=0;break;}
            cursor_=at<std::uint32_t>(m,12);++scanned;if(cursor_==address){cursor_=0;++malformed;break;}
            if(models_.count(address))continue; // Its current identity/placement is validated below.
            const auto shared=at<std::uint32_t>(m,44);std::uint32_t data=0;
            if(!(at<std::uint32_t>(m,16)&1)||at<std::uint32_t>(m,72)||!pointer(shared)||!sharedData(read,shared,data)||!pointer(data))continue;
            const auto limited=budgetLimited;Asset* a=asset(read,shared,data);if(budgetLimited!=limited){cursor_=address;break;}if(!a||a->lights.empty())continue;
            const auto origin=at<std::uint32_t>(m,0x74);auto found=models_.find(address);
            if(found!=models_.end()&&(found->second.shared!=shared||found->second.data!=data||found->second.origin!=origin)){models_.erase(found);found=models_.end();}
            if(found==models_.end()){
                float d=std::numeric_limits<float>::infinity();for(const auto& l:a->lights){float position[3];for(unsigned k=0;k<3;++k){position[k]=at<float>(m,0xe4+4*k);for(unsigned j=0;j<3;++j)position[k]+=l.point[j]*at<float>(m,0xb4+4*(j*4+k));}d=std::min(d,distance(position,camera));}
                if(!std::isfinite(d)||d>(NorthlightLocalLightSelection::VisibilityEnd+12.f)*(NorthlightLocalLightSelection::VisibilityEnd+12.f))continue;
                if(models_.size()==ModelLimit){auto farthest=std::max_element(models_.begin(),models_.end(),[](const auto& x,const auto& y){return x.second.distance<y.second.distance;});if(d>=farthest->second.distance)continue;models_.erase(farthest);}
                const auto id=serial_;serial_+=MaxLights;models_.emplace(address,Model{shared,data,origin,0,id+1,d});
            }
        }
        };
        discover();evaluate();
        std::uint32_t again=0;std::array<std::uint32_t,6> end{};
        readCalls_+=2;readBytes_+=28; // Reserved independently of the work budget.
        if(!input(ScenePointer,&again,4)||again!=scene||!input(scene,end.data(),sizeof end)||end!=header){output.clear();return;}
        output.scene=scene;output.time=header[3];output.valid=1;
    }
};
} // namespace NorthlightNativeLights
