#include "point_light_shadow.h"
#include "baseline_point_light_shadow.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
struct Box{float lo[3],hi[3];};
template<class Clip> [[gnu::noinline]] unsigned update(const float (&matrices)[6][16],const NorthlightLocalLights::Light& light,const Box* boxes,unsigned count){
 const Clip clip(matrices,light);unsigned sum=0;
 for(unsigned i=0;i<count;++i)sum+=clip.mask(boxes[i].lo,boxes[i].hi,true);
 return sum;
}
int main(){
 float matrices[6][16],position[]={1345,-2499,317};for(unsigned i=0;i<6;++i)NorthlightPointShadow::faceMatrix(position,.1f,48,i,matrices[i]);
 NorthlightLocalLights::Light light{};std::copy_n(position,3,light.position);light.attenuationEnd=48;
 std::mt19937 rng(8397);std::uniform_real_distribution<float> unit(-1,1);volatile unsigned checksum=0;
 for(float spread:{32.f,96.f})for(unsigned requested:{63u,3u,1u}){
  std::vector<Box> boxes(8192);for(auto& b:boxes)for(unsigned k=0;k<3;++k){const float at=position[k]+spread*unit(rng),half=std::abs(unit(rng))*4;b.lo[k]=at-half;b.hi[k]=at+half;}
  NorthlightPointShadow::FaceClip current(matrices,light,requested);OldPointShadow::FaceClip old(matrices,light,requested);
  for(const auto& b:boxes)if(current.mask(b.lo,b.hi,true,requested)!=old.mask(b.lo,b.hi,true,requested))return 2;
  for(unsigned block=0;block<16;++block){const unsigned mode=(block/2)%2?1-block%2:block%2;unsigned sum=0;const auto started=std::chrono::steady_clock::now();
   for(unsigned repeat=0;repeat<20;++repeat)for(const auto& b:boxes)sum+=mode?current.mask(b.lo,b.hi,true,requested):old.mask(b.lo,b.hi,true,requested);
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();checksum+=sum;
   printf("CLIP_BENCH spread=%.0f requested=%u block=%u warm=%u mode=%u cpuMs=%.9f candidates=163840\n",spread,requested,block,unsigned(block<4),mode,ms);
  }
  if(spread==32&&requested==63)for(unsigned count:{0u,1u,8u,64u})for(unsigned block=0;block<16;++block){
   const unsigned mode=(block/2)%2?1-block%2:block%2;float moved[6][16];std::memcpy(moved,matrices,sizeof moved);
   unsigned sum=0;const auto started=std::chrono::steady_clock::now();
   for(unsigned repeat=0;repeat<4096;++repeat){moved[0][12]+=.0001f;
    sum+=mode?update<NorthlightPointShadow::FaceClip>(moved,light,boxes.data(),count):update<OldPointShadow::FaceClip>(moved,light,boxes.data(),count);}
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();checksum+=sum;
   printf("CLIP_SETUP count=%u block=%u warm=%u mode=%u cpuMs=%.9f checksum=%u\n",count,block,unsigned(block<4),mode,ms,sum);
  }
 }
 printf("CLIP_BENCH_PASS checksum=%u\n",unsigned(checksum));
}
