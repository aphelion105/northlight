"""Extract shadow shader evidence from an isolated local Forever CASC copy.

Never opens the Battle.net installation. Unavailable entries remain explicit
errors in the manifest; they are not evidence that the client lacks a shader.
This tool neither copies nor downloads client files.
"""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import zlib


def dxbc_programs(package):
    """Decode the observed HSXG zlib blocks, including programs across blocks.

    This is an evidence extractor, not a complete BLS permutation-table reader.
    The number of recovered containers is not a count of runtime permutations.
    """
    if package[:4] != b'HSXG':
        raise ValueError('Expected the observed HSXG shader package')
    blocks, offset = [], 0
    while offset < len(package):
        offset = package.find(b'\x78', offset)
        if offset < 0:
            break
        decoder = zlib.decompressobj()
        try:
            data = decoder.decompress(package[offset:], 64 * 1024 * 1024)
        except zlib.error:
            offset += 1
            continue
        if not decoder.eof:
            offset += 1
            continue
        blocks.append(data)
        offset = len(package) - len(decoder.unused_data)
    data, offset = b''.join(blocks), 0
    while offset < len(data):
        offset = data.find(b'DXBC', offset)
        if offset < 0:
            break
        size = struct.unpack_from('<I', data, offset + 24)[0] if offset + 32 <= len(data) else 0
        if 32 <= size <= len(data) - offset:
            yield data[offset:offset + size]
            offset += size
        else:
            offset += 4


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--storage', type=Path, required=True)
    parser.add_argument('--casclib', type=Path, required=True)
    parser.add_argument('--listfile', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--disassemble', action='append', default=[], metavar='PACKAGE',
                        help='Exact listfile shader name to decode and disassemble')
    parser.add_argument('--dxc', type=Path, help='Local DXC executable for -dumpbin')
    args = parser.parse_args()
    storage = args.storage.resolve()
    for path in (storage, args.casclib, args.listfile, args.output, args.dxc):
        if path and 'battle.net' in (part.lower() for part in path.resolve().parts):
            parser.error('Use isolated copies and tools, never the Battle.net installation.')
    if args.disassemble and not args.dxc:
        parser.error('--disassemble requires --dxc')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    library = c.WinDLL(str(args.casclib.resolve()), use_last_error=True)
    handle = c.c_void_p
    for name, params, result in [
        ('CascOpenStorage', [c.c_char_p, c.c_uint32, c.POINTER(handle)], c.c_bool),
        ('CascOpenFile', [handle, c.c_void_p, c.c_uint32, c.c_uint32, c.POINTER(handle)], c.c_bool),
        ('CascGetFileSize64', [handle, c.POINTER(c.c_uint64)], c.c_bool),
        ('CascGetFileInfo', [handle, c.c_uint, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)], c.c_bool),
        ('CascReadFile', [handle, c.c_void_p, c.c_uint32, c.POINTER(c.c_uint32)], c.c_bool),
        ('CascCloseFile', [handle], c.c_bool), ('CascCloseStorage', [handle], c.c_bool),
    ]:
        fn = getattr(library, name)
        fn.argtypes, fn.restype = params, result
    class Info(c.Structure):
        _fields_ = [('ckey', c.c_ubyte * 16), ('ekey', c.c_ubyte * 16), ('archive', c.c_char * 16),
                    *[(name, c.c_uint64) for name in ['storage_offset', 'segment_offset', 'tags', 'name_hash', 'size', 'encoded_size']],
                    *[(name, c.c_uint32) for name in ['segment', 'spans', 'file_id', 'locale', 'content']]]
    owner = handle()
    if not library.CascOpenStorage((str(storage) + '*wowf').encode(), 2, c.byref(owner)):
        raise c.WinError(c.get_last_error())
    records, missing = [], set()
    try:
        for line in args.listfile.open(encoding='utf-8'):
            line = line.rstrip('\r\n')
            fid, name = line.split(';', 1)
            if not re.search(r'^shaders/(pixel|vertex|compute)/dx_[56]_0/(.*shadow.*|lightbuffer.*|combiners_uber_2_2_no_mod_fog_alpha)\.bls$', name):
                continue
            file = handle()
            item = {'file_id': int(fid), 'name': name}
            records.append(item)
            if not library.CascOpenFile(owner, int(fid), 2, 0x13, c.byref(file)):
                item['open_error'] = c.get_last_error()
                continue
            try:
                info, needed = Info(), c.c_size_t()
                if library.CascGetFileInfo(file, 2, c.byref(info), c.sizeof(info), c.byref(needed)):
                    item.update(archive=info.archive.decode(), offset=info.segment_offset,
                                size=info.size, ekey=bytes(info.ekey).hex())
                size = c.c_uint64()
                if not library.CascGetFileSize64(file, c.byref(size)) or size.value > 64 * 1024 * 1024:
                    item['size_error'] = c.get_last_error()
                    continue
                data, count = c.create_string_buffer(size.value), c.c_uint32()
                if not library.CascReadFile(file, data, size.value, c.byref(count)) or count.value != size.value:
                    item['read_error'] = c.get_last_error()
                    if item.get('archive') and not (storage/'Data/data'/item['archive']).is_file():
                        missing.add(item['archive'])
                    continue
                path = output/name.replace('/', '__')
                path.write_bytes(data.raw)
                item.update(path=path.name, sha256=hashlib.sha256(data.raw).hexdigest())
                if name in args.disassemble:
                    item['programs'] = []
                    for index, code in enumerate(dxbc_programs(data.raw)):
                        binary = output / f'{path.stem}-{index}.dxbc'
                        binary.write_bytes(code)
                        assembly = binary.with_suffix('.ll')
                        with assembly.open('w', encoding='utf-8') as sink:
                            subprocess.run([str(args.dxc.resolve()), '-dumpbin', str(binary)],
                                           stdout=sink, stderr=subprocess.STDOUT, check=True, timeout=30)
                        item['programs'].append({'path': binary.name, 'assembly': assembly.name,
                                                 'sha256': hashlib.sha256(code).hexdigest()})
            finally:
                library.CascCloseFile(file)
    finally:
        library.CascCloseStorage(owner)
    report = {'storage_copy': str(storage), 'records': records, 'missing_archives': sorted(missing)}
    (output/'manifest.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'selected': len(records), 'extracted': sum('sha256' in r for r in records),
                      'missing_archives': sorted(missing)}))


if __name__ == '__main__':
    main()
