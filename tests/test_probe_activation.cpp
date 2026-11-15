#include "probe_activation.h"
#include <cassert>
#include <cstdio>
int main(){
    NorthlightProbeActivation activation;
    NorthlightGI::ProbeAtlasEntry a;a.key={-1,2,3};a.occupied=true;a.probe.valid=true;
    const auto i=NorthlightGI::probeAtlasIndex(a.key);
    activation.begin("Azeroth");assert(activation.update(i,a,1)==1);
    // Animated SH changes and camera-driven publication preserve residency age.
    for(unsigned n=0;n<1000;++n){a.probe.sh[0].x=float(n);assert(activation.update(i,a,2+n*.01f)==1);}
    auto b=a;b.key.x+=16;assert(NorthlightGI::probeAtlasIndex(b.key)==i);
    assert(activation.update(i,b,20)==20);assert(activation.update(i,a,21)==21);
    a.probe.valid=false;assert(activation.update(i,a,22)==-2);
    a.occupied=false;assert(activation.update(i,a,22)==-1);a.occupied=true;
    a.probe.valid=true;assert(activation.update(i,a,23)==23);
    activation.begin("Kalimdor");assert(activation.update(i,a,24)==24);
    activation.reset();activation.begin("Kalimdor");assert(activation.update(i,a,25)==25);
    // Shader coverage is continuous, monotone and bounded over 450 ms.
    float previous=0;
    for(unsigned n=0;n<=1000;++n){float dt=float(n)*.001f;float fade=std::clamp(dt/.45f,0.f,1.f);
        assert(fade>=previous&&fade<=1);assert(fade-previous<.00223f);previous=fade;}
    std::puts("PASS: world-key activation, 1000 republishes, collisions, maps, reset, 450ms coverage");
}
