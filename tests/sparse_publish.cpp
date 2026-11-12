#undef NDEBUG
#define NORTHLIGHT_STREAM_CHECK_WAIT
#include "command_queue.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
using namespace NorthlightStream;
static void put(Queue& q,uint64_t value){std::memcpy(q.reserve(7,8),&value,8);q.commit();}
static void take(Queue& q,uint64_t value){const auto* h=q.next(false);assert(h&&h->id==7&&h->size==16);uint64_t got;std::memcpy(&got,h+1,8);assert(got==value);q.retire(h);}
template<class F> static void until(F predicate){const auto end=nowNs()+5000000000ull;while(!predicate()){assert(nowNs()<end);std::this_thread::yield();}}
int main(){
 static_assert(BudgetBytes==(16u<<20)&&kMaxBudgetBytes==(32u<<20));
 for(unsigned n:{1u,2u,8u,63u,64u,65u,127u,128u,256u}){
  Queue q;q.trackIdlePublications(true);q.testSparseSleep();
  for(unsigned i=0;i<n;++i)put(q,i);
  const auto point=q.idlePoint();const auto expected=NORTHLIGHT_EXPECT_PARTIAL?1+(n-1)/64*64:n/64*64;
  assert(point.consistent&&point.recorded==n&&point.published==expected&&!point.replayed);
  const auto stats=q.wakePublicationDiagnostics();assert(stats.partialBatches==NORTHLIGHT_EXPECT_PARTIAL&&stats.partialCommands==NORTHLIGHT_EXPECT_PARTIAL);
  assert(q.wakeSignals()==unsigned(NORTHLIGHT_EXPECT_PARTIAL||n>=64));
  q.publish();q.publish();assert(q.wakeSignals()==1);
  for(unsigned i=0;i<n;++i)take(q,i);assert(!q.next(false)&&!q.depth());
 }
 {Queue q;q.trackIdlePublications(true);q.testSparseSleep();auto* data=q.reserve(7,8);uint64_t value=42;std::memcpy(data,&value,8);
  assert(!q.next(false)&&q.idlePoint().published==0);q.commit();
  if(NORTHLIGHT_EXPECT_PARTIAL)assert(q.idlePoint().published==1);else assert(!q.next(false));
  q.publish();take(q,value);}
 {Queue q;q.trackIdlePublications(true);for(unsigned i=0;i<192;++i)put(q,i);assert(q.idlePoint().published==192&&!q.wakeSignals());
  assert(!q.wakePublicationDiagnostics().partialBatches);q.publish();for(unsigned i=0;i<192;++i)take(q,i);}
 if(NORTHLIGHT_EXPECT_PARTIAL){
  Queue q;std::atomic<unsigned> done{0};constexpr unsigned rounds=2048;
  std::thread consumer([&]{for(unsigned i=0;i<rounds;++i){const auto* h=q.next(true);assert(h&&h->id==7);uint64_t value;std::memcpy(&value,h+1,8);assert(value==i);q.retire(h);done.store(i+1);}});
  for(unsigned i=0;i<rounds;++i){until([&]{return q.testSparseSleeping();});put(q,i);until([&]{return done.load()==i+1;});}
  consumer.join();assert(!q.depth()&&q.replayedSeq()==rounds);
 }
 // Descriptive producer cost, not a scheduling or gameplay performance gate.
 constexpr unsigned commands=150000;uint64_t best=~uint64_t(0);
 for(unsigned repeat=0;repeat<5;++repeat){Queue q;const auto begin=nowNs();for(unsigned i=0;i<commands;++i)put(q,i);q.publish();best=std::min(best,nowNs()-begin);for(unsigned i=0;i<commands;++i)take(q,i);}
 std::printf("SPARSE_PUBLISH_PASS partial=%u sparse=1,2,8,63,64,65,127,128,256 handshake=%u busyBatch=64 producerCommands=%u bestProducerNs=%llu caps=unchanged order=exact\n",NORTHLIGHT_EXPECT_PARTIAL,NORTHLIGHT_EXPECT_PARTIAL?2048:0,commands,(unsigned long long)best);
}
