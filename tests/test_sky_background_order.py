# northlight-test: requires=zig
"""Run the production sky boundary with native foreground/grade ownership."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('renderer.cpp').read_text()
start = source.index('    void composeSkyBackground(){')
end = source.index('    NorthlightAuroraNative::DiagnosticCapture', start)
method = source[start:end]
effects = source[source.index('    void renderEffects() {'):source.index('    bool nameMaskReady(){')]
assert 'renderSkyBackground(' not in effects
hook = source[source.index('    template<class Capture,class Draw> HRESULT drawHook('):]
hook = hook[:hook.index('    // 0.3.154: blob shadow claim')]
assert hook.count('composeSkyBackground();') == 2
assert hook.index('composeSkyBackground();') < hook.index('spellDraw(') < hook.index('const HRESULT hr=waterMrtDraw(')

fixture = r'''
#include <cassert>
#include <cstdio>
#include "world_draw_domain.h"
struct Texture {};
struct Surface {int color=10;};
struct D3DSURFACE_DESC {unsigned Width=8,Height=8,Format=21;};
struct D3DVIEWPORT9 {float MinZ=0,MaxZ=.94f;};
constexpr unsigned D3DTEXF_NONE=0;
#define FAILED(x) ((x)<0)
namespace NorthlightCelestial {struct Context {};}
struct Device {
    Surface target,copy;Texture texture;
    bool copyOk=true;
    int StretchRect(Surface* from,void*,Surface* to,void*,unsigned){if(!copyOk)return -1;to->color=from->color;return 0;}
};
struct ExtensionDevice {struct RawScope {explicit RawScope(Device&) {}};};
struct SavedState {bool ok=true;Surface* targets[1];SavedState(Device* d,int*):targets{&d->target}{}};
struct Fixture {
    unsigned frame=1,skyBackgroundFrame=~0u,width=8,height=8,debugMode=0,worldDebug=0;
    bool enabled=true,failed=false,applied=false,full=true,back=true,resourceOk=true;
    float worldMinDepth=0,worldMaxDepth=.94f;D3DVIEWPORT9 vp;
    Device device;Device* ext=&device;int stateBlocks=0;
    Surface* sceneSurface=&device.copy;Texture* scene=&texture;Texture texture;
    struct Layers {bool available=true;Texture image;Texture* textureAt(){return available?&image:nullptr;}Texture* texture(unsigned){return textureAt();}} skyLayers;
    struct World {
        struct Environment {bool skyValid=true;struct {float color[8][3]{};} sky;} environment;
        bool context=true;
        const Environment& atmosphereEnvironment(){return environment;}
        bool celestialContext(NorthlightCelestial::Context&,float*,float*){return context;}
    } worldStorage;World* world=&worldStorage;
    struct Celestial {
        unsigned draws=0;
        bool renderSkyBackground(Surface* target,Texture*,Texture* depth,Texture*,unsigned,unsigned,const float*,const float*,const float[8][3]){
            assert(!depth);target->color=20;++draws;return true;
        }
    } celestialStorage;Celestial* celestialDiscs=&celestialStorage;
    bool fullViewport(D3DSURFACE_DESC&,D3DVIEWPORT9* out,bool){*out=vp;return full;}
    bool hdrBackTarget(){return back;}
    bool resources(unsigned,unsigned,unsigned,bool){return resourceOk;}
'''
fixture += method + r'''
};
int main(){
    Fixture f;f.vp={.9990234375f,1};f.composeSkyBackground();assert(!f.celestialStorage.draws);
    f.vp={0,.94f};f.composeSkyBackground();assert(f.device.target.color==20&&f.device.copy.color==10);
    f.device.target.color=70; // Native floating name/alpha foreground follows the sky.
    f.composeSkyBackground();assert(f.device.target.color==70&&f.celestialStorage.draws==1);
    f.device.target.color=35; // Native death/scene grading sees the replaced sky too.
    f.applied=true;f.composeSkyBackground();assert(f.device.target.color==35);
    for(unsigned kind=0;kind<6;++kind){
        Fixture g;
        if(kind==0)g.skyLayers.available=false; // Catalog-proven native death sky.
        if(kind==1)g.worldStorage.context=false;
        if(kind==2)g.worldStorage.environment.skyValid=false;
        if(kind==3)g.resourceOk=false;
        if(kind==4)g.device.copyOk=false;
        if(kind==5)g.back=false;
        g.composeSkyBackground();assert(!g.celestialStorage.draws&&g.device.target.color==10);
        if(kind==1){g.worldStorage.context=true;g.device.target.color=70;g.composeSkyBackground();assert(g.device.target.color==70);}
    }
    ++f.frame;f.applied=false;f.composeSkyBackground();assert(f.celestialStorage.draws==2);
    std::puts("PASS sky precedes names, alpha and native grading; per-frame latch and native fallback");
}
'''
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
cpp = out/'sky-background-order.cpp'
cpp.write_text(fixture, encoding='utf-8', newline='\n')
exe = out/'sky-background-order.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86_64-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                *fp.include_flags(), str(cpp), '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True)
