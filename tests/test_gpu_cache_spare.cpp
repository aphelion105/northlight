// Pressure must reuse spare peer quota without reuploading or evicting peer data.
#define main previous_gpu_spare_main
#include "test_shared_gpu_residency.cpp"
#undef main
#include "scene_native_store.h"
#ifdef NORTHLIGHT_GPU_SPARE_REFERENCE
#define NorthlightGpuScene NorthlightGpuSceneReference
#include NORTHLIGHT_GPU_SPARE_REFERENCE
#undef NorthlightGpuScene
#endif

enum class Space {Spare,Unshared,Zero,Full,OneShort};
struct SpareResult {unsigned resident=0,returnHits=0,reloads=0;size_t fallback=0,upload=0,transferred=0;};
template<class Store,class Client,class Consumer>
static SpareResult pressure(unsigned lane,bool batched,Space space,bool keep){
 Store scene;Client source(scene,Consumer(lane)),peer(scene,Consumer(1-lane));Device device;
 const unsigned count=batched?16:1;std::vector<Owner> old;
 std::vector<std::unique_ptr<Binding>> held;Binding incomingBinding,peerBinding,borrowed;
 auto incoming=authored(650000,!batched),unrelated=authored(650001,!batched);
 scene.setFrame(1);
 for(unsigned i=0;i<count;++i){old.push_back(authored(640000+i,!batched));held.push_back(std::make_unique<Binding>());auto& b=*held.back();assert(!source.bind(&device,old.back(),b.vb,b.ib,admit,b.at));}
 assert(!source.bind(&device,incoming,incomingBinding.vb,incomingBinding.ib,admit,incomingBinding.at));
 assert(!peer.bind(&device,unrelated,peerBinding.vb,peerBinding.ib,admit,peerBinding.at));
 scene.setFrame(2);
 for(unsigned i=0;i<count;++i){auto& b=*held[i];source.bind(&device,old[i],b.vb,b.ib,admit,b.at);}source.commitPending();
 for(unsigned i=0;i<count;++i){auto& b=*held[i];assert(source.bind(&device,old[i],b.vb,b.ib,admit,b.at)&&exact(*old[i],b));}
 peer.bind(&device,unrelated,peerBinding.vb,peerBinding.ib,admit,peerBinding.at);peer.commitPending();
 assert(peer.bind(&device,unrelated,peerBinding.vb,peerBinding.ib,admit,peerBinding.at)&&exact(*unrelated,peerBinding));
 // Only one member needs a peer user: ownership of an immutable batch is atomic.
 if(space!=Space::Unshared)assert(peer.bind(&device,old[count/2],borrowed.vb,borrowed.ib,admit,borrowed.at));
 const size_t metadata=(source.chargedBytes()-source.bytes())/(count+1),transfer=source.chargedBytes()-metadata,peerBytes=peer.chargedBytes();
 source.setLimit(source.chargedBytes());
 peer.setLimit(space==Space::Zero?0:space==Space::Full?peerBytes:peerBytes+transfer-(space==Space::OneShort));
 const auto creates=device.calls,locks=freshLocks;
 scene.setFrame(3);source.bind(&device,incoming,incomingBinding.vb,incomingBinding.ib,admit,incomingBinding.at);source.commitPending();
 assert(source.bind(&device,incoming,incomingBinding.vb,incomingBinding.ib,admit,incomingBinding.at)&&exact(*incoming,incomingBinding));
 assert(device.calls==creates+(batched?2:3)&&freshLocks==locks+(batched?2:3));
 if constexpr(std::is_same_v<Store,NorthlightGpuScene::NativeStore>){
  assert(source.counters().loans==(keep?1u:0u)&&source.counters().loanedBytes==(keep?transfer:0));
 }
 SpareResult result;result.transferred=keep?transfer:0;
 for(unsigned i=0;i<count;++i){result.resident+=scene.resident(old[i]);assert(exact(*old[i],*held[i]));}
 assert(result.resident==(keep?count:0));
 if(space!=Space::Zero){assert(scene.resident(unrelated)&&!peer.stats().evictions);assert(peer.bind(&device,unrelated,peerBinding.vb,peerBinding.ib,admit,peerBinding.at)&&exact(*unrelated,peerBinding));}
 assert(source.chargedBytes()<=source.limit()&&peer.chargedBytes()<=peer.limit());
 // Camera return: misses use the original geometry, then rebuild next frame.
 scene.setFrame(4);const auto beforeReturn=device.calls;
 for(auto& mesh:old){Binding b;if(peer.bind(&device,mesh,b.vb,b.ib,admit,b.at)){++result.returnHits;assert(exact(*mesh,b));}else result.fallback+=mesh->byteSize();}
 assert(result.returnHits==(keep?count:0));
 scene.setFrame(5);
 for(auto& mesh:old){Binding b;peer.bind(&device,mesh,b.vb,b.ib,admit,b.at);}peer.commitPending();
 result.reloads=device.calls-beforeReturn;result.upload=peer.uploaded();
 if(space==Space::Spare){for(auto& mesh:old){Binding b;assert(peer.bind(&device,mesh,b.vb,b.ib,admit,b.at)&&exact(*mesh,b));}assert(result.reloads==(keep?0u:(batched?2u:3u)));}
 assert(source.chargedBytes()<=source.limit()&&peer.chargedBytes()<=peer.limit());
 return result;
}
static void entryCount(){
 using namespace NorthlightGpuScene;
 for(unsigned lane=0;lane<2;++lane){
  NativeStore scene;NativeClient source(scene,Consumer(lane)),peer(scene,Consumer(1-lane));Device device;Binding held,borrowed,scratch;
  auto resident=authored(660000);scene.setFrame(1);source.bind(&device,resident,held.vb,held.ib,admit,held.at);
  scene.setFrame(2);source.bind(&device,resident,held.vb,held.ib,admit,held.at);source.commitPending();
  assert(source.bind(&device,resident,held.vb,held.ib,admit,held.at)&&peer.bind(&device,resident,borrowed.vb,borrowed.ib,admit,borrowed.at));
  scene.setFrame(3);
  for(size_t i=0;i<NativeStore::entryLimit();++i){
   auto mesh=std::make_shared<Mesh>();mesh->streams[0].stride=48;mesh->streams[0].bytes.resize(144);mesh->sourceIdentity=NorthlightSourceIdentity::make({670000,i});
   assert(!source.bind(&device,mesh,scratch.vb,scratch.ib,admit,scratch.at));
  }
  // All probation is current-frame protected. Count pressure must really erase
  // the old resident, even though the peer could hold its bytes.
  assert(source.population().entries==NativeStore::entryLimit()&&!scene.resident(resident)&&source.stats().residentEvictions==1&&!source.counters().loans);
  assert(exact(*resident,held)&&exact(*resident,borrowed));
 }
 assert(!alive);std::puts("GPU_SPARE_ENTRY_COUNT_PASS both_consumers real_erase current_probation held_views no_capacity_pingpong");
}
static void aggregateBudget(){
 using namespace NorthlightGpuScene;
 for(unsigned lane=0;lane<2;++lane)for(bool transition:{false,true}){
  NativeStore scene;NativeClient a(scene,Consumer(lane)),b(scene,Consumer(1-lane));Device device;Binding held,shared,scratch;
  std::vector<Owner> meshes;for(unsigned i=0;i<8;++i)meshes.push_back(authored(680000+i,true));
  auto big=std::const_pointer_cast<Mesh>(authored(685000,true));big->streams[0].bytes.resize(big->streams[0].bytes.size()*2);
  scene.setFrame(1);b.bind(&device,big,scratch.vb,scratch.ib,admit,scratch.at);for(unsigned i=0;i<8;++i){auto& c=i<4?a:b;assert(!c.bind(&device,meshes[i],scratch.vb,scratch.ib,admit,scratch.at));}
  const auto metadata=scene.chargedBytes(),cap=metadata+4*meshes[0]->byteSize();scene.setTotalLimit(cap);
  scene.setFrame(2);
  for(unsigned i:{0u,1u,2u})assert(a.bind(&device,meshes[i],held.vb,held.ib,admit,held.at));
  assert(b.bind(&device,meshes[4],scratch.vb,scratch.ib,admit,scratch.at));
  assert(scene.chargedBytes()==cap&&a.bytes()==3*meshes[0]->byteSize()&&b.bytes()==meshes[4]->byteSize());
  // Aggregate pressure can evict another owner's oldest entry, while a source
  // drawn in this frame remains protected. No accounting-only loan helps here.
  if(transition)b.setLimit(b.chargedBytes()+big->byteSize()-meshes[0]->byteSize()/2);
  scene.setFrame(3);assert(a.bind(&device,meshes[0],held.vb,held.ib,admit,held.at));
  const Owner incoming=transition?Owner(big):meshes[5];assert(b.bind(&device,incoming,scratch.vb,scratch.ib,admit,scratch.at));
  assert(scene.resident(meshes[0])&&!scene.resident(meshes[1])&&scene.resident(meshes[2])&&scene.resident(meshes[4])==!transition&&scene.resident(incoming));
  assert(!b.counters().loans&&scene.chargedBytes()<=cap);
  for(unsigned frame=4;frame<16;++frame){scene.setFrame(frame);
   for(unsigned n=0;n<8;++n){const unsigned i=(n+frame)%8;auto& c=i<4?a:b;
    if(c.bind(&device,meshes[i],scratch.vb,scratch.ib,admit,scratch.at))assert(exact(*meshes[i],scratch));
    assert(scene.chargedBytes()<=cap&&a.chargedBytes()<=a.limit()&&b.chargedBytes()<=b.limit());}
  }
  scene.setTotalLimit(0);assert(!scene.chargedBytes()&&!scene.bytes()&&!a.population().entries&&!b.population().entries&&exact(*meshes[0],held));
  scene.setTotalLimit(SIZE_MAX);b.setLimit(32u<<20);scene.setFrame(30);for(unsigned i=0;i<8;++i){auto& c=i<4?a:b;c.bind(&device,meshes[i],scratch.vb,scratch.ib,admit,scratch.at);}
  scene.setFrame(31);for(unsigned i=0;i<8;++i){auto& c=i<4?a:b;assert(c.bind(&device,meshes[i],scratch.vb,scratch.ib,admit,scratch.at)&&exact(*meshes[i],scratch));}
  scene.clearAll();scene.setFrame(40);auto mesh=authored(690000);assert(!a.bind(&device,mesh,held.vb,held.ib,admit,held.at));
  scene.setFrame(41);a.bind(&device,mesh,held.vb,held.ib,admit,held.at);assert(a.commitPending()&&a.bind(&device,mesh,held.vb,held.ib,admit,held.at));
  assert(b.bind(&device,mesh,shared.vb,shared.ib,admit,shared.at));const auto before=scene.chargedBytes();scene.setTotalLimit(before);a.clear();
  assert(!a.chargedBytes()&&b.chargedBytes()==before&&scene.resident(mesh)&&exact(*mesh,held)&&exact(*mesh,shared));
  scene.clearAll();scene.setTotalLimit(1);scene.setFrame(50);assert(!a.bind(&device,mesh,scratch.vb,scratch.ib,admit,scratch.at)&&!scene.chargedBytes());
 }
 assert(!alive);std::puts("GPU_AGGREGATE_BUDGET_PASS both_consumers cross_owner_eviction current_protection bounded_metadata_reservations clear_zero recovery no_double_charge exact_bytes");
}
int main(){
 NorthlightReplayGPU::createBudgetMs()=1e6;
 for(unsigned lane=0;lane<2;++lane)for(bool batched:{false,true}){
  for(auto space:{Space::Spare,Space::Unshared,Space::Zero,Space::Full,Space::OneShort}){
   auto r=pressure<NorthlightGpuScene::NativeStore,NorthlightGpuScene::NativeClient,NorthlightGpuScene::Consumer>(lane,batched,space,space==Space::Spare);
   assert(space!=Space::Spare||(!r.fallback&&!r.reloads&&!r.upload&&r.resident==(batched?16u:1u)));
   assert(!alive&&!overlaps&&!plainLocks);
   if(space==Space::Spare){
#ifdef NORTHLIGHT_GPU_SPARE_REFERENCE
    auto old=pressure<NorthlightGpuSceneReference::NativeStore,NorthlightGpuSceneReference::NativeClient,NorthlightGpuSceneReference::Consumer>(lane,batched,space,false);
    assert(!alive&&old.fallback&&old.upload&&old.reloads&&!r.fallback&&!r.upload&&!r.reloads);
    std::printf("GPU_SPARE_COMPARE_PASS lane=%u batched=%u resident=%u/%u returnHits=%u/%u fallbackBytes=%zu/%zu reloadBytes=%zu/%zu reloadBuffers=%u/%u transferred=%zu exact_bytes=1\n",lane,batched,old.resident,r.resident,old.returnHits,r.returnHits,old.fallback,r.fallback,old.upload,r.upload,old.reloads,r.reloads,r.transferred);
#endif
   }
  }
 }
 entryCount();aggregateBudget();
 std::puts("GPU_SPARE_CONTROLS_PASS both_consumers batch_separate exact_capacity one_byte_short unshared zero_full_peer no_peer_eviction held_views original_geometry exact_bytes");
 return 0;
}
