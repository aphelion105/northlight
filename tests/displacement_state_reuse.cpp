#define NOMINMAX
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include "forwarders.h"
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char*,...){}
#include "quality_settings.h"
#include "auto_displacement_renderer.h"
#include "device_mirror.h"

struct DisplacementStateTestAccess {
    static bool read(NorthlightAutoDisplacement& g,bool skinned=true){return g.readConstants(skinned?0:20,skinned?256:4,skinned);}
    static const float* values(const NorthlightAutoDisplacement& g){return g.constants;}
    static bool certified(const NorthlightAutoDisplacement& g){return g.constantStamp.valid;}
    static void declaration(NorthlightAutoDisplacement& g,IDirect3DVertexDeclaration9* d,UINT stride,bool accepted=true,bool indexed=false){
        const D3DVERTEXELEMENT9* e=nullptr;UINT n=0;assert(g.declarations.get(d,e,n));
        float vertices[16]={};NorthlightDrawSnapshot::Mesh mesh;
        const NorthlightDrawSnapshot::Draw draw{D3DPT_TRIANGLELIST,0,0,3,0,1,indexed};const uint16_t indices[]={0,1,2};
        assert(g.snapshots.readUP(d,draw,indexed?indices:nullptr,indexed?D3DFMT_INDEX16:D3DFMT_UNKNOWN,vertices,stride,mesh)==accepted);
        assert(n==2&&e[1].Stream==0xff); // The source end element must never be overwritten.
    }
};
struct Backend:ForwardIDirect3DDevice9 {
    float bank[1024]={};bool fail=false;IDirect3DDevice9* mutate=nullptr;
    Backend():ForwardIDirect3DDevice9(nullptr){}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* out,UINT count)override{
        if(fail)return D3DERR_INVALIDCALL;std::memcpy(out,bank+first*4,count*16);
        if(mutate){auto* writer=mutate;mutate=nullptr;const float changed[4]={21,22,23,24};assert(SUCCEEDED(writer->SetVertexShaderConstantF(64,changed,1)));}return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT first,const float* in,UINT count)override{if(fail)return D3DERR_INVALIDCALL;std::memcpy(bank+first*4,in,count*16);return D3D_OK;}
};
struct Meter:ForwardIDirect3DDevice9 {
    unsigned calls=0,registers=0;bool fail=false;explicit Meter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* out,UINT count)override{++calls;registers+=count;return fail?D3DERR_INVALIDCALL:real->GetVertexShaderConstantF(first,out,count);}
};
struct Declaration:IDirect3DVertexDeclaration9 {
    unsigned refs=1,reads=0;D3DVERTEXELEMENT9 elements[2];bool fail=false;
    explicit Declaration(WORD offset=0,WORD stream=0):elements{{stream,offset,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()}{}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count)override{++reads;if(fail||*count<2)return D3DERR_INVALIDCALL;std::memcpy(out,elements,sizeof elements);*count=2;return D3D_OK;}
};
int main(){
    Backend backend;DeviceMirror mirror;ExtensionDevice extension(&backend,&mirror);Meter meter(&extension);
    NorthlightAutoDisplacement g(&meter);
    auto attach=[&]{g.setConstantEpochSource(&extension,[](void* c,NorthlightConstantEpoch::Stamp& s){return static_cast<ExtensionDevice*>(c)->constantStamp(s);});};
    auto read=[&]{assert(DisplacementStateTestAccess::read(g));assert(!std::memcmp(DisplacementStateTestAccess::values(g),backend.bank,sizeof backend.bank));};
    attach();read();assert(meter.calls==1&&meter.registers==256);
    for(unsigned i=0;i<100;++i)read();assert(meter.calls==1); // No call or bank copy on an unchanged epoch.
    float row[8]={3,4,5,6,7,8,9,10};assert(SUCCEEDED(extension.SetVertexShaderConstantF(31,row,2)));
    read();assert(meter.calls==2&&meter.registers==288); // A write crossing two blocks reads both.
    assert(SUCCEEDED(extension.SetVertexShaderConstantF(252,row,1)));read();assert(meter.registers==304);
    assert(SUCCEEDED(extension.SetVertexShaderConstantF(64,row,1)));meter.fail=true;assert(!DisplacementStateTestAccess::read(g));assert(!DisplacementStateTestAccess::certified(g));meter.fail=false;read();
    assert(meter.registers==576);const auto invalidatedStart=meter.registers;
    mirror.invalidate();backend.bank[70*4]=9;read();assert(meter.registers==invalidatedStart+256);
    {ExtensionDevice::RawScope raw(extension);backend.bank[80*4]=10;read();assert(!DisplacementStateTestAccess::certified(g));}
    read();assert(meter.registers==invalidatedStart+768);
    mirror.recording=true;mirror.invalidate();read();assert(!DisplacementStateTestAccess::certified(g));mirror.recording=false;mirror.invalidate();read();
    backend.fail=true;mirror.invalidate();assert(!DisplacementStateTestAccess::read(g));assert(!DisplacementStateTestAccess::certified(g));backend.fail=false;read();
    mirror.enabled=false;mirror.invalidate();backend.bank[90*4]=11;read();assert(!DisplacementStateTestAccess::certified(g));mirror.enabled=true;mirror.invalidate();read();
    mirror.invalidate();const auto old=backend.bank[64*4];backend.mutate=&extension;assert(DisplacementStateTestAccess::read(g));assert(DisplacementStateTestAccess::values(g)[64*4]==old);
    assert(!DisplacementStateTestAccess::certified(g)&&!mirror.vsFloatKnown[64]);read();assert(DisplacementStateTestAccess::certified(g)&&DisplacementStateTestAccess::values(g)[64*4]==21);
    const auto before=meter.calls;assert(DisplacementStateTestAccess::read(g,false));read();assert(meter.calls==before+2);
    g.setConstantEpochSource(nullptr,nullptr);read();read();assert(!DisplacementStateTestAccess::certified(g));attach();read();g.reset();read();
    // A new device's equal serials cannot inherit the old object's bank.
    Backend other;other.bank[0]=12;DeviceMirror otherMirror;ExtensionDevice otherExtension(&other,&otherMirror);NorthlightAutoDisplacement next(&otherExtension);
    next.setConstantEpochSource(&otherExtension,[](void* c,NorthlightConstantEpoch::Stamp& s){return static_cast<ExtensionDevice*>(c)->constantStamp(s);});assert(DisplacementStateTestAccess::read(next));assert(DisplacementStateTestAccess::values(next)[0]==12);

    Declaration decl;for(unsigned i=0;i<100;++i)DisplacementStateTestAccess::declaration(g,&decl,12,true,i%2);assert(decl.reads==1&&decl.refs==2);
    g.reset();assert(decl.refs==1);DisplacementStateTestAccess::declaration(g,&decl,12);assert(decl.reads==2);
    Declaration unsupported(0,4);DisplacementStateTestAccess::declaration(g,&unsupported,12,false);assert(unsupported.reads==1);
    Declaration failed;failed.fail=true;const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
    NorthlightVertexDeclarations::Cache cache;assert(!cache.get(&failed,elements,count));failed.fail=false;assert(cache.get(&failed,elements,count));assert(failed.reads==2);
    std::vector<std::unique_ptr<Declaration>> layouts;for(unsigned i=0;i<129;++i){layouts.push_back(std::make_unique<Declaration>());assert(cache.get(layouts.back().get(),elements,count));}
    assert(layouts[0]->refs==1);assert(cache.get(layouts[0].get(),elements,count));assert(layouts[0]->reads==2);cache.clear();for(auto& layout:layouts)assert(layout->refs==1);
    alignas(Declaration) unsigned char storage[sizeof(Declaration)];auto* reused=new(storage) Declaration;
    DisplacementStateTestAccess::declaration(g,reused,12);assert(reused->refs==2);g.reset();assert(reused->refs==1);reused->~Declaration();
    reused=new(storage) Declaration(4);DisplacementStateTestAccess::declaration(g,reused,16);assert(reused->reads==1&&reused->elements[0].Offset==4);g.reset();reused->~Declaration();
    std::puts("PASS displacement state reuse: exact and partial epochs, invalidation, inactive sources, failed/raced reads, source replacement, immutable declarations, UP layouts and address reuse");
}
