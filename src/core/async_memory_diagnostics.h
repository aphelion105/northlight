#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

// Diagnostics and the memory guard's trim trigger (memory_guard.h). Never use
// this delayed sample for memory admission, which must retain its synchronous,
// fresh allocation checks.
namespace NorthlightMemoryDiagnostics {
struct Sample {
    std::uint64_t availableVirtual=0,totalVirtual=0,availablePhysical=0;
    std::uint64_t largestFree=0,totalFree=0,regions=0;
    std::uint64_t tick=0,wallNanoseconds=0;
    bool valid=false;
};
class Sampler {
public:
    using Query=Sample(*)(void*);
    // One queued job (e.g. a MEMMAP in-world snapshot): a by-value payload copied under the mutex, run on the worker thread.
    using Job=void(*)(void*,const void*,std::size_t);
    static constexpr std::size_t JobBytes=1024;
private:
    Query query_;void* context_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_=false,pending_=false,jobPending_=false;
    Job job_=nullptr;void* jobContext_=nullptr;std::size_t jobSize_=0;alignas(8) unsigned char jobData_[JobBytes];
    Sample result_;
    std::atomic<bool> available_{false};
    std::thread worker_;
    void run(){
        std::unique_lock<std::mutex> lock(mutex_);
        for(;;){
            wake_.wait(lock,[&]{return stopping_||pending_||jobPending_;});
            if(stopping_)return;
            if(pending_){
                pending_=false;
                lock.unlock();
                Sample next;
                try{if(query_)next=query_(context_);}catch(...){next={};}
                lock.lock();
                if(stopping_)return;
                result_=next;available_.store(true,std::memory_order_release);
            }
            // Order matters: the guard's sample is taken and published first; a job (MEMMAP) only follows, so the next guard sample waits at most for one job.
            if(jobPending_){
                alignas(8) unsigned char local[JobBytes];const std::size_t size=jobSize_;std::memcpy(local,jobData_,size);
                const Job job=job_;void* context=jobContext_;jobPending_=false;
                lock.unlock();
                try{if(job)job(context,local,size);}catch(...){}
                lock.lock();
                if(stopping_)return;
            }
        }
    }
public:
    explicit Sampler(Query query,void* context=nullptr):query_(query),context_(context),worker_([this]{run();}){}
    Sampler(const Sampler&)=delete;Sampler& operator=(const Sampler&)=delete;
    ~Sampler(){
        {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;pending_=false;}
        wake_.notify_one();if(worker_.joinable())worker_.join();
    }
    // At most one queued request and one completed result. A slow query never
    // holds this mutex or accumulates an unbounded queue on the render thread.
    void request(){
        {std::lock_guard<std::mutex> lock(mutex_);if(stopping_)return;pending_=true;}
        wake_.notify_one();
    }
    // The job runner; set before the first requestJob.
    void setJob(Job job,void* context=nullptr){std::lock_guard<std::mutex> lock(mutex_);job_=job;jobContext_=context;}
    // Queues one job (payload <= JobBytes, copied now); false when one is already waiting, stopping, no runner or too large.
    // Never waits for a running job: the mutex is held only for the copy.
    bool requestJob(const void* data,std::size_t size){
        {std::lock_guard<std::mutex> lock(mutex_);
         if(stopping_||jobPending_||!job_||size>JobBytes)return false;
         std::memcpy(jobData_,data,size);jobSize_=size;jobPending_=true;}
        wake_.notify_one();return true;
    }
    bool take(Sample& sample){
        if(!available_.load(std::memory_order_acquire))return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!available_.exchange(false,std::memory_order_relaxed))return false;
        sample=result_;return true;
    }
};
}
