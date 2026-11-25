#pragma once
#include "replay_gpu_binding.h"
#include "replay_gpu_policy.h"
#include "lock_meter.h"
#include "intrusive_lru.h"
#include <array>
#include <cassert>
#include <tuple>

namespace NorthlightGpuScene {
using Mesh=NorthlightDrawSnapshot::Mesh;
using Owner=std::shared_ptr<const Mesh>;
using Placement=NorthlightReplayGPU::Placement;
enum class Consumer:unsigned {Material=0,Shadow=1};
class NativeStore {
public:
 static constexpr size_t MaterialLimit=32u<<20,ShadowLimit=64u<<20;
 using Stats=NorthlightReplayGPU::Stats;
 using Population=NorthlightReplayGPU::Population;
 using ClearStats=NorthlightReplayGPU::ClearStats;
 using BatchStats=NorthlightReplayGPU::BatchStats;
 struct Counters {size_t uploaded=0,reused=0,loanedBytes=0;unsigned hits=0,crossOwner=0,reconstructed=0,loans=0;};
private:
 struct Entry;
 struct Batch {
  IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
  Entry* first=nullptr;
  size_t capacity=0,liveBytes=0,slot=0;unsigned live=0,owner=0;
  ~Batch(){if(vb)vb->Release();if(ib)ib->Release();}
 };
 struct Entry {
  std::weak_ptr<const Mesh> source;NorthlightSourceIdentity::Handle identity;const Mesh* sourceAddress=nullptr;
  std::uint64_t hash=0,first=0,touched=0;unsigned owner=0,users=0;
  size_t metadata=0,bytes=0,reserved=0;Batch* batch=nullptr;bool pending=false,sourceIndexed=false;
  UINT sourceVertices=0; // Decoded count for leases obtained without the CPU mesh.
  IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;Placement at;
  Entry* previous=nullptr;Entry* next=nullptr;Entry* batchNext=nullptr;
  Entry* untrackedPrevious=nullptr;Entry* untrackedNext=nullptr;
  NorthlightIntrusiveLRU::Links<Entry> lru; // Nonresident entries, in the same order as the full LRU.
  Entry()=default;Entry(const Entry&)=delete;Entry& operator=(const Entry&)=delete;
  ~Entry(){for(auto p:vb)if(p)p->Release();if(ib)ib->Release();}
 };
 struct Prehashed {
  size_t operator()(std::uint64_t value)const noexcept{
   if constexpr(sizeof(size_t)<sizeof(value))return size_t(value)^size_t(value>>32);
   else return size_t(value);
  }
 };
 // Node references survive rehash: LRU, pending and batch links keep stable
 // addresses without a second allocation for each entry.
 using Table=std::unordered_multimap<std::uint64_t,Entry,Prehashed>;
 Table entries_;std::vector<std::unique_ptr<Batch>> batches_;
 NorthlightIntrusiveLRU::List<Entry> probation_;
 std::array<std::vector<Entry*>,2> pending_;
 Entry* oldest_=nullptr;Entry* newest_=nullptr;IDirect3DDevice9* device_=nullptr;UINT maxVertexIndex_=0;
 Entry** removalCursor_=nullptr; // Stack-owned during eviction/trim; removals skip erased siblings.
 struct OwnerScan {Entry* next=nullptr;bool started=false;};
 OwnerScan ownerScans_[2];
 std::uint64_t frame_=0;bool explicitClock_=false,orderedFrames_=true;Entry* untrackedFirst_=nullptr;
 const size_t materialLimit_;
 size_t limit_[2]={MaterialLimit,ShadowLimit},gpu_[2]={},metadata_[2]={},reserved_[2]={},attempted_[2]={};
 size_t retain_[2]={MaterialLimit,ShadowLimit};size_t totalLimit_;
 bool denseNormalCapture_=false,memoryEpisode_=false;
 unsigned attempts_[2]={},groups_[2]={};bool committed_[2]={},ignoreTime_[2]={},timeDeferred_[2]={};
 Stats stats_[2];Counters counters_[2];ClearStats clears_[2];BatchStats batchStats_[2];
 static constexpr size_t EntryLimit=8192,UploadLimit=4u<<20;
 static constexpr unsigned GroupLimit=16,BatchMemberLimit=16,AttemptLimit=GroupLimit*BatchMemberLimit;
 static unsigned index(Consumer c){return unsigned(c);}
 struct RemovalScan {
  NativeStore& store;Entry* next;Entry** previous;
  explicit RemovalScan(NativeStore& owner):store(owner),next(owner.oldest_),previous(owner.removalCursor_){store.removalCursor_=&next;}
  ~RemovalScan(){store.removalCursor_=previous;if(previous)*previous=store.oldest_;}
 };
 static const auto& identity(const Mesh& mesh){return mesh.replayIdentity?mesh.replayIdentity:mesh.sourceIdentity;}
 static std::uint64_t hash(const Owner& mesh){const auto& id=identity(*mesh);return id?id->hash:std::uint64_t(reinterpret_cast<std::uintptr_t>(mesh.get()));}
 static bool sameSource(const Entry& entry,const Owner& mesh){
  return entry.sourceAddress==mesh.get()&&!entry.source.owner_before(mesh)&&!mesh.owner_before(entry.source);
 }
 auto find(const Owner& mesh){
  auto range=entries_.equal_range(hash(mesh));const auto& id=identity(*mesh);
  for(auto it=range.first;it!=range.second;++it){const auto& e=it->second;
   if(id?NorthlightSourceIdentity::same(id,e.identity):!e.identity&&(sameSource(e,mesh)||e.source.lock()==mesh))return it;
  }return entries_.end();
 }
 void unlink(Entry& e){
  for(auto& scan:ownerScans_)if(scan.next==&e)scan.next=e.next;
  if(e.previous)e.previous->next=e.next;else oldest_=e.next;if(e.next)e.next->previous=e.previous;else newest_=e.previous;e.previous=e.next=nullptr;
 }
 void append(Entry& e){e.previous=newest_;e.next=nullptr;if(newest_)newest_->next=&e;else oldest_=&e;newest_=&e;}
 void touch(Entry& e){if(e.touched==frame_)return;e.touched=frame_;if(!e.bytes)probation_.touch(e);if(newest_!=&e){unlink(e);append(e);++stats_[e.owner].lruMoves;}}
 // Within an ordered frame, new or moved entries are protected at the LRU tail.
 // Reuse the already searched prefix on repeated admissions for this owner.
 Entry* oldestEvictableOwner(unsigned lane){
  auto& scan=ownerScans_[lane];
  if(!scan.started){scan.next=oldest_;scan.started=true;}
  while(scan.next){
   auto* e=scan.next;
   ++stats_[lane].ownerVictimVisits;
   if(e->touched==frame_){scan.next=nullptr;break;}
   if(e->owner==lane)return e;
   scan.next=e->next;
  }
  return nullptr;
 }
 void changeOwner(Entry& e,unsigned lane){
  if(e.touched!=frame_)ownerScans_[lane]={};
  e.owner=lane;
 }
 size_t charged(unsigned lane)const{return gpu_[lane]+metadata_[lane]+reserved_[lane];}
 void dropBatch(Batch* batch){
  const auto slot=batch->slot;assert(slot<batches_.size()&&batches_[slot].get()==batch);gpu_[batch->owner]-=batch->capacity;
  auto retired=std::move(batches_[slot]);
  // This array owns buffers; LRU and draw order live in the entries instead.
  if(slot+1<batches_.size()){batches_[slot]=std::move(batches_.back());batches_[slot]->slot=slot;}
  batches_.pop_back();
 }
 void release(Entry& e){
  if(e.pending){auto& list=pending_[e.owner];list.erase(std::find(list.begin(),list.end(),&e));reserved_[e.owner]-=e.reserved;e.reserved=0;e.pending=false;}
  if(e.batch){auto* b=e.batch;auto** link=&b->first;
   while(*link!=&e){assert(*link);link=&(*link)->batchNext;}*link=e.batchNext;e.batchNext=nullptr;
   b->liveBytes-=e.bytes;e.batch=nullptr;if(!--b->live)dropBatch(b);}
  else if(e.bytes){gpu_[e.owner]-=e.bytes;for(auto& p:e.vb){if(p)p->Release();p=nullptr;}if(e.ib)e.ib->Release();e.ib=nullptr;}
  e.bytes=0;e.at={};
 }
 void erase(Entry& e){
  if(removalCursor_&&*removalCursor_==&e)*removalCursor_=e.next;
  if(!e.identity){
   if(e.untrackedPrevious)e.untrackedPrevious->untrackedNext=e.untrackedNext;else untrackedFirst_=e.untrackedNext;
   if(e.untrackedNext)e.untrackedNext->untrackedPrevious=e.untrackedPrevious;
  }
  if(!e.bytes)probation_.remove(e);
  release(e);metadata_[e.owner]-=e.metadata;unlink(e);auto range=entries_.equal_range(e.hash);
  for(auto it=range.first;it!=range.second;++it)if(&it->second==&e){entries_.erase(it);return;}assert(false);
 }
 void evict(Entry& e,unsigned cause,bool current=false){
  auto* batch=e.batch;const unsigned siblings=batch?batch->live-1:0;
  ++stats_[cause].evictions;(e.bytes?stats_[cause].residentEvictions:stats_[cause].probationEvictions)++;
  erase(e);if(!siblings)return;
  // Batches contain at most BatchMemberLimit meshes. Never search the whole scene
  // for siblings; save next before erase may release the last buffer owner.
  for(auto* other=batch->first;other;){auto* next=other->batchNext;
   if(current||other->touched!=frame_){++stats_[cause].evictions;++stats_[cause].residentEvictions;erase(*other);}other=next;}
 }
 bool room(unsigned lane,size_t extra,bool promotion,bool current=false,size_t ceiling=SIZE_MAX,size_t totalExtra=SIZE_MAX){
  if(totalExtra==SIZE_MAX)totalExtra=extra;
  if(totalExtra>totalLimit_){++stats_[lane].roomRejected;return false;}
  ceiling=std::min(ceiling,limit_[lane]);
  const bool count=!promotion&&entries_.size()>=EntryLimit;const bool bytes=charged(lane)+extra>ceiling||charged(0)+charged(1)+totalExtra>totalLimit_;
  stats_[lane].countPressure+=count;stats_[lane].bytePressure+=bytes;
  if(!count&&!bytes)return true;
  // With an ordered frame clock, current-frame touches form an LRU suffix.
  // If even the oldest entry is protected, no victim exists anywhere.
  if(!current&&orderedFrames_&&oldest_&&oldest_->touched==frame_){++stats_[lane].evictionChecks;++stats_[lane].roomRejected;return false;}
  RemovalScan scan(*this);bool ownerScan=charged(lane)+extra>ceiling;
  while(charged(lane)+extra>ceiling||charged(0)+charged(1)+totalExtra>totalLimit_||(!promotion&&entries_.size()>=EntryLimit)){
   ++stats_[lane].evictionChecks;Entry* victim=nullptr;
   if(charged(lane)+extra>ceiling||charged(0)+charged(1)+totalExtra>totalLimit_){
    const bool ownerPressure=charged(lane)+extra>ceiling;
    if(ownerPressure!=ownerScan){scan.next=oldest_;ownerScan=ownerPressure;}
    // Revisit other owners when local pressure gives way to aggregate pressure.
    // Keep the original first eligible LRU victim.
    if(ownerPressure&&!current&&orderedFrames_)victim=oldestEvictableOwner(lane);
    else while(scan.next){auto* e=scan.next;scan.next=e->next;
     if(ownerPressure)++stats_[lane].ownerVictimVisits;
     if((current||e->touched!=frame_)&&(!ownerPressure||e->owner==lane)){victim=e;break;}}
   }else{
    // Skip resident GPU meshes when looking for metadata-only victims. Pending
    // uploads stay in this ordered subset until commit succeeds, so a failed
    // upload keeps its original eviction position without rebuilding a list.
    for(auto* e=probation_.oldest();e;e=e->lru.newer){
     if(!current&&e->touched==frame_){if(orderedFrames_)break;continue;}
     if(!e->pending){victim=e;break;}}
    if(!victim)for(auto* e=oldest_;e;e=e->next)if(current||e->touched!=frame_){victim=e;break;}
   }
   if(!victim){++stats_[lane].roomRejected;return false;}
   // Keep shared immutable data when the other pass has unused capacity.
   // Count pressure must erase an entry; explicit trims keep their one-way
   // policy and must never initiate a reciprocal preservation.
   if(!current&&victim->bytes&&charged(0)+charged(1)+totalExtra<=totalLimit_&&(promotion||entries_.size()<EntryLimit)){
    const auto before=charged(lane);
    if(preserve(*victim,1-lane,true)){++counters_[lane].loans;counters_[lane].loanedBytes+=before-charged(lane);continue;}
   }
   evict(*victim,lane,current);
  }return true;
 }
 bool transferMetadata(Entry& e,unsigned lane){
  if(e.owner==lane)return true;if(!room(lane,e.metadata,true,false,SIZE_MAX,0))return false;
  metadata_[e.owner]-=e.metadata;metadata_[lane]+=e.metadata;changeOwner(e,lane);return true;
 }
 template<class Buffer>static bool write(Buffer* b,UINT offset,const void* data,size_t bytes){
  void* out=nullptr;if(FAILED(b->Lock(offset,UINT(bytes),&out,NorthlightUpload::FreshBufferLock)))return false;
  if(out)std::memcpy(out,data,bytes);NorthlightLockMeter::staging(bytes);const auto hr=b->Unlock();return out&&SUCCEEDED(hr);
 }
 static bool batchable(const Mesh& m){return m.streams[0].stride&&!m.streams[0].bytes.empty()&&m.streams[0].bytes.size()%m.streams[0].stride==0&&m.streams[1].bytes.empty()&&m.streams[2].bytes.empty()&&m.streams[3].bytes.empty();}
 bool separate(Entry& e,const Mesh& m){
  Entry built;
  for(unsigned s=0;s<4;++s){auto& bytes=m.streams[s].bytes;if(bytes.empty())continue;
   if(FAILED(device_->CreateVertexBuffer(UINT(bytes.size()),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&built.vb[s],nullptr))||!built.vb[s]||!write(built.vb[s],0,bytes.data(),bytes.size()))return false;}
  if(!m.indices.empty()){const UINT size=UINT(m.indices.size()*4);if(FAILED(device_->CreateIndexBuffer(size,D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&built.ib,nullptr))||!built.ib||!write(built.ib,0,m.indices.data(),size))return false;}
  for(unsigned s=0;s<4;++s)std::swap(e.vb[s],built.vb[s]);std::swap(e.ib,built.ib);probation_.remove(e);e.bytes=m.byteSize();gpu_[e.owner]+=e.bytes;++batchStats_[e.owner].separateUploads;return true;
 }
 void commitPrepared(unsigned lane){
  auto& list=pending_[lane];if(list.empty())return;
  assert(list.size()<=BatchMemberLimit);std::array<Entry*,BatchMemberLimit> members;std::array<Owner,BatchMemberLimit> meshes;size_t memberCount=0,vertices=0,indices=0;
  try{if(batches_.size()==batches_.capacity())batches_.reserve(std::max(size_t(BatchMemberLimit),batches_.size()*2));
   for(auto* e:list){auto m=e->source.lock();if(!m)continue;auto stride=m->streams[0].stride;
    e->at.vertexBase=UINT((vertices+stride-1)/stride);vertices=size_t(e->at.vertexBase)*stride+m->streams[0].bytes.size();
    e->at.indexStart=UINT(indices/4);indices+=m->indices.size()*4;members[memberCount]=e;meshes[memberCount++]=std::move(m);}
  }catch(...){while(!list.empty())release(*list.back());return;}
  // Ending reservations must preserve the layout just calculated.
  for(auto* e:list){reserved_[lane]-=e->reserved;e->reserved=0;e->pending=false;}list.clear();if(!memberCount)return;
  struct Timer {double& target;std::chrono::steady_clock::time_point begin=std::chrono::steady_clock::now();~Timer(){target+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();}} timer{stats_[lane].createMs};
  // Select width after rebasing; every encoded index must fit exactly. Keep
  // the conservative 32-bit admission reservation until this physical upload.
  bool narrow=true;
  for(size_t i=0;i<memberCount;++i){const auto& mesh=*meshes[i];auto& at=members[i]->at;uint32_t highest=0;
   for(auto value:mesh.indices)highest=std::max(highest,value);
   at.absoluteIndices=mesh.indexed&&mesh.vertexCount&&mesh.vertexCount<=mesh.streams[0].bytes.size()/mesh.streams[0].stride&&
       at.vertexBase<=maxVertexIndex_&&mesh.vertexCount-1<=maxVertexIndex_-at.vertexBase&&highest<mesh.vertexCount;
   narrow&=uint64_t(highest)+(at.absoluteIndices?at.vertexBase:0)<=UINT16_MAX;
  }
  const size_t indexWidth=narrow?2:4;indices=indices/4*indexWidth;
  auto batch=std::make_unique<Batch>();batch->capacity=vertices+indices;batch->owner=lane;
  auto fail=[&]{++batchStats_[lane].batchFailures;for(size_t i=0;i<memberCount;++i)members[i]->at={};};
  if(FAILED(device_->CreateVertexBuffer(UINT(vertices),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&batch->vb,nullptr))||!batch->vb||
    (indices&&(FAILED(device_->CreateIndexBuffer(UINT(indices),D3DUSAGE_WRITEONLY,narrow?D3DFMT_INDEX16:D3DFMT_INDEX32,D3DPOOL_DEFAULT,&batch->ib,nullptr))||!batch->ib))){fail();return;}
  void* out=nullptr;if(FAILED(batch->vb->Lock(0,UINT(vertices),&out,NorthlightUpload::FreshBufferLock))){fail();return;}
  if(out)for(size_t i=0;i<memberCount;++i){const auto& v=meshes[i]->streams[0];std::memcpy(static_cast<uint8_t*>(out)+size_t(members[i]->at.vertexBase)*v.stride,v.bytes.data(),v.bytes.size());}
  NorthlightLockMeter::staging(vertices);auto hr=batch->vb->Unlock();if(!out||FAILED(hr)){fail();return;}
  if(indices){out=nullptr;if(FAILED(batch->ib->Lock(0,UINT(indices),&out,NorthlightUpload::FreshBufferLock))){fail();return;}
   if(out)for(size_t i=0;i<memberCount;++i)if(!meshes[i]->indices.empty()){
    const auto& at=members[i]->at;const auto base=at.absoluteIndices?at.vertexBase:0;const auto& mesh=*meshes[i];
    const auto write=[&](auto* target){for(auto value:mesh.indices)*target++=value+base;};
    if(narrow)write(static_cast<uint16_t*>(out)+at.indexStart);
    else if(at.absoluteIndices)write(static_cast<uint32_t*>(out)+at.indexStart);
    else std::memcpy(static_cast<uint32_t*>(out)+at.indexStart,mesh.indices.data(),mesh.indices.size()*4);
   }
   NorthlightLockMeter::staging(indices);hr=batch->ib->Unlock();if(!out||FAILED(hr)){fail();return;}}
  auto* b=batch.get();b->slot=batches_.size();batches_.push_back(std::move(batch));gpu_[lane]+=b->capacity;committed_[lane]=true;
  for(size_t i=0;i<memberCount;++i){auto& e=*members[i];e.batch=b;e.batchNext=b->first;b->first=&e;probation_.remove(e);e.bytes=meshes[i]->byteSize()-meshes[i]->indices.size()*(4-indexWidth);++b->live;b->liveBytes+=e.bytes;counters_[lane].uploaded+=e.bytes;++stats_[lane].created;++batchStats_[lane].batchedUploads;}
 }
 void commit(unsigned lane)noexcept{
  try{commitPrepared(lane);}catch(...){++batchStats_[lane].batchFailures;while(!pending_[lane].empty())release(*pending_[lane].back());for(auto& item:entries_)if(!item.second.bytes)item.second.at={};}
 }
 void bindReady(const Entry& e,bool indexed,IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9*& ib,Placement& at){
  IDirect3DVertexBuffer9* streams[4]={e.batch?e.batch->vb:e.vb[0],e.vb[1],e.vb[2],e.vb[3]};
  for(unsigned s=0;s<4;++s)if(vb[s]!=streams[s]){if(vb[s])vb[s]->Release();vb[s]=streams[s];if(vb[s])vb[s]->AddRef();}
  auto* index=e.batch?(indexed?e.batch->ib:nullptr):e.ib;
  if(ib!=index){if(ib)ib->Release();ib=index;if(ib)ib->AddRef();}at=e.at;
 }
 bool preserve(Entry& entry,unsigned other,bool spareOnly=false){
  if(!(entry.users&(1u<<other))){
   if(!entry.batch)return false;
   // An unused oldest entry must not evict siblings still used by another pass.
   bool shared=false;for(auto* e=entry.batch->first;e;e=e->batchNext)if(e->users&(1u<<other)){shared=true;break;}
   if(!shared)return false;
  }
  const auto source=entry.owner;
  const auto fits=[&](size_t need){
   if(need>retain_[other])return false;
   if(spareOnly){const auto used=charged(other);return used<=retain_[other]&&need<=retain_[other]-used;}
   return room(other,need,true,true,retain_[other],0);
  };
  if(entry.batch){auto* batch=entry.batch;size_t meta=0;for(auto* e=batch->first;e;e=e->batchNext)meta+=e->metadata;
   if(!fits(batch->capacity+meta))return false;
   gpu_[source]-=batch->capacity;gpu_[other]+=batch->capacity;batch->owner=other;
   for(auto* e=batch->first;e;){auto* next=e->batchNext;
    metadata_[source]-=e->metadata;metadata_[other]+=e->metadata;changeOwner(*e,other);
    if(!e->users)erase(*e);e=next;
   }return true;
  }
  if(entry.pending)release(entry);
  const auto need=entry.bytes+entry.metadata;if(!fits(need))return false;
  gpu_[source]-=entry.bytes;gpu_[other]+=entry.bytes;metadata_[source]-=entry.metadata;metadata_[other]+=entry.metadata;changeOwner(entry,other);return true;
 }
public:
 explicit NativeStore(bool shareMaterialCapacity=false):materialLimit_(shareMaterialCapacity?ShadowLimit:MaterialLimit),
  totalLimit_(shareMaterialCapacity?MaterialLimit+ShadowLimit:SIZE_MAX){}
 NativeStore(const NativeStore&)=delete;NativeStore& operator=(const NativeStore&)=delete;
 ~NativeStore(){clearAll();}
 void advance(std::uint64_t frame){
  if(frame_==frame)return;orderedFrames_=entries_.empty()||(orderedFrames_&&frame>frame_);frame_=frame;for(unsigned i=0;i<2;++i){ownerScans_[i]={};stats_[i]={};counters_[i]={};attempted_[i]=attempts_[i]=groups_[i]=0;timeDeferred_[i]=false;}
  // Only entries without a source certificate expire with their CPU owner.
  for(auto* e=untrackedFirst_;e;){auto* next=e->untrackedNext;if(e->source.expired()){++stats_[e->owner].expiredEntries;stats_[e->owner].expiredBytes+=e->bytes;erase(*e);}e=next;}
  commit(0);commit(1);
 }
 void setFrame(std::uint64_t frame){explicitClock_=true;advance(frame);}
 // One aggregate allowance prevents either consumer being disabled under VA pressure.
 void setTotalLimit(size_t value){totalLimit_=value;room(0,0,true,true);}
 size_t totalLimit()const{return totalLimit_;}
 bool currentFrame(std::uint64_t frame)const{return explicitClock_&&frame_==frame;}
 void boundary(Consumer consumer){if(!explicitClock_)advance(frame_+1);auto lane=index(consumer);stats_[lane]={};counters_[lane]={};}
 std::uint64_t frame()const{return frame_;}
 // Borrow an immutable upload by the current raw draw certificate. Content
 // aliases alone cannot establish that this draw still references those bytes.
 template<class Words>bool bindSource(Consumer consumer,IDirect3DDevice9* device,const Words& words,UINT& vertexCount,
                                      IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9*& ib,Placement& at){
  if(device!=device_)return false;
  auto range=entries_.equal_range(NorthlightSourceIdentity::Key::fingerprint(words));
  for(auto it=range.first;it!=range.second;++it){
   auto& e=it->second;
   if(!e.bytes||!e.sourceVertices||!std::equal(words.begin(),words.end(),e.identity->words.begin(),e.identity->words.end()))continue;
   const auto lane=index(consumer);e.users|=1u<<lane;touch(e);retain_[lane]=limit_[lane];timeDeferred_[lane]=false;
   ++stats_[lane].identityHits;++counters_[lane].hits;counters_[lane].reused+=e.bytes;counters_[lane].crossOwner+=e.owner!=lane;
   vertexCount=e.sourceVertices;bindReady(e,e.sourceIndexed,vb,ib,at);return true;
  }
  return false;
 }
 template<class Admission>bool bind(Consumer consumer,IDirect3DDevice9* device,const Owner& mesh,IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9*& ib,Admission admit,Placement& at,bool residentOnly=false){
  const auto lane=index(consumer);at={};timeDeferred_[lane]=false;if(!mesh||!mesh->byteSize())return false;if(device_&&device_!=device)return false;
  if(!device_){D3DCAPS9 caps{};maxVertexIndex_=SUCCEEDED(device->GetDeviceCaps(&caps))?caps.MaxVertexIndex:0;device_=device;}retain_[lane]=limit_[lane];
  try{auto it=find(mesh);if(residentOnly&&(it==entries_.end()||!it->second.bytes))return false;
   if(it==entries_.end()){
    const auto& id=identity(*mesh);const size_t metadata=sizeof(Entry)+64+(id?id->bytes():0);
    if(metadata>limit_[lane]||!room(lane,metadata,false))return false;
    const auto key=hash(mesh);auto inserted=entries_.emplace(std::piecewise_construct,std::forward_as_tuple(key),std::forward_as_tuple());auto& e=inserted->second;
    e.source=mesh;e.sourceAddress=mesh.get();e.identity=id;e.hash=key;e.first=e.touched=frame_;e.owner=lane;e.users=1u<<lane;e.metadata=metadata;
    if(id&&id==mesh->sourceIdentity&&mesh->indexed==!mesh->indices.empty()&&!mesh->streams[0].bytes.empty()&&
       mesh->streams[1].bytes.empty()&&mesh->streams[2].bytes.empty()&&mesh->streams[3].bytes.empty()){
     e.sourceVertices=mesh->vertexCount;e.sourceIndexed=mesh->indexed;
    }
    if(!e.identity){e.untrackedNext=untrackedFirst_;if(untrackedFirst_)untrackedFirst_->untrackedPrevious=&e;untrackedFirst_=&e;}
    metadata_[lane]+=metadata;append(e);probation_.append(e);++stats_[lane].warmup;return false;
   }
   auto& e=it->second;const bool wasResident=e.bytes!=0;e.users|=1u<<lane;touch(e);
   if(e.identity){++stats_[lane].identityHits;if(!sameSource(e,mesh)){
    if(e.source.lock()!=mesh){++stats_[lane].reconstructedHits;++counters_[lane].reconstructed;}e.source=mesh;e.sourceAddress=mesh.get();}}
   if(e.pending)return false;
   if(!e.bytes){
    const auto bytes=mesh->byteSize();if(e.first==frame_){++stats_[lane].warmup;return false;}
    const bool batched=batchable(*mesh);
    const auto flushFull=[&]{if(batched&&pending_[lane].size()==BatchMemberLimit)commit(lane);};
    const auto allowed=[&]{
     // A shared VB/IB is one allocation group, not sixteen independent
     // creations. Separate layouts still consume one group per mesh.
     if(attempts_[lane]>=AttemptLimit||((!batched||pending_[lane].empty())&&groups_[lane]>=GroupLimit)){++stats_[lane].uploadDeferred;++stats_[lane].attemptDeferred;return false;}
     if(bytes>UploadLimit-attempted_[lane]){++stats_[lane].uploadDeferred;++stats_[lane].uploadByteDeferred;return false;}
     if(NorthlightReplayGPU::useCreateTimeBudget()&&!ignoreTime_[lane]&&groups_[lane]&&stats_[lane].createMs>=NorthlightReplayGPU::createBudgetMs()){++stats_[lane].uploadDeferred;++stats_[lane].timeDeferred;timeDeferred_[lane]=true;return false;}
     return true;
    };
    flushFull();if(!allowed())return false;
    if(!admit(bytes)){++stats_[lane].admissionRejected;return false;}
    // Admission may submit other work; keep the pending-array and byte bounds valid afterwards.
    flushFull();if(!allowed())return false;
    const size_t reserve=bytes+(batched?mesh->streams[0].stride:0);
    if(!transferMetadata(e,lane)||!room(lane,reserve,true))return false;
    if(!allowed())return false;
    ++attempts_[lane];if(!batched||pending_[lane].empty())++groups_[lane];attempted_[lane]+=bytes;
    if(batched){pending_[lane].push_back(&e);e.pending=true;e.reserved=reserve;reserved_[lane]+=reserve;if(!ignoreTime_[lane])return false;commit(lane);if(!e.bytes)return false;}
    else{const auto start=std::chrono::steady_clock::now();bool ready=separate(e,*mesh);stats_[lane].createMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(!ready)return false;counters_[lane].uploaded+=e.bytes;++stats_[lane].created;}
   }
   if(wasResident){++counters_[lane].hits;counters_[lane].reused+=e.bytes;}counters_[lane].crossOwner+=e.owner!=lane;bindReady(e,!mesh->indices.empty(),vb,ib,at);return true;
  }catch(...){return false;}
 }
 bool commitPending(Consumer consumer){const auto lane=index(consumer);commit(lane);return std::exchange(committed_[lane],false);}
 bool resident(const Owner& mesh){if(!mesh)return false;auto it=find(mesh);return it!=entries_.end()&&it->second.bytes;}
 void ignoreTime(Consumer consumer,bool on){ignoreTime_[index(consumer)]=on;}
 bool timeDeferred(Consumer consumer)const{return timeDeferred_[index(consumer)];}
 size_t bytes(Consumer consumer)const{return gpu_[index(consumer)];}
 size_t chargedBytes(Consumer consumer)const{return charged(index(consumer));}
 size_t limit(Consumer consumer)const{return limit_[index(consumer)];}
 size_t defaultLimit(Consumer consumer)const{return consumer==Consumer::Material?(denseNormalCapture_?materialLimit_:MaterialLimit):ShadowLimit;}
 const Counters& counters(Consumer consumer)const{return counters_[index(consumer)];}
 const Stats& stats(Consumer consumer){auto lane=index(consumer);stats_[lane].identityBytes=metadata_[lane];return stats_[lane];}
 const ClearStats& clearStats(Consumer consumer)const{return clears_[index(consumer)];}
 Population population(Consumer consumer)const{Population p;for(const auto& item:entries_)if(item.second.owner==index(consumer)){++p.entries;p.resident+=item.second.bytes!=0;}p.probation=p.entries-p.resident;return p;}
 BatchStats batchStats(Consumer consumer)const{auto result=batchStats_[index(consumer)];result.batches=result.batchBytes=result.liveBytes=0;for(auto& b:batches_)if(b->owner==index(consumer)){++result.batches;result.batchBytes+=b->capacity;result.liveBytes+=b->liveBytes;}result.separateBytes=gpu_[index(consumer)]-result.batchBytes;return result;}
 size_t metadataBytes(Consumer consumer)const{return metadata_[index(consumer)];}
 template<class Visit>void buffers(Visit visit)const{for(const auto& batch:batches_){visit(batch->vb);visit(batch->ib);}for(const auto& item:entries_)if(!item.second.batch){for(auto* vb:item.second.vb)visit(vb);visit(item.second.ib);}}
 size_t bytes()const{return gpu_[0]+gpu_[1];}size_t chargedBytes()const{return charged(0)+charged(1);}
 static constexpr size_t entryLimit(){return EntryLimit;}
 void clearAll(){oldest_=newest_=untrackedFirst_=nullptr;probation_.clear();for(auto& p:pending_)p.clear();entries_.clear();orderedFrames_=true;batches_.clear();device_=nullptr;for(unsigned i=0;i<2;++i){ownerScans_[i]={};gpu_[i]=metadata_[i]=reserved_[i]=0;committed_[i]=false;}}
 void trim(Consumer consumer,size_t bytes){
  const auto lane=index(consumer);retain_[lane]=std::min(limit_[lane],bytes);if(charged(lane)<=bytes)return;
  // Preserve only transfers OUT of this lane. A forward scan therefore keeps
  // the same oldest-first victims without revisiting the other lane's prefix.
  RemovalScan scan(*this);
  while(charged(lane)>bytes){
   while(scan.next&&scan.next->owner!=lane)scan.next=scan.next->next;
   auto* victim=scan.next;if(!victim)break;scan.next=victim->next;
   if(!preserve(*victim,1-lane))evict(*victim,lane,true);
  }
 }
 void setLimit(Consumer consumer,size_t bytes){const auto lane=index(consumer);limit_[lane]=std::min(bytes,defaultLimit(consumer));trim(consumer,limit_[lane]);}
 bool setNormalCaptureDemand(unsigned previousPackets){
  // Use the completed frame for admission. Leave room for changes in coverage
  // and camera position before shrinking a cache that has already warmed.
  const bool dense=materialLimit_>MaterialLimit&&previousPackets>=(denseNormalCapture_?3072u:4096u);
  if(dense!=denseNormalCapture_){
   denseNormalCapture_=dense;
   if(!memoryEpisode_)setLimit(Consumer::Material,defaultLimit(Consumer::Material));
  }
  return dense;
 }
 void setMemoryPressure(bool episode,bool critical){
  memoryEpisode_=episode;
  const size_t normalLimit=materialLimit_>MaterialLimit?MaterialLimit+ShadowLimit:SIZE_MAX;
  setTotalLimit(episode&&critical?MaterialLimit:normalLimit);
  setLimit(Consumer::Material,episode?MaterialLimit:defaultLimit(Consumer::Material));
  setLimit(Consumer::Shadow,episode?MaterialLimit:ShadowLimit);
 }
 void clear(Consumer consumer){const auto lane=index(consumer);++clears_[lane].calls;clears_[lane].entries+=population(consumer).entries;clears_[lane].bytes+=gpu_[lane];for(auto& item:entries_)item.second.users&=~(1u<<lane);trim(consumer,0);committed_[lane]=false;}
};
class NativeClient {
 NativeStore& store_;Consumer consumer_;
public:
 using Stats=NativeStore::Stats;using Population=NativeStore::Population;using ClearStats=NativeStore::ClearStats;
 NativeClient(NativeStore& store,Consumer consumer):store_(store),consumer_(consumer){}
 void clear(){store_.clear(consumer_);}void beginFrame(){store_.boundary(consumer_);}
 void trimTo(size_t bytes){store_.trim(consumer_,bytes);}void setLimit(size_t bytes){store_.setLimit(consumer_,bytes);}
 size_t limit()const{return store_.limit(consumer_);}size_t bytes()const{return store_.bytes(consumer_);}size_t chargedBytes()const{return store_.chargedBytes(consumer_);}size_t queueBytes()const{return 0;}
 size_t defaultLimit()const{return store_.defaultLimit(consumer_);}
 size_t uploaded()const{return store_.counters(consumer_).uploaded;}size_t reused()const{return store_.counters(consumer_).reused;}unsigned hits()const{return store_.counters(consumer_).hits;}
 const Stats& stats()const{return store_.stats(consumer_);}const ClearStats& clearStats()const{return store_.clearStats(consumer_);}Population population()const{return store_.population(consumer_);}
 static constexpr size_t entryLimit(){return NativeStore::entryLimit();}bool timeDeferredLast()const{return store_.timeDeferred(consumer_);}void ignoreTimeBudget(bool on){store_.ignoreTime(consumer_,on);}
 const auto& counters()const{return store_.counters(consumer_);}
 auto batchStats()const{return store_.batchStats(consumer_);}bool commitPending(){return store_.commitPending(consumer_);}bool resident(const Owner& mesh){return store_.resident(mesh);}
 template<class Words>bool bindSource(IDirect3DDevice9* device,const Words& words,UINT& vertices,IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9*& ib,Placement& at){return store_.bindSource(consumer_,device,words,vertices,vb,ib,at);}
 template<class Admission>bool bind(IDirect3DDevice9* device,const Owner& mesh,IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9*& ib,Admission admit,Placement& at,bool residentOnly=false){return store_.bind(consumer_,device,mesh,vb,ib,admit,at,residentOnly);}
};
}

namespace NorthlightReplayGPU {
inline auto batchStats(const NorthlightGpuScene::NativeClient& c){return c.batchStats();}
template<class Replays,class Bind>void commitAdmissions(NorthlightGpuScene::NativeClient& c,Replays& replays,Bind bind){
 if(c.commitPending())for(auto& p:replays)if(!p->gpuCached&&c.resident(p->shared))bind(*p);
}
template<class Replay,class Admission>bool bindResident(NorthlightGpuScene::NativeClient& c,IDirect3DDevice9* d,Replay& p,bool allowCache,Admission admit){
 Placement at;p.gpuCached=allowCache&&c.bind(d,p.shared,p.stream,p.index,admit,at);
 if(p.gpuCached){for(unsigned s=0;s<4;++s){p.offset[s]=0;p.stride[s]=p.mesh().streams[s].stride;}
  p.start=p.indexed?at.indexStart:at.vertexBase;p.base=p.indexed?at.baseVertex():0;p.min=p.indexed?at.minimumVertex():0;}
 else {p.base=0;p.min=0;}return p.gpuCached;
}
}
