#pragma once
#include <d3d9.h>
#include <cstdint>
#include <vector>

// Borrowed only inside the material factory's native draw scope. The bound
// pair must still match: displacement or another shader replacement invalidates it.
struct NorthlightNativePOM {
    IDirect3DVertexShader9 *originalVS=nullptr,*vs=nullptr;
    IDirect3DPixelShader9 *originalPS=nullptr,*ps=nullptr;
    unsigned sourceControl=0,count=0;
    bool dual=false,shared=false;
    bool (*sharedVertex)(const DWORD*,size_t,std::vector<uint32_t>&)=nullptr;
    bool matches(IDirect3DVertexShader9* v,IDirect3DPixelShader9* p)const{
        return originalVS&&originalPS&&vs==v&&ps==p&&count==(shared?3u:2u)&&(!shared||sharedVertex);
    }
};
