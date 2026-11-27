#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Compile weather dispatch: native world particles, menu precipitation and sheltered optics."""

from source_units import source_text
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

renderer = source_text(fp.src('renderer.cpp'))
start = renderer.index('    void surfaceWeather(){')
end = renderer.index('    void renderEffects()', start)
method = renderer[start:end]
source = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include "weather_roof.h"
#include "quality_settings.h"
#include "surface_frame.h"
#include "weather_state.h"
#include "diagnostic_capture_request.h"
static unsigned weatherReceipts=0;
static void logf(const char* format,unsigned frame,double phase,const char* kind,double blend,double intensity,double gate){
    assert(std::strncmp(format,"WEATHER_PARITY ",15)==0);
    assert(frame==weatherReceipts+1&&phase==45.25&&std::strcmp(kind,"rain")==0);
    assert(blend==1&&intensity==1&&gate>0&&gate<1);
    ++weatherReceipts;
}
struct IDirect3DSurface9 {};
using HRESULT=int;
struct Texture {};
struct ExtensionDevice {struct RawScope {RawScope(ExtensionDevice&) {}};};
struct SavedState {static inline unsigned saves=0;bool ok=true;IDirect3DSurface9* targets[1]={nullptr};SavedState(ExtensionDevice*,int*) {++saves;}};
namespace NorthlightLightingCapture {
struct Capture {
    static inline unsigned draws=0,finished=0;
    static inline std::string reason;
    template<class... Args> explicit Capture(Args&&...){}
    void bindings(const char*,unsigned,HRESULT hr){assert(hr==0);++draws;}
    void bindingSkip(const char*,const char* why){reason=why;}
    void finish(bool success){assert(success);++finished;}
};
}
struct NorthlightSurfaceRenderer {
    struct BindingAudit {
        void (*record)(void*,const char*,unsigned,HRESULT);
        void* context;
        void operator()(const char* entry,unsigned mask)const{if(record)record(context,entry,mask,0);}
    };
};
struct World {
    NorthlightQuality::Settings q;
    NorthlightWeather::State state{NorthlightWeather::Kind::Rain,1,1,0,0};
    bool exposed=false,menu=false;float underwater=1;unsigned shelterQueries=0;
    bool isMenu()const{return menu;}
    unsigned weatherSetting()const{return q.weather;}
    float underwaterScale()const{return underwater;}
    const NorthlightWeather::State& weather()const{return state;}
    const NorthlightQuality::Settings& surfaceSettings()const{return q;}
    NorthlightQuality::Preset qualityPreset()const{return q.preset;}
    bool supplementalWeatherOutdoors(){++shelterQueries;return exposed;}
    float weatherLensHeat()const{return 0;}
};
struct Surface {
    using Audit=NorthlightSurfaceRenderer::BindingAudit;
    unsigned particles=0,lenses=0,sand=0;bool snow=false,rivulets=false;float particleAmount=0,particleSize=0,lensSize=0,frost=0,lensVisibility=0;
    void setMotion(const NorthlightSurfaceFrame&,unsigned,const NorthlightQuality::Settings&,double){}
    void precipitation(IDirect3DSurface9*,Texture*,unsigned,unsigned,const NorthlightSurfaceFrame& context,NorthlightQuality::Preset,IDirect3DSurface9*,const SavedState* saved,std::shared_ptr<const NorthlightWeatherShelter::Roof>,Audit audit){assert(saved&&saved->ok);++particles;particleAmount=context.c[3][0];snow=context.c[3][1]>0;particleSize=context.c[3][2];audit("Precipitation",8);}
    void lens(IDirect3DSurface9*,Texture*,IDirect3DSurface9*,unsigned,unsigned,bool,double,float visibility,IDirect3DSurface9*,float,float,float size,bool streams,float edge,const SavedState* saved,Audit audit){assert(saved&&saved->ok);++lenses;lensSize=size;rivulets=streams;frost=edge;lensVisibility=visibility;audit("Lens",1);}
    void sandLens(IDirect3DSurface9*,unsigned,unsigned,double,float visibility,IDirect3DSurface9*,float,float,const SavedState* saved,Audit audit){assert(saved&&saved->ok&&visibility>0);++sand;audit("SandLens",16);}
};
struct Host {
    World instance;World* world=&instance;Surface draw;Surface* surface=&draw;
    bool applied=true,enabled=true,failed=false,debugMode=false,worldDebug=false,skyBandSeen=true,hdrSceneReady=false,maskStencil=false,menuScene=false;
    struct Guard {bool capsHalved()const{return false;}} memoryGuard;
    struct Mask {bool armed()const{return false;}bool hasMarks()const{return false;}} nameMask;
    struct Profile {void mark(const char*){}} profiler;Profile* gpuProfile=&profiler;
    ExtensionDevice device;ExtensionDevice* ext=&device;int stateBlocks=0;
    Texture texture;Texture *depthTex=&texture,*scene=&texture;
    IDirect3DSurface9 image;IDirect3DSurface9 *worldDepth=&image,*sceneSurface=&image,*hdrPresentBack=&image;
    struct Hdr {Texture t;IDirect3DSurface9 s;Texture* texture(){return &t;}IDirect3DSurface9* surface(){return &s;}} hdrFrame;
    struct Parity {
        std::string path="fixture";bool enabled=false;
        const std::string* auditDirectory(unsigned)const{return enabled?&path:nullptr;}
    } frameParity;
    unsigned frame=1,lateWeatherAuditFrame=~0u;
    unsigned width=960,height=640;float weatherLayerGate=0;double surfaceTime=0;
    bool surfaceContext(NorthlightSurfaceFrame&)const{return true;}
    unsigned captures=0;const char* captureReason=nullptr;
    void captureWeatherDiagnostics(const NorthlightSurfaceFrame*,IDirect3DSurface9*,IDirect3DSurface9*,const char* reason){
        if(NorthlightDiagnosticCapture::take(NorthlightDiagnosticCapture::Weather)){++captures;captureReason=reason;}}
__METHOD__
};
int main(){
    Host missing;missing.surfaceWeather();assert(missing.draw.particles==0);
    for(auto kind:{NorthlightWeather::Kind::Rain,NorthlightWeather::Kind::Snow}){
        Host menu;menu.menuScene=menu.instance.menu=true;menu.instance.state.kind=kind;
        menu.instance.q.rainLens=menu.instance.q.snowLens=menu.instance.q.snowFrost=0;
        menu.instance.q.rainDropSize=125;menu.instance.q.snowFlakeSize=150;
        menu.surfaceWeather();assert(menu.draw.particles==0&&menu.draw.lenses==0);
        menu.instance.exposed=true;menu.surfaceWeather();assert(menu.draw.particles==1&&menu.draw.lenses==0&&menu.draw.particleAmount>0);
        assert(menu.draw.snow==(kind==NorthlightWeather::Kind::Snow)&&menu.draw.particleSize==(menu.draw.snow?.5f:.25f));
        menu.menuScene=false;menu.surfaceWeather();assert(menu.draw.particles==1);
        menu.menuScene=true;menu.instance.q.weather=0;menu.surfaceWeather();assert(menu.draw.particles==1);
    }
    // The eye is sheltered by a mushroom/roof, with open exterior still visible.
    for(auto kind:{NorthlightWeather::Kind::Rain,NorthlightWeather::Kind::Snow}){
        Host h;h.instance.state.kind=kind;h.surfaceWeather();
        assert(h.draw.particles==0&&h.draw.lenses==0);
        assert(h.instance.shelterQueries==1);
        h.instance.exposed=true;h.surfaceWeather();assert(h.draw.particles==0&&h.draw.lenses==1);
        const float gate=h.weatherLayerGate;
        h.skyBandSeen=false;h.surfaceWeather();assert(h.draw.particles==0&&h.draw.lenses==2&&h.weatherLayerGate>gate);
        h.skyBandSeen=true;h.instance.q.weather=0;h.surfaceWeather();assert(h.draw.particles==0&&h.draw.lenses==2);
        h.instance.q.weather=1;h.instance.state.kind=NorthlightWeather::Kind::Sand;h.surfaceWeather();assert(h.draw.particles==0&&h.draw.lenses==2&&h.draw.sand==1);
        h.instance.exposed=false;h.surfaceWeather();assert(h.draw.sand==1);h.instance.exposed=true;h.instance.q.sandLens=0;h.surfaceWeather();assert(h.draw.sand==1);
    }
    // Start without a sky draw, then keep turning toward/away from it. Neither
    // scene precipitation nor exposed lens effects may reset with the view.
    for(auto kind:{NorthlightWeather::Kind::Rain,NorthlightWeather::Kind::Snow,NorthlightWeather::Kind::Sand}){
        Host view;view.instance.state.kind=kind;view.instance.exposed=true;
        for(unsigned frame=0;frame<1200;++frame){
            view.skyBandSeen=frame%600>=540;const float gate=view.weatherLayerGate;view.surfaceWeather();
            assert(view.weatherLayerGate>=gate&&view.weatherLayerGate>0);
            assert(kind==NorthlightWeather::Kind::Sand?view.draw.sand==frame+1:
                view.draw.particles==0&&view.draw.lenses==frame+1);
        }
    }
    Host h;h.instance.exposed=true;h.instance.q.rainLens=0;const auto saves=SavedState::saves;h.surfaceWeather();assert(h.draw.particles==0&&h.instance.shelterQueries==0&&SavedState::saves==saves);
    h.instance.q.rainDropSize=250;h.instance.q.rainLens=1;h.instance.q.rainLensSize=75;h.surfaceWeather();
    assert(h.draw.particles==0&&h.draw.lensSize==.75f);
    h.instance.q.rainRivulets=0;h.surfaceWeather();assert(!h.draw.rivulets);
    Host ice;ice.instance.exposed=true;ice.instance.state.kind=NorthlightWeather::Kind::Snow;ice.instance.q.snowLens=0;ice.surfaceWeather();
    assert(ice.draw.lenses==1&&ice.draw.lensVisibility==0&&ice.draw.frost>0); // Frost is independently available with snow clumps disabled.
    ice.instance.q.snowFrost=0;const auto queries=ice.instance.shelterQueries;ice.surfaceWeather();assert(ice.draw.lenses==1&&ice.instance.shelterQueries==queries);
    NorthlightDiagnosticCapture::request(NorthlightDiagnosticCapture::Weather|NorthlightDiagnosticCapture::AO);
    h.surfaceWeather();assert(h.captures==1&&!h.captureReason&&NorthlightDiagnosticCapture::take()==NorthlightDiagnosticCapture::AO);
    h.skyBandSeen=false;NorthlightDiagnosticCapture::request(NorthlightDiagnosticCapture::Weather);h.surfaceWeather();
    assert(h.captures==2&&!h.captureReason&&NorthlightDiagnosticCapture::take()==0);
    h.instance.q.weather=0;NorthlightDiagnosticCapture::request(NorthlightDiagnosticCapture::Weather);h.surfaceWeather();
    assert(h.captures==3&&h.captureReason&&NorthlightDiagnosticCapture::take()==0);
    Host audit;audit.frameParity.enabled=true;audit.surfaceTime=45.25;audit.surfaceWeather();
    using Capture=NorthlightLightingCapture::Capture;
    assert(Capture::finished==1&&Capture::draws==0&&Capture::reason=="camera-sheltered"&&weatherReceipts==1);
    audit.surfaceWeather();assert(Capture::finished==1&&weatherReceipts==1);
    ++audit.frame;audit.instance.exposed=true;audit.surfaceWeather();
    assert(Capture::finished==2&&Capture::draws==1&&weatherReceipts==2);
    std::puts("PASS weather dispatch: no scene particles or roof queries, exposed lens, camera turns/no sky, disabled, sand, lazy shelter, lens sizes");
}
'''.replace('__METHOD__', method)
output = fp.output_dir()
cpp = output / 'weather-exterior.cpp'
cpp.write_text(source, encoding='utf-8')
exe = output / 'weather-exterior.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                *fp.include_flags(), str(cpp), '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True)
