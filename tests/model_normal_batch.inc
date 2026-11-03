// Included by the native-model fixture: identical shaders, changing poses and
// source bytes, interleaved MSAA scene draws, then exact normal/depth comparison.
static uint64_t normalSourceRevision=1;
static void normalBatch(IDirect3D9* api,CaptureCalls& calls,IDirect3DVertexShader9* shader,
                        unsigned vertexCount,unsigned indexCount,const std::vector<unsigned char>& source,bool gpuTiming){
    SavedState original(d);assert(original.ok);
    for(unsigned stage=1;stage<16;++stage)bind(stage,nullptr);
    for(unsigned stage=0;stage<4;++stage)ok(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+stage,nullptr));
    for(unsigned stage=1;stage<4;++stage)ok(d->SetRenderTarget(stage,nullptr));
    constexpr unsigned W=3840,H=2160,N=640;
    IDirect3DSurface9 *color=nullptr,*depth=nullptr;
    ok(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_2_SAMPLES,nullptr));
    ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&color,nullptr));
    ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&depth,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,color));ok(d->SetDepthStencilSurface(depth));
    D3DVIEWPORT9 viewport{0,0,W,H,0,1};ok(d->SetViewport(&viewport));
    float constants[256][4];ok(d->GetVertexShaderConstantF(0,constants[0],256));
    // A detailed scene resists render-target compression, as the actual game
    // does. A black clear alone conceals the cost of interrupting its MSAA pass.
    IDirect3DTexture9* noise=nullptr;ok(d->CreateTexture(512,512,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&noise,nullptr));
    D3DLOCKED_RECT lock{};ok(noise->LockRect(0,&lock,nullptr,0));unsigned random=12345;
    for(unsigned y=0;y<512;++y)for(unsigned x=0;x<512;++x){random=random*1664525+1013904223;reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=random|0xff000000;}
    ok(noise->UnlockRect(0));
    auto background=[&]{SavedState saved(d);assert(saved.ok);ok(d->SetPixelShader(nullptr));bind(0,noise);
        ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE));
        ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));Target t;t.w=W;t.h=H;quad(t);};
    auto bytes=source;auto* vertices=bytes.data()+12;auto* indices=vertices+vertexCount*48;
    const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,vertexCount,0,indexCount/3,true};
    auto draw=[&]{return d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,vertexCount,indexCount/3,indices,D3DFMT_INDEX32,vertices,48);};
    IDirect3DQuery9 *begin=nullptr,*end=nullptr,*frequency=nullptr;
    if(gpuTiming){ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));}
    auto wait=[](IDirect3DQuery9* q){UINT64 value=0;const DWORD start=GetTickCount();HRESULT hr;
        while((hr=q->GetData(&value,sizeof value,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-start<10000);Sleep(0);}ok(hr);return value;};
    auto normals=std::make_unique<NorthlightModelNormals>(&calls);
    std::vector<unsigned char> expected;
    std::vector<double> times[2],cpu[2];unsigned targetSets[2]={};
    // ABBA; two warm rounds and four retained pairs. Native and captured output
    // both use each draw's current constants and current private source bytes.
    for(unsigned round=0;round<6;++round)for(bool batch:{false,true,true,false}){
        normals->endFrame();normals->setDeferredCapture(batch);
        ok(d->SetVertexShaderConstantF(0,constants[0],256));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
        background();
        if(gpuTiming){ok(frequency->Issue(D3DISSUE_END));ok(begin->Issue(D3DISSUE_END));}LARGE_INTEGER t0{},t1{},hz{};QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&t0);
        const unsigned sets=calls.targetSets;
        ok(d->BeginScene());
        for(unsigned i=0;i<N;++i){
            auto pose=constants[31];const float old=pose[3];pose[3]=old+float(i%7)*.017f;
            ok(d->SetVertexShaderConstantF(31,pose,1));pose[3]=old;
            bytes=source;
            float x;std::memcpy(&x,vertices,4);x+=float(i%3)*.001f;std::memcpy(vertices,&x,4);
            normals->capture(shader,draw,true,&geometry,indices,D3DFMT_INDEX32,vertices,48);
            ok(draw());
            // Reusing an UP array after capture must never change its replay.
            std::fill(bytes.begin()+12,bytes.end(),0);
        }
        if(batch)assert(normals->pendingTextures());
        assert(normals->finishCaptures());ok(d->EndScene());
        QueryPerformanceCounter(&t1);double gpuMs=0;
        if(gpuTiming){ok(end->Issue(D3DISSUE_END));ok(d->Present(nullptr,nullptr,nullptr,nullptr));const auto b=wait(begin),e=wait(end),f=wait(frequency);gpuMs=double(e-b)*1000/double(f);}
        if(round>=2){times[batch].push_back(gpuMs);cpu[batch].push_back(double(t1.QuadPart-t0.QuadPart)*1000/hz.QuadPart);}
        targetSets[batch]=calls.targetSets-sets;
        assert(normals->texture());
        if(round==0){auto image=capturedPixels(normals->texture(),W,H);if(!batch)expected=std::move(image);else assert(expected==image);}
    }
    for(auto& a:times)std::sort(a.begin(),a.end());for(auto& a:cpu)std::sort(a.begin(),a.end());
    assert(targetSets[1]<targetSets[0]/22); // Bound full-resolution MSAA interruptions, including full-batch transitions.
    std::printf("MODEL_NORMALS_BATCH_PASS pixels=exact draws=%u width=%u height=%u msaa=2 targetSets=%u/%u gpuMs=%.6f/%.6f cpuMs=%.6f/%.6f order=ABBA gpuTiming=%u\n",
        N,W,H,targetSets[0],targetSets[1],times[0][times[0].size()/2],times[1][times[1].size()/2],cpu[0][cpu[0].size()/2],cpu[1][cpu[1].size()/2],unsigned(gpuTiming));
    // All four proxy entry points, including source-buffer writes after capture.
    std::vector<unsigned char> expanded(indexCount*48);
    for(unsigned i=0;i<indexCount;++i){unsigned index;std::memcpy(&index,source.data()+12+vertexCount*48+4*i,4);std::memcpy(expanded.data()+i*48,source.data()+12+index*48,48);}
    IDirect3DVertexBuffer9* vertexBuffer=nullptr;IDirect3DIndexBuffer9* indexBuffer=nullptr;
    ok(d->CreateVertexBuffer(UINT(expanded.size()),0,0,D3DPOOL_MANAGED,&vertexBuffer,nullptr));
    ok(d->CreateIndexBuffer(indexCount*4,0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&indexBuffer,nullptr));
    NorthlightDrawSnapshot::Frame peer{8u<<20,0};
    const auto identity=+[](void* p,bool)->uint64_t{return reinterpret_cast<uintptr_t>(p);};
    const auto version=+[](void*,bool)->uint64_t{return normalSourceRevision;};
    peer.setIdentityProvider(identity);peer.setVersionProvider(version);peer.setSnapshotCacheLimit(8u<<20);
    assert(normals->shareCaptureSources(peer,identity,version,nullptr));
    NorthlightGpuScene::NativeStore store;NorthlightGpuScene::NativeClient cache(store,NorthlightGpuScene::Consumer::Material);
    uint64_t cacheFrame=1;store.advance(cacheFrame);normals->setGeometryCache(&cache);
    normals->setRasterStateSource(&calls,[](void* context,const D3DRENDERSTATETYPE* keys,unsigned count,DWORD* out){
        auto& calls=*static_cast<CaptureCalls*>(static_cast<IDirect3DDevice9*>(context));++calls.bulkReads;
        if(calls.bulkUnknown){++calls.bulkMisses;return false;}
        for(unsigned i=0;i<count;++i)if(FAILED(d->GetRenderState(keys[i],out+i)))return false;
        return true;
    });
    for(unsigned mode=0;mode<4;++mode){
        calls.bulkUnknown=bool(mode&1);
        std::vector<unsigned char> reference;
        for(bool batch:{false,true}){
            normals->endFrame();normals->setDeferredCapture(batch);bytes=source;
            const bool indexed=mode<2,buffered=mode&1;
            const auto* data=indexed?vertices:expanded.data();const unsigned count=indexed?vertexCount:indexCount;
            void* memory=nullptr;ok(vertexBuffer->Lock(0,count*48,&memory,0));std::memcpy(memory,data,count*48);ok(vertexBuffer->Unlock());
            ok(indexBuffer->Lock(0,indexCount*4,&memory,0));std::memcpy(memory,indices,indexCount*4);ok(indexBuffer->Unlock());
            ok(d->SetStreamSource(0,vertexBuffer,0,48));ok(d->SetIndices(indexBuffer));
            auto submit=[&]{return buffered?(indexed?d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,count,0,indexCount/3):d->DrawPrimitive(D3DPT_TRIANGLELIST,0,indexCount/3)):
                indexed?draw():d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,indexCount/3,data,48);};
            const NorthlightDrawSnapshot::Draw args{D3DPT_TRIANGLELIST,0,0,count,0,indexCount/3,indexed};
            ++normalSourceRevision;peer.clearFrame();
            NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> handedDeclaration;std::shared_ptr<const NorthlightDrawSnapshot::Mesh> handed;
            if(buffered||mode==0){ok(d->GetVertexDeclaration(handedDeclaration.out()));NorthlightDrawSnapshot::Mesh local;
                if(buffered)assert(peer.read(d,handedDeclaration.p,args,local,nullptr,false,&handed)&&handed);
                else {assert(peer.readUP(handedDeclaration.p,args,indices,D3DFMT_INDEX32,data,48,local));handed=peer.retain(std::move(local));}}
            const bool handoff=mode<2;
            ok(d->BeginScene());normals->capture(shader,submit,!buffered,&args,indexed?indices:nullptr,D3DFMT_INDEX32,buffered?nullptr:data,48,handoff?&handed:nullptr,handoff?handedDeclaration.p:nullptr);
            if(batch)assert(normals->pendingTextures());
            if(batch&&buffered){assert(normals->reusedSources()==1);if(mode==1)peer.clearSnapshotCache();}
            ++normalSourceRevision;
            ok(vertexBuffer->Lock(0,count*48,&memory,0));std::memset(memory,0,count*48);ok(vertexBuffer->Unlock());
            ok(indexBuffer->Lock(0,indexCount*4,&memory,0));std::memset(memory,0,indexCount*4);ok(indexBuffer->Unlock());
            assert(normals->finishCaptures());ok(d->EndScene());
            auto image=capturedPixels(normals->texture(),W,H);if(batch)assert(image==reference);else reference=std::move(image);
            if(batch&&buffered){
                normals->endFrame();ok(d->BeginScene());normals->capture(shader,submit,false,&args,nullptr,D3DFMT_UNKNOWN,nullptr,0,&handed,nullptr); // Mismatched handoff must use current buffers.
                assert(normals->reusedSources()==0&&normals->finishCaptures());ok(d->EndScene());
                auto empty=capturedPixels(normals->texture(),W,H);assert(std::all_of(empty.begin(),empty.end(),[](unsigned char x){return x==0;}));
                // No peer populated the current revision: the normal consumer
                // must retain it itself, then read zero buffer bytes next frame.
                assert(normals->readSourceBytes()>0);
                ++normalSourceRevision;
                ok(vertexBuffer->Lock(0,count*48,&memory,0));std::memcpy(memory,data,count*48);ok(vertexBuffer->Unlock());
                ok(indexBuffer->Lock(0,indexCount*4,&memory,0));std::memcpy(memory,indices,indexCount*4);ok(indexBuffer->Unlock());
                for(unsigned repeat=0;repeat<3;++repeat){
                    if(repeat==2){normals->reset();cache.clear();}else normals->endFrame();
                    store.advance(++cacheFrame);
                    ok(d->BeginScene());normals->capture(shader,submit,false,&args);
                    assert(normals->reusedSources()==unsigned(repeat==1));
                    assert((normals->readSourceBytes()==0)==(repeat==1));
                    assert(normals->finishCaptures());ok(d->EndScene());
                    assert(normals->cachedDraws()==unsigned(repeat==1));
                    assert(capturedPixels(normals->texture(),W,H)==reference);
                    assert(peer.sharedSourceBytes()<=peer.sharedSourceLimit());
                    if(repeat==1&&mode==1){
                        NorthlightDrawSnapshot::Mesh local;std::shared_ptr<const NorthlightDrawSnapshot::Mesh> direct;
                        assert(peer.read(d,handedDeclaration.p,args,local,nullptr,false,&direct)&&direct);
                        float palette[4];ok(d->GetVertexShaderConstantF(31,palette,1));
                        constexpr unsigned crowd=1200;
                        std::vector<double> gpu[3],cpuCost[3];std::vector<unsigned char> referenceCrowd;unsigned crowdSets[3]={};
                        for(unsigned round=0;round<(gpuTiming?6u:1u);++round)for(unsigned strategy:{0u,1u,2u,2u,1u,0u}){
                            normals->endFrame();normals->setGeometryCache(strategy?&cache:nullptr);store.advance(++cacheFrame);
                            ok(d->SetVertexShaderConstantF(31,palette,1));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));background();
                            if(gpuTiming){ok(frequency->Issue(D3DISSUE_END));ok(begin->Issue(D3DISSUE_END));}
                            LARGE_INTEGER start{},finish{},hz{};QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&start);const auto sets=calls.targetSets;
                            ok(d->BeginScene());
                            for(unsigned i=0;i<crowd;++i){float pose[4];std::copy(palette,palette+4,pose);pose[3]+=float(i%7)*.017f;ok(d->SetVertexShaderConstantF(31,pose,1));
                                normals->capture(shader,submit,false,&args,nullptr,D3DFMT_UNKNOWN,nullptr,0,strategy==2?&direct:nullptr,strategy==2?handedDeclaration.p:nullptr);ok(submit());}
                            assert(normals->finishCaptures());ok(d->EndScene());QueryPerformanceCounter(&finish);double gpuMs=0;
                            if(gpuTiming){ok(end->Issue(D3DISSUE_END));ok(d->Present(nullptr,nullptr,nullptr,nullptr));const auto b=wait(begin),e=wait(end),f=wait(frequency);gpuMs=double(e-b)*1000/double(f);}
                            if(round>=2||!gpuTiming){gpu[strategy].push_back(gpuMs);cpuCost[strategy].push_back(double(finish.QuadPart-start.QuadPart)*1000/hz.QuadPart);}
                            crowdSets[strategy]=calls.targetSets-sets;
                            assert(normals->cachedDraws()==(strategy?crowd:0)&&cache.uploaded()==0&&normals->readSourceBytes()==0);
                            if(strategy)assert(crowdSets[strategy]==11); // One pass under the unchanged 8 MiB CPU budget.
                            if(round==0){auto image=capturedPixels(normals->texture(),W,H);if(referenceCrowd.empty())referenceCrowd=std::move(image);else assert(image==referenceCrowd);}
                        }
                        ok(d->SetVertexShaderConstantF(31,palette,1));normals->setGeometryCache(&cache);
                        for(unsigned i=0;i<3;++i){std::sort(gpu[i].begin(),gpu[i].end());std::sort(cpuCost[i].begin(),cpuCost[i].end());}
                        std::printf("MODEL_NORMALS_RESIDENCY_BENCH draws=%u pixels=exact uploadedBytes=0 strategies=UP/resident/handoff targetSets=%u/%u/%u gpuMs=%.6f/%.6f/%.6f cpuMs=%.6f/%.6f/%.6f gpuTiming=%u\n",crowd,
                            crowdSets[0],crowdSets[1],crowdSets[2],gpu[0][gpu[0].size()/2],gpu[1][gpu[1].size()/2],gpu[2][gpu[2].size()/2],cpuCost[0][cpuCost[0].size()/2],cpuCost[1][cpuCost[1].size()/2],cpuCost[2][cpuCost[2].size()/2],unsigned(gpuTiming));
                        normals->endFrame();ok(d->BeginScene());normals->capture(shader,submit,false,&args);
                        cache.clear(); // A queued resident draw owns its buffers even after eviction.
                        assert(normals->finishCaptures()&&normals->cachedDraws()==1);ok(d->EndScene());
                        assert(capturedPixels(normals->texture(),W,H)==reference);
                    }
                }
            }
        }
    }
    assert(calls.bulkReads>calls.bulkMisses&&calls.bulkMisses>0);calls.bulkUnknown=false;
    std::puts("MODEL_NORMALS_BATCH_STATE_PASS known=exact unknown=fallback");
    vertexBuffer->Release();indexBuffer->Release();
    std::puts("MODEL_NORMALS_BATCH_SOURCE_PASS inputs=indexedUP/indexedBuffer/primitiveUP/primitiveBuffer mutation=exact shared=tracked eviction=exact stale=rejected independent=zero_reads trim=cold gpu_resident=exact handoff=exact");
    normals->endFrame();assert(!normals->texture()&&!normals->pendingTextures());normals.reset();
    if(begin)begin->Release();if(end)end->Release();if(frequency)frequency->Release();color->Release();depth->Release();noise->Release();
}
