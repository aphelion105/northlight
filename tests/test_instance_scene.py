#!/usr/bin/env python3
# northlight-test:
"""world_scene_builder on synthetic bytes (no client, no StormLib):
- a global-WMO map (WDT MPHD flag 1): the one WMO lands in every tile its bounds overlap, under the same uid and
  model key, named <x>_<y> by the runtime's lookup, with the zero-offset frame (-z, -x, y) instead of the continents'
  17066.67 one; a WMO group with a non-finite vertex is skipped and reported, the rest is built;
- an ADT whose terrain is fully holed: a tile with placed objects is written without the terrain record, one without
  objects is EmptyTile (empty by design), one whose objects failed to load is still an error;
- several --map values: tiles_total once, one build report per map, a global map as one progress unit."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import collections, hashlib, json, math, struct, tempfile, types
sys.modules.setdefault('mpq', types.SimpleNamespace(Archive=None))   # archives are never opened here
import world_scene_builder as wsb
import instance_maps as im


def chunk(tag, data):
    return struct.pack('<4sI', tag[::-1].encode(), len(data)) + data


def wmo_root(groups):
    return b''.join([chunk('MVER', struct.pack('<I', 17)), chunk('MOHD', struct.pack('<16I', 0, groups, *[0] * 14)),
                     chunk('MOTX', b'wall.blp\0\0\0\0'), chunk('MOMT', struct.pack('<16I', 0, 0, 0, 0, *[0] * 12))])


def wmo_group(verts):
    """One quad (two triangles) over four vertices."""
    n = len(verts)
    sub = b''.join([chunk('MOPY', struct.pack('<4B', 0, 0, 0, 0)), chunk('MOVI', struct.pack('<6H', 0, 1, 2, 2, 1, 3)),
                    chunk('MOVT', struct.pack(f'<{3 * n}f', *[c for v in verts for c in v])),
                    chunk('MONR', struct.pack(f'<{3 * n}f', *[0, 0, 1] * n)), chunk('MOTV', struct.pack(f'<{2 * n}f', *[0, 0] * n)),
                    chunk('MOBA', struct.pack('<6hI3H2B', 0, 0, 0, 0, 0, 0, 0, 6, 0, 3, 0, 0))])
    header = struct.pack('<2I', 0, 0) + struct.pack('<I6f', 0x2000, 0, 0, 0, 1, 1, 1) + bytes(68 - 36)
    return chunk('MVER', struct.pack('<I', 17)) + chunk('MOGP', header + sub)


class FakeAssets:
    def __init__(self, files):
        self.files, self.stats = {k.lower(): v for k, v in files.items()}, collections.Counter()
        self.missing, self.unsupported, self.clamped = set(), set(), set()

    def read(self, name):
        if name.lower() not in self.files:
            raise FileNotFoundError(name)
        return self.files[name.lower()]


def wdt(modf=None, mwmo=b'World\\wmo\\x\\big.wmo\0'):
    flags = 1 if modf else 0
    return b''.join([chunk('MVER', struct.pack('<I', 18)), chunk('MPHD', struct.pack('<I', flags) + bytes(28)), chunk('MAIN', bytes(4096 * 8)),
                     chunk('MWMO', mwmo) if modf else b'', chunk('MODF', struct.pack('<2I12f4H', *modf)) if modf else b''])


def tile_records(path):
    data = path.read_bytes()
    magic, version, count = struct.unpack_from('<4sII', data)
    assert magic == b'FGS3' and version == 3 and len(data) == 12 + 116 * count, path
    return [struct.unpack_from('<32sQI18f', data, 12 + 116 * i) for i in range(count)]


# a) A global-WMO map. A rectangle in the WMO's own axes, large enough to cross several tile borders.
LOCAL = [(-900., 0., -50.), (900., 0., -50.), (-900., 0., 50.), (900., 0., 50.)]   # three groups share it below
UID = 0xffffffff
pos = (100., 5., -200.)   # ADT-axes position of the MODF entry
modf = (0, UID, *pos, 0., 0., 0., -1000., -1000., -1000., 1000., 1000., 1000., 0, 0, 0, 0)
bad = [(float('nan'), 0., 0.)] + LOCAL[1:]
files = {im.wdt_path('Fake'): wdt(modf), 'world\\wmo\\x\\big.wmo': wmo_root(2),
         'world\\wmo\\x\\big_000.wmo': wmo_group([(0., 0., 0.), (0., 700., 0.), (1300., 0., 0.), (1300., 700., 0.)]),
         'world\\wmo\\x\\big_001.wmo': wmo_group(bad)}
with tempfile.TemporaryDirectory() as tmp:
    out = Path(tmp)
    assets = FakeAssets(files)
    builder = wsb.InstancedBuilder(assets, out)
    reports, info = builder.build_global('Fake')
    mesh = builder.wmo('world\\wmo\\x\\big.wmo')
    assert info['skipped_wmo_groups'] == [{'wmo': 'world\\wmo\\x\\big.wmo', 'group': 1, 'reason': 'nonfinite_vertex'}], info
    assert info['kind'] == 'global_wmo' and info['wmo'] == 'world\\wmo\\x\\big.wmo' and info['wmo_uid'] == UID and not info['tiles_outside_grid']
    matrix, _ = wsb.placement(pos, (0., 0., 0.), 1.)
    translation = (-pos[2], -pos[0], pos[1])   # no 17066.67 offset
    world = [tuple(sum(matrix[i][j] * v[j] for j in range(3)) + translation[i] for i in range(3)) for v in mesh[0]]
    # expected tiles from the runtime formula over the rectangle the placed vertices span
    lo = [min(w[i] for w in world) for i in range(2)]
    hi = [max(w[i] for w in world) for i in range(2)]
    corners = {(math.floor((17066.6666667 - y) / 533.3333333), math.floor((17066.6666667 - x) / 533.3333333))
               for x in (lo[0], hi[0]) for y in (lo[1], hi[1])}
    xs, ys = sorted({c[0] for c in corners}), sorted({c[1] for c in corners})
    expected = {(x, y) for x in range(xs[0], xs[-1] + 1) for y in range(ys[0], ys[-1] + 1)}
    assert len(expected) >= 4, expected   # it really spans several tiles
    assert {tuple(r['tile']) for r in reports} == expected, (sorted(r['tile'] for r in reports), sorted(expected))
    assert {p.name for p in (out / 'Fake').glob('*.fg3')} == {f'{x}_{y}.fg3' for x, y in expected}
    key = hashlib.sha256(b'FGS3-v1:world\\wmo\\x\\big.wmo').digest()   # the continents' key scheme
    for x, y in expected:
        records = tile_records(out / 'Fake' / f'{x}_{y}.fg3')
        assert [(r[0], r[1], r[2]) for r in records] == [(key, UID, 2)], records   # the same uid in every tile, no terrain record
        assert tuple(round(v, 3) for v in records[0][-3:]) == tuple(round(v, 3) for v in translation), records[0][-3:]
    assert (out / 'models' / (key.hex() + '.fgs')).is_file() and not list((out / 'models').glob('*.tmp'))
    # the lights builder finds the same owner (key, uid, kind) with the same transform from the WDT alone
    import world_light_placements as placements
    found = list(placements.discover_global(im.read_wdt(files[im.wdt_path('Fake')]), lambda path: placements.wmo_doodads(files[path])))
    assert len(found) == 1 and found[0][0] == 'world\\wmo\\x\\big.wmo', found
    scene = tile_records(out / 'Fake' / f'{xs[0]}_{ys[0]}.fg3')[0]
    assert (found[0][1][0], found[0][1][1], found[0][1][2]) == scene[:3] and found[0][1][9:] == scene[9:], (found, scene)
    one = out / 'Fake' / f'{xs[0]}_{ys[0]}.fg3'
    assert wsb.built_global(out, 'Fake') is False   # no report yet: a resumed run builds it again
    (out / 'build-Fake-1-1.json').write_text(json.dumps({'generated': reports}))
    assert wsb.built_global(out, 'Fake') is True
    one.unlink()
    assert wsb.built_global(out, 'Fake') is False   # a tile of the report is gone


# b) ADT tiles with fully holed terrain.
def mcnk(holes):
    header = bytearray(128)
    struct.pack_into('<I', header, 60, holes)
    struct.pack_into('<I', header, 20, 8 + 128)   # ofsHeight, relative to the chunk header
    return chunk('MCNK', bytes(header) + chunk('MCVT', struct.pack('<145f', *[0.] * 145)))


def adt(holes, wmo=None):
    parts = [chunk('MVER', struct.pack('<I', 18)), mcnk(holes)]
    if wmo:
        parts += [chunk('MWMO', wmo.encode() + b'\0'), chunk('MWID', struct.pack('<I', 0)),
                  chunk('MODF', struct.pack('<2I12f4H', 0, 77, 17066.6666667, 0., 17066.6666667, 0., 0., 0., *[0.] * 6, 0, 0, 0, 0))]
    return b''.join(parts)


files = {'world\\maps\\fake\\fake_1_1.adt': adt(0xffff, 'a.wmo'),   # holed, with a WMO
         'world\\maps\\fake\\fake_1_2.adt': adt(0xffff),            # holed, nothing placed
         'world\\maps\\fake\\fake_1_3.adt': adt(0),                 # open terrain
         'world\\maps\\fake\\fake_1_4.adt': adt(0xffff, 'gone.wmo'),   # holed, its WMO cannot be read
         'a.wmo': wmo_root(1), 'a_000.wmo': wmo_group(LOCAL)}
with tempfile.TemporaryDirectory() as tmp:
    out = Path(tmp)
    builder = wsb.InstancedBuilder(FakeAssets(files), out)
    report = builder.build('Fake', 1, 1, out / 'Fake' / '1_1.fg3')
    records = tile_records(out / 'Fake' / '1_1.fg3')
    assert [r[2] for r in records] == [2] and records[0][1] == 77 and report['terrain_bytes'] == 0 and report['triangles'] == 0, records
    assert report['instances'] == 1 and report['bytes'] == (out / 'Fake' / '1_1.fg3').stat().st_size and report['tile'] == [1, 1]
    assert all(lo <= hi for lo, hi in zip(*report['bounds'])) and not list((out / 'models').glob('*terrain*'))
    try:
        builder.build('Fake', 1, 2, out / 'Fake' / '1_2.fg3')
        raise AssertionError('an empty tile was written')
    except wsb.EmptyTile as exc:
        assert 'Empty world tile' in str(exc) and not (out / 'Fake' / '1_2.fg3').exists()
    try:
        builder.build('Fake', 1, 4, out / 'Fake' / '1_4.fg3')
        raise AssertionError('a tile whose objects failed was accepted')
    except wsb.EmptyTile:
        raise AssertionError('a tile whose WMO failed to load is an error, not empty by design')
    except ValueError as exc:
        assert str(exc) == 'Empty world tile' and any('gone.wmo' in u for u in builder.assets.unsupported), builder.assets.unsupported
    builder.build('Fake', 1, 3, out / 'Fake' / '1_3.fg3')
    assert [r[2] for r in tile_records(out / 'Fake' / '1_3.fg3')] == [0], 'open terrain keeps its terrain record'
    # the FGS2 builder (no --instanced) keeps its plain ValueError for a tile with no geometry at all
    try:
        wsb.Builder(FakeAssets(files)).build('Fake', 1, 2, out / 'x.fgs')
        raise AssertionError('Builder wrote an empty tile')
    except ValueError as exc:
        assert 'Empty world tile' in str(exc) and not isinstance(exc, wsb.EmptyTile)


# c) main(): several maps, one total, a report per map.
class Args(types.SimpleNamespace):
    pass


files.update({im.wdt_path('Fake'): wdt(modf), 'world\\wmo\\x\\big.wmo': wmo_root(1),
              'world\\wmo\\x\\big_000.wmo': wmo_group([(0., 0., 0.), (0., 700., 0.), (1300., 0., 0.), (1300., 700., 0.)])})
main_tiles = bytearray(4096 * 8)
for x, y in ((1, 1), (1, 2)):
    struct.pack_into('<II', main_tiles, (y * 64 + x) * 8, 1, 0)
files[im.wdt_path('Tiled')] = b''.join([chunk('MVER', struct.pack('<I', 18)), chunk('MPHD', bytes(32)), chunk('MAIN', bytes(main_tiles))])
for name in ('fake_1_1', 'fake_1_2'):
    files[f'world\\maps\\tiled\\{name.replace("fake", "tiled")}.adt'] = files[f'world\\maps\\fake\\{name}.adt']
with tempfile.TemporaryDirectory() as tmp:
    out = Path(tmp)
    fake = FakeAssets(files)
    fake.client, fake.providers, fake.warnings = out, {}, lambda: []
    fake.paths, fake.fingerprint, fake.close = [], lambda: {}, lambda: None
    sys.argv = ['world_scene_builder.py', '--instanced', '--map', 'Tiled', 'Fake', '--output', str(out / 'cache')]
    printed = []
    wsb.Assets.from_args = classmethod(lambda cls, args: fake)
    wsb.print = lambda *a, **k: printed.append(a[0])
    try:
        wsb.main()
    finally:
        del wsb.print
    events = [json.loads(p) for p in printed if p.startswith('{')]
    assert events[0] == {'tiles_total': 3, 'maps': ['Tiled', 'Fake']}, events[0]   # 2 ADT tiles + 1 unit for the global map
    done = [e for e in events if 'tile' in e]
    assert [e['tile'] for e in done] == ['Tiled/1_1', 'Tiled/1_2', 'Fake/global'], done
    assert done[1].get('empty') is True   # the holed tile without objects still counts as progress
    names = sorted(p.name.rsplit('-', 2)[0] for p in (out / 'cache').glob('build-*.json'))
    assert names == ['build-Fake', 'build-Tiled'], names
    tiled = json.loads(next((out / 'cache').glob('build-Tiled-*.json')).read_text())
    assert len(tiled['generated']) == 1 and tiled['empty_source_tiles'] == [{'map': 'Tiled', 'tile': [1, 2]}] and not tiled['failures'], tiled
    fake_report = json.loads(next((out / 'cache').glob('build-Fake-*.json')).read_text())
    assert fake_report['generated'] and not fake_report['failures'] and fake_report['global_wmo']['wmo_uid'] == UID
    assert not any(t['map'] != 'Fake' for t in fake_report['generated']), 'a report holds one map'
print('instance_scene: global WMO in every overlapping tile with one uid, zero-offset frame, bad group skipped, '
      'holed tiles with/without objects, per-map reports OK')
