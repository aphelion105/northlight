#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Main-world snapshot admission and replay after coordinate-compatible sky draws."""
import subprocess
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir()
stream=fp.src('stream_device.h').read_text()
on_draw=stream.split('    void onDraw(unsigned){',1)[1].split('    void takeSnapshot(',1)[0]
(out/'world_snapshot_on_draw.inc').write_text('    void onDraw(unsigned){'+on_draw)
for name,opts in [('test_world_snapshot_domain',('-O0','-O2')),('test_world_snapshot_producer',('-O0','-O2')),('test_game_snapshot',('-O2',))]:
    for opt in opts:
        exe=out/(name+'.exe')
        subprocess.run([str(fp.zig()),'c++','-std=c++17',opt,'-UNDEBUG','-Wall','-Wextra','-Werror',
                        '-I',str(out),*fp.test_include_flags(),str(fp.TESTS/(name+'.cpp')),'-o',str(exe)],env=fp.zig_env(),check=True)
        subprocess.run([str(exe)],check=True)
# Regression control: treating the PE section envelope as code loses the
# frozen native-shadow state on clients with an appended readonly code section.
private=out/'snapshot-envelope-control';private.mkdir(exist_ok=True)
store=fp.src('snapshot_store.h').read_bytes()
old=store.replace(b'if(!count)return true;',b'if(true)return true;')
assert old!=store
(private/'snapshot_store.h').write_bytes(old)
(private/'game_snapshot.h').write_bytes(fp.src('game_snapshot.h').read_bytes())
exe=private/'control.exe'
subprocess.run([str(fp.zig()),'c++','-std=c++17','-O2','-UNDEBUG','-I',str(private),
                *fp.test_include_flags(),str(fp.TESTS/'test_game_snapshot.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True)
(private/'run.log').write_text(result.stdout+result.stderr)
assert result.returncode and 'SNAPSHOT_WRITABLE_GLOBAL_MISMATCH' in result.stderr
print('PASS disjoint PE sections preserve shadow state; old envelope rejected')
renderer=fp.src('renderer.cpp').read_text()
assert 'st.viewport.known&&st.rs[D3DRS_ZENABLE].known&&st.rs[D3DRS_ZFUNC].known' in stream
assert 'mainWorldDraw(st.rs[D3DRS_ZENABLE].v,st.rs[D3DRS_ZFUNC].v,st.viewport.v.MinZ,st.viewport.v.MaxZ)' in stream
assert 'takeSnapshot(policy.onDraw(tags,mainWorld,mainMenu))' in stream
before=renderer.split('void beforeDraw(IDirect3DVertexShader9* vs)',1)[1].split('void uiFallbackDraw',1)[0]
assert 'if(model||wmo)' in before and 'mainWorldDraw(z,zf,viewport.MinZ,viewport.MaxZ)' in before
assert before.index('mainWorldDraw(')<before.index('readProjection(')<before.index('world->wmoContext(vs)')
print('PASS producer and renderer share main-world draw admission before camera context')
