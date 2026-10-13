#!/usr/bin/env python3
# northlight-test:
"""Static wiring audit: the geometry worker falls back to the base terrain reach on a memory
refusal, logs the stall episodes, and no admission margin changed."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re

w = fp.src('world_renderer.h').read_text()
g = fp.src('geometry_memory.h').read_text()
r = fp.src('renderer.cpp').read_text()
f = fp.src('terrain_reach_fallback.h').read_text()
assert 'constexpr uint64_t ProcessReserve=512*MiB;' in g and 'constexpr uint64_t ContiguousMargin=32*MiB;' in g
assert 'b.available=ProcessReserve+256*MiB;' in g and 'add(largestAllocation,ContiguousMargin,b.largest)' in g
# Allocations inside a build that passed buildBudget at entry use the smaller in-build headroom.
assert 'constexpr uint64_t InBuildHeadroom=128*MiB;' in g and 'b.available=ProcessReserve+InBuildHeadroom;' in g
assert w.count('NorthlightGeometryMemory::inBuildBudget(') == 5 and w.count('NorthlightGeometryMemory::inBuildBudget(bytes)') == 4
wc = w.replace('\n', ' ')
for lam in ('localAdmission', 'terrainAdmission'):
    assert re.search(r'AllocationAdmission %s=\[&\]\(uint64_t bytes\)\{[^}]*?workerMemory\(NorthlightGeometryMemory::inBuildBudget\(bytes\)\)\s*;\s*if\(NorthlightGeometryMemory::admits\(sample,NorthlightGeometryMemory::inBuildBudget\(bytes\)\)\)' % lam, wc), lam
assert re.search(r'logGeometryMemory\("after-bvh"[^;]*;\s*if\(!NorthlightGeometryMemory::admits\(memory,NorthlightGeometryMemory::inBuildBudget\(\)\)\)\{\s*replacement\.reset\(\);stallBegin\("plan"', wc)
# stage-entry checks keep the full 768 MiB budget
assert w.count('admits(memory,NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB))') == 2
assert 'workerMemory(NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB))' in w
assert 'localSample=sample' in w and 'plan-deferred' in w
assert 'BaseReach=928.f' in f and 'BackoffMs=30000' in f and '#include <windows.h>' not in f
st = fp.src('shadow_terrain.h').read_text()
assert 'constexpr float Radius=928.f;' in st
assert '#include "terrain_reach_fallback.h"' in w
for stage in ('shadow-terrain', 'mesh-page"', 'mesh-page-seal'):
    assert re.search(r'stallBegin\("%s,?[^;]*;\s*if\(reduceReach\(\)\)goto terrainStage;' % re.escape(stage.rstrip('"')), w.replace('\n', ' ')), stage
assert w.count('goto terrainStage') == 3 and w.count('terrainStage:') == 1
assert 'NorthlightTerrainReach::choose(reachState,profileReach' in w
assert 'reach=reachChoice.reach;' in w and 'const bool extended=reach>NorthlightShadowTerrain::Radius;' in w
for text in ('WORLD geometry memory stall begin stage=%s availableMiB=%llu largestMiB=%llu generations=%zu',
             'WORLD geometry memory stall end ms=%u attempts=%u outcome=%s',
             'WORLD shadow terrain reach reduced from=%.0f to=%.0f reason=memory',
             'WORLD shadow terrain reach restored to=%.0f'):
    assert text in w, text
assert 'stallEnd(memoryStall.reduced?"reduced":"published")' in w and w.count('stallEnd("superseded")') == 3
assert 'WORLD skip episode begin reason=%s' in r and 'WORLD skip episode reason=%s last=%s frames=%u ms=%lu' in r
assert '#include "terrain_reach_fallback.h"' in (Path(__file__).parent / 'test_concurrent_geometry_build.py').read_text()
assert 'stallEnd("error")' in w and 'if(abandoned)stallEnd("superseded")' in w
print('terrain reach fallback wiring PASS')
