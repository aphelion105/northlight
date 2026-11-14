// Experimental consumer fusion. Included in NorthlightModelNormalMsaa.
private:
    struct WorldImage {
        IUnknown* resource=nullptr;VkImage image=VK_NULL_HANDLE;VkImageView view=VK_NULL_HANDLE;
        VkImageLayout layout{};VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT,barrierAspect=VK_IMAGE_ASPECT_COLOR_BIT;
    };
    struct WorldSlot {
        Slot work;VkDescriptorSet descriptor=VK_NULL_HANDLE;WorldImage image[4];
    };
    std::array<WorldSlot,4> worldSlots{};
    VkDescriptorSetLayout worldSetLayout=VK_NULL_HANDLE;VkDescriptorPool worldDescriptors=VK_NULL_HANDLE;
    VkPipelineLayout worldLayout=VK_NULL_HANDLE;VkPipeline worldPipeline=VK_NULL_HANDLE;VkCommandPool worldPool=VK_NULL_HANDLE;
    VkSampler worldSceneSampler=VK_NULL_HANDLE;
    IDirect3DTexture9 *worldNormal=nullptr,*worldDistance=nullptr;
    unsigned worldCursor=0;

    void releaseWorldImage(WorldImage& image){
        if(image.view)DestroyImageView(device,image.view,nullptr);drop(image.resource);image={};
    }
    void releaseWorld(){
        for(auto& s:worldSlots){if(s.work.pending)WaitForFences(device,1,&s.work.fence,VK_TRUE,UINT64_MAX);
            for(auto& i:s.image)releaseWorldImage(i);if(s.work.fence)DestroyFence(device,s.work.fence,nullptr);s={};}
        if(worldPool)DestroyCommandPool(device,worldPool,nullptr);worldPool=VK_NULL_HANDLE;
        if(worldPipeline)DestroyPipeline(device,worldPipeline,nullptr);worldPipeline=VK_NULL_HANDLE;
        if(worldLayout)DestroyPipelineLayout(device,worldLayout,nullptr);worldLayout=VK_NULL_HANDLE;
        if(worldDescriptors)DestroyDescriptorPool(device,worldDescriptors,nullptr);worldDescriptors=VK_NULL_HANDLE;
        if(worldSetLayout)DestroyDescriptorSetLayout(device,worldSetLayout,nullptr);worldSetLayout=VK_NULL_HANDLE;
        if(worldSceneSampler)DestroySampler(device,worldSceneSampler,nullptr);worldSceneSampler=VK_NULL_HANDLE;
        drop(worldNormal);drop(worldDistance);worldCursor=0;
    }
    bool worldImage(IUnknown* resource,WorldImage& image,bool storage){
        if(resource&&image.resource==resource)return true;
        releaseWorldImage(image);if(!resource)return false;
        TextureInterop* texture=nullptr;if(FAILED(resource->QueryInterface(TextureId,reinterpret_cast<void**>(&texture)))||!texture)return false;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};const auto hr=texture->GetVulkanImageInfo(&image.image,&image.layout,&info);texture->Release();
        const auto usage=storage?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT;
        if(FAILED(hr)||!image.image||info.samples!=VK_SAMPLE_COUNT_1_BIT||info.mipLevels!=1||info.arrayLayers!=1||!(info.usage&usage))return false;
        if(info.format==VK_FORMAT_D16_UNORM||info.format==VK_FORMAT_D32_SFLOAT||info.format==VK_FORMAT_D24_UNORM_S8_UINT||info.format==VK_FORMAT_D32_SFLOAT_S8_UINT)image.aspect=VK_IMAGE_ASPECT_DEPTH_BIT;
        image.barrierAspect=image.aspect;
        if(info.format==VK_FORMAT_D24_UNORM_S8_UINT||info.format==VK_FORMAT_D32_SFLOAT_S8_UINT)image.barrierAspect|=VK_IMAGE_ASPECT_STENCIL_BIT;
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=image.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=info.format;
        view.subresourceRange={image.aspect,0,1,0,1};if(CreateImageView(device,&view,nullptr,&image.view)!=VK_SUCCESS)return false;
        image.resource=resource;resource->AddRef();return true;
    }
    bool worldTarget(D3DFORMAT format,IDirect3DTexture9*& result){
        ImageDesc desc{D3DRTYPE_TEXTURE,width/2,height/2,1,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,D3DMULTISAMPLE_NONE,0,false,false,false,VK_IMAGE_USAGE_STORAGE_BIT};
        IDirect3DResource9* resource=nullptr;const auto hr=interop->CreateImage(&desc,&resource);if(FAILED(hr)||!resource)return false;
        const auto queried=resource->QueryInterface(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&result));resource->Release();return SUCCEEDED(queried)&&result;
    }
    bool makeWorld(){
        if(worldPipeline)return true;
        if(!worldTarget(D3DFMT_A16B16G16R16F,worldNormal)||!worldTarget(D3DFMT_R32F,worldDistance))return false;
        VkDescriptorSetLayoutBinding bindings[6]{};
        for(unsigned i=0;i<5;++i)bindings[i]={i,i<3?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        bindings[5]={5,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sampler.maxLod=0;
        if(CreateSampler(device,&sampler,nullptr,&worldSceneSampler)!=VK_SUCCESS)return false;
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=6;set.pBindings=bindings;
        if(CreateDescriptorSetLayout(device,&set,nullptr,&worldSetLayout)!=VK_SUCCESS)return false;
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,128};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&worldSetLayout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
        if(CreatePipelineLayout(device,&layout,nullptr,&worldLayout)!=VK_SUCCESS)return false;
        VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=sizeof(kWorldNormalsMsaa);module.pCode=kWorldNormalsMsaa;VkShaderModule compiled=VK_NULL_HANDLE;
        if(CreateShaderModule(device,&module,nullptr,&compiled)!=VK_SUCCESS)return false;
        VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};compute.layout=worldLayout;
        compute.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,compiled,"WorldNormalsMsaa",nullptr};
        const auto result=CreateComputePipelines(device,VK_NULL_HANDLE,1,&compute,nullptr,&worldPipeline);DestroyShaderModule(device,compiled,nullptr);if(result!=VK_SUCCESS)return false;
        const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,12},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,8},{VK_DESCRIPTOR_TYPE_SAMPLER,4}};
        VkDescriptorPoolCreateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};allocation.maxSets=4;allocation.poolSizeCount=3;allocation.pPoolSizes=sizes;
        if(CreateDescriptorPool(device,&allocation,nullptr,&worldDescriptors)!=VK_SUCCESS)return false;
        VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};commands.queueFamilyIndex=queueFamily;commands.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if(CreateCommandPool(device,&commands,nullptr,&worldPool)!=VK_SUCCESS)return false;
        for(auto& slot:worldSlots){
            VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};alloc.descriptorPool=worldDescriptors;alloc.descriptorSetCount=1;alloc.pSetLayouts=&worldSetLayout;
            if(AllocateDescriptorSets(device,&alloc,&slot.descriptor)!=VK_SUCCESS)return false;
            VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};command.commandPool=worldPool;command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command.commandBufferCount=1;
            if(AllocateCommandBuffers(device,&command,&slot.work.command)!=VK_SUCCESS)return false;
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
            if(CreateFence(device,&fence,nullptr,&slot.work.fence)!=VK_SUCCESS)return false;
        }
        return true;
    }
