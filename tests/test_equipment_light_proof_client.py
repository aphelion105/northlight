#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig
"""Read-only native shader audit plus altered-layout and control-flow negatives."""
import collections
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from extract_m2_shader_families import collect

HARNESS = r'''
#include "equipment_light_proof.h"
#include <cstdio>
#include <vector>
int main(int argc,char** argv){
    if(argc!=2)return 2;auto* f=std::fopen(argv[1],"rb");if(!f)return 2;
    unsigned n=0;if(std::fread(&n,4,1,f)!=1)return 2;
    while(n--){unsigned len=0;if(std::fread(&len,4,1,f)!=1||len>65536)return 2;
        std::vector<std::uint32_t> code(len);if(std::fread(code.data(),4,len,f)!=len)return 2;
        std::printf("%d\n",NorthlightEquipmentLightProof::analyze(code.data(),len));}
    std::fclose(f);
}
'''

def check(exe, path, codes):
    path.write_bytes(struct.pack('<I', len(codes)) + b''.join(struct.pack('<I', len(c)//4) + c for c in codes))
    return list(map(int, subprocess.check_output([str(exe), str(path)], text=True).split()))

def main():
    shaders = list(collect(fp.client_root())[0].values())
    with tempfile.TemporaryDirectory(prefix='northlight-native-lights-') as tmp:
        tmp = Path(tmp)
        source = tmp/'proof.cpp'
        source.write_text(HARNESS)
        exe = tmp/('proof.exe' if sys.platform == 'win32' else 'proof')
        subprocess.run([str(fp.zig()), 'c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                        *fp.test_include_flags(), str(source), '-o', str(exe)], env=fp.zig_env(), check=True)
        flags = check(exe, tmp/'code.bin', [code for _, code in shaders])
        summary = collections.Counter()
        accepted = []
        for (family, code), flag in zip(shaders, flags):
            version = struct.unpack_from('<I', code)[0]
            summary[(family, (version>>8)&255, flag)] += 1
            if flag and family == 'Model':
                accepted.append(code)
        assert accepted, 'No native shader admitted: do not ship a silently inactive feature'
        negatives = []
        for code in accepted:
            words = list(struct.unpack('<%dI' % (len(code)//4), code))
            # c21 is the first actual source position. A different layout must
            # not be mistaken for the supported native register bank.
            changed = [(w ^ 1) if ((w>>28)&7)|((w>>8)&24) == 2 and (w&2047)==21 else w for w in words]
            negatives.append(struct.pack('<%dI' % len(changed), *changed))
            changed = [(w ^ 1) if (((w>>28)&7)|((w>>8)&24)) == 2 and (w&2047)==25 else w for w in words]
            negatives.append(struct.pack('<%dI' % len(changed), *changed))
            negatives.append(code[:-4])
            negatives.append(code[:-4] + struct.pack('<II', 0x00000028, 0x0000ffff)) # IF/flow
        assert not any(check(exe, tmp/'negative.bin', negatives))
    print(json.dumps({'native_shaders': len(shaders), 'accepted': len(accepted), 'negative_cases': len(negatives),
                      'by_family_model_result': {'%s/vs%d/%d'%k:v for k,v in sorted(summary.items())}}, indent=2))

if __name__ == '__main__':
    main()
