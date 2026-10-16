#!/usr/bin/env python3
# northlight-test: requires=cxx
"""World mesh buffer store (mesh_buffer_store.h, 0.3.216 issue #66): content-key lookup over active and idle buffers,
claims that never touch the active table, atomic commit, cancel with invalidated keys, shared duplicate pages, idle-only
trim order, and a randomised invariant run. Native clang++ with ASan/UBSan. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='mesh-buffer-store-') as temp:
    binary = Path(temp) / 'test'
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    *fp.test_include_flags(), str(HERE / 'test_mesh_buffer_store.cpp'), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], text=True, capture_output=True)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()

# Source wiring (world_renderer.h): the store replaces meshPool[2]; an active buffer is never written; claims are cancelled on every
# path that drops a pending staging; only the missed pages are uploaded and admitted.
w = fp.src('world_renderer.h').read_text()
for gone in ('meshPool[', 'activeMesh', 'stagingSlot', 'vbCapacity=0,ibCapacity=0;};'):
    assert gone not in w, gone
assert '#include "mesh_buffer_store.h"' in w and 'NorthlightMeshBufferStore::Store meshStore;' in w
acquire = w[w.index('bool acquireMeshPage(uint32_t id'):w.index('void releaseMeshPool()')]
assert 'if(!meshStore.writable(id)||id>=meshBuffers.size())return false;' in acquire and acquire.index('writable(id)') < acquire.index('Create')
bind = w[w.index('bool bindMeshPage('):w.index('std::vector<Batch> batches;')]
assert 'meshStore.bufferOf(batch.page)' in bind and 'meshBuffers[id]' in bind
upload = w[w.index('bool upload(NorthlightStreaming::Budget& streamBudget){'):w.index('if(!vertices||!indices||uploadedMap!=active->map)return false;')]
perform = upload[upload.index('auto perform=['):upload.index('while(pageBudget.available()')]
assert 'if(!meshStore.markWriting(id)||id>=meshBuffers.size())return false;' in perform and perform.index('markWriting') < perform.index('memcpy') < perform.index('Unlock')  # writable + key invalidated before the first write
assert 'staging.upload[cursor]' in perform and 'staging.uploadBuffer[cursor]' in perform  # the cursor runs over the miss list
assert upload.count('meshStore.beginStaging(pageKeys)') == 1 and upload.index('meshStore.beginStaging(pageKeys)') < upload.index('NorthlightGeometryMemory::admits(memory,budget)')
assert 'uploadBudget(missVertexBytes,missIndexBytes,plan.textureBytes,largestTexture)' in upload and 'for(uint32_t pageIndex:staged.upload)' in upload
commit = upload[upload.index('Keep the previous complete geometry drawable'):]
assert commit.count('meshStore.commit()') == 1 and commit.index('retiredMaterials.enqueue(materials,uploadedTextureBytes)') < commit.index('meshStore.commit()') < commit.index('plan.payload.discard()')
assert 'meshStore.bufferOf(0)' in commit and commit.index('meshStore.commit()') > commit.index('clearMesh();')
for field in ('pages=%zu', 'reusedPages=%zu', 'uploadedPages=%zu', 'reusedMiB=%.2f', 'uploadedMiB=%.2f'):
    assert field in commit, field
assert 'stagingMs=%lu releasedPayloadMiB=%.2f pages=%zu' in commit  # existing fields keep their order, new ones append
# Every path that drops a pending staging cancels its claims (partial content: idle, key invalid).
orphan = w[w.index('void releaseOrphanedPending('):w.index('void requestPayloadRebuild()')]
failure = w[w.index('bool streamingFailure('):w.index('bool uploadRegionalFog()')]
for body in (orphan, failure):
    assert 'meshStore.cancel();' in body and body.index('meshStore.cancel();') < body.index('pendingMesh.reset();')
release = w[w.index('void releaseGPU(bool keepMesh=false){'):]
release = release[:release.index('\n')]
assert 'if(!keepMesh){pendingMesh.reset();clearMesh();}' in release and 'if(!keepMesh){releaseMeshPool();uploadedMap.clear();}' in release  # releaseMeshPool resets the store, claims included
assert 'meshStore.reset();' in w[w.index('void releaseMeshPool()'):w.index('size_t trimMeshPools(')]
assert 'meshIdleMiB=%.1f' in w and 'meshStore.poolBytes()' in w
print('wiring ok')
