# northlight-test: requires=cxx
"""Exercise the production resource path and frame-boundary AO switch with observed lifetimes."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('renderer.cpp').read_text(encoding='utf-8')

def extract(marker):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

harness = r'''
#include "quality_settings.h"
#include <cassert>
#include <iostream>
using UINT=unsigned;using DWORD=unsigned;using HRESULT=int;using D3DFORMAT=unsigned;
constexpr unsigned MAKEFOURCC(char a,char b,char c,char d){return unsigned(a)|(unsigned(b)<<8)|(unsigned(c)<<16)|(unsigned(d)<<24);}
constexpr unsigned D3DFMT_UNKNOWN=0,D3DFMT_A16B16G16R16F=1,D3DUSAGE_RENDERTARGET=1,D3DUSAGE_DEPTHSTENCIL=2,D3DPOOL_DEFAULT=0,D3DMULTISAMPLE_NONE=0;
bool FAILED(int hr){return hr<0;}
namespace NorthlightBackend {bool isPackagedDxvk(int){return false;}}
const DWORD kAoShader[]={1},kAoContactBloomShader[]={2},kHdaoContactBloomShader[]={3},kHbaoPlusContactBloomShader[]={4},kGtaoContactBloomShader[]={5},kAoBlurShader[]={6},kContactBloomShader[]={7},kCompositeShader[]={8};
unsigned alive=0;
struct Surface {Surface(){++alive;}void Release(){--alive;delete this;}};
struct Texture {Texture(){++alive;}void Release(){--alive;delete this;}int GetSurfaceLevel(unsigned,Surface** out){*out=new Surface;return 0;}};
struct Shader {const DWORD* code;Shader(const DWORD* p):code(p){++alive;}void Release(){--alive;delete this;}};
template<class T>void drop(T*& p){if(p){p->Release();p=nullptr;}}
struct Device {
    unsigned textures=0,shaders=0;bool failShader=false;
    int CreatePixelShader(const DWORD* code,Shader** out){if(failShader)return -1;++shaders;*out=new Shader(code);return 0;}
    int CreateTexture(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,Texture** out,void*){++textures;*out=new Texture;return 0;}
};
struct Service {int preview=-1;int aoPreview()const{return preview;}};
struct World {unsigned method=0;bool enabled=true;unsigned aoMethod()const{return method;}bool contactAO()const{return enabled;}};
struct Renderer {
    Device* ext;Service* settingsService;World* world;
    unsigned width=0,height=0,aoMethod=0,selectedBackend=0;
    unsigned sceneFormat=0,depthSnapshotFormat=0;
    bool worldDepthDescKnown=false,df24Refused=false,rainDepthOk=false,rainMaskCleared=false,rainMaskDrawn=false,rainMaskFailed=false,captured=false,contactAO=true,failed=false;
    struct {unsigned MultiSampleType=0;}worldDepthDesc;
    Surface *sceneSurface=nullptr,*aoSurface=nullptr,*aoRawSurface=nullptr,*rainMaskMS=nullptr,*rainMaskSurface=nullptr;
    Texture *scene=nullptr,*ao=nullptr,*aoRaw=nullptr,*depthTex=nullptr,*rainDepth=nullptr,*rainMask=nullptr;
    Shader *aoPS=nullptr,*aoContactBloomPS=nullptr,*aoBlurPS=nullptr,*contactBloomPS=nullptr,*compositePS=nullptr;
    bool error(int hr,const char*){if(hr<0)failed=true;return hr<0;}
    void rainMrtUnbind(){}
    template<class...T>void logf(const char*,T...){}
    __RESOURCES__
    void nextFrame(){__FRAME__}
    ~Renderer(){drop(sceneSurface);drop(aoSurface);drop(aoRawSurface);drop(scene);drop(ao);drop(aoRaw);drop(depthTex);drop(aoPS);drop(aoContactBloomPS);drop(aoBlurPS);drop(contactBloomPS);drop(compositePS);}
};
int main(){
    Device device;Service service;World world;
    {
        Renderer r;r.ext=&device;r.settingsService=&service;r.world=&world;
        assert(r.resources(800,600,10));assert(device.textures==4&&device.shaders==5);
        const auto scene=r.scene;const auto depth=r.depthTex;const auto ao=r.ao;const auto raw=r.aoRaw;
        const DWORD* expected[]={kAoContactBloomShader,kHdaoContactBloomShader,kHbaoPlusContactBloomShader,kGtaoContactBloomShader};
        for(unsigned pass=0;pass<200;++pass){
            service.preview=int(pass%8);r.nextFrame();assert(r.aoMethod==pass%4&&r.contactAO==bool(pass&4));
            assert(r.resources(800,600,10));assert(r.aoPS&&r.aoContactBloomPS&&r.aoContactBloomPS->code==expected[pass%4]);
            assert(r.scene==scene&&r.depthTex==depth&&r.ao==ao&&r.aoRaw==raw&&device.textures==4);
            auto count=device.shaders;r.nextFrame();assert(r.resources(800,600,10)&&device.shaders==count);
        }
        world.method=2;world.enabled=false;service.preview=-1;r.nextFrame();assert(r.aoMethod==2&&!r.contactAO);assert(r.resources(800,600,10));
        assert(r.aoContactBloomPS->code==kHbaoPlusContactBloomShader&&device.textures==4);
        service.preview=7;r.nextFrame();device.failShader=true;assert(!r.resources(800,600,10)&&r.failed);
        device.failShader=false;r.failed=false;assert(r.resources(800,600,10)&&r.aoContactBloomPS->code==kGtaoContactBloomShader&&device.textures==4);
    }
    assert(alive==0);
    std::cout<<"AO live switch: 200 production frame/resource transitions, all shaders, retained targets, no redundant creation, failure/retry and lifetimes passed\n";
}
'''.replace('__RESOURCES__', extract('    bool resources(UINT w, UINT h, D3DFORMAT format)')).replace('__FRAME__', extract('        if(settingsService&&world){'))
with tempfile.TemporaryDirectory(prefix='northlight-ao-live-') as tmp:
    cpp = Path(tmp) / 'ao_live.cpp'
    exe = Path(tmp) / 'ao_live.exe'
    cpp.write_text(harness, encoding='utf-8')
    subprocess.run(['clang++', '-std=c++17', '-O2', '-UNDEBUG', *fp.test_include_flags(), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
