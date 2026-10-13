#!/usr/bin/env python3
# northlight-test:
"""scripts/affected_tests.py maps changed files to the tests that cover them (source only, no game,
compiler or GPU). Real repository cases: a header only some tests build selects those and not an
unrelated test; an #include chain is followed; a test selects itself; a test that scans every
source (fp.sources) is selected by any production header; docs select nothing."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
sys.path.insert(0, str(fp.SCRIPTS))
import affected_tests

graph = affected_tests.Graph()


def covering(name):
    return {t for t, _ in graph.covering(fp.src(name).relative_to(fp.REPO).as_posix())}


spell = covering('spell_layer.h')
lock = covering('upload_lock.h')   # included by replay_copy_reader.h and draw_snapshot.h
checks = {
    'a header names its own test': 'test_spell_layer.py' in spell,
    'an unrelated test is not selected': 'test_ao_blur.py' not in spell and 'test_spell_layer.py' not in lock,
    # test_tracked_buffers.cpp includes replay_copy_reader.h, which includes upload_lock.h; the test names neither
    'a header included by a built header selects the test': 'test_tracked_buffers.py' in lock,
    'a header included through test-side native files selects the test': 'test_draw_snapshot.py' in lock,
    'a changed test selects itself': ('test_upload_locks.py', 'is the changed test') in graph.covering('tests/test_upload_locks.py'),
    'a source scanner is selected by any production header': 'test_upload_locks.py' in spell and 'test_upload_locks.py' in covering('world_gi.cpp'),
    'a shader selects the tests that read it': {'test_ao_blur.py', 'test_rain_mask.py'} <= covering('effects.hlsl'),
    'a shader does not select a header-only test': 'test_spell_layer.py' not in covering('effects.hlsl'),
    'a file no test names selects nothing (outside the source scanners)': graph.covering('docs/unmapped' + '.xyz') == [],
    'the reason chain ends at the test': all(why.endswith(t) for t, why in graph.covering('src/proxy/upload_lock.h') if 'scans' not in why and 'is the changed' not in why),
}
for name, ok in checks.items():
    print(('ok   ' if ok else 'FAIL ') + name)
sys.exit(0 if all(checks.values()) else 1)
