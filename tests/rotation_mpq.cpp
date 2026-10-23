// Actual MPQ positions/holes and captured camera motion through the production capture cache.
#include "rotation_device.h"
#include "memory_guard.h"
#include <chrono>
#include <fstream>
#include <string>

namespace NorthlightDrawSnapshot {struct Frame {static constexpr uint64_t SnapshotCacheLimit=64ull<<20;};}
struct LimitSink {void setSnapshotCacheLimit(uint64_t){} void setLimit(uint64_t){};};
template<class Cache> struct WorldCaps {
    Cache& terrainBoundsCache;bool memoryPressure=false;
    LimitSink replaySnapshots,replayGpuCache;
    explicit WorldCaps(Cache& cache):terrainBoundsCache(cache){}
    void setNearReserve(){}
#include "rotation_setter.h"
};
struct Camera {uint64_t tick=0;float eye[3]{},forward[3]{},distance=0;};
struct Draw {unsigned base=0,vertices=0,start=0,count=0;float center[3]{};};
template<class T> static void read(std::ifstream& f,T& v){assert(f.read(reinterpret_cast<char*>(&v),sizeof v));}
static unsigned header(std::ifstream& f,const char* expected){char magic[4];assert(f.read(magic,4)&&!std::memcmp(magic,expected,4));unsigned n;read(f,n);assert(n&&n<100000);return n;}
static std::vector<Camera> cameras(const std::string& path){
    std::ifstream f(path,std::ios::binary);std::vector<Camera> out(header(f,"NLC1"));
    for(auto& c:out){read(f,c.tick);for(auto& v:c.eye)read(f,v);for(auto& v:c.forward)read(f,v);read(f,c.distance);}
    assert(f.peek()==EOF);return out;
}
static std::vector<Draw> terrain(Device& d,const std::string& path){
    std::ifstream f(path,std::ios::binary);const auto n=header(f,"NLT1");std::vector<Draw> draws(n);
    std::vector<Position> positions;std::vector<uint16_t> indices;
    for(auto& draw:draws){
        draw.base=unsigned(positions.size());draw.start=unsigned(indices.size());read(f,draw.vertices);unsigned count;read(f,count);
        assert(draw.vertices&&draw.vertices<=65535&&count&&count%3==0);draw.count=count/3;
        float lo[3]={INFINITY,INFINITY,INFINITY},hi[3]={-INFINITY,-INFINITY,-INFINITY};
        for(unsigned i=0;i<draw.vertices;++i){Position p;read(f,p.x);read(f,p.y);read(f,p.z);assert(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z));positions.push_back(p);unsigned j=0;for(float v:{p.x,p.y,p.z}){lo[j]=std::min(lo[j],v);hi[j]=std::max(hi[j],v);++j;}}
        for(unsigned i=0;i<3;++i)draw.center[i]=(lo[i]+hi[i])*.5f;
        for(unsigned i=0;i<count;++i){unsigned v;read(f,v);assert(v<draw.vertices);indices.push_back(uint16_t(v));}
    }
    assert(f.peek()==EOF);d.vb.bytes.resize(16+positions.size()*24);for(unsigned i=0;i<positions.size();++i)d.put(i,positions[i]);
    d.ib.desc.Format=D3DFMT_INDEX16;d.ib.bytes.resize(indices.size()*2);std::memcpy(d.ib.bytes.data(),indices.data(),d.ib.bytes.size());return draws;
}
static uint64_t identity(void*,bool index){return index?12:11;}
static uint64_t vertexVersion=21;
static uint64_t version(void*,bool index){return index?22:vertexVersion;}
static uint64_t geometry(const MeshSnapshot& mesh,const Device& d,const Draw& draw){
    assert(mesh.indices.size()==size_t(draw.count)*3);uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<mesh.indices.size();++i){uint16_t index;std::memcpy(&index,d.ib.bytes.data()+2*(draw.start+i),2);Position expected;std::memcpy(&expected,d.vb.bytes.data()+16+24*(draw.base+index)+4,12);
        const auto actual=mesh.positions[mesh.indices[i]];assert(!std::memcmp(&actual,&expected,sizeof actual));
        for(float v:{actual.x,actual.y,actual.z}){uint32_t word;std::memcpy(&word,&v,4);h^=word;h*=1099511628211ull;}}
    assert(!mesh.bounds.chunks.empty());return h;
}
static bool visible(const Draw& draw,const Camera& camera,float reach){
    const float dx=draw.center[0]-camera.eye[0],dy=draw.center[1]-camera.eye[1];
    const float distance=std::sqrt(dx*dx+dy*dy);if(distance>reach)return false;
    const float length=std::sqrt(camera.forward[0]*camera.forward[0]+camera.forward[1]*camera.forward[1]);assert(length>0);
    // The original D3D draw stream is unavailable: use an explicit 110-degree candidate cone.
    return distance<48||(dx*camera.forward[0]+dy*camera.forward[1])>=distance*length*.573576436f;
}
static void scenario(Device& d,const std::vector<Draw>& draws,const std::vector<Camera>& cameras,float reach,bool trueLow,bool writes){
    vertexVersion=21;
    auto cache=std::make_unique<FrameCache>();cache->setIdentityProvider(identity);cache->setVersionProvider(version);
    WorldCaps<FrameCache> world{*cache};NorthlightMemoryGuard::Guard guard;
    uint64_t allHash=0;size_t totalCalls=0,totalMisses=0,totalEvictions=0,totalReads=0,totalReuses=0;double totalMs=0;
    for(unsigned cycle=0;cycle<4;++cycle){
        if(cycle==1){
            const auto decision=guard.update(755ull<<20,(trueLow?159ull:237ull)<<20,1000,768ull<<20);assert(decision.trim&&decision.entered);
            cache->clearFrame();cache->clearPersistent();
#if BASELINE_PRESSURE
            world.setMemoryPressure(guard.pressure());
#else
            world.setMemoryPressure(guard.pressure(),guard.capsHalved());
#endif
        }
        for(unsigned frame=0;frame<cameras.size();++frame){
            // Model DISCARD/rewrite revisions without changing the MPQ XYZ payload.
            if(writes)++vertexVersion;
            cache->clearFrame();unsigned calls=0,hits=0;uint64_t digest=0;double cpuMs=0;
            for(unsigned i=0;i<draws.size();++i){const auto& draw=draws[i];if(!visible(draw,cameras[frame],reach))continue;
                std::shared_ptr<const MeshSnapshot> mesh;Diagnostics detail;
                const auto start=std::chrono::steady_clock::now();
                assert(cache->readMeshShared(&d,D3DPT_TRIANGLELIST,INT(draw.base),0,draw.vertices,draw.start,draw.count,d.view,mesh,&detail));
                cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                ++calls;hits+=detail.contentCacheHit;digest^=geometry(*mesh,d,draw)+(uint64_t(i)<<32);
            }
            assert(calls&&cache->persistentBytes()<=cache->persistentByteLimit());const auto m=cache->maintenance();allHash=(allHash^digest^(uint64_t(cycle)<<32)^frame)*1099511628211ull;
            std::printf("ROTATE low=%u writes=%u cycle=%u frame=%u calls=%u hits=%u misses=%u reads=%llu evictions=%u exactReuses=%u vertexChanged=%u cap=%llu retained=%llu captureMs=%.6f digest=%016llx\n",unsigned(trueLow),unsigned(writes),cycle,frame,calls,hits,m.lookupMisses,(unsigned long long)cache->bytesRead(),m.evictions,m.exactReuses,m.vertexChanged,(unsigned long long)cache->persistentByteLimit(),(unsigned long long)cache->persistentBytes(),cpuMs,(unsigned long long)digest);
            if(cycle>=2){totalCalls+=calls;totalMisses+=m.lookupMisses;totalEvictions+=m.evictions;totalReads+=cache->bytesRead();totalReuses+=m.exactReuses;totalMs+=cpuMs;}
        }
    }
    std::printf("ROTATE_SUMMARY low=%u writes=%u calls=%zu misses=%zu reads=%zu evictions=%zu exactReuses=%zu captureMs=%.6f geometry=%016llx\n",unsigned(trueLow),unsigned(writes),totalCalls,totalMisses,totalReads,totalEvictions,totalReuses,totalMs,(unsigned long long)allHash);
    if(trueLow)assert(cache->persistentByteLimit()==16ull*1024*1024);
}
int main(int argc,char** argv){
    assert(argc==4);Device d;auto draws=terrain(d,argv[1]);auto trace=cameras(argv[2]);float reach=std::stof(argv[3]);assert(reach>0);
    for(bool writes:{false,true}){scenario(d,draws,trace,reach,false,writes);scenario(d,draws,trace,reach,true,writes);}
    assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
}
