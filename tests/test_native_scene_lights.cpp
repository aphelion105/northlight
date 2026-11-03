#include "native_scene_light_fixture.h"
int main(int argc,char** argv){
    {Fixture f;f.add();f.update();assert(f.output.valid&&f.output.count==1);const auto first=f.output.lights[0];
     assert(std::fabs(first.color[0]-.97254908f*.75f)<1e-6f&&first.position[2]==1); // never drawn, native RGB/visible/stamp all zero
     put(f.memory.object(f.models[0]),0xe4,7.f);put(f.memory.object(f.models[0]),0x178,.5f);f.update();assert(f.output.lights[0].id==first.id&&f.output.lights[0].position[0]==7&&std::fabs(f.output.lights[0].color[0]-first.color[0]*.5f)<1e-6f);
     f.memory.set(f.lights[0],0x64,0u);f.update();assert(!f.output.count);
     f.memory.set(f.lights[0],0x64,1u);put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,0);f.update();assert(!f.output.count);
     put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,1);f.update();assert(f.output.lights[0].color[0]>0);
     put(f.memory.object(f.models[0]),40,0u);put(f.memory.object(f.scene),8,0u);f.update();assert(f.output.valid&&!f.output.count&&!f.reader.modelCount());
    }
    {Fixture f;f.add();put(f.memory.object(f.models[0]),0x64,350u);f.update();assert(std::fabs(f.output.lights[0].color[0]-.97254908f*.625f)<1e-6f);}
    {Fixture f(modelAsset(),0x90000000);f.add();f.update();assert(f.output.valid&&f.output.count==1);} // LAA x86 allocations above 2 GB
    // Constant translations and rotation/scale about the emitter's exact pivot
    // are fixed points. Empty sequences must contribute their default transform.
    {Bytes b=modelAsset();authoredTrack(b,320,12,{{2,3,4}},3);authoredTrack(b,360,12,{{1,1,1,2,3,4}},3);
     Fixture f(b);f.add();f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==2&&f.output.lights[0].position[2]==5);
     put(b,648+4,.01f);Fixture moving(b);moving.add();moving.update();assert(!moving.output.count&&moving.reader.unsupported);}
    for(unsigned flags:{0x200u,0x80u}){Bytes b=modelAsset();put(b,308,flags);authoredTrack(b,320,12,{{},{2,3,4}},3);
     Fixture f(b);f.add();f.update();assert(!f.output.count&&f.reader.unsupported);}
    {Fixture f;f.add();put(f.memory.object(f.bones[0]),0x8c,0x200u);f.update();assert(f.output.count==1);}
    // Step visibility selects the new sequence immediately even while linear
    // colour/intensity tracks blend with the previous sequence.
    {Fixture f;f.add();auto& b=f.memory.object(f.bones[0]);put<std::uint16_t>(b,0x6c,0);put(b,0x70,100u);put(b,0x74,2100u);put(b,0x78,1.f);put(b,0x9c,1100u);put(b,0xa0,.001f);put(b,0xa4,1.f);
     f.update();assert(f.output.count==1&&f.output.lights[0].color[0]>0);}
    {Bytes b=modelAsset();authoredTrack(b,648+136,1,{{1,0},{1,0}},1);put<std::uint16_t>(b,648+136,1);Fixture f(b);f.add();f.update();assert(f.output.count==1); // byte keys remain step-valued even with a linear track type
     put(f.memory.object(f.scene),12,1100u);f.update();assert(!f.output.count);}
    // Global sequences use the model's loop origin, independently of a paused
    // ordinary sequence and the current camera or native animation stamp.
    {Bytes b=modelAsset();unsigned duration=1000;put(b,20,1u);put(b,24,append(b,&duration,4));put<std::int16_t>(b,648+76+2,0);
     Fixture f(b);f.add();put(f.memory.object(f.models[0]),0x64,350u);f.update();assert(std::fabs(f.output.lights[0].color[0]-.97254908f*.75f)<1e-6f);
     put(f.memory.object(f.scene),12,1100u);f.update();assert(std::fabs(f.output.lights[0].color[0]-.97254908f*.5f)<1e-6f);}
    // The native root may have no active sequence; stored clock and global
    // loops still drive light tracks before its first native animation/draw.
    {Fixture f;f.add();auto& b=f.memory.object(f.bones[0]);put<std::uint16_t>(b,0x48,0xffff);put<std::uint16_t>(b,0x44,1);put(b,0x40,500u);
     f.update();assert(f.output.count==1&&std::fabs(f.output.lights[0].color[0]-.97254908f*.75f)<1e-6f);}
    {Bytes b=modelAsset();unsigned duration=1000;put(b,20,1u);put(b,24,append(b,&duration,4));put<std::int16_t>(b,648+76+2,0);
     Fixture f(b);f.add();put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,0xffff);put<std::uint16_t>(f.memory.object(f.bones[0]),0x44,1);f.update();assert(f.output.count==1);
     const auto before=f.output.lights[0].color[0];put(f.memory.object(f.scene),12,1100u);f.update();assert(f.output.lights[0].color[0]<before);}
    // Shared identity may be reused between frames; same-frame metadata reuse
    // must not hide a new relocated asset or reuse the old source identity.
    {Fixture f;f.add();f.add(4);f.update();const auto id=f.output.lights[0].id;Bytes replacement=modelAsset();put(replacement,648+4,2.f);const auto data=f.memory.next;relocate(replacement,data);f.memory.add(std::move(replacement));put(f.memory.object(f.shared),0x150,data);
     f.update();f.update();assert(f.output.count==2&&f.output.lights[0].id!=id&&f.output.lights[0].position[0]>=2);}
    {Fixture f;f.add();put(f.memory.object(f.models[0]),0xb4,NAN);f.update();assert(!f.output.count);}
    {Bytes b=modelAsset();put<std::uint16_t>(b,648+56,2);Fixture f(b);f.add();f.update();assert(!f.output.count&&f.reader.unsupported);}
    for(unsigned field:{0x88u,0x8cu}){Fixture f;f.add();put(f.memory.object(f.bones[0]),field,1u);f.update();assert(f.output.valid&&!f.output.count&&f.reader.unsupported);}
    {Fixture f;f.add();f.memory.blocks[0x834550][0]^=1;f.update();assert(!f.output.valid&&!f.output.count);}
    {Fixture f;f.add();f.memory.fail=f.lights[0];f.update();assert(!f.output.count&&f.reader.failures);f.memory.fail=0;f.update();assert(f.output.count==1);}
    {Fixture f;for(unsigned i=0;i<SceneDetail::DiscoverySteps+8;++i)f.add(float(i)*2);for(unsigned n=0;n<3;++n)f.update();assert(f.output.valid&&f.reader.scanned>SceneDetail::DiscoverySteps);
     bool found=false;for(unsigned i=0;i<f.output.count;++i)found|=f.output.lights[i].position[0]==0;assert(found);}
    // Cold metadata and cache churn cannot starve an already known light or
    // permanently stop discovery after the first four unique assets.
    {Fixture f;f.add();f.update();const auto id=f.output.lights[0].id;const auto originalData=f.data,originalShared=f.shared;
     f.add(5);for(unsigned i=0;i<SceneDetail::AssetLimit+12;++i){Bytes b=modelAsset();f.data=f.memory.next;relocate(b,f.data);f.memory.add(std::move(b));f.shared=f.memory.add(Bytes(0x154));put(f.memory.object(f.shared),0x150,f.data);f.add(1000.f+i);}
     f.data=originalData;f.shared=originalShared;
     for(unsigned frame=0;frame<50;++frame){f.update();assert(f.output.valid);bool kept=false;for(unsigned i=0;i<f.output.count;++i)kept|=f.output.lights[i].id==id;assert(kept);assert(f.reader.readCalls()<=4096&&f.reader.readBytes()<=1024*1024);}
     assert(f.output.count==2&&f.reader.assetCount()<=SceneDetail::AssetLimit);}
    {Fixture f;f.add();f.update();const auto id=f.output.lights[0].id;f.add(5);
     Bytes b=modelAsset();std::vector<std::vector<float>> keys(512,{1,1,1});authoredTrack(b,648+56,12,keys,3);
     f.data=f.memory.next;relocate(b,f.data);f.memory.add(std::move(b));f.shared=f.memory.add(Bytes(0x154));put(f.memory.object(f.shared),0x150,f.data);f.add(10);
     for(unsigned frame=0;frame<4;++frame){f.update();assert(f.output.valid);bool kept=false;for(unsigned i=0;i<f.output.count;++i)kept|=f.output.lights[i].id==id;assert(kept);}
     assert(f.output.count==2&&f.reader.metadataLimited&&f.reader.readCalls()<=4096);}
    // Exhaust the runtime work budget with long stationary bone chains. The
    // reserved final scene check must still succeed, and unread entries stay
    // registered instead of being mistaken for despawned objects.
    {Fixture f;f.add();f.update();const auto id=f.output.lights[0].id;
     Bytes b=modelAsset(),bones(SceneDetail::MaxBones*88);for(unsigned i=0;i<SceneDetail::MaxBones;++i){std::memcpy(bones.data()+i*88,b.data()+304,88);put<std::int16_t>(bones,i*88+8,static_cast<std::int16_t>(i?i-1:0xffff));}
     put(b,44,SceneDetail::MaxBones);put(b,48,append(b,bones.data(),unsigned(bones.size())));put<std::uint16_t>(b,650,SceneDetail::MaxBones-1);
     f.data=f.memory.next;relocate(b,f.data);f.memory.add(std::move(b));f.shared=f.memory.add(Bytes(0x154));put(f.memory.object(f.shared),0x150,f.data);for(unsigned i=0;i<12;++i)f.add(5.f+i);
     for(unsigned frame=0;frame<3;++frame){f.update();assert(f.output.valid&&f.reader.modelCount()==13);bool kept=false;for(unsigned i=0;i<f.output.count;++i)kept|=f.output.lights[i].id==id;assert(kept);assert(f.reader.readCalls()<=4096&&f.reader.readBytes()<=1024*1024);}assert(f.reader.budgetLimited);}
    {Fixture f;f.add();f.add(5);const auto malformed=f.memory.add(Bytes(304));put(f.memory.object(malformed),0,0x3032444du);put(f.memory.object(malformed),4,264u);put(f.memory.object(malformed),44,1u);put(f.memory.object(malformed),48,0xfffffffcu);put(f.memory.object(malformed),264,1u);
     const auto shared=f.memory.add(Bytes(0x154));put(f.memory.object(shared),0x150,malformed);put(f.memory.object(f.models.back()),44,shared);f.update();assert(f.output.valid&&f.output.count==1&&f.reader.malformed);}
    // Real read-only MPQ models relocated like the client's M2 loader. Stand is
    // disabled; sequence149 is enabled. No original game file is modified.
    for(int arg=1;arg<argc;++arg){const bool pe=std::strcmp(argv[arg],"--exe")==0;if(pe)assert(++arg<argc);
        std::ifstream in(argv[arg],std::ios::binary|std::ios::ate);assert(in);Bytes blob(std::size_t(in.tellg()));in.seekg(0);in.read(reinterpret_cast<char*>(blob.data()),std::streamsize(blob.size()));
        if(pe){const auto nt=get<unsigned>(blob,60);assert(get<unsigned>(blob,nt)==0x4550);const auto sections=get<std::uint16_t>(blob,nt+6),optional=get<std::uint16_t>(blob,nt+20);const auto base=get<unsigned>(blob,nt+24+28);assert(base==0x400000);
            const auto read=[&](unsigned address,void* out,std::size_t n){for(unsigned i=0;i<sections;++i){const unsigned at=nt+24+optional+i*40,rva=get<unsigned>(blob,at+12),size=get<unsigned>(blob,at+16),offset=get<unsigned>(blob,at+20);
                if(address>=base+rva&&address-base-rva<=size&&n<=size-(address-base-rva)&&offset+(address-base-rva)+n<=blob.size()){std::memcpy(out,blob.data()+offset+(address-base-rva),n);return true;}}return false;};
            assert(Reader::verify(read));std::puts("ACTUAL_CLIENT_LAYOUT_PROOFS_PASS read-only PE section mapping");continue;}
        Fixture f(blob);f.add(0,3);f.update();assert(f.output.valid&&f.output.count>0&&f.output.lights[0].color[0]>0);
        put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,0);f.update();assert(!f.output.count);
        std::printf("REAL_NATIVE_LAMP_PASS %s\n",argv[arg]);}
