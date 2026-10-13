#pragma once
// Minimal D3D9 API (only what spell_layer.h and saved_state.h use) and a FakeDevice that models the D3D9 semantics the spell layer depends on:
// a full draw-relevant state (render states, sampler states, textures, shaders, declaration/FVF, constants of both stages, viewport, scissor, clip
// planes, streams, indices, and the fixed-function state: transforms, texture stage states, material, lights), state blocks (ALL = everything except RT/DS, used by SavedState), SetRenderTarget resetting viewport/scissor, UP draws
// clearing stream 0 / indices, buffer locks with DISCARD / NOOVERWRITE / READONLY slices, and a record of every draw with the exact bytes it fetched
// at execution time. Refcounted objects with a leak check. `sets` counts the Set* calls (not the Gets, not the draws).
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#define NORTHLIGHT_SPELL_LAYER_TEST_API
using HRESULT=int32_t;using LONG=int32_t;using ULONG=uint32_t;using DWORD=uint32_t;using UINT=uint32_t;using INT=int32_t;using BOOL=int32_t;using WORD=uint16_t;using BYTE=uint8_t;using HANDLE=void*;
#define FALSE 0
#define TRUE 1
constexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=-1,E_FAIL=-6;
inline bool SUCCEEDED(HRESULT h){return h>=0;}inline bool FAILED(HRESULT h){return h<0;}
using D3DFORMAT=unsigned;using D3DPOOL=unsigned;using D3DPRIMITIVETYPE=unsigned;using D3DSTATEBLOCKTYPE=unsigned;using D3DRENDERSTATETYPE=unsigned;using D3DSAMPLERSTATETYPE=unsigned;using D3DRESOURCETYPE=unsigned;using D3DTRANSFORMSTATETYPE=unsigned;using D3DTEXTURESTAGESTATETYPE=unsigned;
constexpr D3DSTATEBLOCKTYPE D3DSBT_ALL=1;
constexpr D3DPRIMITIVETYPE D3DPT_POINTLIST=1,D3DPT_LINELIST=2,D3DPT_LINESTRIP=3,D3DPT_TRIANGLELIST=4,D3DPT_TRIANGLESTRIP=5,D3DPT_TRIANGLEFAN=6;
constexpr D3DFORMAT D3DFMT_INDEX16=101,D3DFMT_INDEX32=102,D3DFMT_VERTEXDATA=100;
constexpr D3DPOOL D3DPOOL_DEFAULT=0,D3DPOOL_MANAGED=1;
constexpr DWORD D3DUSAGE_RENDERTARGET=1,D3DUSAGE_WRITEONLY=8,D3DUSAGE_DYNAMIC=0x200;
constexpr D3DRESOURCETYPE D3DRTYPE_TEXTURE=3,D3DRTYPE_VOLUMETEXTURE=4,D3DRTYPE_CUBETEXTURE=5;
constexpr DWORD D3DLOCK_READONLY=0x10,D3DLOCK_NOOVERWRITE=0x1000,D3DLOCK_DISCARD=0x2000;
constexpr D3DRENDERSTATETYPE D3DRS_ZENABLE=7,D3DRS_ZWRITEENABLE=14,D3DRS_ALPHATESTENABLE=15,D3DRS_SRCBLEND=19,D3DRS_DESTBLEND=20,D3DRS_ALPHAREF=24,D3DRS_ALPHABLENDENABLE=27,D3DRS_STENCILENABLE=52,D3DRS_COLORWRITEENABLE=168,D3DRS_BLENDOP=171,D3DRS_POINTSIZE=154;
struct D3DVIEWPORT9{DWORD X=0,Y=0,Width=0,Height=0;float MinZ=0,MaxZ=1;};
struct D3DMATRIX{float m[4][4];};struct D3DCOLORVALUE{float r,g,b,a;};struct D3DVECTOR{float x,y,z;};
struct D3DMATERIAL9{D3DCOLORVALUE Diffuse,Ambient,Specular,Emissive;float Power;};
struct D3DLIGHT9{DWORD Type;D3DCOLORVALUE Diffuse,Specular,Ambient;D3DVECTOR Position,Direction;float Range,Falloff,Attenuation0,Attenuation1,Attenuation2,Theta,Phi;};
struct RECT{int left=0,top=0,right=0,bottom=0;};
struct D3DVERTEXELEMENT9{WORD Stream;WORD Offset;BYTE Type;BYTE Method;BYTE Usage;BYTE UsageIndex;};
struct D3DVERTEXBUFFER_DESC{D3DFORMAT Format;D3DRESOURCETYPE Type;DWORD Usage;D3DPOOL Pool;UINT Size;DWORD FVF;};
struct D3DSURFACE_DESC{D3DFORMAT Format;D3DRESOURCETYPE Type;DWORD Usage;D3DPOOL Pool;UINT Width,Height;};
struct D3DINDEXBUFFER_DESC{D3DFORMAT Format;D3DRESOURCETYPE Type;DWORD Usage;D3DPOOL Pool;UINT Size;};

