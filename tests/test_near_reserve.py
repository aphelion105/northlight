#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.172 near capture reserve: native model test of near_reserve.h (clang++, plain and ASan/UBSan)
and a wiring audit of world_renderer.h / world_memory_guard.inl: the near test runs only on the two
budget branches of captureModel (captureExhausted and a read refused for Budget), the read is
retried at most once, the reserve is 4 MiB and 0 under memory pressure or with ActorShadows=0.
The Frame budget itself is tested in test_draw_snapshot.py. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text();g=fp.src('world_memory_guard.inl').read_text();r=fp.src('near_reserve.h').read_text()
capture=w[w.index('    void captureModel(D3DPRIMITIVETYPE type,'):w.index('    // One directional replay draw in the 0.3.142 order')]
checks={}
checks['near reserve is asked only on the two budget branches (0.3.210: the admission also passes near hits, nearDraw)']=(capture.count('nearCandidate(')==2 and w.count('nearCandidate(')==3 and w.count('nearDraw(')==3
    and 'if(replaySnapshots.captureExhausted(priority)){const bool candidate=nearCandidate(priority,rooted?root:nullptr);' in capture
    and 'if(!captured&&why.error==NorthlightDrawSnapshot::Error::Budget&&!nearby&&nearCandidate(priority,rooted?root:nullptr)){' in capture)
checks['skinned draws only, never past the draw-count cap, only with a reserve']='return priority&&replaySnapshots.nearReserve()&&!replaySnapshots.countExhausted(priority)&&nearDraw(root);}' in w
checks['the read is retried once (two calls, no loop)']=(len(re.findall(r'\bread\(\)',capture))==2 and capture.count('bool captured=read();')==1
    and re.search(r'\b(for|while)\s*\([^\n]*\bread\(\)',capture) is None)
checks['the retry is flagged nearby, the first read is not (unless the exhausted branch admitted it)']=('nearby=true;nearFrom=replaySnapshots.bytesRead();captured=read();' in capture
    and 'if(candidate&&!replaySnapshots.captureExhausted(priority,true))nearby=true;' in capture and 'bool nearby=false;' in capture)
checks['anchor once per capture frame, no shadowPivot()']=('if(!nearAnchorReady){nearAnchorReady=true;' in w and 'nearAnchorReady=false;' in w
    and 'shadowPivot' not in w[w.index('    void nearAnchorTake('):w.index('    bool nearCandidate(')]
    and 'actorShadowHistory.selfHold()>0?actorShadowHistory.selfAt():nullptr' in w)
checks['reserve 4 MiB, 0 under memory pressure or ActorShadows=0']=('constexpr std::size_t ReserveBytes=4u<<20;' in r
    and 'void setNearReserve(){replaySnapshots.setNearReserve(memoryPressure||!quality.actorShadows?0:NorthlightNearReserve::ReserveBytes);}' in g
    and 'memoryPressure=on;setNearReserve();' in g and 'configureBudget(size_t(captureBudgetMiB)*1048576,size_t(captureBudgetMiB)*524288);setNearReserve();' in w)
checks['logged on the sampled MODEL frame capture line']='nearAdmitted=%u nearBytes=%zu nearRefused=%u nearSelf=%d nearReserve=%zu' in w
checks['anchors: 6 yd self, 3.5 yd ray miss, +-12 yd window, 0.5..80 yd']='constexpr float SelfRadius=6.f,RayMiss=3.5f,RayWindow=12.f,RayMin=.5f,RayMax=80.f;' in r
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-near-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_near_reserve.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS near capture reserve: model and wiring')
