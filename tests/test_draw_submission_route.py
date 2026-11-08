#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compile actual pre-refactor four draw routes against the current shared route.
Wrappers model state transitions and failures; this proves routing equivalence,
not GPU shader correctness. Capture/hook/spell code is also compared verbatim.
"""
from pathlib import Path
import re, subprocess, sys, tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
root=Path(__file__).resolve().parents[1]
before=(root/'tests/fixtures/draw_submission_before.inc').read_text()
current=(root/'src/proxy/renderer.cpp').read_text()
def member(text, signature):
    start=text.index(signature); brace=text.index('{',start); depth=1; end=brace+1
    while depth:
        depth += (text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]
names=['DrawPrimitive','DrawIndexedPrimitive','DrawPrimitiveUP','DrawIndexedPrimitiveUP']
methods=[]
for name in names:
    sig='HRESULT STDMETHODCALLTYPE '+name+'('
    old=member(before,sig); new=member(current,sig)
    route=lambda text: next(line.strip() for line in text.splitlines() if 'auto draw=' in line)
    assert old.replace(route(old),'ROUTE')==new.replace(route(new),'ROUTE'),name+' capture/hook changed'
    declaration=old[:old.index(' override')].replace('HRESULT STDMETHODCALLTYPE '+name,'HRESULT run')
    methods.append((declaration,route(old),route(new)))
helper=member(current,'template<class Draw> HRESULT submitGameDraw(')
harness=r'''#include <cassert>
#include <vector>
#include <cstdio>
using HRESULT=int;using UINT=unsigned;using INT=int;using D3DPRIMITIVETYPE=int;using D3DFORMAT=int;
struct IDirect3DVertexShader9{};
struct Model {
    std::vector<int> trace; unsigned flags=0; bool enabled=true,applied=false;int bound=0,calls=0;bool captured=false;
    IDirect3DVertexShader9 source;Model* ext=this;Model* autoBump=this;
    void note(int v){trace.push_back(v);}
    HRESULT actual(int form){++calls;note(100+form);note(bound);return flags&64?-7:3;}
    template<class... A> HRESULT DrawPrimitive(A...){return actual(0);}
    template<class... A> HRESULT DrawIndexedPrimitive(A...){return actual(1);}
    template<class... A> HRESULT DrawPrimitiveUP(A...){return actual(2);}
    template<class... A> HRESULT DrawIndexedPrimitiveUP(A...){return actual(3);}
    template<class F> HRESULT hdrDraw(F f){note(1);int save=bound;bound=flags&1?10:0;if(flags&128)applied=true;auto hr=f();bound=save;note(2);return hr;}
    template<class F> HRESULT projectedDraw(UINT n,unsigned phase,F f){note(3);note(n);note(phase);return f();}
    template<class F> HRESULT effectCoverageDraw(F f,bool replay,bool sky,bool* cap){note(4);note(replay);note(sky);int save=bound;if(flags&2)bound+=20;auto hr=f();bound=save;if(hr>=0&&sky)*cap=true;
        if(hr>=0&&(flags&4)&&!(flags&8)){note(5);f();}note(6);return hr;}
    template<class F> HRESULT nativeLocalSubmit(F f,bool prepared=false,IDirect3DVertexShader9* src=nullptr){note(7);note(prepared);note(src==&source);int save=bound;
        if(!(flags&256))bound+=40;auto hr=f();bound=save;note(8);return hr;}
    template<class F,class S> HRESULT geometryDraw(F raw,bool native,S submit){note(9);note(native);
        if(flags&16){int save=bound;bound+=80;auto hr=submit([&]{return actual(form);},flags&512?nullptr:&source);bound=save;return hr;}
        return raw();}
    int form=0;
};
'''
for i,(decl,old,new) in enumerate(methods):
    for label,route in [('Old',old),('New',new)]:
        harness+='struct '+label+str(i)+':Model {\n'+(helper+'\n' if label=='New' else '')+decl+'{bool nativeSky=flags&32,skyCaptured=false;'+route+'auto hr=draw();captured=skyCaptured;return hr;}\n};\n'
harness+='int main(){unsigned cases=0;\n'
args=['1,2,3','1,-2,3,4,5,6','1,3,nullptr,12','1,2,3,4,nullptr,0,nullptr,12']
for i,arg in enumerate(args):
    harness+='for(unsigned flags=0;flags<1024;++flags)for(int mode=0;mode<8;++mode){Old'+str(i)+' a;New'+str(i)+' b;for(Model* p:{static_cast<Model*>(&a),static_cast<Model*>(&b)}){p->flags=flags;p->form='+str(i)+';p->bound=mode&4?160:0;p->enabled=!(mode&1);p->autoBump=mode&2?nullptr:p;}auto x=a.run('+arg+'),y=b.run('+arg+');assert(x==y&&a.trace==b.trace&&a.calls==b.calls&&a.bound==(mode&4?160:0)&&b.bound==a.bound&&a.captured==b.captured);++cases;}\n'
harness+='std::printf("PASS submission routes: %u actual before/current traces; four capture/hook bodies unchanged\\n",cases);}'
with tempfile.TemporaryDirectory(prefix='northlight-submit-') as tmp:
    source=Path(tmp)/'test.cpp';source.write_text(harness);exe=Path(tmp)/'test.exe'
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',str(source),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True)