public:
    IDirect3DTexture9* worldTexture()const{return worldNormal;}
    IDirect3DTexture9* worldDistanceTexture()const{return worldDistance;}
    // constants: existing c0..5, c33 and c79, with c79.y = distance write.
    bool composeWorld(IDirect3DTexture9* scene,IDirect3DTexture9* depth,const float* constants){
        if(!pipeline||!makeWorld())return false;
        WorldSlot* selected=nullptr;
        for(unsigned i=0;i<worldSlots.size();++i){auto& s=worldSlots[(worldCursor+i)%worldSlots.size()];const auto status=GetFenceStatus(device,s.work.fence);
            if(status==VK_SUCCESS){selected=&s;worldCursor=(worldCursor+i+1)%worldSlots.size();break;}if(status!=VK_NOT_READY)return false;}
        if(!selected)return false;auto& s=*selected;
        IUnknown* resources[]={scene,depth,worldNormal,worldDistance};
        for(unsigned i=0;i<4;++i)if(!worldImage(resources[i],s.image[i],i>=2))return false;
        VkDescriptorImageInfo image[6]{};image[0]={VK_NULL_HANDLE,views[0],VK_IMAGE_LAYOUT_GENERAL};image[5].sampler=worldSceneSampler;
        for(unsigned i=0;i<4;++i)image[i+1]={VK_NULL_HANDLE,s.image[i].view,VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[6]{};
        for(unsigned i=0;i<5;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=s.descriptor;w.dstBinding=i;w.descriptorCount=1;w.descriptorType=i<3?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=image+i;}
        writes[5]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,s.descriptor,5,0,1,VK_DESCRIPTOR_TYPE_SAMPLER,image+5,nullptr,nullptr};
        UpdateDescriptorSets(device,6,writes,0,nullptr);
        if(ResetCommandBuffer(s.work.command,0)!=VK_SUCCESS)return false;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};if(BeginCommandBuffer(s.work.command,&begin)!=VK_SUCCESS)return false;
        recordSourceCopy(s.work.command);
        VkImageMemoryBarrier barriers[5]{};
        for(unsigned i=0;i<5;++i){auto& b=barriers[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=i>=3?VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT;b.oldLayout=i?s.image[i-1].layout:layouts[0];b.newLayout=VK_IMAGE_LAYOUT_GENERAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=i?s.image[i-1].image:images[0];b.subresourceRange={i?s.image[i-1].barrierAspect:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
        CmdPipelineBarrier(s.work.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,5,barriers);
        CmdBindPipeline(s.work.command,VK_PIPELINE_BIND_POINT_COMPUTE,worldPipeline);CmdBindDescriptorSets(s.work.command,VK_PIPELINE_BIND_POINT_COMPUTE,worldLayout,0,1,&s.descriptor,0,nullptr);
        CmdPushConstants(s.work.command,worldLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,128,constants);CmdDispatch(s.work.command,(width/2+7)/8,(height/2+7)/8,1);
        for(auto& b:barriers){std::swap(b.srcAccessMask,b.dstAccessMask);std::swap(b.oldLayout,b.newLayout);}
        CmdPipelineBarrier(s.work.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,5,barriers);
        if(EndCommandBuffer(s.work.command)!=VK_SUCCESS)return false;
        interop->FlushRenderingCommands();if(ResetFences(device,1,&s.work.fence)!=VK_SUCCESS)return false;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&s.work.command;
        interop->LockSubmissionQueue();const auto status=QueueSubmit(queue,1,&submit,s.work.fence);interop->ReleaseSubmissionQueue();
        if(status!=VK_SUCCESS)return false;s.work.pending=true;return true;
    }
