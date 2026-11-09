#include "native_scene_light_fixture.h"

namespace NorthlightNativeLights {
struct NativeSceneLightCacheTest {
    static void check(const Reader& reader){
#ifndef NORTHLIGHT_NATIVE_CACHE_REFERENCE
        std::size_t bytes=0;assert(reader.assetLookup_.size()==reader.assets_.size());
        for(auto it=reader.assets_.begin();it!=reader.assets_.end();++it){bytes+=it->bytes;
            const auto range=reader.assetLookup_.equal_range(it->shared);unsigned matches=0;
            for(auto i=range.first;i!=range.second;++i)matches+=&*i->second==&*it;
            assert(matches==1);
        }
        assert(bytes==reader.assetBytes_&&bytes<=SceneDetail::CacheBytes);
#else
        (void)reader;
#endif
    }
};
}

int main(int argc,char** argv){
    assert(argc==3);FILE* output=std::fopen(argv[1],"wb");assert(output);
    const bool timing=std::atoi(argv[2])!=0;
    Bytes blob=modelAsset();authoredTrack(blob,320,12,{{0,0,0,4,0,0}},3);
    Fixture f(blob);std::vector<unsigned> assets;
    for(unsigned asset=0;asset<96;++asset){
        if(asset)f.useAsset(blob);assets.push_back(f.data);
        for(unsigned model=0;model<4;++model)f.add(float((asset+model)%16)*2);
    }
    const auto update=[&](unsigned frame){
        put(f.memory.object(f.scene),12,600+6*frame);f.update();
        if(!timing)NativeSceneLightCacheTest::check(f.reader);
        assert(f.output.valid&&f.output.count);
        assert(std::fwrite(&f.output,16,1,output)==1);
        assert(std::fwrite(f.output.lights.data(),sizeof(Point),f.output.count,output)==f.output.count);
    };
    for(unsigned i=0;i<64;++i)update(i);
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=64;i<464;++i){
        if(!timing){
            if(i%17==0)put(f.memory.object(assets[i%assets.size()]),296,i);
            if(i%29==0)f.memory.set(f.models[i%f.models.size()],0x178,i%2?.5f:1.f);
            if(i%101==0)f.reader.reset();
            if(i%113==0)f.reader.fixtures(std::make_shared<NorthlightNativeFixtures::Catalog>());
        }
        update(i);
    }
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/400;
    f.reader.reset();NativeSceneLightCacheTest::check(f.reader);std::fclose(output);
    std::printf("NATIVE_LIGHT_ASSET_CACHE_PASS mode=%s models=384 assets=96 warmMs=%.6f current_pose=1 byte_ledger=1 header_rebuild=1 reset=1\n",timing?"timing":"mutation",ms);
}
