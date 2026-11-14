#pragma once
#include <d3d9.h>
#include <array>
#include <vector>
#include <cstring>
#define VK_NO_PROTOTYPES
#include "../vendor-vulkan/vulkan_core.h"
#ifdef NORTHLIGHT_WORLD_NORMAL_MSAA_TEST
#include "world_normals_msaa_spirv.h"
#endif

// DXVK interop: sample zero agrees with its RESZ depth resolve. Ordinary color
// resolve averages different surfaces and cannot be used for normal/depth data.
// Native D3D9 and unsupported devices retain the existing replay transaction.
class NorthlightModelNormalMsaa {
    struct TextureInterop: IUnknown {
        virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(VkImage*,VkImageLayout*,VkImageCreateInfo*)=0;
    };
    struct ImageDesc {
        D3DRESOURCETYPE type;UINT width,height,depth,levels;DWORD usage;D3DFORMAT format;D3DPOOL pool;
        D3DMULTISAMPLE_TYPE samples;DWORD quality;bool discard,attachmentOnly,lockable;VkImageUsageFlags imageUsage;
    };
    struct DeviceInterop: IUnknown {
        virtual void STDMETHODCALLTYPE GetVulkanHandles(VkInstance*,VkPhysicalDevice*,VkDevice*)=0;
        virtual void STDMETHODCALLTYPE GetSubmissionQueue(VkQueue*,uint32_t*,uint32_t*)=0;
        virtual void STDMETHODCALLTYPE TransitionTextureLayout(TextureInterop*,const VkImageSubresourceRange*,VkImageLayout,VkImageLayout)=0;
        virtual void STDMETHODCALLTYPE FlushRenderingCommands()=0;
        virtual void STDMETHODCALLTYPE LockSubmissionQueue()=0;
        virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue()=0;
        virtual void STDMETHODCALLTYPE LockDevice()=0;
        virtual void STDMETHODCALLTYPE UnlockDevice()=0;
        virtual bool STDMETHODCALLTYPE WaitForResource(IDirect3DResource9*,DWORD)=0;
        virtual HRESULT STDMETHODCALLTYPE CreateImage(const ImageDesc*,IDirect3DResource9**)=0;
    };
    static constexpr GUID DeviceId={0x2eaa4b89,0x0107,0x4bdb,{0x87,0xf7,0x0f,0x54,0x1c,0x49,0x3c,0xe0}};
    static constexpr GUID TextureId={0xd56344f5,0x8d35,0x46fd,{0x80,0x6d,0x94,0xc3,0x51,0xb4,0x72,0xc1}};
    template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
    DeviceInterop* interop=nullptr;VkDevice device=nullptr;VkQueue queue=nullptr;HMODULE loader=nullptr;
    IDirect3DSurface9* multisample=nullptr;IDirect3DTexture9* resolved=nullptr;
    VkImage images[2]{};VkImageLayout layouts[2]{};VkImageView views[2]{};
    VkImage copySource=VK_NULL_HANDLE;VkImageLayout copyLayout{};VkDeviceMemory copyMemory=VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;VkDescriptorPool descriptors=VK_NULL_HANDLE;VkDescriptorSet descriptor=VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;VkCommandPool pool=VK_NULL_HANDLE;
    struct Slot {VkCommandBuffer command=nullptr;VkFence fence=VK_NULL_HANDLE;bool pending=false;};std::array<Slot,4> slots{};
    UINT width=0,height=0;D3DMULTISAMPLE_TYPE samples=D3DMULTISAMPLE_NONE;DWORD quality=0;bool tried=false,failed=false;
    unsigned queueFamily=0,cursor=0;const char* failure="none";
#define NL_NORMAL_VK_FUNCTIONS(X) \
    X(CreateImageView) X(DestroyImageView) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) \
    X(CreateComputePipelines) X(DestroyPipeline) X(CreateCommandPool) X(DestroyCommandPool) \
    X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) \
    X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdDispatch) X(CreateFence) X(DestroyFence) \
    X(GetFenceStatus) X(ResetFences) X(WaitForFences) X(QueueSubmit) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindImageMemory) X(CmdCopyImage) \
    X(ResetCommandBuffer) X(CmdPushConstants) X(CreateSampler) X(DestroySampler)
