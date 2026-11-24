    static void paletteReplay(TargetGpuMeter& d){
        using NorthlightDrawSnapshot::Ref;
        using NorthlightModelNormalSlice::dst;
        using NorthlightModelNormalSlice::src;
        constexpr UINT size=64;
        NorthlightAutoBump b(&d);b.width=b.heightPixels=size;b.independentCaptureWrites=true;
        b.setTextureWriteGuard(true,nullptr,nullptr,+[](void*,void*,bool,uint64_t){
            return NorthlightCaptureMetadata::ReadLease{nullptr,nullptr};
        });
        assert(!b.guardSourceRead);
        ok(d.CreateTexture(size,size,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&b.height,nullptr));
        ok(b.height->GetSurfaceLevel(0,&b.target));
        ok(d.CreateTexture(size,size,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&b.occlusionMap,nullptr));
        ok(b.occlusionMap->GetSurfaceLevel(0,&b.occlusionTarget));
        ok(d.CreateDepthStencilSurface(size,size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&b.captureDepth,nullptr));

        const DWORD vertexWords[]={0xfffe0300,0x05000051,dst(2,0),0x40400000,0,0,0,
            0x0200001f,0x80000000,dst(1,0),0x0200001f,0x80000002,dst(1,2),
            0x0200001f,0x80000000,dst(6,0),0x0200001f,0x8000000a,dst(6,1),
            0x0200001f,0x8001000a,dst(6,2),
            0x03000005,dst(0,0,1),src(1,2,0),src(2,0,0),0x0200002e,dst(3,0,1),src(0,0,0),
            0x04000002,dst(0,1),src(1,0),src(2,31)|0x2000u,src(3,0,0),
            0x03000002,dst(0,1),src(0,1),src(2,2),
            0x03000002,dst(6,0),src(0,1),src(2,210),
            0x03000001,dst(6,1),src(2,32)|0x2000u,src(3,0,0),
            0x02000001,dst(0,2),src(2,13),0x03000002,dst(6,2),src(0,2),src(2,239),65535};
        DWORD pixelWords[]={0xffff0300,0x0200001f,0x8000000a,dst(1,0),0x0200001f,0x8001000a,dst(1,1),
            0x02000001,dst(8,0),src(1,0),0x02000001,dst(8,1),src(1,0),65535};
        const D3DVERTEXELEMENT9 elements[]={
            {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,16,D3DDECLTYPE_UBYTE4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
        Ref<IDirect3DVertexShader9> vs;Ref<IDirect3DPixelShader9> ps,litPs;Ref<IDirect3DVertexDeclaration9> decl;
        ok(d.CreateVertexShader(vertexWords,vs.out()));ok(d.CreatePixelShader(pixelWords,ps.out()));
        pixelWords[12]=src(1,1);
        ok(d.CreatePixelShader(pixelWords,litPs.out()));
        ok(d.CreateVertexDeclaration(elements,decl.out()));
        auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();
        struct Vertex {float position[4];DWORD bones;};
        const Vertex vertices[]={{{-.6f,-.6f,.5f,1},0x05050505},{{.6f,-.6f,.5f,1},0x05050505},{{0,.6f,.5f,1},0x05050505}};
        mesh->vertexCount=3;mesh->primitiveCount=1;mesh->indexed=mesh->sharedCapture=true;
        mesh->sourceIdentity=NorthlightSourceIdentity::make({42});mesh->indices={0,1,2};
        mesh->streams[0].stride=sizeof(Vertex);mesh->streams[0].bytes.resize(sizeof vertices);
        std::memcpy(mesh->streams[0].bytes.data(),vertices,sizeof vertices);

        constexpr UINT vertexOffset=sizeof(Vertex),sourceBytes=vertexOffset+sizeof vertices;
        constexpr WORD sourceIndices[]={0,1,2,3};
        const NorthlightDrawSnapshot::Draw sourceDraw{D3DPT_TRIANGLELIST,-1,1,3,1,1,true};
        Ref<IDirect3DVertexBuffer9> vb;Ref<IDirect3DIndexBuffer9> ib;
        ok(d.CreateVertexBuffer(sourceBytes,0,0,D3DPOOL_MANAGED,vb.out(),nullptr));
        ok(d.CreateIndexBuffer(sizeof sourceIndices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,ib.out(),nullptr));
        void* bytes=nullptr;
        ok(vb.p->Lock(vertexOffset,sizeof vertices,&bytes,0));std::memcpy(bytes,vertices,sizeof vertices);ok(vb.p->Unlock());
        ok(ib.p->Lock(0,0,&bytes,0));std::memcpy(bytes,sourceIndices,sizeof sourceIndices);ok(ib.p->Unlock());
        b.setGeometryTracking(+[](void* buffer,bool){return uint64_t(reinterpret_cast<std::uintptr_t>(buffer));},
            +[](void*,bool){return uint64_t(1);},
            +[](const NorthlightCaptureMetadata::Request* requests,NorthlightCaptureMetadata::Info* out,unsigned count){
                for(unsigned i=0;i<count;++i){const auto& request=requests[i];
                    out[i]={uint64_t(reinterpret_cast<std::uintptr_t>(request.buffer)),1,
                        request.index?UINT(sizeof sourceIndices):sourceBytes,0,request.index?D3DFMT_INDEX16:D3DFMT_VERTEXDATA,true};}
            });
        ok(d.SetVertexDeclaration(decl.p));ok(d.SetStreamSource(0,vb.p,vertexOffset,sizeof(Vertex)));ok(d.SetIndices(ib.p));
        NorthlightDrawSnapshot::Mesh local;
        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> gameMesh;
        assert(b.deferredSnapshots.read(&d,decl.p,sourceDraw,local,nullptr,false,&gameMesh)&&gameMesh&&gameMesh->sourceIdentity);

        const auto render=[&](bool palette,bool fault,unsigned changedRegister,bool readsLighting=false,bool fog=false,bool nativeSource=false,bool guardedSource=false){
            b.materialPalette.unavailable=!palette;d.indexed=d.instanceFailures=0;d.failInstanceBind=fault;
            for(unsigned i=0;i<3;++i){
                auto p=std::make_unique<NorthlightAutoBump::Deferred>();
                p->mesh=mesh;p->tag=19;p->vertex=vs.p;vs->AddRef();p->pixel=readsLighting?litPs.p:ps.p;p->pixel->AddRef();p->declaration=decl.p;decl->AddRef();
                if(nativeSource){
                    p->mesh=gameMesh;p->sourceDraw=sourceDraw;p->sourceOffset=vertexOffset;p->sourceStride=sizeof(Vertex);
                    p->gpuVertex[0]=vb.p;vb.p->AddRef();p->gpuIndex=ib.p;ib.p->AddRef();
                    if(guardedSource){
                        p->mesh.reset();
                        p->guardedVertex={vb.p,+[](void*){}};
                        p->guardedIndex={ib.p,+[](void*){}};
                    }
                }
                p->vertexFloatOnly=p->pixelFloatOnly=true;p->occlusion=true;
                std::fill_n(p->vertexConstants,1024,0.f);
                // c0 is a shader literal. c3 and c255 are unread.
                for(unsigned reg:{0,3,255})p->vertexConstants[4*reg]=float(i+10);
                // These rows feed COLOR1, which the unlit PS declares but never reads.
                p->vertexConstants[4*13]=float(i)*.2f;
                p->vertexConstants[4*239]=float(i)*.1f;
                if(i==2)p->vertexConstants[4*changedRegister]=.2f;
                p->vertexConstants[4*(31+3*5)]=.23f*float(int(i)-1);
                p->vertexConstants[4*(32+3*5)+i]=1.f;p->vertexConstants[4*(32+3*5)+3]=1.f;
                p->pixelConstants={0.f,0.f,0.f,0.f};p->viewport={0,0,size,size,0,1};
                NorthlightAutoBump::ReplayState state(&d,nullptr,{});
                for(unsigned j=0;j<14;++j)p->states[j]=state.states[j].replay;
                p->states[0]=TRUE;p->states[2]=fog;p->states[5]=FALSE;
                std::fill_n(p->raster,NorthlightAutoBump::DeferredRasterCount,0);
                p->raster[0]=D3DCULL_NONE;p->raster[1]=TRUE;p->raster[2]=D3DCMP_LESSEQUAL;p->raster[7]=~DWORD(0);
                p->raster[8]=TRUE;p->raster[12]=D3DSHADE_GOURAUD;p->raster[13]=D3DFILL_SOLID;
                b.deferred.push_back(std::move(p));
            }
            ok(d.BeginScene());assert(b.finishCaptures());ok(d.EndScene());
            assert(b.flushed==3);
            for(UINT stream=0;stream<2;++stream){UINT frequency=0;ok(d.GetStreamSourceFreq(stream,&frequency));assert(frequency==1);}
            auto result=pixels(&d,b.target,size,size,D3DFMT_A16B16G16R16F,8);
            const auto aux=pixels(&d,b.occlusionTarget,size,size,D3DFMT_A8R8G8B8,4);result.insert(result.end(),aux.begin(),aux.end());
            const unsigned saved=b.palettePackets-b.paletteDraws;
            if(nativeSource)assert(b.sourcePaletteSaved==saved);
            b.endFrame();
            return std::make_pair(std::move(result),saved);
        };
        for(unsigned warm=0;warm<5;++warm)render(false,false,3);
        for(unsigned changedRegister:{3,2,210}){
            const auto reference=render(false,false,changedRegister);
            assert(d.indexed==3&&std::count_if(reference.first.begin(),reference.first.end(),[](auto x){return x!=0;})>512);
            assert(render(true,true,changedRegister).first==reference.first&&d.indexed==3&&d.instanceFailures==1);
            const auto candidate=render(true,false,changedRegister);
            const unsigned expectedDraws=changedRegister==3?1:2;
            assert(d.indexed==expectedDraws&&candidate.second==3-expectedDraws&&candidate.first==reference.first);
        }
        const auto litReference=render(false,false,3,true);
        const auto litCandidate=render(true,false,3,true);
        assert(d.indexed==3&&litCandidate.second==0&&litCandidate.first==litReference.first);
        const auto fogReference=render(false,false,3,false,true);
        const auto fogCandidate=render(true,false,3,false,true);
        assert(d.indexed==3&&fogCandidate.second==0&&fogCandidate.first==fogReference.first);
        const auto sourceReference=render(false,false,3,false,false,true);
        assert(d.indexed==3&&sourceReference.first==render(false,false,3).first);
        const auto sourceFault=render(true,true,3,false,false,true);
        assert(d.indexed==3&&d.instanceFailures==1&&sourceFault.first==sourceReference.first);
        const auto sourceCandidate=render(true,false,3,false,false,true);
        assert(d.indexed==1&&sourceCandidate.second==2&&sourceCandidate.first==sourceReference.first);
        const auto guardedCandidate=render(true,false,3,false,false,true,true);
        assert(d.indexed==3&&guardedCandidate.first==sourceReference.first);
        std::puts("MATERIAL_SOURCE_PALETTE_GPU_PASS draws=3->1 offset=exact negativeBase=exact index16=exact rollback=exact");
        std::puts("MATERIAL_PALETTE_GPU_PASS unreadDraws=3->1 liveDraws=3->2 linkedColorDraws=3->3 fogFallback=1 equalNormalAndOcclusion=1 partialBindFallback=1");
    }
