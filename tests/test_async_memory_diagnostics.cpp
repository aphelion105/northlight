#include "async_memory_diagnostics.h"
#include <cassert>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>

using namespace NorthlightMemoryDiagnostics;
using namespace std::chrono_literals;
struct Backend {
    std::mutex mutex;std::condition_variable wake;
    unsigned entered=0,released=0,finished=0;
    bool fail=false;
    std::thread::id caller=std::this_thread::get_id(),worker;
    static Sample query(void* context){
        auto& b=*static_cast<Backend*>(context);std::unique_lock<std::mutex> lock(b.mutex);
        assert(std::this_thread::get_id()!=b.caller);b.worker=std::this_thread::get_id();
        const auto call=++b.entered;b.wake.notify_all();
        b.wake.wait(lock,[&]{return b.released>=call;});
        ++b.finished;b.wake.notify_all();
        if(b.fail){b.fail=false;throw std::runtime_error("query failure");}
        Sample s;s.valid=true;s.tick=call;s.regions=100+call;s.availableVirtual=1000+call;
        s.totalVirtual=2000+call;s.availablePhysical=3000+call;s.largestFree=400+call;
        s.totalFree=500+call;s.wallNanoseconds=600+call;return s;
    }
    void await(unsigned count){std::unique_lock<std::mutex> lock(mutex);assert(wake.wait_for(lock,5s,[&]{return entered>=count;}));}
    void release(unsigned count){std::lock_guard<std::mutex> lock(mutex);released=count;wake.notify_all();}
};
static Sample result(Sampler& sampler){
    Sample s;const auto limit=std::chrono::steady_clock::now()+5s;
    while(!sampler.take(s)){assert(std::chrono::steady_clock::now()<limit);std::this_thread::yield();}
    return s;
}
int main(){
    static_assert(!std::is_copy_constructible<Sampler>::value,"worker ownership must not copy");
    Backend b;
    {
        Sampler sampler(&Backend::query,&b);Sample none;assert(!sampler.take(none));
        sampler.request();b.await(1);
        // The worker is blocked in the query. All request/take operations must
        // still complete before we release it, with one coalesced queued query.
        for(unsigned n=0;n<10000;++n){sampler.request();assert(!sampler.take(none));}
        b.release(1);b.await(2);auto s=result(sampler);
        assert(s.valid&&s.tick==1&&s.regions==101&&s.availableVirtual==1001&&s.totalVirtual==2001&&
            s.availablePhysical==3001&&s.largestFree==401&&s.totalFree==501&&s.wallNanoseconds==601);
        b.release(2);s=result(sampler);assert(s.valid&&s.tick==2&&!sampler.take(none));
        {std::lock_guard<std::mutex> lock(b.mutex);assert(b.entered==2&&b.finished==2);b.fail=true;}
        sampler.request();b.await(3);b.release(3);s=result(sampler);assert(!s.valid);
        sampler.request();b.await(4);b.release(4);s=result(sampler);assert(s.valid&&s.tick==4);
    }
    // Destruction while a query is active waits for that query, rejects the
    // queued one, and leaves no callback able to access a destroyed context.
    Backend ending;auto sampler=std::make_unique<Sampler>(&Backend::query,&ending);
    sampler->request();ending.await(1);sampler->request();
    auto destroyed=std::async(std::launch::async,[&]{sampler.reset();});
    assert(destroyed.wait_for(20ms)==std::future_status::timeout);
    // Let a possibly scheduled second request finish too: race with shutdown
    // is legal, but the destructor must join every started callback.
    ending.release(2);assert(destroyed.wait_for(5s)==std::future_status::ready);destroyed.get();
    {std::lock_guard<std::mutex> lock(ending.mutex);assert(ending.entered==ending.finished&&ending.entered<=2);}
    {Sampler idle(&Backend::query,&ending);} // no request, no query
    // task 42 (MEMMAP jobs): runs on the worker thread with a by-value payload; one waiting job, a running job never blocks requestJob/take/request.
    {
        struct JobBox {std::mutex m;std::condition_variable cv;unsigned started=0,done=0;bool hold=true;std::thread::id caller=std::this_thread::get_id(),worker;char seen[16]={};std::size_t size=0;};
        JobBox box;
        Backend idle;
        Sampler sampler(&Backend::query,&idle);
        const char payload[16]="point-one";
        assert(!sampler.requestJob(payload,sizeof payload)); // no runner yet
        sampler.setJob([](void* c,const void* data,std::size_t size){
            auto& b=*static_cast<JobBox*>(c);std::unique_lock<std::mutex> lock(b.m);
            b.worker=std::this_thread::get_id();std::memcpy(b.seen,data,size<sizeof b.seen?size:sizeof b.seen);b.size=size;++b.started;b.cv.notify_all();
            b.cv.wait(lock,[&]{return !b.hold;});++b.done;b.cv.notify_all();},&box);
        assert(!sampler.requestJob(payload,Sampler::JobBytes+1)); // too large
        assert(sampler.requestJob(payload,sizeof payload));
        {std::unique_lock<std::mutex> lock(box.m);assert(box.cv.wait_for(lock,5s,[&]{return box.started==1;}));assert(box.worker!=box.caller&&box.size==sizeof payload&&!std::strcmp(box.seen,"point-one"));}
        // the job is blocked: a second one queues (payload copied now), a third is refused, and request()/take() still return at once
        char second[16]="point-two";
        assert(sampler.requestJob(second,sizeof second));std::memset(second,0,sizeof second);
        assert(!sampler.requestJob(payload,sizeof payload));
        Sample none;assert(!sampler.take(none));sampler.request();idle.release(1);
        {std::lock_guard<std::mutex> lock(box.m);box.hold=false;box.cv.notify_all();}
        {std::unique_lock<std::mutex> lock(box.m);assert(box.cv.wait_for(lock,5s,[&]{return box.done==2;}));assert(!std::strcmp(box.seen,"point-two"));}
        idle.await(1);auto s=result(sampler);assert(s.valid&&s.tick==1); // the guard query still ran between/after jobs
    }
    // task 42 review: with a guard query running and a job queued behind it, the guard's sample is published before the job starts,
    // and a guard request made during the job is served right after it (the job delays it by at most its own runtime).
    {
        struct Order {Sampler* sampler=nullptr;bool sampleBeforeJob=false,done=false;std::mutex m;std::condition_variable cv;};
        Order order;Backend guard;Sampler sampler(&Backend::query,&guard);order.sampler=&sampler;
        sampler.setJob([](void* c,const void*,std::size_t){auto& o=*static_cast<Order*>(c);Sample s;o.sampleBeforeJob=o.sampler->take(s)&&s.valid&&s.tick==1;
            std::lock_guard<std::mutex> lock(o.m);o.done=true;o.cv.notify_all();},&order);
        sampler.request();guard.await(1);
        const char p[4]="x";assert(sampler.requestJob(p,sizeof p));guard.release(1);
        {std::unique_lock<std::mutex> lock(order.m);assert(order.cv.wait_for(lock,5s,[&]{return order.done;}));}
        assert(order.sampleBeforeJob);
        sampler.request();guard.await(2);guard.release(2);auto s=result(sampler);assert(s.valid&&s.tick==2);
    }
    std::puts("PASS async diagnostics: off-caller query; nonblocking poll/10,000 coalesced requests; exact sample publication; failure/recovery; active and idle shutdown; no callbacks after destruction.");
}
