#!/usr/bin/env python3
# northlight-test:
"""ActorShadowRadius wiring audit (static source analysis; nothing is run).
GI stays exactly as before: its actor packets are copied at capture time and the
job is published before the shadow selection runs; the selection never touches
the GI job and only writes the shadow flag (and fate diagnostics, and since 0.3.176 the rigid bone it
tested, for rigid memory) of replays.
Radius 0 keeps the 0.3.144 control flow: choose() gets no Radius and the stable
selection runs only when the quota ranks. The native decision tests are in
test_actor_shadow_radius.cpp (run by test_actor_shadow_selection.py)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import re
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text();ex=fp.src('world_shadow_experiment.inl').read_text();q=fp.src('quality_settings.h').read_text()
render=w[w.index('    bool render(IDirect3DSurface9* targetSurface'):]
# GI: packets built in appendActor() during capture, job published by finishActorScene() before selection.
assert render.index('finishActorScene();')<render.index('if(replayShadows)selectShadowReplays();'),'GI actor job published before the shadow selection'
assert w.count('appendActor(current,*p,sample);')==1 and w.index('appendActor(current,*p,sample);')<w.index('replays.emplace_back(p.release());'),'GI packets copied at capture'
code=re.sub(r'/\*.*?\*/','',re.sub(r'//[^\n]*','',ex),flags=re.S) # comments may mention GI
for needle in ('actorJob','appendActor','packets','finishActorScene','NorthlightGI::'):
    assert needle not in code,f'shadow selection must not touch GI ({needle})'
writes=set(re.findall(r'replays\[[^\]]*\]->(\w+)=',ex))|set(re.findall(r'\bp->(\w+)=(?!=)',ex))|set(re.findall(r'auto& r=\*replays\[[^;]*;r\.(\w+)=',ex))|set(re.findall(r'\bstored\.(\w+)=(?!=)',ex))
assert writes<= {'shadowSelected','fateClass','boneKnown','bone'},writes # 0.3.176 (S2): the stored rigid bone for rigid memory
assert 'r.fateClass=f;r.fateDistance=' in ex # fate diagnostics only
# Radius 0: no Radius reaches choose(), and the stable selection runs only when the quota ranks.
assert 'radius.active()?&radius:nullptr' in ex
assert 'const bool radius=NorthlightActorShadowSelection::Enabled&&quality.actorShadowRadius;' in ex and 'if(!radius)transition(budget&&actorBytes>budget);' in ex
assert 'else if((ranked=NorthlightActorShadowSelection::Enabled&&budget&&actorShadowHistory.shouldRank(actorBytes,budget,selectionTuning()))){' in ex,'radius 0: the 0.3.144 quota decision'
assert 'transition(budget&&stable.radiusInsideBytes>budget);' in ex and 'Radius radius{float(quality.actorShadowRadius),context.camera,true,true};' in ex,'radius: ranks on the bytes inside'
assert 'else if(NorthlightActorShadowSelection::Enabled){if(budget)actorShadowHistory.keptAll();else actorShadowHistory.clear();}' in ex,'radius 0 keeps the 0.3.144 reset'
assert "int(ranked&&stable.actors+stable.rigidActors>0)" in ex,'ranking= keeps its meaning (the quota ranked)'
# Settings: key, range, presets and the log fields.
assert '{"ActorShadowRadius",&Settings::actorShadowRadius,0,200,{40,35,20}}' in q and 'unsigned actorShadowRadius=40;' in q
assert 'radius=%u radiusDropped=%zu radiusDroppedDraws=%zu radiusDroppedBytes=%zu radiusTogglesSinceLog=%zu radiusFlickerSinceLog=%zu radiusRekeyedSinceLog=%zu radiusCharacters=%zu radiusSelf=%zu radiusCompanions=%zu radiusInsideBytes=%zu radiusNoPivot=%zu rekeyFlipSinceLog=%zu attachToFreeBeyondSinceLog=%zu freeToAttachBeyondSinceLog=%zu freeRigidBeyond=%lld freeRigidBeyondBytes=%lld freeRigidMaxYards=%.1f propRigidBeyond=%lld propRigidBeyondBytes=%lld stuckBeyond=%lld' in ex
ini=fp.src('windows-package/northlight-quality.ini').read_text();readme=fp.src('windows-package/README.txt').read_text(encoding='utf-8')
block=ini[ini.index('; Characters and creatures farther than'):ini.index(';ActorShadowRadius=40')]
assert 'Allowed 0..200. 40 / 35 / 20' in block and '; FPS impact:' in block and 'ActorShadowRadius     40 / 35 / 20   characters more than N yards from your own character cast no shadow' in readme
print('PASS actor shadow radius wiring: GI packets before selection and untouched, shadow flag only, radius 0 = 0.3.144 control flow, key/presets/docs/log fields')
