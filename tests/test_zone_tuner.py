#!/usr/bin/env python3
# northlight-test:
"""The developer zone tuner (scripts/dev_tuning/zone_tuner.py, zone_tuner.html): northlight-tuning.ini
round-trips and is range-checked like the dev DLL's parser (src/sky/zone_tuning.h), editing one section
leaves the others and their comments alone, export merges into copies of client-config/
celestial-profiles.ini and shadow-range-profiles.ini without touching unrelated lines, and the HTTP
server serves the page, writes overrides and reports the state file."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import io
import json
import os
import shutil
import tempfile
import threading
import time
import urllib.error
import urllib.request

TOOL = fp.tracked('dev_tuning/zone_tuner.py')   # scripts/dev_tuning/zone_tuner.py
sys.path.insert(0, str(TOOL.parent))
import zone_tuner as zt  # noqa: E402

failures = []


def check(ok, what):
    print(('PASS ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def rejects(fn, what):
    try:
        fn()
    except ValueError as e:
        check(True, f'{what} rejected ({e})')
        return
    check(False, f'{what} rejected')


NAMES = zt.ZoneNames(table={440: 'Tanaris', 12: 'Elwynn Forest', 3: 'Badlands'})

# ---- overrides round trip ----
text = zt.edit_overrides('', section='Kalimdor:440', values={
    'sun_disc': [1, .93, .8], 'sun_light_mix': .3, 'terrain_radius': 4096, 'ground_fog_day_scale': 1.25}, names={'Kalimdor:440': 'Tanaris'})
model = zt.parse_overrides(text)
check(text.startswith(zt.HEADER + '\n[tuning]\nenabled = 1\n'), 'a new file starts with the header and [tuning] enabled = 1')
check('[Kalimdor:440] # Tanaris' in text and 'sun_disc = 1.00, 0.93, 0.80' in text and 'terrain_radius = 4096' in text,
      'section header names the zone; RGB keeps 2 decimals; integers have none')
check(model == {'enabled': True, 'sections': {'Kalimdor:440': {'sun_disc': [1, .93, .8], 'sun_light_mix': .3,
                                                              'terrain_radius': 4096, 'ground_fog_day_scale': 1.25}}},
      'overrides round-trip through northlight-tuning.ini')
check(zt.fmt(1 / 3) == '0.3333' and zt.fmt(2.0) == '2' and zt.fmt(-0.00001) == '0', 'numbers keep at most 4 decimals')
off = zt.edit_overrides(text, enabled=False)
check(zt.parse_overrides(off)['enabled'] is False and off.count('enabled') == 1, 'enabled toggles in place')

# ---- validation ----
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:440', values={'sun_disc_gain': 4.5}), 'disc_gain 4.5')
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:440', values={'sun_fog_gain': 8.01}), 'fog_gain 8.01')
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:440', values={'moon_fog_height_scale': .2}), 'fog_height_scale .2')
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:440', values={'sun_disc': [1, 1.2, 1]}), 'disc component 1.2')
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:440', values={'sun_disc': [1, 1]}), 'disc with 2 components')
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:440', values={'moon_light': [0, 0, .005]}), 'black light')
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:440', values={'terrain_radius': 900}), 'terrain_radius 900')
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:440', values={'terrain_radius': 4128}), 'terrain_radius 4128')
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:440', values={'ground_fog_night_scale': -0.1}), 'ground fog scale -0.1')
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:440', values={'sun_bloom': 1}), 'an unknown key')
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:440', values={'sun_disc_gain': float('nan')}), 'NaN')
rejects(lambda: zt.edit_overrides(text, section='default', values={'terrain_radius': 1024}), 'a zone-only key in [default]')
rejects(lambda: zt.edit_overrides(text, section='Outland:3483', values={'sun_disc_gain': 1}), 'an unsupported map')
rejects(lambda: zt.edit_overrides(text, section='Kalimdor:0', values={'sun_disc_gain': 1}), 'zone id 0')
rejects(lambda: zt.parse_overrides('[tuning]\nenabled = 2\n'), 'enabled = 2')
rejects(lambda: zt.parse_overrides('[Kalimdor:440]\nsun_disc_gain = 1\nsun_disc_gain = 2\n'), 'a duplicate key')
rejects(lambda: zt.parse_overrides('[Kalimdor:440]\n[Kalimdor:440]\n'), 'a duplicate section')
rejects(lambda: zt.parse_overrides('[weather]\n'), 'an unknown section')
check(zt.parse_overrides('[tuning]\nenabled = 1   # A/B\n[default]   # all zones\nsun_fog_gain = 4.3\n')['sections'] ==
      {'default': {'sun_fog_gain': 4.3}}, 'inline comments are allowed, as in the contract example')

# ---- editing one section keeps the others ----
hand = (zt.HEADER + '\n[tuning]\nenabled = 1\n\n# my notes on Elwynn\n[Azeroth:12]\nsun_light_mix = 0.25   # keep\n'
        'terrain_radius = 1856\n\n[Kalimdor:440] # Tanaris\nsun_disc_gain = 3\n# tried 2.5 too\nmoon_fog_gain = 2\n')
edited = zt.edit_overrides(hand, section='Kalimdor:440', values={'sun_disc_gain': 3.25, 'sun_horizon_haze': .5})
check(edited.split('[Kalimdor:440]')[0] == hand.split('[Kalimdor:440]')[0], 'the other sections and their comments are byte-identical')
check('sun_disc_gain = 3.25\nsun_horizon_haze = 0.5\n# tried 2.5 too\n' in edited and 'moon_fog_gain' not in edited,
      'a changed key is replaced in place, a removed key goes, a new key follows the last key')
removed = zt.edit_overrides(hand, section='Azeroth:12', values={})
check('[Azeroth:12]' not in removed and 'sun_light_mix' not in removed and '[Kalimdor:440] # Tanaris\nsun_disc_gain = 3\n' in removed,
      'an empty section is removed; the next one stays')
new_zone = zt.edit_overrides(hand, section='Azeroth:3', values={'sun_disc_gain': 2}, names={'Azeroth:3': 'Badlands'})
check(new_zone.startswith(hand) and new_zone.endswith('\n\n[Azeroth:3] # Badlands\nsun_disc_gain = 2\n'), 'a new section is appended')

# ---- export into copies of client-config ----
work = Path(tempfile.mkdtemp(prefix='zone-tuner-'))
try:
    celestial, shadow = work / 'celestial-profiles.ini', work / 'shadow-range-profiles.ini'
    shutil.copyfile(fp.REPO / 'client-config/celestial-profiles.ini', celestial)
    shutil.copyfile(fp.REPO / 'client-config/shadow-range-profiles.ini', shadow)
    cel_old, sh_old = celestial.read_text(), shadow.read_text()
    old_tanaris = zt.ini_values(celestial)['Kalimdor:440']
    overrides = work / zt.OVERRIDES
    t = zt.edit_overrides('', section='Kalimdor:440', values={
        'sun_disc': [1, .9, .7], 'sun_light_mix': old_tanaris['sun_light_mix'], 'sun_horizon_haze': .6,
        'terrain_radius': 3072, 'ground_fog_day_scale': 1.5})
    t = zt.edit_overrides(t, section='Azeroth:3', values={'moon_disc_gain': 1.4, 'terrain_radius': 2048})
    t = zt.edit_overrides(t, section='Azeroth:12', values={'terrain_radius': 928})
    t = zt.edit_overrides(t, section='default', values={'sun_fog_gain': 4.5})
    overrides.write_text(t)
    out = io.StringIO()
    dry = zt.export(overrides, celestial, shadow, NAMES, dry_run=True, out=out)
    check(dry == [celestial, shadow] and celestial.read_text() == cel_old and shadow.read_text() == sh_old,
          '--dry-run writes nothing')
    out = io.StringIO()
    zt.export(overrides, celestial, shadow, NAMES, out=out)
    report = out.getvalue()
    cel, sh = celestial.read_text(), shadow.read_text()
    old_lines, new_lines = cel_old.splitlines(), cel.splitlines()
    tanaris = new_lines.index('[Kalimdor:440]')
    check(old_lines.index('[Kalimdor:440]') == tanaris, 'the Tanaris section stays where it was')
    section = new_lines[tanaris:new_lines.index('', tanaris)]
    old_section = old_lines[tanaris:old_lines.index('', tanaris)]
    check([l for l in section if l.startswith('sun_disc =')] == ['sun_disc = 1.00, 0.90, 0.70']
          and section.index('sun_disc = 1.00, 0.90, 0.70') == [l.split(' =')[0] for l in old_section].index('sun_disc'),
          'an existing key is replaced in place')
    check(section[-1] == 'sun_horizon_haze = 0.6' and len(section) == len(old_section) + 1, 'a missing key is appended to its section')
    check([l for l in section if l.startswith('sun_light_mix')] == [l for l in old_section if l.startswith('sun_light_mix')],
          'a key with the same value keeps its own formatting')
    check(cel.endswith('\n\n# Badlands\n[Azeroth:3]\nmoon_disc_gain = 1.4\n'), 'a new zone section is appended with its name comment')
    check('sun_fog_gain = 4.5' in new_lines[:new_lines.index('[Azeroth:12]')] and 'terrain_radius' not in cel and 'ground_fog' not in cel,
          '[default] is merged; zone-only keys stay out of celestial-profiles.ini')
    kept = [l for l in old_lines if l.startswith('#')]
    check(all(l in new_lines for l in kept) and len(new_lines) == len(old_lines) + 1 + 4, 'every comment and unrelated line is kept')
    check('[Kalimdor:440] # Tanaris\nterrain_radius = 3072\n' in sh and sh.endswith('\n\n[Azeroth:3] # Badlands\nterrain_radius = 2048\n')
          and '[Azeroth:12] # Elwynn Forest\nterrain_radius = 928\n' in sh, 'shadow ranges: replaced in place, appended, 928 kept in its section')
    check(sh.replace('terrain_radius = 3072', 'terrain_radius = 4096').replace('terrain_radius = 928', 'terrain_radius = 1856')
          == sh_old + '\n[Azeroth:3] # Badlands\nterrain_radius = 2048\n', 'nothing else in shadow-range-profiles.ini changed')
    check('Kalimdor:440' in report and 'day x1.5' in report and 'if(zone==440){out.day*=1.5f;out.nightExtra*=1f;}' in report,
          'ground fog scales are printed for regional_fog.h')
    check('+sun_horizon_haze = 0.6' in report and '-sun_disc = ' in report, 'the export prints a diff')
    out = io.StringIO()
    zt.export(overrides, celestial, shadow, NAMES, out=out)
    check(celestial.read_text() == cel and shadow.read_text() == sh and 'unchanged' in out.getvalue(), 'a second export changes nothing')

    # ---- base values ----
    base = zt.base_values('Kalimdor:440', celestial, shadow)
    check(base['sun_disc'] == [1, .9, .7] and base['terrain_radius'] == 3072 and base['sun_fog_gain'] == 4.5
          and base['moon_fog_height_scale'] == 1, 'base values: zone section over [default] over the built-in profile')

    # ---- HTTP server ----
    root = work / 'game'
    root.mkdir()
    shutil.copyfile(celestial, root / 'celestial-profiles.ini')
    shutil.copyfile(shadow, root / 'shadow-range-profiles.ini')
    server = zt.make_server(zt.Tuner(root, NAMES), 0)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}'

    def call(path, body=None, method=None, host=None):
        req = urllib.request.Request(url + path, data=None if body is None else json.dumps(body).encode(), method=method)
        req.add_header('Content-Type', 'application/json')
        if host:
            req.add_header('Host', host)
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()
    try:
        code, page = call('/')
        check(code == 200 and b'<title>Northlight Zone Tuner</title>' in page and b'/api/overrides' in page, 'GET / serves the page')
        code, body = call('/api/state')
        check(code == 200 and json.loads(body)['status'] == 'missing', 'no state file: status missing')
        state = {'version': 1, 'tick': 1, 'inWorld': True, 'map': 'Kalimdor', 'zone': 440, 'night': .25,
                 'overridesError': '', 'base': {'terrain_radius': 4096}, 'effective': {}, 'terrainReach': 4096}
        (root / zt.STATE).write_text(json.dumps(state))
        code, body = call('/api/state')
        s = json.loads(body)
        check(code == 200 and s['status'] == 'live' and s['section'] == 'Kalimdor:440' and s['zoneName'] == 'Tanaris'
              and s['state']['night'] == .25, 'GET state reads the state file and names the zone')
        old = time.time() - 10
        os.utime(root / zt.STATE, (old, old))
        check(json.loads(call('/api/state')[1])['status'] == 'stale', 'a state file older than 3 s is stale')
        check(json.loads(call('/api/state')[1])['state']['groundFogActive'] is True, 'a state without groundFogActive counts as true')
        (root / zt.STATE).write_text(json.dumps({**state, 'groundFogActive': False}))
        check(json.loads(call('/api/state')[1])['state']['groundFogActive'] is False, 'groundFogActive false is passed through')
        code, body = call('/api/overrides', {'section': 'Kalimdor:440', 'values': {'sun_disc_gain': 2.5, 'terrain_radius': 4096}}, 'PUT')
        o = json.loads(body)
        written = (root / zt.OVERRIDES).read_text()
        check(code == 200 and o['sections'] == {'Kalimdor:440': {'sun_disc_gain': 2.5, 'terrain_radius': 4096}}
              and o['names'] == {'Kalimdor:440': 'Tanaris'} and '[Kalimdor:440] # Tanaris\nsun_disc_gain = 2.5\n' in written,
              'PUT overrides writes northlight-tuning.ini')
        check(not (root / (zt.OVERRIDES + '.tmp')).exists(), 'the temporary file is renamed away')
        code, body = call('/api/overrides', {'section': 'Kalimdor:440', 'values': {'sun_disc_gain': 9}}, 'PUT')
        check(code == 400 and 'disc_gain' in json.loads(body)['error'] and (root / zt.OVERRIDES).read_text() == written,
              'PUT with an out-of-range value is refused and writes nothing')
        code, body = call('/api/overrides', {'section': 'Kalimdor:440', 'values': {'sun_disc_gain': 2.6}, 'seq': 20}, 'PUT')
        check(code == 200 and 'sun_disc_gain = 2.6' in (root / zt.OVERRIDES).read_text(), 'PUT with seq 20 is applied')
        code, body = call('/api/overrides', {'section': 'Kalimdor:440', 'values': {'sun_disc_gain': 1.5}, 'seq': 19}, 'PUT')
        check(code == 200 and json.loads(body).get('stale') is True and 'sun_disc_gain = 2.6' in (root / zt.OVERRIDES).read_text(),
              'an older seq for the same section is dropped as stale')
        code, body = call('/api/overrides', {'section': 'Azeroth:12', 'values': {'sun_disc_gain': 1.5}, 'seq': 1}, 'PUT')
        check(code == 200 and not json.loads(body).get('stale') and 'Azeroth:12' in json.loads(body)['sections'],
              'seq is counted per section')
        code, body = call('/api/overrides', {'section': 'Azeroth:12', 'values': {}, 'seq': 2}, 'PUT')
        code, body = call('/api/overrides', {'enabled': False}, 'PUT')
        check(code == 200 and json.loads(body)['enabled'] is False and 'enabled = 0' in (root / zt.OVERRIDES).read_text(), 'PUT enabled')
        code, body = call('/api/overrides')
        check(code == 200 and json.loads(body)['error'] == '', 'GET overrides parses the file')
        code, body = call('/api/base?section=Kalimdor:440')
        check(code == 200 and json.loads(body)['values']['terrain_radius'] == 3072, 'GET base reads the game root inis')
        code, _ = call('/api/state', host='evil.example:80')
        check(code == 403, 'a foreign Host header is refused (DNS rebinding)')
        check(server.server_address[0] == '127.0.0.1', 'the server binds 127.0.0.1 only')
    finally:
        server.shutdown()
        server.server_close()
finally:
    shutil.rmtree(work, ignore_errors=True)

if failures:
    raise SystemExit(f'FAIL {len(failures)} check(s): ' + '; '.join(failures))
print('PASS zone tuner')