#define NL_NORMAL_VK_DECLARE(name) PFN_vk##name name=nullptr;
    NL_NORMAL_VK_FUNCTIONS(NL_NORMAL_VK_DECLARE)
#undef NL_NORMAL_VK_DECLARE

    bool initialize(IDirect3DDevice9* d,IDirect3DSurface9* scene){
        if(tried)return interop&&!failed;tried=true;
        // ExtensionDevice intentionally exposes no backend interfaces. Obtain
        // interop from the actual scene resource, without changing its state.
        IDirect3DDevice9* owner=nullptr;
        if(scene&&FAILED(scene->GetDevice(&owner))){failure="interop-owner";return false;}
        const auto query=(owner?owner:d)->QueryInterface(DeviceId,reinterpret_cast<void**>(&interop));drop(owner);
        if(FAILED(query)||!interop){failure="interop-device";return false;}
        VkInstance instance=nullptr;VkPhysicalDevice physical=nullptr;interop->GetVulkanHandles(&instance,&physical,&device);
        uint32_t queueIndex=0;interop->GetSubmissionQueue(&queue,&queueIndex,&queueFamily);
        loader=LoadLibraryW(L"vulkan-1.dll");if(!loader||!device||!queue)return failed=true,false;
        auto get=reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(loader,"vkGetDeviceProcAddr"));if(!get)return failed=true,false;
        auto memory=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(GetProcAddress(loader,"vkGetPhysicalDeviceMemoryProperties"));if(!memory)return failed=true,false;
        memory(physical,&memoryProperties);
#define NL_NORMAL_VK_LOAD(name) name=reinterpret_cast<PFN_vk##name>(get(device,"vk" #name));if(!name)return failed=true,false;
        NL_NORMAL_VK_FUNCTIONS(NL_NORMAL_VK_LOAD)
