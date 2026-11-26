#ifdef _WIN32
// Keep the platform's C++ runtime, then select the portable fake-device wait
// path, as in pipeline_bindings_cpu.cpp. The D3D stub replaces Win32 client APIs.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <unordered_set>
#include <queue>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#undef _WIN32
#undef _WIN64
#endif
#define main command_stream_original_main
#include "test_command_stream.cpp"
#undef main

static void sparseWrites(){
    gTrace.clear();Rig rig(true);auto& q=rig.core().q;void* data=nullptr;
    IDirect3DVertexBuffer9* vb=nullptr;
    CHECK(rig.dev->CreateVertexBuffer(40u<<20,D3::kUsageDynamic,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK);
    constexpr unsigned regions=8,rounds=64,bytes=4096;
    for(unsigned region=0;region<regions;++region){
        CHECK(vb->Lock(region*(5u<<20),bytes,&data,0)==D3D_OK);
        std::memset(data,0x30+region,bytes);CHECK(vb->Unlock()==D3D_OK);
    }
    rig.sync();const auto before=get(q.stats.syncCalls),start=nowNs();
    for(unsigned round=0;round<rounds;++round)for(unsigned region=0;region<regions;++region){
        CHECK(vb->Lock(region*(5u<<20),bytes,&data,D3::kLockNoOverwrite)==D3D_OK);
        CHECK(static_cast<unsigned char*>(data)[bytes-1]==0x30+region);
        if(round)CHECK(static_cast<unsigned char*>(data)[0]==round-1);
        static_cast<unsigned char*>(data)[0]=static_cast<unsigned char>(round);
        CHECK(vb->Unlock()==D3D_OK);
    }
    rig.sync();const auto elapsed=nowNs()-start,waits=get(q.stats.syncCalls)-before-1; // explicit rig.sync above
#if NORTHLIGHT_EXPECT_SPARSE
    CHECK(waits==0);CHECK(q.stats.rangeShadowBytes.load()==regions*bytes);
#else
    CHECK(waits==2*regions*rounds);
#endif
    // Observe the backend after ordered replay, not the CPU mirror.
    for(unsigned region=0;region<regions;++region){
        CHECK(vb->Lock(region*(5u<<20),bytes,&data,D3::kLockReadOnly)==D3D_OK);
        CHECK(static_cast<unsigned char*>(data)[0]==rounds-1);
        for(unsigned i=1;i<bytes;++i)CHECK(static_cast<unsigned char*>(data)[i]==0x30+region);
        CHECK(vb->Unlock()==D3D_OK);
    }
    std::printf("SPARSE_RANGES ms=%.6f syncs=%llu writes=%u bytes=%lld\n",elapsed/1e6,(unsigned long long)waits,regions*rounds,(long long)q.stats.rangeShadowBytes.load());
    vb->Release();rig.sync();CHECK(q.stats.rangeShadowBytes.load()==0);rig.finish();checkClean();gTrace.clear();
}

#if NORTHLIGHT_EXPECT_SPARSE
static void sparseProofs(){
    BufferRangeShadow cache;unsigned char source[16384],result[16384];std::memset(source,0x71,sizeof source);
    cache.remember(0,64,source,RangeShadowBudgetBytes);
    cache.remember(128,64,source,RangeShadowBudgetBytes-cache.capacity());
    CHECK(!cache.canRead(0,192)); // unknown gap cannot become zero-filled proof
    std::memset(source,0x29,sizeof source);cache.remember(32,128,source,RangeShadowBudgetBytes-cache.capacity());
    CHECK(cache.covers(0,192));cache.read(result,0,192);
    for(unsigned i=0;i<192;++i)CHECK(result[i]==(i>=32&&i<160?0x29:0x71));
    // A subset selecting the other overlapping copy must also see the rewrite.
    cache.read(result,128,64);for(unsigned i=0;i<64;++i)CHECK(result[i]==(i<32?0x29:0x71));
    cache.clear();cache.discard();
    constexpr unsigned spans=RangeShadowItemBytes/RangeShadowMinAllocation,gap=3u<<20;
    for(unsigned i=0;i<spans;++i)cache.remember(i*gap,64,source,RangeShadowBudgetBytes-cache.capacity());
    CHECK(cache.canRead(32,64));
    cache.remember(spans*gap,64,source,RangeShadowBudgetBytes-cache.capacity());
    CHECK(!cache.canRead(0,32)&&!cache.canRead(32,64)); // one interval past the span count evicts and revokes DISCARD proof
    CHECK(cache.canRead(1024,32)); // bytes never written since the DISCARD stay undefined
    CHECK(cache.covers(spans*gap,64)&&cache.capacity()<=RangeShadowItemBytes);
    CHECK(!cache.canRead(UINT32_MAX-8,16));
    cache.invalidate();CHECK(!cache.canRead(spans*gap,64));
    cache.discard();cache.clear(); // the slot pool below starts as a fresh buffer: nothing written, no DISCARD proof
    // A slot pool (the client's terrain VB): 8120-byte slots written in one ascending run, then
    // rewritten scattered. Joins stop at RangeShadowJoinBytes, so the whole run stays readable.
    std::vector<unsigned char> slot(8120,0x44);
    for(unsigned i=0;i<256;++i)cache.remember(i*8120u,8120,slot.data(),RangeShadowBudgetBytes-cache.capacity());
    constexpr unsigned scattered=40u<<20;   // 64 slots 2 apart: no joins, more ranges than the old 8
    for(unsigned i=0;i<64;++i)cache.remember(scattered+i*2*8120u,8120,slot.data(),RangeShadowBudgetBytes-cache.capacity());
    for(unsigned i=0;i<64;++i)CHECK(cache.canRead(scattered+i*2*8120u,8120));
    for(unsigned i=0;i<256;i+=7)CHECK(cache.canRead(i*8120u,8120));
    for(unsigned i=0;i<256;i+=7){slot[0]=std::uint8_t(i);cache.remember(i*8120u,8120,slot.data(),RangeShadowBudgetBytes-cache.capacity());}
    for(unsigned i=0;i<256;i+=7){cache.read(result,i*8120u,64);CHECK(result[0]==std::uint8_t(i)&&result[1]==0x44);}
    CHECK(cache.capacity()<=RangeShadowItemBytes);cache.clear();
    // Fresh slots: never-written bytes are undefined even after the cache drops everything, so the next
    // slot past the run stages zeros. Written-and-dropped slots, or a slot touching one, stay unreadable.
    CHECK(!cache.canRead(0,8120)&&!cache.canRead(255*8120u,16240)&&!cache.canRead(scattered,8120));
    CHECK(cache.canRead(256*8120u,8120)&&cache.canRead(scattered-8120u,8120));
    cache.read(result,256*8120u,64);for(unsigned i=0;i<64;++i)CHECK(result[i]==0);
    // More disjoint writes than tracked intervals widen one: never a false proof.
    cache.discard();CHECK(cache.canRead(0,8120));
    for(unsigned i=0;i<16;++i)cache.remember(i*(1u<<20),64,source,0);
    for(unsigned i=0;i<16;++i)CHECK(!cache.canRead(i*(1u<<20),64));
    CHECK(cache.canRead(16u<<20,64));cache.clear();
    // Two buffers share the existing global ceiling. A third cannot allocate
    // proof; replacing a span under pressure must never exceed either ceiling.
    BufferRangeShadow second,third;std::vector<unsigned char> large(1u<<20,0x5a);
    for(unsigned i=0;i<4;++i){
        cache.remember(i*(5u<<20),large.size(),large.data(),RangeShadowBudgetBytes-cache.capacity()-second.capacity());
        second.remember(i*(5u<<20),large.size(),large.data(),RangeShadowBudgetBytes-cache.capacity()-second.capacity());
    }
    CHECK(cache.capacity()==RangeShadowItemBytes&&second.capacity()==RangeShadowItemBytes);
    third.remember(0,64,source,0);CHECK(!third.canRead(0,64)&&third.capacity()==0);
    cache.remember(4*(5u<<20),large.size(),large.data(),0);
    CHECK(cache.covers(4*(5u<<20),large.size())&&cache.capacity()+second.capacity()<=RangeShadowBudgetBytes);
    CHECK(!cache.canRead(0,64));
    std::puts("SPARSE_PROOFS_PASS");
}

static void scatteredSlotRewrites(){
    gTrace.clear();Rig rig(true);auto& stats=rig.core().q.stats;
    constexpr UINT length=53u<<20,slots=400,spacing=128u<<10,size=8120;
    IDirect3DVertexBuffer9* vb=nullptr;void* data=nullptr;
    CHECK(rig.dev->CreateVertexBuffer(length,D3::kUsageDynamic,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK);
    for(UINT slot=0;slot<slots;++slot){
        CHECK(vb->Lock(slot*spacing,size,&data,D3::kLockNoOverwrite)==D3D_OK);
        std::memset(data,slot%199+1,size);CHECK(vb->Unlock()==D3D_OK);
    }
    rig.sync();const auto before=get(stats.syncCalls);
    for(UINT slot=0;slot<slots;++slot){
        CHECK(vb->Lock(slot*spacing,size,&data,D3::kLockNoOverwrite)==D3D_OK);
        auto* bytes=static_cast<unsigned char*>(data);
        CHECK(bytes[0]==slot%199+1&&bytes[size-1]==slot%199+1);
        bytes[0]=0xfa;CHECK(vb->Unlock()==D3D_OK);
    }
    CHECK(get(stats.syncCalls)==before&&stats.rangeShadowBytes.load()==slots*8192);
    for(UINT slot:{0u,255u,256u,399u}){
        CHECK(vb->Lock(slot*spacing,size,&data,D3::kLockReadOnly)==D3D_OK);
        const auto* bytes=static_cast<const unsigned char*>(data);
        CHECK(bytes[0]==0xfa&&bytes[size-1]==slot%199+1);CHECK(vb->Unlock()==D3D_OK);
    }
    CHECK(vb->Lock(0,size,&data,D3::kLockDiscard)==D3D_OK);std::memset(data,0x72,size);CHECK(vb->Unlock()==D3D_OK);
    CHECK(stats.rangeShadowBytes.load()==8192);
    vb->Release();rig.sync();CHECK(stats.rangeShadowBytes.load()==0);rig.finish();checkClean();gTrace.clear();
    std::puts("SCATTERED_SLOT_REWRITES_PASS slots=400 syncs=0 retainedBytes=3276800");
}
#endif

int main(int argc,char** argv){
#if NORTHLIGHT_EXPECT_SPARSE
    if(argc>1&&std::strcmp(argv[1],"full")==0)return command_stream_original_main(1,argv);
    if(argc>1){bufferRangeShadowModel();oversizedBufferRangeShadows();oversizedBufferPumpedWrite();sparseProofs();scatteredSlotRewrites();return 0;}
#endif
    sparseWrites();return 0;
}
