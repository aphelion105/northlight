// Production source capture, then actual point-shadow rasterization; no game.
#define NOMINMAX
#include "renderer.cpp"
#include <cassert>
#include <chrono>
#include <fstream>
static void checked(HRESULT hr){assert(SUCCEEDED(hr));}
struct CaptureMeter:ForwardIDirect3DDevice9 {
 using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;unsigned geometry=0;bool failDeclaration=false;D3DRENDERSTATETYPE failState=D3DRENDERSTATETYPE(0);
 HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE key,DWORD* p)override{if(key==failState)return E_FAIL;return real->GetRenderState(key,p);}
 HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** p)override{if(failDeclaration){*p=nullptr;return E_FAIL;}return real->GetVertexDeclaration(p);}
 HRESULT STDMETHODCALLTYPE GetStreamSource(UINT s,IDirect3DVertexBuffer9** p,UINT* o,UINT* stride)override{++geometry;return real->GetStreamSource(s,p,o,stride);}
 HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT s,UINT* frequency)override{++geometry;return real->GetStreamSourceFreq(s,frequency);}
 HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** p)override{++geometry;return real->GetIndices(p);}
};
struct CaptureRasterSource {
 ExtensionDevice* device;unsigned batches=0;
#if NORTHLIGHT_CAPTURE_RASTER
 static bool read(void* context,const D3DRENDERSTATETYPE* keys,unsigned count,DWORD* out){auto& self=*static_cast<CaptureRasterSource*>(context);++self.batches;return self.device->knownRenderStates(keys,count,out);}
#endif
};
static IDirect3DVertexBuffer9* sourceVB=nullptr;static IDirect3DIndexBuffer9* sourceIB=nullptr;
static D3DVERTEXBUFFER_DESC vertexDesc{};static D3DINDEXBUFFER_DESC indexDesc{};static uint64_t revision=1;
static uint64_t sourceIdentity(void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));}
static uint64_t sourceVersion(void*,bool){return revision;}
static void metadata(const NorthlightCaptureMetadata::Request* requests,NorthlightCaptureMetadata::Info* info,unsigned count){
 for(unsigned i=0;i<count;++i){info[i]={};if(requests[i].buffer!=sourceVB&&requests[i].buffer!=sourceIB)continue;auto& v=info[i];v.known=true;v.identity=sourceIdentity(requests[i].buffer,requests[i].index);v.revision=revision;v.size=requests[i].index?indexDesc.Size:vertexDesc.Size;v.usage=requests[i].index?indexDesc.Usage:vertexDesc.Usage;v.format=requests[i].index?indexDesc.Format:D3DFMT_UNKNOWN;}
}
struct PointTerrainCoverageTest {
 static void setup(WorldRenderer& w,IDirect3DDevice9* d,IDirect3DVertexShader9* original,IDirect3DVertexShader9* patched,const DWORD* code,size_t words){
  w.valid=true;w.captureMode=WorldRenderer::CaptureFresh;w.effects={false,true,false};w.quality.gi=0;w.quality.replayJobs=0;w.quality.actorShadows=1;
  w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map=w.lastRequest.map="capture-handoff";w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.uploaded=w.active->bvh;w.uploadedMap=w.active->map;w.meshGeneration=1;
  for(unsigned i=0;i<16;++i)w.context.view[i]=w.context.inverseView[i]=i%5==0?1.f:0.f;w.projection[0]=w.projection[1]=w.projection[2]=1;
  w.sourceColors[0]={1,1,1};w.sourceWeights[0]=1;w.celestialValid=true;w.lastActorCapture=GetTickCount();
  WorldRenderer::CaptureShader shader;shader.replacement=patched;patched->AddRef();shader.projectionKind=2;shader.usage=NorthlightShaderConstants::analyze(reinterpret_cast<const uint32_t*>(code),words,2);
  w.captureShaders.emplace(original,std::move(shader)); // Authored rigid VS: conservative unbounded replay.
  w.replaySnapshots.setIdentityProvider(sourceIdentity);w.replaySnapshots.setVersionProvider(sourceVersion);w.replaySnapshots.setMetadataProvider(metadata);
  const NorthlightGI::WorldVertex vertices[]={{{-1,-1,0},{0,0,1},0,0},{{1,-1,0},{0,0,1},1,0},{{-1,1,0},{0,0,1},0,1},{{1,1,8},{0,0,-1},1,1},{{-1,1,8},{0,0,-1},0,1},{{1,-1,8},{0,0,-1},1,0}};
  std::vector<WORD> indices;for(unsigned i=0;i<256;++i)for(WORD index:{WORD(i%2?3:0),WORD(i%2?4:1),WORD(i%2?5:2)})indices.push_back(index);
  assert(w.meshStore.beginStaging({NorthlightMeshPageKey::keyOf(vertices,sizeof vertices,indices.data(),indices.size()*2)}));w.meshStore.commit();w.meshBuffers.resize(w.meshStore.entries().size());auto& page=w.meshBuffers[w.meshStore.bufferOf(0)];
  checked(d->CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,&page.vb,nullptr));checked(d->CreateIndexBuffer(UINT(indices.size()*2),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&page.ib,nullptr));void* p=nullptr;checked(page.vb->Lock(0,0,&p,0));std::memcpy(p,vertices,sizeof vertices);checked(page.vb->Unlock());checked(page.ib->Lock(0,0,&p,0));std::memcpy(p,indices.data(),indices.size()*2);checked(page.ib->Unlock());w.vertices=page.vb;w.indices=page.ib;sourceVB=page.vb;sourceIB=page.ib;checked(sourceVB->GetDesc(&vertexDesc));checked(sourceIB->GetDesc(&indexDesc));
 }
 static void configureProducer(WorldRenderer& w,NorthlightDrawSnapshot::Frame& producer){
  producer.setIdentityProvider(sourceIdentity);producer.setVersionProvider(sourceVersion);producer.setMetadataProvider(metadata);
#if NORTHLIGHT_SHARED_SOURCE
  producer.setChargeResidentHits(false);assert(producer.shareSnapshotPool(w.replaySnapshots));
#endif
  producer.setDeclarationProvider(&w.declarationCache,[](void* c,IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& e,UINT& n){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(decl,e,n);});
  producer.setLayoutProvider(&w.declarationCache,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(decl,extent);});
 }
 static void begin(WorldRenderer& w){for(auto& p:w.replays)w.recycleReplay(p.release());w.replays.clear();w.replaySnapshots.clearFrame();w.valid=true;w.captureMode=WorldRenderer::CaptureFresh;
#if NORTHLIGHT_CAPTURE_HANDOFF
  w.captureSourceHandoffs=0;
#endif
 }
 static void capture(WorldRenderer& w,IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* decl,const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& mesh,bool handoff){
#if NORTHLIGHT_CAPTURE_HANDOFF
  w.capture(D3DPT_TRIANGLELIST,0,0,6,0,256,true,shader,false,false,nullptr,handoff?&mesh:nullptr,decl);
#else
  (void)decl;(void)mesh;(void)handoff;w.capture(D3DPT_TRIANGLELIST,0,0,6,0,256,true,shader,false,false);
#endif
 }
 static unsigned packets(const WorldRenderer& w){return unsigned(w.replays.size());}
 static unsigned handoffs(const WorldRenderer& w){
#if NORTHLIGHT_CAPTURE_HANDOFF
  return w.captureSourceHandoffs;
#else
  (void)w;return 0;
#endif
 }
 static size_t charged(const WorldRenderer& w){return w.replaySnapshots.bytesRead();}
