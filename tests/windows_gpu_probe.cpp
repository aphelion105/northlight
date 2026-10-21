// Check the system D3D9 runtime before tests that need a hidden HAL device.
// Feature backends are exercised by the tests themselves.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <string>

int main(){
    wchar_t directory[MAX_PATH];const UINT length=GetSystemDirectoryW(directory,MAX_PATH);
    if(!length||length>=MAX_PATH){std::puts("system D3D9 directory unavailable");return 1;}
    const std::wstring path=std::wstring(directory,length)+L"\\d3d9.dll";
    HMODULE module=LoadLibraryW(path.c_str());
    if(!module){std::puts("system D3D9 runtime unavailable");return 1;}
    using Create=IDirect3D9*(WINAPI*)(UINT);
    auto create=reinterpret_cast<Create>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));
    IDirect3D9* api=create?create(D3D_SDK_VERSION):nullptr;
    if(!api){FreeLibrary(module);std::puts("Direct3DCreate9 unavailable");return 1;}
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* device=nullptr;
    const HRESULT hr=window?api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device):E_FAIL;
    if(device)device->Release();if(window)DestroyWindow(window);api->Release();FreeLibrary(module);
    std::printf("system D3D9 hidden device HRESULT=0x%08lx\n",static_cast<unsigned long>(hr));
    return FAILED(hr)?1:0;
}
