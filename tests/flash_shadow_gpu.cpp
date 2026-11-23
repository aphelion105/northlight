// Exercise the production maps, actor replay and subtraction pass, without WoW.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include <cassert>
static void must(HRESULT h){if(FAILED(h))std::fprintf(stderr,"HRESULT=%08lx\n",(unsigned long)h);assert(SUCCEEDED(h));}

struct FlashCalls:ForwardIDirect3DDevice9 {
 using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
 unsigned failMap=0,refused=0,mapAttempts=0;
 bool tracing=false;
 std::vector<const void*> objects;
 std::vector<unsigned> events;
 unsigned object(const void* p){
  if(!tracing||!p)return 0;
  auto it=std::find(objects.begin(),objects.end(),p);
  if(it!=objects.end())return unsigned(it-objects.begin())+1;
  objects.push_back(p);return unsigned(objects.size());
 }
 void add(unsigned op,unsigned a=0,unsigned b=0){if(tracing)events.insert(events.end(),{op,a,b});}
 void begin(){objects.clear();events.clear();tracing=true;}
 void finish(const char* name,unsigned draws){
  tracing=false;
  unsigned indexed=0,quad=0;
  for(size_t i=0;i<events.size();i+=3){indexed+=events[i]==11;quad+=events[i]==12;}
  assert(indexed+quad==draws);
  std::printf("FLASH_TRACE %s draws=%u indexed=%u quad=%u",name,draws,indexed,quad);
  for(auto value:events)std::printf(" %x",value);
  std::puts("");
 }
 HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared) override{
  if(w==1024&&h==1024&&format==D3DFMT_R32F&&usage==D3DUSAGE_RENDERTARGET){++mapAttempts;
   if(failMap&&!--failMap){++refused;*out=nullptr;return E_OUTOFMEMORY;}}
  return real->CreateTexture(w,h,levels,usage,format,pool,out,shared);
 }
 HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* value) override{add(1,n,object(value));return real->SetRenderTarget(n,value);}
 HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* value) override{add(2,object(value));return real->SetDepthStencilSurface(value);}
 HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* value) override{add(3,value->Width,value->Height);return real->SetViewport(value);}
 HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* rect,DWORD flags,D3DCOLOR color,float z,DWORD stencil) override{add(4,flags,color);return real->Clear(n,rect,flags,color,z,stencil);}
 HRESULT STDMETHODCALLTYPE SetTexture(DWORD n,IDirect3DBaseTexture9* value) override{add(5,n,object(value));return real->SetTexture(n,value);}
 HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* value) override{add(6,object(value));return real->SetVertexShader(value);}
 HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* value) override{add(7,object(value));return real->SetPixelShader(value);}
 HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* value) override{add(8,object(value));return real->SetVertexDeclaration(value);}
 HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT first,const float* data,UINT count) override{add(9,first,count);return real->SetVertexShaderConstantF(first,data,count);}
 HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT first,const float* data,UINT count) override{add(10,first,count);return real->SetPixelShaderConstantF(first,data,count);}
 HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT start,UINT count) override{
  add(11,start,count);return real->DrawIndexedPrimitive(type,base,min,vertices,start,count);
 }
 HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride) override{
  add(12,type,count);return real->DrawPrimitiveUP(type,count,data,stride);
 }
 HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD sampler,D3DSAMPLERSTATETYPE state,DWORD value) override{add(13,(sampler<<16)|state,value);return real->SetSamplerState(sampler,state,value);}
 HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state,DWORD value) override{add(14,state,value);return real->SetRenderState(state,value);}
 HRESULT STDMETHODCALLTYPE SetStreamSource(UINT slot,IDirect3DVertexBuffer9* buffer,UINT offset,UINT stride) override{
  add(15,slot,object(buffer));add(16,offset,stride);return real->SetStreamSource(slot,buffer,offset,stride);
 }
 HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT slot,UINT frequency) override{add(17,slot,frequency);return real->SetStreamSourceFreq(slot,frequency);}
 HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* buffer) override{add(18,object(buffer));return real->SetIndices(buffer);}
};