#undef NL_NORMAL_VK_LOAD
        return true;
    }

    // SPIR-V 1.0: 8x8 compute, bounds-checked texelFetch(source, xy, sample=0)
    // followed by imageStore(destination, xy, value). No arithmetic on pixels.
    static std::vector<uint32_t> shader(){
        std::vector<uint32_t> p={0x07230203,0x00010000,0,40,0};
        auto op=[&](uint16_t code,std::initializer_list<uint32_t> args){p.push_back((uint32_t(args.size()+1)<<16)|code);p.insert(p.end(),args);};
        op(17,{1});op(17,{50}); // Shader, ImageQuery
        op(14,{0,1}); // Logical GLSL450
        op(15,{5,20,0x6e69616d,0,13});op(16,{20,17,8,8,1});
        op(71,{13,11,28}); // GlobalInvocationId
        op(71,{14,34,0});op(71,{14,33,0});op(71,{15,34,0});op(71,{15,33,1});
        op(19,{1});op(33,{2,1});op(21,{3,32,0});op(21,{4,32,1});op(22,{5,32});
        op(23,{6,3,3});op(23,{7,4,2});op(23,{8,5,4});op(20,{9});op(23,{10,9,2});
        op(32,{11,1,6});op(59,{11,13,1});
        op(25,{16,5,1,0,0,1,1,0});op(32,{17,0,16});op(59,{17,14,0});
        op(25,{18,5,1,0,0,0,2,2});op(32,{19,0,18});op(59,{19,15,0});
        op(43,{4,12,0});op(23,{21,3,2});
        op(54,{1,20,0,2});op(248,{22});op(61,{6,23,13});
        op(79,{21,24,23,23,0,1});op(124,{7,25,24});
        op(61,{18,26,15});op(104,{7,27,26});op(177,{10,28,25,27});op(155,{9,29,28});
        op(247,{30,0});op(250,{29,31,30});op(248,{31});
        op(61,{16,32,14});op(95,{8,33,32,25,64,12});op(99,{26,25,33});
        op(249,{30});op(248,{30});op(253,{});op(56,{});return p;
    }
    bool imageInfo(IUnknown* resource,unsigned index){
        TextureInterop* texture=nullptr;if(FAILED(resource->QueryInterface(TextureId,reinterpret_cast<void**>(&texture)))||!texture)return false;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        const auto hr=texture->GetVulkanImageInfo(images+index,layouts+index,&info);texture->Release();
        const auto usage=index?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT;
        if(FAILED(hr)||!images[index]||info.format!=VK_FORMAT_R16G16B16A16_SFLOAT||
           info.extent.width!=width||info.extent.height!=height||info.arrayLayers!=1||info.mipLevels!=1||
           (index?info.samples!=VK_SAMPLE_COUNT_1_BIT:info.samples==VK_SAMPLE_COUNT_1_BIT))return false;
        if(!(info.usage&usage)){
            if(index||!(info.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT))return false;
            copySource=images[0];copyLayout=layouts[0];images[0]=VK_NULL_HANDLE;
            info.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
            if(CreateImage(device,&info,nullptr,images)!=VK_SUCCESS)return false;
            VkMemoryRequirements requirements{};GetImageMemoryRequirements(device,images[0],&requirements);
            unsigned type=0;for(;type<memoryProperties.memoryTypeCount;++type)if((requirements.memoryTypeBits&(1u<<type))&&(memoryProperties.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))break;
            if(type==memoryProperties.memoryTypeCount)return false;
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
            if(AllocateMemory(device,&allocation,nullptr,&copyMemory)!=VK_SUCCESS||BindImageMemory(device,images[0],copyMemory,0)!=VK_SUCCESS)return false;
            layouts[0]=VK_IMAGE_LAYOUT_GENERAL;
        }
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=images[index];view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=info.format;
        view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};return CreateImageView(device,&view,nullptr,views+index)==VK_SUCCESS;
    }
    void recordSourceCopy(VkCommandBuffer command){
            if(copySource){
                VkImageMemoryBarrier copy[2]{};
                for(unsigned i=0;i<2;++i){auto& b=copy[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=i?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_TRANSFER_READ_BIT;b.oldLayout=i?VK_IMAGE_LAYOUT_UNDEFINED:copyLayout;b.newLayout=i?VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=i?images[0]:copySource;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
                CmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,copy);
                VkImageCopy region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.extent={width,height,1};
                CmdCopyImage(command,copySource,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,images[0],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
                for(unsigned i=0;i<2;++i){auto& b=copy[i];std::swap(b.srcAccessMask,b.dstAccessMask);b.oldLayout=b.newLayout;b.newLayout=i?VK_IMAGE_LAYOUT_GENERAL:copyLayout;}
                CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,copy);
            }
    }
    bool makePipeline(){
        const VkDescriptorSetLayoutBinding bindings[]={{0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=2;set.pBindings=bindings;
        if(CreateDescriptorSetLayout(device,&set,nullptr,&setLayout)!=VK_SUCCESS)return false;
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&setLayout;
        if(CreatePipelineLayout(device,&layout,nullptr,&pipelineLayout)!=VK_SUCCESS)return false;
        const auto code=shader();VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=code.size()*4;module.pCode=code.data();VkShaderModule compiled=VK_NULL_HANDLE;
        if(CreateShaderModule(device,&module,nullptr,&compiled)!=VK_SUCCESS)return false;
        VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};compute.layout=pipelineLayout;
        compute.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,compiled,"main",nullptr};
        const auto hr=CreateComputePipelines(device,VK_NULL_HANDLE,1,&compute,nullptr,&pipeline);DestroyShaderModule(device,compiled,nullptr);if(hr!=VK_SUCCESS)return false;
        const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
        VkDescriptorPoolCreateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};allocation.maxSets=1;allocation.poolSizeCount=2;allocation.pPoolSizes=sizes;
        if(CreateDescriptorPool(device,&allocation,nullptr,&descriptors)!=VK_SUCCESS)return false;
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};alloc.descriptorPool=descriptors;alloc.descriptorSetCount=1;alloc.pSetLayouts=&setLayout;
        if(AllocateDescriptorSets(device,&alloc,&descriptor)!=VK_SUCCESS)return false;
        VkDescriptorImageInfo image[2]={{VK_NULL_HANDLE,views[0],VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,views[1],VK_IMAGE_LAYOUT_GENERAL}};
        VkWriteDescriptorSet writes[2]{};for(unsigned i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=descriptor;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;writes[i].pImageInfo=image+i;}
        UpdateDescriptorSets(device,2,writes,0,nullptr);
        VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};commands.queueFamilyIndex=queueFamily;
        if(CreateCommandPool(device,&commands,nullptr,&pool)!=VK_SUCCESS)return false;
        for(auto& slot:slots){
            VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};command.commandPool=pool;command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command.commandBufferCount=1;
            if(AllocateCommandBuffers(device,&command,&slot.command)!=VK_SUCCESS)return false;
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
            if(CreateFence(device,&fence,nullptr,&slot.fence)!=VK_SUCCESS)return false;
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};if(BeginCommandBuffer(slot.command,&begin)!=VK_SUCCESS)return false;
            recordSourceCopy(slot.command);
            VkImageMemoryBarrier barriers[2]{};
            for(unsigned i=0;i<2;++i){auto& b=barriers[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=i?VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT;b.oldLayout=layouts[i];b.newLayout=VK_IMAGE_LAYOUT_GENERAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=images[i];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
            CmdPipelineBarrier(slot.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,2,barriers);
            CmdBindPipeline(slot.command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);CmdBindDescriptorSets(slot.command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,1,&descriptor,0,nullptr);
            CmdDispatch(slot.command,(width+7)/8,(height+7)/8,1);
            for(auto& b:barriers){std::swap(b.srcAccessMask,b.dstAccessMask);std::swap(b.oldLayout,b.newLayout);}
            CmdPipelineBarrier(slot.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,barriers);
            if(EndCommandBuffer(slot.command)!=VK_SUCCESS)return false;
        }
        return true;
    }
    void releaseImages(){
#ifdef NORTHLIGHT_WORLD_NORMAL_MSAA_TEST
        releaseWorld();
#endif
        // Cold reset/resize only. Frames never wait for a Vulkan fence.
        if(WaitForFences)for(auto& slot:slots)if(slot.pending)WaitForFences(device,1,&slot.fence,VK_TRUE,UINT64_MAX);
        if(DestroyCommandPool&&pool)DestroyCommandPool(device,pool,nullptr);pool=VK_NULL_HANDLE;
        if(DestroyFence)for(auto& slot:slots){if(slot.fence)DestroyFence(device,slot.fence,nullptr);slot={};}
        if(DestroyDescriptorPool&&descriptors)DestroyDescriptorPool(device,descriptors,nullptr);descriptors=VK_NULL_HANDLE;descriptor=VK_NULL_HANDLE;
        if(DestroyPipeline&&pipeline)DestroyPipeline(device,pipeline,nullptr);pipeline=VK_NULL_HANDLE;
        if(DestroyPipelineLayout&&pipelineLayout)DestroyPipelineLayout(device,pipelineLayout,nullptr);pipelineLayout=VK_NULL_HANDLE;
        if(DestroyDescriptorSetLayout&&setLayout)DestroyDescriptorSetLayout(device,setLayout,nullptr);setLayout=VK_NULL_HANDLE;
        if(DestroyImageView)for(auto& view:views){if(view)DestroyImageView(device,view,nullptr);view=VK_NULL_HANDLE;}
        if(copySource&&images[0])DestroyImage(device,images[0],nullptr);
        if(copyMemory)FreeMemory(device,copyMemory,nullptr);copyMemory=VK_NULL_HANDLE;copySource=VK_NULL_HANDLE;images[0]=images[1]=VK_NULL_HANDLE;
        drop(multisample);drop(resolved);width=height=0;cursor=0;
    }
