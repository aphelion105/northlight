#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""Report the historical archive-order migration on the configured client and verify that
today's world builder resolves all world/texture providers in game order. Historical changes
depend on the installed pack and locale; they are evidence, not a fixed client fixture.
Reads MPQ listfiles only; no game, no Wine."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import collections, json
import client_archives
from mpq import Archive
from world_scene_builder import Assets

client = fp.client_root()
data = client_archives.data_dir(client)
locale = client_archives.detect_locale(client).lower()


def legacy():
    """world_scene_builder.Assets at 0.3.161: hard-coded enUS, patch*.mpq by name, enus/*.mpq by name."""
    paths = [data / n for n in ('common.mpq', 'common-2.mpq', 'expansion.mpq', 'lichking.mpq')]
    paths += sorted(data.glob('patch*.mpq'), key=lambda p: (p.name != 'patch.mpq', p.name))
    paths += sorted((data / 'enus').glob('*.mpq'), key=lambda p: (p.name.startswith('patch'), p.name))
    return [p for p in paths if p.exists()]


listing = {}


def winners(paths):
    result = {}
    for p in paths:
        key = p.resolve()
        if key not in listing:
            with Archive(p) as a:
                listing[key] = [n.lower() for n in a.names()]
        for name in listing[key]:
            result[name] = p.relative_to(client).as_posix().lower()
    return result


before, after = winners(legacy()), winners(client_archives.chain(client))
changed = sorted(n for n in set(before) | set(after) if before.get(n) != after.get(n))
by_folder = collections.Counter(n.split('\\')[0] for n in changed if n in after and n in before)
dropped = collections.Counter(n.split('\\')[0] for n in changed if n not in after)   # speech, base, backup archives
watch = ['dbfilesclient\\light.dbc', 'dbfilesclient\\lightskybox.dbc', 'dbfilesclient\\lightparams.dbc',
         'dbfilesclient\\areatable.dbc', 'dbfilesclient\\liquidtype.dbc', 'dbfilesclient\\map.dbc']
report = {'client_locale': locale, 'legacy_order': [p.relative_to(client).as_posix() for p in legacy()],
          'game_order': [p.relative_to(client).as_posix() for p in client_archives.chain(client)],
          'winners': {n: {'before': before.get(n), 'after': after.get(n)} for n in watch},
          'changed_winners': sum(by_folder.values()), 'changed_by_folder': dict(by_folder.most_common()),
          'no_longer_loaded_by_folder': dict(dropped.most_common()),
          'changed_dbc': [n for n in changed if n.startswith('dbfilesclient\\')],
          'changed_world_or_texture': [n for n in changed if n.startswith(('world\\', 'textures\\', 'tileset\\'))]}
(fp.output_dir() / 'archive-order.json').write_text(json.dumps(report, indent=2) + '\n')
for n in watch[:1] + watch[3:4]:
    print(f"{n}: {before.get(n)} -> {after.get(n)}")
print('changed winners by folder:', dict(by_folder.most_common()), '| no longer loaded:', sum(dropped.values()))
# The historical migration report depends on the installed pack and locale. Current builders
# must resolve every world/texture provider in the game's order, including locale overrides.
assets = Assets(client, 'all', client_archives.detect_locale(client))
try:
    checked = [n for n in after if n.startswith(('world\\', 'textures\\', 'tileset\\'))]
    assert checked
    for name in checked:
        assert assets.origin(name).lower().replace('\\', '/') == after[name], name
finally:
    assets.close()
print('game-order world/texture providers checked:', len(checked))
