#include "cpu_material_fixture.h"

struct DisplacementStateTestAccess {
    static void aliases(){
        NorthlightAutoDisplacement g(nullptr);std::vector<std::shared_ptr<const Mesh>> sources,aliases;
        for(unsigned i=0;i<32;++i){
            auto original=std::make_shared<Mesh>(geometry(8190));const float unique=float(i);std::memcpy(original->streams[0].bytes.data(),&unique,4);original->sourceIdentity=NorthlightSourceIdentity::make({0xcee111,i});
            auto copy=std::make_shared<Mesh>(*original);copy->sourceIdentity=NorthlightSourceIdentity::make({0xcee222,i});copy->replayIdentity=original->sourceIdentity;
            auto entry=std::make_unique<NorthlightAutoDisplacement::Entry>();entry->source=original;entry->program=7;entry->domain=4;g.retain(std::move(entry));sources.push_back(original);aliases.push_back(copy);
        }
        auto changed=std::make_shared<Mesh>(*aliases[0]);changed->replayIdentity.reset();changed->sourceIdentity=NorthlightSourceIdentity::make({0xbad});changed->streams[0].bytes[100]^=1;
        assert(g.geometrySource(changed,nullptr,7,4)==changed);
        assert(!g.sameSource(*g.entries[0],changed,true));
        auto independent=std::make_shared<Mesh>(*sources[0]);independent->sourceIdentity=NorthlightSourceIdentity::make({0x123});
        assert(g.sameSource(*g.entries[0],independent,true)); // Full-byte fallback still accepts independent identical sources.
        assert(!g.sameSource(*g.entries[0],aliases[0],false));
        assert(g.geometrySource(aliases[0],nullptr,8,4)==aliases[0]);
        assert(g.geometrySource(aliases[0],nullptr,7,3)==aliases[0]);
        uint64_t digest=0;const auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<128;++frame)for(unsigned i=0;i<aliases.size();++i){auto result=g.geometrySource(aliases[i],nullptr,7,4);assert(result==sources[i]);digest=digest*33+result->vertexCount;}
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        // A memo must not authorize a copied claim with different bytes/layout.
        for(unsigned field=0;field<5;++field){auto copy=std::make_shared<Mesh>(*aliases[0]);
            if(field==0)copy->streams[0].bytes[100]^=1;
            if(field==1)++copy->streams[0].stride;
            if(field==2)copy->streams[1].bytes={1};
            if(field==3)++copy->primitiveCount;
            if(field==4)copy->topology=D3DPT_TRIANGLESTRIP;
            assert(g.geometrySource(copy,nullptr,7,4)==copy);
        }
        g.reset();assert(g.geometrySource(aliases[0],nullptr,7,4)==aliases[0]);
        std::printf("CPU_STAGE stage=aliases cpuMs=%.6f operations=4096 digest=%llu\n",ms,(unsigned long long)digest);
    }
};