struct IUnknown{virtual ULONG AddRef()=0;virtual ULONG Release()=0;protected:virtual ~IUnknown()=default;};
struct IDirect3DVertexShader9:IUnknown{};struct IDirect3DPixelShader9:IUnknown{};struct IDirect3DBaseTexture9:IUnknown{virtual D3DRESOURCETYPE GetType()=0;};
struct IDirect3DTexture9:IDirect3DBaseTexture9{virtual HRESULT GetLevelDesc(UINT,D3DSURFACE_DESC*)=0;};
struct IDirect3DCubeTexture9:IDirect3DBaseTexture9{virtual HRESULT GetLevelDesc(UINT,D3DSURFACE_DESC*)=0;};
struct IDirect3DVolumeTexture9:IDirect3DBaseTexture9{};struct IDirect3DSurface9:IUnknown{};
struct IDirect3DVertexDeclaration9:IUnknown{virtual HRESULT GetDeclaration(D3DVERTEXELEMENT9* e,UINT* n)=0;};
struct IDirect3DVertexBuffer9:IUnknown{virtual HRESULT Lock(UINT off,UINT size,void** data,DWORD flags)=0;virtual HRESULT Unlock()=0;virtual HRESULT GetDesc(D3DVERTEXBUFFER_DESC*)=0;};
struct IDirect3DIndexBuffer9:IUnknown{virtual HRESULT Lock(UINT off,UINT size,void** data,DWORD flags)=0;virtual HRESULT Unlock()=0;virtual HRESULT GetDesc(D3DINDEXBUFFER_DESC*)=0;};
struct IDirect3DStateBlock9:IUnknown{virtual HRESULT Capture()=0;virtual HRESULT Apply()=0;};
struct IDirect3DDevice9{
    virtual HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9**)=0;
    virtual HRESULT CreateVertexBuffer(UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*)=0;
    virtual HRESULT CreateIndexBuffer(UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DIndexBuffer9**,HANDLE*)=0;
    virtual HRESULT SetRenderTarget(DWORD,IDirect3DSurface9*)=0;virtual HRESULT GetRenderTarget(DWORD,IDirect3DSurface9**)=0;
    virtual HRESULT SetDepthStencilSurface(IDirect3DSurface9*)=0;virtual HRESULT GetDepthStencilSurface(IDirect3DSurface9**)=0;
    virtual HRESULT SetViewport(const D3DVIEWPORT9*)=0;virtual HRESULT GetViewport(D3DVIEWPORT9*)=0;
    virtual HRESULT SetScissorRect(const RECT*)=0;virtual HRESULT GetScissorRect(RECT*)=0;
    virtual HRESULT SetRenderState(D3DRENDERSTATETYPE,DWORD)=0;virtual HRESULT GetRenderState(D3DRENDERSTATETYPE,DWORD*)=0;
    virtual HRESULT SetSamplerState(DWORD,D3DSAMPLERSTATETYPE,DWORD)=0;virtual HRESULT GetSamplerState(DWORD,D3DSAMPLERSTATETYPE,DWORD*)=0;
    virtual HRESULT SetTexture(DWORD,IDirect3DBaseTexture9*)=0;virtual HRESULT GetTexture(DWORD,IDirect3DBaseTexture9**)=0;
    virtual HRESULT SetVertexShader(IDirect3DVertexShader9*)=0;virtual HRESULT GetVertexShader(IDirect3DVertexShader9**)=0;
    virtual HRESULT SetPixelShader(IDirect3DPixelShader9*)=0;virtual HRESULT GetPixelShader(IDirect3DPixelShader9**)=0;
    virtual HRESULT SetFVF(DWORD)=0;virtual HRESULT GetFVF(DWORD*)=0;
    virtual HRESULT SetClipPlane(DWORD,const float*)=0;virtual HRESULT GetClipPlane(DWORD,float*)=0;
    virtual HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9*)=0;virtual HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9**)=0;
    virtual HRESULT SetStreamSource(UINT,IDirect3DVertexBuffer9*,UINT,UINT)=0;virtual HRESULT GetStreamSource(UINT,IDirect3DVertexBuffer9**,UINT*,UINT*)=0;
    virtual HRESULT SetStreamSourceFreq(UINT,UINT)=0;virtual HRESULT GetStreamSourceFreq(UINT,UINT*)=0;
    virtual HRESULT SetIndices(IDirect3DIndexBuffer9*)=0;virtual HRESULT GetIndices(IDirect3DIndexBuffer9**)=0;
    virtual HRESULT SetVertexShaderConstantF(UINT,const float*,UINT)=0;virtual HRESULT GetVertexShaderConstantF(UINT,float*,UINT)=0;
    virtual HRESULT SetVertexShaderConstantI(UINT,const int*,UINT)=0;virtual HRESULT GetVertexShaderConstantI(UINT,int*,UINT)=0;
    virtual HRESULT SetVertexShaderConstantB(UINT,const BOOL*,UINT)=0;virtual HRESULT GetVertexShaderConstantB(UINT,BOOL*,UINT)=0;
    virtual HRESULT SetPixelShaderConstantF(UINT,const float*,UINT)=0;virtual HRESULT GetPixelShaderConstantF(UINT,float*,UINT)=0;
    virtual HRESULT SetPixelShaderConstantI(UINT,const int*,UINT)=0;virtual HRESULT GetPixelShaderConstantI(UINT,int*,UINT)=0;
    virtual HRESULT SetPixelShaderConstantB(UINT,const BOOL*,UINT)=0;virtual HRESULT GetPixelShaderConstantB(UINT,BOOL*,UINT)=0;
    virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE,const D3DMATRIX*)=0;virtual HRESULT GetTransform(D3DTRANSFORMSTATETYPE,D3DMATRIX*)=0;
    virtual HRESULT SetTextureStageState(DWORD,D3DTEXTURESTAGESTATETYPE,DWORD)=0;virtual HRESULT GetTextureStageState(DWORD,D3DTEXTURESTAGESTATETYPE,DWORD*)=0;
    virtual HRESULT SetMaterial(const D3DMATERIAL9*)=0;virtual HRESULT GetMaterial(D3DMATERIAL9*)=0;
    virtual HRESULT SetLight(DWORD,const D3DLIGHT9*)=0;virtual HRESULT GetLight(DWORD,D3DLIGHT9*)=0;
    virtual HRESULT LightEnable(DWORD,BOOL)=0;virtual HRESULT GetLightEnable(DWORD,BOOL*)=0;
    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE,UINT,UINT)=0;
    virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)=0;
    virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void*,UINT)=0;
    virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT)=0;
protected:virtual ~IDirect3DDevice9()=default;
};
#include "spell_layer.h"

