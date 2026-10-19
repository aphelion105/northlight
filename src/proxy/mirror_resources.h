#pragma once
#include "mirror_resource_forwarders.h"
#include "mirror_guard.h"
#include "render_thread_probe.h"
#include "frame_stall_profile.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <unordered_map>
#include <type_traits>

// Game-facing resources only. Extension code continues to use raw resources.
// No registry reference owns a proxy: its COM references alone own its lifetime.
namespace NorthlightMirrorResources {
static constexpr bool kProxyShapeUnwrap=true;
// Cross-device arguments still need to reach the backend as raw resources.
// Lock order: a device gate, then this mutex; never acquire another device gate.
// Bumped whenever a texture record (2D, cube or volume base texture) is created or destroyed, and on device Reset: a raw texture address seen before may now name
// another object. Surface, volume and other records do not bump it (GetRenderTarget/GetSurfaceLevel churn would invalidate the cache every draw). The spell layer
// keys its per-texture render-target verdicts on it.
inline std::atomic<std::uint32_t> generation{0};
inline std::mutex allProxiesMutex;
inline std::unordered_map<void*,IUnknown*> allProxies;
class Registry;
struct Record {
    Registry& registry;
    IUnknown* raw;
    IUnknown* exposed=nullptr;
    ULONG refs=1;
    bool texture=false; // a base texture: its creation and destruction bump `generation`
    Record(Registry& r,IUnknown* p):registry(r),raw(p){}
    virtual ~Record()=default;
};
template<class T> struct Factory;
class Registry {
    IDirect3DDevice9* owner_;
    IDirect3DDevice9* rawDevice_;
    MirrorGate& gate_;
    void(*disable_)(void*,const char*);
    void* context_;
    std::unordered_map<void*,Record*> raw_,exposed_;
    // O(1) unwrap. Each registered proxy class contributes its primary vtable
    // and the byte offset from its exposed interface to its Record. A live COM
    // argument (the caller owns a reference) whose first word is one of OUR
    // vtables is that proxy class: backend objects' vtables live in another
    // module. It must still name this registry and itself as exposed, else
    // the map path decides. Gate-protected like the maps.
    struct Shape {const void* vtable;std::ptrdiff_t record;};
    static constexpr unsigned MaxShapes=16;
    Shape shapes_[MaxShapes]={};unsigned shapeCount_=0;bool shapesUnsafe_=false;
    static const void* vtableOf(const void* object){return *static_cast<const void* const*>(object);}
    void learnShape(Record* record){
        const void* vtable=vtableOf(record->exposed);
        const std::ptrdiff_t offset=reinterpret_cast<char*>(record)-reinterpret_cast<char*>(record->exposed);
        for(unsigned i=0;i<shapeCount_;++i)if(shapes_[i].vtable==vtable){if(shapes_[i].record!=offset)shapesUnsafe_=true;return;}
        if(shapeCount_<MaxShapes)shapes_[shapeCount_++]={vtable,offset};
    }
    Record* shaped(void* object)const{
        if(!kProxyShapeUnwrap||shapesUnsafe_)return nullptr;
        const void* vtable=vtableOf(object);
        for(unsigned i=0;i<shapeCount_;++i)if(shapes_[i].vtable==vtable){
            auto* record=reinterpret_cast<Record*>(static_cast<char*>(object)+shapes_[i].record);
            return &record->registry==this&&record->exposed==object?record:nullptr;
        }
        return nullptr;
    }
public:
    NorthlightFrameStalls::Queries queryProfile;
    Registry(IDirect3DDevice9* owner,MirrorGate& gate,void(*disable)(void*,const char*),void* context,IDirect3DDevice9* rawDevice=nullptr)
        :owner_(owner),rawDevice_(rawDevice),gate_(gate),disable_(disable),context_(context){}
    Registry(const Registry&)=delete;
    Registry& operator=(const Registry&)=delete;
    MirrorGate& gate()const{return gate_;}
    IDirect3DDevice9* owner()const{return owner_;}
    void disable(const char* why)noexcept{disable_(context_,why);}
    static void unsafe(void* context,const char* why){static_cast<Registry*>(context)->disable(why);}
    size_t size()const{MirrorGuard lock(gate_,MirrorSite::Registry);return raw_.size();}
    unsigned shapes()const{return shapeCount_;}
    template<class T> T* unwrap(T* object)noexcept{
        if(!object)return nullptr;
        MirrorGuard lock(gate_,MirrorSite::Registry);
        if(Record* record=shaped(static_cast<IUnknown*>(object)))return static_cast<T*>(record->raw);
        auto found=exposed_.find(object);
        if(found==exposed_.end()){
            disable("untracked or foreign resource input");
            std::lock_guard<std::mutex> global(allProxiesMutex);
            auto other=allProxies.find(object);
            // Caller owns the argument COM reference throughout this call.
            return other==allProxies.end()?object:static_cast<T*>(other->second);
        }
        return static_cast<T*>(found->second->raw);
    }
    // the raw object behind an exposed pointer the GAME stored in its
    // own memory (an opaque number: map lookup only, never dereferenced, no
    // disable on a miss). A miss is 0 while the mirror is active: a stale
    // exposed value could equal a live raw texture's address. Only after an
    // escape (the game may then hold raw objects) is a miss returned unchanged.
    std::uintptr_t rawOf(std::uintptr_t exposed,bool passThroughMiss=false)const noexcept{
        if(!exposed)return 0;
        MirrorGuard lock(gate_,MirrorSite::Registry);
        auto found=exposed_.find(reinterpret_cast<void*>(exposed));
        if(found==exposed_.end())return passThroughMiss?exposed:0;
        return reinterpret_cast<std::uintptr_t>(found->second->raw);
    }
    template<class T> void wrap(T** ownedResult)noexcept;
    void wrap(IDirect3DBaseTexture9** ownedResult)noexcept;
    void wrap(IDirect3DResource9** ownedResult)noexcept;
    void wrap(IUnknown** ownedResult)noexcept;
    template<class T> void expose(T** ownedResult)noexcept{wrap(ownedResult);}
    void wrapInterface(REFIID id,void** ownedResult)noexcept;
    bool isOwnDevice(IDirect3DDevice9* device)noexcept{
        if(!rawDevice_||!device)return false;
        IUnknown* expected=nullptr;IUnknown* actual=nullptr;
        HRESULT a=rawDevice_->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&expected));
        HRESULT b=device->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&actual));
        const bool same=SUCCEEDED(a)&&SUCCEEDED(b)&&expected&&expected==actual;
        if(expected)expected->Release();if(actual)actual->Release();return same;
    }
    ULONG addRef(Record& record){MirrorGuard lock(gate_,MirrorSite::Registry);return ++record.refs;}
    ULONG release(Record& record){
        IDirect3DDevice9* owner=nullptr;
        ULONG remaining;
        {
            MirrorGuard lock(gate_,MirrorSite::Registry);
            remaining=--record.refs;
            if(!remaining){
                raw_.erase(record.raw);exposed_.erase(record.exposed);if(record.texture)++generation;
                {std::lock_guard<std::mutex> global(allProxiesMutex);allProxies.erase(record.exposed);}
                owner=owner_;
                IUnknown* raw=record.raw;
                delete &record;
                raw->Release();
            }
        }
        // This can destroy Device, this registry and its gate. No further access
        // to any of those objects (including the guard's destructor) follows.
        if(owner)owner->Release();
        return remaining;
    }
};

