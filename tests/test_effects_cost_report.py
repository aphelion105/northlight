#!/usr/bin/env python3
# northlight-test:
"""renderer/effects_cost_report.py on a synthetic log: segment split, background samples (focus=0, or presumed from the 30 fps plateau without a focus field), per-sample state, means, ON/OFF comparison."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import contextlib, io, tempfile, unittest
import effects_cost_report as r

def frame(ms, focus=None, enabled=1, gi=1, shadows=1, fog=1):
    return (f'FRAME interval device=1 frame=120 tick=1 enabled={enabled} samples=120 meanMs={ms} p50Ms=5 p95Ms=6 maxMs=9 fps=1 '
            f'gi={gi} shadows={shadows} fog={fog}' + ('' if focus is None else f' focus={focus}'))
LOG = '\n'.join([
    'FRAME interval meanMs=99.0',   # before any segment: ignored
    'Effects ON tick=1', frame(6.0), frame(8.0), frame(33.8),
    'CPU profile frame=1 drawGate=1.000ms capture=2.000ms effects=3.000ms cleanup=0.500ms total=6.500ms enabled=1 drawHooks=-1.000ms',
    'CPU profile frame=2 drawGate=3.000ms capture=2.000ms effects=3.000ms cleanup=0.500ms total=8.500ms enabled=1 drawHooks=2.000ms',
    'GPU profile frame=1 total=5.000ms AO=1.000ms Fog=3.000ms tail=0.002ms',
    'GPU profile frame=2 total=7.000ms AO=3.000ms Fog=3.000ms tail=0.002ms',
    'CELESTIAL terrain mask map=K generation=8 draws=9 triangles=1000000 candidates=1 accepted=1 runs=1 listed=1 redraws=5,1 reuses=0,2 ms=0 peakMs=0',
    'CELESTIAL terrain mask map=K generation=8 draws=9 triangles=3000000 candidates=1 accepted=1 runs=1 listed=1 redraws=3,0 reuses=1,0 ms=0 peakMs=0',
    'CELESTIAL orbit gameDay=0.8 sun=1.70->1.73 moon=-9.86->-3.31 schedule=native weights=0.319/0.000',
    'LOCAL direct lights=55 limit=64 available=77 selectUs=1000.0 selectMaxUs=1',
    'LOCAL direct lights=40 limit=64 available=70 selectUs=2000.0 selectMaxUs=1',
    'Effects OFF tick=2', frame(4.0, enabled=0), frame(34.0, enabled=0), frame(35.0, enabled=0),
    'Effects components tick=3 master=1 GI=ON shadows=OFF fog=ON giCache=warm', frame(10.0, 1, shadows=0), frame(33.9, 0, shadows=0), frame(40.0, 1, shadows=0), frame(8.0, 0, shadows=0),
    # F10 back on with GI still off (F10 does not reset the components): a segment labelled ON, a partial state
    'Effects ON tick=4', frame(9.0, 1, gi=0), frame(12.0, 1), ''])

class Report(unittest.TestCase):
    def setUp(self):
        self.segments = r.parse(LOG.splitlines())
    def test_split(self):
        self.assertEqual([s.text for s in self.segments], ['ON', 'OFF', 'master=1 GI=ON shadows=OFF fog=ON', 'ON'])
        self.assertEqual([s.start for s in self.segments], [2, 15, 19, 24])
        self.assertEqual(self.segments[3].states, ['gi=0', 'all on'])
    def test_background_excluded(self):
        on, off, comp, _ = self.segments
        self.assertEqual((on.frames, on.background, on.presumed), ([6.0, 8.0], [33.8], 1))
        self.assertEqual((off.frames, off.background, off.presumed), ([4.0], [34.0, 35.0], 2))
        # With a focus field, focus decides: a slow focused sample is real cost, a fast unfocused one is not.
        self.assertEqual((comp.frames, comp.background, comp.presumed, comp.slow_focused), ([10.0, 40.0], [33.9, 8.0], 0, 1))
    def test_means(self):
        on = self.segments[0]
        self.assertAlmostEqual(r.mean(on.cpu['drawGate']), 2.0)
        self.assertEqual(on.cpu['drawHooks'], [2.0])   # -1 (unavailable) is skipped
        self.assertAlmostEqual(r.mean(on.gpu['AO']), 2.0); self.assertAlmostEqual(r.mean(on.gpu['total']), 6.0)
        self.assertEqual((on.redraws, on.reuses, r.mean(on.triangles)), ([8, 1], [1, 2], 2000000))
        self.assertAlmostEqual(r.mean(on.direct['selectUs']), 1500.0); self.assertEqual(on.local['lights'], 40)
    def test_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'northlight-renderer.log'; path.write_text(LOG)
            out = io.StringIO()
            with contextlib.redirect_stdout(out): self.assertEqual(r.main([str(path)]), 0)
        text = out.getvalue()
        self.assertIn('frames=2 meanMs=7.000 slowFocused=0 background=1 (presumed=1) backgroundMeanMs=33.800', text); self.assertIn('orbit: sun=1.73 moon=-3.31 weights=0.319/0.000', text)
        self.assertIn('all on  : samples=3 meanMs=8.667', text)   # (6+8+12)/3: no partial state
        self.assertIn('all off : samples=1 meanMs=4.000', text)
        self.assertIn('shadows=0: samples=2 meanMs=25.000', text); self.assertIn('gi=0    : samples=1 meanMs=9.000', text)
        self.assertIn('all on minus all off: +4.667 ms', text)
        self.assertIn('warning: no focus= field', text)

if __name__ == '__main__':
    unittest.main(verbosity=2)
