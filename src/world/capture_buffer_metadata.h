#pragma once
#include <d3d9.h>
#include <cstdint>
#include <memory>

namespace NorthlightCaptureMetadata {
struct Request {
    void* buffer=nullptr;bool index=false;
    // Zero bytes requests the whole resource generation. A range token covers
    // every intersected page, including padding between declared vertices.
    uint64_t begin=0,bytes=0;
    // An index request can supply element coordinates before GetDesc. The
    // metadata owner resolves its immutable INDEX16/INDEX32 width in one batch.
    bool indexElements=false;
};
struct Info {
    std::uint64_t identity=0,revision=0;
    UINT size=0;DWORD usage=0;D3DFORMAT format={};bool known=false;
};
using Read=void(*)(const Request*,Info*,unsigned);
// Pins a native buffer and protects it with a pre-write barrier until release.
using ReadLease=std::unique_ptr<void,void(*)(void*)>;
using GuardRead=ReadLease(*)(void*,void*,bool,std::uint64_t);
// Stack-scoped proof shared by consumers of one unchanged draw invocation.
struct PositionBounds {
    const void* owner=nullptr;const void* shader=nullptr;
    float low[3]={},high[3]={};bool known=false,stream0Float3=false;
};
}
