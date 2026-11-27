static void nativeIndexedPairs(PaletteDevice& d){
    using NorthlightDrawSnapshot::Ref;
    using NorthlightDrawSnapshot::Draw;
    constexpr UINT size=64;
    const D3DVERTEXELEMENT9 layout[]={
        {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,16,D3DDECLTYPE_UBYTE4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
    struct Vertex {float position[4];DWORD bones;};
    const Vertex vertices[]={{{-.6f,-.6f,.5f,1},0x05050505},{{.6f,-.6f,.5f,1},0x05050505},{{0,.6f,.5f,1},0x05050505},{{},0xffffffff}};
    const WORD indices[]={0,1,2};
    Ref<IDirect3DVertexDeclaration9> decl;Ref<IDirect3DVertexBuffer9> vb;Ref<IDirect3DIndexBuffer9> ib;
    Ref<IDirect3DVertexShader9> vs,mixedVs;Ref<IDirect3DPixelShader9> ps,probe;
    Ref<IDirect3DSurface9> target,depth,readback;
    must(d.CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,target.out(),nullptr));
    must(d.CreateDepthStencilSurface(size,size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,depth.out(),nullptr));
    must(d.CreateOffscreenPlainSurface(size,size,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,readback.out(),nullptr));
    must(d.CreateVertexDeclaration(layout,decl.out()));
    must(d.CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,vb.out(),nullptr));
    must(d.CreateIndexBuffer(sizeof indices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,ib.out(),nullptr));
    void* bytes=nullptr;must(vb->Lock(0,0,&bytes,0));std::memcpy(bytes,vertices,sizeof vertices);must(vb->Unlock());
    must(ib->Lock(0,0,&bytes,0));std::memcpy(bytes,indices,sizeof indices);must(ib->Unlock());
    must(d.CreateVertexShader(reinterpret_cast<const DWORD*>(vertexCode.data()),vs.out()));
    auto mixedCode=vertexCode;
    const auto color=std::prev(mixedCode.end(),5);
    color[1]=dst(0,3);
    constexpr unsigned fixtureConstants[]={10,11,12,17,18,19,20,21,22,23,24,29,236,237,238,239,240,241,242,243};
    for(unsigned c:fixtureConstants)mixedCode.insert(mixedCode.end()-1,{0x03000002,dst(0,3),src(0,3),src(2,c)});
    mixedCode.insert(mixedCode.end()-1,{0x02000001,dst(6,1),src(0,3)});
    must(d.CreateVertexShader(reinterpret_cast<const DWORD*>(mixedCode.data()),mixedVs.out()));
    const DWORD pixel[]={0xffff0300,0x0200001f,0x8000000a,dst(1,0),0x02000001,dst(8,0),src(1,0),65535};
    const DWORD mask[]={0xffff0300,0x05000051,dst(2,0),0x3f800000,0x3f800000,0x3f800000,0x3f800000,
        0x02000001,dst(8,0),src(2,0),65535};
    must(d.CreatePixelShader(pixel,ps.out()));must(d.CreatePixelShader(mask,probe.out()));
    must(d.SetStreamSource(0,vb.p,0,sizeof(Vertex)));must(d.SetIndices(ib.p));must(d.SetVertexDeclaration(decl.p));
    NorthlightDrawSnapshot::Frame capture;
    capture.setChargeResidentHits(false);
    capture.setIdentityProvider(+[](void* p,bool){return std::uint64_t(reinterpret_cast<std::uintptr_t>(p));});
    capture.setVersionProvider(+[](void*,bool){return std::uint64_t(1);});
    static std::uint64_t vertexRevision=1,indexRevision=1;
    capture.setMetadataProvider(+[](const NorthlightCaptureMetadata::Request* requests,NorthlightCaptureMetadata::Info* out,unsigned count){
        for(unsigned i=0;i<count;++i){
            out[i]={};
            if(!requests[i].buffer)continue;
            out[i].known=true;
            out[i].identity=reinterpret_cast<std::uintptr_t>(requests[i].buffer);
            out[i].revision=requests[i].index?indexRevision:vertexRevision;
            out[i].size=requests[i].index?sizeof indices:sizeof vertices;
            out[i].format=requests[i].index?D3DFMT_INDEX16:D3DFMT_VERTEXDATA;
        }
    });
    capture.setSnapshotCacheLimit(1u<<20);
    const Draw args{D3DPT_TRIANGLELIST,0,0,4,0,1,true};
    NorthlightDrawSnapshot::Mesh temporary;std::shared_ptr<const NorthlightDrawSnapshot::Mesh> mesh;
    assert(capture.read(&d,decl.p,args,temporary,nullptr,false,&mesh)&&mesh&&mesh->hasCheckedIndices()&&mesh->sourceIdentity);
    assert(mesh->vertexCount==3);
    const auto exactSource=[&](UINT offset){return capture.matchesIndexedSource(mesh,decl.p,vb.p,offset,sizeof(Vertex),ib.p,args);};
    assert(exactSource(0)&&!exactSource(sizeof(Vertex)));
    auto differentRange=args;differentRange.vertices=3;
    assert(!capture.matchesIndexedSource(mesh,decl.p,vb.p,0,sizeof(Vertex),ib.p,differentRange));
    vertexRevision=2;assert(!exactSource(0));vertexRevision=1;
    indexRevision=2;assert(!exactSource(0));
    indexRevision=0;assert(!exactSource(0));indexRevision=1;
    const auto pixels=[&] {
        must(d.GetRenderTargetData(target.p,readback.p));
        D3DLOCKED_RECT locked{};must(readback->LockRect(&locked,nullptr,D3DLOCK_READONLY));
        std::vector<DWORD> result(size*size);
        for(UINT y=0;y<size;++y)std::memcpy(result.data()+y*size,static_cast<char*>(locked.pBits)+y*locked.Pitch,size*4);
        must(readback->UnlockRect());return result;
    };
    enum class Split {None, Barrier, Stencil, Shared, Serial, Observe};
    const auto render=[&](bool combine,bool mark,Split split,PaletteDevice::Fault fault,bool mixed=false,
                          unsigned instances=4,bool useMirror=false,bool alphaTest=false,bool deferStencil=false,bool reuseSource=false){
        DeviceMirror mirrorState;
        ExtensionDevice mirror(&d,&mirrorState);
        IDirect3DDevice9* logical=useMirror?static_cast<IDirect3DDevice9*>(&mirror):static_cast<IDirect3DDevice9*>(&d);
        NorthlightGameIndexedPair pair(&d,useMirror?&mirror:nullptr);
        pair.setMixedConstants(mixed);
        auto* shader=mixed?mixedVs.p:vs.p;
        must(d.SetDepthStencilSurface(nullptr));must(d.SetRenderTarget(0,target.p));must(d.SetDepthStencilSurface(depth.p));
        must(logical->SetVertexDeclaration(decl.p));must(logical->SetVertexShader(shader));must(logical->SetPixelShader(ps.p));
        must(logical->SetStreamSource(0,vb.p,0,sizeof(Vertex)));must(logical->SetIndices(ib.p));
        must(logical->SetRenderState(D3DRS_ZENABLE,TRUE));must(logical->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
        must(logical->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));must(logical->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS));
        must(logical->SetRenderState(D3DRS_ALPHATESTENABLE,alphaTest));
        must(logical->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));must(logical->SetRenderState(D3DRS_ALPHAREF,127));
        must(logical->SetRenderState(D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP));must(logical->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP));
        must(logical->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE));must(logical->SetRenderState(D3DRS_STENCILMASK,128));
        must(d.Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,mark?0:128));
        const ExtensionDevice::IndexedStencil baseStencil{FALSE,128,255};
        const auto setStencil=[&](const ExtensionDevice::IndexedStencil& stamp){
            for(unsigned i=0;i<stamp.size();++i)must(logical->SetRenderState(ExtensionDevice::IndexedStencilKeys[i],stamp[i]));
        };
        setStencil(baseStencil);
        if(deferStencil){
            assert(combine&&useMirror);
            mirror.setIndexedPair(&pair,[](void* context){must(static_cast<NorthlightGameIndexedPair*>(context)->flush());},
                [](void* context,UINT first,const float* values,UINT count){return static_cast<NorthlightGameIndexedPair*>(context)->allowsConstants(first,values,count);});
        }
        unsigned materialized=0,sourceReuses=0;
        d.calls=d.hits=0;d.fault=fault;must(d.BeginScene());
        for(unsigned i=0;i<instances;++i){
            float pose[12]={};pose[0]=.2f*float(int(i%4)-2);pose[4+i%3]=pose[7]=1;
            if(i+1==instances)pose[0]=.2f;
            if(alphaTest&&(i%2))pose[7]=.25f;
            if(mixed){
                constexpr unsigned first=2,count=243-first+1;
                std::array<float,count*4> values{};
                values[1]=split==Split::Shared&&i>=2?.08f:0.f;
                for(unsigned c:fixtureConstants)values[(c-first)*4+2]=.002f*float(i+1);
                values[(17-first)*4]=.05f*float(i+1);
                values[(243-first)*4+1]=.1f*float(i+1);
                std::memcpy(values.data()+(Base+5*RowsPerBone-first)*4,pose,sizeof pose);
                if(combine&&!pair.allowsConstants(first,values.data(),count))must(pair.flush());
                must(logical->SetVertexShaderConstantF(first,values.data(),count));
            }else must(logical->SetVertexShaderConstantF(Base+5*RowsPerBone,pose,3));
            if(i==2&&split==Split::Barrier)must(pair.flush());
            const DWORD ref=(mark!=(split==Split::Stencil&&i>=2))?128:0;
            const ExtensionDevice::IndexedStencil stamp{TRUE,ref,128};
            if(deferStencil){
                const unsigned changes=ref==baseStencil[1]?5u:7u;
                assert(mirror.beginIndexedStencil(stamp,changes));
                DWORD physical=TRUE;must(d.GetRenderState(D3DRS_STENCILENABLE,&physical));assert(!physical);
                if(split==Split::Observe&&i==2){
                    DWORD value=0;must(logical->GetRenderState(D3DRS_STENCILWRITEMASK,&value));assert(value==128);
                    must(d.GetRenderState(D3DRS_STENCILENABLE,&value));assert(value);
                }
            }else setStencil(stamp);
            const auto serial=[&]{return logical->DrawIndexedPrimitive(args.topology,args.base,args.minimum,args.vertices,args.start,args.primitives);};
            if(combine){
                MirrorGuard drawLock(mirrorState.gate);
                if(reuseSource){
                    pair.clearSource();
                    if(i){
                        const auto hint=pair.pendingSource(args,shader);
                        const auto& stream=mirrorState.streams[0];
                        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> reused;
                        assert(hint==mesh&&stream.known&&mirrorState.indicesKnown);
                        assert(capture.matchesIndexedSource(hint,decl.p,stream.buffer,stream.offset,stream.stride,mirrorState.indices,args,&reused));
                        assert(reused==mesh);++sourceReuses;
                        pair.noteSource(shader,decl.p,reused);
                    }else{
                        NorthlightDrawSnapshot::Mesh local;
                        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> first;
                        assert(capture.read(logical,decl.p,args,local,nullptr,false,&first,false,true)&&first==mesh);
                        pair.noteSource(shader,decl.p,first);
                    }
                }else if(split==Split::Serial&&i==2)pair.clearSource();
                else pair.noteSource(shader,decl.p,mesh);
                must(pair.submit(args,mark?3:2,serial));
            }
            else must(serial());
            if(!deferStencil)setStencil(baseStencil);
            else if(!mirror.endIndexedStencil()){++materialized;setStencil(baseStencil);}
        }
        must(pair.flush());must(d.EndScene());
        if(deferStencil){
            mirror.setIndexedPair(nullptr,nullptr,nullptr);
            const unsigned expectedMaterialized=unsigned(split==Split::Serial||split==Split::Observe);
            assert(materialized==expectedMaterialized);
            for(unsigned i=0;i<baseStencil.size();++i){
                DWORD value=0;must(d.GetRenderState(ExtensionDevice::IndexedStencilKeys[i],&value));assert(value==baseStencil[i]);
            }
        }
        const unsigned expected=combine&&fault==PaletteDevice::Fault::None?
            (split==Split::Serial?3:split==Split::None||split==Split::Observe?1:2):instances;
        assert(d.calls==expected&&pair.saved==instances-expected&&pair.receiverSaved==(mark?pair.saved:0));
        assert(pair.alphaSaved==(alphaTest?pair.saved:0));
        assert(sourceReuses==(reuseSource?instances-1:0));
        if(useMirror&&combine)assert(pair.mirroredFloatReads>pair.backendFloatReads&&pair.backendFloatReads>0);
        assert(pair.mixedSaved==(mixed?pair.saved:0));
        assert(pair.constantBarriers==unsigned(combine&&split==Split::Shared));
        assert(d.hits==unsigned(fault!=PaletteDevice::Fault::None));
        float live[12]{};must(d.GetVertexShaderConstantF(Base+5*RowsPerBone,live,3));assert(live[0]==.2f&&live[4+(instances-1)%3]==1.f);
        if(mixed){
            must(d.GetVertexShaderConstantF(17,live,1));assert(live[0]==.05f*instances);
            must(d.GetVertexShaderConstantF(243,live,1));assert(live[1]==.1f*instances);
        }
        auto result=pixels();assert(std::count_if(result.begin(),result.end(),[](DWORD x){return (x&0xffffff)!=0;})>256);
        must(d.SetVertexShader(nullptr));must(d.SetFVF(D3DFVF_XYZRHW));must(d.SetPixelShader(probe.p));
        must(d.SetRenderState(D3DRS_ZENABLE,FALSE));must(d.SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
        must(d.SetRenderState(D3DRS_STENCILENABLE,TRUE));must(d.SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL));
        must(d.SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_KEEP));must(d.SetRenderState(D3DRS_STENCILWRITEMASK,0));
        must(d.Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        const float quad[]={-.5f,-.5f,.5f,1, size-.5f,-.5f,.5f,1, -.5f,size-.5f,.5f,1, size-.5f,size-.5f,.5f,1};
        must(d.BeginScene());must(d.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(float)*4));must(d.EndScene());
        const auto stencil=pixels();
        const auto stencilPixels=std::count(stencil.begin(),stencil.end(),0xffffffffu);
        if(stencilPixels<=256)std::fprintf(stderr,"stencil pixels=%d combine=%d mark=%d split=%d fault=%d mixed=%d instances=%u deferred=%d\n",
            int(stencilPixels),combine,mark,int(split),int(fault),mixed,instances,deferStencil);
        assert(stencilPixels>256);
        assert(std::count(stencil.begin(),stencil.end(),0xffffffffu)<int(stencil.size()));
        result.insert(result.end(),stencil.begin(),stencil.end());
        must(d.SetRenderState(D3DRS_STENCILENABLE,FALSE));must(d.SetRenderState(D3DRS_ZENABLE,TRUE));
        must(d.SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));must(d.Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        must(d.BeginScene());must(d.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(float)*4));must(d.EndScene());
        const auto depthPixels=pixels();
        assert(std::count(depthPixels.begin(),depthPixels.end(),0xffffffffu)>256);
        assert(std::count(depthPixels.begin(),depthPixels.end(),0xffffffffu)<int(depthPixels.size()));
        result.insert(result.end(),depthPixels.begin(),depthPixels.end());return result;
    };
    using Fault=PaletteDevice::Fault;
    const auto serial=render(false,false,Split::None,Fault::None);
    assert(render(true,false,Split::None,Fault::None)==serial);
    assert(render(true,false,Split::Barrier,Fault::None)==serial);
    assert(render(true,false,Split::None,Fault::Bind)==serial);
    assert(render(true,true,Split::None,Fault::None)==render(false,true,Split::None,Fault::None));
    assert(render(true,true,Split::Stencil,Fault::None)==render(false,true,Split::Stencil,Fault::None));
    const auto mixedSerial=render(false,true,Split::None,Fault::None,true);
    assert(render(true,true,Split::None,Fault::None,true)==mixedSerial);
    assert(render(true,true,Split::None,Fault::None,true,4,true,false,true,true)==mixedSerial);
    assert(render(true,true,Split::None,Fault::Bind,true)==mixedSerial);
    assert(render(true,true,Split::Shared,Fault::None,true)==render(false,true,Split::Shared,Fault::None,true));
    assert(render(true,true,Split::None,Fault::None,true,9,true)==render(false,true,Split::None,Fault::None,true,9));
    assert(render(true,true,Split::None,Fault::Bind,true,9,true)==render(false,true,Split::None,Fault::None,true,9));
    const auto cutout=render(false,false,Split::None,Fault::None,true,9,false,true);
    assert(render(true,false,Split::None,Fault::None,true,9,true,true)==cutout);
    assert(render(true,false,Split::None,Fault::Bind,true,9,true,true)==cutout);
    assert(render(true,false,Split::None,Fault::None,true,9,true,true,true)==cutout);
    assert(render(true,false,Split::None,Fault::Bind,true,9,true,true,true)==cutout);
    assert(render(true,false,Split::Serial,Fault::None,true,9,true,true,true)==cutout);
    assert(render(true,false,Split::Observe,Fault::None,true,9,true,true,true)==cutout);
    assert(render(true,true,Split::Stencil,Fault::None,false,4,true,false,true)==render(false,true,Split::Stencil,Fault::None));
    assert(cutout!=render(false,false,Split::None,Fault::None,true,9));

    Ref<IDirect3DVertexBuffer9> rangeVertices;
    Vertex twoTriangles[6];
    for(unsigned i=0;i<6;++i){
        auto& v=twoTriangles[i];v=vertices[i%3];
        v.position[0]=v.position[0]*.8f+(i<3?-.28f:.28f);
        v.position[2]=i<3?.5f:.6f;
    }
    must(d.CreateVertexBuffer(sizeof twoTriangles,0,0,D3DPOOL_MANAGED,rangeVertices.out(),nullptr));
    must(rangeVertices->Lock(0,0,&bytes,0));std::memcpy(bytes,twoTriangles,sizeof twoTriangles);must(rangeVertices->Unlock());
    const auto rangeCase=[&](auto indexValue,D3DFORMAT format){
        using Index=decltype(indexValue);
        Ref<IDirect3DIndexBuffer9> input;
        Index values[]={0,1,2,0,1,0,3,4,5,0,1,0};
        must(d.CreateIndexBuffer(sizeof values,0,format,D3DPOOL_MANAGED,input.out(),nullptr));
        NorthlightReplayCopies::Slot slot;std::atomic<unsigned> locks{0};
        NorthlightReplayCopies::attach(slot,input.p,input.p,input.p,sizeof values,&locks);
        static NorthlightCaptureMetadata::Info source;
        source={1,1,sizeof values,0,format,true};
        const auto writeIndices=[&](bool copy){
            void* targetBytes=nullptr;must(input->Lock(0,0,&targetBytes,0));
            std::memcpy(targetBytes,values,sizeof values);must(input->Unlock());
            NorthlightReplayCopies::invalidate(slot);
            if(copy){
                assert(NorthlightReplayCopies::beginFill(slot,sizeof values));
                const auto* bytes=reinterpret_cast<const unsigned char*>(values);
                slot.data.assign(bytes,bytes+sizeof values);
                assert(NorthlightReplayCopies::endFill(slot,true));NorthlightReplayCopies::unpin(slot);
            }
        };
        writeIndices(true);
        DeviceMirror state;
        ExtensionDevice ext(&d,&state);
        NorthlightGameIndexedPair pair(&d,&ext,[](const NorthlightCaptureMetadata::Request*,NorthlightCaptureMetadata::Info* info,unsigned count){
            assert(count==1);*info=source;
        });
        const auto renderRanges=[&](bool join,unsigned secondStart,Fault fault,unsigned expected,bool constantBarrier=false,unsigned drawCount=3){
            pair.clearCounters();pair.setJoinRanges(join);
            must(ext.SetDepthStencilSurface(nullptr));must(ext.SetRenderTarget(0,target.p));must(ext.SetDepthStencilSurface(depth.p));
            must(ext.SetStreamSource(0,rangeVertices.p,0,sizeof(Vertex)));must(ext.SetIndices(input.p));
            must(ext.SetVertexDeclaration(decl.p));must(ext.SetVertexShader(vs.p));must(ext.SetPixelShader(ps.p));
            const float zero[4]={},pose[12]={0,0,0,0,1,0,0,1};
            must(ext.SetVertexShaderConstantF(2,zero,1));must(ext.SetVertexShaderConstantF(Base+5*RowsPerBone,pose,3));
            must(ext.SetRenderState(D3DRS_ZENABLE,TRUE));must(ext.SetRenderState(D3DRS_ZWRITEENABLE,TRUE));must(ext.SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
            must(ext.SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));must(ext.SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            must(ext.SetRenderState(D3DRS_STENCILENABLE,TRUE));must(ext.SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS));
            must(ext.SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE));must(ext.SetRenderState(D3DRS_STENCILREF,128));
            must(ext.SetRenderState(D3DRS_STENCILWRITEMASK,128));
            must(ext.Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,0));
            if(join){
                must(ext.SetRenderState(D3DRS_STENCILENABLE,FALSE));
                ext.setIndexedPair(&pair,[](void* context){must(static_cast<NorthlightGameIndexedPair*>(context)->flush());},
                    [](void* context,UINT first,const float* values,UINT count){return static_cast<NorthlightGameIndexedPair*>(context)->allowsConstants(first,values,count);});
            }
            d.calls=d.hits=0;d.fault=fault;must(ext.BeginScene());
            const Draw packet[]={{D3DPT_TRIANGLELIST,0,0,3,0,1,true},{D3DPT_TRIANGLELIST,0,0,6,secondStart,1,true},
                                 {D3DPT_TRIANGLELIST,0,0,6,secondStart+3,1,true}};
            for(unsigned i=0;i<drawCount;++i){
                const auto& draw=packet[i];
                if(draw.start&&constantBarrier){
                    const float nextColor[]={0,1,0,1};must(ext.SetVertexShaderConstantF(Base+5*RowsPerBone+1,nextColor,1));
                }
                const auto submit=[&]{return ext.DrawIndexedPrimitive(draw.topology,draw.base,draw.minimum,draw.vertices,draw.start,draw.primitives);};
                if(join){
                    assert(ext.beginIndexedStencil({TRUE,128,128},1));
                    must(pair.submit(draw,2,submit));assert(ext.endIndexedStencil()==1);
                }else must(submit());
            }
            must(pair.flush());must(ext.EndScene());
            ext.setIndexedPair(nullptr,nullptr,nullptr);
            assert(d.calls==expected&&pair.rangeSaved==drawCount-expected&&d.hits==unsigned(fault!=Fault::None));
            Ref<IDirect3DIndexBuffer9> live;must(d.GetIndices(live.out()));assert(live.p==input.p);
            DWORD stencilEnabled=TRUE;must(d.GetRenderState(D3DRS_STENCILENABLE,&stencilEnabled));assert(stencilEnabled==DWORD(!join));
            auto result=pixels();
            must(ext.SetVertexShader(nullptr));must(ext.SetFVF(D3DFVF_XYZRHW));must(ext.SetPixelShader(probe.p));
            must(ext.SetRenderState(D3DRS_ZWRITEENABLE,FALSE));must(ext.SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));
            must(ext.SetRenderState(D3DRS_STENCILENABLE,TRUE));must(ext.SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL));must(ext.SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_KEEP));
            must(ext.Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
            const float quad[]={-.5f,-.5f,.5f,1, size-.5f,-.5f,.5f,1, -.5f,size-.5f,.5f,1, size-.5f,size-.5f,.5f,1};
            must(ext.BeginScene());must(ext.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(float)*4));must(ext.EndScene());
            const auto depthStencil=pixels();assert(std::count(depthStencil.begin(),depthStencil.end(),0xffffffffu)>128);
            result.insert(result.end(),depthStencil.begin(),depthStencil.end());return result;
        };
        const auto original=renderRanges(false,6,Fault::None,3);
        assert(renderRanges(true,6,Fault::None,1)==original&&pair.indexRangeCache().built==1);
        assert(renderRanges(true,6,Fault::None,1)==original&&pair.indexRangeCache().hits==1);
        assert(renderRanges(true,6,Fault::IndexBind,3)==original&&pair.indexRangeCache().hits==1);
        assert(renderRanges(true,3,Fault::None,1)==renderRanges(false,3,Fault::None,3));
        const auto two=renderRanges(false,6,Fault::None,2,false,2);
        assert(renderRanges(true,6,Fault::None,2,false,2)==two&&pair.indexRangeCache().hits==0);
        const auto splitConstants=renderRanges(false,6,Fault::None,3,true);
        assert(splitConstants!=original&&renderRanges(true,6,Fault::None,3,true)==splitConstants);
        values[6]=0;values[7]=1;values[8]=2;++source.revision;writeIndices(true);
        const auto changed=renderRanges(false,6,Fault::None,3);
        assert(original!=changed&&renderRanges(true,6,Fault::None,1)==changed&&pair.indexRangeCache().built==1);
        ++source.revision;writeIndices(false);
        assert(renderRanges(true,6,Fault::None,3)==changed&&pair.indexRangeCache().unavailable==1);
        NorthlightReplayCopies::detach(slot);
    };
    rangeCase(WORD(0),D3DFMT_INDEX16);
    rangeCase(DWORD(0),D3DFMT_INDEX32);
    std::puts("GAME_INDEXED_RANGES_PASS draws=3->1 index16=exact index32=exact contiguous=exact revision=exact fallback=exact");
    must(d.SetDepthStencilSurface(nullptr));
    std::puts("NATIVE_INDEXED_PAIR_PASS draws=9->1 color=exact depth=exact stencil=exact receiver=exact mixedConstants=exact mirror=exact alphaTest=exact deferredStencil=exact sharedBarrier=ordered bindFallback=exact");
}
