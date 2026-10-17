#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Runner for test_zone_tuning.cpp (native clang++, plain and ASan/UBSan; no game, Wine or GPU).

Protects src/sky/zone_tuning.h (the NORTHLIGHT_DEV_TUNING overlay: northlight-tuning.ini parser and ranges, [default]
propagation onto shipped zone profiles, worker-side terrain_radius / ground fog scales, the state JSON) and the
reusable NorthlightCelestialProfiles::assignKey / explicitKeys in src/sky/celestial_profiles.h. The state JSON the
C++ test writes is loaded here and checked against the contract keys of scripts/dev_tuning/. Writes only a temp folder."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json, subprocess, tempfile

HERE = Path(__file__).resolve().parent
CELESTIAL = ['disc', 'disc_gain', 'light', 'light_mix', 'light_strength', 'fog_gain', 'fog_height_scale', 'horizon_haze']
with tempfile.TemporaryDirectory(prefix='northlight-zone-tuning-') as tmp:
    for label, flags in [('O2', ['-O2']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])]:
        exe = Path(tmp) / ('test-' + label)
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', *flags, *fp.test_include_flags(),
                        str(HERE / 'test_zone_tuning.cpp'), '-o', str(exe)], check=True)
        state = Path(tmp) / f'state-{label}.json'
        print(label, subprocess.check_output([str(exe), str(state)], text=True), end='', flush=True)
    j = json.loads(state.read_text(encoding='utf-8'))
    assert j['version'] == 1 and j['tick'] == 123456 and j['inWorld'] is True and j['map'] == 'Kalimdor' and j['zone'] == 215
    assert j['build'] == 'dev "tuning"' and j['overridesError'] == 'line 2: bad\nthing' and j['overridesRevision'] == 3 and j['builtRevision'] == 2
    assert j['overridesEnabled'] is True and j['overridesLoaded'] == 7 and j['terrainReach'] == 2048 and j['groundFogActive'] is True and j['night'] == 0.25
    assert j['camera'] == [-1234.5, 7, 0]  # a non-finite number is written as 0, never as NaN
    expected = [f'{b}_{k}' for b in ('sun', 'moon') for k in CELESTIAL] + ['terrain_radius', 'ground_fog_day_scale', 'ground_fog_night_scale']
    for part in ('base', 'effective'):
        assert sorted(j[part]) == sorted(expected), part
        assert len(j[part]['sun_disc']) == 3 and len(j[part]['moon_light']) == 3
    assert j['base']['terrain_radius'] == 1500 and j['effective']['terrain_radius'] == 4096
    assert abs(j['effective']['sun_disc'][1] - 0.93) < 1e-5 and abs(j['base']['sun_light_mix'] - 0.4) < 1e-5
    print('state json ok')
