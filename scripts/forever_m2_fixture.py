"""Emit a deterministic loopback ForeverCore scene from the 3.3.5 benchmark layout."""

import argparse
from functools import lru_cache
from hashlib import sha256
import json
from math import floor
from pathlib import Path
import struct

from northrend_fixture_server import M2_STRESS_SIZE, SPAWN, crowd_roster


MAP = 0
ANCHOR = (-10210.0, 1300.0)
# One display per classic race, keeping the same head and shoulder display IDs
# in builds 71037 and 12340. The displays include particle and additive effects.
# Order: human, orc, dwarf, night elf, undead, tauren, gnome, troll.
MODELS = (28172, 15988, 14653, 28190, 15461, 15997, 28192, 28203)
WEAPONS = (19019, 17182)  # Thunderfury and Sulfuras.
FIRST_ENTRY = 950001
FIRST_GUID = 920000001


@lru_cache
def height_grid(directory, x, y):
    data = (directory / f'{MAP:04}_{x:02}_{y:02}.map').read_bytes()
    terrain, version, build = struct.unpack_from('<4sII', data)
    if terrain != b'MAPS' or version != 10 or build != 71037:
        raise ValueError('Expected mapextractor data from Forever 1.60.1.71037')
    offset = struct.unpack_from('<I', data, 20)[0]
    tag, flags, minimum, maximum = struct.unpack_from('<4sIff', data, offset)
    if tag != b'MHGT':
        raise ValueError('Expected a height grid in the fixture map tile')
    return data, offset + 16, flags, minimum, maximum


def terrain_height(directory, x, y):
    gx, gy = 32 - x / (1600 / 3), 32 - y / (1600 / 3)
    tx, ty = floor(gx), floor(gy)
    px, py = (gx - tx) * 128, (gy - ty) * 128
    ix, iy = floor(px), floor(py)
    fx, fy = px - ix, py - iy
    data, offset, flags, minimum, maximum = height_grid(directory, tx, ty)
    if flags & 1:
        return minimum

    def vertex(index):
        if flags & 2:
            return minimum + (maximum - minimum) * struct.unpack_from('<H', data, offset + index * 2)[0] / 65535
        if flags & 4:
            return minimum + (maximum - minimum) * data[offset + index] / 255
        return struct.unpack_from('<f', data, offset + index * 4)[0]

    corner = ix * 129 + iy
    first = vertex(corner) * (1 - fx) + vertex(corner + 129) * fx
    second = vertex(corner + 1) * (1 - fx) + vertex(corner + 130) * fx
    return first * (1 - fy) + second * fy


def build_scene(maps, count, anchor=ANCHOR):
    assert count in (0, 128, M2_STRESS_SIZE)
    roster = list(crowd_roster(M2_STRESS_SIZE, stress=True).values())[:count]
    statements = ['START TRANSACTION;']
    actors = []
    for index, display in enumerate(MODELS):
        entry = FIRST_ENTRY + index
        weapon = WEAPONS[index % len(WEAPONS)]
        statements.extend((
            'INSERT INTO creature_template (entry,name,faction,unit_class,unit_flags,type,BaseAttackTime) '
            f"VALUES ({entry},'Northlight M2 Fixture {index + 1}',35,1,770,7,2000);",
            'INSERT INTO creature_template_model (CreatureID,Idx,CreatureDisplayID,DisplayScale,Probability,VerifiedBuild) '
            f'VALUES ({entry},0,{display},1,1,71037);',
            'INSERT INTO creature_equip_template (CreatureID,ID,ItemID1,VerifiedBuild) '
            f'VALUES ({entry},1,{weapon},71037);',
            'INSERT INTO creature_template_addon (entry,SheathState) '
            f'VALUES ({entry},1);',
        ))
    for index, (_, (x, y, _, orientation), _, _) in enumerate(roster):
        row, column = divmod(index, 32)
        model = (column * 3 + row) % len(MODELS)
        x, y = anchor[0] + x - SPAWN[0], anchor[1] + y - SPAWN[1]
        z = terrain_height(maps, x, y) + .02
        actors.append((FIRST_GUID + index, FIRST_ENTRY + model, MODELS[model], x, y, z, orientation))
    if actors:
        rows = ',\n'.join(
            f'({guid},{entry},{MAP},{display},1,{x:.5f},{y:.5f},{z:.5f},{facing:.6f},71037)'
            for guid, entry, display, x, y, z, facing in actors)
        statements.append('INSERT INTO creature (guid,id,map,modelid,equipment_id,position_x,position_y,position_z,'
                          'orientation,VerifiedBuild) VALUES\n' + rows + ';')
    statements.append('COMMIT;')
    manifest = dict(build=71037, map=MAP, count=count, display_ids=MODELS,
                    weapons={display: WEAPONS[index % len(WEAPONS)] for index, display in enumerate(MODELS)},
                    observer=[*anchor, terrain_height(maps, *anchor) + .02, SPAWN[3]], actors=actors)
    data = '\n'.join(statements) + '\n'
    manifest['sql_sha256'] = sha256(data.encode('ascii')).hexdigest()
    return data, manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--maps', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Empty or new fixture output directory')
    parser.add_argument('--count', type=int, choices=(0, 128, M2_STRESS_SIZE), default=M2_STRESS_SIZE)
    parser.add_argument('--anchor', type=float, nargs=2, metavar=('X', 'Y'), default=ANCHOR)
    options = parser.parse_args()
    data, manifest = build_scene(options.maps.resolve(), options.count, options.anchor)
    options.output.mkdir(parents=True, exist_ok=True)
    with (options.output / 'scene.sql').open('x', encoding='ascii', newline='\n') as stream:
        stream.write(data)
    with (options.output / 'scene.json').open('x', encoding='utf-8') as stream:
        json.dump(manifest, stream, indent=2)


if __name__ == '__main__':
    main()
