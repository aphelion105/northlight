#!/usr/bin/env python3
# northlight-test:
"""renderer/instance_maps.py (synthetic bytes, no client, no StormLib): the six groups (74 maps, no duplicates,
the unused maps left out, Naxxramas under Wrath and Onyxia under Classic), the selection syntax of resolve(),
read_wdt() for an ADT-tiled and a global-WMO WDT, and tile_of() against the runtime's formula."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import math, struct
import instance_maps as im


def must_raise(fn, text):
    try:
        fn()
    except ValueError as exc:
        assert text in str(exc), exc
        return
    raise AssertionError('no ValueError: ' + text)


# a) The table: order, sizes, names as in Map.dbc.
assert im.KEYS == ['classic-dungeons', 'classic-raids', 'tbc-dungeons', 'tbc-raids', 'wotlk-dungeons', 'wotlk-raids']
assert [g['label'] for g in im.GROUPS] == ['Classic dungeons', 'Classic raids', 'TBC dungeons', 'TBC raids', 'Wrath dungeons', 'Wrath raids']
assert [len(g['maps']) for g in im.GROUPS] == [19, 6, 16, 9, 16, 8]
assert len(im.ALL_MAPS) == 74 == len(set(im.ALL_MAPS)) and im.ALL_MAPS == [m for g in im.GROUPS for m in g['maps']]
assert all(g['est_gb'] > 0 and g['est_minutes'] > 0 for g in im.GROUPS)
assert not {'Monastery', 'Sunwell5Man', 'EmeraldDream', 'test', 'development', 'DeathKnightStart', 'Azeroth'} & set(im.ALL_MAPS)
assert not {m for m in im.ALL_MAPS if m.lower() in ('azeroth', 'kalimdor', 'expansion01', 'northrend')}
assert im.group_of('Stratholme Raid') == 'wotlk-raids' and im.group_of('OnyxiaLairInstance') == 'classic-raids'
assert im.group_of("Zul'gurub") == 'classic-raids' and im.group_of('Sunwell5ManFix') == 'tbc-dungeons'
assert im.group_of('Karazahn') == 'tbc-raids' and im.group_of('CavernsOfTime') == 'tbc-dungeons' and im.group_of('Azeroth') is None
assert im.maps_for(['wotlk-raids', 'classic-raids'])[:2] == ['OnyxiaLairInstance', "Zul'gurub"] and len(im.maps_for(im.KEYS)) == 74
assert im.maps_for(['tbc-raids', 'tbc-raids']) == im.GROUPS[3]['maps'] and im.maps_for([]) == []
must_raise(lambda: im.maps_for(['nope']), 'nope')

# b) resolve(): every input form, GROUPS order, once each, the bad token named.
assert im.resolve(None) == im.resolve('') == im.resolve('none') == im.resolve('  NONE ') == []
assert im.resolve('all') == im.KEYS
assert im.resolve('1 2 6') == ['classic-dungeons', 'classic-raids', 'wotlk-raids']
assert im.resolve('6,1,1') == ['classic-dungeons', 'wotlk-raids']
assert im.resolve('wotlk-raids, tbc-dungeons') == ['tbc-dungeons', 'wotlk-raids']
assert im.resolve('classic-raids 5,3') == ['classic-raids', 'tbc-dungeons', 'wotlk-dungeons']
assert im.resolve('all 2') == im.KEYS
for bad, token in (('7', '7'), ('0', '0'), ('1 classic', 'classic'), ('none 1', 'none'), ('1;2', '1;2')):
    must_raise(lambda: im.resolve(bad), repr(token))


# c) read_wdt(): an ADT-tiled map (MAIN index y*64+x) and a global-WMO map.
def chunk(tag, data):
    return struct.pack('<4sI', tag[::-1].encode(), len(data)) + data


def main_chunk(tiles):
    entries = bytearray(4096 * 8)
    for x, y in tiles:
        struct.pack_into('<II', entries, (y * 64 + x) * 8, 1, 0)
    return chunk('MAIN', bytes(entries))


tiled = chunk('MVER', struct.pack('<I', 18)) + chunk('MPHD', bytes(32)) + main_chunk([(30, 31), (1, 2), (2, 1)])
wdt = im.read_wdt(tiled)
assert wdt == {'global_wmo': False, 'tiles': [(1, 2), (2, 1), (30, 31)], 'mwmo': None, 'modf': None}, wdt
modf = (0, 0xffffffff, 0., 0., 0., 0., 180., 0., -1., -2., -3., 4., 5., 6., 0, 0, 0, 0)
glob = (chunk('MVER', struct.pack('<I', 18)) + chunk('MPHD', struct.pack('<I', 1) + bytes(28)) + main_chunk([]) +
        chunk('MWMO', b'World\\wmo\\Dungeon\\X\\x.wmo\0') + chunk('MODF', struct.pack('<2I12f4H', *modf)))
wdt = im.read_wdt(glob)
assert wdt['global_wmo'] and wdt['tiles'] == [] and wdt['mwmo'] == 'World\\wmo\\Dungeon\\X\\x.wmo', wdt
assert wdt['modf'] == modf and wdt['modf'][6] == 180., wdt
must_raise(lambda: im.read_wdt(chunk('MVER', struct.pack('<I', 18))), 'MPHD')
must_raise(lambda: im.read_wdt(tiled[:-100]), 'chunk')
assert im.wdt_path("Zul'gurub") == "world\\maps\\zul'gurub\\zul'gurub.wdt"


# d) tile_of() is the runtime lookup (world_renderer.h): x from the world Y, y from the world X.
def runtime(x, y):
    return math.floor((17066.6666667 - y) / 533.3333333), math.floor((17066.6666667 - x) / 533.3333333)


assert im.tile_of(0, 0) == (32, 32) == runtime(0, 0)
assert im.tile_of(1000, -500) == (32, 30)   # the axes swap: y feeds the first index
for x in (-1500., -533.4, -0.1, 0., 533.2, 1333., 1500.):
    for y in (-1260., -533.4, 0., 445., 1500.):
        assert im.tile_of(x, y) == runtime(x, y), (x, y)
print('instance_maps: 74 maps in six groups, resolve forms, WDT of both kinds, tile_of OK')
