#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib
"""Offline executable signatures, original shader proof and native CPU tests."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
from extract_wmo_context import lit_wmo,fog_wmo
from extract_signatures import variants
from mpq import Archive

def main():
    data=(fp.client_root()/'Wow.exe').read_bytes();pe=struct.unpack_from('<I',data,60)[0]
    count=struct.unpack_from('<H',data,pe+6)[0];opt=struct.unpack_from('<H',data,pe+20)[0]
    base=struct.unpack_from('<I',data,pe+52)[0];assert base==0x400000
    sections=[struct.unpack_from('<8sIIII',data,pe+24+opt+i*40) for i in range(count)]
    def at(address,size):
        for _,_,rva,raw,offset in sections:
            delta=address-base-rva
            if 0<=delta and delta+size<=raw:return data[offset+delta:offset+delta+size]
        raise AssertionError(hex(address))
    header=fp.src('wmo_context.h').read_text()
    arrays={name:bytes(int(x,16) for x in re.findall(r'0x[0-9a-f]+',values)) for name,values in re.findall(r'unsigned char (\w+)\[\]=\{([^}]+)\}',header)}
    proof=[]
    for address,name in re.findall(r'\{(0x[0-9a-f]+),(\w+),sizeof \w+\}',header):
        expected=arrays[name];assert at(int(address,16),len(expected))==expected,(address,name)
        proof.append({'address':address,'name':name,'bytes':expected.hex()})
    assert len(proof)==27  # 8 lighting proofs + 4 light-slot proofs (lightSlotProofs) + 15 prediction proofs (predictionProofs)
    assert abs(struct.unpack('<f',at(0xa45564,4))[0]-1/255)<1e-9
    weather=fp.src('weather_memory.h').read_text()  # the game's weather state read from memory, 5 more proofs (GetType, s_weather use, intensity store, two constants)
    weather_arrays={name:bytes(int(x,16) for x in re.findall(r'0x[0-9a-f]+',values)) for name,values in re.findall(r'unsigned char (\w+)\[\]=\{([^}]+)\}',weather)}
    weather_proofs=re.findall(r'\{(0x[0-9a-f]+),(\w+),sizeof \w+\}',weather)
    assert len(weather_proofs)==5
    for address,name in weather_proofs:assert at(int(address,16),len(weather_arrays[name]))==weather_arrays[name],(address,name)
    with Archive(fp.client_root()/'Data/patch.mpq') as archive:
        source=dict((i,b) for i,_,b in variants(archive.read('shaders\\vertex\\vs_3_0\\MapObjDiffuse_T1.bls')))
    assert lit_wmo(source[1]) and not lit_wmo(source[0])
    assert fog_wmo(source[1]) and fog_wmo(source[0])
    words=list(struct.unpack('<%dI'%(len(source[1])//4),source[1]))
    # Mutate source direction sign and ambient/direct mapping independently.
    for old,new in [(0xa1e4000c,0xa0e4000c),(0xa0e4000b,0xa0e4000a),(0xa0e4000a,0xa0e4000b)]:
        assert old in words
        changed=[new if w==old else w for w in words]
        assert not lit_wmo(struct.pack('<%dI'%len(changed),*changed))
    for old,new in [(0xa000001e,0xa055001e),(0xa0aa001e,0xa055001e)]:
        assert old in words
        changed=[new if w==old else w for w in words]
        assert not fog_wmo(struct.pack('<%dI'%len(changed),*changed))
    with tempfile.TemporaryDirectory(prefix='northlight-wmo-context-') as tmp:
        binary=str(Path(tmp)/'test')
        subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',*fp.test_include_flags(),str(HERE/'test_wmo_context.cpp'),'-o',binary],check=True)
        output=subprocess.check_output([binary],text=True)
    files=['wmo_context.h','wmo_shader_signatures.h','extract_wmo_context.py','test_wmo_context.cpp','validate_wmo_context.py']
    report={'result':'passed','game_launched':False,'graphics_device_created':False,'exe_sha256':hashlib.sha256(data).hexdigest(),
            'signatures':proof,'shader_mutations_rejected':5,'sanitizers':['address','undefined'],'cpu_tests':output.splitlines(),
            'source_sha256':{f:hashlib.sha256(fp.tracked(f).read_bytes()).hexdigest() for f in files},
            'limitations':['Independent sky lighting is outdoor environment, not prelit WMO vertex colors or indoor group overrides.',
                           'Only405 SM3 lit variants have optional local-constant proof; all1967 WMO identities can use global lighting.',
                           'Requires current independent camera/projection validation and user runtime testing.']}
    (fp.output_dir()/'wmo-context-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(output,end='')
if __name__=='__main__':main()
