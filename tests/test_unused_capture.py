#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Task 43 (B) unused-capture memory: native model test of unused_capture.h (clang++, plain and ASan/UBSan), and a wiring audit of
world_renderer.h / world_point_rendering.inl: the prediction sits after the admission block and before the budget check, Mode 2 never skips a
near-reserve hit or a GI-capture frame, every consumer stamps usedStamp (submitReplay, the static-proof skips, GI packets, point cubes),
the verdicts are observed once per fresh capture frame before the profile line, and the counters reach the WORLD CPU capture line. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='northlight-unused-') as tmp:
    p=Path(tmp)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        exe=p/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_unused_capture.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
w=fp.src('world_renderer.h').read_text();pt=fp.src('world_point_rendering.inl').read_text();h=fp.src('unused_capture.h').read_text()
capture=w[w.index('    void captureModel(D3DPRIMITIVETYPE type,'):w.index('    // One directional replay draw in the 0.3.142 order')]
checks={}
checks['prediction after admission, before the budget check; skinned rooted draws only']=(capture.index('NorthlightCaptureAdmission::Decision admit;')<capture.index('NorthlightUnusedCapture::key(')<capture.index('if(replaySnapshots.captureExhausted(priority))')
    and 'const bool unusedKeyed=NorthlightUnusedCapture::Mode&&rooted&&priority&&quality.actorShadows;' in capture)
checks['Mode 2 spares near-reserve hits and GI capture frames']='if(NorthlightUnusedCapture::Mode>=2&&NorthlightNearReserve::test(nearAnchor,root)==NorthlightNearReserve::None&&!actorCaptureEnabled()){++unusedSkipped;' in capture
checks['shipped off (issue #63): no per-draw cost']='constexpr unsigned Mode=0;' in h
checks['every consumer stamps the replay']=(w.count('p->usedStamp=unusedCapture.frame();')>=3 and 'replay.usedStamp=unusedCapture.frame();' in w and 'p->usedStamp=unusedCapture.frame();' in pt)
checks['observed once per fresh capture frame, before the profile line and the cascade reset']=(w.count('unusedObserve();')==1 and w.index('        unusedObserve();')<w.index('if(profileSampled())logRenderProfile()')<w.index("cascadeActions[slot]='-';")
    and 'if(!NorthlightUnusedCapture::Mode||captureMode!=CaptureFresh)return;' in w and "cascadeActions[slot]=='R'||cascadeActions[slot]=='-'" in w and 'pointSelected.sourceId==0' in w)
checks['counters on the WORLD CPU capture line and reset per frame']=('rejSmall=%u rejGate=%u rejCut=%u rejBudget=%u unusedMode=%u unusedKnown=%u unusedPredicted=%u unusedPredictedUsed=%u unusedSkipped=%u' in w
    and 'unusedPredicted=unusedPredictedUsed=unusedSkipped=unusedKnown=rejSmall=rejGate=rejCut=rejBudget=rejOther=0;' in w)
bad=[k for k,v in checks.items() if not v]
for k,v in checks.items():print(('ok   ' if v else 'FAIL ')+k)
sys.exit(1 if bad else 0)
