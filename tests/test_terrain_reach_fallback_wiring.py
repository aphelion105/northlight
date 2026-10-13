#!/usr/bin/env python3
# northlight-test:
"""Static wiring audit: the geometry worker falls back to the base terrain reach on a memory
refusal, logs the stall episodes; stage entry uses buildBudget (768 MiB), interior allocations inBuildBudget."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re

w = fp.src('world_renderer.h').read_text()
g = fp.src('geometry_memory.h').read_text()
r = fp.src('renderer.cpp').read_text()
f = fp.src('terrain_reach_fallback.h').read_text()
assert 'constexpr uint64_t ProcessReserve=512*MiB;' in g and 'constexpr uint64_t ContiguousMargin=32*MiB;' in g
assert 'constexpr uint64_t EntryHeadroom=256*MiB;' in g and 'budgetWith(EntryHeadroom,largestAllocation)' in g and 'add(ProcessReserve,headroom,b.available)' in g and 'add(largestAllocation,ContiguousMargin,b.largest)' in g
# Allocations inside a build that passed buildBudget at entry use the smaller in-build headroom.
assert 'constexpr uint64_t InBuildHeadroom=128*MiB;' in g and 'budgetWith(largestAllocation>InBuildHeadroom?largestAllocation:InBuildHeadroom,largestAllocation)' in g
def body(start, end='};'):
    i = w.index(start); return w[i:w.index(end, i)]
for lam in ('NorthlightGI::AllocationAdmission localAdmission=', 'NorthlightGI::AllocationAdmission terrainAdmission='):
    assert 'NorthlightGeometryMemory::inBuildBudget(' in body(lam), lam
    assert 'NorthlightGeometryMemory::buildBudget(' not in body(lam), lam
# the after-bvh gate and the two stage-entry checks keep the full entry budget
assert 'NorthlightGeometryMemory::buildBudget(' in body('logGeometryMemory("after-bvh"', 'continue;')
assert 'inBuildBudget(' not in body('logGeometryMemory("after-bvh"', 'continue;')
entry = body('auto memory=workerMemory(NorthlightGeometryMemory::buildBudget(64', 'stallBegin("build-deferred"')
assert entry.count('NorthlightGeometryMemory::buildBudget(') >= 2 and 'inBuildBudget(' not in entry
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