struct FlashShadowTest {
 static void run(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& pp){
  FlashCalls calls(device);IDirect3DDevice9* d=&calls;
  WorldRenderer w(d);w.valid=true;w.width=w.height=64;
  w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map="offline-flash";
  w.uploadedMap=w.active->map;w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.uploaded=w.active->bvh;
  for(unsigned i=0;i<16;++i)w.context.inverseView[i]=i%5==0?1.f:0.f;
  w.projection[0]=w.projection[1]=100000;w.projection[2]=1;
  const NorthlightGI::WorldVertex vertices[]={{{-10,-10,0},{0,0,-1},0,0},{{10,-10,0},{0,0,-1},1,0},{{-10,10,0},{0,0,-1},0,1},{{10,10,0},{0,0,-1},1,1}};
  const WORD indices[]={0,1,2,2,1,3};
  assert(w.meshStore.beginStaging({NorthlightMeshPageKey::keyOf(vertices,sizeof vertices,indices,sizeof indices)}));w.meshStore.commit();w.meshBuffers.resize(w.meshStore.entries().size());auto& page=w.meshBuffers[w.meshStore.bufferOf(0)];
  must(d->CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,&page.vb,nullptr));must(d->CreateIndexBuffer(sizeof indices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&page.ib,nullptr));void* bytes=nullptr;
  must(page.vb->Lock(0,sizeof vertices,&bytes,0));std::memcpy(bytes,vertices,sizeof vertices);must(page.vb->Unlock());must(page.ib->Lock(0,sizeof indices,&bytes,0));std::memcpy(bytes,indices,sizeof indices);must(page.ib->Unlock());
  D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
  const auto setupShadows=[&]{
   must(d->CreateVertexDeclaration(elements,&w.shadowPrograms.worldLayout_));must(d->CreateVertexShader(kShadowVSShader,&w.shadowPrograms.casterVertex_));must(d->CreatePixelShader(kShadowPSShader,&w.shadowPrograms.casterPixel_));must(d->CreatePixelShader(kShadowReplayPSShader,&w.shadowPrograms.replay_));
   must(d->CreateDepthStencilSurface(1024,1024,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&w.directionalMaps.depth_,nullptr));
  };
  setupShadows();
  w.materials.push_back(nullptr);w.uploadedAlphaCutoffs.push_back(-1);
  WorldRenderer::Batch batch{};batch.page=0;batch.count=2;batch.vertexCount=4;batch.terrain=true;batch.boundsLow={-10,-10,0};batch.boundsHigh={10,10,0};w.batches.push_back(batch);
  IDirect3DTexture9* clear=nullptr;must(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&clear,nullptr));D3DLOCKED_RECT lock{};
  must(clear->LockRect(0,&lock,nullptr,0));*static_cast<DWORD*>(lock.pBits)=0;must(clear->UnlockRect(0));
  w.materials.push_back(clear);w.uploadedAlphaCutoffs.push_back(.5f);
  NorthlightLocalLightSelection::Selection lights;lights.count=1;lights.sources[0]={{0,0,-1000},{4,2,1},.5f,2000,0xfed0000000000001ull,2,NorthlightLocalLights::Flash};
  auto& slots=w.flashShadows.slots_;
  auto maps=[&](bool fresh=true,bool withReplays=true){w.replayBoundsBegin();must(d->BeginScene());w.renderFlashShadows(lights,fresh,withReplays);must(d->EndScene());return slots[0].ready;};
  IDirect3DSurface9* mapRead=nullptr;must(d->CreateOffscreenPlainSurface(1024,1024,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&mapRead,nullptr));
  auto mapImage=[&](unsigned slot=0,unsigned cascade=0){must(d->GetRenderTargetData(slots[slot].surface[cascade],mapRead));must(mapRead->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> result(1024*1024);
   for(unsigned y=0;y<1024;++y)std::memcpy(result.data()+1024*y,static_cast<char*>(lock.pBits)+y*lock.Pitch,4096);must(mapRead->UnlockRect());return result;};
  calls.begin();assert(maps());calls.finish("world-casters",2);
  const auto reference=mapImage(),referenceFar=mapImage(0,1);assert(*std::min_element(reference.begin(),reference.end())<.9f);
  std::printf("FLASH_MAPS near=%llx far=%llx\n",
   (unsigned long long)NorthlightShaderTags::fnv1a(reference.data(),reference.size()*sizeof(float)),
   (unsigned long long)NorthlightShaderTags::fnv1a(referenceFar.data(),referenceFar.size()*sizeof(float)));
  w.batches[0].material=1;assert(maps());auto transparent=mapImage();assert(*std::min_element(transparent.begin(),transparent.end())==1);w.batches.clear();
  // Actual native-style row projection, patched by the same production patcher.
  const uint32_t original[]={0xfffe0300,
   0x0200001f,0x80000000,0x900f0000,0x0200001f,0x80000005,0x900f0001,
   0x0200001f,0x80000000,0xe00f0000,0x0200001f,0x80000005,0xe0030001,
   0x03000009,0xe0010000,0xa0e40002,0x90e40000,0x03000009,0xe0020000,0xa0e40003,0x90e40000,
   0x03000009,0xe0040000,0xa0e40004,0x90e40000,0x03000009,0xe0080000,0xa0e40005,0x90e40000,
   0x02000001,0xe0030001,0x90e40001,0xffff};
  std::vector<uint32_t> patched;assert(NorthlightShadowShader::patch(original,sizeof(original)/4,patched));
  auto replay=std::make_unique<WorldRenderer::Replay>();must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&replay->shader));
  replay->decl=w.shadowPrograms.worldLayout();replay->decl->AddRef();replay->stream[0]=page.vb;page.vb->AddRef();replay->stride[0]=sizeof(NorthlightGI::WorldVertex);replay->index=page.ib;page.ib->AddRef();
  replay->type=D3DPT_TRIANGLELIST;replay->indexed=true;replay->vertices=4;replay->count=2;replay->cutoff=-1;replay->projectionKind=2;replay->constantUsage.floats={2,4};
  w.replays.push_back(std::move(replay));calls.begin();assert(maps());calls.finish("actor-casters",2);assert(mapImage()==reference);
  // Move the original model transform (inverse view). The shadow follows, without a stale map.
  w.context.inverseView[12]=25;assert(maps());assert(mapImage()!=reference);w.context.inverseView[12]=0;assert(maps());assert(mapImage()==reference);
  // ActorShadows=0 must never dereference a stale replay, and must keep world shadows.
  auto stale=std::move(w.replays[0]);w.captureDemand=false;assert(maps(true,false)&&!w.captureDemand);
  for(unsigned cascade=0;cascade<2;++cascade){const auto image=mapImage(0,cascade);assert(*std::min_element(image.begin(),image.end())==1);}
  w.batches.push_back(batch);assert(maps(true,false)&&!w.captureDemand);assert(mapImage()==reference&&mapImage(0,1)==referenceFar);
  w.batches.clear();w.replays[0]=std::move(stale);assert(maps());assert(mapImage()==reference);
  // Match the exact production direct-light subtraction on a receiver behind the blocker.
  auto tex=[&](float x,float y=0,float z=0,float a=0){IDirect3DTexture9* t=nullptr;must(d->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&t,nullptr));must(t->LockRect(0,&lock,nullptr,0));const float p[]={x,y,z,a};std::memcpy(lock.pBits,p,16);must(t->UnlockRect(0));return t;};
  const float nearZ=.2f,farZ=2000,viewZ=30;
  auto depth=tex(farZ/(farZ-nearZ)*(1-nearZ/viewZ)),normal=tex(0,0,-1,1),zero=tex(0),base=tex(1,1,1,1);
  IDirect3DSurface9 *target=nullptr,*readback=nullptr;must(d->CreateRenderTarget(32,32,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));must(d->CreateOffscreenPlainSurface(32,32,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
  IDirect3DPixelShader9* direct=nullptr;must(d->CreatePixelShader(kLocalFlashShader,&direct));
  float c[84][4]{};c[0][0]=c[0][1]=1.f/64;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=c[1][1]=100000;c[1][2]=1;c[2][0]=1;c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;
  c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[20][2]=1;c[24][0]=NorthlightWorldMath::ShadowBiasWorld*NorthlightWorldMath::InverseShadowDepth;c[35][0]=NorthlightWorldMath::InverseShadowDepth;c[52][0]=c[52][1]=1;
  for(unsigned k=0;k<3;++k){c[36][k]=lights.sources[0].position[k];c[44][k]=lights.sources[0].diffuse[k];}c[36][3]=2000;c[44][3]=NorthlightLocalLightSelection::transport(lights.sources[0]);
  auto pixel=[&](bool shadow){must(d->SetDepthStencilSurface(nullptr));must(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp{0,0,32,32,0,1};must(d->SetViewport(&vp));must(d->SetVertexShader(nullptr));must(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
   for(unsigned s=0;s<16;++s){must(d->SetTexture(s,zero));for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(s,k,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
   must(d->SetTexture(1,depth));must(d->SetTexture(14,normal));must(d->SetTexture(12,base));must(d->SetPixelShaderConstantF(0,c[0],84));
   for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SCISSORTESTENABLE})d->SetRenderState(k,FALSE);
   must(d->SetPixelShader(direct));must(d->BeginScene());must(NorthlightScreen::drawViewportQuad(w.d,32,32));
   if(shadow){d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
    calls.begin();assert(w.renderFlashLighting());calls.finish("local-correction",1);}
   must(d->EndScene());must(d->GetRenderTargetData(target,readback));must(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));float value;std::memcpy(&value,static_cast<char*>(lock.pBits)+16*lock.Pitch+16*16,4);must(readback->UnlockRect());return value;};
  const float lit=pixel(false),dark=pixel(true);std::printf("FLASH_RECEIVER lit=%g shadowed=%g\n",lit,dark);assert(lit>.001f&&std::fabs(dark)<lit*.001f);
  w.replays.clear();assert(maps());const float open=pixel(true);assert(std::fabs(open-lit)<lit*.001f);
  // No current flash / incomplete capture cannot expose a previous frame's map.
  assert(!maps(false)&&w.captureDemand);lights.count=0;must(d->BeginScene());w.renderFlashShadows(lights,true,true);must(d->EndScene());assert(!slots[0].ready&&!slots[1].ready);
  lights.count=2;lights.sources[1]=lights.sources[0];++lights.sources[1].sourceId;lights.sources[1].position[0]=500;
  // Refuse the second cascade of the cold slot. The first slot may publish,
  // while the partially allocated slot stays invisible until both maps draw.
  calls.failMap=2;assert(maps()&&!slots[1].ready&&calls.refused==1);
  auto* partial=slots[1].map[0];assert(partial&&slots[1].surface[0]&&!slots[1].map[1]);
  const unsigned attempts=calls.mapAttempts;
  assert(!maps()&&calls.mapAttempts==attempts&&!slots[1].ready);
  w.flashShadows.flashRetryAt=GetTickCount();
  assert(maps()&&slots[1].ready&&slots[1].map[0]==partial&&calls.mapAttempts==attempts+1);
  for(unsigned i=0;i<16;++i)must(d->SetTexture(i,nullptr));
  must(d->SetPixelShader(nullptr));must(d->SetDepthStencilSurface(nullptr));
  IDirect3DSurface9* backbuffer=nullptr;must(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer));must(d->SetRenderTarget(0,backbuffer));backbuffer->Release();
  direct->Release();for(auto t:{depth,normal,zero,base})t->Release();readback->Release();target->Release();mapRead->Release();
  w.reset();assert(!w.flashShadows.ready()&&!w.flashShadows.flashShadowPS&&!w.flashShadows.flashRetryAt);
  w.flashShadows.visitTextures([](auto* texture){assert(!texture);});
  must(d->Reset(&pp));setupShadows();w.width=w.height=64;
  lights.count=1;w.batches.push_back(batch);
  must(d->CreateOffscreenPlainSurface(1024,1024,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&mapRead,nullptr));
  assert(maps()&&mapImage()==reference&&mapImage(0,1)==referenceFar);
  mapRead->Release();
  std::puts("PASS FLASH_OWNER partial allocation, retry, D3D Reset, rebuilt cascade pixels");
  std::puts("PASS FLASH_SHADOW production cascades, alpha cutout, moving model replay, ActorShadows=0 preserves world shadows without replay reads, exact radiance subtraction, expiry, two flashes");
 }
};
int main(int argc,char** argv){
 assert(argc==3);SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[2]);auto path=std::wstring(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightFlashShadowTest";assert(RegisterClassW(&wc));auto window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
 IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&d));FlashShadowTest::run(d,pp);
 d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
