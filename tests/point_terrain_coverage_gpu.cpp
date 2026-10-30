// Production point-shadow camera coverage regression; no game/cache or timings.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include <cassert>
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
struct PointTerrainCoverageTest {
 static void run(IDirect3DDevice9* d){
  // One cached terrain blocker. The camera may see a different native portion
  // of the same chunk: that is not proof that this blocker was captured live.
  WorldRenderer w(d);w.valid=true;w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.active->map="authored-point-coverage";w.uploadedMap=w.active->map;w.uploaded=w.active->bvh;w.meshGeneration=1;
  const NorthlightGI::WorldVertex vertices[]={{{-1,-1,0},{0,0,1},0,0},{{1,-1,0},{0,0,1},1,0},{{-1,1,0},{0,0,1},0,1},{{1,1,0},{0,0,1},1,1}};const WORD indices[]={0,1,2,2,1,3};
  assert(w.meshStore.beginStaging({NorthlightMeshPageKey::keyOf(vertices,sizeof vertices,indices,sizeof indices)}));w.meshStore.commit();w.meshBuffers.resize(w.meshStore.entries().size());auto& page=w.meshBuffers[w.meshStore.bufferOf(0)];
  must(d->CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,&page.vb,nullptr));must(d->CreateIndexBuffer(sizeof indices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&page.ib,nullptr));void* bytes=nullptr;
  must(page.vb->Lock(0,sizeof vertices,&bytes,0));std::memcpy(bytes,vertices,sizeof vertices);must(page.vb->Unlock());must(page.ib->Lock(0,sizeof indices,&bytes,0));std::memcpy(bytes,indices,sizeof indices);must(page.ib->Unlock());w.vertices=page.vb;w.indices=page.ib;
  D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};must(d->CreateVertexDeclaration(elements,&w.shadowDecl));must(d->CreatePixelShader(kShadowUnionShader,&w.unionPS));
  w.materials.push_back(nullptr);w.uploadedAlphaCutoffs.push_back(-1);WorldRenderer::Batch batch{};batch.page=0;batch.start=0;batch.count=2;batch.minVertex=0;batch.vertexCount=4;batch.material=0;batch.terrain=true;batch.chunkX=7;batch.chunkY=9;batch.boundsLow={-1,-1,0};batch.boundsHigh={1,1,0};w.batches.push_back(batch);
  w.pointLiveBatches.push_back({0,1,{30,30,0},{31,31,1}});w.quality.pointShadowFacesPerFrame=6;w.quality.pointShadowRefreshMs=0;
  const NorthlightLocalLights::Light light{{0,0,4},{1,.4f,.1f},1,12,12345,3,NorthlightLocalLights::CastShadow};
  IDirect3DSurface9* readback=nullptr;must(d->CreateOffscreenPlainSurface(256,256,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));std::vector<float> reference;
  for(unsigned frame=0;frame<4;++frame){w.liveTerrainChunks.clear();if(frame&1)w.liveTerrainChunks.emplace(7,9);
   must(d->BeginScene());const bool rendered=w.renderPointShadow(light,true,false);must(d->EndScene());assert(rendered&&w.pointSlot().ready);IDirect3DSurface9* face=nullptr;must(w.pointSlot().cube->GetCubeMapSurface(D3DCUBEMAP_FACE_NEGATIVE_Z,0,&face));must(d->GetRenderTargetData(face,readback));face->Release();D3DLOCKED_RECT lock{};must(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> image(256*256);for(unsigned y=0;y<256;++y)std::memcpy(image.data()+256*y,static_cast<char*>(lock.pBits)+y*lock.Pitch,256*4);must(readback->UnlockRect());
   std::printf("POINT_TERRAIN_FRAME nativeChunkVisible=%u centerDepth=%.9f draws=%u\n",frame&1,image[128*256+128],w.pointDraws);std::fflush(stdout);
   assert(image[128*256+128]<1.f);if(reference.empty())reference=image;else assert(image==reference);
  }
  readback->Release();std::printf("POINT_TERRAIN_COVERAGE_GPU_PASS cameraMembershipStates=4 cachedBlockerRetained=1 exactCube=1 sameSource=1 timings=0\n");
 }
 };
int main(int argc,char** argv){assert(argc==3);SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[2]);auto path=std::wstring(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightPointCoverage";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&d));PointTerrainCoverageTest::run(d);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);}
