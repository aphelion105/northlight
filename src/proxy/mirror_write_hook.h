#pragma once
// Task 34: what the game-visible resource proxies (mirror_resource_forwarders.h, generated) report to the spell layer. Everything is inert until the
// Device installs a sink (SpellLayer=1): a texture writer pays one relaxed load of `pending`, which is nonzero only while the layer holds a deferred
// draw; Query::Issue pays one relaxed load of `queries`. The sink runs on the thread that executes the device (replay thread with the stream on).
// Keys are the RAW objects (the forwarders' `real`), the same pointers the layer got from GetTexture on the extension device.
#include <d3d9.h>
#include <atomic>
namespace NorthlightMirrorWrite {
struct Sink {
    void (*texture)(void* context,void* rawTexture)=nullptr;
    void (*query)(void* context,void* rawQuery,DWORD issueFlags)=nullptr;
    void* context=nullptr;
};
inline Sink sink;
constexpr DWORD kLockReadOnly=0x10; /* D3DLOCK_READONLY */
#ifdef D3DLOCK_READONLY
static_assert(DWORD(D3DLOCK_READONLY)==kLockReadOnly,"lock flag value");
#endif
inline bool writes(DWORD lockFlags){return !(lockFlags&kLockReadOnly);}
inline std::atomic<unsigned> pending{0},queries{0};
inline void texture(IDirect3DBaseTexture9* raw){if(pending.load(std::memory_order_relaxed)&&sink.texture&&raw)sink.texture(sink.context,static_cast<void*>(raw));}
// A surface or volume level: the write lands in its container texture (none for a standalone surface).
template<class Level> inline void level(Level* raw){
    if(!pending.load(std::memory_order_relaxed)||!sink.texture||!raw)return;
    IDirect3DBaseTexture9* container=nullptr;
    if(SUCCEEDED(raw->GetContainer(__uuidof(IDirect3DBaseTexture9),reinterpret_cast<void**>(&container)))&&container){sink.texture(sink.context,static_cast<void*>(container));container->Release();}
}
inline void query(IDirect3DQuery9* raw,DWORD flags){if(queries.load(std::memory_order_relaxed)&&sink.query)sink.query(sink.context,static_cast<void*>(raw),flags);}
}
