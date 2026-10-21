#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.181 (r90): the DLL's memcmp (northlight_mem.h) against an independent byte loop, with exact int
values in both argument orders: random sizes to 8192 at every offset 0..63, swept and multiple
differences, signedness byte pairs, exhaustive n <= 80, and guard pages after and before the buffers.
Built native arm64 (the word path) and x86_64 run under Rosetta (the SSE2 path), each at -O2 and with
ASan+UBSan. Counterfactuals must fail: a sign-only result, a signed-char difference, no overlapping
tail, and a 16-byte tail loaded at i (faults on the guard page, in a child process). Plus source
needles: one strong memcmp, no_builtin, in renderer.cpp only and not exported. Running a test binary
under Rosetta is neither WoW nor Wine; nothing else is run."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib,json,platform,re,signal,subprocess,tempfile
ARCHES=('arm64','x86_64') if sys.platform=='darwin' else (platform.machine(),)
HERE=Path(__file__).resolve().parent
FILES=['northlight_mem.h','renderer.cpp','frd9.def','test_fast_memcmp.cpp','test_fast_memcmp.py']
report={'scope':'Host tests of NorthlightMem::compare; no DLL, game or Wine run.',
        'source_sha256':{n:hashlib.sha256(fp.tracked(n).read_bytes()).hexdigest() for n in FILES},'runs':[]}
SANITIZE=['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
with tempfile.TemporaryDirectory(prefix='northlight-fast-memcmp-') as tmp:
    def build(name,arch,flags):
        exe=Path(tmp)/name
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*(['-arch',arch] if sys.platform=='darwin' else []),*flags,*fp.test_include_flags(),str(HERE/'test_fast_memcmp.cpp'),'-o',str(exe)],check=True)
        return exe
    for arch in ARCHES:
        for label,flags in (('O2',['-O2']),('asan+ubsan',SANITIZE)):
            run=subprocess.run([str(build(f'{arch}-{label}',arch,flags))],capture_output=True,text=True)
            print(f'{arch} {label}: {run.stdout.strip()}',flush=True);assert run.returncode==0,(arch,label,run.stdout,run.stderr)
            report['runs'].append({'arch':arch,'build':label,'stdout':run.stdout})
    counterfactuals={1:'sign-only result',2:'signed-char difference',3:'no overlapping tail',4:'16-byte tail loaded at i (over-read)'}
    for cf,what in counterfactuals.items():
        for arch in tuple(a for a in ARCHES if cf!=4 or a in ('x86_64','AMD64')):
            exe=build(f'cf{cf}-{arch}',arch,['-O2',f'-DNORTHLIGHT_MEMCMP_COUNTERFACTUAL={cf}'])
            run=subprocess.run([str(exe),'guard' if cf==4 else 'all'],capture_output=True,text=True)
            failed=run.returncode in (-signal.SIGSEGV,-signal.SIGBUS) if cf==4 else run.returncode!=0 and 'MISMATCH' in run.stderr
            print(f'counterfactual {cf} ({what}, {arch}): '+('fails as required' if failed else 'NOT DETECTED')+f' (exit {run.returncode})',flush=True)
            assert failed,(cf,arch,run.returncode,run.stdout,run.stderr)
            report['runs'].append({'counterfactual':cf,'arch':arch,'exit_code':run.returncode})

src={p.name:p.read_text() for d in fp.SRC_DIRS for p in Path(d).glob('*') if p.suffix in ('.h','.cpp','.inl')}
definitions=[(n,m.group(0)) for n,t in src.items() for m in re.finditer(r'extern "C"[^;{]*\bint memcmp\(',t)]
renderer=src['renderer.cpp']
checks={
 'exactly one strong memcmp, in renderer.cpp, with no_builtin':len(definitions)==1 and definitions[0][0]=='renderer.cpp'
   and 'no_builtin("memcmp")' in definitions[0][1] and 'no_builtin("bcmp")' in definitions[0][1]
   and 'int memcmp(const void* a,const void* b,size_t n){return NorthlightMem::compare(a,b,n);}' in renderer,
 'no bcmp or other mem* override':not any(re.search(r'extern "C"[^;{]*\b(bcmp|memcpy|memset|memmove)\(',t) for t in src.values()),
 'memcmp is not exported':'memcmp' not in fp.src('frd9.def').read_text(),
 'the compare folds into memcmp (always_inline), the legacy loop is kept apart (noinline, no_builtin)':
   '#define NORTHLIGHT_MEM_INLINE inline __attribute__((always_inline))' in src['northlight_mem.h']
   and '__attribute__((noinline,no_builtin("memcmp"),no_builtin("bcmp")))\ninline int byteCompare(' in src['northlight_mem.h'],
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values()),definitions
report['checks']=checks
for n,digest in report['source_sha256'].items():assert hashlib.sha256(fp.tracked(n).read_bytes()).hexdigest()==digest,n
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
(out/'fast-memcmp-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS fast memcmp: exact values on both paths, guard pages clean, four counterfactuals fail')
