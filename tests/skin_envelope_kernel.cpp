#define NOMINMAX
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <random>
#include <chrono>
#include <type_traits>
#include <cfenv>
#include "replay_bounds.h"
#ifdef NORTHLIGHT_SKIN_REFERENCE
namespace NorthlightSkinReference {using namespace NorthlightReplayBounds;}
#define NorthlightReplayBounds NorthlightSkinReference
#include NORTHLIGHT_SKIN_REFERENCE
#undef NorthlightReplayBounds
#endif
namespace B=NorthlightReplayBounds;
using Four=NorthlightActorDeformation::Four;
static volatile float sink=0;
struct Case {B::SkinEnvelope skin;float constants[1024]={},inverse[16]={};};
static Case authored(unsigned seed,unsigned bones){
 Case c;std::mt19937 rng(seed);auto f=[&](float range){return float(int(rng()%20001)-10000)*(range/10000);};
 for(unsigned i=0;i<160;++i){Four inputs[3]={};for(unsigned k=0;k<3;++k)inputs[0][k]=f(8);inputs[0][3]=1;
  for(unsigned k=0;k<4;++k){inputs[1][k]=float(rng()%1001)/1000;inputs[2][k]=float(rng()%bones);}
  if(!i)inputs[2][0]=float(bones-1); // Exercise the last palette row, including bone 74/c255.
  assert(c.skin.add(inputs));}
 for(unsigned i=0;i<75;++i){float* r=c.constants+4*(31+3*i);for(unsigned k=0;k<12;++k)r[k]=f(k%4==3?40.f:1.2f);}
 const float x=f(3.1f),y=f(3.1f),z=f(3.1f),cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
 c.inverse[0]=cz*cy;c.inverse[4]=cz*sy*sx-sz*cx;c.inverse[8]=cz*sy*cx+sz*sx;
 c.inverse[1]=sz*cy;c.inverse[5]=sz*sy*sx+cz*cx;c.inverse[9]=sz*sy*cx-cz*sx;
 c.inverse[2]=-sy;c.inverse[6]=cy*sx;c.inverse[10]=cy*cx;c.inverse[15]=1;
 c.inverse[12]=f(1000);c.inverse[13]=f(1000);c.inverse[14]=f(1000);return c;
}
#ifdef NORTHLIGHT_SKIN_REFERENCE
static NorthlightSkinReference::SkinEnvelope frozen(const B::SkinEnvelope& source){
 static_assert(std::is_trivially_copyable_v<B::SkinEnvelope>&&std::is_trivially_copyable_v<NorthlightSkinReference::SkinEnvelope>);
 static_assert(sizeof(B::SkinEnvelope)==sizeof(NorthlightSkinReference::SkinEnvelope));
 NorthlightSkinReference::SkinEnvelope result;std::memcpy(&result,&source,sizeof result);return result;
}
static void compare(const Case& c){
 auto old=frozen(c.skin);
 for(bool aligned:{false,true}){B::Bounds a;NorthlightSkinReference::Bounds b;
  const bool x=c.skin.evaluate(c.constants,c.inverse,a,nullptr,nullptr,aligned),y=old.evaluate(c.constants,c.inverse,b,nullptr,nullptr,aligned);
  assert(x==y&&a.valid==b.valid&&!std::memcmp(a.low,b.low,sizeof a.low)&&!std::memcmp(a.high,b.high,sizeof a.high));
 }
}
#endif
int main(){
 unsigned cases=0;
 for(unsigned bones:{1u,4u,16u,75u})for(unsigned pose=0;pose<512;++pose){auto c=authored(911+pose,bones);
#ifdef NORTHLIGHT_SKIN_REFERENCE
  compare(c);++cases;
  for(unsigned basis=0;basis<2;++basis){auto affine=c;
   if(basis==0){for(unsigned axis=0;axis<3;++axis){affine.inverse[axis]*=-.75f;affine.inverse[4+axis]*=1.5f;affine.inverse[8+axis]*=.5f;}}
   else {for(unsigned i=0;i<12;++i)affine.inverse[i]=0;affine.inverse[0]=affine.inverse[5]=.6f;affine.inverse[1]=.8f;affine.inverse[4]=-.8f;affine.inverse[10]=1;}
   compare(affine);++cases;
  }
  for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
   const int before=std::fegetround();assert(std::fesetround(mode)==0);
   compare(c);++cases;
   for(bool negativeZero:{false,true}){auto identity=c;
    for(unsigned i=0;i<16;++i)identity.inverse[i]=i%5==0?1.f:negativeZero?-0.f:0.f;
    compare(identity);++cases;
   }
   assert(std::fesetround(before)==0);
  }
  for(unsigned invalid=0;invalid<7;++invalid){auto bad=c;const unsigned first=4*(31+3*c.skin.order[0]);
   if(invalid<3)bad.constants[first]=invalid==0?INFINITY:invalid==1?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::denorm_min();
   else if(invalid==3)bad.inverse[15]=2;else if(invalid==4)bad.inverse[0]=INFINITY;else if(invalid==5)bad.skin.sumHigh=17;else bad.skin.usedBones=0;
   compare(bad);++cases;
  }
  for(bool aligned:{false,true})for(unsigned stop:{1u,2u,3u}){
   struct Stop {unsigned calls=0,limit;};Stop x{0,stop},y{0,stop};const auto halt=[](void* p){auto& s=*static_cast<Stop*>(p);return ++s.calls>=s.limit;};
   auto old=frozen(c.skin);B::Bounds a;NorthlightSkinReference::Bounds b;
   const bool aa=c.skin.evaluate(c.constants,c.inverse,a,halt,&x,aligned),bb=old.evaluate(c.constants,c.inverse,b,halt,&y,aligned);
   assert(aa==bb&&a.valid==b.valid&&x.calls==y.calls&&!std::memcmp(a.low,b.low,sizeof a.low)&&!std::memcmp(a.high,b.high,sizeof a.high));++cases;
  }
#else
  for(bool aligned:{false,true}){B::Bounds b;assert(c.skin.evaluate(c.constants,c.inverse,b,nullptr,nullptr,aligned)&&b.valid);++cases;}
#endif
 }