template<class T> bool supported(REFIID id){
    if(id==__uuidof(IUnknown)||id==__uuidof(T))return true;
    if constexpr(std::is_base_of<IDirect3DResource9,T>::value)
        if(id==__uuidof(IDirect3DResource9))return true;
    if constexpr(std::is_base_of<IDirect3DBaseTexture9,T>::value)
        if(id==__uuidof(IDirect3DBaseTexture9))return true;
    return false;
}
template<class T,class Forward> class Proxy:public Forward,public Record {
public:
    Proxy(T* p,Registry& r):Forward(p,r.gate(),&Registry::unsafe,&r),Record(r,p){this->exposed=static_cast<T*>(this);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        MirrorGuard lock(this->registry.gate(),MirrorSite::Resource);
        if(!out)return E_POINTER;
        if(supported<T>(id)){*out=static_cast<T*>(this);++this->refs;return S_OK;}
        // Preserve optional/driver interfaces, but never return one while
        // claiming that every device write is still intercepted.
        HRESULT hr=this->real->QueryInterface(id,out);
        if(SUCCEEDED(hr)&&*out)this->registry.disable("resource QueryInterface returned an untracked interface");
        return hr;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return this->registry.addRef(*this);}
    ULONG STDMETHODCALLTYPE Release()override{return this->registry.release(*this);}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out)override{
        MirrorGuard lock(this->registry.gate(),MirrorSite::Resource);
        if(!out)return D3DERR_INVALIDCALL;
        *out=this->registry.owner();(*out)->AddRef();return D3D_OK;
    }
};
template<class T,class Forward> class ChildProxy:public Proxy<T,Forward> {
public:
    using Proxy<T,Forward>::Proxy;
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID id,void** out)override{
        MirrorGuard lock(this->registry.gate(),MirrorSite::Resource);
        HRESULT hr=this->real->GetContainer(id,out);
        if(SUCCEEDED(hr))this->registry.wrapInterface(id,out);
        return hr;
    }
};
class TextureProxy final:public Proxy<IDirect3DTexture9,ForwardIDirect3DTexture9> {
public:
    using Proxy::Proxy;
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level,IDirect3DSurface9** out)override{
        MirrorGuard lock(this->registry.gate(),MirrorSite::Resource);
        HRESULT hr=this->real->GetSurfaceLevel(level,out);if(SUCCEEDED(hr))this->registry.wrap(out);return hr;
    }
};
class CubeProxy final:public Proxy<IDirect3DCubeTexture9,ForwardIDirect3DCubeTexture9> {
public:
    using Proxy::Proxy;
    HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES face,UINT level,IDirect3DSurface9** out)override{
        MirrorGuard lock(this->registry.gate(),MirrorSite::Resource);
        HRESULT hr=this->real->GetCubeMapSurface(face,level,out);if(SUCCEEDED(hr))this->registry.wrap(out);return hr;
    }
};
class VolumeTextureProxy final:public Proxy<IDirect3DVolumeTexture9,ForwardIDirect3DVolumeTexture9> {
public:
    using Proxy::Proxy;
    HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT level,IDirect3DVolume9** out)override{
        MirrorGuard lock(this->registry.gate(),MirrorSite::Resource);
        HRESULT hr=this->real->GetVolumeLevel(level,out);if(SUCCEEDED(hr))this->registry.wrap(out);return hr;
    }
};
#define NORTHLIGHT_RESOURCE_FACTORY(T,P) template<> struct Factory<T>{static Record* make(T* raw,Registry& registry){return new P(raw,registry);}};
using SurfaceProxy=ChildProxy<IDirect3DSurface9,ForwardIDirect3DSurface9>;
using VolumeProxy=ChildProxy<IDirect3DVolume9,ForwardIDirect3DVolume9>;
using VertexShaderProxy=Proxy<IDirect3DVertexShader9,ForwardIDirect3DVertexShader9>;
using PixelShaderProxy=Proxy<IDirect3DPixelShader9,ForwardIDirect3DPixelShader9>;
using DeclarationProxy=Proxy<IDirect3DVertexDeclaration9,ForwardIDirect3DVertexDeclaration9>;
class QueryProxy final:public Proxy<IDirect3DQuery9,ForwardIDirect3DQuery9> {
    NorthlightFrameStalls::QuerySpan wait;unsigned queryType=~0u;
    static int64_t now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
public:
    using Proxy::Proxy;
    HRESULT STDMETHODCALLTYPE Issue(DWORD flags)override{MirrorGuard lock(this->registry.gate(),MirrorSite::Resource);wait.reset();NorthlightMirrorWrite::query(this->real,flags);return this->real->Issue(flags);}
    HRESULT STDMETHODCALLTYPE GetData(void* data,DWORD size,DWORD flags)override{
        MirrorGuard lock(this->registry.gate(),MirrorSite::Resource);
        if(!NorthlightRenderThreadProbe::profiling()){wait.reset();return this->real->GetData(data,size,flags);}
        if(queryType==~0u)queryType=unsigned(this->real->GetType());
        const int64_t start=now();const HRESULT hr=this->real->GetData(data,size,flags);const int64_t end=now();
        wait.record(this->registry.queryProfile,queryType,flags,hr,start,end);return hr;
    }
};
NORTHLIGHT_RESOURCE_FACTORY(IDirect3DSurface9,SurfaceProxy)
NORTHLIGHT_RESOURCE_FACTORY(IDirect3DTexture9,TextureProxy)
NORTHLIGHT_RESOURCE_FACTORY(IDirect3DCubeTexture9,CubeProxy)
NORTHLIGHT_RESOURCE_FACTORY(IDirect3DVolumeTexture9,VolumeTextureProxy)
NORTHLIGHT_RESOURCE_FACTORY(IDirect3DVolume9,VolumeProxy)
NORTHLIGHT_RESOURCE_FACTORY(IDirect3DVertexShader9,VertexShaderProxy)
NORTHLIGHT_RESOURCE_FACTORY(IDirect3DPixelShader9,PixelShaderProxy)
NORTHLIGHT_RESOURCE_FACTORY(IDirect3DVertexDeclaration9,DeclarationProxy)
NORTHLIGHT_RESOURCE_FACTORY(IDirect3DQuery9,QueryProxy)
#undef NORTHLIGHT_RESOURCE_FACTORY

