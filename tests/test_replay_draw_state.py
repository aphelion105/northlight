#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Replay draw-state binding: shader/pass preconditions of the opaque-material skip, then the
native equivalence test (0.3.142 cache vs current, software R32F+D24 raster) and the existing
render-state cache test, at -O2 and under ASan/UBSan. No game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from scripts.shaders.shader_sources import world_source_text
import subprocess
import tempfile
from pathlib import Path
from source_units import source_text

HERE = Path(__file__).resolve().parent
read = lambda name: fp.tracked(name).read_text()
world, point, proxy, probe = source_text(fp.src('world_renderer.h')), read('world_point_rendering.inl'), source_text(fp.src('renderer.cpp')), read('world_replay_probe.inl')
point_replays = read('world_point_replays.inl')

# Both shadow shaders bypass every alpha operation at cutoff -1. Native
# shader masks are enabled only with a nonnegative cutoff and an explicit mode.
# Fixed alpha testing emits the proved output alpha on RT0; RT1 retains depth.
for source,material in (('world_effects.hlsl','Material'),('local_light_effects.hlsl','LocalMaterial')):
    text=world_source_text(fp.src(source)) if source=='world_effects.hlsl' else read(source)
    assert f'if({material}.w>=0)' in text
    assert f'{material}.x==2?diffuse.a:tex2D(' in text
    assert (f'if({material}.x==1)alpha*=diffuse.a;' in text or
            f'if({material}.x==1||{material}.x==3)alpha*=diffuse.a;' in text)
    if f'if({material}.x==3)' in text:
        assert 'clip(alpha-(rank+.5)/16);' in text
    assert f'clip(alpha-{material}.w);' in text
    assert f'if({material}.y>.5){{output.native.a=alpha;return output;}}' in text
    assert 'float4 depth:COLOR1;' in text
assert 'p->cutoff=alpha?(float(ref)+(alphaFunc==D3DCMP_GREATER?.5f:0.f))/255.f:shaderCutoff;p->alphaMode=alphaMode;' in world
assert 'if(skip_&&material_&&p.cutoff<=-1.f)' in read('replay_draw_state.h')
# One binding cache per pass, including the extracted actor and flash draws.
for owner in ('world_directional_pass.inl', 'world_directional_actor_draw.inl',
              'world_flash_rendering.inl', 'world_replay_probe.inl'):
    assert read(owner).count('NorthlightShadowReplay::Pass ') == 1, owner
bake_start=point.index('bool bakePointSubset(')
point_start=point.index('bool renderPointShadow(')
bake=point[bake_start:point_start]
assert bake.count('NorthlightShadowReplay::Pass ') == 1
assert point[point_start:].count('NorthlightShadowReplay::Pass ') == 1
assert bake.index('SavedState saved(') < bake.index('submitPointReplayFace(') < bake.index('saved.restore()')
issue = probe[probe.index('NorthlightShadowReplay::Pass replayPass(d);'):probe.index('        if(draws==replayProbeList.size())return true;')]
assert 'submitReplay(p,replayPass,rows,worldMatrix,mode,' in issue
for forbidden in ('SetTexture(', 'SetSamplerState(', 'SetPixelShader(', 'SetPixelShaderConstantF(', 'SetRenderState('):
    assert forbidden not in issue, forbidden
    assert forbidden not in probe, forbidden
replay_shader = 'gpuPass.bindReplayShader(shadowPrograms.replay());'
assert world.index(replay_shader) < world.index('replayProbe(rows,matrices[cascade],realLoopMs);') < world.index('unionShadow(directionalMaps.current(slot).surface')
assert 'NorthlightWorldDirectionalTargets::merge(d,shadowPrograms.unionShader(),target,live,cached' in fp.src('world_near_environment.inl').read_text()
assert 'd->SetPixelShader(shader);' in fp.src('world_directional_targets.h').read_text()
loop = world[world.index(replay_shader)+len(replay_shader):world.index('replayPosePrepared+=replayPass.posesPrepared();')]
assert 'NorthlightShadowReplay::Pass replayPass(d);' in loop
for forbidden in ('SetTexture(', 'SetSamplerState(', 'SetPixelShader(', 'SetPixelShaderConstantF(', 'SetRenderState('):
    assert forbidden not in loop, forbidden
cube = point_replays[point_replays.index('bool submitPointReplayFace('):]
for forbidden in ('SetTexture(', 'SetSamplerState(', 'SetPixelShader(', 'SetPixelShaderConstantF(', 'SetRenderState('):
    assert forbidden not in cube, forbidden
