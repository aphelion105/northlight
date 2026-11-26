#!/usr/bin/env python3
# northlight-test:
"""0.3.151 buffer Lock audit (source only, no game, Wine or GPU). Every IDirect3D*Buffer9 Lock in
the renderer sources is listed below with the creation that decides its DXVK map mode. A flags-0
lock of a DEFAULT|WRITEONLY buffer is a DIRECT mapping without a sequence number: it drains the
CS thread (upload_lock.h). Such buffers are fresh and write-once and lock with
NorthlightUpload::FreshBufferLock; a new DEFAULT buffer or Lock fails here until it is classified."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import re
HERE=Path(__file__).resolve().parent
F='NorthlightUpload::FreshBufferLock'
def arguments(text,open_at):
    depth=0;out=[];cur=''
    for ch in text[open_at:]:
        if ch=='(':
            depth+=1
            if depth==1:continue
        elif ch==')':
            depth-=1
            if depth==0:out.append(cur.strip());return out
        elif ch==',' and depth==1:out.append(cur.strip());cur='';continue
        cur+=ch
    raise AssertionError('unbalanced call')
sources={f.name:f.read_text(errors='replace') for f in fp.sources(('.h','.inl','.cpp'))}
# (file, receiver) -> (flags, class, creation evidence in the same file or None)
Fresh,Managed,Dynamic,Read,Forward,Pooled='fresh DEFAULT|WRITEONLY','MANAGED','DEFAULT|DYNAMIC','READONLY','game flags','MANAGED or immutable DEFAULT|DYNAMIC pooled pages'
expected={
 ('imgui_impl_dx9.cpp','bd->pVB'):('D3DLOCK_DISCARD',Dynamic,'D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_CUSTOMVERTEX, D3DPOOL_DEFAULT, &bd->pVB'),
 ('imgui_impl_dx9.cpp','bd->pIB'):('D3DLOCK_DISCARD',Dynamic,'D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32, D3DPOOL_DEFAULT, &bd->pIB'),
 ('model_normal_renderer.h','streamVB'):('vslot.flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&streamVB'),
 ('model_normal_renderer.h','streamIB'):('islot.flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&streamIB'),
 ('model_normal_palette.h','buffer'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,buffer.out()'),
 ('game_indexed_ranges.h','created'):(F,Fresh,'D3DUSAGE_WRITEONLY,info.format,D3DPOOL_DEFAULT,created.out()'),
 ('game_particle_atlas.h','vertices'):('discard?D3DLOCK_DISCARD:D3DLOCK_NOOVERWRITE',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,vertices.out()'),
 ('game_particle_pair.h','buffer'):('D3DLOCK_READONLY',Read,None),
 ('scene_native_store.h','b'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&built.vb[s]'),
 ('scene_native_store.h','batch->vb'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&batch->vb'),
 ('scene_native_store.h','batch->ib'):(F,Fresh,'D3DUSAGE_WRITEONLY,narrow?D3DFMT_INDEX16:D3DFMT_INDEX32,D3DPOOL_DEFAULT,&batch->ib'),
 ('replay_gpu_batches.h','b'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&built->vertices[s]'),
 ('replay_gpu_batches.h','batch->vertices'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&batch->vertices'),
 ('replay_gpu_batches.h','batch->indices'):(F,Fresh,'D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&batch->indices'),
 ('replay_gpu_cache.h','built->vertices[s]'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&built->vertices[s]'),
 ('replay_gpu_cache.h','built->indices'):(F,Fresh,'D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&built->indices'),
 ('static_shadow_gpu.h','r.vb'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&r->vb'),
 ('static_shadow_gpu.h','r.ib'):(F,Fresh,'D3DFMT_INDEX16:D3DFMT_INDEX32,D3DPOOL_DEFAULT,&r->ib'),
 ('world_mesh_pool.h','page.vb'):('flags',Pooled,'CreateVertexBuffer(capacity,usage,0,pool,&slot.vb'),
 ('world_mesh_pool.h','page.ib'):('flags',Pooled,'CreateIndexBuffer(capacity,usage,D3DFMT_INDEX16,pool,&slot.ib'),
 ('live_terrain_gpu.h','vertices_'):('discarding?D3DLOCK_DISCARD:D3DLOCK_NOOVERWRITE',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vertices_'),
 ('static_shadow_gpu.h','instances_'):('flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&instances_'),
 ('live_terrain_indices.h','buffer_'):('slot.flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&buffer_'),
 ('shadow_palette_pass.h','ids'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,ids.out()'),
 ('point_replay_indices.h','buffer'):(F,Fresh,'D3DUSAGE_WRITEONLY,narrow?D3DFMT_INDEX16:D3DFMT_INDEX32,D3DPOOL_DEFAULT,buffer.out()'),
 ('world_replay_buffers.h','vertices_[s]'):('slot.flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vertices_[s]'),
 ('world_replay_buffers.h','indices_'):('slot.flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&indices_'),
 ('replay_copy_reader.h','b'):('NorthlightUpload::readBackLock()',Read,None), # 0.3.192 (CS): the 8 capture read-backs go through NorthlightReplayCopies::Reader (CPU copy first, this lock otherwise)
 ('spell_layer.h','upVb_'):('flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&upVb_'), # task 34: the layer's own replay upload buffers, DISCARD on the first upload of a frame / after a wrap, NOOVERWRITE appends after (see the audit below)
 ('spell_layer.h','upIb_[fmt]'):('flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,fmt?D3DFMT_INDEX32:D3DFMT_INDEX16,D3DPOOL_DEFAULT,&upIb_[fmt]'), # one per index format
 ('ui_draw_diagnostic.h','buffer'):('out.flags',Read,None),
 ('auto_displacement_renderer.h','entry->vb'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&entry->vb'),
 ('auto_displacement_renderer.h','entry->ib'):(F,Fresh,'D3DUSAGE_WRITEONLY,indexBytes==sizeof(uint16_t)?D3DFMT_INDEX16:D3DFMT_INDEX32,D3DPOOL_DEFAULT,&entry->ib'),
 ('shared_pom.h','e->vb'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&e->vb'),
 ('forwarders.h','real'):('Flags',Forward,None),('tracked_buffers.h','this->real'):('flags',Forward,None),
}
seen=set();fresh=calls=0
for name,text in sources.items():
    for m in re.finditer(r'([A-Za-z_][\w.\->\[\]]*?)(?:->|\.)Lock\(',text):
        line=text.count('\n',0,m.start())+1;flags=arguments(text,m.end()-1)[-1];key=(name,m.group(1))
        assert key in expected,f'{name}:{line}: unclassified buffer Lock of {key[1]} with {flags}: add it to test_upload_locks.py'
        want,kind,created=expected[key];seen.add(key)
        assert flags==want,f'{name}:{line}: {key[1]} ({kind}) locks with {flags}, expected {want}'
        assert created is None or created in text,f'{name}:{line}: {key[1]} is no longer created as {kind}: {created}'
        fresh+=flags==F;calls+=1
assert seen==set(expected),f'stale entries: {sorted(set(expected)-seen)}'
if 'ui_draw_diagnostic.h' in sources:
    assert 'out.flags=NorthlightUpload::readBackLock();' in sources['ui_draw_diagnostic.h']
assert fresh==17
# spell_layer.h upload(): DISCARD only while *Fresh_ (first of the frame, or after a wrap), NOOVERWRITE otherwise; beginFrame and the wrap re-arm Fresh_, so an append never overlaps submitted draws' bytes
sl=sources['spell_layer.h']
assert 'const DWORD flags=fresh?D3DLOCK_DISCARD:D3DLOCK_NOOVERWRITE;' in sl and 'cursor-=base;base=0;fresh=true;' in sl
assert 'vbFresh_=ibFresh_[0]=ibFresh_[1]=true;' in sl and sl.count('resetUploadCursors();')>=2
print(f'PASS lock audit: {calls} buffer Lock calls in {len(expected)} classified sites, {fresh} fresh DEFAULT locks use {F}')
# Every DEFAULT buffer without DYNAMIC is one of the fresh write-once sites above.
creates=[]
for name,text in sources.items():
    for m in re.finditer(r'(?:->|\.)(Create(?:Vertex|Index)Buffer)\(',text):
        a=arguments(text,m.end()-1)
        if len(a)!=6 or (a[0] in ('size','Length','v[0]') and name not in ('scene_native_store.h','game_indexed_ranges.h')):continue # the device wrappers and the stream's replay forward the game's own arguments
        pool,usage=a[3],a[1]
        if name=='world_mesh_pool.h' and a[4] in ('&slot.vb','&slot.ib'):
            assert pool=='pool' and usage=='usage'
            assert 'const DWORD usage=D3DUSAGE_WRITEONLY|(pool==D3DPOOL_DEFAULT?D3DUSAGE_DYNAMIC:0);' in text
            assert 'D3DPOOL pool=D3DPOOL_MANAGED' in sources['world_renderer.h']
            continue # explicitly classified pooled creations; Lock(0) synchronizes reuse
        if 'D3DPOOL_DEFAULT' in pool and 'D3DUSAGE_DYNAMIC' not in usage:creates.append((name,a[4]))
        else:assert 'D3DPOOL_MANAGED' in pool or 'D3DUSAGE_DYNAMIC' in usage,(name,a)
assert sorted(creates)==sorted([('scene_native_store.h','&built.vb[s]'),('scene_native_store.h','&built.ib'),('scene_native_store.h','&batch->vb'),('scene_native_store.h','&batch->ib'),('replay_gpu_batches.h','&built->vertices[s]'),('replay_gpu_batches.h','&built->indices'),('replay_gpu_batches.h','&batch->vertices'),
    ('replay_gpu_batches.h','&batch->indices'),('replay_gpu_cache.h','&built->vertices[s]'),('replay_gpu_cache.h','&built->indices'),('auto_displacement_renderer.h','&entry->vb'),('auto_displacement_renderer.h','&entry->ib'),('shared_pom.h','&e->vb'),('static_shadow_gpu.h','&r->vb'),('static_shadow_gpu.h','&r->ib'),('shadow_palette_pass.h','ids.out()'),
    ('point_replay_indices.h','buffer.out()'),('model_normal_palette.h','buffer.out()'),('game_indexed_ranges.h','created.out()')]),creates
print(f'PASS DEFAULT|WRITEONLY creations: {len(creates)}, all fresh write-once sites')
# The constant and its kill switch; FreshBufferLock appears nowhere else.
upload=fp.src('upload_lock.h').read_text()
assert 'inline constexpr bool NoOverwriteFreshBuffers=true;' in upload
assert 'inline constexpr DWORD FreshBufferLock=NoOverwriteFreshBuffers?DWORD(D3DLOCK_NOOVERWRITE):DWORD(0);' in upload
assert {n for n,t in sources.items() if 'FreshBufferLock' in t}=={'upload_lock.h','replay_gpu_batches.h','replay_gpu_cache.h','scene_native_store.h','auto_displacement_renderer.h','shared_pom.h','world_mesh_pool.h','static_shadow_gpu.h','shadow_palette_pass.h','point_replay_indices.h','model_normal_palette.h','game_indexed_ranges.h'}
assert '#include "upload_lock.h"' in sources['world_mesh_pool.h']
# Publication strictly after the last Unlock of each fresh buffer; nothing else reaches an unpublished one.
def body(text,start,end):i=text.index(start);return text[i:text.index(end,i)]
commit=body(sources['replay_gpu_batches.h'],'    void commit(){','    void compact(){')
assert commit.count('->Lock(')==2 and commit.rindex('->Unlock()')<commit.index('batches_.push_back(std::move(batch));')<commit.index('e.batch=b;')
separate=body(sources['replay_gpu_batches.h'],'    bool storeSeparate(','    // The reserved admissions')
assert separate.rindex('write(')<separate.index('std::swap(e.vertices[s],built->vertices[s]);')
cache=body(sources['replay_gpu_cache.h'],'auto built=std::make_unique<Entry>();','}else{reused_+=e.bytes;++hits_;}')
assert cache.count('->Lock(')==2 and cache.rindex('->Unlock()')<cache.index('std::swap(e.vertices[s],built->vertices[s]);')
native=sources['scene_native_store.h']
write=body(native,' template<class Buffer>static bool write(', ' static bool batchable(')
separateNative=body(native,' bool separate(', ' void commitPrepared(')
commitNative=body(native,' void commitPrepared(', ' void commit(')
assert write.index('b->Unlock()') < write.index('return out&&SUCCEEDED(hr);')
assert separateNative.rindex('write(') < separateNative.index('std::swap(e.vb[s],built.vb[s])')
assert commitNative.rindex('->Unlock()') < commitNative.index('batches_.push_back(std::move(batch))')
joined=sources['game_indexed_ranges.h']
assert joined.count('->Lock(')==1 and joined.index('created->Unlock()')<joined.index('std::swap(created.p,oldest->buffer.p)')
print('PASS fresh buffers: replay batches/meshes are published only after their last Unlock and never re-locked')
# POM entries allocate and fill privately once; cache hits bind their immutable buffer.
pom=sources['shared_pom.h'];prepare=body(pom,'    bool prepare(','    IDirect3DSurface9* target(')
build=body(prepare,'        if(!current){','        if(nextTag>')
assert pom.count('->Lock(')==1 and build.count('->Lock(')==1 and build.count('->Unlock()')==1
assert build.index('auto e=std::make_unique<Entry>();')<build.index('CreateVertexBuffer(')<build.index('e->vb->Lock(')<build.index('e->vb->Unlock()')<build.index('CreateVertexDeclaration(')<build.index('current=e.get();entries.push_back(std::move(e));')
assert 'IDirect3DVertexBuffer9* vb=nullptr;' in pom and '~Entry(){drop(sourceDecl);drop(decl);drop(vb);}' in pom
assert 'FAILED(e->vb->Unlock())||FAILED(d->CreateVertexDeclaration(layout.data(),&e->decl)))return false;' in build
assert '}catch(...){current=nullptr;return false;}' in prepare and 'entries.clear();' in pom and 'entries.erase(old);' in build
assert 'd->SetStreamSource(0,owner.current->vb,0,owner.current->stride)' in pom
print('PASS POM fresh buffer: private allocation/one fill, checked Unlock before publication, RAII failure/eviction/reset release, cache hits never re-lock')
# 0.3.192 (DXVK3): every READONLY buffer read-back goes through readBackLock(); the definition, the gate and the absence of bare flags.
assert 'inline std::atomic<bool> ReadBackNoOverwrite{false};' in upload
assert 'inline DWORD readBackLock(){return DWORD(D3DLOCK_READONLY)|(ReadBackNoOverwrite.load(std::memory_order_relaxed)?DWORD(D3DLOCK_NOOVERWRITE):0u);}' in upload
assert upload.count('readBackLock()')>=1 and upload.count('D3DLOCK_READONLY)|')==1 # READONLY is always kept: tracked_buffers.h keys the revision on it
world_dir=fp.SRC/'world'
bare=[(f.name,text.count('\n',0,m.start())+1) for f in sorted(world_dir.iterdir()) if f.suffix in('.h','.inl','.cpp')
      for text in [f.read_text(errors='replace')] for m in re.finditer(r'(?:->|\.)Lock\(',text) if 'D3DLOCK_READONLY' in arguments(text,m.end()-1)[-1]]
assert not bare,f'bare D3DLOCK_READONLY buffer Lock in src/world (use NorthlightUpload::readBackLock()): {bare}'
sites=('draw_snapshot.h','geometry_capture.h','terrain_capture_bounds.h')
assert sum(sources[n].count('NorthlightUpload::readBackLock()') for n in sites)==0 # no capture site locks a game buffer itself any more
readers=sum(sources[n].count('NorthlightReplayCopies::Reader<') for n in sites)
assert readers==8,readers # 4 + 2 + 2 read-back sites, every one through the reader (replay_copy_reader.h)
reader=sources['replay_copy_reader.h']
assert reader.count('NorthlightUpload::readBackLock()')==2 and reader.count('->Lock(')==2 # the whole-buffer fill and the ordinary fallback lock
assert reader.index('if(enabled.load(std::memory_order_relaxed)&&b){')<reader.index('->Lock(0,slot->size') # the stream gate comes first: with it off only the fallback runs
renderer=sources['renderer.cpp']
backend=sources['renderer_backend_windows.h']
gate='NorthlightUpload::ReadBackNoOverwrite.store(module&&!result.fallback&&last.info.dxvk&&NorthlightBackend::dxvkMajor(last.info.dxvkVersion)>=3,std::memory_order_relaxed);'
stores=[n for n,t in sources.items() if re.search(r'ReadBackNoOverwrite(?:\.store\(|\s*=[^=])',t.replace('inline std::atomic<bool> ReadBackNoOverwrite{false};',''))]
assert stores==['renderer_backend_windows.h'] and backend.count('ReadBackNoOverwrite.store(')==1 and backend.count(gate)==1,stores
# The gate runs after the BACKEND selected= log (the loaded module decides) and before CreateDevice can run.
assert backend.index('BACKEND selected=')<backend.index(gate)<backend.index('readBackLock=0x%x')
print('PASS read-back lock: readBackLock() defined once, 8 read-back sites, no bare READONLY buffer Lock in src/world, gate stored once from the loaded DXVK >= 3 backend')
# 0.3.192 (CS): the replay-side CPU copies (replay_copies.h): one choke point (the tracked wrapper), gated on the stream, pressure-shrunk once per frame.
tracked=sources['tracked_buffers.h'];copies=sources['replay_copies.h']
assert tracked.count('NorthlightReplayCopies::writeLocked(')==1 and tracked.count('NorthlightReplayCopies::beforeUnlock(')==1 # the only two write hooks: Lock records the range, Unlock copies it out of the mapped pointer
assert tracked.index('NorthlightReplayCopies::beforeUnlock(')<tracked.index('this->real->Unlock()') # before the pointer is released
written=body(tracked,'inline void written(', 'inline void markSpellGuard(')
assert 'NorthlightReplayCopies::invalidate(r.copy,true)' in written
process_vertices=body(renderer,'    HRESULT STDMETHODCALLTYPE ProcessVertices(', '    HRESULT STDMETHODCALLTYPE CreateVertexShader(')
mark_written='NorthlightTrackedBuffers::written(buffer);'
assert process_vertices.count(mark_written)==2
assert process_vertices.index(mark_written)<process_vertices.index('ext->ProcessVertices(')<process_vertices.rindex(mark_written)
reset=tracked[tracked.index('inline void invalidateAll('):tracked.index('template<class T,class Forward> class Buffer')]
assert 'NorthlightReplayCopies::invalidate(r.copy)' in reset
assert '!preserveManaged||!r.poolKnown||r.pool!=D3DPOOL_MANAGED||r.locks.load(std::memory_order_relaxed)' in reset
assert tracked.count('NorthlightReplayCopies::enabled.load(')>=3 # attach, Lock and Unlock are all behind the stream gate
enable='NorthlightReplayCopies::enabled.exchange(true,std::memory_order_relaxed)'
assert renderer.index('\n        HRESULT hr=real->CreateDevice(adapter,type,window,flags,pp,out)')<renderer.index(enable)<renderer.index('new Device(*out,this,presentation)')
assert 'streamSetup(stream&&SUCCEEDED(hr)&&out&&*out)' in renderer and 'NorthlightReplayCopies::setPressure(memoryCaps==1)' in renderer
assert [(n,t.count(enable)) for n,t in sources.items() if enable in t]==[('renderer.cpp',1)]
# every write a stream replays into a game buffer is a Lock/Unlock on the stream's inner object, which is the wrapper (Device::Create*Buffer wraps)
replay=sources['replay_thread.h']
assert 'static_cast<IDirect3DVertexBuffer9*>(p->inner)->Lock(off,size,&dst,flags)' in replay and replay.count('->Lock(off,size,&dst,flags)')==2 and 'writeBuffer(a->proxy,a->off,a->size,a->flags,a->src+a->off,a->firstSize)' in replay and 'static_cast<IDirect3DVertexBuffer9*>(p->inner)->Unlock()' in replay
dev=renderer[renderer.index('class Device final'):renderer.index('class Factory final')]
assert 'ext->CreateVertexBuffer(' in dev and 'NorthlightTrackedBuffers::wrap<IDirect3DVertexBuffer9' in dev and 'NorthlightTrackedBuffers::wrap<IDirect3DIndexBuffer9' in dev
assert dev.count('NorthlightTrackedBuffers::unwrap(buffer)')==1 # ProcessVertices is the only unwrapping use of a game buffer for a write
# R1: the capture holds the RAW buffer (ext->GetStreamSource/GetIndices after Device::SetStreamSource/SetIndices unwrap): the copy registry must resolve raw pointers too
assert 'attach(record.copy,record.raw,record.exposed,this,' in tracked and 's.registry[raw]=&c;s.registry[exposed]=&c;' in copies and 's.registry.erase(c.raw);s.registry.erase(c.exposed)' in copies
assert 'static_cast<T*>(this),record.metadata.size' not in tracked # not keyed by the wrapper pointer only
# R2: the thrash guard and the deferred fill are measured in frames, advanced once per frame on the replay thread (WorldRenderer::endFrame), never in reads
assert 'kThrashReads' not in copies and 'kBackoffReads' not in copies and 's.tick' not in copies and 'if(c.frames<2)return false;' in copies and 'c.frames=0;' in copies
assert sources['world_renderer.h'].count('NorthlightReplayCopies::advanceFrame()')==1
# R3: a pin holds a COM reference on the wrapper that owns the slot; the last Release happens outside the store mutex
assert 'c.ref(c.owner,true)' in copies and copies.count('ref(owner,false)')==2
print('PASS replay copies wiring: gate set before the first buffer, wrapper hooks, ProcessVertices/Reset invalidation, pressure shrink')

# A staged pool entry can have been drawn by an older generation. Lock(0)
# retains driver synchronization before reuse; NOOVERWRITE would be unsafe.
page_stage=sources['world_mesh_publication.inl']
page_create=sources['world_mesh_pool.h']
assert 'pool==D3DPOOL_DEFAULT?D3DUSAGE_DYNAMIC:0' in page_create
assert 'store.writable(id)' in page_create
assert 'payload.pages->defaultPool()?D3DPOOL_DEFAULT:D3DPOOL_MANAGED' in page_stage
assert 'slot.vbFresh=true;' in page_create and 'slot.ibFresh=true;' in page_create
assert '(vertex?page.vbFresh:page.ibFresh)&&pool==D3DPOOL_DEFAULT?NorthlightUpload::FreshBufferLock:DWORD(0)' in page_create
page_commit=page_stage[page_stage.index('const auto stagedPages=meshStore.commit();'):]
assert 'for(auto id:meshStore.table())if(id<meshBuffers.size())meshBuffers[id].vbFresh=meshBuffers[id].ibFresh=false;' in page_commit
print('PASS pooled pages: fresh DEFAULT NOOVERWRITE only before first publication; synchronized Lock(0) for drawable reuse')
