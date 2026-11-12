"""Convert locally extracted Forever DB2 tables to Northlight's optional NFA1.

Input directory: tables/*.db2 (or *.partial.db2) and WoWDBDefs *.dbd.
No game assets are fetched or included in the repository. See the report for
the source hashes, unsupported sections, and renderer adaptations.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
from forever_db2 import decode


def rgb(value):
    return [(value >> s & 255) / 255 for s in (16, 8, 0)]


def layer(row):
    if row is None:
        return [0.0] * 40
    fields = [row[f'Field_1_60_1_69876_{i:03}'] for i in range(1, 29)]
    colors = [c for v in fields[:4] for c in rgb(v)]
    values = fields[4:21] + fields[23:26] + fields[26] + fields[27]
    return colors + values + [float(bool(fields[21] & bit)) for bit in (1, 2, 4, 8)]


def convert(root, output):
    root, output = Path(root), Path(output)
    names = ('Light', 'LightData', 'LightDataGlobalVolumeFog', 'ZoneLight', 'ZoneLightPoint', 'Weather')
    tables = {name: decode(name, root) for name in names}
    lights = [v for v in tables['Light'].values() if v['ContinentID'] in (0, 1)]
    params = {p for l in lights for p in l['LightParamsID'] if p}
    fog = {}
    for row in tables['LightDataGlobalVolumeFog'].values():
        slot = row['Field_1_60_1_69876_023']
        if not 0 <= slot < 3:
            raise ValueError(f'Unknown fog layer slot {slot}')
        key = (row['LightDataID'], slot)
        if key in fog:
            raise ValueError(f'Duplicate layer {key}')
        fog[key] = row
    keys = sorted((v for v in tables['LightData'].values() if v['LightParamID'] in params),
                  key=lambda k: (k['LightParamID'], k['Time']))
    points, zones = [], []
    light_ids = {l['ID'] for l in lights}
    for z in tables['ZoneLight'].values():
        if z['MapID'] not in (0, 1) or z['LightID'] not in light_ids or z['PlayerConditionID']:
            continue
        outline = sorted((p for p in tables['ZoneLightPoint'].values() if p['ZoneLightID'] == z['ID']), key=lambda p: p['PointOrder'])
        if len(outline) < 3:
            raise ValueError(f'Incomplete zone outline {z["ID"]}')
        zones.append((z['MapID'], z['LightID'], len(points), len(outline), z['Zmin'], z['Zmax']))
        points.extend(p['Pos'] for p in outline)
    data = bytearray(struct.pack('<6I', 0x3141464e, 2, len(lights), len(keys), len(zones), len(points)))
    for l in lights:
        data += struct.pack('<2I5f8I', l['ID'], l['ContinentID'], *l['GameCoords'], l['GameFalloffStart'], l['GameFalloffEnd'], *l['LightParamsID'])
    for k in keys:
        if not 0 <= k['Time'] < 2880:
            raise ValueError('Time must be in half minutes')
        data += struct.pack('<4I6f', k['LightParamID'], k['Time'], k['ID'], 0, *rgb(k['DirectColor']), *rgb(k['AmbientColor']))
        for slot in range(3):
            data += struct.pack('<40f', *layer(fog.get((k['ID'], slot))))
    for z in zones:
        data += struct.pack('<4I2f', *z)
    for p in points:
        data += struct.pack('<2f', *p)
    for k in keys:
        colors = ('SkyTopColor', 'SkyMiddleColor', 'SkyBand1Color', 'SkyBand2Color',
                  'SkySmogColor', 'SkyFogColor', 'SunColor', 'CloudSunColor')
        data += struct.pack('<24f', *(c for name in colors for c in rgb(k[name])))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    report = {'format': 'NFA2', 'source_build': '1.60.1.70888', 'lights': len(lights), 'time_keys': len(keys), 'outlines': len(zones),
              'sha256': hashlib.sha256(data).hexdigest(), 'sources': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'tables').glob('*.db2')},
              'weather': list(tables['Weather'].values())}
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'Wrote {output}: {len(lights)} lights, {len(keys)} time keys, {len(zones)} outlines, {len(data)} bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    convert(args.source, args.output)