// ---------------- fake objects ----------------
namespace Fake{
inline int live=0; // every refcounted object alive
template<class B> struct Obj:B{
    LONG refs=1;Obj(){++live;}
    ULONG AddRef()override{return ULONG(++refs);}
    ULONG Release()override{ULONG r=ULONG(--refs);if(!r)delete this;return r;}
    ~Obj()override{--live;}
};
struct Surface:Obj<IDirect3DSurface9>{UINT w,h;Surface(UINT w_,UINT h_):w(w_),h(h_){}};
struct Texture:Obj<IDirect3DTexture9>{
    DWORD usage=0;HRESULT descResult=D3D_OK;int descCalls=0;int ver=0; // ver: the texel contents (a game write bumps it); every draw records the version of each bound texture it executes with
    D3DRESOURCETYPE GetType()override{return D3DRTYPE_TEXTURE;}
    HRESULT GetLevelDesc(UINT,D3DSURFACE_DESC* d)override{++descCalls;if(FAILED(descResult))return descResult;*d=D3DSURFACE_DESC{};d->Type=D3DRTYPE_TEXTURE;d->Usage=usage;return D3D_OK;}
};
struct CubeTexture:Obj<IDirect3DCubeTexture9>{
    DWORD usage=0;
    D3DRESOURCETYPE GetType()override{return D3DRTYPE_CUBETEXTURE;}
    HRESULT GetLevelDesc(UINT,D3DSURFACE_DESC* d)override{*d=D3DSURFACE_DESC{};d->Type=D3DRTYPE_CUBETEXTURE;d->Usage=usage;return D3D_OK;}
};
struct VolumeTexture:Obj<IDirect3DVolumeTexture9>{D3DRESOURCETYPE GetType()override{return D3DRTYPE_VOLUMETEXTURE;}};
struct VShader:Obj<IDirect3DVertexShader9>{};struct PShader:Obj<IDirect3DPixelShader9>{};
struct Decl:Obj<IDirect3DVertexDeclaration9>{
    std::vector<D3DVERTEXELEMENT9> el;
    explicit Decl(std::vector<D3DVERTEXELEMENT9> e):el(std::move(e)){el.push_back(D3DVERTEXELEMENT9{0xFF,0,17,0,0,0});}
    HRESULT GetDeclaration(D3DVERTEXELEMENT9* out,UINT* n)override{if(out)std::memcpy(out,el.data(),el.size()*sizeof(D3DVERTEXELEMENT9));*n=UINT(el.size());return D3D_OK;}
};
// Buffer storage with D3D9 lock semantics. DISCARD hands out a fresh slice (poisoned 0xDD, the old bytes are unreachable for later draws),
// NOOVERWRITE and 0 write the current slice in place, READONLY reads it. `guarded` buffers call `hook` before every non-READONLY lock.
struct BufState{
    std::vector<std::vector<uint8_t>> slices;DWORD usage=0;UINT size=0;int locks=0;bool guarded=false;void* key=nullptr;
    std::function<void(BufState&,UINT,UINT,DWORD)> hook;int failReadOnly=0;
    void init(UINT n,DWORD u){size=n;usage=u;slices.assign(1,std::vector<uint8_t>(n,0));}
    uint8_t* cur(){return slices.back().data();}
    HRESULT lock(UINT off,UINT sz,void** data,DWORD flags){
        if((flags&D3DLOCK_READONLY)&&failReadOnly>0){--failReadOnly;return D3DERR_INVALIDCALL;}
        if(guarded&&!(flags&D3DLOCK_READONLY)&&hook)hook(*this,off,sz,flags);
        if(flags&D3DLOCK_DISCARD){if(!(usage&D3DUSAGE_DYNAMIC))return D3DERR_INVALIDCALL;slices.emplace_back(size,uint8_t(0xDD));}
        if(off>size||(sz&&off+sz>size))return D3DERR_INVALIDCALL;
        ++locks;*data=cur()+off;return D3D_OK;
    }
};
struct VB:Obj<IDirect3DVertexBuffer9>{
    BufState b;
    HRESULT Lock(UINT o,UINT s,void** d,DWORD f)override{return b.lock(o,s,d,f);}
    HRESULT Unlock()override{--b.locks;return D3D_OK;}
    HRESULT GetDesc(D3DVERTEXBUFFER_DESC* d)override{*d=D3DVERTEXBUFFER_DESC{D3DFMT_VERTEXDATA,0,b.usage,D3DPOOL_DEFAULT,b.size,0};return D3D_OK;}
};
struct IB:Obj<IDirect3DIndexBuffer9>{
    BufState b;D3DFORMAT fmt=D3DFMT_INDEX16;
    HRESULT Lock(UINT o,UINT s,void** d,DWORD f)override{return b.lock(o,s,d,f);}
    HRESULT Unlock()override{--b.locks;return D3D_OK;}
    HRESULT GetDesc(D3DINDEXBUFFER_DESC* d)override{*d=D3DINDEXBUFFER_DESC{fmt,0,b.usage,D3DPOOL_DEFAULT,b.size};return D3D_OK;}
};
// Everything a state block / the game sees, except RT/DS.
struct State{
    DWORD rs[256]={};DWORD ss[20][16]={}; /* by texture slot, sampler state type 0..15 */
    IDirect3DBaseTexture9* tex[20]={}; /* 0..15 pixel samplers, 16..19 vertex texture samplers (stages 257..260) */ IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;DWORD fvf=0;
    struct S{IDirect3DVertexBuffer9* vb=nullptr;UINT off=0,stride=0,freq=1;} st[16];
    IDirect3DIndexBuffer9* ib=nullptr;float vsc[256][4]={};int vsi[16][4]={};BOOL vsb[16]={};float psc[224][4]={};int psi[16][4]={};BOOL psb[16]={};float clip[6][4]={};D3DVIEWPORT9 vp;RECT scissor;
    D3DMATRIX tf[14]={};DWORD tss[8][33]={};D3DMATERIAL9 mat={};D3DLIGHT9 light[8]={};BOOL lightSet[8]={},lightOn[8]={}; /* tf: WORLD, VIEW, PROJECTION, TEXTURE0..7, WORLD1..3 */
};
template<class T> inline void swapRef(T*& dst,T* src){if(src)src->AddRef();if(dst)dst->Release();dst=src;}
inline void retain(State& s){
    for(auto* t:s.tex)if(t)t->AddRef();
    if(s.vs)s.vs->AddRef();if(s.ps)s.ps->AddRef();if(s.decl)s.decl->AddRef();if(s.ib)s.ib->AddRef();
    for(auto& x:s.st)if(x.vb)x.vb->AddRef();
}
inline void drop(State& s){
    for(auto*& t:s.tex)if(t){t->Release();t=nullptr;}
    if(s.vs){s.vs->Release();s.vs=nullptr;}if(s.ps){s.ps->Release();s.ps=nullptr;}if(s.decl){s.decl->Release();s.decl=nullptr;}if(s.ib){s.ib->Release();s.ib=nullptr;}
    for(auto& x:s.st)if(x.vb){x.vb->Release();x.vb=nullptr;}
}
inline void assign(State& dst,const State& src){State n=src;retain(n);drop(dst);dst=n;}
inline bool equal(const State& a,const State& b,bool ignoreGeometry){
    if(std::memcmp(a.rs,b.rs,sizeof a.rs)||std::memcmp(a.ss,b.ss,sizeof a.ss)||std::memcmp(a.tex,b.tex,sizeof a.tex)||std::memcmp(a.vsc,b.vsc,sizeof a.vsc)||std::memcmp(a.vsi,b.vsi,sizeof a.vsi)||std::memcmp(a.vsb,b.vsb,sizeof a.vsb))return false;
    if(std::memcmp(a.psc,b.psc,sizeof a.psc)||std::memcmp(a.psi,b.psi,sizeof a.psi)||std::memcmp(a.psb,b.psb,sizeof a.psb)||std::memcmp(a.clip,b.clip,sizeof a.clip)||a.fvf!=b.fvf)return false;
    if(std::memcmp(a.tf,b.tf,sizeof a.tf)||std::memcmp(a.tss,b.tss,sizeof a.tss)||std::memcmp(&a.mat,&b.mat,sizeof a.mat)||std::memcmp(a.light,b.light,sizeof a.light)||std::memcmp(a.lightSet,b.lightSet,sizeof a.lightSet)||std::memcmp(a.lightOn,b.lightOn,sizeof a.lightOn))return false;
    if(a.vs!=b.vs||a.ps!=b.ps||a.decl!=b.decl)return false;
    if(std::memcmp(&a.vp,&b.vp,sizeof a.vp)||a.scissor.left!=b.scissor.left||a.scissor.top!=b.scissor.top||a.scissor.right!=b.scissor.right||a.scissor.bottom!=b.scissor.bottom)return false;
    if(!ignoreGeometry){
        if(a.ib!=b.ib)return false;
        for(int i=0;i<16;++i)if(a.st[i].vb!=b.st[i].vb||a.st[i].off!=b.st[i].off||a.st[i].stride!=b.st[i].stride||a.st[i].freq!=b.st[i].freq)return false;
    }else for(int i=0;i<16;++i)if(a.st[i].freq!=b.st[i].freq)return false;
    return true;
}
struct Snapshot{State s;IDirect3DSurface9* rt[4]={};IDirect3DSurface9* ds=nullptr;}; // raw pointers, no references
struct DrawRec{
    int kind=0;D3DPRIMITIVETYPE prim=0;State st;IDirect3DSurface9* rt[4]={};IDirect3DSurface9* ds=nullptr;
    std::vector<uint8_t> vtx; std::vector<uint32_t> idx;bool oob=false;int tag=0;INT base=0;UINT first=0;int texVer[20]={};
};
struct StateBlock:Obj<IDirect3DStateBlock9>{
    struct FakeDevice* dev;State s;
    explicit StateBlock(FakeDevice* d);
    ~StateBlock()override{drop(s);}
    HRESULT Capture()override;HRESULT Apply()override;
};
// The state cache the layer reads in bulk (MirrorView of spell_layer.h), as the production DeviceMirror lays it out; refreshed from the device state at every
// enter(). `hide*` make slots unknown (known flag false, junk value) so the layer must fall back to the Get calls for them.
struct Mirror{
    DWORD rs[256]={};bool rsK[256]={};DWORD smp[16][16]={};bool smpK[16][16]={};IDirect3DBaseTexture9* tex[16]={};bool texK[16]={};
    float vsF[256][4]={};bool vsFK[256]={};float psF[224][4]={};bool psFK[224]={};int vsI[16][4]={};bool vsIK[16]={};BOOL vsB[16]={};bool vsBK[16]={};
    IDirect3DVertexShader9* vs=nullptr;bool vsK=false;IDirect3DPixelShader9* ps=nullptr;bool psK=false;IDirect3DVertexDeclaration9* decl=nullptr;bool declK=false;
    DWORD fvf=0;bool fvfK=false;D3DVIEWPORT9 vp;bool vpK=false;
    bool hideRs[256]={},hideSmp[16]={},hideTex[16]={},hideVsF[256]={},hidePsF[224]={},hideVsI=false,hideVsB=false,hideShaders=false,hideDecl=false,hideVp=false,failEnter=false;
    unsigned enters=0,leaves=0;int depth=0;
};
struct FakeDevice:IDirect3DDevice9{
    State s;Mirror mir;IDirect3DSurface9* rt[4]={};IDirect3DSurface9* ds=nullptr;
    std::vector<DrawRec> draws;unsigned calls=0,sets=0;
    bool failCreateBlock=false,failCapture=false,failCreateVB=false,failCreateIB=false,failGetPsConst=false,failGetTss=false,failGetLightEnable=false;int failCaptureAfter=-1;unsigned blocksCreated=0,failNthCreate=0,createCalls=0,captures=0,applies=0;bool failGetRs=false,failGetSampler=false,failGetStream=false; // captures/applies: StateBlock::Capture/Apply calls; failGet*: those Get calls fail // failNthCreate: the nth CreateStateBlock call fails once
    FakeDevice(){
        s.vp=D3DVIEWPORT9();s.scissor=RECT();
        for(auto& x:s.st)x.freq=1;
    }
    ~FakeDevice()override{
        drop(s);for(auto*& r:rt)if(r){r->Release();r=nullptr;}
        if(ds){ds->Release();ds=nullptr;}
    }
    static size_t texSlot(DWORD stage){return stage>=257?size_t(16+(stage-257)):size_t(stage);}
    // ---- test conveniences (not part of the API; not counted) ----
    int tagOf()const{return int(s.rs[165]);} // the draw tag lives in D3DRS_DEBUGMONITORTOKEN: every draw records render states (a fixed-function draw reads no constants)
    Snapshot snap()const{Snapshot n;n.s=s;for(int i=0;i<4;++i)n.rt[i]=rt[i];n.ds=ds;return n;}
    // ---- IDirect3DDevice9 ----
    HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE t,IDirect3DStateBlock9** out)override{
        ++calls;if(failCreateBlock||t!=D3DSBT_ALL||(failNthCreate&&++createCalls==failNthCreate))return E_FAIL;
        ++blocksCreated;*out=new StateBlock(this);return D3D_OK;
    }
    HRESULT CreateVertexBuffer(UINT n,DWORD u,DWORD,D3DPOOL,IDirect3DVertexBuffer9** out,HANDLE*)override{
        ++calls;if(failCreateVB)return E_FAIL;auto* b=new VB;b->b.init(n,u);b->b.key=static_cast<IDirect3DVertexBuffer9*>(b);*out=b;return D3D_OK;
    }
    HRESULT CreateIndexBuffer(UINT n,DWORD u,D3DFORMAT f,D3DPOOL,IDirect3DIndexBuffer9** out,HANDLE*)override{
        ++calls;if(failCreateIB)return E_FAIL;auto* b=new IB;b->b.init(n,u);b->fmt=f;b->b.key=static_cast<IDirect3DIndexBuffer9*>(b);*out=b;return D3D_OK;
    }
    HRESULT SetRenderTarget(DWORD i,IDirect3DSurface9* t)override{
        ++calls;++sets;if(i>3||(i==0&&!t))return D3DERR_INVALIDCALL;
        swapRef(rt[i],t);
        if(i==0){auto* f=static_cast<Surface*>(t);s.vp=D3DVIEWPORT9();s.vp.Width=f->w;s.vp.Height=f->h;s.vp.MaxZ=1;s.scissor=RECT();s.scissor.right=int(f->w);s.scissor.bottom=int(f->h);}
        return D3D_OK;
    }
    HRESULT GetRenderTarget(DWORD i,IDirect3DSurface9** o)override{++calls;if(i>3||!rt[i]){*o=nullptr;return D3DERR_INVALIDCALL;}rt[i]->AddRef();*o=rt[i];return D3D_OK;}
    HRESULT SetDepthStencilSurface(IDirect3DSurface9* d)override{++calls;++sets;swapRef(ds,d);return D3D_OK;}
    HRESULT GetDepthStencilSurface(IDirect3DSurface9** o)override{++calls;if(ds)ds->AddRef();*o=ds;return ds?D3D_OK:D3DERR_INVALIDCALL;}
    HRESULT SetViewport(const D3DVIEWPORT9* v)override{++calls;++sets;s.vp=*v;return D3D_OK;}
    HRESULT GetViewport(D3DVIEWPORT9* v)override{++calls;*v=s.vp;return D3D_OK;}
    HRESULT SetScissorRect(const RECT* r)override{++calls;++sets;s.scissor=*r;return D3D_OK;}
    HRESULT GetScissorRect(RECT* r)override{++calls;*r=s.scissor;return D3D_OK;}
    HRESULT SetRenderState(D3DRENDERSTATETYPE t,DWORD v)override{++calls;++sets;s.rs[t]=v;return D3D_OK;}
    HRESULT GetRenderState(D3DRENDERSTATETYPE t,DWORD* v)override{++calls;if(failGetRs)return E_FAIL;*v=s.rs[t];return D3D_OK;}
    HRESULT SetSamplerState(DWORD i,D3DSAMPLERSTATETYPE t,DWORD v)override{++calls;++sets;s.ss[texSlot(i)][t]=v;return D3D_OK;}
    HRESULT GetSamplerState(DWORD i,D3DSAMPLERSTATETYPE t,DWORD* v)override{++calls;if(failGetSampler)return E_FAIL;*v=s.ss[texSlot(i)][t];return D3D_OK;}
    HRESULT SetTexture(DWORD i,IDirect3DBaseTexture9* t)override{++calls;++sets;swapRef(s.tex[texSlot(i)],t);return D3D_OK;}
    HRESULT GetTexture(DWORD i,IDirect3DBaseTexture9** o)override{++calls;auto*& t=s.tex[texSlot(i)];if(t)t->AddRef();*o=t;return D3D_OK;}
    HRESULT SetVertexShader(IDirect3DVertexShader9* v)override{++calls;++sets;swapRef(s.vs,v);return D3D_OK;}
    HRESULT GetVertexShader(IDirect3DVertexShader9** o)override{++calls;if(s.vs)s.vs->AddRef();*o=s.vs;return D3D_OK;}
    HRESULT GetPixelShader(IDirect3DPixelShader9** o)override{++calls;if(s.ps)s.ps->AddRef();*o=s.ps;return D3D_OK;}
    HRESULT SetFVF(DWORD f)override{++calls;++sets;swapRef(s.decl,static_cast<IDirect3DVertexDeclaration9*>(nullptr));s.fvf=f;return D3D_OK;}
    HRESULT GetFVF(DWORD* f)override{++calls;*f=s.fvf;return D3D_OK;}
    HRESULT SetClipPlane(DWORD i,const float* p)override{++calls;++sets;if(i>=6)return D3DERR_INVALIDCALL;std::memcpy(s.clip[i],p,16);return D3D_OK;}
    HRESULT GetClipPlane(DWORD i,float* p)override{++calls;if(i>=6)return D3DERR_INVALIDCALL;std::memcpy(p,s.clip[i],16);return D3D_OK;}
    HRESULT SetPixelShader(IDirect3DPixelShader9* v)override{++calls;++sets;swapRef(s.ps,v);return D3D_OK;}
    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9* d)override{++calls;++sets;swapRef(s.decl,d);s.fvf=0;return D3D_OK;}
    HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9** o)override{++calls;if(s.decl)s.decl->AddRef();*o=s.decl;return D3D_OK;}
    HRESULT SetStreamSource(UINT i,IDirect3DVertexBuffer9* b,UINT off,UINT stride)override{++calls;++sets;if(i>=16)return D3DERR_INVALIDCALL;swapRef(s.st[i].vb,b);s.st[i].off=off;s.st[i].stride=stride;return D3D_OK;}
    HRESULT GetStreamSource(UINT i,IDirect3DVertexBuffer9** b,UINT* off,UINT* stride)override{
        ++calls;if(i>=16||failGetStream)return D3DERR_INVALIDCALL;if(s.st[i].vb)s.st[i].vb->AddRef();*b=s.st[i].vb;*off=s.st[i].off;*stride=s.st[i].stride;return D3D_OK;
    }
    HRESULT SetStreamSourceFreq(UINT i,UINT f)override{++calls;++sets;if(i>=16)return D3DERR_INVALIDCALL;s.st[i].freq=f;return D3D_OK;}
    HRESULT GetStreamSourceFreq(UINT i,UINT* f)override{++calls;if(i>=16)return D3DERR_INVALIDCALL;*f=s.st[i].freq;return D3D_OK;}
    HRESULT SetIndices(IDirect3DIndexBuffer9* b)override{++calls;++sets;swapRef(s.ib,b);return D3D_OK;}
    HRESULT GetIndices(IDirect3DIndexBuffer9** o)override{++calls;if(s.ib)s.ib->AddRef();*o=s.ib;return D3D_OK;}
    HRESULT SetVertexShaderConstantF(UINT r,const float* v,UINT n)override{++calls;++sets;if(r+n>256)return D3DERR_INVALIDCALL;std::memcpy(s.vsc[r],v,n*16);return D3D_OK;}
    HRESULT GetVertexShaderConstantF(UINT r,float* v,UINT n)override{++calls;if(r+n>256)return D3DERR_INVALIDCALL;std::memcpy(v,s.vsc[r],n*16);return D3D_OK;}
    HRESULT SetVertexShaderConstantI(UINT r,const int* v,UINT n)override{++calls;++sets;if(r+n>16)return D3DERR_INVALIDCALL;std::memcpy(s.vsi[r],v,n*16);return D3D_OK;}
    HRESULT GetVertexShaderConstantI(UINT r,int* v,UINT n)override{++calls;if(r+n>16)return D3DERR_INVALIDCALL;std::memcpy(v,s.vsi[r],n*16);return D3D_OK;}
    HRESULT SetVertexShaderConstantB(UINT r,const BOOL* v,UINT n)override{++calls;++sets;if(r+n>16)return D3DERR_INVALIDCALL;std::memcpy(s.vsb+r,v,n*4);return D3D_OK;}
    HRESULT GetVertexShaderConstantB(UINT r,BOOL* v,UINT n)override{++calls;if(r+n>16)return D3DERR_INVALIDCALL;std::memcpy(v,s.vsb+r,n*4);return D3D_OK;}
    HRESULT SetPixelShaderConstantF(UINT r,const float* v,UINT n)override{++calls;++sets;if(r+n>224)return D3DERR_INVALIDCALL;std::memcpy(s.psc[r],v,n*16);return D3D_OK;}
    HRESULT GetPixelShaderConstantF(UINT r,float* v,UINT n)override{++calls;if(failGetPsConst||r+n>224)return D3DERR_INVALIDCALL;std::memcpy(v,s.psc[r],n*16);return D3D_OK;}
    HRESULT SetPixelShaderConstantI(UINT r,const int* v,UINT n)override{++calls;++sets;if(r+n>16)return D3DERR_INVALIDCALL;std::memcpy(s.psi[r],v,n*16);return D3D_OK;}
    HRESULT GetPixelShaderConstantI(UINT r,int* v,UINT n)override{++calls;if(r+n>16)return D3DERR_INVALIDCALL;std::memcpy(v,s.psi[r],n*16);return D3D_OK;}
    HRESULT SetPixelShaderConstantB(UINT r,const BOOL* v,UINT n)override{++calls;++sets;if(r+n>16)return D3DERR_INVALIDCALL;std::memcpy(s.psb+r,v,n*4);return D3D_OK;}
    HRESULT GetPixelShaderConstantB(UINT r,BOOL* v,UINT n)override{++calls;if(r+n>16)return D3DERR_INVALIDCALL;std::memcpy(v,s.psb+r,n*4);return D3D_OK;}
    // ---- fixed-function state (the valid texture stage state types only; transforms WORLD, VIEW, PROJECTION, TEXTURE0..7, WORLD1..3) ----
    static int tfSlot(DWORD t){if(t==256)return 0;if(t==2||t==3)return int(t)-1;if(t>=16&&t<=23)return int(t)-13;if(t>=257&&t<=259)return int(t)-246;return -1;}
    static bool tssValid(DWORD t){return (t>=1&&t<=11)||t==22||t==23||t==24||t==26||t==27||t==28||t==32;}
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE t,const D3DMATRIX* m)override{++calls;++sets;const int i=tfSlot(t);if(i<0||!m)return D3DERR_INVALIDCALL;s.tf[i]=*m;return D3D_OK;}
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE t,D3DMATRIX* m)override{++calls;const int i=tfSlot(t);if(i<0||!m)return D3DERR_INVALIDCALL;*m=s.tf[i];return D3D_OK;}
    HRESULT SetTextureStageState(DWORD st,D3DTEXTURESTAGESTATETYPE t,DWORD v)override{++calls;++sets;if(st>=8||!tssValid(t))return D3DERR_INVALIDCALL;s.tss[st][t]=v;return D3D_OK;}
    HRESULT GetTextureStageState(DWORD st,D3DTEXTURESTAGESTATETYPE t,DWORD* v)override{++calls;if(st>=8||!tssValid(t)||failGetTss)return D3DERR_INVALIDCALL;*v=s.tss[st][t];return D3D_OK;}
    HRESULT SetMaterial(const D3DMATERIAL9* m)override{++calls;++sets;s.mat=*m;return D3D_OK;}
    HRESULT GetMaterial(D3DMATERIAL9* m)override{++calls;*m=s.mat;return D3D_OK;}
    HRESULT SetLight(DWORD i,const D3DLIGHT9* l)override{++calls;++sets;if(i>=8)return D3DERR_INVALIDCALL;s.light[i]=*l;s.lightSet[i]=TRUE;return D3D_OK;}
    HRESULT GetLight(DWORD i,D3DLIGHT9* l)override{++calls;if(i>=8||!s.lightSet[i])return D3DERR_INVALIDCALL;*l=s.light[i];return D3D_OK;}
    HRESULT LightEnable(DWORD i,BOOL on)override{++calls;++sets;if(i>=8)return D3DERR_INVALIDCALL;if(!s.lightSet[i]){s.light[i]=D3DLIGHT9{};s.lightSet[i]=TRUE;}s.lightOn[i]=on?TRUE:FALSE;return D3D_OK;} // like D3D9: enabling a light never set creates a default one
    HRESULT GetLightEnable(DWORD i,BOOL* on)override{++calls;if(i>=8||!s.lightSet[i]||failGetLightEnable)return D3DERR_INVALIDCALL;*on=s.lightOn[i];return D3D_OK;}
    // ---- draws: record state and the bytes fetched NOW ----
    DrawRec begin(int kind,D3DPRIMITIVETYPE prim){
        DrawRec d;d.kind=kind;d.prim=prim;d.st=s;for(int i=0;i<4;++i)d.rt[i]=rt[i];d.ds=ds;d.tag=tagOf();for(int i=0;i<20;++i){auto* x=dynamic_cast<Texture*>(s.tex[i]);d.texVer[i]=x?x->ver:-1;}return d; // st copy: raw pointers, no refs
    }
    static const uint8_t* bufBytes(IDirect3DVertexBuffer9* b,UINT& size){auto* v=static_cast<VB*>(b);size=v->b.size;return v->b.cur();}
    void fetch(DrawRec& d,UINT stream,int64_t vertex){ // one vertex of one stream
        const auto& st=s.st[stream];UINT size=0;
        if(!st.vb){d.oob=true;return;}
        const uint8_t* base=bufBytes(st.vb,size);const int64_t at=int64_t(st.off)+vertex*st.stride;
        if(at<0||at+st.stride>size){d.oob=true;return;}
        d.vtx.insert(d.vtx.end(),base+at,base+at+st.stride);
    }
    std::vector<UINT> usedStreams()const{
        std::vector<UINT> u;
        if(auto* d=static_cast<Decl*>(s.decl)){for(auto& e:d->el)if(e.Stream!=0xFF&&std::find(u.begin(),u.end(),UINT(e.Stream))==u.end())u.push_back(e.Stream);}
        else u.push_back(0);
        return u;
    }
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE p,UINT start,UINT count)override{
        ++calls;DrawRec d=begin(0,p);d.first=start;const UINT n=NorthlightSpellLayer::vertexCount(p,count);
        for(UINT v=0;v<n;++v)for(UINT st:usedStreams())fetch(d,st,int64_t(start)+v);
        draws.push_back(std::move(d));return D3D_OK;
    }
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE p,INT base,UINT minIdx,UINT num,UINT startIndex,UINT count)override{
        ++calls;DrawRec d=begin(1,p);d.base=base;d.first=startIndex;const UINT n=NorthlightSpellLayer::vertexCount(p,count);
        auto* ib=static_cast<IB*>(s.ib);if(!ib){d.oob=true;draws.push_back(std::move(d));return D3D_OK;}
        const UINT isz=ib->fmt==D3DFMT_INDEX16?2:4;
        for(UINT i=0;i<n;++i){
            const uint8_t* at=ib->b.cur()+size_t(startIndex+i)*isz;
            if(size_t(startIndex+i+1)*isz>ib->b.size){d.oob=true;break;}
            uint32_t v=0;std::memcpy(&v,at,isz);d.idx.push_back(v);
            if(v<minIdx||v>=minIdx+num)d.oob=true;
            for(UINT st:usedStreams())fetch(d,st,int64_t(base)+v);
        }
        draws.push_back(std::move(d));return D3D_OK;
    }
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE p,UINT count,const void* data,UINT stride)override{
        ++calls;DrawRec d=begin(2,p);const UINT n=NorthlightSpellLayer::vertexCount(p,count);
        d.vtx.assign(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+size_t(n)*stride);
        draws.push_back(std::move(d));
        swapRef(s.st[0].vb,static_cast<IDirect3DVertexBuffer9*>(nullptr));s.st[0].off=0;s.st[0].stride=0; // the call's API side effect
        return D3D_OK;
    }
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE p,UINT minIdx,UINT num,UINT count,const void* idx,D3DFORMAT fmt,const void* data,UINT stride)override{
        ++calls;DrawRec d=begin(3,p);const UINT n=NorthlightSpellLayer::vertexCount(p,count);const UINT isz=fmt==D3DFMT_INDEX16?2:4;
        {volatile uint8_t sink=0;for(size_t k=0;k<size_t(minIdx+num)*stride;++k)sink=sink^static_cast<const uint8_t*>(data)[k];(void)sink;} // like DXVK FillUPVertexBuffer: the driver reads [0,(min+num)*stride) of the user pointer
        for(UINT i=0;i<n;++i){
            uint32_t v=0;std::memcpy(&v,static_cast<const uint8_t*>(idx)+size_t(i)*isz,isz);d.idx.push_back(v);
            if(v<minIdx||v>=minIdx+num){d.oob=true;continue;}
            const uint8_t* at=static_cast<const uint8_t*>(data)+size_t(v)*stride;d.vtx.insert(d.vtx.end(),at,at+stride);
        }
        draws.push_back(std::move(d));
        swapRef(s.st[0].vb,static_cast<IDirect3DVertexBuffer9*>(nullptr));s.st[0].off=0;s.st[0].stride=0;swapRef(s.ib,static_cast<IDirect3DIndexBuffer9*>(nullptr));
        return D3D_OK;
    }
};
inline void refreshMirror(FakeDevice& d){
    Mirror& m=d.mir;const State& s=d.s;
    for(unsigned i=0;i<256;++i){m.rsK[i]=!m.hideRs[i];m.rs[i]=m.hideRs[i]?0xBADBAD00u+i:s.rs[i];}
    for(unsigned t=0;t<16;++t){
        for(unsigned k=0;k<16;++k){m.smpK[t][k]=!m.hideSmp[t];m.smp[t][k]=m.hideSmp[t]?0xBADBAD00u+k:s.ss[t][k];}
        m.texK[t]=!m.hideTex[t];m.tex[t]=m.hideTex[t]?reinterpret_cast<IDirect3DBaseTexture9*>(uintptr_t(0x1234)):s.tex[t];
    }
    for(unsigned i=0;i<256;++i){m.vsFK[i]=!m.hideVsF[i];for(int k=0;k<4;++k)m.vsF[i][k]=m.hideVsF[i]?-7.f:s.vsc[i][k];}
    for(unsigned i=0;i<224;++i){m.psFK[i]=!m.hidePsF[i];for(int k=0;k<4;++k)m.psF[i][k]=m.hidePsF[i]?-7.f:s.psc[i][k];}
    for(unsigned i=0;i<16;++i){m.vsIK[i]=!m.hideVsI;m.vsBK[i]=!m.hideVsB;for(int k=0;k<4;++k)m.vsI[i][k]=m.hideVsI?-3:s.vsi[i][k];m.vsB[i]=m.hideVsB?-3:s.vsb[i];}
    m.vsK=m.psK=!m.hideShaders;m.vs=m.hideShaders?nullptr:s.vs;m.ps=m.hideShaders?nullptr:s.ps;
    m.declK=m.fvfK=!m.hideDecl;m.decl=m.hideDecl?nullptr:s.decl;m.fvf=m.hideDecl?0xDEAD:s.fvf;
    m.vpK=!m.hideVp;m.vp=m.hideVp?D3DVIEWPORT9():s.vp;
}
inline bool mirrorEnter(void* c){auto* d=static_cast<FakeDevice*>(c);++d->mir.enters;if(d->mir.failEnter)return false;if(d->mir.depth++)std::abort();refreshMirror(*d);return true;}
inline void mirrorLeave(void* c){auto* d=static_cast<FakeDevice*>(c);++d->mir.leaves;--d->mir.depth;}
inline NorthlightSpellLayer::MirrorView makeView(FakeDevice& d){
    Mirror& m=d.mir;NorthlightSpellLayer::MirrorView v;
    v.rs=m.rs;v.rsKnown=m.rsK;v.smp=m.smp;v.smpKnown=m.smpK;v.tex=m.tex;v.texKnown=m.texK;v.vsF=m.vsF;v.vsFKnown=m.vsFK;v.psF=m.psF;v.psFKnown=m.psFK;
    v.vsI=m.vsI;v.vsIKnown=m.vsIK;v.vsB=m.vsB;v.vsBKnown=m.vsBK;v.vs=&m.vs;v.vsKnown=&m.vsK;v.ps=&m.ps;v.psKnown=&m.psK;v.decl=&m.decl;v.declKnown=&m.declK;
    v.fvf=&m.fvf;v.fvfKnown=&m.fvfK;v.vp=&m.vp;v.vpKnown=&m.vpK;v.enter=&mirrorEnter;v.leave=&mirrorLeave;v.ctx=&d;return v;
}
inline StateBlock::StateBlock(FakeDevice* d):dev(d){s=State();assign(s,d->s);}
inline HRESULT StateBlock::Capture(){
    ++dev->calls;++dev->captures;if(dev->failCapture)return E_FAIL;
    if(dev->failCaptureAfter==0)return E_FAIL;if(dev->failCaptureAfter>0)--dev->failCaptureAfter;
    assign(s,dev->s);return D3D_OK;
}
inline HRESULT StateBlock::Apply(){ // everything except RT/DS, scissor and viewport included
    ++dev->calls;++dev->applies;State n=s;retain(n);drop(dev->s);dev->s=n;return D3D_OK;
}
}
