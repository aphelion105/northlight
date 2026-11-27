#include "terrain_unique.h"
#include "northlight_mem.h"
#include <random>
#include <chrono>
extern "C" int memcmp(const void*a,const void*b,size_t n){return NorthlightMem::compare(a,b,n);}
using namespace NorthlightActorDeformation;using namespace NorthlightReplayBounds::detail;
bool DisplacementStateTestAccess::terrainCall(const Program& p,const float* low,const float* high,const float* bank,float& out,unsigned kind,NorthlightMaterialViewBounds::DisplacementLod* lod){return NorthlightAutoDisplacement::nearestViewDepth(p,0,low,high,bank,out,kind,nullptr,lod);}
bool DisplacementStateTestAccess::terrainMatch(const Program& p){return NorthlightAutoDisplacement::terrainDepthProgram(p);}
bool DisplacementStateTestAccess::wallMatch(const Program& p){return NorthlightAutoDisplacement::wallDepthProgram(p);}
volatile float sink;
int main(){Program p;p.major=3;p.positionRegister=0;p.inputs={{0,D3DDECLUSAGE_POSITION,0}};
 const unsigned code[]={5,4,4,2};const Word a[]={0xa0e40001,0x90000000,0x90aa0000,0x80e40000},b[]={0x90550000,0xa0e40000,0xa0e40002,0xa0e40003},c[]={0,0x80e40000,0x80e40000,0};
 for(unsigned i=0;i<4;++i){Operation op;op.code=code[i];op.destination=0x800f0000;op.source[0]={a[i],0};op.source[1]={b[i],0};op.source[2]={c[i],0};p.operations.push_back(op);}buildKernel(p);assert(DisplacementStateTestAccess::terrainMatch(p));
 for(unsigned i=0;i<4;++i)for(unsigned field=0;field<8;++field){auto q=p;if(field==0)q.operations[i].code^=1;else if(field==1)q.operations[i].destination^=1;else if(field<5)q.operations[i].source[field-2].token^=1;else q.operations[i].source[field-5].address=1;assert(!DisplacementStateTestAccess::terrainMatch(q));}
 {auto q=p;q.major=2;assert(!DisplacementStateTestAccess::terrainMatch(q));q=p;q.inputs[0].reg=1;assert(!DisplacementStateTestAccess::terrainMatch(q));q=p;q.inputs[0].usage=D3DDECLUSAGE_NORMAL;assert(!DisplacementStateTestAccess::terrainMatch(q));q=p;q.textureCoordinates=true;assert(!DisplacementStateTestAccess::terrainMatch(q));}

 float bank[1024]={},lo[3]={0,0,5},hi[3]={.1f,.1f,5.1f};
 bank[0]=bank[5]=bank[10]=bank[15]=1;
 for(bool fast:{false,true}){
  float nearest;NorthlightMaterialViewBounds::DisplacementLod lod{12};
  lo[0]=0;hi[0]=.1f;
  assert(DisplacementStateTestAccess::terrainCall(p,lo,hi,bank,nearest,fast,&lod)&&nearest>4&&lod.small&&!lod.outOfRange);
  hi[0]=.75f;
  assert(DisplacementStateTestAccess::terrainCall(p,lo,hi,bank,nearest,fast,&lod)&&!lod.small&&!lod.outOfRange);
  lo[0]=13;hi[0]=14;
  assert(DisplacementStateTestAccess::terrainCall(p,lo,hi,bank,nearest,fast,&lod)&&nearest<12&&!lod.small&&lod.outOfRange);
 }

 std::mt19937 gen(17);for(unsigned sample=0;sample<200000;++sample){for(unsigned k=0;k<3;++k){lo[k]=float(int(gen()%10000)-5000)*.01f;hi[k]=lo[k]+float(gen()%100)*.01f;}for(unsigned k=0;k<16;++k)bank[k]=float(int(gen()%2000)-1000)*.001f;for(unsigned k=0;k<3;++k)bank[k*4+3]=0;bank[15]=1;if(sample%17==0)bank[gen()%16]=std::numeric_limits<float>::quiet_NaN();if(sample%41==0){unsigned bits=gen();std::memcpy(&bank[gen()%16],&bits,4);}if(sample%23==0){unsigned bits=gen();std::memcpy(&lo[gen()%3],&bits,4);}p.definitions.clear();if(sample%7==0){Definition d;d.reg=gen()%4;for(auto &v:d.value){unsigned bits=gen();std::memcpy(&v,&bits,4);}p.definitions.push_back(d);if(sample%14==0)p.definitions.push_back(d);}if(sample%31==0)bank[gen()%16]=std::numeric_limits<float>::denorm_min();float a=0,b=0;bool x=DisplacementStateTestAccess::terrainCall(p,lo,hi,bank,a,false),y=DisplacementStateTestAccess::terrainCall(p,lo,hi,bank,b,true);if(x!=y||(x&&std::memcmp(&a,&b,4))){printf("MISMATCH %u %u %u %.9g %.9g\n",sample,x,y,a,b);return 1;}}
 Program wall=p;wall.operations.clear();wall.definitions.clear();
 const unsigned wallCode[]={9,9,1,9};
 const Word wallDest[]={0x80010000,0x80020000,0x80080000,0x80040000};
 const Word wallSource[]={0xa0e4001f,0xa0e40020,0xa0000000,0xa0e40021};
 for(unsigned i=0;i<4;++i){
  Operation op;op.code=wallCode[i];op.destination=wallDest[i];
  op.source[0]={wallSource[i],0};if(i!=2)op.source[1]={0x90e40000,0};
  wall.operations.push_back(op);
 }
 buildKernel(wall);assert(DisplacementStateTestAccess::wallMatch(wall));
 for(unsigned sample=0;sample<200000;++sample){
  for(unsigned k=0;k<3;++k){
   lo[k]=float(int(gen()%100000)-50000)*.01f;
   hi[k]=lo[k]+float(gen()%100)*.01f;
  }
  for(unsigned k=124;k<136;++k)bank[k]=float(int(gen()%2000)-1000)*.001f;
  bank[0]=1;
  if(sample%17==0)bank[124+gen()%12]=std::numeric_limits<float>::quiet_NaN();
  if(sample%23==0)lo[0]=-std::numeric_limits<float>::max();
  if(sample%31==0)bank[124+gen()%12]=std::numeric_limits<float>::max();
  if(sample%41==0)bank[124+gen()%12]=std::numeric_limits<float>::denorm_min();
  wall.definitions.clear();
  if(sample%7==0){
   Definition d;d.reg=31+gen()%3;
   for(auto& v:d.value){const unsigned bits=gen();std::memcpy(&v,&bits,4);}
   wall.definitions.push_back(d);
  }
  float slow=0,fast=0;
  const bool x=DisplacementStateTestAccess::terrainCall(wall,lo,hi,bank,slow,0);
  const bool y=DisplacementStateTestAccess::terrainCall(wall,lo,hi,bank,fast,2);
  if(x!=y||(x&&std::memcmp(&slow,&fast,4))){
   std::printf("WMO_MISMATCH %u %u %u %.9g %.9g\n",sample,x,y,slow,fast);
   return 1;
  }
 }
 std::puts("MATERIAL_TERRAIN_BOUNDS_PASS terrain=200000 wmo=200000 current_constants definitions duplicate_defs nan subnormal random_bits program_mismatch");
}
