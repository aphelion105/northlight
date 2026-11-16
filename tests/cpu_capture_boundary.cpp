#include "cpu_native_fixture.h"

int main(){
    using namespace NorthlightGpuScene;NativeStore scene;NativeClient cache(scene,Consumer::Material);Device device;Binding binding;
    scene.setFrame(1);std::vector<Owner> legacy;
    for(unsigned i=0;i<6000;++i){auto mesh=authored(100000+i);assert(!cache.bind(&device,mesh,binding.vb,binding.ib,admit,binding.at));}
    for(unsigned i=0;i<4;++i){auto mesh=std::make_shared<Mesh>(*authored(900000+i));mesh->sourceIdentity.reset();legacy.push_back(mesh);assert(!cache.bind(&device,mesh,binding.vb,binding.ib,admit,binding.at));}
    assert(cache.population().entries==6004);
    legacy[0].reset();legacy[2].reset();scene.setFrame(2);assert(cache.population().entries==6002&&cache.stats().expiredEntries==2);
    scene.setFrame(1);scene.setFrame(3);assert(cache.population().entries==6002);
    const auto start=std::chrono::steady_clock::now();
    for(unsigned frame=0;frame<2048;++frame)scene.setFrame(100+frame);
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    assert(cache.population().entries==6002&&!device.calls);
    legacy.clear();scene.setFrame(3000);assert(cache.stats().expiredEntries==2&&cache.population().entries==6000);
    cache.setLimit(0);scene.setFrame(3001);assert(!cache.population().entries);scene.clearAll();assert(!alive);
    std::printf("CPU_STAGE stage=boundary cpuMs=%.6f operations=2048 digest=6002\n",ms);
}
