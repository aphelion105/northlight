"""Extract the sky's float tables from the audited local Forever executable.

No executable code or textures are copied into Northlight. Unknown builds fail
closed; the source hash and original offsets accompany the generated constants.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SOURCE_SHA256 = '054a166a86c2ab79f49366d319458a77c0653f0410c6add534385796f1dc32a9'
TABLES = {
    'SunPolar': (0x49067A8, 7), 'MoonPolar': (0x49067E0, 6),
    'SunAzimuth': (0x4906850, 3), 'MoonAzimuth': (0x4906868, 3),
    'SecondaryMoonPolar': (0x4906880, 5), 'SecondaryMoonAzimuth': (0x49068A8, 3),
    'SunScale': (0x49068C0, 4), 'MoonScale': (0x49068E0, 4),
}

def extract(path):
    data = Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise ValueError('Expected the audited Forever 1.60.1.70888 executable')
    pe = struct.unpack_from('<I', data, 60)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    sections = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0]
    def offset(rva):
        for i in range(count):
            _, va, size, raw = struct.unpack_from('<4I', data, sections + i * 40 + 8)
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError('Table outside PE sections')
    return {name: {'rva': hex(rva), 'file_offset': offset(rva),
                   'keys': [struct.unpack_from('<2f', data, offset(rva) + i * 8) for i in range(n)]}
            for name, (rva, n) in TABLES.items()}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    tables = extract(args.exe)
    lines = ['#pragma once', '// Extracted by scripts/import_forever_celestial.py; Forever 1.60.1.70888.',
             '// Source SHA256: ' + SOURCE_SHA256,
             'namespace NorthlightForeverSky {', 'struct Key {float day,value;};']
    for name, table in tables.items():
        values = ','.join('{' + ','.join(format(v, '.9e') + 'f' for v in pair) + '}' for pair in table['keys'])
        lines += [f'// RVA {table["rva"]}', f'inline constexpr Key {name}[]={{' + values + '};']
    lines += ['} // namespace NorthlightForeverSky', '']
    args.output.write_text('\n'.join(lines), encoding='utf-8', newline='\n')
    args.output.with_suffix('.json').write_text(json.dumps({'source_build': '1.60.1.70888',
        'source_sha256': SOURCE_SHA256, 'tables': tables}, indent=2) + '\n', encoding='utf-8', newline='\n')

if __name__ == '__main__':
    main()