face = read('point_shadow_face_pass.h')
assert 'check_(device_->SetPixelShader(caster_),"cube PS")' in face
assert point.index('NorthlightShadowReplay::Pass replayPass(d);') < point.index('NorthlightPointShadow::FacePass facePass(')
bind = point[point.index('const auto bindWorldGeometry=[&]'):point.index('if(rebuildMask&(1u<<face)){')]
assert bind.index('replayPass.invalidateBindings();') < bind.index('facePass.bindWorldGeometry(')
assert 'return !drawWorldGeometry||bindWorldGeometry();' in point
union = point[point.index('if(!facePass.resolve('):point.index('facePass.finishFace();')]
assert union.index('if(facePass.usesUnion())') < union.index('replayPass.invalidateBindings();') < union.index('++pointWork.unions;')
assert 'if(instanced){replayPass.afterPalette();return D3D_OK;}' in cube
owner = read('shadow_replay_submit.h')
assert cube.index('replayPass.beginFace(pointReplayStateReuse());') < cube.index('replayPass.preparePose(')
face_setup = owner[owner.index('void beginFace('):owner.index('void recordFloatPrefix(')]
assert face_setup.index('bindings_.beginMaterialPass();') < face_setup.index('pose_.beginProjection();')
assert 'NorthlightReplayDrawState::Cache bindings_;' in owner and 'NorthlightReplayPoses::Pass<BOOL> pose_;' in owner
assert bind.index('facePass.bindWorldGeometry(') < bind.index('if(bound)replayPass.recordFloatPrefix(matrix,4);')
assert 'submitPointReplayFace(replayPass,replayPackets,' in point
# Directional pass baseline: LESSEQUAL depth, no blend/alpha test/stencil, all
# colour channels. The per-draw native alpha wrapper restores this baseline.
directional=read('directional_gpu_pass.h')
initial_state=directional[directional.index('void prepareDevice('):directional.index('struct TerrainWork')]
for state in ('{D3DRS_ZFUNC,D3DCMP_LESSEQUAL}', '{D3DRS_ALPHATESTENABLE,FALSE}', '{D3DRS_ALPHABLENDENABLE,FALSE}', '{D3DRS_STENCILENABLE,FALSE}', '{D3DRS_COLORWRITEENABLE,15}'):
    assert state in initial_state, state
assert 'for(unsigned i=0;i<14;++i)d->SetTexture(i,nullptr);' in initial_state
render=world[world.index('bool render(IDirect3DSurface9* targetSurface'):]
assert render.index('prepareWorldShadowState();')<render.index('if(!renderDirectional(')
# The float-alpha guard sees every game texture creation.
for method in ('CreateTexture', 'CreateVolumeTexture', 'CreateCubeTexture'):
    line = next(l for l in proxy.splitlines() if f'HRESULT STDMETHODCALLTYPE {method}(' in l and 'ext->' in l)
    assert 'NorthlightReplayDrawState::noteTextureFormat(Format);' in line, method
assert 'return shadowReplayDraw(*p,draw);' in world
assert 'result=grouped?issue():alphaDraw(issue);' in owner
assert 'NorthlightShadowAlpha::drawSequence(d,candidates.size()' in cube and 'shadowReplayDraw(*replays[candidates[at]],draw)' in cube
assert 'return shadowReplayAlpha.draw(p.alphaMode,' in world
assert 'const HRESULT hr=NorthlightShadowAlpha::draw(device_,mode,' in read('shadow_alpha_replay.h')
directional = fp.src('world_directional_replays.inl').read_text(encoding='utf-8')
assert 'NorthlightShadowAlpha::drawSequence(d,candidates.size()' in directional
assert 'shadowReplayDraw(*replays[candidates[at]],draw)' in directional
assert directional.index('shadowPalette.submit(') < directional.index('replayPass.afterPalette();') < directional.index('return result;')
assert 'pointReplayBatchState(first,next,skipOpaque)' in directional and 'NorthlightReplayPoses::sameCaptured(first,next)' in directional
assert 'next.index!=first.index||next.base!=first.base' in directional
pair = fp.src('world_shadow_index_pairs.inl').read_text(encoding='utf-8')
assert 'p.gpuCached&&p.index&&p.indexed' in pair and 'p.base==0' in pair
assert 'pointReplayRangesJoin(first,next)||pointReplayRangesJoin(next,first)' in pair
assert 'at=start+pair.packets;' in directional and 'run=pair.run;' in directional
assert 'if(FAILED(result)&&indices&&indices->buffer)' in owner
assert owner.index('indices->buffer=nullptr;') < owner.index('return bindings_.geometry(packet);')
assert world.count('shadowReplayIndices.begin(frames);') == 1
print('PASS preconditions: replay/local alpha outputs, cutoff encoding, fixed-PS loops, native alpha wrapper wiring, pass states, float-alpha guard wiring')

with tempfile.TemporaryDirectory() as tmp:
    for source in ('test_replay_draw_state.cpp', 'test_render_state_caches.cpp'):
        for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']):
            exe = Path(tmp) / 'test'
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, *fp.test_include_flags(), str(HERE / source), '-o', str(exe)], check=True)
            out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
            print(f'{source} {" ".join(flags[:1])}{" asan+ubsan" if len(flags) > 1 else ""}:\n' + out.strip())
