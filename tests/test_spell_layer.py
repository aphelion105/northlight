#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Task 34 spell layer: native behaviour tests of the production spell_layer.h (clang++, plain and ASan/UBSan) against a mock D3D9 device
(test_spell_layer_api.h) that models state blocks, render target/viewport rules, UP draw side effects, buffer lock versions and records the
exact bytes every draw fetches. Classification table, frame state machine, guard table, deferral equivalence (state and bytes identical to the
in-place run), copy-on-conflict, every hazard, caps, reset and teardown. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='northlight-spell-layer-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_spell_layer.cpp'),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=300)
        print(label,run.stdout,run.stderr,end='',flush=True);run.check_returncode()
        assert 'tests passed' in run.stdout
# SpellFog review: the vertex shader hash lookup of a defer happens only when a SPELLFOG sample can still be logged (diagnostics on, sample room, SpellFog on)
r=fp.src('renderer.cpp').read_text()
i=r.index('spellArgs.vsHash=0;')
seg=r[i:i+400]
assert seg.index('diagnostics()&&spell.fogSampleRoom()')<seg.index('vsHashes.find(vs)'), seg
print('renderer: vsHashes.find only for a logged SPELLFOG sample')
