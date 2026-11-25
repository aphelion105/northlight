#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cassert>
int main(){
#ifdef _WIN64
    HMODULE module=LoadLibraryA("d3d9.dll");assert(module);
    wchar_t path[MAX_PATH]={};
    assert(GetModuleFileNameW(module,path,MAX_PATH));
    wchar_t local[MAX_PATH]={};
    assert(GetFullPathNameW(L"d3d9.dll",MAX_PATH,local,nullptr));
    assert(lstrcmpiW(path,local)==0);
    assert(GetProcAddress(module,"Direct3DCreate9")==GetProcAddress(module,MAKEINTRESOURCEA(37)));
    auto createEx=reinterpret_cast<decltype(&Direct3DCreate9Ex)>(GetProcAddress(module,"Direct3DCreate9Ex"));
    assert(createEx&&GetProcAddress(module,MAKEINTRESOURCEA(38))==GetProcAddress(module,"Direct3DCreate9Ex"));
#else
    HMODULE module=LoadLibraryW(L".\\d3d9.dll");assert(module);
#endif
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);
    IDirect3D9* api=create(D3D_SDK_VERSION);assert(api);
#ifdef _WIN64
    api->Release();
    assert(FreeLibrary(module));
    // Match the client: finish adapter enumeration and open D3D9 again.
    assert(GetModuleHandleW(L"d3d9.dll")==module);
    assert(LoadLibraryA("d3d9.dll")==module);
    api=create(D3D_SDK_VERSION);assert(api);
#endif
#if defined(_WIN64) || defined(NORTHLIGHT_ASSET_TEST_DEVICE)
    HWND window=CreateWindowExW(0,L"STATIC",L"Northlight D3D9 test",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    assert(window);
    D3DPRESENT_PARAMETERS pp={};
    pp.Windowed=TRUE;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=64;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.EnableAutoDepthStencil=TRUE;
    pp.AutoDepthStencilFormat=D3DFMT_D24S8;
    IDirect3DDevice9* device=nullptr;
    assert(SUCCEEDED(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device)));
    IDirect3DTexture9* managed=nullptr;
    assert(SUCCEEDED(device->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&managed,nullptr)));
    D3DLOCKED_RECT pixels={};
    assert(SUCCEEDED(managed->LockRect(0,&pixels,nullptr,0)));
    *static_cast<DWORD*>(pixels.pBits)=0xff285080;
    assert(SUCCEEDED(managed->UnlockRect(0)));
    for(unsigned frame=0;frame!=6;++frame){
        assert(SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff285080,1.f,0)));
        assert(SUCCEEDED(device->BeginScene()));
        assert(SUCCEEDED(device->EndScene()));
        assert(SUCCEEDED(device->Present(nullptr,nullptr,nullptr,nullptr)));
        if(frame==2)assert(SUCCEEDED(device->Reset(&pp)));
    }
    assert(SUCCEEDED(managed->LockRect(0,&pixels,nullptr,D3DLOCK_READONLY)));
    assert(*static_cast<const DWORD*>(pixels.pBits)==0xff285080);
    assert(SUCCEEDED(managed->UnlockRect(0)));
    managed->Release();
    device->Release();
    DestroyWindow(window);
#endif
#ifdef _WIN64
    IDirect3D9Ex* apiEx=nullptr;
    assert(SUCCEEDED(createEx(D3D_SDK_VERSION,&apiEx)));
    apiEx->Release();
#endif
    api->Release();
    assert(FreeLibrary(module));
}
