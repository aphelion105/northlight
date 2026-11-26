#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exercise the four production draw adapters and shared route with modeled D3D state."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

before=(fp.TESTS/'fixtures/draw_submission_before.inc').read_text()
current=fp.src('renderer.cpp').read_text()


def member(text, signature):
    start=text.index(signature)
    brace=text.index('{',start)
    depth,end=1,brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}')
        end+=1
    return text[start:end]


harness=r'''#include <cassert>
#include <cstdio>
#include <optional>
#include <vector>
using HRESULT=int;using UINT=unsigned;using INT=int;using D3DPRIMITIVETYPE=int;using D3DFORMAT=int;
constexpr int D3DFMT_UNKNOWN=0;
constexpr int D3DPT_TRIANGLELIST=1;
inline bool SUCCEEDED(HRESULT hr){return hr>=0;}
struct IDirect3DVertexShader9{};
namespace NorthlightDrawSnapshot {
struct Draw{D3DPRIMITIVETYPE type;INT base;UINT minimum,vertices,start,primitives;bool indexed;};
}
namespace NorthlightAutoDisplacement {
struct Args{NorthlightDrawSnapshot::Draw draw;const void* indices=nullptr;D3DFORMAT format=0;const void* vertices=nullptr;UINT stride=0;};
}
namespace NorthlightWaterRenderer {enum UserPointer{NoUserPointer,UserVertices,UserVerticesAndIndices};}
namespace NorthlightNativeMoreLightShader {enum class Kind{None,Model};}
namespace NorthlightSpellLayer {
struct DrawArgs{
    template<class... T> static int primitive(T...){return 1;}
    template<class... T> static int indexed(T...){return 2;}
    template<class... T> static int primitiveUP(T...){return 3;}
    template<class... T> static int indexedUP(T...){return 4;}
};
}
enum Stage{Scene,Aurora,Planet,Material,Canopy,Paired};
struct Model {
    std::vector<int> trace;std::vector<Stage> stages;
    unsigned flags=0,frame=1,worldDebug=0,debugMode=0,captures=0;
    bool enabled=true,applied=false,failed=false,skyCaptured=false,restoreFailed=false;
    int bound=0,calls=0,form=0,spellArgs=0;
    IDirect3DVertexShader9 source;Model *ext=this,*autoBump=this,*world=this,*nativeMoreLights=nullptr,*gameParticlePair=this;
    bool indexedPairGate=false,particlePairGate=false,auroraDrawProven=false;
    struct {bool hybridRequested()const{return false;}} normals;
    decltype(normals)* modelNormals=&normals;
    struct HybridNormalDraw {
        struct {IDirect3DVertexShader9* p=nullptr;} shader;
        int handoff=0;
        bool submitted=false,written=false;
    };
    HybridNormalDraw* hybridNormalDraw=nullptr;
    int cpuCapture=0,cpuPrep=0;
    struct CpuScope{explicit CpuScope(int*){}};
    bool sampledDrawTimers()const{return false;}
    template<class Work> void extensionWork(const char*,Work work){work();}
    template<class... Args> void captureModelNormals(Args&&...){assert(false);}
    struct {int gate=0;IDirect3DVertexShader9* vertexShader=nullptr;} mirrorState;
    struct Guard{explicit Guard(int){}};
    struct {bool active=true;} hdr;
    struct {bool ready()const{return false;}} skyPlanet;
    struct {bool left=false;} uiDrawDiagnostic;
    struct {void* nativePixel()const{return nullptr;}} effectCoverage;
    bool diagnostics()const{return false;}
    bool atlasEnabled()const{return true;}
    bool hdrSkyHasEmission()const{return false;}
    bool needsDiagnosticFallback()const{return false;}
    bool nativePOM()const{return false;}
    bool nativeDisplacement()const{return false;}
    bool ownsNativeExtra()const{return false;}
    bool peekVertexShader(IDirect3DVertexShader9*& out){out=&source;return true;}
    auto kind(IDirect3DVertexShader9*){return NorthlightNativeMoreLightShader::Kind::Model;}
    void requestNativeExtra(bool){}
    void nativeExtraRgb(bool,float*){}
    template<class... T> bool keepsModelState(T...){return true;}
    template<class K,class M,class H,class F> HRESULT draw(K,M,H,F f,bool){return f();}
    void beginIndexedPairDraw(){}
    template<class F> HRESULT submitIndexedPair(const NorthlightDrawSnapshot::Draw& geometry,F f){
        assert(geometry.indexed&&geometry.base==-2&&geometry.start==5);
        return f();
    }
    template<class F> HRESULT captureNativeSceneDraw(unsigned f,UINT n,F draw){assert(f==frame&&n==3+(form==1?3:form==3?1:0));stages.push_back(Scene);return draw();}
    template<NorthlightWaterRenderer::UserPointer P,class R,class F>
    void captureGameDraw(IDirect3DVertexShader9*,const NorthlightAutoDisplacement::Args& geometry,R,F){
        assert(unsigned(P)==(form<2?0u:unsigned(form-1)));
        assert(geometry.draw.indexed==(form%2==1));assert(geometry.draw.type==1);
        assert(geometry.stride==(form>=2?12u:0u));++captures;
    }
    template<class C,class F> HRESULT drawHook(D3DPRIMITIVETYPE t,UINT n,C capture,F draw,bool* nativeSky,bool* captured,const NorthlightAutoDisplacement::Args* geometry){
        assert(t==1&&n==geometry->draw.primitives);
        *nativeSky=flags&32;if(enabled)capture(&source);
        const auto hr=draw();skyCaptured=*captured;return hr;
    }
    void note(int v){trace.push_back(v);}
    HRESULT actual(int f){assert(f==form);++calls;note(100+form);note(bound);return flags&64?-7:3;}
    HRESULT DrawPrimitive(int t,UINT start,UINT n){assert(t==1&&start==2&&n==3);return actual(0);}
    HRESULT DrawIndexedPrimitive(int t,INT base,UINT min,UINT vs,UINT start,UINT n){assert(t==1&&base==-2&&min==3&&vs==4&&start==5&&n==6);return actual(1);}
    HRESULT DrawPrimitiveUP(int t,UINT n,const void* data,UINT stride){assert(t==1&&n==3&&data==nullptr&&stride==12);return actual(2);}
    HRESULT DrawIndexedPrimitiveUP(int t,UINT min,UINT vs,UINT n,const void* indices,int fmt,const void* data,UINT stride){assert(t==1&&min==2&&vs==3&&n==4&&indices==nullptr&&fmt==0&&data==nullptr&&stride==12);return actual(3);}
    template<class F> HRESULT hdrDraw(F f){note(1);int save=bound;bound=flags&1?10:0;if(flags&128)applied=true;auto hr=f();bound=save;note(2);return hr;}
    template<class F> HRESULT projectedDraw(UINT n,unsigned phase,F f){note(3);note(n);note(phase);return f();}
    template<class F> HRESULT effectCoverageDraw(F f,bool replay,bool sky,bool* cap,const NorthlightDrawSnapshot::Draw* ordered=nullptr){
        note(4);note(replay);note(sky);if(ordered){assert(form==1&&!sky);stages.push_back(Paired);}
        int save=bound;if(flags&2)bound+=20;auto hr=f();bound=save;if(hr>=0&&sky)*cap=true;
        if(hr>=0&&(flags&4)&&!(flags&8)){note(5);f();}note(6);return hr;
    }
    template<class F> HRESULT nativeLocalSubmit(F f,bool prepared=false,IDirect3DVertexShader9* src=nullptr){
        note(7);note(prepared);note(src==&source);int save=bound;if(!(flags&256))bound+=40;auto hr=f();bound=save;note(8);return hr;
    }
    template<class F> HRESULT auroraNativeDraw(F f){stages.push_back(Aurora);return f();}
    template<class F> HRESULT skyPlanetNativeDraw(F f){stages.push_back(Planet);return f();}
    template<class F> HRESULT submitNativeMaterial(F f,void*){stages.push_back(Material);return f();}
    template<class F> HRESULT submitCanopy(F f){stages.push_back(Canopy);return f();}
    template<class F,class S> HRESULT geometryDraw(F raw,bool native,S submit){
        note(9);note(native);if(!(flags&16))return raw();
        int save=bound;bound+=80;
        auto hr=submit([&]{return actual(form);},flags&512?nullptr:&source);
        bound=save;return hr;
    }
};
'''
names=['DrawPrimitive','DrawIndexedPrimitive','DrawPrimitiveUP','DrawIndexedPrimitiveUP']
args=['1,2,3','1,-2,3,4,5,6','1,3,nullptr,12','1,2,3,4,nullptr,0,nullptr,12']
helper=member(current,'template<class Draw> HRESULT submitGameDraw(')
for i,name in enumerate(names):
    signature='HRESULT STDMETHODCALLTYPE '+name+'('
    older=member(before,signature)
    now=member(current,signature)
    decl=older[:older.index(' override')].replace(signature,'HRESULT run(')
    route=next(line.strip() for line in older.splitlines() if 'auto draw=' in line)
    adapter=now.replace(signature,'HRESULT run(',1).replace(' override {',' {',1)
    harness+=f'struct Old{i}:Model {{\n{decl}{{bool nativeSky=flags&32,skyCaptured=false;{route}auto hr=draw();this->skyCaptured=skyCaptured;return hr;}}\n}};\n'
    harness+=f'struct Current{i}:Model {{\n{helper}\n{adapter}\n}};\n'
