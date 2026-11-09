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

source = fp.src('renderer.cpp').read_text()
start = source.index('template<class Draw> void captureModelNormals(')
end = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
method = source[start:end]
fixture = r'''
#include <cassert>
#include <map>
using IDirect3DVertexShader9=int;
using D3DFORMAT=int;
namespace NorthlightAutoDisplacement {
struct Args {int draw=0,indices=0,format=0,vertices=0,stride=0;};
struct CaptureSource {int mesh=0,pose=0;void* declaration=nullptr;};
}
struct Normals {unsigned captures=0;template<class... A> void capture(A&&...){++captures;}};
struct Materials {bool enabled=false;bool active()const{return enabled;}};
struct Hook {
    Normals normals;Normals* modelNormals=&normals;
    Materials materials;Materials* autoBump=nullptr;
    bool enabled=true,applied=false,failed=false;
    std::map<IDirect3DVertexShader9*,unsigned> vsHashes;
    unsigned m2ShaderFamily(unsigned hash){return hash;}
    @METHOD@
};
int main(){
    int shader=0;NorthlightAutoDisplacement::Args geometry;
    for(unsigned mode=0;mode<3;++mode){
        Hook h;h.vsHashes[&shader]=3;
        if(mode)h.autoBump=&h.materials;h.materials.enabled=mode==2;
        h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        assert(h.normals.captures==1);
        h.enabled=false;h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        h.enabled=true;h.applied=true;h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        h.applied=false;h.failed=true;h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        h.failed=false;h.vsHashes[&shader]=1;h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        h.vsHashes.clear();h.captureModelNormals(&shader,[]{return 0;},false,geometry);
        assert(h.normals.captures==1);
    }
}
'''.replace('@METHOD@', method)
with tempfile.TemporaryDirectory(prefix='northlight-normal-consumers-') as tmp:
    path = Path(tmp) / 'consumers.cpp'
    path.write_text(fixture)
    exe = path.with_suffix('.exe')
    compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name == 'nt' else ['clang++']
    subprocess.run([*compiler, '-std=c++17', '-O2', '-UNDEBUG', str(path), '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
print('MODEL_NORMAL_CONSUMERS_PASS materials=absent/off/on lighting-receivers=retained')
