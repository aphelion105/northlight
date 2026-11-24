#!/usr/bin/env python3
# northlight-test: requires=zig
"""Native tests of the hotkeys and production switch transition; no game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json
import subprocess
import tempfile
import os
from pathlib import Path

world = fp.src('world_renderer.h').read_text()


def block(start):
    a = world.index(start)
    b = world.index('{', a)
    depth = 1
    end = b + 1
    while depth:
        depth += (world[end] == '{') - (world[end] == '}')
        end += 1
    return world[a:end]


setter = block('    void setEffects(')
body = r'''
#include "effect_switches.h"
#include "quality_settings.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
struct Fixture {
    NorthlightEffectSwitches::Settings effects,requestedEffects;
    NorthlightQuality::Settings quality; // 0.3.138 northlight-quality.ini; defaults: giPass is a pass-through
    bool shadowsComposited=true,settingsRequestDirty=false,pipelineActive=true;
    struct {bool valid=true;void invalidate(){valid=false;}} history;
    bool shadowFrameReady=true,pointReady=true;
    bool cacheValid=true;unsigned transitionReleases=0;
    void releaseShadowTransitions(){++transitionReleases;}
    bool keepStatic=false;
    void invalidateShadowCache(bool retain){cacheValid=false;keepStatic=retain;}
    SETTER
};
int main(){
    using namespace NorthlightEffectSwitches;
    for(unsigned quality=0;quality<8;++quality)for(unsigned keys=0;keys<8;++keys)for(bool active:{false,true}){
        Fixture f;f.pipelineActive=active;f.quality.gi=quality&1;f.quality.worldShadows=quality&2;f.quality.worldFog=quality&4;
        Settings requested{bool(keys&1),bool(keys&2),bool(keys&4)};f.setEffects(requested);
        assert(f.effects.gi==(active&&bool(quality&1)&&requested.gi));
        assert(f.effects.shadows==(active&&bool(quality&2)&&requested.shadows));
        assert(f.effects.fog==(active&&bool(quality&4)&&requested.fog));
        f.pipelineActive=true;f.setEffects(f.requestedEffects);assert(f.requestedEffects==requested);
    }
    // All combinations, key repeat, modifiers and focus transitions.
    for(unsigned mask=0;mask<=All;++mask){
        Hotkeys k;
        assert(k.poll(true,true,mask)==mask);
        assert(k.settings.gi==!(mask&GI));
        assert(k.settings.shadows==!(mask&Shadows));
        assert(k.settings.fog==!(mask&Fog));
        auto once=k.settings;
        for(int i=0;i<1000;++i)assert(k.poll(true,true,mask)==0&&k.settings==once);
        k.poll(true,true,0);assert(k.poll(true,true,mask)==mask);
        assert(k.settings==Settings{});
    }
    Hotkeys keys;
    assert(!keys.poll(false,true,All));
    assert(!keys.poll(true,true,All)&&keys.settings==Settings{});
    keys.poll(true,true,0);
    assert(!keys.poll(true,false,All));
    assert(!keys.poll(true,true,All)&&keys.settings==Settings{});
    keys.poll(true,true,0);
    assert(!keys.poll(true,true,8)); // no F11 bit
    assert(keys.poll(true,true,Fog)==Fog);
    assert(keys.poll(true,true,Fog|GI)==GI);
    assert(!keys.settings.fog&&!keys.settings.gi&&keys.settings.shadows);

    // Run the production transition code for each enabled/disabled combination.
    for(unsigned mask=0;mask<=All;++mask){
        Fixture f;Settings next{!(mask&GI),!(mask&Shadows),!(mask&Fog)};
        f.setEffects(next);
        assert(f.effects==next);
        assert(f.history.valid==!mask);
        assert(f.settingsRequestDirty==bool(mask&(Fog|GI)));
        assert(f.cacheValid==!(mask&Shadows));
        assert(f.keepStatic==bool(mask&Shadows));
        assert(f.shadowFrameReady==!(mask&Shadows));
        assert(f.pointReady==!(mask&Shadows));
        assert(f.transitionReleases==unsigned(bool(mask&Shadows)));
        f.history.valid=true;f.settingsRequestDirty=false;
        f.setEffects(next);assert(f.history.valid&&!f.settingsRequestDirty);
        assert(f.transitionReleases==unsigned(bool(mask&Shadows)));
    }
    // GI=0 (northlight-quality.ini): F8 can never enable the GI pass; shadows/fog still switch.
    for(unsigned mask=0;mask<=All;++mask){
        Fixture g;g.quality.gi=0;g.effects.gi=false;Settings next{!(mask&GI),!(mask&Shadows),!(mask&Fog)};
        g.setEffects(next);
        assert(!g.effects.gi&&g.effects.shadows==next.shadows&&g.effects.fog==next.fog);
        assert(g.cacheValid==!(mask&Shadows));
        const bool onlyGI=(mask&~GI)==0; // GI toggles alone are no transition when GI=0
        assert(g.history.valid==onlyGI);
        assert(g.settingsRequestDirty==bool(mask&Fog));
    }
    Fixture f;
    f.setEffects({true,false,true});
    f.history.valid=true;
    f.cacheValid=f.shadowFrameReady=f.shadowsComposited=f.pointReady=true;
    f.setEffects({true,true,true});
    assert(!f.history.valid&&!f.cacheValid&&f.keepStatic);
    assert(!f.shadowFrameReady&&!f.shadowsComposited&&!f.pointReady&&f.transitionReleases==1);
    puts("PASS: effect combinations, focus/repeat, history invalidation and warm static cache");
}
'''
body = body.replace('SETTER', setter)

# Production wiring of the fog switch: the real key poll
# reaches setEffects() and disabling fog reaches the horizon haze policy.
renderer = fp.src('renderer.cpp').read_text()
assert 'if(effectKeys.poll(focus,modifiers,componentKeys)){' in renderer
assert renderer.index('effectKeys.poll(') < renderer.index('if(world)world->setEffects(settings);')
bank = fp.src('world_frame_constants.inl').read_text()
assert 'farZ,frame.hazeZone,effects.fog&&!atmosphere.imported.valid,celestialValid,hazeSun,hazeSunWeight,hazeLift,wx.hazeTauScale());' in bank
composite=fp.src('world_composite.h').read_text()
assert 'layers,material,haze.haze,uploadedFog' in world
assert composite.index('d->SetPixelShaderConstantF(34,in.haze,1);') < composite.index('"world composite"')

with tempfile.TemporaryDirectory(prefix='fr-effect-switches-') as temp:
    folder = Path(temp)
    (folder / 'test.cpp').write_text(body)
    compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if os.name=='nt' else ['clang++']
    variants=(['-O2'],['-O0','-g']) if os.name=='nt' else (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])
    for flags in variants:
        subprocess.run([*compiler, '-std=c++17', '-UNDEBUG', *flags, *fp.test_include_flags(),
                        str(folder / 'test.cpp'), '-o', str(folder / 'test')], check=True)
        subprocess.run([str(folder / 'test')], check=True)
print(json.dumps({'game_launched': False, 'native': 'PASS', 'sanitizers': 'not run on Windows' if os.name=='nt' else 'PASS'}))
