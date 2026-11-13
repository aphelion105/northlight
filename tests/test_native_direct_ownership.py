#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Exact scene/slot ownership, scoped native RGB handoff and live cube checks."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir();exe=out/'native-direct-ownership.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(fp.TESTS/'test_native_direct_ownership.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
subprocess.run([str(exe)],check=True)
world=fp.src('world_renderer.h').read_text()
draw=world[world.index('    template<class Draw> HRESULT nativeLocalDraw('):world.index('    void observeLightAttachments(')]
assert draw.index('nativeDirectBankMemo.reset();')<draw.index('NorthlightNativeLights::frame(')
for gate in ('ownershipGain(selected,*native,resolved.data())','observedOwnershipGain(selected,entry,ownerNow,nativeDirectScene)','owner->second','pixelOwner->second',
             'D3DRS_ZWRITEENABLE','D3DRS_ALPHABLENDENABLE','localLightMap==active->map',
             'ownerMask&(1u<<slot)','renderer-owned-direct','return ownedSubmit();'):
    assert gate in draw,gate
assert 'binding->native=&binding->observed;' in draw
assert 'if(!gpuNormalDiagnosticRequested||privateMismatch)return submit();' in draw
assert 'return captureNativeWmoDraw(' in draw
assert 'if(canOwn&&(candidateMask||nativeExtraRequested)){' in draw
assert 'canOwn||gpuNormalDiagnosticRequested?' in draw
assert 'if(gpuNormalDiagnosticRequested&&candidateMask)' in draw
assert 'selected-native-source-unmodified' in draw
assert 'nativeTerrainPointShaders.count(sourceVS)!=0' in draw
assert 'NorthlightNativeDirect::terrainBank(' in draw
assert 'terrainReceiver?4:2' in draw and 'terrainReceiver?11:14' in draw
assert 'cachedTerrainBank(mirror)' in draw
assert 'start=terrainReceiver?29+3*first:17+first' in draw
assert 'terrainReceiver||!matched||privateMismatch' in draw
coverage=world[world.index('    bool terrainShadowCoverage('):world.index('    bool terrainShadowActive()')]
assert 'registerNativePointShader(shader,reinterpret_cast<const DWORD*>(output.data()),output.size(),false,true)' in coverage
assert 'nativePointPixelOwners[result.shader]=NorthlightNativeLocalPatch::pointOwnerPixel' in coverage
neutral=world[world.index('    IDirect3DPixelShader9* terrainShadowReplacement('):world.index('    bool terrainShadowCoverage(')]
assert 'nativePointPixelOwners[replacement]=NorthlightNativeLocalPatch::pointOwnerPixel' in neutral
wmo=world[world.index('    template<class Draw> HRESULT captureNativeWmoDraw('):world.index('    template<class Draw> HRESULT nativeLocalDraw(')]
assert wmo.index('!gpuNormalDiagnosticRequested')<wmo.index('d->GetDepthStencilSurface')
for gate in ('wmoShaders.count(sourceVS)','depth.p!=mainDepth','D3DRS_ZWRITEENABLE','D3DRS_ALPHABLENDENABLE','native-wmo-without-proven-point-bank'):
    assert gate in wmo,gate
assert 'nativeDiagnosticWmoImages<4' in wmo
assert 'if(token&0x80000000u)++nativeDiagnosticWmoImages;' in wmo
assert draw.index('const unsigned candidateMask=')<draw.index('d->GetRenderState(D3DRS_ZWRITEENABLE')
assert draw.index('if(ownerMask&(1u<<slot))continue;')<draw.index('!*binding->ready')
resolved=draw.index('nativeDirectBankMemo.resolve(')
for live in ('!*binding->ready','!*binding->usable','std::isfinite(*binding->gain)',
             '*binding->cube!=binding->light.cube','*binding->map!=active->map',
             'binding->selected->sourceId!=binding->sourceId','binding->selected->attenuationEnd!=binding->light.range',
             'output.gain=std::clamp(*binding->gain'):
    assert draw.index(live)>resolved,live
release=fp.src('world_point_rendering.inl').read_text()
release=release[release.index('    void releasePointGPU('):release.index('    void ',release.index('    void releasePointGPU(')+10)]
assert 'nativeDirectBankMemo.reset();' in release
print('actual world: binding rebuild/release invalidation and live resource/gain checks remain outside memo PASS')
