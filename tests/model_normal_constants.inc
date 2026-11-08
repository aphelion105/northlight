// Actual deferred packets and GPU output, with the reuse path disabled as control.
static void normalConstants(const std::string& dir,IDirect3DVertexDeclaration9* declaration,IDirect3DVertexShader9* single,const std::vector<unsigned char>& source,unsigned vertices,unsigned indices){
    SavedState saved(d);assert(saved.ok);auto output=target(128,128,D3DFMT_A8R8G8B8,4);
    IDirect3DSurface9* depth=nullptr;ok(d->CreateDepthStencilSurface(128,128,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    float original[1024];ok(d->GetVertexShaderConstantF(0,original,256));auto rigid=vs(dir+"model-vs3-rigid.bin");
    auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->topology=D3DPT_TRIANGLELIST;mesh->vertexCount=vertices;mesh->primitiveCount=indices/3;mesh->indexed=true;mesh->streams[0].stride=48;
    mesh->streams[0].bytes.assign(source.begin()+12,source.begin()+12+48*vertices);mesh->indices.resize(indices);std::memcpy(mesh->indices.data(),source.data()+12+48*vertices,indices*4);
    std::shared_ptr<const NorthlightDrawSnapshot::Mesh> immutable=mesh;
    const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,vertices,0,indices/3,true};
    unsigned beforeReads=0,afterReads=0,nonempty=0;uint64_t epochHits=0,blockHits=0;unsigned cases=0;
    for(unsigned scenario=0;scenario<12;++scenario){std::vector<unsigned char> reference,nextReference;unsigned baselineReads=0;
        for(bool reuse:{false,true}){
            CaptureCalls calls(d);DeviceMirror mirror,foreignMirror;ExtensionDevice extension(&calls,&mirror),foreign(&calls,&foreignMirror);MirrorGuard held(mirror.gate);
            NorthlightModelNormals normals(&extension);normals.setDeferredCapture(true);normals.testRange(false);normals.testConstantReuse(reuse);normals.setTrustedPoseSource(extension.trustedConstantSource());
            for(unsigned slot=1;slot<4;++slot)ok(extension.SetRenderTarget(slot,nullptr));ok(extension.SetDepthStencilSurface(nullptr));ok(extension.SetRenderTarget(0,output.s));ok(extension.SetDepthStencilSurface(depth));
            D3DVIEWPORT9 viewport{0,0,128,128,0,1};ok(extension.SetViewport(&viewport));ok(extension.SetVertexDeclaration(declaration));ok(extension.SetVertexShader(single));
            ok(extension.SetVertexShaderConstantF(0,original,256));
            for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE})ok(extension.SetRenderState(key,TRUE));
            for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS})ok(extension.SetRenderState(key,0));
            ok(extension.SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(extension.SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(extension.SetRenderState(D3DRS_COLORWRITEENABLE,15));
            IDirect3DStateBlock9* block=nullptr;ok(extension.CreateStateBlock(D3DSBT_ALL,&block));
            ok(extension.BeginScene());
            for(unsigned draw=0;draw<12;++draw){
                float bank[1024];std::memcpy(bank,original,sizeof bank);
                if(scenario==1){bank[11*4+3]=.2f+.05f*draw;ok(extension.SetVertexShaderConstantF(11,bank+11*4,1));}
                if(scenario==2){bank[31*4+3]+=.005f*draw;ok(extension.SetVertexShaderConstantF(31,bank+31*4,3));}
                if(scenario==3){bank[2*4]+=.0001f*draw;bank[255*4]=float(draw);ok(extension.SetVertexShaderConstantF(2,bank+8,1));ok(extension.SetVertexShaderConstantF(255,bank+255*4,1));}
                if(scenario==4)ok(extension.SetVertexShaderConstantF(11,bank+44,1));
                if(scenario==5&&(draw==3||draw==8))mirror.invalidate();
                if(scenario==6&&(draw==3||draw==8)){ExtensionDevice::RawScope raw(extension);uint32_t bits=draw==3?0x80000000u:0x7fc12345u;std::memcpy(bank+17*4,&bits,4);ok(extension.SetVertexShaderConstantF(17,bank+17*4,1));}
                if(scenario==7&&(draw==3||draw==8))ok(block->Apply());
                if(scenario==8&&draw==3)normals.setTrustedPoseSource({});
                if(scenario==8&&draw==8)normals.setTrustedPoseSource(extension.trustedConstantSource());
                if(scenario==9&&draw==3)normals.setTrustedPoseSource(foreign.trustedConstantSource());
                if(scenario==11){int ints[4]={int(draw),0,0,0};BOOL boolean=draw&1;ok(extension.SetVertexShaderConstantI(0,ints,1));ok(extension.SetVertexShaderConstantB(0,&boolean,1));}
                auto* vertex=scenario==10&&(draw&1)?rigid:single;ok(extension.SetVertexShader(vertex));
                normals.capture(vertex,[&]{return extension.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,vertices,indices/3,mesh->indices.data(),D3DFMT_INDEX32,mesh->streams[0].bytes.data(),48);},true,&geometry,nullptr,D3DFMT_UNKNOWN,nullptr,0,&immutable,declaration);
                assert(normals.testPendingPackets()==draw+1);float expected[1024];ok(d->GetVertexShaderConstantF(0,expected,256));ModelNormalPacketTestAccess::exactBank(normals,expected);
            }
            const auto stats=normals.testConstantStats();const auto fetched=normals.testConstantReadRegisters();
            assert(normals.finishCaptures());ok(extension.EndScene());auto image=capturedPixels(normals.texture(),128,128);
            for(size_t at=6;at<image.size();at+=8){uint16_t distance;std::memcpy(&distance,image.data()+at,2);nonempty+=distance!=0;}
            if(!reuse){reference=image;baselineReads=fetched;beforeReads+=fetched;}else{assert(image==reference);afterReads+=fetched;epochHits+=stats.hits;blockHits+=stats.blockTests;
                assert(!stats.selfCheckMismatches&&fetched<=baselineReads);if(scenario<3||scenario==4||scenario==11)assert(fetched*3<baselineReads);}
            // A new frame must never inherit a pointer into recycled storage.
            normals.endFrame();assert(!normals.testConstantStats().hits&&!normals.testConstantReadRegisters());
            normals.setTrustedPoseSource(extension.trustedConstantSource());ok(extension.SetVertexShader(single));
            float next[1024];std::memcpy(next,original,sizeof next);next[31*4+3]+=.01f;ok(extension.SetVertexShaderConstantF(0,next,256));ok(extension.BeginScene());
            for(unsigned draw=0;draw<2;++draw){normals.capture(single,[&]{return extension.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,vertices,indices/3,mesh->indices.data(),D3DFMT_INDEX32,mesh->streams[0].bytes.data(),48);},true,&geometry,nullptr,D3DFMT_UNKNOWN,nullptr,0,&immutable,declaration);assert(normals.testPendingPackets()==draw+1);ModelNormalPacketTestAccess::exactBank(normals,next);}
            assert(normals.testConstantReadRegisters()>0&&normals.testConstantStats().hits==unsigned(reuse));assert(normals.finishCaptures());ok(extension.EndScene());auto nextImage=capturedPixels(normals.texture(),128,128);
            if(!reuse)nextReference=std::move(nextImage);else assert(nextImage==nextReference);block->Release();++cases;
        }
    }
    assert(nonempty>256&&afterReads*2<beforeReads&&epochHits&&blockHits);rigid->Release();depth->Release();output.read->Release();output.s->Release();output.t->Release();
    std::printf("MODEL_NORMALS_CONSTANT_REUSE_PASS cases=%u packets=%u beforeRegisters=%u afterRegisters=%u epochHits=%llu blockHits=%llu banks=bitexact pixels=bitexact resetRawApplyDetachSwitch=checked nonemptyPixels=%u timings=0\n",cases,cases*12,beforeReads,afterReads,(unsigned long long)epochHits,(unsigned long long)blockHits,nonempty);
}
