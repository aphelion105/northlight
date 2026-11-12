#!/usr/bin/env python3
"""Run the renderer test suite in parallel at low CPU priority.

    python3 scripts/run_tests.py                     # every tests/test_*.py
    python3 scripts/run_tests.py test_replay_*       # by name pattern (validate_*/verify_* only when named)
    python3 scripts/run_tests.py --require client    # a missing client FAILs instead of SKIPping
    python3 scripts/run_tests.py --record 0.3.158    # reports go to renderer/records/validation-0.3.158

Each test declares what it needs on one line near the top:
    # northlight-test: requires=client,cxx timing slow
A test whose requirement is not available is reported SKIP with the reason; see tests/README.md.
`known-fail=<reason>` (last on the line) marks a test that fails at HEAD for a known reason, listed
in tests/KNOWN_FAILURES.md: it still runs, a failure is reported XFAIL and does not fail the run,
a pass is reported XPASS (remove the tag).
Named suites live in tests/suites.json. --strict requires PASS for every selection;
--list never probes tools. Every run records input hashes and rejects source drift.
Tests run under `nice -n 15`, 4 at a time, slow ones first; GPU/`timing` tests run alone
at the end. Reports go to out/test-output/<unique-run>/<test>/ unless --record is given.
"""
import argparse
import concurrent.futures as cf
import fnmatch
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp  # noqa: E402
import check_layout  # noqa: E402  (scripts/ is this script's directory)

TAG_LINE = re.compile(r'^#\s*northlight-test:(.*)$', re.M)
known_fail = {}   # test name -> reason, from 'known-fail=<reason>'
DISCOVERY = fp.TESTS / 'discovery.txt'
SUITES = fp.TESTS / 'suites.json'


def suites():
    data = json.loads(SUITES.read_text(encoding='utf-8'))
    for name, suite in data.items():
        tests = suite['tests']
        if not tests or len(tests) != len(set(tests)) or any(
                Path(t).name != t or not t.startswith('test_') or not t.endswith('.py') for t in tests):
            raise ValueError(f'{SUITES.name}: invalid or repeated test in {name}')
    return data


