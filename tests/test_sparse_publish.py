#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Ready sparse commands must wake an idle replay before the next full batch."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,default=fp.output_dir());a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=[fp.src(n) for n in ('command_queue.h','stream_wait.h','stream_stats.h','stream_idle_profile.h')]+[Path(__file__),fp.TESTS/'sparse_publish.cpp'];inputs={str(p):sha(p) for p in files}
source=fp.src('command_queue.h').read_text(encoding='utf-8')
branch='''        else if(sleeping_.load()){
            if(idleTracking_){own(diagnosticPartialBatches_);own(diagnosticPartialCommands_,sinceCmds_);}
            publishLazy(); // A sleeping replay needs the ready prefix, not a full batch.
        }
'''
assert source.count(branch)==1
seam='unsigned wakeSignals()const{return wakeSignals_;}'
assert source.count(seam)==1
runs=[]
for name,partial in [('control',False),('production',True)]:
 folder=out/name;folder.mkdir(exist_ok=True);text=source if partial else source.replace(branch,'')
 text=text.replace(seam,seam+'\n    void testSparseSleep(){sleeping_.store(true);}\n    bool testSparseSleeping()const{return sleeping_.load();}')
 (folder/'command_queue.h').write_text(text,encoding='utf-8')
 exe=folder/'check.exe';command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wall','-Wextra','-Werror','-DNORTHLIGHT_EXPECT_PARTIAL='+str(int(partial)),'-I'+str(folder),'-I'+str(fp.src('command_queue.h').parent),fp.TESTS/'sparse_publish.cpp','-o',exe]
 with (folder/'compile.log').open('w') as f:subprocess.run(list(map(str,command)),env=fp.zig_env(),stdout=f,stderr=subprocess.STDOUT,check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120);(folder/'check.log').write_text(r.stdout+r.stderr,encoding='utf-8');assert r.returncode==0,r.stdout+r.stderr
 assert 'SPARSE_PUBLISH_PASS' in r.stdout;runs.append({'name':name,'stdout':r.stdout,'best_producer_ns':int(re.search(r'bestProducerNs=(\d+)',r.stdout)[1])});print(r.stdout,flush=True)
assert all(sha(Path(p))==h for p,h in inputs.items()),'Source changed during sparse queue checks'
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'runs':runs,'scope':'Command order, first-prefix availability and real CPU handshake; timing is descriptive, not gameplay FPS.'},indent=2)+'\n',encoding='utf-8')
