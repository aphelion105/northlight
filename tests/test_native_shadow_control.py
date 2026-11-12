#!/usr/bin/env python3
# northlight-test: requires=zig,client
"""Verified native producer quality gates and Northlight provider transactions."""
from pathlib import Path
import hashlib,json,os,re,struct,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def main():
    out=fp.output_dir()/'native-shadow-control';out.mkdir(parents=True,exist_ok=True)
    root=fp.REPO;adapter=fp.src('native_shadow_control_windows.h');code=fp.client_root()/'Wow.exe';data=code.read_bytes()
    pe=struct.unpack_from('<I',data,60)[0];count=struct.unpack_from('<H',data,pe+6)[0];opt=pe+24;assert struct.unpack_from('<I',data,opt+28)[0]==0x400000
    sections=[struct.unpack_from('<4I',data,opt+struct.unpack_from('<H',data,pe+20)[0]+i*40+8) for i in range(count)]
    def at(address,size):
        for _,va,raw,offset in sections:
            start=address-0x400000-va
            if 0<=start and start+size<=raw:return data[offset+start:offset+start+size]
        raise AssertionError(hex(address))
    proofs=re.findall(r'\{(0x[0-9a-f]+),(\d+),"([0-9a-f]{64})"\}',adapter.read_text());assert len(proofs)==4
    for address,size,digest in proofs:assert hashlib.sha256(at(int(address,16),int(size))).hexdigest()==digest
    builder=out/'producer.bin';builder.write_bytes(at(0x875f80,983))
    inputs=[Path(__file__),fp.TESTS/'test_native_shadow_control.cpp',fp.src('native_shadow_control.h'),adapter,fp.src('frame_requirements.h'),fp.src('quality_settings.h')]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();before={str(p.resolve()):sha(p) for p in inputs};runs=[]
    variants=[('O2',['-O2']),('O0',['-O0','-g'])] if os.name=='nt' else [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]
    for name,flags in variants:
        exe=out/(name+('.exe' if os.name=='nt' else ''));compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if os.name=='nt' else ['clang++']
        command=[*compiler,'-std=c++17','-UNDEBUG','-Wall','-Wextra',*flags,*fp.test_include_flags(),str(fp.TESTS/'test_native_shadow_control.cpp'),'-o',str(exe)]
        if os.name=='nt':command+=['-Wl,--image-base,0x10000000']
        subprocess.run(command,env=fp.zig_env() if os.name=='nt' else None,check=True)
        result=subprocess.run([str(exe),str(builder)],capture_output=True,text=True)
        (out/(name+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:print(result.stdout+result.stderr,file=sys.stderr)
        result.check_returncode();assert 'NATIVE_SHADOW_CONTROL_PASS' in result.stdout
        print(name,result.stdout.strip(),flush=True);runs.append(dict(mode=name,sha256=sha(exe),pass_=True,stdout=result.stdout))
    assert all(sha(Path(p))==h for p,h in before.items())
    (out/'validation.json').write_text(json.dumps(dict(pass_=True,inputs=before,client_sha256=sha(code),code_proofs=proofs,runs=runs,game_launched=False,client_modified=False),indent=2)+'\n')

if __name__=='__main__':main()
