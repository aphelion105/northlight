#include "shadow_replay_submit.h"
#include "forwarders.h"
#include <cassert>
#include <cstdio>
#include <string_view>
#include <vector>

namespace {
enum Command {Layout,Stream,Indices,PackedIndices,Vertex,Floats,Bools,Ints,Texture,U,V,Cutoff,Alpha,Indexed,Unindexed};

struct Device final: ForwardIDirect3DDevice9 {
    std::vector<Command> commands;
    IDirect3DIndexBuffer9* packedIndex=reinterpret_cast<IDirect3DIndexBuffer9*>(2);
    Command fault=Alpha;
    UINT drawStart=0,drawCount=0;
    float projectionValue=0;
    Device():ForwardIDirect3DDevice9(nullptr){}
    HRESULT call(Command command){
        commands.push_back(command);
        return command==fault?E_OUTOFMEMORY:D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9*) override{return call(Layout);}
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT,IDirect3DVertexBuffer9*,UINT,UINT) override{return call(Stream);}
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* indices) override{return call(indices==packedIndex?PackedIndices:Indices);}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9*) override{return call(Vertex);}
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT first,const float* values,UINT count) override{
        if(first<=2&&first+count>2)projectionValue=values[(2-first)*4];
        return call(Floats);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT first,const BOOL* values,UINT count) override{
        assert(first==1&&count==1&&values[0]==TRUE);
        return call(Bools);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT first,const int* values,UINT count) override{
        assert(first==2&&count==1&&values[0]==33);
        return call(Ints);
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD slot,IDirect3DBaseTexture9*) override{assert(slot==0);return call(Texture);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD slot,D3DSAMPLERSTATETYPE kind,DWORD) override{
        assert(slot==0&&(kind==D3DSAMP_ADDRESSU||kind==D3DSAMP_ADDRESSV));
        return call(kind==D3DSAMP_ADDRESSU?U:V);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT first,const float* values,UINT count) override{
        assert(first==0&&count==1&&values[3]==-1.f);
        return call(Cutoff);
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT start,UINT count) override{
        assert(type==D3DPT_TRIANGLELIST&&base==11&&min==3&&vertices==20);
        drawStart=start;drawCount=count;
        return call(Indexed);
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type,UINT start,UINT count) override{
        assert(type==D3DPT_TRIANGLELIST);
        drawStart=start;drawCount=count;
        return call(Unindexed);
    }
};

struct Replay {
    IDirect3DVertexDeclaration9* decl=nullptr;
    IDirect3DVertexShader9* shader=nullptr;
    IDirect3DVertexBuffer9* stream[4]={};
    UINT offset[4]={},stride[4]={};
    IDirect3DIndexBuffer9* index=reinterpret_cast<IDirect3DIndexBuffer9*>(1);
    IDirect3DBaseTexture9* texture=nullptr;
    DWORD addressU=D3DTADDRESS_CLAMP,addressV=D3DTADDRESS_WRAP;
    float cutoff=-1;
    unsigned alphaMode=0,constantGroup=7,projectionKind=2,directionalPaletteEnd=0;
    bool indexed=true;
    D3DPRIMITIVETYPE type=D3DPT_TRIANGLELIST;
    INT base=11;
    UINT start=123,count=5,min=3,vertices=20;
    float constants[32]={};
    BOOL bools[2]={FALSE,TRUE};
    int ints[12]={};
    NorthlightShaderConstants::Usage constantUsage;
    Replay(){
        constantUsage.floats={0,8};
        constantUsage.booleans={1,1};
        constantUsage.integers={2,1};
        ints[8]=33;
    }
};

const std::vector<Command> geometry{Layout,Stream,Stream,Stream,Stream,Indices,Vertex};
const std::vector<Command> material{Texture,U,V,Cutoff};

void expect(const Device& device,std::initializer_list<std::vector<Command>> parts){
    std::vector<Command> commands;
    for(const auto& part:parts)commands.insert(commands.end(),part.begin(),part.end());
    assert(device.commands==commands);
}
}

