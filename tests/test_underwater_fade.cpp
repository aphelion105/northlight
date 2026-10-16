#include "underwater_fade.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightUnderwater;
int main(){
    {Fade f;assert(f.update("m",true,false,1000)==0&&!f.underwater&&!f.changed);}           // dry stays 0
    {Fade f;f.update("m",true,false,1000);float v=f.update("m",true,true,1016);assert(f.changed&&f.underwater&&v>0&&v<.1f);  // fades in, no pop
     std::uint32_t t=1016;while(f.update("m",true,true,t+=16)<1.f)assert(t<3000);assert(t-1016>=Fade::FadeMs-32);assert(f.scale()==0);
     assert(!f.changed);float w=f.update("m",true,false,t+=16);assert(f.changed&&w<1&&w>.9f);                // fades out
     while(f.update("m",true,false,t+=16)>0.f)assert(t<6000);assert(f.scale()==1);}
    {Fade f;f.update("m",true,true,1000);std::uint32_t t=1000;while(f.update("m",true,true,t+=16)<1.f);   // unknown frames hold within HoldMs
     for(std::uint32_t u=t;u<=t+Fade::HoldMs;u+=16){assert(f.update("m",false,false,u)==1.f&&f.underwater);}
     float v=f.update("m",false,false,t+Fade::HoldMs+16);assert(!f.underwater&&f.changed&&v<1.f);}           // then not underwater
    {Fade f;for(std::uint32_t t=1000;t<2000;t+=16)f.update("m",false,true,t);assert(f.value==0&&!f.underwater);} // never known: never underwater
    {Fade f;f.update("m",true,true,1000);for(std::uint32_t t=1016;t<2000;t+=16)f.update("m",true,true,t);
     f.update("n",false,false,2016);assert(!f.underwater);}                                                  // map change drops the hold
    {Fade f;f.update("m",true,true,1000);float v=f.update("m",true,true,100000);assert(v<=1.f&&v>=0);}      // a long stall steps at most 250 ms
    std::puts("underwater fade ok");return 0;
}
