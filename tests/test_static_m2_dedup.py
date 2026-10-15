#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Static M2 dedup (static_m2_dedup.h): native proof of the decision table (match, model mismatch,
animated, cache invalid, bounds outside, skinned), the opaque vertex coverage proof and the stability
memory, plus the renderer wiring invariants. Never launches game/Wine."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
src=fp.src('world_renderer.h').read_text()
gpu=fp.src('static_shadow_gpu.h').read_text()
# The M2 skip is its own mask: the WMO proof path (exact triangles) is untouched.
assert 'for(auto& p:replays)p->staticProofMask=p->staticM2Mask=0;' in src
assert 'try{prepareStaticM2();}catch(...){for(auto& p:replays)p->staticM2Mask=0;}' in src
loop=src[src.index('A rigid M2 doodad at its resting pose is in this slot'):]
loop=loop[:loop.index('++replaySlotTested[slot]')]
# After the actor-map decision (a doodad the actor map takes is not skipped here) and the bounds test against the cached matrix.
assert 'StaticShadow::containsBounds(cachedMatrix,p->staticProofLow,p->staticProofHigh)' in loop and 'key.valid' in loop and 'readyModels' in loop
assert src.index('if(inActor){actorReplays.push_back')<src.index('A rigid M2 doodad at its resting pose is in this slot')
# Verification gates: audited rigid program, opaque, shared snapshot, placement match, vertex coverage, residency every frame.
body=src[src.index('void prepareStaticM2(){'):src.index('uint64_t staticSignature(const float* matrix){')]
for needle in ('rigidProgram(p.originalShader)','p.cutoff>=0','!p.shared','NorthlightRigidGeometry::staticPlacement(','StaticM2Dedup::covers(','StaticM2Dedup::inside(',
               'staticCasters.coverageAt(','StaticM2Dedup::decide(facts)!=StaticM2Dedup::Verdict::Skip','rigidIndexCurrent()'):
    assert needle in body,needle
assert 'm2Skipped=%u m2Verified=%u m2Rejected=%u' in src and 'staticM2Skipped=%llu,%llu,%llu,%llu' in src
assert 'bool coverageAt(size_t index,const Placement& placement,uint64_t revision)const' in gpu
with tempfile.TemporaryDirectory() as t:
    exe=Path(t)/'m2'
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',*fp.test_include_flags(),str(HERE/'test_static_m2_dedup.cpp'),str(fp.src('static_shadow_scene.cpp')),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
    out=subprocess.run([str(exe)],check=True,capture_output=True,text=True).stdout
    assert 'PASS static M2 dedup' in out,out
    print(out.strip())
print('PASS static M2 dedup wiring and native proof')
