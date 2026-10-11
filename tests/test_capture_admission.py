#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.210 (task 31) capture admission: native model test of capture_admission.h (clang++, plain and
ASan/UBSan) against the real NorthlightDrawSnapshot::Frame (32/16 MiB, 4 MiB near reserve), nearest-first
ordering, hysteresis, near hits, the radius gate, the GI exception and the stepwise cut; and a wiring audit
of world_renderer.h / world_shadow_experiment.inl: the root is computed once per skinned draw after the
small-shadow return and before captureExhausted, admission sits between the rigid-memory test and the
budget check, the gate never sets captureShortfall, the cut does, gated bodies feed rigid memory, the end
of frame computes the next cut, the far list comes from radiusEntries() after the selection, and the
MODEL frame capture line carries the new fields. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import ast,subprocess,tempfile
HERE=Path(__file__).resolve().parent
stub=next(ast.literal_eval(n.value) for n in ast.parse((HERE/'test_terrain_snapshot.py').read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='stub' for t in n.targets))
with tempfile.TemporaryDirectory(prefix='northlight-admission-') as tmp:
    p=Path(tmp);(p/'d3d9.h').write_text(stub)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        exe=p/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(p),*fp.test_include_flags(),str(HERE/'test_capture_admission.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
w=fp.src('world_renderer.h').read_text();x=fp.src('world_shadow_experiment.inl').read_text();m=fp.src('world_rigid_memory.inl').read_text()
sel=fp.src('actor_shadow_selection.h').read_text();adm=fp.src('capture_admission.h').read_text();fate=fp.src('shadow_fate.h').read_text()
capture=w[w.index('    void captureModel(D3DPRIMITIVETYPE type,'):w.index('    // One directional replay draw in the 0.3.142 order')]
gate=capture[capture.index('NorthlightCaptureAdmission::Decision admit;'):capture.index('fate.reason=NorthlightShadowFate::CaptureBudget;\n        // Only reject')]
checks={}
checks['the root once per skinned draw: after the small-shadow return, before captureExhausted; the palette read stays in paletteRows']=(capture.count('rooted=drawRoot(current,root)')==1
    and capture.index('if(smallShadow&&!actorCaptureEnabled())')<capture.index('NorthlightCaptureAdmission::Decision admit;')<capture.index('if(replaySnapshots.captureExhausted(priority))')
    and capture.index('const auto& metadata=*found;')<capture.index('if(replays.size()>=4096)')<capture.index('NorthlightCaptureAdmission::Decision admit;')
    and w.count('GetVertexShaderConstantF(UINT(program->second->paletteBase),rows,3)')==1)
checks['skinned draws with ActorShadows only; the gate never sets captureShortfall, the cut does']=('if(priority&&quality.actorShadows){nearAnchorTake();rooted=drawRoot(current,root);' in gate
    and 'fate.reason=NorthlightShadowFate::RadiusGate;gatedBodies.insert(gatedBodies.end(),root,root+3);}' in gate and 'fate.reason=NorthlightShadowFate::DistanceCut;captureShortfall=true;}' in gate
    and gate.count('captureShortfall')==1)
checks['near hits come from the same root (no second palette read)']='admission.decide(reinterpret_cast<std::uintptr_t>(current),count,vertexTotal,rooted?root:nullptr,nearDraw(rooted?root:nullptr))' in gate
checks['real bytes on a read, an estimate on real exhaustion (both budget branches)']=(capture.count('admission.budgetRefused(admit,root,vertexTotal,count)')==2
    and 'if(captured&&admit.rooted)admission.admitted(admit,reinterpret_cast<std::uintptr_t>(current),count,root,replaySnapshots.bytesRead()-readBefore);' in capture)
checks['the root travels: Replay::captureRoot set at capture, cleared on injected copies, carried by prepareRecord into the selection draw']=(
    'float captureRoot[3]={};bool captureRootValid=false;' in w and 'p->captureRootValid=rooted;if(rooted)std::memcpy(p->captureRoot,root,sizeof p->captureRoot);' in capture
    and 'p->captureRootValid=false;' in m and 'item.captureRootValid=p.captureRootValid;' in fp.src('prepare_worker.h').read_text())
checks['RadiusEntry carries shader, count and root passively: written once at publish, never read by the selection']=(
    sel.count('r.shader=a.keyShader;r.count=a.keyCount;std::memcpy(r.rootAt,a.keyRoot,sizeof r.rootAt);r.rootValid=a.keyRootValid;')==1
    and sum('rootAt' in l for l in sel.splitlines())==2 and sum('rootValid' in l for l in sel.splitlines())==2)
checks['far list from radiusEntries() right after the selection (outside the try); admission forgotten on a lost selection, a map change and releaseGPU']=(
    x.index('}catch(...){')<x.index('if(radius)admission.importFar(actorShadowHistory.radiusEntries());')<x.index('if(captureSampled){const double ms=')
    and 'actorShadowHistory.clear();admission.reset();' in x and 'rigidMemory.resetStats();admission.reset();' in m and 'rigidMemoryClear();admission.reset();admissionOn=false;' in w)
checks['gated bodies are rigid-memory bodies (a prop held by a far NPC is not a ghost)']=('rigidBodies.insert(rigidBodies.end(),gatedBodies.begin(),gatedBodies.end());' in m)
checks['end of frame: the cut after the sampled log, before the counters of the next frame']=(
    w.index('admissionCentre=%s')<w.index('admission.endFrame(size_t(captureBudgetMiB)*1048576,replaySnapshots.bytesRead());'))
checks['logged on the MODEL frame capture line']=('radiusGated=%u radiusGatedBytes=%zu cutRejected=%u cutYards=%.1f insideRefused=%u insideRefusedActors=%u noRoot=%u admissionCentre=%s' in w
    and w.count('admissionCentre=%s')==1)
checks['centre once per capture frame at the first skinned ask (the near anchor), self else pivot; GI cadence read without deciding it']=(
    'if(nearAnchor.self){centre.centre=nearAnchor.selfAt;centre.self=true;}' in w and 'pivot[k]=nearAnchor.eye[k]+nearAnchor.forward[k]*pivotDistance' in w and 'nearAnchorReady=false;' in w
    and 'bool actorCaptureWouldRun()const{' in w and w.count('if(nearAnchor.self){centre.centre=')==1)
checks['fate: RadiusGate and DistanceCut']=('RadiusGate,DistanceCut,FateCount' in fate and '"radiusGate","distanceCut"' in fate)
checks['constants: gate = yards + 2*band + ClusterExtent + 4; cut target 0.92, MinCut 12, grow after 10 frames, shrink after 2']=(
    'return yards+2*NorthlightActorShadowSelection::band(yards)+NorthlightActorShadowSelection::ClusterExtent+GateExtra;' in adm and 'GateExtra=4.f' in adm and 'Margin=.92f' in adm and 'MinCut=12.f' in adm
    and 'GrowFrames=10,ShrinkFrames=2' in adm)
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
print('PASS capture admission: model and wiring')
