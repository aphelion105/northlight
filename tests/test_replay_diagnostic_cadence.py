#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Compile the production sampler: an inactive world's frozen frame must not flood logs."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import subprocess, tempfile

source=fp.src('world_point_rendering.inl').read_text()
start=source.index('    unsigned replayBoundsLogFrame=')
end=source.index('    // A metadata fallback',start)
sampler=source[start:end]
assert source.count('const bool diagnostics=replayBoundsDiagnosticSample();')==2
harness='''#include <cassert>
namespace NorthlightDiagnostics {bool on=true;bool enabled(){return on;}}
struct World {unsigned frames=0;
'''+sampler+'''};
int main(){
    World w;unsigned reports=0;
    for(unsigned i=0;i<100000;++i)reports+=w.replayBoundsDiagnosticSample();
    assert(reports==1); // no geometry, 100,000 game frames, one initial report
    w.frames=1;assert(!w.replayBoundsDiagnosticSample());
    w.frames=120;assert(w.replayBoundsDiagnosticSample());
    for(unsigned i=0;i<100000;++i)assert(!w.replayBoundsDiagnosticSample());
    NorthlightDiagnostics::on=false;w.frames=240;assert(!w.replayBoundsDiagnosticSample());
    NorthlightDiagnostics::on=true;assert(w.replayBoundsDiagnosticSample());
    World active;reports=0;
    for(active.frames=0;active.frames<=600;++active.frames){
        reports+=active.replayBoundsDiagnosticSample();
        assert(!active.replayBoundsDiagnosticSample()); // sync/async cannot double-log
    }
    assert(reports==6); // initial report and the same 120-rendered-frame cadence
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-replay-diag-') as tmp:
    root=Path(tmp);(root/'test.cpp').write_text(harness)
    subprocess.run(['c++','-std=c++17','-O2','-UNDEBUG',str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
print('PASS replay diagnostic cadence: frozen frames log once; active cadence and Diagnostics=0 preserved')
