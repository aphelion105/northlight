// Real capture and replay, partitioning a stock M2 into adjacent indexed draws.
struct ModelNormalMergeTest {
    using Normals=NorthlightModelNormals;
    static void controls(Normals& n){
        assert(n.pending.size()==16);auto& p=*n.pending[1];
        auto packets=[&]{size_t at=0;return n.drawRun(at).packets;};
        assert(packets()==16);unsigned breaks=0;
        auto change=[&](auto& value,auto replacement){const auto saved=value;value=replacement;assert(packets()==1);value=saved;assert(packets()==16);++breaks;};
        change(p.resident,false);change(p.placement.absoluteIndices,false);
        change(p.placement.indexStart,p.placement.indexStart+1);
        change(p.mesh.indexed,false);change(p.mesh.topology,D3DPT_TRIANGLESTRIP);
        change(p.mesh.streams[0].stride,p.mesh.streams[0].stride+4);
        change(p.mesh.vertexCount,UINT_MAX);change(p.viewport.Width,p.viewport.Width-1);
        change(p.raster[0],p.raster[0]^1u);
        change(p.gpuVertex[0],static_cast<IDirect3DVertexBuffer9*>(nullptr));
        change(p.gpuIndex,static_cast<IDirect3DIndexBuffer9*>(nullptr));
        change(p.declaration,static_cast<IDirect3DVertexDeclaration9*>(nullptr));
        auto vertex=*p.vertex;vertex.shader=nullptr;change(p.vertex,&vertex);
        auto pixel=*p.pixel;change(p.pixel,&pixel);
        std::vector<float> bank(1024);std::memcpy(bank.data()+p.vertex->first*4,p.constants+p.vertex->first*4,p.vertex->count*16);const auto* saved=p.constants;p.constants=bank.data();
        change(bank[p.vertex->first*4],bank[p.vertex->first*4]+1.f);p.constants=saved;
        change(p.ps.back(),p.ps.back()+1.f);
        change(n.primitiveLimit,n.pending[0]->geometry().primitiveCount);
        // All integer/boolean banks and sampled inputs participate in equality.
        const auto vr=p.vertex->reads,pr=p.pixel->reads;
        assert(!vr.i&&!vr.b&&!pr.i&&!pr.b);
        for(auto& q:n.pending){std::fill_n(q->intStorage,64,0);std::fill_n(q->boolStorage,16,FALSE);
            std::fill_n(q->pi,64,0);std::fill_n(q->pb,16,FALSE);q->ints=q->intStorage;q->bools=q->boolStorage;}
        p.vertex->reads.i=p.vertex->reads.b=p.pixel->reads.i=p.pixel->reads.b=1;
        change(p.intStorage[0],1);change(p.boolStorage[0],BOOL(TRUE));change(p.pi[0],1);change(p.pb[0],BOOL(TRUE));
        p.vertex->reads=vr;p.pixel->reads=pr;
        const unsigned slot=pr.samplers?unsigned(__builtin_ctz(pr.samplers)):0;
        if(!pr.samplers){for(auto& q:n.pending)std::fill_n(q->samplers[slot],13,0);p.pixel->reads.samplers=1u<<slot;}
        change(p.samplers[slot][0],p.samplers[slot][0]^1u);
        const auto texture=p.textures[slot];p.textures[slot]=reinterpret_cast<IDirect3DBaseTexture9*>(uintptr_t(1));assert(packets()==1);++breaks;p.textures[slot]=texture;
        p.pixel->reads=pr;assert(packets()==16);
        std::printf("MODEL_NORMALS_MERGE_CONTROLS_PASS breaks=%u\n",breaks);
    }
    static void sharing(Normals& n,bool value){n.pixelSharingEnabled=value;n.pixelCanonicalEnabled=value;}
    static void programs(const Normals& n,unsigned built,unsigned reused){assert(n.pixelBuilt==built&&n.pixelReused==reused);}
    static void programControls(IDirect3DDevice9* d,IDirect3DPixelShader9* source,const std::vector<std::uint32_t>& native){
        Normals n(d);auto* owner=n.pixel(source,false);assert(owner&&owner->shader);
        auto different=native;different.insert(different.end()-1,{0x01000041u,NorthlightWaterShaderPatch::dst(0,0)});
        IDirect3DPixelShader9* distinct=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(different.data()),&distinct));
        std::vector<std::uint32_t> full,slice;unsigned control=0;
        assert(NorthlightNativeMaterialCapture::patch(different.data(),different.size(),full,control,false,false,true));
        const bool compact=NorthlightModelNormalSlice::pixel(full.data(),full.size(),slice);if(compact){full.swap(slice);
            if(NorthlightModelNormalSlice::canonicalPixel(full.data(),full.size(),control,slice)){full.swap(slice);control=0;}}
        const auto saved=*owner;owner->control=control;owner->compactVertex=compact;
        owner->programBytes=UINT(full.size()*4);owner->programHash=NorthlightShaderTags::fnv1a(full.data(),full.size()*4);
        auto* collision=n.pixel(distinct,false);assert(collision&&collision->shader&&collision!=owner&&!collision->equivalent);
        *owner=saved;distinct->Release();
        // Identical bytecode with a different control binding or VS contract
        // cannot alias even if its prefilter and exact bytes match.
        for(unsigned contract=0;contract<2;++contract){auto aliasNative=native;
            aliasNative.insert(aliasNative.end()-1,{0x02000001u,NorthlightWaterShaderPatch::dst(8,0,7),NorthlightWaterShaderPatch::src(2,20+contract)});
            IDirect3DPixelShader9* alias=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(aliasNative.data()),&alias));
            if(contract)owner->compactVertex=!owner->compactVertex;else ++owner->control;
            auto* isolated=n.pixel(alias,false);assert(isolated&&isolated->shader&&isolated!=owner);
            *owner=saved;alias->Release();
        }
        std::puts("MODEL_NORMALS_PROGRAM_CONTROLS_PASS hashCollision=exact_bytes controlBinding=isolated vertexContract=isolated");
    }
    static void nativeVertexSources(const Normals& n,const std::vector<IDirect3DVertexShader9*>& sources){for(size_t i=0;i<n.pending.size();++i)assert(n.pending[i]->vertex->source==sources[i%sources.size()]);}
    static unsigned submitted(const Normals& n){return n.draws-n.mergedDraws;}
    static void enable(Normals& n,bool value){n.mergeEnabled=value;}
    static void dedup(Normals& n,bool value){n.deduplicateEnabled=value;}
    static unsigned duplicates(const Normals& n){return n.duplicateDraws;}
    static void duplicateControls(Normals& n,unsigned expected){
        auto count=[&]{for(auto& q:n.pending)q->redundant=false;n.findRedundant();unsigned total=0;for(auto& q:n.pending)total+=q->redundant;return total;};
        assert(count()==expected);auto& p=*n.pending[0];assert(p.resident&&!p.redundant&&!n.pending.back()->redundant);
        unsigned checks=0;auto change=[&](auto& value,auto replacement){const auto old=value;value=replacement;assert(count()==expected-1&&!p.redundant);value=old;assert(count()==expected);++checks;};
        change(p.placement.vertexBase,p.placement.vertexBase+1);change(p.placement.indexStart,p.placement.indexStart+1);
        change(p.placement.absoluteIndices,!p.placement.absoluteIndices);change(p.mesh.vertexCount,p.mesh.vertexCount-1);
        change(p.mesh.primitiveCount,p.mesh.primitiveCount-1);change(p.mesh.streams[0].stride,p.mesh.streams[0].stride+4);
        change(p.mesh.topology,D3DPT_TRIANGLESTRIP);change(p.mesh.indexed,false);
        change(p.gpuVertex[0],static_cast<IDirect3DVertexBuffer9*>(nullptr));change(p.gpuIndex,static_cast<IDirect3DIndexBuffer9*>(nullptr));
        change(p.viewport.Width,p.viewport.Width-1);change(p.raster[0],p.raster[0]^1u);change(p.ps.back(),p.ps.back()+1.f);
        std::vector<float> bank(1024);std::memcpy(bank.data()+p.vertex->first*4,p.constants+p.vertex->first*4,p.vertex->count*16);
        const auto* old=p.constants;p.constants=bank.data();change(bank[p.vertex->first*4],bank[p.vertex->first*4]+1.f);p.constants=old;
        std::printf("MODEL_NORMALS_DUPLICATE_CONTROLS_PASS checks=%u\n",checks);
    }
};

