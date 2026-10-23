#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""Regional fog is byte-identical across installed locales and a translated-name AreaTable
fixture, in both stock and full views of four Elwynn tiles. The fixture always exercises an
empty English name column, the former failure on non-English clients. Client files stay read-only."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import contextlib, io, json, os, shutil, struct, subprocess
from unittest.mock import patch
import client_archives
import regional_fog_builder as rfb
from build_lighting import DBC, putu, u
from world_scene_builder import Assets

client = fp.client_root()
out = fp.output_dir()
cache = out / 'cache'
shutil.rmtree(cache, ignore_errors=True)
env = dict(os.environ, NORTHLIGHT_STORMLIB=str(fp.stormlib()))
locales = client_archives.installed_locales(client)
assert locales
locale = client_archives.detect_locale(client)


def run(script, *args):
    r = subprocess.run([sys.executable, '-I', '-B', '-X', 'utf8', str(fp.tracked(script)), '--client', str(client),
                        '--without', 'z', *map(str, args)], env=env, capture_output=True, encoding='utf-8',
                       errors='replace', timeout=1800)
    assert r.returncode == 0, (script, args, r.stdout[-1500:], r.stderr[-1500:])


run('world_scene_builder.py', '--instanced', '--map', 'Azeroth', '--tiles', 31, 48, 32, 49, '--archives', 'stock',
    '--locale', locale, '--output', cache)
tiles = sorted(p.stem for p in (cache / 'Azeroth').glob('*.fg3'))
assert len(tiles) == 4, tiles
for view in ('stock', 'all'):
    fog = {}
    for installed in locales:
        folder = out / f'fog-{view}-{installed}'
        shutil.rmtree(folder, ignore_errors=True)
        run('regional_fog_builder.py', '--cache', cache, '--output', folder, '--archives', view, '--locale', installed)
        manifest = json.loads((folder / 'manifest.json').read_text())
        azeroth = manifest['maps']['Azeroth']
        assert (azeroth['tiles'], azeroth['expected']) == (4, 4) and not manifest['unsupported'], manifest
        fog[installed] = {p.name: p.read_bytes() for p in sorted((folder / 'Azeroth').glob('*.frf'))}
        print(view, installed, 'zone 12 named', repr(manifest['zones']['12']['name']))
    # Installed locale folders keep their on-disk case (enus); detect_locale() returns enUS.
    reference = next(files for name, files in fog.items() if name.lower() == locale.lower())
    assert sorted(reference) == [t + '.frf' for t in tiles]
    assert all(files == reference for files in fog.values()), view
    # Always exercise the formerly failing non-English schema, even on a single-locale client.
    # Only the AreaTable name columns change; all geometry and the production builder remain real.
    assets = Assets(client, view, locale, without='z')
    read = assets.read
    table = DBC(read('DBFilesClient\\AreaTable.dbc'))
    for row in table.rows:
        for column in range(11, 27):putu(row, column, 0)
        putu(row, 14, len(table.strings))
        table.strings += f'Localized zone {u(row, 0)}'.encode() + b'\0'
    assets.read = lambda name: table.bytes() if name.lower() == 'dbfilesclient\\areatable.dbc' else read(name)
    translated = out / f'fog-{view}-translated'
    shutil.rmtree(translated, ignore_errors=True)
    args = ['regional_fog_builder.py', '--client', str(client), '--cache', str(cache), '--output', str(translated),
            '--archives', view, '--locale', locale]
    with patch.object(sys, 'argv', args), patch.object(rfb.Assets, 'from_args', return_value=assets), contextlib.redirect_stdout(io.StringIO()):
        rfb.main()
    assert {p.name: p.read_bytes() for p in (translated / 'Azeroth').glob('*.frf')} == reference, view
    zones = {z for blob in reference.values() for z in struct.unpack_from('<256I', blob, 24)}
    assert 12 in zones, sorted(zones)
shutil.rmtree(cache)
print('fog: installed locales and translated-name fixture byte-identical in both views; forest zone present')
