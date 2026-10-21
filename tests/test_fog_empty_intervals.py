"""Near-fade clipping keeps every interval with nonzero optical depth."""
import math
import random

rng = random.Random(21)
for count in (12, 20, 24, 32, 40, 48):
    for _ in range(3000):
        major = rng.choice((-1, 1)) * rng.uniform(1/math.sqrt(3), 1)
        camera = rng.uniform(-20000, 20000)
        spacing = 128/count
        step = spacing/abs(major)
        offset = (camera * (-1 if major < 0 else 1)/spacing) % 1
        near = rng.choice((0, 3.5, 8, rng.uniform(0, 256)))
        surface = rng.uniform(0, 128)
        first = max(0, math.floor(max(near, 0)/step+offset))
        before = after = 0.
        for i in range(count+1):
            a = max(0, (i-offset)*step)
            if a >= surface:
                break
            b = min((i+1-offset)*step, surface)
            t = max(0, min(1, ((a+b)*.5-near)/12))
            optical = t*t*(3-2*t)*(b-a)
            before += optical
            if i >= first:
                after += optical
            else:
                assert optical == 0, (count, near, surface, i, first)
        assert before == after
print('PASS 18000 fog interval cases: exact optical path, both ray signs and all step budgets')
