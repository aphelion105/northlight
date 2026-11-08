#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Check the production range guard used before the optional fog broad phase."""
from pathlib import Path
import json,shutil,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
compiler=shutil.which('clang++') or 'c++'
host=fp.src('world_renderer.h').read_text()
start=host.index('    void actorShaderResources(')
end=host.index('{',start)+1;depth=1
while depth:
    depth+=(host[end]=='{')-(host[end]=='}');end+=1
creation=host[start:end]
start=host.index('                fogActorTiledCurrent=fogActorCurrent&&')
end=host.index('                if(profileSampled())',start)
binding=host[start:end]
manifest=json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']
fixture=r'''
#include "actor_shadow_map.h"
#include <cassert>
#include <cstdio>
#define FAILED(x) ((x)<0)
static const unsigned kActorTilesWideShader[]={5},kWorldFogActorTiledShader[]={6},kFogCloudsActorTiledShader[]={7};
static const unsigned kActorShadowShader[]={1},kActorTilesShader[]={2},kActorShadowUniformShader[]={3};
static constexpr unsigned kWorldFogActorTiledShaderSlots=@FOG@,kFogCloudsActorTiledShaderSlots=@CLOUD@;
struct D3DCAPS9 {unsigned MaxPixelShader30InstructionSlots=0;};
static void logf(const char*,...){}
static int live=0;
struct Device {
 unsigned attempts=0,fail=0;int* texture=nullptr;
 int CreatePixelShader(const unsigned* code,int** out){if(++attempts==fail)return -1;*out=new int(int(*code));++live;return 0;}
 void SetTexture(unsigned slot,int* t){assert(slot==5);texture=t;}
};
struct Renderer {
 Device device;Device* d=&device;D3DCAPS9 fogCaps;
 struct {unsigned actorShadows=1,actorShadowDetail=1;}quality;
 int old=9;int *actorShadowPS=nullptr,*actorTilesPS=nullptr,*actorUniformPS=nullptr,*fogActorPS=&old,*fogCloudsActorPS=&old;
 int *actorTilesWidePS=nullptr,*fogActorTiledPS=nullptr,*fogCloudsActorTiledPS=nullptr;
 bool fogActorCurrent=false,fogActorTiledCurrent=false,actorTilesWideCurrent[2]={};float actorMatrices[2][16]={},c[79][4]={};
 int tex[3]={10,20,30};int* actorTiles[2]={&tex[0],&tex[1]};int* textures[6]={nullptr,nullptr,nullptr,nullptr,nullptr,&tex[2]};
 Renderer(){for(auto& m:actorMatrices){m[0]=m[5]=1.f/12;m[10]=1.f/1280;m[15]=1;}c[24][0]=.0001f;}
 static void drop(int*& value){if(value){delete value;value=nullptr;--live;}}
 @CREATE@
 void create(){actorShaderResources(fogCaps);}
 void bind(int source){@BIND@}
 void clear(){drop(actorShadowPS);drop(actorTilesPS);drop(actorUniformPS);drop(actorTilesWidePS);drop(fogActorTiledPS);drop(fogCloudsActorTiledPS);}
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
  r.create();assert(bool(r.actorShadowPS)==base&&bool(r.actorTilesPS)==base&&bool(r.actorUniformPS)==base);
  assert(bool(r.fogActorTiledPS)==tiled);assert(bool(r.fogCloudsActorTiledPS)==tiled&&bool(r.actorTilesWidePS)==tiled);
  const unsigned attempts=settings!=3?0:(fail&&fail<=3)?fail:eligible?(fail?fail:6):3;
  assert(r.device.attempts==attempts);assert(live==(base?3:0)+(tiled?3:0));
  if(base){r.create();assert(r.device.attempts==attempts&&live==3+(tiled?3:0));}
  r.clear();assert(!live);++cases;
 }
 for(unsigned flags=0;flags<64;++flags)for(int source=0;source<2;++source){
  Renderer r;r.fogActorCurrent=flags&1;r.actorTilesWideCurrent[source]=flags&2;
  if(flags&4){r.fogActorTiledPS=new int(1);++live;}if(flags&8){r.fogCloudsActorTiledPS=new int(2);++live;}
  if(!(flags&16))r.c[15][0]=1000;if(!(flags&32))r.c[24][0]=-.0001f;
  r.bind(source);const bool expected=flags==63;assert(r.fogActorTiledCurrent==expected&&"STALE_FOG_TILES");
  assert(r.device.texture==(expected?r.actorTiles[source]:r.textures[5])&&"WRONG_FOG_TILE_SOURCE");
  r.actorTilesWideCurrent[source]=false;r.bind(source);assert(!r.fogActorTiledCurrent&&r.device.texture==r.textures[5]);r.clear();assert(!live);++cases;
 }
 std::printf("PASS production tiled fog helper/binding: %u cases, actor switches/three-shader success boundary, six shader failures, caps and repeated initialization; fresh current-source maps and bounded coordinates required\n",cases);
}
'''.replace('@FOG@',str(manifest['WorldFogActorTiled']['static_instruction_slots'])).replace('@CLOUD@',str(manifest['FogCloudsActorTiled']['static_instruction_slots'])).replace('@CREATE@',creation).replace('@BIND@',binding)
with tempfile.TemporaryDirectory(prefix='fog-actor-tiles-') as temp:
    cpp=Path(temp)/'binding.cpp';cpp.write_text(fixture)
    for name,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(temp)/name
        subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(fp.TESTS/'test_fog_actor_tiles.cpp'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
        binding_exe=Path(temp)/('binding-'+name)
        subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(cpp),'-o',str(binding_exe)],check=True)
        subprocess.run([str(binding_exe)],check=True)
