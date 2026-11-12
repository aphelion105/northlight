#undef NDEBUG
#include "query_gpu_wait.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightStream;
namespace NorthlightStream {
struct QueryGpuWaitTest {
 struct Driver:QueryGpuWait::Interop {
  unsigned flush=0,lock=0,unlock=0,resets=0,submits=0,waits=0,destroys=0,releases=0;bool locked=false,busy=false;int waitResult=0,submitResult=0,resetResult=0;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
  ULONG STDMETHODCALLTYPE AddRef()override{return 1;}ULONG STDMETHODCALLTYPE Release()override{++releases;return 1;}
  void STDMETHODCALLTYPE GetVulkanHandles(void**,void**,void**)override{assert(false);}
  void STDMETHODCALLTYPE GetSubmissionQueue(void**,std::uint32_t*,std::uint32_t*)override{assert(false);}
  void STDMETHODCALLTYPE TransitionTextureLayout(void*,const void*,std::int32_t,std::int32_t)override{assert(false);}
  void STDMETHODCALLTYPE FlushRenderingCommands()override{assert(!locked);++flush;}
  void STDMETHODCALLTYPE LockSubmissionQueue()override{assert(!locked);locked=true;++lock;}
  void STDMETHODCALLTYPE ReleaseSubmissionQueue()override{assert(locked);locked=false;++unlock;}
 };
 static inline Driver* driver=nullptr;
 static std::int32_t WINAPI reset(void*,std::uint32_t,const std::uint64_t*){assert(!driver->locked&&!driver->busy);++driver->resets;return driver->resetResult;}
 static std::int32_t WINAPI submit(void*,std::uint32_t count,const void* commands,std::uint64_t){assert(driver->locked&&!count&&!commands);++driver->submits;if(!driver->submitResult)driver->busy=true;return driver->submitResult;}
 static std::int32_t WINAPI wait(void*,std::uint32_t,const std::uint64_t*,std::uint32_t,std::uint64_t ns){assert(!driver->locked&&ns);++driver->waits;if(ns==UINT64_MAX||!driver->waitResult||driver->waitResult==-4){driver->busy=false;return driver->waitResult==-4?-4:0;}return driver->waitResult;}
 static std::int32_t WINAPI status(void*,std::uint64_t){return driver->busy?1:0;}
 static void WINAPI destroy(void*,std::uint64_t,const void*){assert(!driver->busy);++driver->destroys;}
 static void bind(QueryGpuWait& w,Driver& d){driver=&d;w.tried_=true;w.fence_=7;w.interop_=&d;w.reset_=reset;w.submit_=submit;w.wait_=wait;w.status_=status;w.destroy_=destroy;}
 static void run(){
  for(unsigned mode=0;mode<6;++mode){Driver d;{QueryGpuWait w;bind(w,d);const auto deadline=nowNs()+100000000;
    if(mode==1)d.waitResult=2;if(mode==2)d.submitResult=-1;if(mode==3)d.resetResult=-1;if(mode==4)d.waitResult=-4;
    const auto result=w.wait(static_cast<IDirect3DQuery9*>(nullptr),11,3,mode==5?0:deadline);
    if(!mode){assert(result==0);assert(w.wait(static_cast<IDirect3DQuery9*>(nullptr),12,3,deadline)==0);assert(d.submits==2);}
    if(mode==1){assert(result==2&&w.pending_);assert(w.wait(static_cast<IDirect3DQuery9*>(nullptr),12,3,deadline)==QueryGpuWait::Unavailable);assert(d.submits==1&&d.resets==1);assert(w.wait(static_cast<IDirect3DQuery9*>(nullptr),11,3,deadline)==2);assert(d.submits==1);}
    if(mode==2||mode==3)assert(result==-1&&!w.pending_);
    if(mode==4)assert(result==-4&&w.disabled_);
    if(mode==5)assert(result==2&&d.waits==0&&w.pending_);
    w.release();w.release();assert(d.destroys==1&&d.releases==1&&d.lock==d.unlock);
   }assert(d.destroys==1);}
  std::puts("QUERY_GPU_WAIT_PASS timeout_retains_fence no_reset_inflight old_generation_rejected queue_unlock error_fallback owner_release=exact");
 }
};
}
int main(){QueryGpuWaitTest::run();}