#ifdef NORTHLIGHT_SKIN_REFERENCE
 std::printf("SKIN_KERNEL_EXACT_PASS cases=%u modes=2 invalid_inputs stop_callbacks bitexact=1\n",cases);
 std::vector<Case> inputs;std::vector<NorthlightSkinReference::SkinEnvelope> old;
 for(unsigned bones:{1u,4u,16u,75u})for(unsigned i=0;i<64;++i){inputs.push_back(authored(71+i,bones));old.push_back(frozen(inputs.back().skin));}
 for(bool identity:{false,true})for(bool aligned:{false,true}){double times[2][9]={};
  if(identity)for(auto& c:inputs)for(unsigned i=0;i<16;++i)c.inverse[i]=i%5==0?1.f:0.f;
  for(unsigned round=0;round<9;++round)for(unsigned turn=0;turn<2;++turn){const unsigned variant=(round+turn)%2;const auto start=std::chrono::steady_clock::now();
   for(unsigned frame=0;frame<64;++frame)for(unsigned i=0;i<inputs.size();++i){const auto& c=inputs[i];
    if(variant){B::Bounds b;assert(c.skin.evaluate(c.constants,c.inverse,b,nullptr,nullptr,aligned));sink=b.low[0];}
    else {NorthlightSkinReference::Bounds b;assert(old[i].evaluate(c.constants,c.inverse,b,nullptr,nullptr,aligned));sink=b.low[0];}}
   times[variant][round]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/64;
  }
  for(auto& t:times)std::sort(t,t+9);
  std::printf("SKIN_KERNEL_TIMING aligned=%u identity=%u boundsPerFrame=256 referenceMs=%.6f currentMs=%.6f sink=%g\n",unsigned(aligned),unsigned(identity),times[0][4],times[1][4],float(sink));
 }
#else
 std::printf("SKIN_KERNEL_EXACT_PASS cases=%u modes=2 bitexact=0 reference=absent\n",cases);
#endif
 return 0;
}