#if NORTHLIGHT_CAPTURE_HANDOFF
 static void controls(WorldRenderer& w,CaptureMeter& d,DeviceMirror& mirror,IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* declaration,NorthlightDrawSnapshot::Frame& producer){
  const NorthlightDrawSnapshot::Draw draw{D3DPT_TRIANGLELIST,0,0,6,0,256,true};NorthlightDrawSnapshot::Mesh scratch;
  D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1];UINT count=MAXD3DDECLLENGTH+1;checked(declaration->GetDeclaration(elements,&count));IDirect3DVertexDeclaration9* duplicate=nullptr;checked(d.CreateVertexDeclaration(elements,&duplicate));
  for(unsigned reason=0;reason<5;++reason){begin(w);producer.clearFrame();std::shared_ptr<const NorthlightDrawSnapshot::Mesh> source;assert(producer.read(&d,declaration,draw,scratch,nullptr,false,&source)&&source);
   if(reason==1)producer.clearSnapshotCache();
   if(reason==2)source=std::make_shared<const NorthlightDrawSnapshot::Mesh>(*source); // Same identity/data, not a resident object.
   mirror.invalidate();d.failDeclaration=reason==4;capture(w,shader,reason==3?duplicate:declaration,source,true);d.failDeclaration=false;
   assert(packets(w)==(reason==4?0u:1u)&&handoffs(w)==(reason==0?1u:0u));
  }duplicate->Release();puts("CAPTURE_HANDOFF_CONTROLS_PASS resident=1 evicted_fallback=1 reconstructed_fallback=1 declaration_fallback=1 declaration_failure=1");
 }
