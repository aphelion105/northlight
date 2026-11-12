#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Static-screen lifecycle regression using extracted production methods; no game/GPU."""
import hashlib,json,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def method(text,head):
    start=text.index(head);i=text.index('{',start);depth=0
    while True:
        depth+={'{':1,'}':-1}.get(text[i],0)
        if depth==0:return text[start:i+1]
        i+=1

out=fp.output_dir()/'rigid-static-coverage';out.mkdir(parents=True,exist_ok=True)
source=fp.src('world_rigid_memory.inl').read_text()
methods='\n'.join(method(source,h) for h in ['    void rigidIndexStep(){','    int rigidStaticCovered('])
fixture=(fp.TESTS/'test_rigid_static_coverage.cpp').read_text().replace('/*SCREEN_METHODS*/',methods)
cpp=out/'fixture.cpp';cpp.write_text(fixture);exe=out/'test.exe'
with (out/'compile.log').open('w') as log:
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(cpp),'-o',str(exe)],env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True)
(out/'run.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,flush=True)
assert run.returncode==0,run.returncode
# Existing portable registry suite: same source, without client-asset sections.
registry=(fp.TESTS/'test_rigid_memory.cpp').read_text().split('static Program compiled(')[0]
registry='\n'.join(line for line in registry.splitlines() if '#include "actor_client_programs.h"' not in line)
registry=registry.replace('using namespace NorthlightRigidMemory;','#undef near\n#undef far\nusing namespace NorthlightRigidMemory;')
registry+='\nint main(){settle();sticky();ranges();held();forgetting();identity();absence();states();trackMerge();caps();rebaseRoundTrip();}\n'
registry_cpp=out/'registry.cpp';registry_cpp.write_text(registry);registry_exe=out/'registry.exe'
with (out/'registry.compile.log').open('w') as log:
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(registry_cpp),'-o',str(registry_exe)],env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
run=subprocess.run([str(registry_exe)],capture_output=True,text=True)
(out/'registry.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,flush=True)
assert run.returncode==0,run.returncode
(out/'validation.json').write_text(json.dumps({'pass':True,'fixture_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest(),'game_launched':False},indent=2)+'\n')