def input_snapshot():
    """Versioned and new non-ignored inputs, including the test/harness itself."""
    inputs = {}
    for name in sorted(set(check_layout.git_files())):
        path = fp.REPO / name
        try:
            inputs[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        except FileNotFoundError:
            inputs[name] = None  # A tracked deletion is part of the working tree too.
    return inputs


def input_identity(inputs):
    return hashlib.sha256(json.dumps(inputs, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def git_state():
    def read(*args):
        return subprocess.check_output(['git', *args], cwd=fp.REPO).decode('utf-8', 'replace').strip()
    return dict(commit=read('rev-parse', 'HEAD'), branch=read('branch', '--show-current'),
                status=read('status', '--porcelain=v1').splitlines())


def verify_report(path):
    report = json.loads(path.read_text(encoding='utf-8'))
    current = input_identity(input_snapshot())
    valid = (report['status'] == 'PASS' and report['all_passed'] and report['tests']
             and all(test['status'] == 'PASS' for test in report['tests'].values())
             and report['input_identity'] == report['input_identity_after'] == current
             and report['git']['commit'] == git_state()['commit'])
    print(f'{"PASS" if valid else "REJECTED"}: {path}')
    return 0 if valid else 1


def probe(name):
    """None if the requirement is available, else the reason it is not."""
    def found(fn):
        try:
            fn()
            return None
        except fp.Missing as e:
            return str(e)
    if name == 'cxx':
        missing = [c for c in ('clang++', 'c++') if not shutil.which(c)]
        return f'host C++ compiler not on PATH: {", ".join(missing)}' if missing else None
    if name == 'libcxx':
        if sys.platform == 'darwin':
            return None
        if not shutil.which('clang++'):
            return 'clang++ with LLVM libc++ is not on PATH'
        output = fp.out() / 'requirements'
        output.mkdir(parents=True, exist_ok=True)
        result = subprocess.run(['clang++', '-stdlib=libc++', '-x', 'c++', '-', '-o', str(output / 'libcxx-probe')],
                                input='#include <vector>\n#ifndef _LIBCPP_VERSION\n#error LLVM libc++ required\n#endif\nint main(){std::vector<int> v;return v.size();}\n',
                                capture_output=True, text=True)
        return None if result.returncode == 0 else 'LLVM libc++ headers/runtime missing (install libc++-dev and libc++abi-dev)'
    if name == 'lua51':
        env = os.environ.copy()
        if env.get('NORTHLIGHT_LUA_PYTHONPATH'):
            env['PYTHONPATH'] = env['NORTHLIGHT_LUA_PYTHONPATH'] + os.pathsep + env.get('PYTHONPATH', '')
        r = subprocess.run([sys.executable, '-c', 'from lupa.lua51 import LuaRuntime; assert LuaRuntime().eval("_VERSION") == "Lua 5.1"'], env=env, capture_output=True)
        return None if r.returncode == 0 else 'Lua 5.1 test runtime missing (lupa.lua51)'
    if name == 'windows-exe':
        return None if os.name == 'nt' else 'The host cannot execute Windows test programs'
    if name in ('lldb', 'objdump'):
        return None if shutil.which(name) else f'{name} is not on PATH'
    if name == 'windows-gpu':
        if os.name != 'nt':
            return 'The offscreen D3D9 oracle requires Windows'
        try:
            zig = fp.zig()
        except fp.Missing as e:
            return str(e)
        source = fp.TESTS / 'windows_gpu_probe.cpp'
        output = fp.out() / 'requirements'
        output.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha256(source.read_bytes() + str(zig).encode()).hexdigest()[:16]
        executable = output / f'windows-gpu-{digest}.exe'
        if not executable.is_file():
            subprocess.run([str(zig), 'c++', '-target', 'x86-windows-gnu', '-O2', '-std=c++17',
                            str(source), '-luser32', '-o', str(executable)], env=fp.zig_env(), check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        return None if result.returncode == 0 else result.stdout.strip() or 'System D3D9 hidden device unavailable'
    if name == 'base':
        base = os.environ.get('BASE')
        return None if base and (Path(base) / 'renderer.cpp').is_file() else \
            'BASE is not set to a pristine older renderer tree (the release gate compares against it)'
    probes = {'client': fp.client_root, 'zig': fp.zig, 'wine': fp.wine_root, 'stormlib': fp.stormlib,
              'archive': fp.archive, 'backups': fp.backups, 'dll': fp.dll,
              'stormlib-src': fp.stormlib_source, 'world-cache': fp.world_cache}
    if name not in probes:
        return f'unknown requirement {name!r}'
    return found(probes[name])


def tags(path):
    """(requires, flags) from the '# northlight-test:' line in the first 30 lines."""
    head = ''.join(path.open(encoding='utf-8', errors='replace').readlines()[:30])
    m = TAG_LINE.search(head)
    requires, flags = [], set()
    line = m.group(1) if m else ''
    if 'known-fail=' in line:   # the reason runs to the end of the line
        line, reason = line.split('known-fail=', 1)
        flags.add('known-fail')
        known_fail[path.name] = reason.strip()
    for word in line.split('(')[0].split():   # '(...)' is a comment
        if word.startswith('requires='):
            requires += [r for r in word[len('requires='):].split(',') if r]
        else:
            flags.add(word)
    return requires, flags


def discover(patterns):
    everything = sorted(p for g in ('test_*.py', 'validate_*.py', 'verify_*.py') for p in fp.TESTS.glob(g))
    if not patterns:
        return [p for p in everything if p.name.startswith('test_')]
    names = [n if n.endswith('.py') or '*' in n else n + '.py' for n in patterns]
    return [p for p in everything if any(fnmatch.fnmatch(p.name, n) or fnmatch.fnmatch(p.stem, n) for n in names)]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('patterns', nargs='*', help='test names or fnmatch patterns (default: tests/test_*.py)')
    ap.add_argument('-j', '--jobs', type=int, help='parallel tests (default 4; architecture/GPU suites 1)')
    ap.add_argument('--suite', help='named test suite from tests/suites.json')
    ap.add_argument('--list', action='store_true', help='list the selection without probing tools or running tests')
    ap.add_argument('--strict', action='store_true', help='require PASS for every selected test; no skips or known failures')
    ap.add_argument('--output', type=Path, help='new run directory (must not already exist)')
    ap.add_argument('--verify-report', type=Path, help='accept an existing PASS report only if it still matches this tree')
    ap.add_argument('--require', default=os.environ.get('NORTHLIGHT_REQUIRE', ''),
                    help='comma-separated requirements that must be available (FAIL instead of SKIP)')
    ap.add_argument('--record', metavar='VERSION', help='write reports to renderer/records/validation-VERSION')
    ap.add_argument('--timeout', type=int, default=1800, help='per-test timeout in seconds')
    ap.add_argument('--no-nice', action='store_true', help='do not lower the CPU priority')
    ap.add_argument('--update-discovery', action='store_true', help=f'rewrite {DISCOVERY.relative_to(fp.REPO)}')
    args = ap.parse_args(argv)
    if args.suite and args.patterns:
        ap.error('Choose a suite or test patterns, not both.')
    if args.verify_report and (args.suite or args.patterns or args.list or args.update_discovery or args.output or args.record):
        ap.error('--verify-report is a separate read-only operation.')
    if args.record and args.output:
        ap.error('Choose --record or --output, not both.')
    if args.update_discovery and (args.patterns or args.suite):
        ap.error('--update-discovery inventories the whole suite; do not combine it with a selection.')
    if args.jobs is not None and args.jobs < 1 or args.timeout < 1:
        ap.error('--jobs and --timeout must be positive.')
    if args.record and not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', args.record):
        ap.error('--record must be a version name, not a path.')

    problems = check_layout.check()
    if problems:
        print('\n'.join(['check_layout failed:', *problems]), file=sys.stderr)
        return 2
    if args.verify_report:
        try:
            return verify_report(args.verify_report)
        except (OSError, ValueError, KeyError, TypeError) as e:
            ap.error(f'Invalid report: {e}')
    names = [t.name for t in discover([])]
    if args.update_discovery:
        DISCOVERY.write_bytes(''.join(n + '\n' for n in names).encode('utf-8'))
        print(f'Updated {DISCOVERY.name}: {len(names)} tests; nothing executed.')
        return 0
    listed = DISCOVERY.read_text().split() if DISCOVERY.is_file() else []
    if names != listed:
        print(f'Discovered tests differ from {DISCOVERY.name}: '
              f'new {sorted(set(names) - set(listed))}, missing {sorted(set(listed) - set(names))}. '
              'Run --update-discovery if intended.', file=sys.stderr)
        return 2
    try:
        definitions = suites()
        for name, suite in definitions.items():
            missing = set(suite['tests']) - set(names)
            if missing:
                raise ValueError(f'Suite {name}: missing tests {sorted(missing)}')
        if args.suite and args.suite not in definitions:
            raise ValueError(f'Unknown suite {args.suite!r}; choose {", ".join(definitions)}')
    except (OSError, ValueError, KeyError, TypeError) as e:
        ap.error(str(e))
    patterns = definitions[args.suite]['tests'] if args.suite else args.patterns
    for pattern in patterns:
        if not discover([pattern]):
            ap.error(f'No tests match {pattern!r}.')
    tests = discover(patterns)
    if not tests:
        print('No tests match.', file=sys.stderr)
        return 2
    if args.list:
        for t in tests:
            requires, flags = tags(t)
            print(f'{t.name}\trequires={",".join(requires) or "none"}\t{" ".join(sorted(flags))}')
        return 0

    required = {r for r in args.require.split(',') if r}
    run_id = time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8]
    out_root = (fp.RECORDS / f'validation-{args.record}' / run_id if args.record else
                fp.protect_output(args.output.resolve()) if args.output else fp.out() / 'test-output' / run_id)
    out_root = out_root.resolve()
    if not args.record and fp.REPO in out_root.parents and not any(
            allowed == out_root or allowed in out_root.parents for allowed in
            (fp.out().resolve(), fp.REPO / 'out', fp.REPO / '.local')):
        ap.error('Run output belongs under the configured output root, out/, .local/, or outside the checkout.')
    try:
        out_root.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        ap.error(f'Run directory already exists: {out_root}; choose a new path.')
    prefix = [] if args.no_nice or not shutil.which('nice') else ['nice', '-n', '15']
    before = input_snapshot()
    git_before = git_state()
    probes = {}
    plan, results = [], {}
    for t in tests:
        requires, flags = tags(t)
        for requirement in requires:
            if requirement not in probes:
                try:
                    probes[requirement] = probe(requirement)
                except (OSError, subprocess.SubprocessError) as e:
                    probes[requirement] = f'prerequisite probe failed: {e}'
        missing = [(r, probes[r]) for r in requires]
        missing = [(r, why) for r, why in missing if why]
        if 'manual' in flags and not patterns:
            results[t.name] = ('SKIP', 0.0, 'manual: run it by name')
        elif missing:
            status = 'FAIL' if any(r in required for r, _ in missing) else 'SKIP'
            results[t.name] = (status, 0.0, '; '.join(f'requires {r}: {why}' for r, why in missing))
        else:
            plan.append((t, flags))

    def run(item):
        t, flags = item
        env = dict(os.environ, NORTHLIGHT_TEST_OUTPUT_DIR=str(out_root / t.stem), PYTHONIOENCODING='utf-8')
        start = time.perf_counter()
        try:
            r = subprocess.run(prefix + [sys.executable, str(t)], cwd=fp.TESTS, env=env,
                               capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=args.timeout)
            status = 'PASS' if r.returncode == 0 else 'FAIL'
            tail = (r.stdout + r.stderr).strip()
        except subprocess.TimeoutExpired as e:
            def text(value):
                return value.decode('utf-8', 'replace') if isinstance(value, bytes) else value or ''
            status, tail = 'TIMEOUT', text(e.stdout) + text(e.stderr) + '\nTest timed out.'
        except OSError as e:
            status, tail = 'FAIL', str(e)
        if 'known-fail' in flags and status != 'TIMEOUT':
            status = {'FAIL': 'XFAIL', 'PASS': 'XPASS'}[status]   # XPASS: remove the tag
        log = out_root / 'logs' / (t.stem + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(tail + '\n', encoding='utf-8')
        last = tail.splitlines()[-1] if tail and status not in ('PASS', 'XPASS') else ''
        if status in ('XFAIL', 'XPASS'):
            last = f'known-fail: {known_fail.get(t.name, "")} | {last}' if status == 'XFAIL' else 'passes now: remove known-fail'
        return t.name, (status, time.perf_counter() - start, last[:300])

    # Device tests and timing tests never compete within this runner.
    def exclusive(item):
        return 'timing' in item[1] or 'windows-gpu' in tags(item[0])[0]
    parallel = [i for i in plan if not exclusive(i)]
    parallel.sort(key=lambda i: ('slow' not in i[1], i[0].name))
    serial = [i for i in plan if exclusive(i)]
    print(f'{len(plan)} to run ({len(serial)} GPU/timing tests serially at the end), '
          f'{len(results)} not run; logs in {out_root.relative_to(fp.REPO) if fp.REPO in out_root.parents else out_root}', flush=True)
    jobs = args.jobs or (1 if args.suite in ('architecture', 'gpu') else 4)
    with cf.ThreadPoolExecutor(max_workers=jobs) as ex:
        for name, res in ex.map(run, parallel):
            results[name] = res
            print(f'{res[0]:7} {name}', flush=True)
    for item in serial:
        name, res = run(item)
        results[name] = res
        print(f'{res[0]:7} {name}', flush=True)

    out_root.mkdir(parents=True, exist_ok=True)
    with open(out_root / 'results.tsv', 'w', encoding='utf-8') as f:
        for name in sorted(results):
            status, seconds, note = results[name]
            f.write(f'{name}\t{status}\t{seconds:.0f}\t{note}\n')
    counts = {s: sum(1 for v in results.values() if v[0] == s) for s in ('PASS', 'FAIL', 'TIMEOUT', 'SKIP', 'XFAIL', 'XPASS')}
    after = input_snapshot()
    git_after = git_state()
    changed = sorted(name for name in before.keys() | after.keys() if before.get(name) != after.get(name))
    stale = bool(changed or git_before['commit'] != git_after['commit'])
    incomplete = any(v[0] != 'PASS' for v in results.values())
    failed = bool(counts['FAIL'] or counts['TIMEOUT'])
    overall = 'STALE' if stale else 'FAIL' if failed else 'INCOMPLETE' if incomplete else 'PASS'
    exit_code = int(stale or failed or (args.strict and incomplete))
    report = dict(schema_version=1, suite=args.suite, patterns=patterns, strict=args.strict,
                  scope=definitions[args.suite]['description'] if args.suite else 'Selected repository tests; not a live-game performance measurement.',
                  status=overall, all_passed=overall == 'PASS', exit_code=exit_code,
                  environment=dict(python=sys.version, executable=sys.executable, platform=sys.platform),
                  git=git_before, git_after=git_after, input_identity=input_identity(before),
                  input_identity_after=input_identity(after), inputs=before, changed_inputs=changed,
                  requirements=probes, counts=counts, tests={
                      name: dict(status=result[0], seconds=result[1], note=result[2],
                                 command=prefix + [sys.executable, str(fp.TESTS / name)] if (out_root / 'logs' / (Path(name).stem + '.log')).is_file() else None)
                      for name, result in sorted(results.items())})
    (out_root / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    for name in sorted(results):
        status, _, note = results[name]
        if status != 'PASS':
            print(f'{status:7} {name}: {note}')
    print(' '.join(f'{k}={v}' for k, v in counts.items()) + f'  ({out_root / "results.tsv"})')
    print(f'{overall}: {out_root / "results.json"}' + (f'; inputs changed: {changed}' if stale else ''))
    return exit_code


if __name__ == '__main__':
    sys.exit(main())