#endif
#if NORTHLIGHT_CAPTURE_RASTER
 static void stateControls(WorldRenderer& w,CaptureRasterSource& source,CaptureMeter& raw,DeviceMirror& mirror,IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* declaration,NorthlightDrawSnapshot::Frame& producer){
  const D3DRENDERSTATETYPE keys[]={D3DRS_COLORWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE};
  const NorthlightDrawSnapshot::Draw draw{D3DPT_TRIANGLELIST,0,0,6,0,256,true};NorthlightDrawSnapshot::Mesh scratch;unsigned cases=0;
  for(unsigned scenario=0;scenario<11;++scenario){unsigned expected=0;
   for(bool batch:{false,true}){
    mirror.enabled=true;mirror.recording=false;mirror.rawDepth=0;mirror.invalidate();
    checked(raw.SetRenderState(D3DRS_COLORWRITEENABLE,scenario==1?0:15));checked(raw.SetRenderState(D3DRS_ALPHABLENDENABLE,scenario==2));checked(raw.SetRenderState(D3DRS_ALPHATESTENABLE,scenario==3));checked(raw.SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_NEVER));checked(raw.SetRenderState(D3DRS_ALPHAREF,128));
    for(auto key:keys){DWORD value=0;checked(source.device->GetRenderState(key,&value));}
    if(scenario==4)mirror.renderStateKnown[D3DRS_ALPHATESTENABLE]=false;
    if(scenario==5)mirror.recording=true;
    if(scenario==6)mirror.rawDepth=1;
    if(scenario==7)mirror.enabled=false;
    if(scenario>=8){mirror.invalidate();raw.failState=keys[scenario-8];}
    begin(w);producer.clearFrame();std::shared_ptr<const NorthlightDrawSnapshot::Mesh> mesh;assert(producer.read(source.device,declaration,draw,scratch,nullptr,false,&mesh)&&mesh);
    w.setCaptureRasterStateSource(batch?&source:nullptr,batch?&CaptureRasterSource::read:nullptr);
    capture(w,shader,declaration,mesh,true);const unsigned actual=packets(w);
    if(!batch)expected=actual;else assert(actual==expected);++cases;
    raw.failState=D3DRENDERSTATETYPE(0);mirror.enabled=true;mirror.recording=false;mirror.rawDepth=0;mirror.invalidate();
   }
   if(scenario==0||scenario==4||scenario==5||scenario==6||scenario==7||scenario==8)assert(expected==1);else assert(expected==0);
  }
  checked(raw.SetRenderState(D3DRS_COLORWRITEENABLE,15));checked(raw.SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));checked(raw.SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));mirror.invalidate();
  printf("CAPTURE_RASTER_CONTROLS_PASS cases=%u partial_unknown recording raw disabled native_failure fade blend alpha\n",cases);
 }
