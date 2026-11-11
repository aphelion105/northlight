#undef NDEBUG
#include "native_shadow_control.h"
#include "frame_requirements.h"
#include "snapshot_store.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <fstream>
#ifdef _WIN32
#include "native_shadow_control_windows.h"
#endif
using namespace NorthlightNativeShadowControl;

int main(int argc,char** argv){
    std::map<std::uintptr_t,unsigned> memory{{CVar,0xe00000},{0xe00030,5},{Quality,5},{Dirty,0}};
    auto read=[&](std::uintptr_t address,void* value,std::size_t size){auto p=memory.find(address);if(size!=4||p==memory.end())return false;std::memcpy(value,&p->second,4);return true;};
    unsigned writes=0,fail=0;
    auto write=[&](std::uintptr_t address,unsigned value){assert(address==Quality||address==Dirty);++writes;if(writes==fail)return false;memory[address]=value;return true;};
    Controller control;assert(control.update(sample(read),true,write));assert(memory[Quality]==0&&memory[Dirty]==1&&memory[0xe00030]==5);
    const auto acquired=writes;for(unsigned i=0;i<10000;++i)assert(control.update(sample(read),true,write));assert(writes==acquired);
    memory[0xe00030]=3;memory[Quality]=3;assert(control.update(sample(read),true,write));assert(memory[Quality]==0);
    assert(control.update(sample(read),false,write));assert(memory[Quality]==3&&!control.leased());
    const auto restored=writes;assert(control.update(sample(read),false,write)&&writes==restored);
    for(unsigned failed:{1u,2u}){
        memory[Quality]=memory[0xe00030]=5;memory[Dirty]=0;Controller c;writes=0;fail=failed;
        assert(!c.update(sample(read),true,write));assert(memory[Quality]==5&&c.leased());
        fail=0;assert(c.update(sample(read),true,write)&&memory[Quality]==0);
        writes=0;fail=failed;assert(!c.update(sample(read),false,write)&&c.leased());
        fail=0;assert(c.update(sample(read),false,write)&&memory[Quality]==5&&!c.leased());
    }
    for(auto key:{CVar,std::uintptr_t(0xe00030),Quality,Dirty}){
        const auto previous=memory[key];memory[key]=key==CVar?1:99;assert(!sample(read).readable);memory[key]=previous;
    }
    Controller empty;const auto beforeInvalid=writes;assert(!empty.update({},true,write)&&writes==beforeInvalid&&!empty.leased());
    {Controller pending;memory[Quality]=0;memory[Dirty]=1;const auto previous=writes;
     assert(pending.update(sample(read),true,write)&&!pending.leased()&&writes==previous);
     memory[Quality]=5;memory[Dirty]=0;assert(pending.update(sample(read),true,write));assert(pending.update(sample(read),false,write)&&memory[Quality]==5);}
    {
        using Snapshot=NorthlightStream::GameSnapshot;Snapshot frozen,missing,failed;
        memory[Quality]=5;for(const auto& item:memory)assert(frozen.record(item.first,&item.second,4,true));
        memory[Quality]=0;unsigned liveReads=0;auto live=[&](std::uintptr_t a,void* p,std::size_t n){++liveReads;return read(a,p,n);};
        assert(sampleFrame(&frozen,true,live).runtime==5&&liveReads==0);
        assert(!sampleFrame<Snapshot>(nullptr,true,live).readable&&liveReads==0);
        assert(!sampleFrame(&missing,true,live).readable&&liveReads==0);
        assert(failed.record(CVar,nullptr,4,false));assert(!sampleFrame(&failed,true,live).readable&&liveReads==0);
        assert(sampleFrame<Snapshot>(nullptr,false,live).runtime==0&&liveReads==4);
    }
    NorthlightQuality::Settings q;q.giStrength=q.localLights=q.worldFog=q.weather=q.autoBump=0;q.contactAO=1;
    NorthlightEffectSwitches::Settings effects;effects.gi=effects.fog=false;
    for(unsigned provider:{0u,1u})for(bool available:{false,true}){
        q.shadowProvider=provider;effects.shadows=true;auto plan=NorthlightFrameRequirements::make(q,effects,false,false,available);
        assert(plan.shadows==bool(provider&&available));
        effects.shadows=false;plan=NorthlightFrameRequirements::make(q,effects,false,false,available);assert(plan.ao&&!plan.lighting&&!plan.modelNormals&&!plan.shadows);
    }
    q.shadowProvider=0;effects.shadows=true;q.localLights=q.localLightLimit=q.pointShadows=q.pointShadowLimit=1;
    assert(NorthlightQuality::actorShadowWork(q,true)&&!NorthlightQuality::actorShadowMap(q,true));
    q.pointShadows=0;assert(!NorthlightQuality::actorShadowWork(q,true));
    unsigned nativeFrames=0,cleanup=0,regenerate=0,nativeTransitions=0;
#ifdef _WIN32
    // Run the exact native producer's quality gates, with isolated fake client
    // globals. Only its expensive calls are replaced by counting exits. This
    // never loads or starts the game, or invokes a graphics driver.
    assert(argc==2);std::ifstream file(argv[1],std::ios::binary);std::vector<unsigned char> builder((std::istreambuf_iterator<char>(file)),{});assert(builder.size()==983);
    auto* globals=static_cast<unsigned char*>(VirtualAlloc(nullptr,65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(globals);
    auto translate=[&](std::uintptr_t address)->unsigned char*{
        if(address>=0xd43000&&address<0xd44000)return globals+address-0xd40000;
        if(address==Dirty)return globals+0x4000;if(address==CVar)return globals+0x4004;if(address==0xe00030)return globals+0x4008;
        assert(false);return nullptr;
    };
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(code);std::memcpy(code,builder.data(),builder.size());
    unsigned relocated=0;
    for(unsigned i=0;i+4<=builder.size();++i){std::uint32_t address;std::memcpy(&address,code+i,4);
        if(address==Dirty||(address>=0xd43000&&address<0xd44000)){address=std::uint32_t(reinterpret_cast<std::uintptr_t>(translate(address)));std::memcpy(code+i,&address,4);++relocated;i+=3;}}
    assert(relocated>=10);
    auto branch=[&](unsigned at,unsigned target){assert(code[at]==0xe8);const auto rel=std::int32_t(target-at-5);std::memcpy(code+at+1,&rel,4);};
    auto stub=[&](unsigned at,unsigned* counter,bool finish){
        code[at]=0xff;code[at+1]=0x05;const auto address=std::uint32_t(reinterpret_cast<std::uintptr_t>(counter));std::memcpy(code+at+2,&address,4);
        if(finish){code[at+6]=0x83;code[at+7]=0xc4;code[at+8]=4;code[at+9]=0xe9;const auto rel=std::int32_t(0x3a9-(at+14));std::memcpy(code+at+10,&rel,4);}else code[at+6]=0xc3;
    };
    stub(1024,&cleanup,false);stub(1040,&regenerate,false);stub(1056,&nativeFrames,true);
    branch(0x15,1024);branch(0x1a,1040);branch(0x6f,1056);
    DWORD previous=0;assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&previous));assert(FlushInstructionCache(GetCurrentProcess(),code,4096));
    auto cell=[&](std::uintptr_t address)->unsigned&{return *reinterpret_cast<unsigned*>(translate(address));};
    cell(CVar)=0xe00000;cell(0xe00030)=cell(Quality)=5;cell(Dirty)=0;
    for(std::uintptr_t address=0xd43158;address<=0xd43164;address+=4)cell(address)=1;
    auto rawRead=[&](std::uintptr_t address,void* value,std::size_t size){assert(size==4);std::memcpy(value,translate(address),size);return true;};
    auto rawWrite=[&](std::uintptr_t address,unsigned value){assert(address==Quality||address==Dirty);cell(address)=value;return true;};
    using Producer=int(__cdecl*)(const float*,int);auto producer=reinterpret_cast<Producer>(code);
    assert(producer(nullptr,0)==1&&nativeFrames==1);
    Controller nativeControl;
    for(unsigned pass=0;pass<200;++pass){
        const bool replace=(pass&1)==0;assert(nativeControl.update(sample(rawRead),replace,rawWrite));const auto before=nativeFrames;
        assert(producer(nullptr,0)==1);assert(nativeFrames-before==unsigned(!replace));assert(cell(Dirty)==0&&cell(0xe00030)==5);
        ++nativeTransitions;
    }
    assert(!nativeControl.leased()&&cell(Quality)==5&&cleanup==200&&regenerate==200);
    assert(!verified());publish(code,true);producerPresent();assert(cell(Quality)==5);release(code); // An unsupported image is untouched.
    assert(VirtualFree(code,0,MEM_RELEASE)&&VirtualFree(globals,0,MEM_RELEASE));
#else
    (void)argc;(void)argv;
#endif
    std::printf("NATIVE_SHADOW_CONTROL_PASS nativeTransitions=%u stableFrames=10000 nativeProducerVisits=%u cleanup=%u regenerate=%u rollback=4 strictFrameState=1 savedCvarUntouched=1 aoIndependent=1 pointIndependent=1\n",nativeTransitions,nativeFrames,cleanup,regenerate);
}
