#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Point cube schedule, 0.3.151 face cycles (PointShadowFacesPerFrame) and per-face static content keys:
native test_point_shadow_schedule.cpp (plain and ASan/UBSan) plus renderer wiring. No game, Wine or device."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
pt=fp.src('world_point_rendering.inl').read_text()
face=fp.src('point_shadow_face_pass.h').read_text()
# The same per-face invariants now apply to each independently owned light slot.

body=pt[pt.index('    bool renderPointShadow('):pt.index('    bool renderPointLighting(')]
pred=pt[pt.index('    bool pointRefreshPredicted()const{'):pt.index('    bool renderPointShadow(')]
# 3a: static faces keyed on content; the generation only through the unknown-records fallback.
assert 'pointCacheSerial' not in pt and pt.count('pointStaticRebuild(')==2
assert 'NorthlightPointShadow::selectMany(active->localLights.data(),active->localLights.size(),context.camera,NorthlightQuality::localShadowLimit(quality),{},float(quality.pointShadowDistance),true).count!=0||' in pred
assert 'NorthlightPointShadow::selectMany(equipmentLights.output.data(),equipmentLights.outputCount,context.camera,NorthlightQuality::localShadowLimit(quality),{},float(quality.pointShadowDistance),true).count!=0;' in pred
assert 'const unsigned rebuildMask=pointStaticRebuild(pointSlot().selected,lightChanged);' in body
assert 'if(!pointSlot().cacheValid||lightChanged||pointSlot().cacheSource!=light.sourceId)return 63;' in pt
assert 'if(pointStaticCandidates[face].empty()){drop(pointSlot().cacheSurface[face]);drop(pointSlot().cacheFace[face]);}' in body
static_submission='drawMesh(pointStaticCandidates[face],MeshPass::Static,pointWork.statics)'
assert body.index('if(!pointCacheResources(face)||') < body.index('!facePass.beginStatic(pointSlot().cacheSurface[face]') < body.index(static_submission)
assert 'pointSlot().faceContent[face]=digests?digests[face]' in pt
assert body.index('pointRememberStaticFace(face,lightChanged)')>body.index(static_submission),'a face key is recorded only after its static draws'
# S1'': no fragment beyond the light's range is stored (every cube caster, one shader), so a
# face depends only on in-range casters and the sphere + face digest is complete.
hlsl=fp.src('local_light_effects.hlsl').read_text()
ps=hlsl[hlsl.index('LocalShadowOutput LocalShadowPS('):hlsl.index('float2 localDepthUV(')]
assert 'float4 LocalSphere : register(c1);' in hlsl and 'clip(LocalSphere.x-dot(offset,offset));' in ps
assert 'clipPosition.x+LocalSphere.y*clipPosition.w,clipPosition.y-LocalSphere.y*clipPosition.w,clipPosition.w' in ps
assert 'facePass.beginFace(pointSlot().faces[face],pointSlot().selected.attenuationEnd)' in body
assert 'sphere_[0]=range*range;' in face and 'sphere_[1]=1.f/viewport_.Width;' in face
assert 'check_(device_->SetPixelShaderConstantF(1,sphere_,1),"cube sphere")' in face
# c1 lifetime: the union (c0..c1) runs after each face's casters, so the sphere is rebound by
# bindCaster before any LocalShadowPS draw of the next face.
bind=face[face.index('bool bindCaster('):face.index('\npublic:')]
assert bind.index('SetPixelShader(caster_)')<bind.index('SetPixelShaderConstantF(1,sphere_,1)')
loop=body[body.index('if(rebuildMask&(1u<<face)){'):body.index('pointSlot().faceWeights[face]=pointDraws-faceDraws')]
assert face.count('SetPixelShaderConstantF(1')==1 and 'SetPixelShaderConstantF(1' not in loop
assert 'SetPixelShaderConstantF(0,&constants[0][0],2)' in face[face.index('bool resolve('):]
assert loop.index('facePass.beginStatic(pointSlot().cacheSurface[face]') < loop.index(static_submission) < loop.index('facePass.finishStatic()')
for draw in ('drawMesh(pointTerrainCandidates[face],MeshPass::Terrain','facePass.drawLiveTerrain(','submitPointReplayFace('):
    assert loop.index('facePass.beginDynamic(pointTargets.scratchSurface()')<loop.index(draw),draw
assert body.index('facePass.resolve(staticOnly')<body.index('pointSlot().schedule.commit(',body.index('const auto& bindings='))
assert 'recordOutsideSphere(r.low,r.high,l,FaceContentSphereMargin)' in fp.src('point_light_shadow.h').read_text()
assert 'POINT static faces rebuilt=%u keptByContent=%u' in pt and 'if(NorthlightQuality::renderProfile(quality))' in pt
# 3b: default 6 draws all six; any light change or unusable cube draws all six and invalidates first.
assert 'const bool cycling=quality.pointShadowFacesPerFrame<6&&pointSlot().schedule.usable&&!lightChanged&&pointSlot().cacheValid&&pointSlot().cacheSource==pointSlot().selected.sourceId;' in body
assert 'pointSlot().faceCycle.reset();pointSlot().schedule.invalidate();}' in body and 'faces=pointSlot().faceCycle.mask();pointSlot().schedule.stale();}' in body
# Candidate lists are rebuilt from this frame's replays every call (indices never cross frames).
assert body.index('list.clear();')<body.index('pointAppendCandidates(pointReplayCandidates,i,mask,faces);')
# No progress and no commit without replays; the cycle commits as an update of its first frame.
i=body.index('if(!fresh){')
assert i<body.index('if(cycling&&!pointSlot().faceCycle.advance())')<body.index('pointSlot().schedule.commit(pointSlot().faceCycle.startedAt,pointSlot().selected,pointSlot().faceCycle.generation);')
assert 'if(!pointSlot().schedule.usable||lightChanged)return false;' in body
assert body.index('pointSlot().schedule.due(')<i<body.index('for(unsigned face=0;face<6;++face)')
with tempfile.TemporaryDirectory(prefix='northlight-point-schedule-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_point_shadow_schedule.cpp'),'-o',str(exe)],check=True)
        out=subprocess.check_output([str(exe)],text=True)
        assert 'point face cycles passed' in out and 'point schedule passed' in out,out
        print(label,out,end='',flush=True)
print('PASS point schedule, face cycles and per-face static content keys')
