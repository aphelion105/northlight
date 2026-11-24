#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Prepared cube planes preserve scalar masks, overflow guards, and fail-open inputs."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
source=Path(__file__).with_suffix('.cpp')
with tempfile.TemporaryDirectory(prefix='northlight-point-clip-') as tmp:
 for mode,flags in [('release',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
  exe=Path(tmp)/mode
  subprocess.run(['clang++','-std=c++17','-UNDEBUG',*flags,*fp.test_include_flags(),str(source),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
renderer=fp.src('world_point_rendering.inl').read_text()
assert 'const NorthlightPointShadow::FaceClip clip(faceMatrices,pointSlot().selected,faces);' in renderer
assert 'if((faces&(1u<<face))&&!NorthlightPointShadow::faceMatrix(' in renderer
assert 'pointMeshBounds.mask(i,batch,clip,faces)' in fp.src('world_point_mesh.inl').read_text()
assert 'pointMeshBounds.mask(i,batch,clip,mask)' in renderer
assert 'pointMask(batch.low,batch.high,clip,faces)' in renderer
assert 'pointReplayCasters.classify(clip,faces' in renderer
assert 'clip.mask(caster.bounds,groupFaces)' in fp.src('point_replay_casters.h').read_text()
assert 'const FaceClip clip(matrices);' in fp.src('point_light_shadow.h').read_text()
print('PASS prepared clipping is shared by static, terrain, live, replay, and content paths')
