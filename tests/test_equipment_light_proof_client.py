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
#include <cstdlib>
#include <vector>
#include <unordered_set>
struct IDirect3DVertexShader9 {};
using DWORD=std::uint32_t;
namespace NorthlightDiagnostics {inline bool enabled(){return false;}}
static void logf(const char*,...){}
struct Registration {
    bool on=false;
    std::unordered_map<IDirect3DVertexShader9*,unsigned> equipmentShaders,nativePointShaders;
    std::unordered_map<IDirect3DVertexShader9*,bool> nativePointOwners;
    std::unordered_set<IDirect3DVertexShader9*> terrainShaders;
    std::unordered_set<IDirect3DVertexShader9*> nativeTerrainPointShaders;
    bool equipmentLightsOn()const{return on;}
    void registerEquipmentShader(IDirect3DVertexShader9* shader,const DWORD* code,size_t count){registerNativePointShader(shader,code,count,true);}
    // PRODUCTION_REGISTRATION
};
static void registration(unsigned lights,const std::vector<std::uint32_t>& code){
    if(!lights)return;Registration r;IDirect3DVertexShader9 shader;
    const auto require=[](bool value){if(!value){std::fputs("EQUIPMENT_LIVE_REGISTRATION_MISMATCH\n",stderr);std::abort();}};
    r.registerEquipmentShader(&shader,code.data(),code.size());
    require(!r.on&&r.equipmentShaders.size()==1&&r.equipmentShaders.at(&shader)==lights);
    r.on=true;require(r.equipmentShaders.at(&shader)==lights); // no shader recreation
    r.on=false;r.registerEquipmentShader(&shader,nullptr,0);require(r.equipmentShaders.empty());
    r.registerEquipmentShader(&shader,code.data(),code.size());
    const DWORD unsupported[]={0xfffe0200u,0xffffu};r.registerEquipmentShader(&shader,unsupported,2);require(r.equipmentShaders.empty());
    r.registerEquipmentShader(nullptr,code.data(),code.size());require(r.equipmentShaders.empty());
    r.on=true;r.registerEquipmentShader(&shader,code.data(),code.size());require(r.equipmentShaders.size()==1&&r.equipmentShaders.at(&shader)==lights);
}
int main(int argc,char** argv){
    if(argc!=2&&argc!=3)return 2;const bool terrain=argc==3;auto* f=std::fopen(argv[1],"rb");if(!f)return 2;
    unsigned n=0;if(std::fread(&n,4,1,f)!=1)return 2;
    while(n--){unsigned len=0;if(std::fread(&len,4,1,f)!=1||len>65536)return 2;
        std::vector<std::uint32_t> code(len);if(std::fread(code.data(),4,len,f)!=len)return 2;
        bool rgbOnly=false;const unsigned lights=NorthlightEquipmentLightProof::analyze(code.data(),len,&rgbOnly,terrain);if(!terrain)registration(lights,code);std::printf("%u\n",lights|(terrain&&rgbOnly?16u:0u));}
    std::fclose(f);
}
'''

def check(exe, path, codes, terrain=False):
    path.write_bytes(struct.pack('<I', len(codes)) + b''.join(struct.pack('<I', len(c)//4) + c for c in codes))
    return list(map(int, subprocess.check_output([str(exe), str(path), *(['terrain'] if terrain else [])], text=True).split()))

def main():
    shaders = list(collect(fp.client_root())[0].values())
    world=fp.src('world_renderer.h').read_text()
    begin=world.index('    void registerNativePointShader(');end=world.index('    void captureEquipmentLights(',begin)
    method=world[begin:end];assert 'equipmentShaders.erase(shader)' in method
    harness=HARNESS.replace('    // PRODUCTION_REGISTRATION',method)
    with tempfile.TemporaryDirectory(prefix='northlight-native-lights-') as tmp:
        tmp = Path(tmp)
        source = tmp/'proof.cpp'
        source.write_text(harness)
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
        from world_scene_builder import Assets
        from extract_signatures import variants
        assets=Assets(archives='stock')
        try: terrain_by_model={model:[code for _,_,code in variants(assets.read('shaders/vertex/'+model+'/Terrain.bls'))] for model in ('vs_1_1','vs_2_0','vs_3_0')}
        finally: assets.close()
        terrain=[code for codes in terrain_by_model.values() for code in codes]
        terrain_flags=check(exe,tmp/'terrain.bin',terrain,True)
        assert set(terrain_flags)=={0,19},collections.Counter(terrain_flags)
        assert terrain_flags==([0]*64+[19]*64)*3
        altered=[]
        for code,flag in zip(terrain,terrain_flags):
            if flag!=19:continue
            # c29 RGB must not feed clip position or colour alpha.
            major=(struct.unpack_from('<I',code)[0]>>8)&255
            for kind,target,mask in ((6 if major==3 else 4,0,1),(6 if major==3 else 5,1 if major==3 else 0,8)):
                altered.append(code[:-4]+struct.pack('<3I',0x00000001 if major==1 else 0x02000001,0x80000000|(kind<<28)|(mask<<16)|target,0xa000001d)+code[-4:])
        assert set(check(exe,tmp/'terrain-unsafe.bin',altered,True))=={3}
        # A production-method counterfactual reproduces the original live-enable bug.
        unsafe=method.replace('        const unsigned lights=','        if(!equipmentLightsOn())return;\n        const unsigned lights=',1)
        assert unsafe!=method;source.write_text(HARNESS.replace('    // PRODUCTION_REGISTRATION',unsafe))
        bad=tmp/('registration-unsafe.exe' if sys.platform=='win32' else 'registration-unsafe')
        subprocess.run([str(fp.zig()),'c++','-std=c++17','-O1','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(source),'-o',str(bad)],env=fp.zig_env(),check=True)
        positive=tmp/'registration.bin';positive.write_bytes(struct.pack('<II',1,len(accepted[0])//4)+accepted[0])
        result=subprocess.run([str(bad),str(positive)],capture_output=True,text=True)
        assert result.returncode!=0 and 'EQUIPMENT_LIVE_REGISTRATION_MISMATCH' in result.stderr,result.stdout+result.stderr
    print(json.dumps({'native_shaders': len(shaders), 'accepted': len(accepted), 'negative_cases': len(negatives),
                      'terrain_variants':len(terrain),'terrain_rgb_owners':terrain_flags.count(19),'terrain_unsafe_position_alpha_cases':len(altered),
                      'production_registration':True,'off_then_live_enable':True,'pointer_reuse_unsupported_erased':True,'bytecode_or_com_retained':False,'unsafe_off_registration_rejected':True,
                      'by_family_model_result': {'%s/vs%d/%d'%k:v for k,v in sorted(summary.items())}}, indent=2))

if __name__ == '__main__':
    main()