static void normalMerge(IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* declaration,
        unsigned vertexCount,unsigned indexCount,const std::vector<unsigned char>& source,const std::string& directory){
    SavedState saved(d);assert(saved.ok);
    constexpr unsigned N=16,W=384,H=288;
    auto color=target(W,H,D3DFMT_A8R8G8B8,4);IDirect3DSurface9* depth=nullptr;
    ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));
    ok(d->SetRenderTarget(0,color.s));ok(d->SetDepthStencilSurface(depth));viewport(color);
    ok(d->SetVertexDeclaration(declaration));ok(d->SetVertexShader(shader));auto* pixel=ps(directory+"ModelPS.bin");ok(d->SetPixelShader(pixel));
    for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE})ok(d->SetRenderState(key,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
    ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> meshes;
    const auto primitives=indexCount/3;assert(primitives>=N);
    for(unsigned i=0;i<N;++i){auto m=std::make_shared<NorthlightDrawSnapshot::Mesh>();m->indexed=true;m->topology=D3DPT_TRIANGLELIST;
        m->vertexCount=vertexCount;m->primitiveCount=(i+1)*primitives/N-i*primitives/N;m->streams[0].stride=48;
        m->streams[0].bytes.assign(source.begin()+12,source.begin()+12+vertexCount*48);m->indices.resize(m->primitiveCount*3);
        std::memcpy(m->indices.data(),source.data()+12+vertexCount*48+(i*primitives/N)*12,m->indices.size()*4);
        m->sourceIdentity=NorthlightSourceIdentity::make({0xfeedu,i+1});m->sharedCapture=true;meshes.push_back(std::move(m));}
    struct Calls:ForwardIDirect3DDevice9{unsigned submitted=0,vertexCreates=0;using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
        HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out)override{++vertexCreates;return real->CreateVertexShader(code,out);}
        HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT b,UINT m,UINT v,UINT s,UINT c)override{++submitted;return real->DrawIndexedPrimitive(t,b,m,v,s,c);}} calls(d);
    NorthlightGpuScene::NativeStore store;NorthlightGpuScene::NativeClient cache(store,NorthlightGpuScene::Consumer::Material);
    auto normals=std::make_unique<NorthlightModelNormals>(&calls);normals->setGeometryCache(&cache);normals->setDeferredCapture(true);
    std::vector<unsigned char> reference;unsigned submitted[2]={};
    for(unsigned frame=0;frame<6;++frame){const bool merged=frame>=3;normals->endFrame();store.setFrame(frame+1);ModelNormalMergeTest::enable(*normals,merged);calls.submitted=0;
        ok(d->BeginScene());for(const auto& mesh:meshes){NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,vertexCount,0,mesh->primitiveCount,true};
            normals->capture(shader,[]{return D3D_OK;},true,&geometry,nullptr,D3DFMT_UNKNOWN,nullptr,0,&mesh,declaration);}
        assert(normals->testPendingPackets()==N);
        if(frame==4)ModelNormalMergeTest::controls(*normals);
        assert(normals->finishCaptures());ok(d->EndScene());assert(normals->texture());
        const auto image=capturedPixels(normals->texture(),W,H);assert(std::any_of(image.begin(),image.end(),[](unsigned char x){return x!=0;}));
        if(frame==0)reference=image;else assert(image==reference);
        if(frame>=2){assert(normals->cachedDraws()==N);assert(calls.submitted==(merged?1u:N));assert(ModelNormalMergeTest::submitted(*normals)==calls.submitted);submitted[merged]=calls.submitted;}}
    std::printf("MODEL_NORMALS_MERGE_GPU_PASS pixels=bitexact draws=%u/%u width=%u height=%u\n",submitted[0],submitted[1],W,H);
    // Eight genuinely different native RGB programs reduce to identical
    // receiver code. Alternating them used to split an otherwise adjacent run.
    std::vector<IDirect3DPixelShader9*> variants;
    const auto nativeBytes=read(directory+"ModelPS.bin");std::vector<std::uint32_t> native(nativeBytes.size()/4);
    std::memcpy(native.data(),nativeBytes.data(),nativeBytes.size());assert(native.back()==65535);
    ModelNormalMergeTest::programControls(d,pixel,native);
    for(unsigned i=0;i<8;++i){auto changed=native;
        changed.insert(changed.begin()+1,{0x05000051u,NorthlightWaterShaderPatch::dst(2,i%2?0:187),0x3f000000u+i,0,0,0});
        changed.insert(changed.begin()+1,{0x0200001fu,0x80010005u,NorthlightWaterShaderPatch::dst(1,3+i%4)});
        changed.insert(changed.end()-1,{0x02000001u,NorthlightWaterShaderPatch::dst(8,0,7),NorthlightWaterShaderPatch::src(2,i)});
        IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(changed.data()),&shader));variants.push_back(shader);}
    std::unique_ptr<NorthlightModelNormals> programNormals[2];
    for(unsigned mode=0;mode<2;++mode){programNormals[mode]=std::make_unique<NorthlightModelNormals>(&calls);
        programNormals[mode]->setGeometryCache(&cache);programNormals[mode]->setDeferredCapture(true);ModelNormalMergeTest::sharing(*programNormals[mode],mode!=0);}
    std::vector<double> sharingTimes[2];unsigned sharingCalls[2]={};
    for(unsigned frame=0;frame<22;++frame){const unsigned mode=frame%2;auto& n=*programNormals[mode];
        if(frame==2||frame==3)n.reset(true); // Cache aliases survive target/resource reset.
        n.endFrame();store.setFrame(100+frame);calls.submitted=0;ok(d->BeginScene());
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<N;++i){const auto& mesh=meshes[i];ok(d->SetPixelShader(variants[i%variants.size()]));
            const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,vertexCount,0,mesh->primitiveCount,true};
            n.capture(shader,[]{return D3D_OK;},true,&geometry,nullptr,D3DFMT_UNKNOWN,nullptr,0,&mesh,declaration);}
        assert(n.testPendingPackets()==N);assert(n.finishCaptures());
        if(frame>=4)sharingTimes[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        ok(d->EndScene());assert(capturedPixels(n.texture(),W,H)==reference);
        IDirect3DPixelShader9* restored=nullptr;ok(d->GetPixelShader(&restored));assert(restored==variants.back());restored->Release();
        ModelNormalMergeTest::programs(n,mode?1:8,mode?7:0);
        assert(calls.submitted==(mode?1u:N));sharingCalls[mode]=calls.submitted;
    }
    for(auto& t:sharingTimes)std::sort(t.begin(),t.end());
    std::printf("MODEL_NORMALS_PROGRAM_SHARING_PASS pixels=bitexact nativePrograms=8 capturePrograms=8/1 draws=%u/%u reset=preserved restored=exact\n",sharingCalls[0],sharingCalls[1]);
    std::printf("MODEL_NORMALS_PROGRAM_SHARING_CPU captureReplayMedianMs=%.6f/%.6f alternatingPairs=9 scope=component_submission\n",sharingTimes[0][4],sharingTimes[1][4]);
    for(auto& n:programNormals)n.reset();for(auto* p:variants)p->Release();ok(d->SetPixelShader(pixel));
    UINT nativeSize=0;ok(shader->GetFunction(nullptr,&nativeSize));std::vector<std::uint32_t> originalVS(nativeSize/4);ok(shader->GetFunction(originalVS.data(),&nativeSize));
    if(originalVS[0]==0xfffe0300u){
        std::vector<NorthlightModelNormalSlice::Op> operations;assert(NorthlightModelNormalSlice::parse(originalVS.data(),originalVS.size(),true,operations));
        std::vector<IDirect3DVertexShader9*> sources;
        for(unsigned i=0;i<8;++i){auto changed=originalVS;
            for(const auto& op:operations)if(op.code!=81&&op.code!=48&&op.code!=47)
                for(unsigned k=op.code==31?2:1;k<=op.count;++k){auto& token=changed[op.at+k];const unsigned type=NorthlightWaterShaderPatch::regType(token),reg=NorthlightWaterShaderPatch::regIndex(token);
                    const unsigned mapped=type==0?(reg+i+1)%32:type==1?(reg+i+1)%16:type==6?(reg+i+1)%12:reg;token=(token&~2047u)|mapped;}
            IDirect3DVertexShader9* source=nullptr;ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(changed.data()),&source));sources.push_back(source);}
        auto vertexNormals=std::make_unique<NorthlightModelNormals>(&calls);vertexNormals->setGeometryCache(&cache);vertexNormals->setDeferredCapture(true);
        calls.vertexCreates=0;std::vector<double> times;unsigned submitted=0;
        const bool shared=std::getenv("NORTHLIGHT_TEST_VERTEX_SHARING")!=nullptr;
        for(unsigned frame=0;frame<13;++frame){vertexNormals->endFrame();store.setFrame(150+frame);calls.submitted=0;ok(d->BeginScene());
            const auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<N;++i){const auto& mesh=meshes[i];auto* source=sources[i%sources.size()];ok(d->SetVertexShader(source));
                const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,vertexCount,0,mesh->primitiveCount,true};
                vertexNormals->capture(source,[]{return D3D_OK;},true,&geometry,nullptr,D3DFMT_UNKNOWN,nullptr,0,&mesh,declaration);}
            ModelNormalMergeTest::nativeVertexSources(*vertexNormals,sources);assert(vertexNormals->finishCaptures());
            if(frame>=4)times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
            ok(d->EndScene());assert(capturedPixels(vertexNormals->texture(),W,H)==reference);
            IDirect3DVertexShader9* restored=nullptr;ok(d->GetVertexShader(&restored));assert(restored==sources.back());restored->Release();
            assert(calls.vertexCreates==(shared?1u:8u)&&calls.submitted==(shared?1u:N));submitted=calls.submitted;
        }
        std::sort(times.begin(),times.end());std::printf("MODEL_NORMALS_VERTEX_PROGRAM_PASS pixels=bitexact nativeSources=8 vertexPrograms=%u submitted=%u nativeMetadata=preserved nativeVS=restored medianCpuMs=%.6f\n",calls.vertexCreates,submitted,times[4]);
        vertexNormals.reset();for(auto* source:sources)source->Release();ok(d->SetVertexShader(shader));
    }
    // Interleave an opaque coplanar mesh with opposite normals. Only A is
    // resident, so table collisions cannot hide A's nonadjacent duplicates.
    // Keeping A's first occurrence instead of its last would leave B on top.
    auto a=std::make_shared<NorthlightDrawSnapshot::Mesh>();a->indexed=true;a->topology=D3DPT_TRIANGLELIST;
    a->vertexCount=vertexCount;a->primitiveCount=indexCount/3;a->streams[0].stride=48;
    a->streams[0].bytes.assign(source.begin()+12,source.begin()+12+vertexCount*48);a->indices.resize(indexCount);
    std::memcpy(a->indices.data(),source.data()+12+vertexCount*48,indexCount*4);a->sourceIdentity=NorthlightSourceIdentity::make({0xded0u,1});
    auto b=std::make_shared<NorthlightDrawSnapshot::Mesh>(*a);b->sourceIdentity.reset();
    for(unsigned v=0;v<vertexCount;++v){float normal[3];auto* bytes=b->streams[0].bytes.data()+v*48+20;std::memcpy(normal,bytes,12);for(auto& c:normal)c=-c;std::memcpy(bytes,normal,12);}
    const std::shared_ptr<const NorthlightDrawSnapshot::Mesh> owners[]={a,b};
    std::vector<unsigned char> just[2],duplicateReference;unsigned duplicateCalls[2]={};std::vector<double> duplicateTimes[2];
    for(unsigned frame=0;frame<22;++frame){normals->endFrame();store.setFrame(20+frame);calls.submitted=0;
        const bool dedup=frame>=4&&(frame-4)%2;
        ModelNormalMergeTest::enable(*normals,true);ModelNormalMergeTest::dedup(*normals,dedup);
        ok(d->BeginScene());const unsigned packets=frame<4?1:2*N+1;
        for(unsigned i=0;i<packets;++i){const auto& mesh=owners[frame<4?frame%2:i%2];const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,vertexCount,0,mesh->primitiveCount,true};
            normals->capture(shader,[]{return D3D_OK;},true,&geometry,nullptr,D3DFMT_UNKNOWN,nullptr,0,&mesh,declaration);}
        assert(normals->testPendingPackets()==packets);
        if(frame==7)ModelNormalMergeTest::duplicateControls(*normals,N-1);
        const auto start=std::chrono::steady_clock::now();assert(normals->finishCaptures());
        if(frame>=4)duplicateTimes[dedup].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        ok(d->EndScene());const auto image=capturedPixels(normals->texture(),W,H);
        if(frame==2||frame==3)just[frame%2]=image;
        if(frame>=4){assert(just[0]!=just[1]&&image==just[0]);
            if(frame==4)duplicateReference=image;else assert(image==duplicateReference);
            assert(ModelNormalMergeTest::duplicates(*normals)==(dedup?N-1:0));
            assert(calls.submitted==(dedup?N+2:2*N+1));assert(ModelNormalMergeTest::submitted(*normals)==calls.submitted);duplicateCalls[dedup]=calls.submitted;}
    }
    std::printf("MODEL_NORMALS_DUPLICATE_GPU_PASS pixels=bitexact coplanar_last_wins=1 interleaved_draws=%u/%u first_depth_preserved=1\n",duplicateCalls[0],duplicateCalls[1]);
    for(auto& t:duplicateTimes)std::sort(t.begin(),t.end());
    std::printf("MODEL_NORMALS_DUPLICATE_CPU replayMedianMs=%.6f/%.6f alternatingPairs=9 scope=component_submission\n",duplicateTimes[0][4],duplicateTimes[1][4]);
    normals.reset();store.clearAll();pixel->Release();depth->Release();color.read->Release();color.s->Release();color.t->Release();
}
