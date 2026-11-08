#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Exact current scene/native slot matching; renderer direct remains unchanged."""
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