#endif
 static void packetImage(const WorldRenderer& w,const std::string& path){std::ofstream out(path,std::ios::binary);for(const auto& p:w.replays){const auto& mesh=p->mesh();out.write((const char*)&p->count,sizeof p->count);for(const auto& stream:mesh.streams)out.write((const char*)stream.bytes.data(),stream.bytes.size());out.write((const char*)mesh.indices.data(),mesh.indices.size()*4);out.write((const char*)(p->constants+8),64);}}
 static void raster(WorldRenderer& w,IDirect3DDevice9* d,const std::string& path){assert(w.uploadReplay());w.quality.pointShadowFacesPerFrame=6;w.quality.pointShadowRefreshMs=0;
  NorthlightLocalLights::Light light{{0,0,4},{1,.4f,.1f},1,12,17,2,
#if NORTHLIGHT_SHARED_SOURCE
   NorthlightLocalLights::CastShadow
#else
   0
#endif
  };checked(d->SetRenderState(D3DRS_ZENABLE,TRUE));checked(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));checked(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));checked(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));checked(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));checked(d->BeginScene());
#if NORTHLIGHT_SHARED_SOURCE
  assert(w.renderPointShadow(light,true,true));
#else
  w.active->localLights.push_back(light);assert(w.renderPointShadow(true,true));
#endif
  checked(d->EndScene());
  IDirect3DSurface9* read=nullptr;checked(d->CreateOffscreenPlainSurface(256,256,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&read,nullptr));std::ofstream out(path,std::ios::binary);bool visible=false;
  for(unsigned f=0;f<6;++f){IDirect3DSurface9* face=nullptr;
#if NORTHLIGHT_SHARED_SOURCE
   checked(w.pointSlots[0].cube->GetCubeMapSurface(D3DCUBEMAP_FACES(f),0,&face));
#else
   checked(w.pointCube->GetCubeMapSurface(D3DCUBEMAP_FACES(f),0,&face));
#endif
   checked(d->GetRenderTargetData(face,read));face->Release();D3DLOCKED_RECT lock{};checked(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(unsigned y=0;y<256;++y){const auto* row=(const float*)((const char*)lock.pBits+y*lock.Pitch);for(unsigned x=0;x<256;++x)visible|=row[x]<.99f;out.write((const char*)row,256*4);}checked(read->UnlockRect());}
  assert(visible);read->Release();
 }
};
int main(int argc,char** argv){assert(argc==4||argc==5);std::setvbuf(stdout,nullptr,_IONBF,0);logFile=nullptr;const std::string out=argv[2];const bool handoff=std::atoi(argv[3])!=0,raster=argc==5&&std::atoi(argv[4]);assert(MultiByteToWideChar(CP_UTF8,0,(out+"/").c_str(),-1,rootPath,MAX_PATH));
 auto hwnd=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(hwnd);auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto* api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* raw=nullptr;checked(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));CaptureMeter meter(raw);DeviceMirror mirror;ExtensionDevice device(&meter,&mirror);auto* d=&device;CaptureRasterSource rasterSource{d};
 const DWORD code[]={0xfffe0300,0x0200001f,0x80000000,0x900f0000,0x0200001f,0x80000005,0x900f0001,0x0200001f,0x80000000,0xe00f0000,0x0200001f,0x80000005,0xe0030001,0x03000009,0xe0010000,0xa0e40002,0x90e40000,0x03000009,0xe0020000,0xa0e40003,0x90e40000,0x03000009,0xe0040000,0xa0e40004,0x90e40000,0x03000009,0xe0080000,0xa0e40005,0x90e40000,0x02000001,0xe0030001,0x90e40001,0xffff};
 IDirect3DVertexShader9 *original=nullptr,*patched=nullptr;checked(d->CreateVertexShader(code,&original));std::vector<uint32_t> patch;assert(NorthlightShadowShader::patch((const uint32_t*)code,sizeof code/4,patch));checked(d->CreateVertexShader((const DWORD*)patch.data(),&patched));auto world=std::make_unique<WorldRenderer>(d);PointTerrainCoverageTest::setup(*world,d,original,patched,code,sizeof code/4);
