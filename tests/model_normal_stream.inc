// Real ModelNormals misses with a zero-byte resident cache. Compare the UP
// reference against append/wrap/discard, shared-source reuse and failure fallback.
struct ModelNormalStreamTest {
 static void enable(NorthlightModelNormals& n,bool value){n.streamEnabled=value;}
 static unsigned draws(const NorthlightModelNormals& n){return n.streamedDraws;}
 static size_t bytes(const NorthlightModelNormals& n){return n.streamedBytes;}
 static size_t saved(const NorthlightModelNormals& n){return n.streamSavedBytes;}
 static unsigned batches(const NorthlightModelNormals& n){return n.streamedBatches;}
 static unsigned discards(const NorthlightModelNormals& n){return n.streamedDiscards;}
 static unsigned failures(const NorthlightModelNormals& n){return n.streamFailures;}
};
static void normalStream(IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* declaration,
        unsigned vertexCount,unsigned indexCount,const std::vector<unsigned char>& source,const std::string& directory){
 SavedState saved(d);assert(saved.ok);constexpr unsigned N=128,W=384,H=288;
 auto color=target(W,H,D3DFMT_A8R8G8B8,4);IDirect3DSurface9* depth=nullptr;
 ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
 ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));
 ok(d->SetRenderTarget(0,color.s));ok(d->SetDepthStencilSurface(depth));viewport(color);
 ok(d->SetVertexDeclaration(declaration));ok(d->SetVertexShader(shader));auto* pixel=ps(directory+"ModelPS.bin");ok(d->SetPixelShader(pixel));
 for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE})ok(d->SetRenderState(key,FALSE));
 ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
 ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
 float bank[256][4];ok(d->GetVertexShaderConstantF(0,bank[0],256));
 std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> meshes;const auto primitives=indexCount/3;assert(primitives>=N);
 for(unsigned i=0;i<=N;++i){auto m=std::make_shared<NorthlightDrawSnapshot::Mesh>();m->indexed=true;m->topology=D3DPT_TRIANGLELIST;
  const auto first=i==N?0:i*primitives/N;m->vertexCount=vertexCount;m->primitiveCount=i==N?primitives:(i+1)*primitives/N-first;m->streams[0].stride=48;
  m->streams[0].bytes.assign(source.begin()+12,source.begin()+12+vertexCount*48);m->indices.resize(m->primitiveCount*3);
  std::memcpy(m->indices.data(),source.data()+12+vertexCount*48+first*12,m->indices.size()*4);m->sourceIdentity=NorthlightSourceIdentity::make({0x57ea1u,i});meshes.push_back(std::move(m));}
 struct Calls:ForwardIDirect3DDevice9 {
  using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;unsigned indexed=0,up=0,creates=0,queries=0;int fail=0;
  HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT n,DWORD u,DWORD f,D3DPOOL p,IDirect3DVertexBuffer9** b,HANDLE* h)override{++creates;if(fail==1){*b=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}return real->CreateVertexBuffer(n,u,f,p,b,h);}
  HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT n,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DIndexBuffer9** b,HANDLE* h)override{++creates;if(fail==2){*b=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}return real->CreateIndexBuffer(n,u,f,p,b,h);}
  HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE t,IDirect3DQuery9** q)override{++queries;if(fail==3){*q=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}return real->CreateQuery(t,q);}
  HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT b,UINT m,UINT v,UINT s,UINT c)override{++indexed;return real->DrawIndexedPrimitive(t,b,m,v,s,c);}
  HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT m,UINT v,UINT c,const void* indices,D3DFORMAT f,const void* data,UINT stride)override{++up;return real->DrawIndexedPrimitiveUP(t,m,v,c,indices,f,data,stride);}
 } calls(d);
 for(unsigned mode=0;mode<3;++mode){
  std::vector<unsigned char> reference;unsigned draws[2]={};size_t bytes[2]={};double submit[2]={},complete[2]={};
  for(unsigned variant=0;variant<5;++variant){const bool streamed=variant!=0;calls.fail=variant==2?1:variant==3?2:variant==4?3:0;
   auto normals=std::make_unique<NorthlightModelNormals>(&calls);normals->setDeferredCapture(true);ModelNormalStreamTest::enable(*normals,streamed);
   NorthlightGpuScene::NativeStore store;NorthlightGpuScene::NativeClient cache(store,NorthlightGpuScene::Consumer::Material);cache.setLimit(0);normals->setGeometryCache(&cache);
   for(unsigned frame=0;frame<4;++frame){normals->endFrame();store.setFrame(frame+1);calls.indexed=calls.up=calls.creates=calls.queries=0;ok(d->SetVertexShaderConstantF(0,bank[0],256));
    const auto start=std::chrono::steady_clock::now();ok(d->BeginScene());
    for(unsigned i=0;i<N;++i){const auto& mesh=meshes[mode?N:i];
     // The repeated source remains shared while raster and pose inputs differ.
     ok(d->SetRenderState(D3DRS_CULLMODE,mode==2&&i%2?D3DCULL_CCW:D3DCULL_NONE));
     if(mode==2){auto row=bank[31];float pose[4]={row[0],row[1],row[2],row[3]+float(i%3)*.02f};ok(d->SetVertexShaderConstantF(31,pose,1));}
     const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,vertexCount,0,mesh->primitiveCount,true};
     normals->capture(shader,[]{return D3D_OK;},true,&geometry,nullptr,D3DFMT_UNKNOWN,nullptr,0,&mesh,declaration);
    }
    assert(normals->testPendingPackets()==N);assert(normals->finishCaptures());ok(d->EndScene());const auto submitted=std::chrono::steady_clock::now();
    const auto image=capturedPixels(normals->texture(),W,H);const auto completed=std::chrono::steady_clock::now();assert(std::any_of(image.begin(),image.end(),[](unsigned char x){return x!=0;}));
    if(variant==0&&frame==0)reference=image;else assert(image==reference);
    assert(!cache.bytes()&&!cache.uploaded()&&normals->cachedDraws()==0);
    if(variant==1||variant==4){assert(calls.up==0&&ModelNormalStreamTest::draws(*normals)==N&&ModelNormalStreamTest::failures(*normals)==0);
     assert(calls.indexed==(mode==0?2u:mode==1?1u:N));assert(calls.creates==(frame==0?2u:0u));
     assert(calls.queries<=1);if(mode==0&&frame==3)assert(ModelNormalStreamTest::discards(*normals)>0);
     if(mode){assert(ModelNormalStreamTest::bytes(*normals)==meshes[N]->byteSize()&&ModelNormalStreamTest::saved(*normals)==(N-1)*meshes[N]->byteSize());}
    }else{assert(calls.up==N&&calls.indexed==0);assert(ModelNormalStreamTest::failures(*normals)==(variant?1u:0u));assert(calls.creates<3);}
    if(frame&&variant<2){draws[variant]=calls.up+calls.indexed;bytes[variant]=variant?ModelNormalStreamTest::bytes(*normals):mode?N*meshes[N]->byteSize():N*meshes[0]->streams[0].bytes.size()+indexCount*4;
     submit[variant]+=std::chrono::duration<double,std::milli>(submitted-start).count()/3;complete[variant]+=std::chrono::duration<double,std::milli>(completed-start).count()/3;}
   }
   normals.reset();store.clearAll();
  }
  std::printf("MODEL_NORMALS_STREAM_PASS mode=%u pixels=bitexact packets=%u draws=%u/%u bytes=%zu/%zu submitMeanMs=%.6f/%.6f completeReadbackMeanMs=%.6f/%.6f zeroResidentCache=1 allocationFailures=2 queryFailure=1\n",mode,N,draws[0],draws[1],bytes[0],bytes[1],submit[0],submit[1],complete[0],complete[1]);
 }
 pixel->Release();depth->Release();color.read->Release();color.s->Release();color.t->Release();
}
