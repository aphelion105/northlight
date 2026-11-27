#pragma once

// Runtime display switches. Shared world/GI caches stay warm so an A/B toggle
// does not start another geometry/probe warm-up. Nothing is written to disk.
namespace NorthlightEffectSwitches {
enum WorldView { SceneView, ShadowsView, GIView, FogView, NormalsView, SsaoView, WorldViewCount };
inline WorldView nextWorldView(unsigned current){return static_cast<WorldView>((current+1)%WorldViewCount);}
inline const char* worldViewName(unsigned view){
    constexpr const char* names[]={"scene","shadows","GI","fog volume","normals","SSAO"};
    static_assert(sizeof names/sizeof *names==WorldViewCount);
    return view<WorldViewCount?names[view]:"unknown";
}
struct Settings {
    bool gi=true,shadows=true,fog=true;
    bool operator==(const Settings& b)const{return gi==b.gi&&shadows==b.shadows&&fog==b.fog;}
};
enum Key : unsigned { Fog=1, GI=2, Shadows=4, All=Fog|GI|Shadows };
struct Hotkeys {
    Settings settings;
    unsigned held=0;
    unsigned poll(bool foreground,bool modifiers,unsigned down){
        down&=All;
        const unsigned pressed=down&~held;held=down;
        if(!foreground||!modifiers)return 0;
        if(pressed&Fog)settings.fog=!settings.fog;
        if(pressed&GI)settings.gi=!settings.gi;
        if(pressed&Shadows)settings.shadows=!settings.shadows;
        return pressed;
    }
};
}