int main(){
    namespace S=NorthlightShadowReplay;
    namespace R=NorthlightRenderThreadProbe;
    Device device;
    Replay packet;
    float matrix[16]={4.f};
    size_t bytes=0,calls=0;
    std::string_view failure;
    auto check=[&](HRESULT result,const char* stage){if(SUCCEEDED(result))return true;failure=stage;return false;};
    auto alpha=[&](auto draw){device.commands.push_back(Alpha);return draw();};
    const auto submit=[&](auto& pass,auto& split,const auto& alphaDraw,const S::Range* range=nullptr,S::IndexOverride* indices=nullptr){
        return pass.submit(packet,matrix,matrix,split,bytes,calls,check,alphaDraw,range,indices,{});
    };
    R::Off full;
    S::Pass pass(&device);
    assert(submit(pass,full,alpha));
    expect(device,{geometry,{Floats,Bools,Ints},material,{Alpha,Indexed}});
    assert(bytes==148&&calls==3&&device.projectionValue==4&&device.drawStart==123&&device.drawCount==5);

    device.commands.clear();
    assert(submit(pass,full,alpha));
    expect(device,{{Alpha,Indexed}});
    assert(calls==3&&pass.posesPrepared()==1&&pass.posesReused()==1);

    pass.beginFace(true);
    matrix[0]=5;
    device.commands.clear();
    const S::Range run{400,8,3,20};
    assert(submit(pass,full,alpha,&run));
    expect(device,{{Floats},material,{Indexed}});
    assert(calls==4&&device.projectionValue==5&&device.drawStart==400&&device.drawCount==8);

    pass.afterPalette();
    device.commands.clear();
    assert(submit(pass,full,alpha,&run));
    expect(device,{geometry,{Floats,Bools,Ints},material,{Indexed}});
    assert(calls==7);

    device.commands.clear();
    S::IndexOverride packed{device.packedIndex,{0,19,3,20},2};
    assert(submit(pass,full,alpha,&run,&packed));
    expect(device,{{PackedIndices,Indexed}});
    assert(packed.buffer&&device.drawStart==0&&device.drawCount==19);

    device.commands.clear();
    pass.invalidateBindings();
    device.fault=PackedIndices;
    assert(submit(pass,full,alpha,&run,&packed));
    expect(device,{{Layout,Stream,Stream,Stream,Stream,PackedIndices},geometry,material,{Indexed}});
    assert(!packed.buffer&&device.drawStart==400&&device.drawCount==8);
    device.fault=Alpha;

    device.commands.clear();
    S::Pass diagnostics(&device);
    R::LogicOnly logic;
    R::StateOnly state;
    assert(submit(diagnostics,logic,alpha));
    expect(device,{});
    S::Pass statePass(&device);
    assert(submit(statePass,state,alpha));
    expect(device,{geometry,{Floats,Bools,Ints},material});

    device.commands.clear();
    device.fault=Bools;
    S::Pass failed(&device);
    const size_t before=bytes;
    assert(!submit(failed,full,alpha)&&failure=="replay bool constants"&&bytes==before+128);
    expect(device,{geometry,{Floats,Bools}});

    device.commands.clear();
    device.fault=Alpha;
    packet.indexed=false;
    S::Pass afterReset(&device);
    assert(submit(afterReset,full,alpha));
    expect(device,{geometry,{Floats,Bools,Ints},material,{Alpha,Unindexed}});
    const auto deferred=[](auto){return E_PENDING;};
    device.commands.clear();
    failure={};
    assert(!submit(afterReset,full,deferred)&&failure.empty()&&device.commands.empty());
    std::puts("SHADOW_REPLAY_SUBMIT_PASS order=exact pose_cache=1 point_face=1 palette_reset=1 packed_index_fallback=1 reset_rebind=1 alpha_deferral=1 probe_modes=1");
}
