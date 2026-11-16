#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
#include <algorithm>
#include <array>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#else
using UINT=unsigned;using DWORD=uint32_t;using LONG=long;using HRESULT=int;
constexpr int E_OUTOFMEMORY=-2;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define TRUE 1
#define FALSE 0
#endif
using D3DFORMAT=unsigned;
constexpr HRESULT D3DERR_OUTOFVIDEOMEMORY=-3;
#define D3DVS_VERSION(a,b) (((a)<<8)|(b))
#define D3DDECL_END() {255,0,0,0,0,0}
static constexpr unsigned D3DSTREAMSOURCE_INDEXEDDATA=0x40000000,D3DSTREAMSOURCE_INSTANCEDATA=0x80000000;
enum {D3DCMP_LESSEQUAL,D3DCULL_NONE,D3DDECLMETHOD_DEFAULT,D3DDECLTYPE_FLOAT2,D3DDECLTYPE_FLOAT3,D3DDECLTYPE_FLOAT4,D3DDECLUSAGE_POSITION,D3DDECLUSAGE_TEXCOORD,D3DFILL_SOLID,D3DFMT_A8,D3DFMT_A8R8G8B8,D3DFMT_INDEX16,D3DFMT_INDEX32,D3DLOCK_DISCARD,D3DLOCK_NOOVERWRITE,D3DPOOL_DEFAULT,D3DPOOL_MANAGED,D3DPOOL_SYSTEMMEM,D3DPT_TRIANGLELIST,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_COLORWRITEENABLE,D3DRS_CULLMODE,D3DRS_DEPTHBIAS,D3DRS_FILLMODE,D3DRS_FOGENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SLOPESCALEDEPTHBIAS,D3DRS_SRGBWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ZENABLE,D3DRS_ZFUNC,D3DRS_ZWRITEENABLE,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MAGFILTER,D3DSAMP_MINFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,D3DTEXF_LINEAR,D3DTEXF_NONE,D3DUSAGE_DYNAMIC=512,D3DUSAGE_WRITEONLY=8};
static constexpr unsigned D3DDEVCAPS2_STREAMOFFSET=1;
struct D3DCAPS9 {unsigned VertexShaderVersion=D3DVS_VERSION(3,0),MaxStreams=2,DevCaps2=D3DDEVCAPS2_STREAMOFFSET;};
struct D3DVERTEXELEMENT9 {unsigned short Stream,Offset;unsigned char Type,Method,Usage,UsageIndex;};
#ifndef _WIN32
struct RECT {LONG left,top,right,bottom;};
#endif
struct D3DLOCKED_RECT {int Pitch;void* pBits;};
static const DWORD kStaticCasterVSShader[]={0},kStaticCasterInstancedVSShader[]={0},kStaticCasterPSShader[]={0},kStaticCasterFastPSShader[]={1},kStaticCasterOpaqueFastPSShader[]={2},kStaticCasterOpaquePSShader[]={3};
struct FakeResource {inline static unsigned alive=0;FakeResource(){++alive;}virtual~FakeResource(){--alive;}void Release(){delete this;}};
struct IDirect3DVertexBuffer9:FakeResource {
 struct LockRecord {unsigned offset,bytes,flags;};std::vector<LockRecord> locks;
 std::vector<unsigned char> bytes;bool failLock=false,failUnlock=false;explicit IDirect3DVertexBuffer9(unsigned n):bytes(n){}
 int Lock(unsigned offset,unsigned n,void**p,unsigned flags){if(failLock||offset+n>bytes.size())return -1;locks.push_back({offset,n,flags});*p=bytes.data()+offset;return 0;}int Unlock(){return failUnlock?-1:0;}
};
using IDirect3DIndexBuffer9=IDirect3DVertexBuffer9;
struct IDirect3DTexture9:FakeResource {unsigned width;std::vector<unsigned char> bytes;IDirect3DTexture9(unsigned w,unsigned h):width(w),bytes(w*h*4){}int LockRect(unsigned,D3DLOCKED_RECT*p,const RECT*r,unsigned){p->Pitch=width*4;p->pBits=bytes.data()+r->top*width*4;return 0;}int UnlockRect(unsigned){return 0;}};
struct IDirect3DVertexShader9:FakeResource{};struct IDirect3DPixelShader9:FakeResource{unsigned kind=0;};struct IDirect3DVertexDeclaration9:FakeResource{};
struct IDirect3DDevice9 {
 IDirect3DVertexBuffer9* instanceBuffer=nullptr,*streams[2]={};unsigned streamOffsets[2]={},streamStrides[2]={};
 bool streamOffsetsSupported=true,failLargeInstanceAllocation=false,captureTransforms=false,failInstanceOffset=false;std::vector<std::array<float,12>> drawnTransforms;std::array<float,12> constantTransform{};
 struct DrawRecord {std::array<float,12> transform;unsigned firstIndex,pixel;const IDirect3DVertexBuffer9* vertices=nullptr;const IDirect3DIndexBuffer9* indices=nullptr;unsigned primitives=0;unsigned scissorTest=0;RECT scissor{0,0,0,0};};
 std::vector<DrawRecord> drawnRecords;
 unsigned streamBindings=0,indexBindings=0,samplerBindings=0,textureBindings=0,pixelBindings=0,materialConstants=0;
 unsigned modeCalls=0,failModeAt=0,vertexBindings=0,declarationBindings=0,frequencyBindings=0;
 IDirect3DVertexShader9* currentVertex=nullptr;IDirect3DVertexDeclaration9* currentDeclaration=nullptr;
 IDirect3DIndexBuffer9* lastIndex=nullptr,*currentIndices=nullptr;unsigned scissorTest=0,scissorSets=0;RECT scissor{0,0,0,0};IDirect3DTexture9* lastTexture=nullptr;
 bool failAllocation=false,failInstance=false,failInstancedDraw=false,failOptionalShaders=false;unsigned pixelKind=0,pixelDraws[4]={},maxStreams=2,draws=0,vertexBuffers=0,indexBuffers=0,textures=0,frequencies[2]={1,1};
 int GetDeviceCaps(D3DCAPS9*c){c->MaxStreams=maxStreams;c->DevCaps2=streamOffsetsSupported?D3DDEVCAPS2_STREAMOFFSET:0;return 0;}
 int CreateVertexShader(const DWORD*,IDirect3DVertexShader9**p){*p=new IDirect3DVertexShader9;return 0;}
 int CreatePixelShader(const DWORD* code,IDirect3DPixelShader9**p){if(failOptionalShaders&&code[0])return -1;*p=new IDirect3DPixelShader9;(*p)->kind=code[0];return 0;}
 int CreateVertexDeclaration(const D3DVERTEXELEMENT9*,IDirect3DVertexDeclaration9**p){*p=new IDirect3DVertexDeclaration9;return 0;}
 int CreateVertexBuffer(unsigned n,unsigned usage,unsigned,unsigned pool,IDirect3DVertexBuffer9**p,void*){if(failAllocation||(failLargeInstanceAllocation&&pool==D3DPOOL_DEFAULT&&(usage&D3DUSAGE_DYNAMIC)&&n>256*48))return -1;*p=new IDirect3DVertexBuffer9(n);if(pool==D3DPOOL_DEFAULT&&(usage&D3DUSAGE_DYNAMIC))instanceBuffer=*p;++vertexBuffers;return 0;}
 int CreateIndexBuffer(unsigned n,unsigned,unsigned,unsigned,IDirect3DIndexBuffer9**p,void*){if(failAllocation)return -1;*p=new IDirect3DIndexBuffer9(n);lastIndex=*p;++indexBuffers;return 0;}
 int CreateTexture(unsigned w,unsigned h,unsigned,unsigned,unsigned format,unsigned pool,IDirect3DTexture9**p,void*){if(failAllocation||format==D3DFMT_A8)return -1;*p=new IDirect3DTexture9(w,h);if(pool!=D3DPOOL_SYSTEMMEM){lastTexture=*p;++textures;}return 0;}
 int UpdateTexture(IDirect3DTexture9* from,IDirect3DTexture9* to){to->bytes=from->bytes;return 0;}
 int SetStreamSourceFreq(unsigned s,unsigned f){++frequencyBindings;if(++modeCalls==failModeAt||(failInstance&&f!=1))return -1;if(s<2)frequencies[s]=f;return 0;}
 int SetStreamSource(unsigned s,IDirect3DVertexBuffer9* buffer,unsigned offset,unsigned stride){++streamBindings;if(++modeCalls==failModeAt||(s==1&&offset&&(!streamOffsetsSupported||failInstanceOffset)))return -1;if(s<2){streams[s]=buffer;streamOffsets[s]=offset;streamStrides[s]=stride;}return 0;}int SetIndices(IDirect3DIndexBuffer9* buffer){++indexBindings;currentIndices=buffer;return 0;}
 int SetVertexShader(IDirect3DVertexShader9* shader){++vertexBindings;if(++modeCalls==failModeAt)return -1;currentVertex=shader;return 0;}int SetPixelShader(IDirect3DPixelShader9* shader){++pixelBindings;pixelKind=shader?shader->kind:0;return 0;}
 int SetVertexDeclaration(IDirect3DVertexDeclaration9* declaration){++declarationBindings;if(++modeCalls==failModeAt)return -1;currentDeclaration=declaration;return 0;}int SetVertexShaderConstantF(unsigned first,const float* values,unsigned count){if(first==4&&count==3)std::memcpy(constantTransform.data(),values,48);return 0;}int SetPixelShaderConstantF(unsigned,const float*,unsigned){++materialConstants;return 0;}
 int SetScissorRect(const RECT* r){scissor=*r;++scissorSets;return 0;}
 int SetRenderState(unsigned state,unsigned value){if(state==D3DRS_SCISSORTESTENABLE)scissorTest=value;return 0;}int SetSamplerState(unsigned,unsigned,unsigned){++samplerBindings;return 0;}int SetTexture(unsigned,IDirect3DTexture9*){++textureBindings;return 0;}
 int DrawIndexedPrimitive(unsigned,int,unsigned,unsigned,unsigned firstIndex,unsigned primitives){if(failInstancedDraw&&frequencies[0]!=1)return -1;
 if(captureTransforms){if(frequencies[0]&D3DSTREAMSOURCE_INDEXEDDATA){const unsigned count=frequencies[0]&~D3DSTREAMSOURCE_INDEXEDDATA;for(unsigned i=0;i<count;++i){std::array<float,12> t;const size_t offset=streamOffsets[1]+i*streamStrides[1];if(!streams[1]||offset+48>streams[1]->bytes.size())return -1;std::memcpy(t.data(),streams[1]->bytes.data()+offset,48);drawnTransforms.push_back(t);drawnRecords.push_back({t,firstIndex,pixelKind,streams[0],currentIndices,primitives,scissorTest,scissor});}}else {drawnTransforms.push_back(constantTransform);drawnRecords.push_back({constantTransform,firstIndex,pixelKind,streams[0],currentIndices,primitives,scissorTest,scissor});}}
 ++draws;++pixelDraws[pixelKind];return 0;}
};
