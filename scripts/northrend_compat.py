"""Iterator helpers for the benchmark tools on Python 3.9 and newer."""
from functools import partial
from itertools import tee, zip_longest
import sys


if sys.version_info >= (3, 10):
    from itertools import pairwise

    strict_zip = partial(zip, strict=True)
else:
    def pairwise(iterable):
        left, right = tee(iterable)
        next(right, None)
        return zip(left, right)

    def strict_zip(*iterables):
        missing = object()
        for row in zip_longest(*iterables, fillvalue=missing):
            if any(item is missing for item in row):
                raise ValueError('zip() arguments have different lengths')
            yield row
