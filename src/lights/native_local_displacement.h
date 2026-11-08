#pragma once
#include <d3d9.h>
// Borrowed only while the displacement factory submits its prepared geometry.
struct NorthlightNativeDisplacement {
    IDirect3DVertexShader9 *originalVS=nullptr,*vs=nullptr;
    unsigned control=0,count=0,sampler=0;
    bool gpu=false;
    bool matches(IDirect3DVertexShader9* bound)const{return originalVS&&vs==bound&&count==(gpu?2u:1u);}
};
