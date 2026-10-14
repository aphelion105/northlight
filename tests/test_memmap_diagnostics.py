#!/usr/bin/env python3
# northlight-test: requires=cxx
"""MEMMAP address-space snapshots (0.3.192): host test of the pure grouping/diff/format logic of
address_space_snapshot.h with fake region lists (-O2 and ASan/UBSan), and a source pin of the five
lifecycle call sites in renderer.cpp, the allowed-API set and the line formats. No game, Wine or DLL is run."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='memmap-') as folder:
    for mode,flags in [('O2',['-O2']),('san',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(folder)/('t-'+mode)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_memmap_diagnostics.cpp'),'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);print(r.stdout,r.stderr,flush=True);r.check_returncode()

r=fp.src('renderer.cpp').read_text();h=fp.src('address_space_snapshot.h').read_text()
def body(s,start,end):a=s.index(start);return s[a:s.index(end,a)]
dtor=body(r,'    ~Device() {','    HRESULT STDMETHODCALLTYPE QueryInterface')
ctor=body(r,'        diagnosticId=InterlockedIncrement(&deviceSerial);','    ~Device() {')
fin=body(r,'        ++frame;mirrorState.gate.frame.store','        // Per-type call counts')
checks={
 'destroy-begin before teardown (first statement after its DEVICE line)':dtor.index('memmap("destroy-begin");')<dtor.index('memoryDiagnostics.reset();') and dtor.index('DEVICE lifetime event=destroy-begin')<dtor.index('memmap("destroy-begin");'),
 'destroy-end after the backend Release':dtor.index('real->Release()')<dtor.index('DEVICE lifetime event=destroy-end')<dtor.index('memmap("destroy-end");'),
 'create right after the create line, after the device id is set':ctor.index('DEVICE lifetime event=create')<ctor.index('memmap("create");'),
 'frame 300 of every device, device 1 labelled baseline':'if(frame==300)memmap(diagnosticId==1?"baseline-frame300":"frame300");' in fin,
 'exactly four call sites (+ the definition)':r.count('memmap("')==3 and r.count('memmap(diagnosticId==1')==1 and r.count('void memmap(const char* point)')==1,
 'ungated by Diagnostics':'diagnostics()' not in dtor.split('memmap("destroy-end")')[0].split('memmap("destroy-begin")')[0][-200:] and 'NorthlightMemMap::snapshot(' in r and not re.search(r'diagnostics\(\)[^;{]*memmap\(',r),
 'never throws out of the device (try/catch around the snapshot, which also catches)':'void memmap(const char* point){\n        try{' in r and '}catch(...){}\n    }' in r and 'catch(...){}\n    busy().clear' in h,
 'one snapshot at a time, no re-entry':'test_and_set' in h and 'busy().clear' in h,
 'storage reserved once, no growth past capacity':'allocs_.reserve(MaxAllocs)' in h and 'allocs_.size()>=allocs_.capacity()' in h,
 'only own-process kernel32/toolhelp/ntdll/psapi query APIs; optional ones via GetProcAddress, no hooks, no foreign process, no game-memory reads':all(x in h for x in ('VirtualQuery(','GetProcessHeaps(','CreateToolhelp32Snapshot(','GetModuleFileNameA(','GetSystemInfo(','OpenThread(','ReadProcessMemory(GetCurrentProcess()'))
   and all(x in h for x in ('procAddress<Fn>(GetModuleHandleA("kernel32.dll"),"HeapSummary")','"K32GetProcessMemoryInfo"','"K32GetMappedFileNameA"','"GetMappedFileNameA"','"NtQueryInformationThread"','GetModuleHandleA("ntdll.dll")'))
   and 'HeapWalk' not in h.replace('no HeapWalk','') and not re.search(r'\b(?:EnumProcessModules|GetProcessMemoryInfo|WriteProcessMemory|OpenProcess|SetWindowsHookEx|VirtualProtect\w*|SuspendThread|GetThreadContext|MH_\w+)\s*\(',h) and 'ReadProcessMemory(' in h and h.count('ReadProcessMemory(')==1,
 'heap summary under an 8 ms budget, skipped heaps logged':'ms(th,t2,freq)>8.0' in h and 'skippedByBudget=%u' in h and 'skipped=budget' in h and 'unlisted=%u' in h and 'longestSummaryMs=%.3f' in h,
 'whole user space walked (min..max application address)':'lpMinimumApplicationAddress' in h and 'lpMaximumApplicationAddress' in h,
 'tally from existing counters only (lock meter gauges, stream flags, mirror registry)':all(x in r for x in ('copyResidentBytes','copyLargeBytes','mirrorResources.size()','streamActive','memoryPressure')),
}
for tag in ['summary','top','diff-new','diff-gone','diff-changed','diff','diff-classes','process','heaps','heap','threads','northlight','done','sizehist','modules','mapped','stacks','skipped']:
    checks['line format MEMMAP '+tag]=('MEMMAP '+tag+' ' in h) or (tag in ('diff-new','diff-gone','diff-changed') and '"'+tag+'"' in h)
for key in ['commitMiB=','image=','mapped=','private=','freeMiB=','largestFreeMiB=','regions=','allocations=','walkMs=','base=0x','sizeMiB=','commitMiB=','type=%s','prot=0x','module=%s','wasMiB=','threads=%u modules=%u','owner=%s','file=%s','reserveMiB=%.1f','largestMiB=%.1f','skippedByBudget=','count=%llu totalMiB=%.1f commitMiB=%.1f']:
    checks['field '+key]=key in h
checks['sink prints through logf (one MEMMAP line per call, label = device/point/frame/tick)']='logf("%s",line)' in r and 'device=%ld point=%s frame=%u tick=%lu' in r
for k,v in checks.items():print(('PASS ' if v else 'FAIL ')+k)
assert all(checks.values())
# task 42: new points and their threads
ctor2=body(r,'    Device(IDirect3DDevice9* d,IDirect3D9* p)','    ~Device() {')
sched=body(r,'    void memmapSchedule(){','    bool worldDrawSeen=false')
asyn=body(r,'    void memmapAsync(const char* point){','    // Frame end (finishFrame)')
fac=body(r,'    HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter,D3DDEVTYPE type,HWND window,DWORD flags,D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** out) override {','    }\n};')
bk=body(r,'    HMODULE module=result.module;','    const auto& last=')
checks2={
 'backend-loaded right after the backend DLL load (before any Direct3DCreate9 call into it)':'memmapProcess("backend-loaded",0);' in bk and r.index('NorthlightBackendLoader::load(sys,candidates,system)')<r.index('memmapProcess("backend-loaded",0);')<r.index('logAttempts(result.attempts);'),
 'pre-create just before the backend CreateDevice':fac.index('memmapProcess("pre-create"')<fac.index('real->CreateDevice(adapter,type,window,flags,pp,out)') and fac.index('real->CreateDevice(adapter,type,window,flags,pp,out)')<fac.index('new Device('),
 'process-level points run on the caller, in-world points through the sampler job':'static void memmapProcess(' in r and 'memoryDiagnostics->requestJob(&job,sizeof job)' in asyn and 'NorthlightMemMap::snapshot(' not in asyn and 'memoryDiagnostics->setJob(&memmapJob)' in ctor2,
 'sampler thread: job entry runs the snapshot at below-normal priority':'static void memmapJob(' in r and 'NorthlightMemMap::runJob(&memmapSink,data,size)' in r and 'THREAD_PRIORITY_BELOW_NORMAL' in body(r,'static void memmapJob(','static void memmapProcess('),
 'world-entry: 600 frames after the first successful world draw':'worldDrawSeen?0:(worldDrawSeen=true,worldDrawFrame=frame,0)' in r and 'frame-worldDrawFrame>=600' in sched and 'point="world-entry"' in sched and 'if(!worldDrawSeen)return;' in sched,
 'periodic: every 10 minutes after world-entry':'point="periodic"' in sched and 'memmapPeriodicAt=now+600000' in sched and '300000' not in sched,
 'memory-deferred: first refusal, then at most once per 10 minutes':'point="memory-deferred"' in sched and 'now-memmapDeferredAt>=600000' in sched and 'deferredRequest.exchange(false' in sched,
 'no per-frame cost when nothing is due (one relaxed load, clock every 32nd frame)':'(frame&31)' in sched and sched.count('GetTickCount()')==1 and 'memmapSchedule(); /* task 42' in fin,
 'a stale refusal of an earlier device is cleared at creation':'deferredRequest.store(false' in ctor2,
 'worker-side refusal sets the flag with a relaxed store only':all('NorthlightMemMap::deferredRequest.store(true,std::memory_order_relaxed)' in w for w in (fp.src('world_renderer.h').read_text(),)) and fp.src('world_renderer.h').read_text().count('deferredRequest.store(true')==2,
 'northlight tally: in-world stream atomics and world plain reads, skipped sources named':all(x in r for x in ('streamQueueMiB=','streamBufShadowMiB=','tallySkipped=staticShadowCpuGpu,localScene,replayCaches(need locks)','world->memmapTally(')) and 'meshPoolMiB=%.1f' in fp.src('world_renderer.h').read_text(),
 'async sampler carries the job without holding its mutex while it runs':'bool requestJob(' in fp.src('async_memory_diagnostics.h').read_text() and 'lock.unlock();\n                try{if(job)job(context,local,size);}' in fp.src('async_memory_diagnostics.h').read_text(),
 'diff list raised to 48 and diff-classes line present':'DiffCap=48' in h and 'emitDiffClasses(sink,label,d)' in h,
 'owner marks in top/diff lists (owner=stack/heap)':'" owner=%s"' in h and 'tagStacks(cur,spans,got)' in h and 'tagHeaps(cur,heapBases,heapHave)' in h and h.index('tagHeaps(cur,heapBases,heapHave)')<h.index('report(sink,label,cur'),
 'busy snapshot is logged, not silent':'MEMMAP skipped %s reason=busy' in h,
}
for k,v in checks2.items():print(('PASS ' if v else 'FAIL ')+k)
assert all(checks2.values())


hs=body(h,'        if(async){','        if(toolhelp)std::snprintf(line,sizeof line,"MEMMAP threads')
asyncpart=hs.split('}else{using Fn')[0]
checks3={
 'async points take no heap lock (no HeapSummary/HeapLock/HeapWalk on that path)':'async=1 summarized=0' in asyncpart and not re.search(r'HeapSummary|HeapLock|HeapWalk|fn\(',asyncpart) and 'snapshot(sink,job.label,job.tally,true)' in h and 'async?asyncLimits()' in h,
 'game-thread points keep HeapSummary and log the longest single call':'async=0' in hs and 'LARGE_INTEGER c0={},c1={}' in hs and 'longestSummaryMs' in hs and 'snapshot(&memmapSink,label,tally)' in r and 'snapshot(&memmapSink,label,"")' in r,
 'the job reads only its by-value payload (no Device/StreamCore pointer; tally formatted on the render thread)':'activeCore' not in asyn and 'activeCore' not in body(h,'inline void runJob','#endif') and 'job.requestTick=GetTickCount()' in asyn and 'memmapTally(job.tally,sizeof job.tally,true)' in asyn and 'this' not in body(r,'static void memmapJob(','static void memmapProcess('),
 'job runtime logged':'MEMMAP job %s queuedMs=%lu runMs=%.2f' in h,
 'guard sample first, job after it in the sampler loop':fp.src('async_memory_diagnostics.h').read_text().index('if(pending_){')<fp.src('async_memory_diagnostics.h').read_text().index('if(jobPending_){'),
 'async list caps (top 12, sizehist 12, modules 12, mapped 8, diff 32)':'l.top=12;l.hist=12;l.modules=12;l.mapped=8;l.diff=32;' in h,
}
for k,v in checks3.items():print(('PASS ' if v else 'FAIL ')+k)
assert all(checks3.values())
