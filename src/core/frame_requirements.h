#pragma once
#include "quality_settings.h"
#include "effect_switches.h"

namespace NorthlightFrameRequirements {
// Material capture uses potential demand; composition passes actual material
// availability. Other inputs are the effective switches, fixed for the frame.
struct Plan {
    bool geometry=false,shadows=false,probes=false,local=false,fog=false;
    bool lighting=false,modelNormals=false,materials=false,ao=false,weather=false;
    unsigned screenGroups()const{return 1u|(lighting?2u:0u)|(fog?4u:0u)|(materials?8u:0u)|(shadows?16u:0u)|(probes?32u:0u)|(local?64u:0u)|(fog&&local?128u:0u)|(fog&&shadows?256u:0u);}
    unsigned geometryGroups()const{return (shadows?1u:0u)|(probes?2u:0u);}
};
inline Plan make(const NorthlightQuality::Settings& q,const NorthlightEffectSwitches::Settings& effects,bool materials,bool diagnostic=false,bool directionalAvailable=true){
    Plan p;
    p.shadows=effects.shadows&&q.shadowProvider&&directionalAvailable;p.probes=effects.gi&&q.giStrength;p.local=q.localLights&&q.localLightLimit;
    p.fog=effects.fog;p.materials=materials;p.ao=q.contactAO;p.weather=q.weather;
    p.geometry=effects.gi||p.shadows||p.fog||p.local;
    // Fog currently consumes the shared distance/history producer. Its demand
    // is explicit here, rather than hidden in the model capture or compositor.
    p.lighting=p.probes||p.shadows||p.fog||p.local||materials||diagnostic;
    p.modelNormals=p.lighting;
    return p;
}
}
