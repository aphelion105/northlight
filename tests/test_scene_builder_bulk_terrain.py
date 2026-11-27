#!/usr/bin/env python3
# northlight-test:
"""world_scene_builder's terrain and FGS2 writer (no client): the unrolled MCNK terrain and the bulk writer
produce exactly the bytes of the original per-vertex code (kept below as the reference), on random heights,
random hole masks and a flat chunk; a nonfinite or float32-overflowing vertex and an out-of-range triangle are
ValueErrors; --band parts (band_tiles) cover every tile exactly once in contiguous, sorted ranges; an
InstancedBuilder that releases exported meshes places them exactly as one that keeps them."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import collections, math, random, shutil, struct
import world_scene_builder as w

out = fp.output_dir()
shutil.rmtree(out / 'scene', ignore_errors=True)


class NoAssets:
    """The terrain's first-layer texture is missing: material() records it and uses the flat fallback colour."""
    def __init__(self):
        self.unsupported, self.stats = set(), collections.Counter()

    def read(self, name):
        raise FileNotFoundError(name)


def mcnk(heights, holes, position):
    """An MCNK payload: the 128-byte header (holes at 60, position at 104, MCVT offset at 20, no MCLY) + MCVT."""
    header = bytearray(128)
    struct.pack_into('<3I', header, 0, 0, 3, 5)
    struct.pack_into('<I', header, 20, 128 + 8)    # relative to the MCNK chunk header, 8 bytes before the payload
    struct.pack_into('<I', header, 60, holes)
    struct.pack_into('<3f', header, 104, *position)
    return bytes(header) + b'TVCM' + struct.pack('<I', 580) + struct.pack('<145f', *heights)


