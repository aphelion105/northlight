#undef NDEBUG
#include "snapshot_harness.h"
#include <map>
using namespace NorthlightDrawSnapshot;
static std::map<void*,uint64_t> revisions;
static uint64_t identity(void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));}
static uint64_t version(void* p,bool){return revisions[p];}
int main(){unsigned cases=0;
 for(auto lookup:{Frame::Lookup::Map,Frame::Lookup::Predict,Frame::Lookup::Prefetch}){
  Device d;Frame frame,peer;for(auto* f:{&frame,&peer}){f->setIdentityProvider(identity);f->setVersionProvider(version);f->setLookup(lookup);f->setReplayContentReuse(true);}assert(peer.joinSnapshotPool(frame.sharedSources()));
  for(void* p:{static_cast<void*>(&d.vb[0]),static_cast<void*>(&d.vb[1]),static_cast<void*>(&d.ib)})revisions[p]=1;
  const Draw draw{D3DPT_TRIANGLELIST,-3,7,3,1,1,true};Mesh scratch;std::shared_ptr<const Mesh> first,next;
  assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&first)&&first);const auto bytes=frame.sharedSourceBytes(),read=frame.bytesRead();
  for(unsigned retry=0;retry<100;++retry){frame.clearIndexCache(nullptr,nullptr,true);peer.clearIndexCache(nullptr,nullptr,true);frame.clearFrame();peer.clearFrame();assert(frame.snapshotCacheEntries()==1&&frame.sharedSourceBytes()==bytes&&!frame.indexCacheEntries());assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&next==first&&frame.bytesRead()==read);assert(peer.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&next==first);++cases;}
  const auto oldIdentity=first->sourceIdentity;revisions[&d.vb[0]]++;frame.clearFrame();assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&next!=first&&NorthlightSourceIdentity::same(next->replayIdentity,oldIdentity));
  // A changed revision is re-read. Content reuse requires all actual bytes.
  d.vb[0].bytes[d.offset[0]+4*d.stride[0]+1]^=0x40;revisions[&d.vb[0]]++;frame.clearIndexCache(nullptr,nullptr,true);frame.clearFrame();assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&next->streams[0].bytes!=first->streams[0].bytes&&!NorthlightSourceIdentity::same(next->replayIdentity,oldIdentity));
  d.indices({999,9,8,7});revisions[&d.ib]++;frame.clearFrame();assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&next));assert(next->indices!=first->indices);
  // Ordinary clears and memory limits still revoke peer predictors and owners.
  std::weak_ptr<const Mesh> weak=next;first.reset();next.reset();frame.clearIndexCache();assert(!frame.snapshotCacheEntries()&&!frame.sharedSourceBytes()&&weak.expired());peer.clearFrame();assert(peer.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&next);next.reset();frame.setSnapshotCacheLimit(0);peer.setSnapshotCacheLimit(0);assert(!frame.snapshotCacheEntries());++cases;
 }
 assert(!NorthlightSourceIdentity::cpuLiveObjects.load());std::printf("CAPTURE_RESET_CPU_PASS cases=%u retainedSources=exact unchangedReadBytes=0 newRevision=read changedBytesAndIndices=rejected ordinaryClearAndLimit=revoked lookupModes=3\n",cases);
}
