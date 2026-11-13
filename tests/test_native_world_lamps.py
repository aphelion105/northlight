#!/usr/bin/env python3
# northlight-test: requires=zig,stormlib,client
"""Actual native inventory -> ownership -> light selection, with no model draw."""
from pathlib import Path
import argparse
import subprocess
import sys
sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1]/'renderer')]
import northlight_paths as fp
from world_scene_builder import Assets, Builder
from native_fixture_catalog import qualify, shell_geometry, write
from native_fixture_catalog import sources
from world_fixture_emission import native_fixture_offsets

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixtures', type=Path, default=fp.output_dir()/'native-world-lamps')
args = parser.parse_args()
args.fixtures.mkdir(parents=True, exist_ok=True)
assets = Assets()
builder = Builder(assets)
proof, resolved = [], []
cases = [('dalaran-post', 'dalaran_lamppost01'), ('dalaran-wall', 'dalaran_lampwall01'),
         ('medium-brazier', 'mediumbrazier01'), ('tall-brazier', 'tallbrazier01'),
         ('medium-brazier-noomni', 'mediumbraziernoomni01'),
         ('tall-brazier-noomni', 'tallbraziernoomni01'),
         ('small-brazier-noomni', 'smallbraziernoomni01')]
try:
    for _, name in cases:
        paths = [p for p in assets.providers if p.endswith('\\'+name+'.m2')]
        assert len(paths) == 1, (name, paths)
        path = paths[0]
        blob = assets.read(path)
        record = qualify(assets, path, blob, builder)
        assert record and record[3] == 1, name
        proof.append((record, shell_geometry(builder, path, blob, record[3])))
        resolved.append((*proof[-1],native_fixture_offsets(assets,path,blob,sources(blob))))
        (args.fixtures/(name+'.m2')).write_bytes(blob)
    # The same provenance cannot be obtained by a native weapon or spell.
    for path in ('item\\objectcomponents\\weapon\\knife_1h_utgarde_d_03.m2',
                 'spells\\enchantments\\shaman_fire.m2'):
        assert qualify(assets, path, assets.read(path)) is None, path
finally:
    assets.close()
catalog = args.fixtures/'native-fixtures.bin'
write(catalog, proof)
exe = args.fixtures/'native-world-lamps.exe'
command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2', '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
           *fp.test_include_flags(), fp.TESTS/'test_native_world_lamps.cpp', '-o', exe]
subprocess.run(list(map(str, command)), check=True, env=fp.zig_env())
data = catalog.read_bytes()
for label, invalid in (('truncated', data[:-1]), ('trailing', data+b'\0'), ('version', data[:4]+bytes(4)+data[8:]),
                       ('source-mask', data[:48]+bytes(8)+data[56:])):
    path = args.fixtures/(label+'.bin')
    path.write_bytes(invalid)
    subprocess.run([str(exe), '--invalid-catalog', str(path)], check=True)
for case, name in cases:
    output = args.fixtures/case
    output.mkdir(exist_ok=True)
    subprocess.run([str(exe), str(args.fixtures/(name+'.m2')), str(catalog), '-', '-', str(output/'sources.fgl')], check=True)
resolved_catalog=args.fixtures/'native-fixtures-v4.bin'
payload=write(resolved_catalog,resolved)
assert any(profile for _,_,profile in resolved), 'Actual client must exercise emitter resolution'
for case,name in cases:
    output=args.fixtures/(case+'-resolved');output.mkdir(exist_ok=True)
    subprocess.run([str(exe),str(args.fixtures/(name+'.m2')),str(resolved_catalog),'-','-',str(output/'sources.fgl')],check=True)
import struct
profile_start=32+struct.unpack_from('<I',payload,12)[0]*48+struct.unpack_from('<I',payload,20)[0]*52
for field,value in ((0,0),(4,64),(8,0x7fc00000)):
    invalid=bytearray(payload);struct.pack_into('<I',invalid,profile_start+field,value)
    path=args.fixtures/('invalid-profile-'+str(field)+'.bin');path.write_bytes(invalid)
    subprocess.run([str(exe),'--invalid-catalog',str(path)],check=True)
# Existing v2 catalogs remain readable and keep their stricter shell contract.
legacy = args.fixtures/'native-fixtures-v2.bin'
legacy_data = bytearray(write(legacy, proof[:2]))
legacy_data[4:8] = (2).to_bytes(4, 'little')
legacy.write_bytes(legacy_data)
subprocess.run([str(exe), str(args.fixtures/'dalaran_lampwall01.m2'), str(legacy), '-', '-',
                str(args.fixtures/'legacy-sources.fgl')], check=True)
without_shells = args.fixtures/'native-fixtures-no-shells.bin'
write(without_shells, [(record, []) for record, _ in proof])
subprocess.run([str(exe), str(args.fixtures/'mediumbrazier01.m2'), str(without_shells), '-', '-',
                str(args.fixtures/'open-sources.fgl')], check=True)
for case in ('orc-street', 'orc-hanging'):
    output = args.fixtures/case
    if not (output/'source.m2').exists():
        continue
    proof_path = output/'source-proofs.fgl'
    if not proof_path.exists():
        (output/'sources.fgl').replace(proof_path)
    subprocess.run([str(exe), str(output/'source.m2'), str(catalog), str(proof_path), str(output/'sources.fga'), str(output/'sources.fgl')], check=True)