static Status captured(EnvelopeCache& cache,const std::shared_ptr<const Prepared>& prepared,const std::shared_ptr<const Mesh>& mesh,const float* bank,const float* basis,Budget& budget,Bounds& result,const NorthlightConstantEpoch::Stamp& stamp){
#ifdef NORTHLIGHT_CPU_CERTIFICATES
    return cache.calculatePrepared(*prepared,*mesh,mesh,bank,basis,budget,result,EnvelopeCache::Stage::All,nullptr,&stamp);
#else
    (void)stamp;return cache.calculatePrepared(*prepared,*mesh,mesh,bank,basis,budget,result);
#endif
}
static void poses(const char* path){
    auto program=load(path);assert(SkinEnvelope::supports(program));
    auto mesh=std::make_shared<const Mesh>(geometry(1200));auto prepared=EnvelopeCache::prepareProgram(program,declaration,4);assert(prepared);
    float bank[1024],basis[16]={};for(unsigned i=0;i<4;++i)basis[i*5]=1;
    const float a=.37f;basis[0]=basis[10]=std::cos(a);basis[2]=-std::sin(a);basis[8]=std::sin(a);
    EnvelopeCache cache(true),oracle(true);NorthlightConstantEpoch::Clock clock;palette(bank,0);
    Bounds warm,reference;assert(finish(cache,program,mesh,bank,basis,warm)==Status::Valid);assert(finish(oracle,program,mesh,bank,basis,reference)==Status::Valid);
    uint64_t digest=0;double elapsed=0;
    for(unsigned frame=0;frame<128;++frame){
        palette(bank,frame);clock.writeFloat(31,225);auto stamp=clock.stamp(true);stamp.held=true;
        cache.beginFrame(false,8);Budget budget;budget.maxOperations=1000000;Bounds output;
        const auto start=std::chrono::steady_clock::now();
        for(unsigned pass=0;pass<8;++pass)assert(captured(cache,prepared,mesh,bank,basis,budget,output,stamp)==Status::Valid);
        elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        oracle.beginFrame(false);Budget independent;independent.maxOperations=1000000;
        assert(oracle.calculatePrepared(*prepared,*mesh,mesh,bank,basis,independent,reference)==Status::Valid);
        assert(output.valid&&std::memcmp(output.low,reference.low,sizeof output.low)==0&&std::memcmp(output.high,reference.high,sizeof output.high)==0);
        uint32_t bits;std::memcpy(&bits,output.low,4);digest=digest*33+bits;
    }
#ifdef NORTHLIGHT_CPU_CERTIFICATES
    assert(cache.stats().certifiedPoseHits==7);
    for(unsigned change=0;change<9;++change){
        cache.beginFrame(false);oracle.beginFrame(false);Budget budget,plain;budget.maxOperations=plain.maxOperations=1000000;
        const auto before=cache.stats().certifiedPoseHits;
        if(change==0)clock.writeFloat(8,1); // Outside every palette block.
        if(change==1){bank[124+3]+=1;clock.writeFloat(31,1);}
        if(change==2){bank[4*255+3]+=.3f;clock.writeFloat(255,1);}
        if(change==3)clock.invalidate();
        if(change==4)clock.writeBool(1);
        if(change==5)clock.writeInt(1);
        if(change==6)basis[12]+=.25f;
        auto stamp=clock.stamp(change!=8);stamp.held=change!=7;
        Bounds got;assert(captured(cache,prepared,mesh,bank,basis,budget,got,stamp)==Status::Valid);
        assert(oracle.calculatePrepared(*prepared,*mesh,mesh,bank,basis,plain,reference)==Status::Valid);
        assert(!std::memcmp(got.low,reference.low,sizeof got.low)&&!std::memcmp(got.high,reference.high,sizeof got.high));
        assert(cache.stats().certifiedPoseHits-before==(change==0));
    }
    cache.beginFrame(false);Budget denied;denied.maxOperations=0;auto stamp=clock.stamp(true);stamp.held=true;Bounds rejected;
    assert(captured(cache,prepared,mesh,bank,basis,denied,rejected,stamp)==Status::Budget&&!rejected.valid);
    cache.clearPoseCertificates();cache.beginFrame(false);Budget budget;budget.maxOperations=1000000;
    assert(captured(cache,prepared,mesh,bank,basis,budget,rejected,stamp)==Status::Valid&&!cache.stats().certifiedPoseHits);
    // A new immutable owner and clear/rebuild cannot inherit the old serial.
    auto revised=geometry(1200);const float different=1;std::memcpy(revised.streams[0].bytes.data(),&different,4);auto replacement=std::make_shared<const Mesh>(std::move(revised));
    assert(finish(cache,program,replacement,bank,basis,warm)==Status::Valid);
    cache.beginFrame(false);budget={};budget.maxOperations=1000000;
    assert(captured(cache,prepared,replacement,bank,basis,budget,rejected,stamp)==Status::Valid&&!cache.stats().certifiedPoseHits);
    cache.clear();assert(finish(cache,program,mesh,bank,basis,warm)==Status::Valid);
    cache.beginFrame(false);budget={};budget.maxOperations=1000000;
    assert(captured(cache,prepared,mesh,bank,basis,budget,rejected,stamp)==Status::Valid&&!cache.stats().certifiedPoseHits);
#endif
    std::printf("CPU_STAGE stage=poses cpuMs=%.6f operations=1024 digest=%llu cacheObjectBytes=%zu\n",elapsed,(unsigned long long)digest,sizeof(EnvelopeCache));
}
static void posePatterns(const char* path,bool rewrite){
    auto program=load(path);auto input=geometry(1200);
    if(rewrite)for(unsigned i=0;i<input.vertexCount;++i)for(unsigned k=0;k<4;++k)input.streams[0].bytes[i*32+28+k]%=8;
    auto mesh=std::make_shared<const Mesh>(std::move(input));
    auto prepared=EnvelopeCache::prepareProgram(program,declaration,4);assert(prepared);
    float bank[1024],basis[2][16]={};for(unsigned i=0;i<4;++i)basis[0][i*5]=basis[1][i*5]=1;
    const float a=.37f;basis[1][0]=basis[1][10]=std::cos(a);basis[1][2]=-std::sin(a);basis[1][8]=std::sin(a);
    EnvelopeCache cache(true),oracle(true);palette(bank,0);Bounds warm;
    assert(finish(cache,program,mesh,bank,basis[0],warm)==Status::Valid);
    assert(finish(oracle,program,mesh,bank,basis[0],warm)==Status::Valid);
    NorthlightConstantEpoch::Clock clock;uint64_t digest=0;double elapsed=0;
    for(unsigned frame=0;frame<128;++frame){
        palette(bank,frame);clock.writeFloat(31,225);cache.beginFrame(false,8);oracle.beginFrame(false,8);
        Budget budget,plain;budget.maxOperations=plain.maxOperations=1000000;Bounds outputs[8],reference;
        const auto start=std::chrono::steady_clock::now();
        for(unsigned pass=0;pass<8;++pass){
            if(rewrite)clock.writeFloat(31,225);auto stamp=clock.stamp(true);stamp.held=true;
            assert(captured(cache,prepared,mesh,bank,basis[rewrite?0:pass%2],budget,outputs[pass],stamp)==Status::Valid);
        }
        elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        for(unsigned pass=0;pass<8;++pass){
            oracle.beginFrame(false);
            assert(oracle.calculatePrepared(*prepared,*mesh,mesh,bank,basis[rewrite?0:pass%2],plain,reference)==Status::Valid);
            assert(!std::memcmp(outputs[pass].low,reference.low,sizeof reference.low)&&!std::memcmp(outputs[pass].high,reference.high,sizeof reference.high));
            uint32_t bits;std::memcpy(&bits,outputs[pass].low,4);digest=digest*33+bits;
        }
#ifdef NORTHLIGHT_CAPTURE_ACCELERATION
        assert(rewrite?cache.stats().certifiedPoseHits==0:cache.stats().certifiedPoseHits>=6);
#endif
    }
#ifdef NORTHLIGHT_CAPTURE_ACCELERATION
    if(rewrite){
        for(unsigned change=0;change<2;++change){
            const unsigned reg=change?31:255;bank[4*reg+3]+=1;clock.writeFloat(reg,1);
            auto stamp=clock.stamp(true);stamp.held=true;cache.beginFrame(false);oracle.beginFrame(false);
            Budget budget,plain;budget.maxOperations=plain.maxOperations=1000000;Bounds got,reference;
            assert(captured(cache,prepared,mesh,bank,basis[0],budget,got,stamp)==Status::Valid);
            assert(oracle.calculatePrepared(*prepared,*mesh,mesh,bank,basis[0],plain,reference)==Status::Valid);
            assert(!std::memcmp(got.low,reference.low,sizeof got.low)&&!std::memcmp(got.high,reference.high,sizeof got.high));
            assert(cache.stats().certifiedPoseHits==0);
        }
    }
#endif
    std::printf("CPU_STAGE stage=%s cpuMs=%.6f operations=1024 digest=%llu cacheObjectBytes=%zu\n",rewrite?"pose-rewrites":"pose-bases",elapsed,(unsigned long long)digest,sizeof(EnvelopeCache));
}
int main(int argc,char** argv){assert(argc==3);const std::string stage=argv[1];if(stage=="aliases")DisplacementStateTestAccess::aliases();else if(stage=="pose-bases"||stage=="pose-rewrites")posePatterns(argv[2],stage=="pose-rewrites");else poses(argv[2]);}
