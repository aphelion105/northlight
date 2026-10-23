#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exercise the terrain metadata batch against the production cache and mutable D3D fixture."""
import ast
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

fixture=ast.parse((Path(__file__).parent/'test_terrain_snapshot.py').read_text())
values={n.targets[0].id:ast.literal_eval(n.value) for n in fixture.body if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('stub','harness')}
harness=values['harness'].split('int main(){')[0].replace('unsigned refs=1,locks=0,unlocks=0;', 'unsigned refs=1,locks=0,unlocks=0,descs=0;bool failDesc=false;')
harness=harness.replace('HRESULT GetDesc(Description* out)override{', 'HRESULT GetDesc(Description* out)override{++descs;if(failDesc)return D3DERR_INVALIDCALL;')
source=harness+r'''
static Device* device;
static uint64_t ids[]={11,12},versions[]={21,22};
static unsigned legacyCalls=0,batches=0,known=3,flipAt=0;
static uint64_t identity(void*,bool i){++legacyCalls;return ids[i];}
static uint64_t version(void*,bool i){++legacyCalls;return versions[i];}
static void metadata(const NorthlightCaptureMetadata::Request* r,NorthlightCaptureMetadata::Info* out,unsigned n){
 ++batches;if(batches==flipAt)++versions[0];assert(n==2);
 for(unsigned j=0;j<n;++j){const unsigned i=r[j].index;out[j]={};if(!(known&(1u<<i)))continue;
  assert(r[j].buffer==(i?static_cast<void*>(&device->ib):static_cast<void*>(&device->vb)));
  out[j]={ids[i],versions[i],unsigned(i?device->ib.bytes.size():device->vb.bytes.size()),i?device->ib.desc.Usage:device->vb.desc.Usage,i?device->ib.desc.Format:D3DFORMAT{},true};
 }
}
int main(){
 Device d;device=&d;float x=float(17066.666666666666-821*(100./3.)),y=float(17066.666666666666-548*(100./3.));
 d.put(4,{x,y,3});d.put(5,{x+30,y,4});d.put(6,{x,y+30,5});
 FrameCache cache;cache.setIdentityProvider(identity);cache.setVersionProvider(version);
 std::shared_ptr<const MeshSnapshot> a,b;Diagnostics why;
 auto read=[&]{return cache.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,b,&why);};
 assert(read());a=b;cache.clearFrame();legacyCalls=d.vb.descs=d.ib.descs=0;
 assert(read()&&why.trackedCacheHit&&legacyCalls==8&&d.vb.descs==1&&d.ib.descs==1);
 cache.setMetadataProvider(metadata);assert(read()&&!why.trackedCacheHit);a=b;
 cache.clearFrame();legacyCalls=batches=d.vb.descs=d.ib.descs=0;d.vb.failDesc=d.ib.failDesc=true;
 assert(read()&&why.trackedCacheHit&&b==a&&batches==2&&!legacyCalls&&!d.vb.descs&&!d.ib.descs);
 assert(cache.maintenance().metadataBatches==2&&cache.maintenance().metadataHits==2&&!cache.maintenance().metadataFallbacks);
 // Live writes and exact-byte revalidation retain the existing proof rules.
 d.put(4,{x,y,31});++versions[0];assert(read()&&!why.trackedCacheHit&&b->positions[0].z==31&&a->positions[0].z==3);a=b;
 ++versions[0];++versions[1];assert(read()&&why.contentCacheHit&&!why.trackedCacheHit&&b==a);
 ++ids[0];assert(read()&&!why.trackedCacheHit);
 versions[0]=0;assert(read()&&!why.trackedCacheHit);assert(read()&&!why.trackedCacheHit);versions[0]=40;
 // Partial/unknown metadata falls back per buffer, including original failures.
 known=1;assert(!read()&&why.reason==RejectReason::IndexDescription);d.vb.failDesc=d.ib.failDesc=false;
 assert(read());cache.clearFrame();legacyCalls=d.vb.descs=d.ib.descs=0;assert(read()&&why.trackedCacheHit&&d.vb.descs==0&&d.ib.descs==1&&legacyCalls==4);
 known=0;cache.clearFrame();legacyCalls=d.vb.descs=d.ib.descs=0;assert(read()&&d.vb.descs==1&&d.ib.descs==1&&legacyCalls>=4);
 // A post-read revision change forbids proving the snapshot as current.
 known=3;cache.clearPersistent();flipAt=batches+2;assert(read());flipAt=0;
 assert(read()&&!why.trackedCacheHit);assert(read()&&why.trackedCacheHit);
 cache.setMetadataProvider(nullptr);assert(read()&&!why.trackedCacheHit);cache.clearFrame();legacyCalls=0;assert(read()&&why.trackedCacheHit&&legacyCalls==8);
 cache.setMetadataProvider(metadata);cache.setVersionProvider(nullptr);cache.clearFrame();d.vb.descs=d.ib.descs=batches=0;assert(read()&&!batches&&d.vb.descs>=1&&d.ib.descs>=1);
 assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
 std::puts("PASS terrain metadata: 2 GetDesc + 8 legacy reads -> 2 batches; revisions, unknown buffers, failures, reset and COM balance");
}
'''
with tempfile.TemporaryDirectory(prefix='terrain-metadata-') as temp:
    p=Path(temp);(p/'d3d9.h').write_text(values['stub']);(p/'test.cpp').write_text(source)
    for flags in (['-O2'],['-O0','-g']):
        binary=p/'test'
        subprocess.run(['clang++','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,'-I',str(p),*fp.test_include_flags(),str(p/'test.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
