// Same stock M2 and exact pixels, comparing r46's per-mesh quota with grouped
// admission. No timing assertion: this oracle also runs alongside the game.
struct NormalColdResult {std::vector<unsigned char> image;unsigned resident=0,indexed=0,up=0;size_t uploaded=0,upBytes=0;double createMs=0;};
template<class Store,class Client,class Normals,class Consumer>
static NormalColdResult normalColdTrial(Consumer consumer,IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* declaration,
        unsigned vertexCount,unsigned indexCount,const std::vector<unsigned char>& source){
    SavedState saved(d);assert(saved.ok);constexpr unsigned N=128,W=384,H=288;
    auto color=target(W,H,D3DFMT_A8R8G8B8,4);IDirect3DSurface9* depth=nullptr;
    ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));
    ok(d->SetRenderTarget(0,color.s));ok(d->SetDepthStencilSurface(depth));viewport(color);
    ok(d->SetVertexDeclaration(declaration));ok(d->SetVertexShader(shader));
    for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE})ok(d->SetRenderState(key,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
    ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> meshes;const auto primitives=indexCount/3;assert(primitives>=N);
    for(unsigned i=0;i<N;++i){auto m=std::make_shared<NorthlightDrawSnapshot::Mesh>();m->indexed=true;m->topology=D3DPT_TRIANGLELIST;
        m->vertexCount=vertexCount;m->primitiveCount=(i+1)*primitives/N-i*primitives/N;m->streams[0].stride=48;
        m->streams[0].bytes.assign(source.begin()+12,source.begin()+12+vertexCount*48);m->indices.resize(m->primitiveCount*3);
        std::memcpy(m->indices.data(),source.data()+12+vertexCount*48+(i*primitives/N)*12,m->indices.size()*4);
        m->sourceIdentity=NorthlightSourceIdentity::make({0xc01du,i+1});m->sharedCapture=true;meshes.push_back(std::move(m));}
    struct Calls:ForwardIDirect3DDevice9{unsigned indexed=0,up=0;size_t upBytes=0;using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
        HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT b,UINT m,UINT v,UINT s,UINT c)override{++indexed;return real->DrawIndexedPrimitive(t,b,m,v,s,c);}
        HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT m,UINT v,UINT c,const void* indices,D3DFORMAT f,const void* data,UINT stride)override{
            ++up;upBytes+=size_t(v)*stride+size_t(c)*12;return real->DrawIndexedPrimitiveUP(t,m,v,c,indices,f,data,stride);}} calls(d);
    Store store;Client cache(store,consumer);auto normals=std::make_unique<Normals>(&calls);normals->setGeometryCache(&cache);normals->setDeferredCapture(true);
    NormalColdResult result;std::vector<unsigned char> first;
    for(unsigned frame=0;frame<2;++frame){normals->endFrame();store.setFrame(frame+1);calls.indexed=calls.up=0;calls.upBytes=0;
        ok(d->BeginScene());for(const auto& mesh:meshes){NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,vertexCount,0,mesh->primitiveCount,true};
            normals->capture(shader,[]{return D3D_OK;},true,&geometry,nullptr,D3DFMT_UNKNOWN,nullptr,0,&mesh,declaration);}
        assert(normals->testPendingPackets()==N);assert(normals->finishCaptures());ok(d->EndScene());
        result.image=capturedPixels(normals->texture(),W,H);assert(std::any_of(result.image.begin(),result.image.end(),[](unsigned char x){return x!=0;}));
        if(frame==0)first=result.image;else assert(result.image==first);
        result.resident=normals->cachedDraws();result.indexed=calls.indexed;result.up=calls.up;result.upBytes=calls.upBytes;result.uploaded=cache.uploaded();result.createMs=cache.stats().createMs;
        assert(result.uploaded<=4u*1024u*1024u&&cache.chargedBytes()<=cache.limit());
    }
    normals.reset();store.clearAll();depth->Release();color.read->Release();color.s->Release();color.t->Release();return result;
}
static void normalColdAdmission(IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* declaration,
        unsigned vertexCount,unsigned indexCount,const std::vector<unsigned char>& source){
    const double budget=NorthlightReplayGPU::createBudgetMs();NorthlightReplayGPU::createBudgetMs()=1e6;
    auto before=normalColdTrial<NorthlightGpuSceneReference::NativeStore,NorthlightGpuSceneReference::NativeClient,NorthlightModelNormalsReference>(NorthlightGpuSceneReference::Consumer::Material,shader,declaration,vertexCount,indexCount,source);
    auto after=normalColdTrial<NorthlightGpuScene::NativeStore,NorthlightGpuScene::NativeClient,NorthlightModelNormals>(NorthlightGpuScene::Consumer::Material,shader,declaration,vertexCount,indexCount,source);
    NorthlightReplayGPU::createBudgetMs()=budget;
    auto bounded=normalColdTrial<NorthlightGpuScene::NativeStore,NorthlightGpuScene::NativeClient,NorthlightModelNormals>(NorthlightGpuScene::Consumer::Material,shader,declaration,vertexCount,indexCount,source);
    assert(before.image==after.image&&before.resident==16&&before.up==112);
    assert(after.resident==128&&after.up==0&&after.indexed==8);
    assert(bounded.image==before.image&&bounded.resident+bounded.up==128);
    std::printf("MODEL_NORMALS_COLD_ADMISSION_PASS pixels=bitexact resident=%u/%u indexed=%u/%u UP=%u/%u upBytes=%zu/%zu cacheUpload=%zu/%zu limitMiB=4 createMs=%.6f/%.6f boundedResident=%u boundedUP=%u boundedCreateMs=%.6f budgetMs=%.6f\n",
        before.resident,after.resident,before.indexed,after.indexed,before.up,after.up,before.upBytes,after.upBytes,before.uploaded,after.uploaded,before.createMs,after.createMs,bounded.resident,bounded.up,bounded.createMs,budget);
}
