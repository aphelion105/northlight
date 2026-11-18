#pragma once
inline void WorldColdUploadTest::textureAliases(UploadFaultDevice& d,D3DPRESENT_PARAMETERS& parameters){
    namespace M=NorthlightWorldMesh;namespace P=NorthlightWorldMeshPages;using namespace NorthlightGI;
    for(bool spooled:{false,true}){
        Spooling=spooled;auto renderer=std::make_unique<WorldRenderer>(&d);auto& w=*renderer;
        w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));
        if(!spooled)w.shadowTextureFormat.known=true; // Exercise the four-byte GPU fallback as well as native A8.
        auto make=[&]{
            WorldScene source;source.vertices={{{0,0,.5f},{0,0,1},0,0},{{1,0,.5f},{0,0,1},1,0},{{0,1,.5f},{0,0,1},0,1}};
            auto texels=std::make_shared<std::vector<uint8_t>>(512*512*4,127);
            for(unsigned i=0;i<900;++i){WorldMaterial m;m.width=m.height=512;m.alphaCutoff=.1f+.001f*i;m.lampSourceId=i+1;
                if(i==897){m.rgba=*texels;for(size_t j=3;j<m.rgba.size();j+=4)m.rgba[j]=255;m.alphaCutoff=1;}
                else if(i==898){m.rgba=*texels;m.rgba[0]=42;}
                else if(i==899){m.rgba=*texels;m.rgba[3]=17;}else{m.sharedTexels={texels,texels->data()};m.sharedTexelBytes=texels->size();}
                source.materials.push_back(std::move(m));source.triangles.push_back({0,1,2,i});
            }
            auto plan=std::make_shared<M::WorldMeshUploadPlan>();auto pages=std::make_shared<P::Plan>();std::string error;P::Options options;options.spooled=spooled;options.deviceLocal=true;
            M::PlanOptions masks;masks.alphaOnly=true;
            assert(M::buildUploadPlan(source,*plan,error,{},nullptr,masks)&&P::build(*plan,*pages,error,{},options)&&P::seal(*plan,pages,error));
            assert(plan->textureBytes==2*1024*1024+4&&!P::validateSealed(*plan));
            const auto upload=plan->payload.share();
            assert(upload->materials[898].textureOwner==0&&upload->materials[0].pixelBytes()==512*512);
            auto snapshot=std::make_shared<WorldRenderer::Snapshot>();snapshot->bvh=std::make_shared<BVH>();snapshot->meshPlan=plan;snapshot->map="cutout-sharing";return snapshot;
        };
        AddressBallast pressure;
        while(pressure.available()>(528ull<<20)){void* block=VirtualAlloc(nullptr,16u<<20,MEM_RESERVE,PAGE_NOACCESS);assert(block);pressure.blocks.push_back(block);}
        assert(pressure.available()>=(512ull<<20)&&pressure.available()<=(528ull<<20));
        auto snapshot=make();w.active=snapshot;d.failTextureAt=d.textures+2;
        for(unsigned i=0;i<2000&&!w.meshRetry.waiting;++i){NorthlightStreaming::Budget budget(4u<<20,2);w.upload(budget);assert(SUCCEEDED(d.Present(nullptr,nullptr,nullptr,nullptr)));}
        assert(w.meshRetry.waiting&&d.textures==d.failTextureAt&&!w.pendingMesh&&!w.uploaded.lock());
        d.failTextureAt=0;w.meshRetry.clear();
        size_t slots=0,maxSlots=0;unsigned materialVisits=0;
        complete(w,snapshot,[&]{
            const size_t staged=w.pendingMesh?w.pendingMesh->material:w.materials.size();
            if(staged>slots){maxSlots=std::max(maxSlots,staged-slots);++materialVisits;slots=staged;}
        });
        assert(slots==900);
        auto verify=[&]{
            assert(w.materials.size()==900&&w.materials[0]!=w.materials[899]);
            for(unsigned i=0;i<900;++i){assert(w.uploadedAlphaCutoffs[i]==(i==897?0.f:.1f+.001f*i));if(i!=897&&i<899)assert(w.materials[i]==w.materials[0]);}
            assert(readShadowAlpha(&d,w.materials[897])==std::vector<uint8_t>{255});
            for(unsigned i:{0u,899u}){const auto pixels=readShadowAlpha(&d,w.materials[i]);
                for(unsigned y=0;y<512;++y)for(unsigned x=0;x<512;++x)assert(pixels[y*512+x]==(i==899&&y==0&&x==0?17:127));
            }
        };
        verify();
        w.reset();assert(SUCCEEDED(d.Reset(&parameters)));w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));
        if(!spooled)snapshot=make();complete(w,snapshot);verify();
        std::printf("CUTOUT_ALIASES_PASS spooled=%u slots=900 uniqueTextures=3 materialVisits=%u maxSlotsPerVisit=%zu exactPixels=1 cutoffs=1 failedUploadRollback=1 reset=1 availableMiB=%llu\n",unsigned(spooled),materialVisits,maxSlots,(unsigned long long)(pressure.available()>>20));
    }
}
