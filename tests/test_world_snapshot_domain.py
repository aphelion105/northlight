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
renderer=fp.src('renderer.cpp').read_text()
assert 'st.viewport.known&&st.rs[D3DRS_ZENABLE].known&&st.rs[D3DRS_ZFUNC].known' in stream
assert 'mainWorldDraw(st.rs[D3DRS_ZENABLE].v,st.rs[D3DRS_ZFUNC].v,st.viewport.v.MinZ,st.viewport.v.MaxZ)' in stream
assert 'takeSnapshot(policy.onDraw(tags,mainWorld))' in stream
before=renderer.split('void beforeDraw(IDirect3DVertexShader9* vs)',1)[1].split('void uiFallbackDraw',1)[0]
assert 'if(model||wmo)' in before and 'mainWorldDraw(z,zf,viewport.MinZ,viewport.MaxZ)' in before
assert before.index('mainWorldDraw(')<before.index('readProjection(')<before.index('world->wmoContext(vs)')
print('PASS producer and renderer share main-world draw admission before camera context')
