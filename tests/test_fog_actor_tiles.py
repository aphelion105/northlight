#!/usr/bin/env python3
# northlight-test: requires=zig
"""Check the production range guard used before the optional fog broad phase."""
from pathlib import Path
import json,os,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if os.name=='nt' else ['clang++']
host=fp.src('world_atmospheric_fog.inl').read_text()
owner=fp.src('world_actor_shadows.h').read_text()
start=owner.index('    bool createShaders(')
end=owner.index('{',start)+1;depth=1
while depth:
    depth+=(owner[end]=='{')-(owner[end]=='}');end+=1
creation=owner[start:end]
start=host.index('        fogSource.tiledActor=fogSource.actor&&')
end=host.index('        if(profileSampled())',start)
binding=host[start:end]
commands=fp.src('world_atmosphere_commands.h').read_text()
probe_binding=next(line for line in commands.splitlines() if 'void bindProbeOrTiles(' in line)
manifest=json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']
fixture=r'''
#include "actor_shadow_map.h"
#include <cassert>
#include <cstdio>
#ifndef FAILED
#define FAILED(x) ((x)<0)
#endif
static const unsigned kActorTilesWideShader[]={5},kWorldFogActorTiledShader[]={6},kFogCloudsActorTiledShader[]={7};
static const unsigned kActorShadowShader[]={1},kActorTilesShader[]={2},kActorShadowUniformShader[]={3};
static constexpr unsigned kWorldFogActorTiledShaderSlots=@FOG@,kFogCloudsActorTiledShaderSlots=@CLOUD@;
struct D3DCAPS9 {unsigned MaxPixelShader30InstructionSlots=0;};
static int live=0;
struct Device {
 unsigned attempts=0,fail=0;int* texture=nullptr;
 int CreatePixelShader(const unsigned* code,int** out){if(++attempts==fail)return -1;*out=new int(int(*code));++live;return 0;}
 void SetTexture(unsigned slot,int* t){assert(slot==5);texture=t;}
};
using IDirect3DDevice9=Device;
using IDirect3DTexture9=int;
struct NorthlightAtmosphereCommands {Device* d;@PROBE@};
struct ActorShadows {
 int *filter_=nullptr,*tilesShader_=nullptr,*uniform_=nullptr,*tilesWide_=nullptr,*fogTiled_=nullptr,*cloudsTiled_=nullptr;
 struct Target {int* texture;};
 int tex[2]={10,20};
 Target tiles(unsigned source) {return {&tex[source]};}
 int* fogTiled() const {return fogTiled_;}
 int* cloudsTiled() const {return cloudsTiled_;}
 static void drop(int*& value){if(value){delete value;value=nullptr;--live;}}
 @CREATE@
 void clear(){drop(filter_);drop(tilesShader_);drop(uniform_);drop(tilesWide_);drop(fogTiled_);drop(cloudsTiled_);}
};
struct Renderer {
 Device device;Device* d=&device;D3DCAPS9 fogCaps;ActorShadows actorMaps;
 struct {unsigned actorShadows=1,actorShadowDetail=1;}quality;
 int old=9;int *fogActorPS=&old,*fogCloudsActorPS=&old;
 struct {bool actor=false,tiledActor=false;} fogSource;
 bool actorTilesWideCurrent[2]={};float actorMatrices[2][16]={},c[79][4]={};
 int neutral=30;int* textures[6]={nullptr,nullptr,nullptr,nullptr,nullptr,&neutral};
 Renderer(){for(auto& m:actorMatrices){m[0]=m[5]=1.f/12;m[10]=1.f/1280;m[15]=1;}c[24][0]=.0001f;}
 bool create(){return actorMaps.createShaders(d,fogCaps,quality.actorShadows&&quality.actorShadowDetail,fogActorPS&&fogCloudsActorPS);}
 void bind(int source){auto* fallbackProbe=textures[5];const bool wideTileMapReady=actorTilesWideCurrent[source];NorthlightAtmosphereCommands commands{d};@BIND@}
};
int main(){unsigned cases=0;
 // The production helper guarantees actorShadowPS/actorTilesPS/actorUniformPS
 // together before the tiled branch. Exercise that boundary, including every
 // required shader failure, rather than inject impossible partial states there.
 for(unsigned settings=0;settings<4;++settings)for(unsigned mask=0;mask<4;++mask)for(unsigned caps:{512u,600u,662u,663u,32768u})for(unsigned fail=0;fail<7;++fail){
  Renderer r;r.device.fail=fail;r.fogCaps.MaxPixelShader30InstructionSlots=caps;
  r.quality.actorShadows=bool(settings&1);r.quality.actorShadowDetail=bool(settings&2);
  if(!(mask&1))r.fogActorPS=nullptr;
  if(!(mask&2))r.fogCloudsActorPS=nullptr;
  const bool base=settings==3&&!(fail&&fail<=3);
  const bool eligible=base&&mask==3&&caps>=std::max(kWorldFogActorTiledShaderSlots,kFogCloudsActorTiledShaderSlots);
  const bool tiled=eligible&&!(fail>=4);
  assert(r.create()==(settings!=3||base));assert(bool(r.actorMaps.filter_)==base&&bool(r.actorMaps.tilesShader_)==base&&bool(r.actorMaps.uniform_)==base);
  assert(bool(r.actorMaps.fogTiled_)==tiled);assert(bool(r.actorMaps.cloudsTiled_)==tiled&&bool(r.actorMaps.tilesWide_)==tiled);
  const unsigned attempts=settings!=3?0:(fail&&fail<=3)?fail:eligible?(fail?fail:6):3;
  assert(r.device.attempts==attempts);assert(live==(base?3:0)+(tiled?3:0));
  if(base){
   // A one-shot allocation failure leaves the optional trio empty; a later
   // resource request retries it. Successful resources are reused unchanged.
   r.create();const unsigned retried=attempts+(eligible&&!tiled?3:0);
   assert(r.device.attempts==retried&&live==3+(eligible?3:0));
   assert(bool(r.actorMaps.fogTiled_)==eligible&&bool(r.actorMaps.cloudsTiled_)==eligible&&bool(r.actorMaps.tilesWide_)==eligible);
   r.create();assert(r.device.attempts==retried&&live==3+(eligible?3:0));
  }
  r.actorMaps.clear();assert(!live);++cases;
 }
 for(unsigned flags=0;flags<64;++flags)for(int source=0;source<2;++source){
  Renderer r;r.fogSource.actor=flags&1;r.actorTilesWideCurrent[source]=flags&2;
  if(flags&4){r.actorMaps.fogTiled_=new int(1);++live;}if(flags&8){r.actorMaps.cloudsTiled_=new int(2);++live;}
  if(!(flags&16))r.c[15][0]=1000;if(!(flags&32))r.c[24][0]=-.0001f;
  r.bind(source);const bool expected=flags==63;assert(r.fogSource.tiledActor==expected&&"STALE_FOG_TILES");
  assert(r.device.texture==(expected?r.actorMaps.tiles(source).texture:r.textures[5])&&"WRONG_FOG_TILE_SOURCE");
  r.actorTilesWideCurrent[source]=false;r.bind(source);assert(!r.fogSource.tiledActor&&r.device.texture==r.textures[5]);r.actorMaps.clear();assert(!live);++cases;
 }
 std::printf("PASS production tiled fog helper/binding: %u cases, actor switches/three-shader success boundary, six shader failures, caps and repeated initialization; fresh current-source maps and bounded coordinates required\n",cases);
}
'''.replace('@FOG@',str(manifest['WorldFogActorTiled']['static_instruction_slots'])).replace('@CLOUD@',str(manifest['FogCloudsActorTiled']['static_instruction_slots'])).replace('@CREATE@',creation).replace('@BIND@',binding).replace('@PROBE@',probe_binding)
out=fp.output_dir()
cpp=out/'binding.cpp';cpp.write_text(fixture)
variants=[('O2',['-O2']),('O0',['-O0'])] if os.name=='nt' else [
    ('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]
for name,flags in variants:
    for label,source in [('range',fp.TESTS/'test_fog_actor_tiles.cpp'),('binding',cpp)]:
        exe=out/(label+'-'+name+('.exe' if os.name=='nt' else ''))
        subprocess.run([*compiler,'-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),
                        str(source),'-o',str(exe)],env=fp.zig_env(),check=True)
        subprocess.run([str(exe)],check=True)
