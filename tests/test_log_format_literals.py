"""Log format strings must not contain a stray '% ' (percent-space): printf reads it as the space flag of the next
conversion and consumes an argument, which crashed 0.3.165 at startup ("lamps 30% in direct sun" in the banner).
Also pins the startup banner to exactly its four conversions (%s %ls %d %lu)."""
import re, sys
from pathlib import Path
from source_units import source_text
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp  # noqa: E402

CONV = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|L|z|j|t)?([a-zA-Z%])')
bad = []
for path in sorted((fp.REPO / 'src').rglob('*')):
    if path.suffix not in ('.cpp', '.h', '.inl'):
        continue
    text = path.read_text(errors='replace')
    for m in re.finditer(r'\blogf\s*\(\s*"((?:[^"\\]|\\.)*)"', text):
        if re.search(r'%\s', m.group(1)):
            bad.append(f'{path.relative_to(fp.REPO)}: {m.group(1)[:80]}')
assert not bad, 'stray "% " in log format strings:\n' + '\n'.join(bad)
banner = re.search(r'logf\("(Northlight renderer [^"]*)"', source_text(fp.src('renderer.cpp'))).group(1)
assert [c for c in CONV.findall(banner) if c != '%'] == ['s', 's', 'd', 'u'], CONV.findall(banner)
print('PASS log format literals')
