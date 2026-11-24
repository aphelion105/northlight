#!/usr/bin/env python3
# northlight-test: requires=zig
"""Model lighting receivers remain available with every material effect disabled."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def method(name, prefix):
    source = fp.src(name).read_text()
    start = source.index(prefix)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
fixture = r'''
#include <cassert>
#include "m2_draw_section.h"
#include <map>
#include <tuple>
#include <vector>
using IDirect3DVertexShader9=int;
using D3DFORMAT=int;
namespace NorthlightAutoDisplacement {
struct Args {int draw=0,indices=0,format=0,vertices=0,stride=0;};
struct CaptureSource {int mesh=0,pose=0,bank=0;void* declaration=nullptr;};
}
struct Normals {
    unsigned captures=0;const int* bank=nullptr;
    template<class... A> void capture(A&&... args){++captures;bank=std::get<sizeof...(A)-1>(std::forward_as_tuple(args...));}
};
struct Materials {bool enabled=false;bool active()const{return enabled;}};
struct Hook {
    struct CpuScope {explicit CpuScope(unsigned*){}};
    unsigned cpuNormalCapture=0;
    bool sampledDrawTimers()const{return false;}
    struct World {bool ground=false;bool terrainShader(IDirect3DVertexShader9*)const{return ground;}} scene;
    World* world=&scene;
    Normals normals;Normals* modelNormals=&normals;
    Materials materials;Materials* autoBump=nullptr;
    bool modelNormalsNeeded=true,enabled=true,applied=false,failed=false;
    std::map<IDirect3DVertexShader9*,unsigned> vsHashes;
    struct ShaderClass {bool terrain;unsigned family;};
    ShaderClass classifyVs(IDirect3DVertexShader9* vs){
        const auto tag=vsHashes.find(vs);
        return {world->terrainShader(vs),tag==vsHashes.end()?0u:tag->second};
    }
    @METHOD@
};
using HRESULT=int;using DWORD=unsigned;using IDirect3DSurface9=int;using D3DRENDERSTATETYPE=unsigned;
#define FAILED(hr) ((hr)<0)
constexpr unsigned D3DRS_ZENABLE=1,D3DRS_ZWRITEENABLE=2;
struct Backend {
    unsigned reads=0;
    HRESULT GetRenderState(unsigned,DWORD* v){++reads;*v=1;return 0;}
    HRESULT GetDepthStencilSurface(IDirect3DSurface9** v){++reads;static int depth;*v=&depth;return 0;}
};
struct Native {
    bool enabled=true,capable=true,refused=false,complete=true,lastTerrain=false;unsigned calls=0;
    @ACCEPTING@
    void cancel(const char*){complete=false;}
    bool usesDepth(IDirect3DSurface9*)const{return true;}
    struct Result {HRESULT hr;bool probed,written;};
    template<class Draw,class Prove> Result draw(Draw submit,IDirect3DSurface9*,IDirect3DSurface9*,unsigned,unsigned,void*,bool,Prove,bool terrain=false){++calls;lastTerrain=terrain;return {submit(),true,true};}
};
struct NativeHook {
    struct Vertex {void* source=nullptr;bool terrain=false;} vertex;
    struct Packet {Vertex* vertex;bool nativeProbed=false,nativeWritten=false;} packet{&vertex};
    Backend backend;Backend* d=&backend;Native nativeMrt;
    void* rasterContext=nullptr;bool(*rasterReader)(void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*)=nullptr;
    bool bad=false;std::vector<Packet*> pending{&packet};
    int storage=0;IDirect3DSurface9 *surface=&storage,*frameTarget=&storage;
    unsigned width=1,height=1;Packet* nativeCandidate=&packet;
    static void release(IDirect3DSurface9*& p){p=nullptr;}
    @NATIVE_DRAW@
};
int main(){
    int shader=0;NorthlightAutoDisplacement::Args geometry;
    {
        Hook h;h.vsHashes[&shader]=3;NorthlightAutoDisplacement::CaptureSource source;
        h.captureModelNormals(&shader,[]{return 0;},false,geometry,&source);assert(h.normals.bank==&source.bank);
        h.captureModelNormals(&shader,[]{return 0;},true,geometry,&source);assert(!h.normals.bank);
    }
    for(unsigned mode=0;mode<3;++mode){
        Hook h;h.vsHashes[&shader]=3;
        if(mode)h.autoBump=&h.materials;h.materials.enabled=mode==2;
        h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        assert(h.normals.captures==1);
        h.vsHashes.clear();h.scene.ground=true;h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        assert(h.normals.captures==2);h.scene.ground=false;h.vsHashes[&shader]=3;
        h.modelNormalsNeeded=false;h.captureModelNormals(&shader,[]{return 0;},false,geometry);h.modelNormalsNeeded=true;
        h.enabled=false;h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        h.enabled=true;h.applied=true;h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        h.applied=false;h.failed=true;h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        h.failed=false;h.vsHashes[&shader]=1;h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        h.vsHashes.clear();h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        assert(h.normals.captures==2);
    }
    for(unsigned mode=0;mode<4;++mode)for(bool domain:{false,true}){
        NativeHook h;
        if(mode==0)h.nativeMrt.enabled=false;
        if(mode==1)h.nativeMrt.capable=false;
        if(mode==2)h.nativeMrt.refused=true;
        if(mode==3)h.nativeMrt.complete=false;
        unsigned submits=0;
        assert(h.nativeDraw([&]{++submits;return -7;},domain,0)==-7);
        assert(submits==1&&!h.backend.reads&&!h.nativeMrt.calls);
    }
    NativeHook h;unsigned submits=0;
    auto draw=[&]{++submits;return 0;};
    assert(h.nativeDraw(draw,false,0)==0&&h.backend.reads==3&&h.nativeMrt.calls==1);
    h.nativeMrt.cancel("late unsupported writer");
    for(unsigned i=0;i<1000;++i)assert(h.nativeDraw(draw,i%2,0)==0);
    assert(submits==1001&&h.backend.reads==3&&h.nativeMrt.calls==1);
    h.nativeMrt.complete=true;
    assert(h.nativeDraw(draw,true,0)==0&&submits==1002&&h.nativeMrt.calls==2);
    assert(h.packet.nativeProbed&&h.packet.nativeWritten&&!h.nativeMrt.lastTerrain);
    h.vertex.terrain=true;
    assert(h.nativeDraw(draw,true,0)==0&&submits==1003&&h.nativeMrt.calls==3&&h.nativeMrt.lastTerrain);
}
'''.replace('@METHOD@', method('renderer.cpp', 'template<class Draw> void captureModelNormals('))
fixture = fixture.replace('@ACCEPTING@', method('model_normal_native.h', 'bool accepting()const'))
fixture = fixture.replace('@NATIVE_DRAW@', method('model_normal_renderer.h', 'template<class Draw,class Prove> HRESULT nativeDraw('))
with tempfile.TemporaryDirectory(prefix='northlight-normal-consumers-') as tmp:
    path = Path(tmp) / 'consumers.cpp'
    path.write_text(fixture)
    exe = path.with_suffix('.exe')
    compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name == 'nt' else ['clang++']
    subprocess.run([*compiler, '-std=c++17', '-O2', '-UNDEBUG', *fp.test_include_flags(), str(path), '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
print('MODEL_NORMAL_CONSUMERS_PASS materials=absent/off/on lighting-receivers=retained')
print('MODEL_NORMAL_CANCEL_PASS disabled/unsupported/refused/cancelled zero-state-reads single-submit recovery')
