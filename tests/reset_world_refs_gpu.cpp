// Device::Reset releases the frame's held references to the game's targets.
// beforeDraw keeps two references while a world frame is open: worldDepth (the bound
// depth stencil, bindWorldDepth) and terrainAdmissionTarget (render target 0, normally
// the back buffer). clearFrame drops both at Present; a Reset between a world draw and
// that Present must drop them too, or the implicit surfaces stay referenced.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include <cassert>
static void must(HRESULT hr){if(FAILED(hr))std::printf("HRESULT=%08lx\n",(unsigned long)hr);assert(SUCCEEDED(hr));}
static ULONG references(IUnknown* p){p->AddRef();return p->Release();}
struct PlainDevice:ForwardIDirect3DDevice9 {using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;};
struct PipelineOffTest {
 // The state beforeDraw leaves after admitting a terrain draw: the bound depth held
 // through bindWorldDepth and render target 0 held as the admission target.
 static void admitWorldFrame(Device& d){
  IDirect3DSurface9* back=nullptr;must(d.ext->GetRenderTarget(0,&back));D3DSURFACE_DESC desc{};must(back->GetDesc(&desc));
  assert(d.bindWorldDepth(desc)&&d.worldDepth&&d.worldDepthDescKnown);
  drop(d.terrainAdmissionTarget.p);d.terrainAdmissionTarget.p=back; // the reference GetRenderTarget returned
  d.terrainAdmission.target=back;d.terrainAdmission.depth=d.worldDepth;
 }
 static void run(Device& d,D3DPRESENT_PARAMETERS base,const char* backend){
  for(unsigned round=0;round<2;++round){
   admitWorldFrame(d);
   IDirect3DSurface9* depth=nullptr;IDirect3DSurface9* target=nullptr;must(d.ext->GetDepthStencilSurface(&depth));must(d.ext->GetRenderTarget(0,&target));
   assert(depth==d.worldDepth&&target==d.terrainAdmissionTarget.p);
   const ULONG depthHeld=references(depth)-1,targetHeld=references(target)-1; // minus this test's own reference
   depth->Release();target->Release();
   auto pp=base;const HRESULT hr=d.Reset(&pp);
   std::printf("RESET_WORLD_REFS backend=%s round=%u heldDepthRefs=%lu heldTargetRefs=%lu reset=0x%08lx worldDepthAfter=%p admissionTargetAfter=%p\n",backend,round,
    (unsigned long)depthHeld,(unsigned long)targetHeld,(unsigned long)hr,static_cast<void*>(d.worldDepth),static_cast<void*>(d.terrainAdmissionTarget.p));
   assert(!d.worldDepth&&!d.worldDepthDescKnown);
   assert(!d.terrainAdmissionTarget.p&&!d.terrainAdmission.target);
   must(hr);
  }
  std::printf("RESET_WORLD_REFS_PASS backend=%s rounds=2\n",backend);
 }
};
int main(int argc,char** argv){assert(argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);assert(MultiByteToWideChar(CP_UTF8,0,argv[2],-1,rootPath,MAX_PATH));
 auto window=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto* api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
 auto created=pp;IDirect3DDevice9* raw=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&created,&raw));
 PlainDevice plain(raw);NorthlightHdr::Presentation presentation;presentation.logical=pp;presentation.settings.requested=false;raw->AddRef();
 auto* proxy=new Device(&plain,api,presentation);PipelineOffTest::run(*proxy,pp,argv[1]);proxy->Release();raw->Release();
 api->Release();FreeLibrary(module);DestroyWindow(window);
}
