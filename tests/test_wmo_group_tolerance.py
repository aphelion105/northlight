#!/usr/bin/env python3
# northlight-test:
"""An oversized top-level MOGP in a WMO group file (synthetic bytes, no client, no StormLib):
- chunks() still raises on any overrun by default, and with clamp='MOGP' on every other tag;
- the scene builder's wmo() gives the same mesh for an exact and an oversized MOGP and records the
  clamped group; a group whose last sub-chunk is really cut still raises (sub-chunks stay strict);
- fog's indoor_groups() gives the same bounds; a clamped header under 68 bytes raises;
- a fog tile with one unreadable WMO keeps the other's bounds and records the unreadable one, read once."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import collections, struct, types
sys.modules.setdefault('mpq', types.SimpleNamespace(Archive=None))   # archives are never opened here
from world_scene_builder import Builder, chunks, wmo_group
import regional_fog_builder as fog


def chunk(tag, data, extra=0):
    return struct.pack('<4sI', tag[::-1].encode(), len(data) + extra) + data


def must_raise(fn, text):
    try:
        fn()
    except ValueError as exc:
        assert text in str(exc), exc
        return
    raise AssertionError('no ValueError: ' + text)


BOUNDS = (-1., -2., -3., 4., 5., 6.)
ROOT = b''.join([chunk('MVER', struct.pack('<I', 17)), chunk('MOHD', struct.pack('<16I', 0, 1, *[0] * 14)),
                 chunk('MOTX', b'wall.blp\0\0\0\0'), chunk('MOMT', struct.pack('<16I', 0, 0, 0, 0, *[0] * 12))])


def group(extra=0, cut=0, flags=0x2000, moba_flags=0, tz=0, modr=()):
    sub = b''.join([chunk('MOPY', struct.pack('<2B', 0, 0)), chunk('MOVI', struct.pack('<3H', 0, 1, 2)),
                    chunk('MOVT', struct.pack('<9f', 0, 0, 0, 1, 0, 0, 0, 1, 0)), chunk('MONR', struct.pack('<9f', 0, 0, 1, 0, 0, 1, 0, 0, 1)),
                    chunk('MOTV', struct.pack('<6f', 0, 0, 1, 0, 0, 1)), chunk('MOBA', struct.pack('<6hI3H2B', 0, 0, 0, 0, 0, tz, 0, 3, 0, 2, moba_flags, 0))]
                   + ([chunk('MODR', struct.pack(f'<{len(modr)}H', *modr))] if modr else []))
    header = struct.pack('<2I', 0, 0) + struct.pack('<I6f', flags, *BOUNDS) + bytes(68 - 36)
    blob = chunk('MVER', struct.pack('<I', 17)) + chunk('MOGP', header + sub, extra)
    return blob[:len(blob) - cut] if cut else blob


class FakeAssets:
    def __init__(self, files):
        self.files, self.stats = files, collections.Counter()
        self.missing, self.unsupported, self.clamped = set(), set(), set()

    def read(self, name):
        if name not in self.files:
            raise FileNotFoundError(name)
        return self.files[name]


# a) Default chunks() is unchanged; only a top-level MOGP may be clamped, to the rest of the file.
exact = group()
must_raise(lambda: list(chunks(group(extra=64))), "Chunk exceeds file: b'PGOM'")
must_raise(lambda: list(chunks(chunk('MVER', b'\0' * 4) + chunk('MCNK', b'\0' * 8, 4))), "Chunk exceeds file: b'KNCM'")
must_raise(lambda: list(chunks(exact + chunk('MVER', b'\0' * 4, 4), clamp='MOGP')), "Chunk exceeds file: b'REVM'")
top, overrun = wmo_group(group(extra=64))
assert overrun == 64 and top['MOGP'] == dict(chunks(exact))['MOGP'] and wmo_group(exact) == (dict(chunks(exact)), 0)

# b) Scene: the same mesh for the exact and the oversized MOGP; only the second is recorded as clamped.
meshes = []
for extra in (0, 64):
    assets = FakeAssets({'x.wmo': ROOT, 'x_000.wmo': group(extra=extra)})
    meshes.append(Builder(assets).wmo('x.wmo')[:3])
    assert assets.clamped == ({'x_000.wmo'} if extra else set()), assets.clamped
assert meshes[0] == meshes[1] and len(meshes[0][1]) == 1, meshes

# c) A clamped group whose last sub-chunk is really cut still raises (no silent partial mesh).
assets = FakeAssets({'x.wmo': ROOT, 'x_000.wmo': group(extra=10, cut=10)})
must_raise(lambda: Builder(assets).wmo('x.wmo'), "Chunk exceeds file: b'ABOM'")

# d) Fog: the same indoor bounds from the header; a clamped header under 68 bytes raises.
results = []
for extra in (0, 64):
    stats, clamped = collections.Counter(), set()
    results.append(fog.indoor_groups({'x.wmo': ROOT, 'x_000.wmo': group(extra=extra)}.__getitem__, 'x.wmo', stats, clamped))
    assert clamped == ({'x_000.wmo'} if extra else set()) and stats['unique_indoor_groups'] == 1
assert results[0] == results[1] == [tuple(struct.unpack('<6f', struct.pack('<6f', *BOUNDS)))], results
short = chunk('MVER', struct.pack('<I', 17)) + chunk('MOGP', bytes(40), 200)
must_raise(lambda: fog.indoor_groups({'x.wmo': ROOT, 'x_000.wmo': short}.__getitem__, 'x.wmo', stats, set()),
           'Truncated MOGP header')

# e) Fog tile with two WMOs, one unreadable (no root): the tile keeps the readable one's bounds.
files = {'a.wmo': ROOT, 'a_000.wmo': group(extra=64)}
reads = collections.Counter()


def read(name):
    reads[name] += 1
    return files[name]


modf = b''.join(struct.pack('<2I12f4H', i, i, 17066.666666666668, 0, 17066.666666666668, *[0.] * 9, 0, 0, 0, 0) for i in (0, 1))
adt = dict(chunks(chunk('MWMO', b'a.wmo\0b.wmo\0') + chunk('MWID', struct.pack('<2I', 0, 6)) + chunk('MODF', modf)))
memo, stats, clamped, unreadable = {}, collections.Counter(), set(), {}
for _ in range(2):   # a second tile placing the same WMOs reads neither again
    indoors = fog.placed_indoors(adt, lambda path: fog.wmo_indoors(memo, read, path, stats, clamped, unreadable))
    assert len(indoors) == 1 and all(lo <= hi for lo, hi in zip(indoors[0][:3], indoors[0][3:])), indoors
assert list(unreadable) == ['b.wmo'] and 'b.wmo' in unreadable['b.wmo'] and clamped == {'a_000.wmo'}, (unreadable, clamped)
assert stats['wmo_roots'] == 2 and reads == {'a.wmo': 1, 'a_000.wmo': 1, 'b.wmo': 1}, (stats, reads)
# f) Interior is the game's test (CMap::CreateMapObjDefGroups): neither 0x8 (outdoor) nor 0x40 (exterior lit).
for flags, interior in ((0x2000, True), (0, True), (0x2000 | 0x8, False), (0x2000 | 0x40, False), (0x48, False), (0x8, False)):
    got = fog.indoor_groups({'x.wmo': ROOT, 'x_000.wmo': group(flags=flags)}.__getitem__, 'x.wmo', collections.Counter(), set())
    assert bool(got) == interior, (hex(flags), got)

# g) The batch material is always the byte at +0x17 (CMapObj::UnifiedRender), whatever the low flag nibble holds.
mesh = Builder(FakeAssets({'x.wmo': ROOT, 'x_000.wmo': group(moba_flags=2, tz=5)})).wmo('x.wmo')
assert [t[3] for t in mesh[1]] == [0], mesh[1]

# h) The doodad refs (MODR) of every group are collected for the doodad selection.
mesh = Builder(FakeAssets({'x.wmo': ROOT, 'x_000.wmo': group(modr=(3, 7))})).wmo('x.wmo')
assert mesh[4] == {3, 7}, mesh[4]
print('wmo_group_tolerance: clamp only top-level MOGP, identical scene mesh and fog bounds, strict sub-chunks, '
      'unreadable WMO recorded without rejecting the tile OK')
