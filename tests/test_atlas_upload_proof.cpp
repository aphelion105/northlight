// SPDX-License-Identifier: MIT
#include "atlas_upload_proof.h"
#include <cassert>
#include <vector>
#include <cstdio>
using namespace NorthlightAtlasProof;
static const void* key(unsigned n){return reinterpret_cast<const void*>(std::uintptr_t(n));}
int main(){
    enabled=true;Cache cache;constexpr unsigned fmt=0x31545844,pitch=144;
    std::vector<unsigned char> tile(pitch*16,0xee),packed(128*16);
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<128;++x)packed[y*128+x]=tile[y*pitch+x]=static_cast<unsigned char>(x+y*11);
    auto put=[&](unsigned id,unsigned cell,bool success=true){cache.upload(key(id),key(id+100),fmt,512,512,0,cell%8*64,cell/8*64,(cell%8+1)*64,(cell/8+1)*64,tile.data(),tile.size(),pitch,success,0x8007000e);};
    cache.upload(key(1),key(101),fmt,512,512,0,0,0,512,512,tile.data(),tile.size(),pitch,true);assert(!cache.find(key(1)));
    put(1,5);auto* e=cache.find(key(1));assert(e&&e->known==(1ull<<5)&&e->tiles[5]==hashRows(packed.data(),128,16,128));
    put(1,6,false);assert(e->failedUploads==1&&e->lastFailure==0x8007000e&&e->known==((1ull<<5)|(1ull<<6)));
    std::array<Entry,4> recent{};assert(!cache.recent(recent,1));cache.viewed(key(1),10);assert(cache.recent(recent,12)==1&&!cache.recent(recent,13));
    cache.invalidate(key(1));assert(!e->known&&e->invalidations==1&&e->failedUploads==1);
    put(1,2);cache.upload(key(1),key(999),fmt,512,512,0,0,0,64,64,tile.data(),tile.size(),pitch,true);assert(e->owner==key(999)&&e->known==1&&!e->failedUploads);
    cache.upload(key(1),key(999),fmt,512,512,0,0,0,64,64,tile.data(),1,pitch,true);assert(!e->known);
    cache.erase(key(1));assert(!cache.find(key(1)));
    for(unsigned n=1;n<=Capacity+1;++n)put(n,0);assert(!cache.find(key(1))&&cache.find(key(Capacity+1)));
    enabled=false;put(999,0);assert(!cache.find(key(999)));
    std::puts("test_atlas_upload_proof: passed");
}