#if NORTHLIGHT_CAPTURE_RASTER
 if(raster)world->setCaptureRasterStateSource(&rasterSource,&CaptureRasterSource::read);
#endif
 const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};IDirect3DVertexDeclaration9* declaration=nullptr;checked(d->CreateVertexDeclaration(elements,&declaration));checked(d->SetVertexDeclaration(declaration));checked(d->SetVertexShader(original));checked(d->SetStreamSource(0,sourceVB,0,sizeof(NorthlightGI::WorldVertex)));checked(d->SetIndices(sourceIB));float constants[1024]{};constants[8]=constants[13]=constants[18]=constants[22]=1;checked(d->SetVertexShaderConstantF(0,constants,256));checked(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));checked(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));checked(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
 NorthlightDrawSnapshot::Frame producer;PointTerrainCoverageTest::configureProducer(*world,producer);const NorthlightDrawSnapshot::Draw draw{D3DPT_TRIANGLELIST,0,0,6,0,256,true};NorthlightDrawSnapshot::Mesh scratch;
 for(unsigned frame=0;frame<48;++frame){const auto calls=meter.geometry,batches=rasterSource.batches;const auto start=std::chrono::steady_clock::now();PointTerrainCoverageTest::begin(*world);producer.clearFrame();
  for(unsigned i=0;i<512;++i){std::shared_ptr<const NorthlightDrawSnapshot::Mesh> mesh;assert(producer.read(d,declaration,draw,scratch,nullptr,false,&mesh)&&mesh);PointTerrainCoverageTest::capture(*world,original,declaration,mesh,handoff);}
  const auto end=std::chrono::steady_clock::now();assert(PointTerrainCoverageTest::packets(*world)==512);printf("CAPTURE_FRAME frame=%u warm=%u handoff=%u cpuMs=%.9f packets=%u chargedBytes=%zu handoffs=%u geometryCalls=%u rasterState=%u stateBatches=%u\n",frame,unsigned(frame<16),unsigned(handoff),std::chrono::duration<double,std::milli>(end-start).count(),PointTerrainCoverageTest::packets(*world),PointTerrainCoverageTest::charged(*world),PointTerrainCoverageTest::handoffs(*world),meter.geometry-calls,unsigned(raster),rasterSource.batches-batches);
 }
 PointTerrainCoverageTest::packetImage(*world,out+"/packets.bin");PointTerrainCoverageTest::raster(*world,d,out+"/faces.bin");
#if NORTHLIGHT_CAPTURE_HANDOFF
 checked(d->SetVertexDeclaration(declaration));checked(d->SetVertexShader(original));checked(d->SetPixelShader(nullptr));checked(d->SetStreamSource(0,sourceVB,0,sizeof(NorthlightGI::WorldVertex)));checked(d->SetIndices(sourceIB));checked(d->SetVertexShaderConstantF(0,constants,256));PointTerrainCoverageTest::controls(*world,meter,mirror,original,declaration,producer);
#endif
#if NORTHLIGHT_CAPTURE_RASTER
 PointTerrainCoverageTest::stateControls(*world,rasterSource,meter,mirror,original,declaration,producer);
#endif
 world.reset();puts("WORLD_CAPTURE_HANDOFF_GPU_PASS packets=512 faces=6 authored_rigid_native_shader no_game=1");return 0;
}
