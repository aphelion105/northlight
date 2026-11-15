# northlight-test: requires=zig timing
"""Exact sparse writes through StreamDevice, frozen/current sync counts and CPU timings."""
from pathlib import Path
import ast
import hashlib
import io
import json
import os
import re
import statistics
import subprocess
import sys
import tarfile
import types

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
out=fp.output_dir()/'sparse-buffer-ranges';out.mkdir(parents=True,exist_ok=True)
source=fp.TESTS/'test_command_stream.py';tree=ast.parse(source.read_text(encoding='utf-8'))
tree.body=[n for n in tree.body if isinstance(n,(ast.Import,ast.ImportFrom,ast.Assign,ast.FunctionDef))]
checks=types.ModuleType('command_stream_generators');checks.__file__=str(source)
exec(compile(tree,str(source),'exec'),checks.__dict__)
(out/'d3d9_stub.h').write_text('#ifndef FALSE\n#define FALSE 0\n#endif\n'+checks.stub()+'\nconstexpr unsigned D3DRS_ZENABLE=7,D3DRS_ZFUNC=23;\n',encoding='utf-8')
(out/'cs_generated.inc').write_text(checks.generated_cpp(),encoding='utf-8')
# This fake device has no WoW address space. Native shadow CVar ownership is
# checked by test_native_shadow_control, outside the buffer transport contract.
(out/'native_shadow_control_windows.h').write_text('#pragma once\nnamespace NorthlightNativeShadowControl { inline void producerPresent(){} inline void release(const void*){} }\n')
reference='43c9f510d3b46b9cfec3f43f4f86cf3cdde3a144'
baseline=out/'baseline';baseline.mkdir(exist_ok=True)
archive=subprocess.check_output(['git','archive',reference,'src'],cwd=fp.REPO)
with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
    for member in tar:
        if not member.isfile():continue
        target=(baseline/member.name).resolve();assert target.is_relative_to(baseline.resolve())
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(tar.extractfile(member).read())
report={'reference':reference,'pass':False,'scope':'CPU/queue fixture with a fake backend; not game FPS','runs':[]}
for label,root in [('before',baseline),('after',fp.REPO)]:
    includes=[str(root/Path(f).relative_to(fp.REPO)) if Path(f).is_absolute() else f for f in fp.test_include_flags()]
    exe=out/(label+'.exe')
    subprocess.run([str(fp.zig()),'c++',*(['-target','x86-windows-gnu'] if os.name=='nt' else []),'-std=c++17','-O2','-UNDEBUG','-DNL_STUB',f'-DNORTHLIGHT_EXPECT_SPARSE={int(label=="after")}','-pthread','-I',str(out),*includes,str(fp.TESTS/'sparse_buffer_ranges.cpp'),'-o',str(exe)],check=True,env=fp.zig_env())
    report[label+'_exe_sha256']=hashlib.sha256(exe.read_bytes()).hexdigest()
result=subprocess.run([str(out/'after.exe'),'proofs'],capture_output=True,text=True,timeout=180)
(out/'proofs.log').write_text(result.stdout+result.stderr);result.check_returncode();assert 'SPARSE_PROOFS_PASS' in result.stdout
result=subprocess.run([str(out/'after.exe'),'full'],capture_output=True,text=True,timeout=300)
(out/'full.log').write_text(result.stdout+result.stderr);result.check_returncode();assert 'test_command_stream: all passed' in result.stdout
for i,label in enumerate(['before','after','after','before']*3):
    result=subprocess.run([str(out/(label+'.exe'))],capture_output=True,text=True,timeout=180)
    (out/f'{i}-{label}.log').write_text(result.stdout+result.stderr);result.check_returncode()
    match=re.search(r'SPARSE_RANGES ms=([\d.]+) syncs=(\d+) writes=(\d+) bytes=(\d+)',result.stdout);assert match
    report['runs'].append(dict(label=label,ms=float(match[1]),syncs=int(match[2]),writes=int(match[3]),bytes=int(match[4])))
report['pass']=True
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
for label in ('before','after'):
    rows=[r for r in report['runs'] if r['label']==label]
    print(label,'median_ms',statistics.median(r['ms'] for r in rows),'syncs',rows[0]['syncs'],'writes',rows[0]['writes'])