harness+=r'''
template<class Old,class Current,class... Args> unsigned compare(int form,Args... args){
    unsigned cases=0;
    for(unsigned flags=0;flags<1024;++flags)for(int mode=0;mode<16;++mode){
        Old a;Current b;
        for(Model* p:{static_cast<Model*>(&a),static_cast<Model*>(&b)}){
            p->flags=flags;p->form=form;p->bound=mode&4?160:0;p->enabled=!(mode&1);
            p->autoBump=mode&2?nullptr:p;p->particlePairGate=mode&8;
        }
        const auto x=a.run(args...),y=b.run(args...);
        assert(x==y&&a.trace==b.trace&&a.calls==b.calls&&a.bound==(mode&4?160:0)&&b.bound==a.bound&&a.skyCaptured==b.skyCaptured);
        assert(b.captures==unsigned(b.enabled)&&b.spellArgs==(b.enabled?form+1:0));
        std::vector<Stage> expected;
        if(b.enabled&&!b.applied)expected.push_back(Scene);
        if(!(b.enabled&&b.autoBump&&(flags&16))){
            for(int call=0;call<b.calls;++call){
                if(call==0&&form==1&&(mode&8)&&!(flags&32))expected.push_back(Paired);
                expected.push_back(Aurora);expected.push_back(Planet);
                if(b.autoBump)expected.push_back(Material);
                expected.push_back(Canopy);
            }
        }
        assert(b.stages==expected);
        ++cases;
    }
    Current bypass;bypass.form=form;bypass.enabled=false;bypass.hdr.active=false;
    assert(bypass.run(args...)==3&&bypass.calls==1&&bypass.captures==0);
    assert((bypass.stages==std::vector<Stage>{Material,Canopy}));
    return cases+1;
}
int main(){unsigned cases=0;
'''
for i,arguments in enumerate(args):
    harness+=f'cases+=compare<Old{i},Current{i}>({i},{arguments});\n'
harness+='std::printf("PASS submission routes: %u cases, four actual draw adapters\\n",cases);}\n'
out=fp.output_dir()
source=out/'submission.cpp'
source.write_text(harness,encoding='utf-8')
exe=out/'submission.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',str(source),'-o',str(exe)],env=fp.zig_env(),check=True)
subprocess.run([str(exe)],check=True)