template<class T> void Registry::wrap(T** out)noexcept{
    if(!out||!*out)return;
    MirrorGuard lock(gate_,MirrorSite::Registry);
    T* raw=*out;
    // Already exposed results retain their existing owned reference.
    if(exposed_.find(raw)!=exposed_.end())return;
    auto found=raw_.find(raw);
    if(found!=raw_.end()){
        auto& record=*found->second;++record.refs;
        *out=static_cast<T*>(record.exposed);raw->Release();return;
    }
    Record* record=nullptr;
    try{
        record=Factory<T>::make(raw,*this);
        record->texture=std::is_base_of<IDirect3DBaseTexture9,T>::value;raw_.emplace(record->raw,record);if(record->texture)++generation;
        exposed_.emplace(record->exposed,record);
        {std::lock_guard<std::mutex> global(allProxiesMutex);allProxies.emplace(record->exposed,record->raw);}
        learnShape(record);
        owner_->AddRef();
        *out=static_cast<T*>(record->exposed);
    }catch(...){
        if(record){raw_.erase(record->raw);exposed_.erase(record->exposed);
            {std::lock_guard<std::mutex> global(allProxiesMutex);allProxies.erase(record->exposed);}}
        delete record; // Original owned raw reference was never consumed.
        disable("resource proxy allocation failed");
    }
}
inline void Registry::wrap(IDirect3DBaseTexture9** out)noexcept{
    if(!out||!*out)return;
    MirrorGuard lock(gate_,MirrorSite::Registry);
    switch((*out)->GetType()){
    case D3DRTYPE_TEXTURE:{auto* p=static_cast<IDirect3DTexture9*>(*out);wrap(&p);*out=p;break;}
    case D3DRTYPE_CUBETEXTURE:{auto* p=static_cast<IDirect3DCubeTexture9*>(*out);wrap(&p);*out=p;break;}
    case D3DRTYPE_VOLUMETEXTURE:{auto* p=static_cast<IDirect3DVolumeTexture9*>(*out);wrap(&p);*out=p;break;}
    default:disable("unknown base texture type");break;
    }
}
inline void Registry::wrap(IDirect3DResource9** out)noexcept{
    if(!out||!*out)return;
    MirrorGuard lock(gate_,MirrorSite::Registry);
    switch((*out)->GetType()){
    case D3DRTYPE_SURFACE:{auto* p=static_cast<IDirect3DSurface9*>(*out);wrap(&p);*out=p;break;}
    case D3DRTYPE_TEXTURE:case D3DRTYPE_CUBETEXTURE:case D3DRTYPE_VOLUMETEXTURE:{auto* p=static_cast<IDirect3DBaseTexture9*>(*out);wrap(&p);*out=p;break;}
    default:disable("untracked resource base interface");break;
    }
}
inline void Registry::wrap(IUnknown** out)noexcept{
    if(!out||!*out)return;
    MirrorGuard lock(gate_,MirrorSite::Registry);
    if(exposed_.find(*out)!=exposed_.end())return;
    auto existing=raw_.find(*out);
    if(existing!=raw_.end()){auto* raw=*out;++existing->second->refs;*out=existing->second->exposed;raw->Release();return;}
    // Containers can ask for IUnknown, so establish their supported concrete
    // interface before choosing a proxy. QI's extra reference is exchanged.
#define NORTHLIGHT_TRY_CONTAINER(T) {T* p=nullptr;if(SUCCEEDED((*out)->QueryInterface(__uuidof(T),reinterpret_cast<void**>(&p)))&&p){IUnknown* raw=*out;wrap(&p);*out=p;raw->Release();return;}}
    NORTHLIGHT_TRY_CONTAINER(IDirect3DSurface9)
    NORTHLIGHT_TRY_CONTAINER(IDirect3DTexture9)
    NORTHLIGHT_TRY_CONTAINER(IDirect3DCubeTexture9)
    NORTHLIGHT_TRY_CONTAINER(IDirect3DVolumeTexture9)
    NORTHLIGHT_TRY_CONTAINER(IDirect3DVolume9)
    NORTHLIGHT_TRY_CONTAINER(IDirect3DVertexShader9)
    NORTHLIGHT_TRY_CONTAINER(IDirect3DPixelShader9)
    NORTHLIGHT_TRY_CONTAINER(IDirect3DVertexDeclaration9)
    NORTHLIGHT_TRY_CONTAINER(IDirect3DQuery9)
#undef NORTHLIGHT_TRY_CONTAINER
    IDirect3DDevice9* device=nullptr;
    if(SUCCEEDED((*out)->QueryInterface(__uuidof(IDirect3DDevice9),reinterpret_cast<void**>(&device)))&&device){
        const bool same=isOwnDevice(device);device->Release();
        if(same){(*out)->Release();*out=owner_;owner_->AddRef();return;}
        disable("foreign resource container device");return;
    }
    disable("unknown resource container IUnknown");
}
inline void Registry::wrapInterface(REFIID id,void** out)noexcept{
    if(!out||!*out)return;
    MirrorGuard lock(gate_,MirrorSite::Registry);
#define NORTHLIGHT_WRAP_INTERFACE(T) if(id==__uuidof(T)){T* p=static_cast<T*>(*out);wrap(&p);*out=p;return;}
    NORTHLIGHT_WRAP_INTERFACE(IUnknown)
    NORTHLIGHT_WRAP_INTERFACE(IDirect3DResource9)
    NORTHLIGHT_WRAP_INTERFACE(IDirect3DBaseTexture9)
    NORTHLIGHT_WRAP_INTERFACE(IDirect3DSurface9)
    NORTHLIGHT_WRAP_INTERFACE(IDirect3DTexture9)
    NORTHLIGHT_WRAP_INTERFACE(IDirect3DCubeTexture9)
    NORTHLIGHT_WRAP_INTERFACE(IDirect3DVolumeTexture9)
    NORTHLIGHT_WRAP_INTERFACE(IDirect3DVolume9)
    NORTHLIGHT_WRAP_INTERFACE(IDirect3DVertexShader9)
    NORTHLIGHT_WRAP_INTERFACE(IDirect3DPixelShader9)
    NORTHLIGHT_WRAP_INTERFACE(IDirect3DVertexDeclaration9)
    NORTHLIGHT_WRAP_INTERFACE(IDirect3DQuery9)
#undef NORTHLIGHT_WRAP_INTERFACE
    if(id==__uuidof(IDirect3DDevice9)){auto* device=static_cast<IDirect3DDevice9*>(*out);
        if(isOwnDevice(device)){device->Release();*out=owner_;owner_->AddRef();}
        else disable("foreign or unverified resource container device");return;}
    disable("unknown resource container interface");
}
}
