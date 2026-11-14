# northlight-test: requires=windows-exe,zig timing
"""Current rigid range admission versus its frozen full-snapshot path."""
import json,re,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir()/'rigid-range';out.mkdir(parents=True,exist_ok=True)
fixture=(fp.TESTS/'displacement_state_reuse.cpp').read_text().split('int main(')[0]
(out/'rigid_range_fixture.h').write_text(fixture,encoding='utf-8')
control=out/'control';control.mkdir(exist_ok=True)
(control/'auto_displacement_renderer.h').write_bytes(subprocess.check_output(['git','show','af4a7c1c:src/water/auto_displacement_renderer.h']))
(control/'auto_bump_renderer.h').write_bytes(fp.src('auto_bump_renderer.h').read_bytes())
executables={}
for name in ('control','current'):
 exe=out/(name+'.exe');executables[name]=exe
 command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-Wno-return-type','-I'+str(out),*(['-I'+str(control)] if name=='control' else []),*fp.test_include_flags(),str(fp.TESTS/'material_rigid_range_cpu.cpp'),'-luser32','-luuid','-o',str(exe)]
 subprocess.run(command,env=fp.zig_env(),check=True)
records=[];reference=None
for turn,name in enumerate(('control','current','current','control')):
 r=subprocess.run([str(executables[name])],capture_output=True,text=True,timeout=120)
 (out/f'{turn}-{name}.log').write_text(r.stdout+r.stderr,encoding='utf-8');r.check_returncode()
 values=re.findall(r'^RANGE_RESULT.*$',r.stdout,re.M);assert len(values)==35
 if reference is None:reference=values
 else:assert values==reference
 results=re.findall(r'RANGE_CPU_PASS medianMs=([\d.]+) rewritten=(\d)',r.stdout);assert len(results)==2
 for ms,rewritten in results:
  records.append({'name':name,'rewritten':int(rewritten),'median_ms':float(ms)});print(name,rewritten,ms,flush=True)
(out/'validation.json').write_text(json.dumps({'pass':True,'exact_cases':35,'records':records,'game_fps_measured':False},indent=2)+'\n')
