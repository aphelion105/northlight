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
world_source = fp.src('world_renderer.h').read_text()
start = world_source.index('    bool skyBackgroundContext(')
end = world_source.index('    bool celestialContext(', start)
camera_method = world_source[start:end]
effects = source[source.index('    void renderEffects() {'):source.index('    bool nameMaskReady(){')]
assert 'renderSkyBackground(' not in effects
hook = source[source.index('    template<class Capture,class Draw> HRESULT drawHook('):]
hook = hook[:hook.index('    // 0.3.154: blob shadow claim')]
assert hook.count('composeSkyBackground();') == 2
assert hook.index('composeSkyBackground();') < hook.index('spellDraw(') < hook.index('const HRESULT hr=waterMrtDraw(')

fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "world_draw_domain.h"
struct Texture {};
struct Surface {int color=10;};
struct D3DSURFACE_DESC {unsigned Width=8,Height=8,Format=21;};
struct D3DVIEWPORT9 {float MinZ=0,MaxZ=.94f;};
constexpr unsigned D3DTEXF_NONE=0;
#define FAILED(x) ((x)<0)
namespace NorthlightCelestial {struct Context {};}
namespace NorthlightWorldCamera {
struct Camera {float inverseView[16]={7};};
bool available=true;unsigned reads=0;std::string mapName="Northrend";Camera current;
bool readSkyPhase(char (&map)[64],float sign,Camera& out){
    ++reads;assert(sign==-1);if(!available)return false;
    std::strcpy(map,mapName.c_str());out=current;return true;
}
}
template<class... Args> void logf(const char*,Args...){}
struct World {
    struct Environment {bool skyValid=true;struct {float color[8][3]{};} sky;} skyEnvironment;
    bool valid=true,pipelineActive=true,failed=false;unsigned fault=0;
    struct {float inverseView[16]={1};} context;
    float projection[3]={2,3,-1};struct {std::string map="Northrend";} lastRequest;
    bool hasContext()const{return valid&&!failed&&!workerFault();}
    unsigned workerFault()const{return fault;}
    const Environment& atmosphereEnvironment()const{return skyEnvironment;}
    // PRODUCTION_CAMERA_METHOD
};
struct Device {
    Surface target,copy;Texture texture;
    bool copyOk=true;
    int StretchRect(Surface* from,void*,Surface* to,void*,unsigned){if(!copyOk)return -1;to->color=from->color;return 0;}
};
struct ExtensionDevice {struct RawScope {explicit RawScope(Device&) {}};};
struct SavedState {bool ok=true;Surface* targets[1];SavedState(Device* d,int*):targets{&d->target}{}};
struct Fixture {
    unsigned frame=1,skyBackgroundFrame=~0u,width=8,height=8,debugMode=0,worldDebug=0,drawCalls=0;
    bool enabled=true,failed=false,applied=false,full=true,back=true,resourceOk=true;
    float worldMinDepth=0,worldMaxDepth=.94f;D3DVIEWPORT9 vp;
    Device device;Device* ext=&device;int stateBlocks=0;
    Surface* sceneSurface=&device.copy;Texture* scene=&texture;Texture texture;
    struct Layers {bool available=true;Texture image;Texture* textureAt(){return available?&image:nullptr;}Texture* texture(unsigned){return textureAt();}} skyLayers;
    World worldStorage;World* world=&worldStorage;
    struct Celestial {
        unsigned draws=0;
        bool renderSkyBackground(Surface* target,Texture*,Texture* depth,Texture*,unsigned,unsigned,const float*,const float*,const float[8][3]){
            assert(!depth);target->color=20;++draws;return true;
        }
    } celestialStorage;Celestial* celestialDiscs=&celestialStorage;
    bool fullViewport(D3DSURFACE_DESC&,D3DVIEWPORT9* out,bool){*out=vp;return full;}
    bool hdrBackTarget(){return back;}
    bool resources(unsigned,unsigned,unsigned,bool){return resourceOk;}
    bool sampled()const{return false;}
'''
fixture += method + r'''
};
int main(){
    // Exercise the production read-only camera path, including fresh movement,
    // map changes and failures. It must not admit subsequent world draws.
    float view[16]{},p[3]{};World w;
    assert(w.skyBackgroundContext(view,p)&&view[0]==1&&p[0]==2&&p[1]==3&&p[2]==-1);
    assert(NorthlightWorldCamera::reads==0);
    w.valid=false;
    assert(w.skyBackgroundContext(view,p)&&view[0]==7&&!w.valid&&w.context.inverseView[0]==1);
    NorthlightWorldCamera::current.inverseView[0]=8;
    assert(w.skyBackgroundContext(view,p)&&view[0]==8&&!w.valid);
    NorthlightWorldCamera::mapName="Expansion01";assert(!w.skyBackgroundContext(view,p));
    NorthlightWorldCamera::mapName="Northrend";
    w.lastRequest.map.clear();assert(!w.skyBackgroundContext(view,p));w.lastRequest.map="Northrend";
    w.failed=true;assert(!w.skyBackgroundContext(view,p));w.failed=false;
    w.fault=1;assert(!w.skyBackgroundContext(view,p));w.fault=0;
    w.pipelineActive=false;assert(!w.skyBackgroundContext(view,p));w.pipelineActive=true;
    w.skyEnvironment.skyValid=false;assert(!w.skyBackgroundContext(view,p));
    Fixture f;f.vp={.9990234375f,1};f.composeSkyBackground();assert(!f.celestialStorage.draws);
    f.vp={0,.94f};f.composeSkyBackground();assert(f.device.target.color==20&&f.device.copy.color==10);
    f.device.target.color=70; // Native floating name/alpha foreground follows the sky.
    f.composeSkyBackground();assert(f.device.target.color==70&&f.celestialStorage.draws==1);
    f.device.target.color=35; // Native death/scene grading sees the replaced sky too.
    f.applied=true;f.composeSkyBackground();assert(f.device.target.color==35);
    // A horizon/unclassified foreground draw can precede the first recognized
    // world draw. Its order must not switch the atmosphere off for this frame.
    Fixture early;early.worldStorage.valid=false;
    early.composeSkyBackground();assert(early.celestialStorage.draws==1&&early.device.target.color==20);
    assert(!early.worldStorage.valid);
    for(unsigned kind=0;kind<6;++kind){
        Fixture g;
        if(kind==0)g.skyLayers.available=false; // Catalog-proven native death sky.
        if(kind==1){g.worldStorage.valid=false;NorthlightWorldCamera::available=false;}
        if(kind==2)g.worldStorage.skyEnvironment.skyValid=false;
        if(kind==3)g.resourceOk=false;
        if(kind==4)g.device.copyOk=false;
        if(kind==5)g.back=false;
        g.composeSkyBackground();assert(!g.celestialStorage.draws&&g.device.target.color==10);
        if(kind==1){NorthlightWorldCamera::available=true;g.worldStorage.valid=true;g.device.target.color=70;g.composeSkyBackground();assert(g.device.target.color==70);}
    }
    ++f.frame;f.applied=false;f.composeSkyBackground();assert(f.celestialStorage.draws==2);
    std::puts("PASS sky camera independent of draw order; current camera, map guard, world ownership, foreground latch and native fallback");
}
'''
fixture = fixture.replace('    // PRODUCTION_CAMERA_METHOD', camera_method)
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
cpp = out/'sky-background-order.cpp'
cpp.write_bytes(fixture.encode('utf-8'))
exe = out/'sky-background-order.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86_64-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                *fp.include_flags(), str(cpp), '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True)