#ifdef _WIN32
    for(unsigned count:{2u,16u,32u,128u,512u}){Fixture f;for(unsigned i=0;i<count;++i)f.add(float(i%16)*2);f.memory.system=true;
     auto start=std::chrono::steady_clock::now();f.update();const auto cold=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();const auto coldCalls=f.reader.readCalls();
     for(unsigned i=0;i<4;++i)f.update();start=std::chrono::steady_clock::now();for(unsigned i=0;i<200;++i){put(f.memory.object(f.scene),12,600+6*i);f.update();}const auto warm=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;
     assert(f.output.count==std::min(count,Capacity)&&f.reader.readCalls()<=4096&&f.reader.readBytes()<=1024*1024);
     std::printf("NATIVE_SCENE_RPM_PASS models=%u coldMs=%.4f coldReads=%u warmMs=%.4f warmReads=%u warmBytes=%zu\n",count,cold,coldCalls,warm,f.reader.readCalls(),f.reader.readBytes());}
    {Fixture f;for(unsigned i=0;i<2048;++i)f.add(i<16?float(i)*2:1000.f+float(i));f.memory.system=true;double coldMax=0;unsigned coldMaxCalls=0;
     for(unsigned i=0;i<16;++i){auto start=std::chrono::steady_clock::now();f.update();coldMax=std::max(coldMax,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());coldMaxCalls=std::max(coldMaxCalls,f.reader.readCalls());}
     assert(f.output.count==16&&f.reader.modelCount()==16);auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<200;++i){put(f.memory.object(f.scene),12,700+6*i);f.update();}
     const auto warm=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;
     std::printf("NATIVE_SCENE_RPM_DISTRIBUTION_PASS models=2048 nearby=16 coldBatchMaxMs=%.4f coldMaxReads=%u warmMs=%.4f\n",coldMax,coldMaxCalls,warm);}
#endif
    std::puts("NATIVE_SCENE_LIGHTS_PASS offscreen first discovery, clocks, current placement/alpha, enable transitions, detach, proof failure, unsafe bones, discovery continuation");
}