def reference_terrain(self, chunk, textures):
    """Builder.terrain as it was before the unrolled version (same arithmetic, one vertex at a time)."""
    flags, ix, iy = w.unpack('<3I', chunk); holes = w.unpack('<I', chunk, 60)[0]
    px, py, pz = w.unpack('<3f', chunk, 104)
    heights = w.unpack('<145f', chunk, 136)
    material = self.material('', 0, True)
    base = len(self.vertices); positions = []; grid = {}; i = 0
    for row in range(17):
        for col in range(8 if row & 1 else 9):
            p = (px - row * w.UNIT * .5, py - (col + .5 * (row & 1)) * w.UNIT, pz + heights[i]); i += 1
            positions.append(p); grid[(row, col)] = len(positions) - 1
    local_triangles = []
    for row in range(8):
        for col in range(8):
            if holes & (1 << ((row // 2) * 4 + col // 2)): continue
            a, b = grid[(row * 2, col)], grid[(row * 2, col + 1)]
            c, d = grid[(row * 2 + 2, col)], grid[(row * 2 + 2, col + 1)]
            center = grid[(row * 2 + 1, col)]
            local_triangles.extend(((a, b, center), (b, d, center), (d, c, center), (c, a, center)))
    normal = [[0., 0., 0.] for p in positions]
    for a, b, c in local_triangles:
        pa, pb, pc = positions[a], positions[b], positions[c]
        u = tuple(pb[i] - pa[i] for i in range(3)); v = tuple(pc[i] - pa[i] for i in range(3))
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        if n[2] < 0: n = tuple(-x for x in n)
        for index in (a, b, c):
            for j in range(3): normal[index][j] += n[j]
        self.triangles.append((base + a, base + b, base + c, material))
    self.vertices.extend(p + w.normalize(normal[i]) + (0., 0.) for i, p in enumerate(positions))
    self.tile_stats['terrain_chunks'] += 1; self.tile_stats['terrain_triangles'] += len(local_triangles)


def reference_write(self, destination):
    """Builder.write_scene as it was before the bulk writer."""
    with destination.open('wb') as f:
        f.write(struct.pack('<4s4I', b'FGS2', 2, len(self.vertices), len(self.triangles), len(self.materials)))
        for vertex in self.vertices:
            f.write(struct.pack('<8f', *vertex))
        for triangle in self.triangles:
            f.write(struct.pack('<4I', *triangle))
        for color, mw, mh, cutoff, rgba in self.materials:
            f.write(struct.pack('<3fIIfI', *color, mw, mh, cutoff, len(rgba))); f.write(rgba)


def build(terrain, chunks):
    builder = w.Builder(NoAssets())
    for chunk in chunks:
        terrain(builder, chunk, [])
    return builder


rng = random.Random(20261127)
chunks = [mcnk([0.] * 145, 0, (100., -200., 5.)),                                     # flat: every normal is +z
          mcnk([rng.uniform(-40, 40) for _ in range(145)], 0, (17066.66, 17066.66, 0.)),
          mcnk([rng.uniform(-400, 400) for _ in range(145)], 0xffff, (-3000.5, 4000.25, 90.))]   # fully holed
chunks += [mcnk([rng.gauss(0, 30) for _ in range(145)], rng.getrandbits(16),
                (rng.uniform(-17066, 17066), rng.uniform(-17066, 17066), rng.uniform(-500, 500))) for _ in range(40)]
new, old = build(w.Builder.terrain, chunks), build(reference_terrain, chunks)
pack = lambda rows: b''.join(struct.pack(f'<{len(r)}d', *r) for r in rows)      # exact doubles, -0.0 included
assert len(new.vertices) == len(old.vertices) == 145 * len(chunks)
assert pack(new.vertices) == pack(old.vertices), 'terrain vertices differ from the per-vertex reference'
assert new.triangles == old.triangles and new.tile_stats == old.tile_stats and new.materials == old.materials
assert all(n[3:6] == (0., 0., 1.) for n in new.vertices[:145])                    # the flat chunk
print(f'terrain: {len(chunks)} chunks, {len(new.triangles)} triangles, identical to the reference')

target = out / 'scene' / 'terrain.fgs'
new.write_scene(target)
reference_write(old, out / 'scene' / 'reference.fgs')
assert target.read_bytes() == (out / 'scene' / 'reference.fgs').read_bytes(), 'bulk FGS2 differs from per-vertex packing'
report = new.finish('Test', 1, 2, out / 'scene' / 'finish.fgs')
assert report['bounds'] == [[min(v[i] for v in new.vertices) for i in range(3)], [max(v[i] for v in new.vertices) for i in range(3)]]
assert not list((out / 'scene').glob('*.tmp'))
print('writer: FGS2 bytes identical to the per-vertex reference; bounds unchanged')

for vertex, why in [((math.nan,) + (0.,) * 7, 'nan'), ((math.inf,) + (0.,) * 7, 'inf'), ((1e39,) + (0.,) * 7, 'float32 overflow')]:
    bad = build(w.Builder.terrain, chunks[:1]); bad.vertices.append(vertex)
    try:
        bad.write_scene(out / 'scene' / 'bad.fgs'); raise AssertionError(why + ' was written')
    except ValueError as e:
        assert 'Nonfinite vertex' in str(e), e
for triangle in [(0, 0, 145, 0), (0, 0, 0, 1)]:
    bad = build(w.Builder.terrain, chunks[:1]); bad.triangles.append(triangle)
    try:
        bad.write_scene(out / 'scene' / 'bad.fgs'); raise AssertionError(f'{triangle} was written')
    except ValueError as e:
        assert 'Invalid output triangle' in str(e), e
assert not (out / 'scene' / 'bad.fgs').exists()
print('writer: nonfinite, float32-overflowing vertices and out-of-range triangles are ValueErrors, nothing written')

tiles = [(x, y) for x in range(64) for y in range(64) if rng.random() < .3]
rng.shuffle(tiles)
for count in range(1, 9):
    parts = [w.band_tiles(tiles, k, count) for k in range(count)]
    assert [t for part in parts for t in part] == sorted(tiles)                     # every tile once, in order
    assert max(map(len, parts)) - min(map(len, parts)) <= 1
assert w.band_tiles([], 0, 4) == [] and w.band_tiles([(1, 1)], 3, 4) == [(1, 1)] and w.band_tiles([(1, 1)], 0, 4) == []
print('bands: 1-8 parts cover every tile once, contiguous and balanced')

# Release of exported meshes (the command line's InstancedBuilder): later placements of a released model give the same
# instances, statistics and model file as with the parsed mesh kept; a WMO stub keeps its root and doodad refs.
def placements(release):
    folder = out / 'scene' / ('released' if release else 'kept')
    builder = w.InstancedBuilder(NoAssets(), folder, release_exported=release)
    vertices = [(float(i), float(i * i % 7), float(i % 3), 0., 0., 1., 0., 0.) for i in range(9)]
    triangles = [(0, 1, 2, 0), (2, 3, 4, 0), (5, 6, 7, 0)]
    for name, extra in (('doodad.m2', ()), ('house.wmo', ({'MODN': b''}, {1, 2}))):
        mesh = (list(vertices), list(triangles), [('', 0, False)], *extra)
        builder.meshes[name] = mesh; builder.mesh_names[id(mesh[0])] = name
    rotation = ((0., -1., 0.), (1., 0., 0.), (0., 0., 2.))
    for name, category in (('doodad.m2', 'm2'), ('house.wmo', 'wmo'), ('doodad.m2', 'wmo_doodad'), ('house.wmo', 'wmo')):
        mesh = builder.meshes[name]
        builder.add_mesh(mesh[:3], rotation, (10., 20., 30.), category, len(builder.instances))
    files = {p.name: p.read_bytes() for p in (folder / 'models').glob('*.fgs')}
    return builder, files

kept, kept_files = placements(False)
released, released_files = placements(True)
assert released.instances == kept.instances and released.tile_stats == kept.tile_stats and released_files == kept_files
assert len(kept_files) == 2 and all(kept.meshes[n][0] for n in kept.meshes)
for name in ('doodad.m2', 'house.wmo'):
    stub = released.meshes[name]
    assert stub[0] == [] and stub[1] == range(3) and released.mesh_names[id(stub[0])] == name
assert released.meshes['house.wmo'][3:] == ({'MODN': b''}, {1, 2}) and len(released.mesh_names) == 2
print('release: placements of released meshes are identical; WMO stubs keep root and doodad refs')
