#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Native test of the world-mesh pool trim decision (surplus over twice the windowed largest mesh, consecutive-commit hysteresis, bounded release per commit)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='mesh-pool-trim-') as temp:
    binary = Path(temp) / 'test'
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    *fp.test_include_flags(), str(HERE / 'test_mesh_pool_trim.cpp'), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], text=True, capture_output=True)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()

# Source wiring: the decision runs once per staged commit, after pendingMesh is cleared; the guard keeps its rule.
w = fp.src('world_renderer.h').read_text()
assert '#include "mesh_pool_trim.h"' in w
commit = w[w.index('WORLD staged mesh committed'):]
commit = commit[:commit.index('if(!vertices||!indices||uploadedMap!=active->map)return false;')]
assert commit.count('trimMeshPoolAfterCommit(stagedPages.uploadedCapacity);') == 1
assert commit.index('pendingMesh.reset();') < commit.index('trimMeshPoolAfterCommit(stagedPages.uploadedCapacity);')
assert 'size_t trimMeshPools(uint64_t maxBytes=UINT64_MAX){\n        if(pendingMesh)return 0;' in w
# 0.3.216: only idle store buffers are released (the store skips active and claimed ones and refuses while a staging is open)
assert 'meshStore.trimIdle(maxBytes,[&](uint32_t id){if(id<meshBuffers.size()){drop(meshBuffers[id].vb);drop(meshBuffers[id].ib);}})' in w
assert 't.gpu+=trimMeshPools();' in fp.src('world_memory_guard.inl').read_text()
assert 'trimMeshPools(request)' in w and 'meshPoolTrim.onCommit(pool,mesh,stagedCapacity)' in w and 'uint64_t maxBytes=UINT64_MAX' in w
assert 'const uint64_t pool=meshStore.poolBytes(),mesh=meshStore.activeBytes();' in w # pool: all buffers; mesh: the active table's
assert 'WORLD mesh pool trimmed freedMiB=%.1f poolMiB=%.1f meshMiB=%.1f' in w
print('wiring ok')
