// Exercise the real Device::Reset path, shared CPU sources and private programs.
#undef NDEBUG
#define NORTHLIGHT_MODEL_NORMALS_TEST
#include "../src/proxy/renderer.cpp"
#include <cassert>
#include <fstream>
static void must(HRESULT hr){if(FAILED(hr))std::printf("HRESULT=%08lx\n",(unsigned long)hr);assert(SUCCEEDED(hr));}
struct ModelNormalPacketTestAccess {static void targets(NorthlightModelNormals& n){assert(n.targets(64,64));}};
struct ResetMeter:ForwardIDirect3DDevice9 {
 using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;unsigned vertexCreates=0,pixelCreates=0;
 HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out)override{++vertexCreates;return real->CreateVertexShader(code,out);}
 HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{++pixelCreates;return real->CreatePixelShader(code,out);}
};
struct PipelineOffTest {
 static std::vector<float> image(Device& proxy){
  auto* d=proxy.ext;SavedState saved(d);assert(saved.ok);auto& w=*proxy.world;for(unsigned i=0;i<3;++i)w.prewarmPointShaders();assert(w.pointShadowVS&&w.pointShadowPS);
  NorthlightDrawSnapshot::Ref<IDirect3DTexture9> texture;NorthlightDrawSnapshot::Ref<IDirect3DSurface9> target,read,depth;
  must(d->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,texture.out(),nullptr));must(texture->GetSurfaceLevel(0,target.out()));must(d->CreateOffscreenPlainSurface(64,64,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,read.out(),nullptr));must(d->CreateDepthStencilSurface(64,64,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,depth.out(),nullptr));
  for(unsigned i=1;i<4;++i)must(d->SetRenderTarget(i,nullptr));must(d->SetDepthStencilSurface(nullptr));must(d->SetRenderTarget(0,target.p));must(d->SetDepthStencilSurface(depth.p));D3DVIEWPORT9 viewport{0,0,64,64,0,1};must(d->SetViewport(&viewport));
  for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE})must(d->SetRenderState(key,TRUE));for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_CLIPPLANEENABLE})must(d->SetRenderState(key,0));must(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));must(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));must(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
  const float matrix[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},material[]={0,0,0,-1},sphere[]={100,1.f/64,0,0};must(d->SetVertexShaderConstantF(0,matrix,4));must(d->SetPixelShaderConstantF(0,material,1));must(d->SetPixelShaderConstantF(1,sphere,1));must(d->SetVertexShader(w.pointShadowVS));must(d->SetPixelShader(w.pointShadowPS));
  D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;must(d->CreateVertexDeclaration(elements,decl.out()));must(d->SetVertexDeclaration(decl.p));
  const float triangle[]={-.6f,-.6f,.5f,0,0,1,0,0,.6f,-.6f,.5f,0,0,1,1,0,0,.6f,.5f,0,0,1,.5f,1};must(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));must(d->BeginScene());must(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,triangle,32));must(d->EndScene());must(d->GetRenderTargetData(target.p,read.p));D3DLOCKED_RECT lock{};must(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> result(4096);for(unsigned y=0;y<64;++y)std::memcpy(result.data()+y*64,static_cast<char*>(lock.pBits)+y*lock.Pitch,256);must(read->UnlockRect());assert(std::count_if(result.begin(),result.end(),[](float z){return z<1.f;})>256);return result;
 }
 static void run(Device& proxy,ResetMeter& meter,D3DPRESENT_PARAMETERS base,const std::vector<uint32_t>& code,bool preserve){
  auto& w=*proxy.world;auto& frame=proxy.autoBump->captureSources();auto* d=proxy.ext;
  IDirect3DVertexShader9* shader=nullptr;must(proxy.CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&shader));
  D3DVERTEXELEMENT9 layout[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};IDirect3DVertexDeclaration9* decl=nullptr;must(proxy.CreateVertexDeclaration(layout,&decl));
  struct Vertex {float xyz[3];unsigned char weights[4],bones[4];float normal[3],uv[2],extra[2];};static_assert(sizeof(Vertex)==48);
  Vertex vertices[]={{{-.6f,-.6f,.5f},{255},{0},{0,0,1},{0,0},{}},{{.6f,-.6f,.5f},{255},{0},{0,0,1},{1,0},{}},{{0,.6f,.5f},{255},{0},{0,0,1},{.5f,1},{}}};const WORD indices[]={0,1,2};
  IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;must(proxy.CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,&vb,nullptr));must(proxy.CreateIndexBuffer(sizeof indices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr));
  void* bytes=nullptr;must(vb->Lock(0,0,&bytes,0));std::memcpy(bytes,vertices,sizeof vertices);must(vb->Unlock());must(ib->Lock(0,0,&bytes,0));std::memcpy(bytes,indices,sizeof indices);must(ib->Unlock());
  auto bind=[&]{must(proxy.SetVertexDeclaration(decl));must(proxy.SetVertexShader(shader));must(proxy.SetStreamSource(0,vb,0,48));must(proxy.SetIndices(ib));};bind();
  const NorthlightDrawSnapshot::Draw draw{D3DPT_TRIANGLELIST,0,0,3,0,1,true};NorthlightActorDeformation::Program program;assert(NorthlightActorDeformation::compile(code.data(),code.size(),program,false,2));
  float bank[1024]={},inverse[16]={};for(unsigned i=0;i<4;++i){bank[(2+i)*4+i]=1;inverse[i*5]=1;}bank[31*4]=bank[32*4+1]=bank[33*4+2]=1;bank[11*4+3]=1;
  auto capture=[&]{frame.clearFrame();NorthlightDrawSnapshot::Mesh scratch;std::shared_ptr<const NorthlightDrawSnapshot::Mesh> source;assert(frame.read(d,decl,draw,scratch,nullptr,false,&source)&&source&&source->sourceIdentity);return source;};
  auto source=capture();const auto identity=source->sourceIdentity;std::weak_ptr<const NorthlightDrawSnapshot::Mesh> weak=source;
  auto bounds=[&](const auto& mesh,bool readyOnly,unsigned& built){w.replayBoundsCache.beginFrame(false,1);NorthlightReplayBounds::Budget budget;budget.maxVertices=100000;budget.maxOperations=1000000;NorthlightReplayBounds::Bounds box;
   const auto status=readyOnly?w.replayBoundsCache.calculateReady(program,*mesh,mesh,layout,6,bank,inverse,budget,box):w.replayBoundsCache.calculatePhase(program,*mesh,mesh,layout,6,bank,inverse,budget,box,NorthlightReplayBounds::EnvelopeCache::Stage::All);built=unsigned(w.replayBoundsCache.envelopeStats().buildVertices);return std::make_pair(status,box);};
  unsigned built=0;const auto first=bounds(source,false,built);assert(first.first==NorthlightReplayBounds::Status::Valid&&built);source.reset();assert(w.pointSoftShaders());auto* oldSoftLight=w.pointSoftLightingPS;const auto beforeImage=image(proxy);auto* oldVS=w.pointShadowVS;auto* oldPS=w.pointShadowPS;
  const auto revision=NorthlightTrackedBuffers::version(vb,false);assert(revision);const auto entries=frame.snapshotCacheEntries(),sourceBytes=frame.sharedSourceBytes();assert(entries&&sourceBytes);
  if(!preserve){proxy.releaseResources();w.reset();w.releasePointGPU();NorthlightTrackedBuffers::invalidateAll();assert(weak.expired());}
  auto bad=base;bad.SwapEffect=static_cast<D3DSWAPEFFECT>(0x7fffffff);assert(FAILED(proxy.Reset(&bad)));assert(frame.snapshotCacheEntries()==(preserve?entries:0));
  for(unsigned retry=0;retry<2;++retry){auto pp=base;must(proxy.Reset(&pp));bind();
   ModelNormalPacketTestAccess::targets(*proxy.modelNormals); // First post-Reset target creation must keep shared sources.
   assert((NorthlightTrackedBuffers::version(vb,false)==revision)==preserve);if(preserve){assert(!weak.expired()&&frame.sharedSourceBytes()==sourceBytes&&w.pointShadowVS==oldVS&&w.pointShadowPS==oldPS);}else assert(!w.pointShadowVS&&!w.pointShadowPS||retry);
   if(preserve&&!retry)assert(w.pointSoftTried&&w.pointSoftLightingPS==oldSoftLight);
   if(preserve&&retry){assert(!w.pointSoftTried&&!w.pointSoftLightingPS);assert(w.pointSoftShaders());}
   source=capture();const bool same=NorthlightSourceIdentity::same(identity,source->sourceIdentity);assert(same==preserve);
   auto current=bounds(source,true,built);assert((current.first==NorthlightReplayBounds::Status::Valid)==(preserve||retry));
   if(!preserve&&!retry){current=bounds(source,false,built);assert(current.first==NorthlightReplayBounds::Status::Valid&&built);}else assert(!built);
   for(unsigned axis=0;axis<3;++axis)assert(current.second.low[axis]==first.second.low[axis]&&current.second.high[axis]==first.second.high[axis]);
   const auto creates=meter.vertexCreates+meter.pixelCreates;assert(image(proxy)==beforeImage);if(preserve)assert(meter.vertexCreates+meter.pixelCreates==creates);source.reset();
   if(preserve&&!retry){drop(w.pointSoftLightingPS);w.pointSoftTried=true;} // A failed optional attempt may retry after Reset.
  }
  // New bytes and a new pose cannot borrow pre-Reset geometry/world bounds.
  vertices[0].xyz[0]-=.2f;must(vb->Lock(0,0,&bytes,0));std::memcpy(bytes,vertices,sizeof vertices);must(vb->Unlock());source=capture();assert(!NorthlightSourceIdentity::same(identity,source->sourceIdentity));
  auto changed=bounds(source,false,built);assert(changed.first==NorthlightReplayBounds::Status::Valid&&changed.second.low[0]<first.second.low[0]);bank[31*4+3]=3;auto moved=bounds(source,true,built);assert(moved.first==NorthlightReplayBounds::Status::Valid&&moved.second.low[0]>changed.second.low[0]+2.9f);
  // Default-pool records still change generation; no default object is held at Reset.
  IDirect3DVertexBuffer9* temporary=nullptr;must(proxy.CreateVertexBuffer(48,D3DUSAGE_DYNAMIC,0,D3DPOOL_DEFAULT,&temporary,nullptr));const auto old=NorthlightTrackedBuffers::version(temporary,false);NorthlightTrackedBuffers::invalidateAll(true);assert(NorthlightTrackedBuffers::version(temporary,false)!=old);temporary->Release();
  std::printf("CAPTURE_RESET_GPU_PASS preserve=%u retries=2 failedReset=checked cpuSources=retained-or-control revision=exact readyBounds=%u sourceWrite=checked currentPose=checked pointShaders=retained-or-control shadowPixels=bitexact poolBytes=%zu defaultInvalidated=1\n",unsigned(preserve),unsigned(preserve),sourceBytes);source.reset();vb->Release();ib->Release();decl->Release();shader->Release();
 }
};
int main(int argc,char** argv){assert(argc==4);std::setvbuf(stdout,nullptr,_IONBF,0);assert(MultiByteToWideChar(CP_UTF8,0,argv[2],-1,rootPath,MAX_PATH));
 std::ifstream file(argv[3],std::ios::binary|std::ios::ate);assert(file);std::vector<uint32_t> code(size_t(file.tellg())/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),code.size()*4);
 auto window=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto* api=create(D3D_SDK_VERSION);assert(api);
 for(bool preserve:{false,true}){D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;IDirect3DDevice9* raw=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));ResetMeter meter(raw);NorthlightHdr::Presentation presentation;presentation.logical=pp;presentation.settings.requested=false;raw->AddRef();auto* proxy=new Device(&meter,api,presentation);PipelineOffTest::run(*proxy,meter,pp,code,preserve);proxy->Release();raw->Release();}
 api->Release();FreeLibrary(module);DestroyWindow(window);
}
