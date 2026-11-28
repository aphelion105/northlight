// The rain mask's render target 1 (rainMrtBegin) is unbound before the game changes
// its targets. While it is bound, a game SetRenderTarget or SetDepthStencilSurface must
// not leave the mask attached to the new targets, and a render target 1 the game binds
// itself must survive the next draw's unbind.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include <cassert>
static void must(HRESULT hr){if(FAILED(hr))std::printf("HRESULT=%08lx\n",(unsigned long)hr);assert(SUCCEEDED(hr));}
struct PlainDevice:ForwardIDirect3DDevice9 {using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;};
struct PipelineOffTest {
 static IDirect3DSurface9* boundTarget(Device& d,DWORD index){
  IDirect3DSurface9* s=nullptr;const HRESULT hr=d.ext->GetRenderTarget(index,&s);if(FAILED(hr))return nullptr;if(s)s->Release();return s; // identity only
 }
 // The state rainMrtBegin leaves: the mask on render target 1, rainMrtBound set.
 static void bindRainMask(Device& d,IDirect3DSurface9* mask){must(d.ext->SetRenderTarget(1,mask));d.rainMrtBound=true;assert(boundTarget(d,1)==mask);}
 static unsigned run(Device& d){
  auto* ext=d.ext;unsigned failures=0;
  auto check=[&](const char* name,bool ok){std::printf("%s %s\n",ok?"PASS":"FAIL",name);failures+=!ok;};
  IDirect3DSurface9* mask=nullptr;must(ext->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&mask,nullptr));
  IDirect3DSurface9 *gameTarget=nullptr,*gameExtra=nullptr,*gameDepth=nullptr,*back=nullptr,*depth=nullptr; // the game's (wrapped) objects
  must(d.CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&gameTarget,nullptr));
  must(d.CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&gameExtra,nullptr));
  must(d.CreateDepthStencilSurface(64,64,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&gameDepth,nullptr));
  must(d.GetRenderTarget(0,&back));must(d.GetDepthStencilSurface(&depth));
  // 1. The game switches render target 0 between two rain draws.
  bindRainMask(d,mask);must(d.SetRenderTarget(0,gameTarget));
  check("SetRenderTarget(0): the rain mask is no longer bound as render target 1",boundTarget(d,1)==nullptr);
  check("SetRenderTarget(0): rainMrtBound cleared",!d.rainMrtBound);
  d.rainMrtUnbind();must(d.SetRenderTarget(0,back));
  // 2. The game switches the depth stencil between two rain draws.
  bindRainMask(d,mask);must(d.SetDepthStencilSurface(gameDepth));
  check("SetDepthStencilSurface: the rain mask is no longer bound as render target 1",boundTarget(d,1)==nullptr);
  check("SetDepthStencilSurface: rainMrtBound cleared",!d.rainMrtBound);
  d.rainMrtUnbind();must(d.SetDepthStencilSurface(depth));
  // 3. The game binds its own render target 1; the next draw's unbind (drawHook: rainMrtBound && !rainBlend) must keep it.
  bindRainMask(d,mask);must(d.SetRenderTarget(1,gameExtra));d.rainMrtUnbind();
  check("SetRenderTarget(1): the game's render target 1 survives the next unbind",boundTarget(d,1)==d.mirrorResources.unwrap(gameExtra));
  must(d.SetRenderTarget(1,nullptr));
  // 4. Without the rain mask bound nothing changes: the game's own render target 1 stays across a render target 0 change.
  must(d.SetRenderTarget(1,gameExtra));must(d.SetRenderTarget(0,gameTarget));
  check("no mask bound: SetRenderTarget(0) keeps the game's render target 1",boundTarget(d,1)==d.mirrorResources.unwrap(gameExtra));
  must(d.SetRenderTarget(0,back));must(d.SetRenderTarget(1,nullptr));
  for(auto* s:{gameTarget,gameExtra,gameDepth,back,depth})s->Release();mask->Release();
  return failures;
 }
};
int main(int argc,char** argv){assert(argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);assert(MultiByteToWideChar(CP_UTF8,0,argv[2],-1,rootPath,MAX_PATH));
 auto window=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto* api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
 auto created=pp;IDirect3DDevice9* raw=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&created,&raw));
 D3DCAPS9 caps{};must(raw->GetDeviceCaps(&caps));
 if(caps.NumSimultaneousRTs<2){std::printf("RAIN_MRT_TARGET_CHANGE_SKIP backend=%s simultaneousRTs=%lu\n",argv[1],(unsigned long)caps.NumSimultaneousRTs);raw->Release();api->Release();return 0;}
 PlainDevice plain(raw);NorthlightHdr::Presentation presentation;presentation.logical=pp;presentation.settings.requested=false;raw->AddRef();
 auto* proxy=new Device(&plain,api,presentation);const unsigned failures=PipelineOffTest::run(*proxy);proxy->Release();raw->Release();
 api->Release();FreeLibrary(module);DestroyWindow(window);
 if(failures){std::printf("RAIN_MRT_TARGET_CHANGE_FAIL backend=%s failures=%u\n",argv[1],failures);return 1;}
 std::printf("RAIN_MRT_TARGET_CHANGE_PASS backend=%s\n",argv[1]);return 0;
}
