// Manual synthetic DXVK oracle. All resources belong to this hidden test device.
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
static void ok(HRESULT hr){if(FAILED(hr)){std::fprintf(stderr,"HRESULT=%08lx\n",(unsigned long)hr);std::abort();}}
static std::vector<unsigned char> read(const std::string& path){std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
struct Image {std::uint32_t width,height,format,bytes;std::vector<unsigned char> pixels;};
static IDirect3DTexture9* texture(IDirect3DDevice9* dev,const Image& im){IDirect3DTexture9* tex=nullptr;ok(dev->CreateTexture(im.width,im.height,1,0,(D3DFORMAT)im.format,D3DPOOL_MANAGED,&tex,nullptr));D3DLOCKED_RECT lock{};ok(tex->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<im.height;++y)std::memcpy((char*)lock.pBits+y*lock.Pitch,im.pixels.data()+std::size_t(y)*im.width*im.bytes,im.width*im.bytes);ok(tex->UnlockRect(0));return tex;}
struct Vertex{float x,y,z,w,u,v;};
int main(int argc,char** argv){
    assert(argc==3);constexpr unsigned count=8;const float clipNear=.5f,clipFar=1277;
    const float mask[count][2]={{1260,1},{1276,1},{1200,1},{1260,0},{0,0},{600,1},{1277.4f,1},{1279,1}};
    const float scene[count]={1,1,1,1,1,.999f,1,1};
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightWaterFogOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[2]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    Image depth{count,1,D3DFMT_R32F,4,std::vector<unsigned char>(sizeof scene)},water{count,1,D3DFMT_G32R32F,8,std::vector<unsigned char>(sizeof mask)};
    std::memcpy(depth.pixels.data(),scene,sizeof scene);std::memcpy(water.pixels.data(),mask,sizeof mask);
    IDirect3DTexture9* textures[]={texture(d,depth),texture(d,water)};
    for(unsigned i=0;i<2;++i){const unsigned slot=i?11:1;ok(d->SetTexture(slot,textures[i]));for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,state,D3DTEXF_POINT));ok(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    auto code=read(argv[1]);IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shader));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(count,1,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(count,1,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetPixelShader(shader));
    float c[31][4]{};c[0][2]=clipNear;c[0][3]=clipFar;c[30][0]=1;ok(d->SetPixelShaderConstantF(0,c[0],31));D3DVIEWPORT9 viewport={0,0,count,1,0,1};ok(d->SetViewport(&viewport));
    Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(count)-.5f,-.5f,0,1,1,0},{-.5f,.5f,0,1,0,1},{float(count)-.5f,.5f,0,1,1,1}};
    ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    unsigned repaired=0,unchanged=0;
    for(unsigned i=0;i<count;++i){float value[4];std::memcpy(value,static_cast<unsigned char*>(lock.pBits)+16*i,16);
        const auto distance=[&](float depth){return clipNear*clipFar/(clipFar-depth*(clipFar-clipNear));};
        const bool wet=mask[i][0]>clipNear&&mask[i][0]<=distance(scene[i])+std::max(.012f,mask[i][0]*.0007f)&&mask[i][1]>.003f;
        for(float component:value)assert(std::isfinite(component));
        if(wet)assert(value[0]==mask[i][0]);else assert(value[0]==value[3]);
        if(i<2||i==6){assert(std::fabs(value[0]-value[2])>10);++repaired;}
        else{assert(value[0]==value[2]);++unchanged;}
    }
    ok(staging->UnlockRect());assert(repaired==3&&unchanged==5);
    std::printf("PASS DXVK3 water fog endpoint GPU oracle: %u corrected far-water pixels, %u exact dry/near/occluded cases, exact mask endpoints, unchanged dry fallback\n",repaired,unchanged);
    d->SetPixelShader(nullptr);d->SetTexture(1,nullptr);d->SetTexture(11,nullptr);shader->Release();for(auto* t:textures)t->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
