#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Underwater fade: native test of the real CPU helper (underwater_fade.h, clang++ plain and ASan/UBSan) and source wiring checks."""

from source_units import source_text
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
BANK = (fp.REPO/'src/world/world_frame_constants.inl').read_text()
WORLD = (fp.REPO/'src/world/world_renderer.h').read_text()
RENDERER = source_text(fp.REPO/'src/proxy/renderer.cpp')


def native():
    with tempfile.TemporaryDirectory(prefix='nl-underwater-') as temp:
        for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']):
            binary = Path(temp)/'test'
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, *fp.test_include_flags(),
                            str(HERE/'test_underwater_fade.cpp'), '-o', str(binary)], check=True)
            out = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
            assert 'underwater fade ok' in out, out


def wiring():
    assert 'underwaterFade.update(map,known,known&&global->liquidType!=0,now)' in WORLD
    assert 'const bool known=global&&global->predictors;' in WORLD  # unproven predictors never count as underwater
    assert 'WORLD underwater %s liquid=%u' in WORLD
    # the scale is applied at the outputs only: never to the source data that feeds GI and the lamp policy
    for bad in ('sunColor[i]*=','sunWeight*=','moonWeight*=','sun.alpha*=','sourceColors[source]=sourceColors','sourceWeights[source]*=','sourceColors[0]*=underwater'):
        assert bad not in WORLD, bad
    assert 'underwaterFade' not in WORLD[WORLD.index('NorthlightCelestialProfiles::apply(palette,renderDirect,celestialLight);'):WORLD.index('requestStaticCasters(map);')]
    assert 'if(volume){const float keep=quality.godrays?underwaterFade.scale():0.f;rgb[0]*=keep;rgb[1]*=keep;rgb[2]*=keep;}' in WORLD  # shafts only
    assert WORLD.count('rgb[0]*=keep')==1
    assert 'c[59][2]=std::max(wx.shadowSoften(),1.f-underwaterFade.scale());' in BANK  # lighting pass: sun correction 0, soft shadow
    assert 'c[27][0]=first?(volume?1.f:1.f-authoredFill):0.f;' in WORLD and 'authoredFill)*underwaterFade' not in WORLD
    assert 'underwaterFade.underwater!=underwaterLogged' in WORLD
    assert 'const float hazeSunWeight=underwaterFade.scale()*sourceWeights[0];' in BANK  # haze sun lobe
    assert 'discGain()*world->underwaterScale()' in RENDERER  # glare weights and veil


if __name__ == '__main__':
    native()
    wiring()
    print('underwater fade: ok')
