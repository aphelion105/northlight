#pragma once
#include <cstdint>
#include "stream_wait.h"
#if defined(_WIN32) && !defined(NL_STUB)
#include <windows.h>
#include <d3d9.h>
#endif
namespace NorthlightStream {
// Optional blocking wait for an audited DXVK device. It never answers a query:
// after waking, the caller must still read the real EVENT result. One fence per
// stream; timeouts retain it until signaled. No D3D9 calls under the queue lock.
class QueryGpuWait {
#if defined(_WIN32) && !defined(NL_STUB)
    friend struct QueryGpuWaitTest;
    // Prefix of DXVK3.1.1 src/d3d9/d3d9_interfaces.h ID3D9VkInteropDevice.
    struct Interop: IUnknown {
        virtual void STDMETHODCALLTYPE GetVulkanHandles(void**,void**,void**)=0;
        virtual void STDMETHODCALLTYPE GetSubmissionQueue(void**,std::uint32_t*,std::uint32_t*)=0;
        virtual void STDMETHODCALLTYPE TransitionTextureLayout(void*,const void*,std::int32_t,std::int32_t)=0;
        virtual void STDMETHODCALLTYPE FlushRenderingCommands()=0;
        virtual void STDMETHODCALLTYPE LockSubmissionQueue()=0;
        virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue()=0;
    };
    struct FenceInfo {std::uint32_t type;const void* next;std::uint32_t flags;};
    using Create=std::int32_t(WINAPI*)(void*,const FenceInfo*,const void*,std::uint64_t*);
    using Destroy=void(WINAPI*)(void*,std::uint64_t,const void*);
    using Reset=std::int32_t(WINAPI*)(void*,std::uint32_t,const std::uint64_t*);
    using Submit=std::int32_t(WINAPI*)(void*,std::uint32_t,const void*,std::uint64_t);
    using Wait=std::int32_t(WINAPI*)(void*,std::uint32_t,const std::uint64_t*,std::uint32_t,std::uint64_t);
    using Status=std::int32_t(WINAPI*)(void*,std::uint64_t);
    Interop* interop_=nullptr;void* device_=nullptr;void* queue_=nullptr;HMODULE loader_=nullptr;
    Create create_=nullptr;Destroy destroy_=nullptr;Reset reset_=nullptr;Submit submit_=nullptr;Wait wait_=nullptr;Status status_=nullptr;
    std::uint64_t fence_=0,sequence_=0;std::uint32_t epoch_=0;bool tried_=false,disabled_=false,pending_=false;
    bool initialize(IDirect3DQuery9* query){
        if(tried_)return fence_&&!disabled_;
        tried_=true;
        IDirect3DDevice9* d=nullptr;if(FAILED(query->GetDevice(&d))||!d)return false;
        constexpr GUID iid={0x2eaa4b89,0x0107,0x4bdb,{0x87,0xf7,0x0f,0x54,0x1c,0x49,0x3c,0xe0}};
        const auto hr=d->QueryInterface(iid,reinterpret_cast<void**>(&interop_));d->Release();
        if(FAILED(hr)||!interop_)return false;
        interop_->GetVulkanHandles(nullptr,nullptr,&device_);interop_->GetSubmissionQueue(&queue_,nullptr,nullptr);
        if(!device_||!queue_)return false;
        loader_=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!loader_)return false;
        using Function=void(WINAPI*)();
        using GetProc=Function(WINAPI*)(void*,const char*);
        const auto get=reinterpret_cast<GetProc>(reinterpret_cast<void*>(GetProcAddress(loader_,"vkGetDeviceProcAddr")));if(!get)return false;
        create_=reinterpret_cast<Create>(get(device_,"vkCreateFence"));destroy_=reinterpret_cast<Destroy>(get(device_,"vkDestroyFence"));
        reset_=reinterpret_cast<Reset>(get(device_,"vkResetFences"));submit_=reinterpret_cast<Submit>(get(device_,"vkQueueSubmit"));
        wait_=reinterpret_cast<Wait>(get(device_,"vkWaitForFences"));status_=reinterpret_cast<Status>(get(device_,"vkGetFenceStatus"));
        if(!create_||!destroy_||!reset_||!submit_||!wait_||!status_)return false;
        const FenceInfo info{8,nullptr,0};std::uint64_t created=0;
        if(create_(device_,&info,nullptr,&created)!=0||!created)return false;
        fence_=created;return true;
    }
#endif
public:
    static constexpr int Unavailable=3; // distinct from Vulkan success0 / timeout2 / negative errors
    QueryGpuWait()=default;QueryGpuWait(const QueryGpuWait&)=delete;QueryGpuWait& operator=(const QueryGpuWait&)=delete;
    ~QueryGpuWait(){release();}
    void release(){
#if defined(_WIN32) && !defined(NL_STUB)
        // Never destroy a submitted fence while the GPU still references it.
        // The interop reference keeps its device alive through this final join.
        if(fence_){if(pending_)(void)wait_(device_,1,&fence_,1,UINT64_MAX);destroy_(device_,fence_,nullptr);}
        if(interop_)interop_->Release();if(loader_)FreeLibrary(loader_);
        fence_=0;pending_=false;interop_=nullptr;loader_=nullptr;disabled_=true;
#endif
    }
    template<class Query> int wait(Query* query,std::uint64_t sequence,std::uint32_t epoch,std::uint64_t deadline){
#if defined(_WIN32) && !defined(NL_STUB)
        if(disabled_||!initialize(query))return Unavailable;
        if(pending_&&(sequence_!=sequence||epoch_!=epoch)){
            const auto ready=status_(device_,fence_);if(ready==1)return Unavailable;
            if(ready<0){disabled_=true;return ready;}pending_=false;
        }
        if(!pending_){
            const auto reset=reset_(device_,1,&fence_);if(reset<0){disabled_=true;return reset;}
            interop_->FlushRenderingCommands();
            // This may flush asynchronously. A prematurely signaled fence is
            // harmless: only the original GetData can establish completion.
            interop_->LockSubmissionQueue();const auto submitted=submit_(queue_,0,nullptr,fence_);interop_->ReleaseSubmissionQueue();
            if(submitted<0){disabled_=true;return submitted;}
            pending_=true;sequence_=sequence;epoch_=epoch;
        }
        const auto now=nowNs();if(now>=deadline)return 2;
        const auto result=wait_(device_,1,&fence_,1,deadline-now);
        if(result==0)pending_=false;else if(result<0)disabled_=true;
        return result;
#else
        (void)query;(void)sequence;(void)epoch;(void)deadline;return Unavailable;
#endif
    }
};
}
