#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Real SDK capture integration with stock native shaders and Cold Blood CPU-upload bytes; no device."""
from pathlib import Path
import os
import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--view-influence-only',action='store_true',help='Only x86 O2/O0 view-influence cases and the old centroid-gate negative control')
args=parser.parse_args()
if os.name != 'nt':
    print('SKIP: native Windows SDK integration runtime required')
    raise SystemExit(0)

fp.use_source_modules()
from world_scene_builder import Assets, array, string, unpack
from extract_signatures import variants
from build_equipment_emitters import lifetime_track


def native_fixture():
    assets = Assets(archives='stock')
    try:
        data = bytearray()
        for path, index in ((r'shaders\vertex\vs_3_0\CDiffuse_T1.bls', 0),
                            (r'shaders\vertex\vs_3_0\Color_T1.bls', 1),
                            (r'shaders\vertex\vs_3_0\Diffuse_T1.bls', 0),
                            (r'shaders\pixel\ps_3_0\Combiners_Mod.bls', 0)):
            code = list(variants(assets.read(path)))[index][2]
            data.extend(struct.pack('<I', len(code) // 4))
            data.extend(code)
        blob = assets.read(r'particles\snowflake.blp')
        assert blob[:4] == b'BLP2'
        encoding, depth, alpha, _ = unpack('<4B', blob, 8)
        assert encoding == 2
        width, height = unpack('<II', blob, 12)
        offsets, sizes = unpack('<16I', blob, 20), unpack('<16I', blob, 84)
        level = next(i for i in range(16) if offsets[i] and sizes[i] and max(width >> i, height >> i) <= 32)  # the cache keeps mips up to 32 texels
        width, height = max(1, width >> level), max(1, height >> level)
        block = 8 if depth in (0, 1) else 16
        d3d = 0x31545844 if block == 8 else 0x33545844 if alpha == 1 else 0x35545844
        raw = blob[offsets[level]:offsets[level] + sizes[level]]
        data.extend(struct.pack('<5I', width, height, d3d, ((width + 3) // 4) * block, len(raw)))
        data.extend(raw)
        model = assets.read(r'spells\ice_precast_med_hand.m2')
        assert model[:4] == b'MD20' and unpack('<I', model, 4)[0] == 264
        count, offset = unpack('<II', model, 80)
        textures = [string(model, name) for _, _, _, name in array(model, offset, count, '<4I')]
        count, offset = unpack('<II', model, 296)
        stride = 492 if unpack('<I', model, 16)[0] & 0x200 else 476
        source = next(offset + i * stride for i in range(count)
                      if textures[unpack('<H', model, offset + i * stride + 22)[0]].lower() == r'particles\snowflake.blp')
        assert model[source + 40] == 4  # Native SrcAlpha / One.
        colors = lifetime_track(model, source + 260, '<3f')
        alpha = lifetime_track(model, source + 276, '<h')
        assert colors[0][0] == alpha[0][0] == 0
        data.extend(struct.pack('<4f', *(v / 255 for v in colors[0][1]), alpha[0][1][0] / 32767))
        return data
    finally:
        assets.close()


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
source=Path(__file__).with_suffix('.cpp')
watched=[p for p in fp.SRC.rglob('*') if p.is_file()]+[source,Path(__file__),fp.REPO/'northlight_paths.py']
inputs={str(p.resolve()):sha(p) for p in watched}
root=fp.output_dir()/('particle-view-influence' if args.view_influence_only else 'particle-probe-reuse');root.mkdir(parents=True,exist_ok=True)
header=fp.src('particle_light_capture.h').read_text()
control=header.replace('bool placed=false;unsigned placedSample=samples;Point probe[3];','bool placed=false;')
saved='''            // No draw or upload occurs inside prepare(). Keep this exact probe's
            // already evaluated vertices for its subsequent sampling iteration.
            std::copy(points,points+3,probe);placedSample=sample;placed=true;'''
sampled='''            Point points[3];
            if(sample==placedSample)std::copy(probe,probe+3,points);
            else if(!load(references[0],points[0])||!load(references[1],points[1])||!load(references[2],points[2]))continue;'''
assert control.count(saved)==1 and control.count(sampled)==1
control=control.replace(saved,'            placed=true;').replace(sampled,'            Point points[3];if(!load(references[0],points[0])||!load(references[1],points[1])||!load(references[2],points[2]))continue;')
control_dir=root/'control';control_dir.mkdir(exist_ok=True);(control_dir/'particle_light_capture.h').write_bytes(control.encode())
records=[]
def parse(text,prefix='PARTICLE_PROBE_CASE '):return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(prefix)]
def command(architecture,optimization,folder,binary):
    return [str(fp.zig()),'c++','-target',architecture+'-windows-gnu',optimization,'-UNDEBUG','-std=c++17','-Wall','-Wextra','-Werror','-Wno-microsoft-exception-spec','-I',str(folder),*fp.test_include_flags(),str(source),'-o',str(binary)]

fixture = root / 'native.bin'
fixture.write_bytes(native_fixture())
if args.view_influence_only:
    for optimization in ('-O2','-O0'):
        binary=root/f'fixture-influence-x86{optimization}.exe';cmd=command('x86',optimization,root,binary)
        subprocess.run(cmd,env=fp.zig_env(),check=True);result=subprocess.run([str(binary),str(fixture),'--view-influence-only'],capture_output=True,text=True)
        (root/f'current-x86{optimization}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0,result.stdout+result.stderr
        rows=parse(result.stdout,'PARTICLE_INFLUENCE_CASE ');assert len(rows)==13
        records.append({'architecture':'x86','optimization':optimization,'name':'view-influence','command':cmd,'exe_sha256':sha(binary),'rows':rows})
    negative=root/'centroid-negative';negative.mkdir(exist_ok=True)
    gate='            bool finiteClip=true;for(float v:clip)finiteClip&=std::isfinite(v);if(!finiteClip)continue;'
    old='            if(!std::isfinite(clip[3])||clip[3]<=0||std::fabs(clip[0])>clip[3]*1.2f||std::fabs(clip[1])>clip[3]*1.2f)continue;'
    assert header.count(gate)==1
    (negative/'particle_light_capture.h').write_bytes(header.replace(gate,old).encode());binary=negative/'fixture-old-centroid.exe'
    cmd=command('x86','-O2',negative,binary);subprocess.run(cmd,env=fp.zig_env(),check=True)
    result=subprocess.run([str(binary),str(fixture),'--view-influence-only'],capture_output=True,text=True)
    (negative/'result.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode!=0 and 'PARTICLE_INFLUENCE_ROTATION_MISMATCH' in result.stderr,result.stdout+result.stderr
else:
    for architecture in ('x86', 'x86_64'):
        for optimization in ('-O2', '-O0'):
            cells={}
            for name,folder in [('control',control_dir),('current',root)]:
                binary=root/f'fixture-{name}-{architecture}{optimization}.exe';cmd=command(architecture,optimization,folder,binary)
                subprocess.run(cmd,env=fp.zig_env(),check=True);result=subprocess.run([str(binary),str(fixture)],capture_output=True,text=True)
                (root/f'{name}-{architecture}{optimization}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
                assert result.returncode==0,result.stdout+result.stderr
                cells[name]=parse(result.stdout);assert len(cells[name])==17
                influence=parse(result.stdout,'PARTICLE_INFLUENCE_CASE ');assert len(influence)==13
                records.append({'architecture':architecture,'optimization':optimization,'name':name,'command':cmd,'exe_sha256':sha(binary),'rows':cells[name],'view_influence_rows':influence})
            for a,b in zip(cells['control'],cells['current']):
                for field in ['scenario','up','prepared','selected','hash','triangles','draws','probes','distant','bufferMiss','uploadMisses']:assert a[field]==b[field],(field,a,b)
                saved_reads=0 if b['up']=='1' or b['scenario'] in ['5','6'] else 6
                assert int(a['uploadHits'])-int(b['uploadHits'])==saved_reads,(a,b)
    negative=root/'negative';negative.mkdir(exist_ok=True)
    unsafe=header.replace('if(sample==placedSample)','if(sample==0&&placedSample<samples)');assert unsafe!=header
    (negative/'particle_light_capture.h').write_bytes(unsafe.encode());binary=negative/'fixture-unsafe.exe'
    cmd=command('x86','-O2',negative,binary);subprocess.run(cmd,env=fp.zig_env(),check=True)
    result=subprocess.run([str(binary),str(fixture)],capture_output=True,text=True);(negative/'result.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode!=0 and 'PARTICLE_PROBE_TRIANGLE_MISMATCH' in result.stderr,result.stdout+result.stderr
assert all(sha(Path(p))==h for p,h in inputs.items()),'Source changed during final validation'
(root/'validation.json').write_text(json.dumps({'inputs':inputs,'inputs_unchanged':True,'fixture_sha256':sha(fixture),'records':records,'view_influence_only':args.view_influence_only,'old_centroid_gate_rejected':args.view_influence_only,'unsafe_probe_reuse_rejected':not args.view_influence_only,'authored_geometry_and_receivers':True,'matched_game_item':False,'timings_claimed':False,'gpu_or_game_launched':False},indent=2)+'\n')
print('PASS actual SDK particle influence:13 exact source/receiver, camera, cluster, track, world-transform, finite-input and bounded-work cases per variant; old centroid negative rejected; no game/GPU/FPS claim.' if args.view_influence_only else 'PASS actual SDK particle capture,17 full-output/count cases plus13 influence cases per variant, indexed/UP and late failed probes, six fewer uploaded input reads per reused indexed probe; no timing benchmark.')
