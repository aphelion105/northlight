#include "frame_requirements.h"
#include <cassert>
#include <cstdio>

using NorthlightFrameRequirements::make;

int main() {
    NorthlightQuality::Settings q;
    q.localLights=q.contactAO=q.weather=0;
    NorthlightEffectSwitches::Settings effects{false,false,false};
    auto off=make(q,effects,false);
    assert(!off.geometry&&!off.lighting&&!off.modelNormals&&!off.materials);
    assert(off.screenGroups()==1&&off.geometryGroups()==0);

    q.contactAO=1;
    auto ao=make(q,effects,false);
    assert(ao.ao&&!ao.geometry&&!ao.lighting&&!ao.modelNormals);
    assert(ao.screenGroups()==off.screenGroups()&&ao.geometryGroups()==0);
    q.contactAO=0;

    auto potential=make(q,effects,true),actual=make(q,effects,false);
    assert(potential.materials&&potential.lighting&&potential.modelNormals&&!potential.geometry);
    assert((potential.screenGroups()&8)&&!actual.materials&&!actual.lighting);
    auto diagnostic=make(q,effects,false,true);
    assert(diagnostic.lighting&&diagnostic.modelNormals&&!diagnostic.materials&&!diagnostic.geometry);

    effects.shadows=true;q.shadowProvider=1;
    auto shadow=make(q,effects,false);
    assert(shadow.shadows&&shadow.geometry&&shadow.lighting&&(shadow.geometryGroups()&1));
    q.shadowProvider=0;
    auto game=make(q,effects,false);
    assert(!game.shadows&&!game.lighting&&game.geometryGroups()==0);
    q.shadowProvider=1;
    auto unavailable=make(q,effects,false,false,false);
    assert(!unavailable.shadows&&!unavailable.lighting&&unavailable.geometryGroups()==0);
    effects.shadows=false;

    effects.gi=true;q.giStrength=1;
    auto gi=make(q,effects,false);
    assert(gi.probes&&gi.geometry&&gi.modelNormals&&gi.geometryGroups()==2);
    q.giStrength=0;
    auto zeroGI=make(q,effects,false);
    assert(!zeroGI.probes&&!zeroGI.lighting&&zeroGI.geometryGroups()==0);
    effects.gi=false;

    // Current fog still needs the distance/history lighting producer. Keep this
    // dependency explicit until a separate producer and its image oracle exist.
    effects.fog=true;
    auto fog=make(q,effects,false);
    assert(fog.fog&&fog.geometry&&fog.lighting&&fog.modelNormals);
    assert((fog.screenGroups()&6)==6&&fog.geometryGroups()==0);
    effects.fog=false;

    q.localLights=1;q.localLightLimit=0;
    assert(!make(q,effects,false).local&&!make(q,effects,false).lighting);
    q.localLightLimit=1;
    auto local=make(q,effects,false);
    assert(local.local&&local.geometry&&local.lighting&&(local.screenGroups()&64));
    q.localLights=0;
    assert(!make(q,effects,false).local);

    // AO/weather toggle combinations cannot accidentally acquire another
    // effect's geometry, light, shadow, fog or material resource groups.
    for(unsigned bits=0;bits<256;++bits) {
        effects={bool(bits&1),bool(bits&2),bool(bits&4)};
        q.giStrength=(bits&8)?100:0;q.shadowProvider=bool(bits&16);
        q.localLights=bool(bits&32);q.localLightLimit=(bits&64)?16:0;
        const bool materials=bits&128;
        q.contactAO=q.weather=0;
        auto base=make(q,effects,materials);
        q.contactAO=q.weather=1;
        auto layers=make(q,effects,materials);
        assert(!base.ao&&!base.weather&&layers.ao&&layers.weather);
        assert(base.screenGroups()==layers.screenGroups());
        assert(base.geometryGroups()==layers.geometryGroups());
        assert(base.geometry==layers.geometry&&base.lighting==layers.lighting);
        assert(base.modelNormals==layers.modelNormals&&base.materials==layers.materials);
    }
    std::puts("FRAME_REQUIREMENTS_PASS isolated consumers, provider readiness, potential/actual material demand, 256 independent layer combinations");
}
