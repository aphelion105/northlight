#include "water_material.h"
#include <cassert>
#include <cstdio>
int main(){
    NorthlightWaterMaterial::Detector d;int lookup,weather,plain,hd;
    d.noteCreate(&lookup,8,64,0);assert(d.candidate(&lookup)&&d.matches(&lookup,512,512,0));
    assert(!d.matches(&lookup,512,256,0)&&!d.matches(&lookup,512,512,1));
    d.noteCreate(&weather,8,128,0);assert(!d.candidate(&weather));
    d.noteCreate(&plain,8,64,1);assert(!d.candidate(&plain));
    d.noteCreate(&lookup,64,64,0);assert(!d.candidate(&lookup)); // reused address
    d.noteCreate(&hd,16,128,0);assert(!d.candidate(&hd));d.learn(&hd);assert(d.matches(&hd,2048,1024,0));
    d.forget(&hd);assert(!d.candidate(&hd));
    for(unsigned size:{2u,4u,8u}){d.noteCreate(&lookup,size,size*8,0);assert(d.matches(&lookup,128,128,0));}
    std::puts("Water material: stock/HD lookup, reduced textures, weather/RT exclusion and reused-address invalidation passed");
}
