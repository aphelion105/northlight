#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cassert>
int main(){
    HMODULE module=LoadLibraryW(L".\\d3d9.dll");assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);
    IDirect3D9* api=create(D3D_SDK_VERSION);assert(api);api->Release();
    assert(FreeLibrary(module));
}