public:
    NorthlightModelNormalMsaa()=default;
    NorthlightModelNormalMsaa(const NorthlightModelNormalMsaa&)=delete;
    NorthlightModelNormalMsaa& operator=(const NorthlightModelNormalMsaa&)=delete;
    const char* failureReason()const{return failure;}
    ~NorthlightModelNormalMsaa(){reset();}
    void reset(){releaseImages();drop(interop);if(loader)FreeLibrary(loader);loader=nullptr;device=nullptr;queue=nullptr;tried=failed=false;}
    IDirect3DSurface9* surface()const{return multisample;}
    IDirect3DTexture9* texture()const{return resolved;}
    bool ensure(IDirect3DDevice9* d,const D3DSURFACE_DESC& desc,IDirect3DSurface9* scene=nullptr){
        // Prototype ceiling includes both MSAA images and the resolve texture.
        const uint64_t sampleCount=desc.MultiSampleType==D3DMULTISAMPLE_NONMASKABLE?16u:unsigned(desc.MultiSampleType);
        if(uint64_t(desc.Width)*desc.Height*8*(2*sampleCount+1)>(640ull<<20)){failure="msaa-budget";return false;}
        if(failed||desc.MultiSampleType==D3DMULTISAMPLE_NONE||!initialize(d,scene))return false;
        if(multisample&&width==desc.Width&&height==desc.Height&&samples==desc.MultiSampleType&&quality==desc.MultiSampleQuality)return true;
        releaseImages();width=desc.Width;height=desc.Height;samples=desc.MultiSampleType;quality=desc.MultiSampleQuality;
        ImageDesc source{D3DRTYPE_SURFACE,width,height,1,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,samples,quality,false,false,false,0};
        IDirect3DResource9* resource=nullptr;
        auto sourceHr=interop->CreateImage(&source,&resource);
        if(SUCCEEDED(sourceHr)&&resource){sourceHr=resource->QueryInterface(__uuidof(IDirect3DSurface9),reinterpret_cast<void**>(&multisample));resource->Release();}
        if(!multisample)sourceHr=d->CreateRenderTarget(width,height,D3DFMT_A16B16G16R16F,samples,quality,FALSE,&multisample,nullptr);
        source.type=D3DRTYPE_TEXTURE;source.samples=D3DMULTISAMPLE_NONE;source.quality=0;source.imageUsage=VK_IMAGE_USAGE_STORAGE_BIT;resource=nullptr;
        if(multisample&&SUCCEEDED(interop->CreateImage(&source,&resource))&&resource){resource->QueryInterface(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&resolved));resource->Release();}
        if(!multisample)failure="source-image";
        else if(!resolved)failure="destination-image";
        else if(!imageInfo(multisample,0))failure="source-view";
        else if(!imageInfo(resolved,1))failure="destination-view";
        else if(!makePipeline())failure="pipeline";
        else return true;
        releaseImages();failed=true;return false;
    }
    bool resolve(){
        if(failed||!pipeline)return false;
        Slot* available=nullptr;for(unsigned i=0;i<slots.size();++i){auto& slot=slots[(cursor+i)%slots.size()];const auto result=GetFenceStatus(device,slot.fence);
            if(result==VK_SUCCESS){available=&slot;cursor=(cursor+i+1)%slots.size();break;}if(result!=VK_NOT_READY)return failed=true,false;}
        if(!available)return false;
        // Flush before taking the queue lock; no D3D9 calls are made under it.
        interop->FlushRenderingCommands();
        if(ResetFences(device,1,&available->fence)!=VK_SUCCESS)return failed=true,false;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&available->command;
        interop->LockSubmissionQueue();const auto result=QueueSubmit(queue,1,&submit,available->fence);interop->ReleaseSubmissionQueue();
        if(result!=VK_SUCCESS)return failed=true,false;available->pending=true;return true;
    }
#ifdef NORTHLIGHT_WORLD_NORMAL_MSAA_TEST
#include "model_normal_world_msaa.inl"
#endif
#undef NL_NORMAL_VK_FUNCTIONS
};
