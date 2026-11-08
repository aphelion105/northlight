#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Coarse effects attribution on sampled and slow applying frames; no game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import os,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
r=fp.src('renderer.cpp').read_text(encoding='utf-8');w=fp.src('world_renderer.h').read_text(encoding='utf-8');h=fp.src('effects_buckets.h').read_text(encoding='utf-8')
effects=r[r.index('    void renderEffects() {'):r.index('    // One shader query/reference per original draw')]
render=w[w.index('    bool render(IDirect3DSurface9* targetSurface'):]
world_marks=len(re.findall(r'\bbucket\(NorthlightEffectsBuckets::',render));outer_marks=effects.count('effectsBuckets.mark(')
checks={
 'all applying RenderProfile frames, no-op guard preserves the first split and sampled CPU scope':('CpuScope cpu(sampled()?&cpuEffects:nullptr);' in effects
    and 'effectsBucketed=NorthlightRenderThreadProbe::profiling();' in effects and 'effectsBuckets.begin(effectsBucketed,&qpcNow,counters,4);}' in effects
    and 'struct BucketsEnd {NorthlightEffectsBuckets::Frame& f;~BucketsEnd(){f.end();}} bucketsEnd{effectsBuckets};' in effects
    and effects.index('CpuScope cpu(sampled()')<effects.index('if (applied || !enabled || failed || !projectionValid) return;')<effects.index('effectsBucketed=')<effects.index('effectsBuckets.begin(')<effects.index('bucketsEnd{effectsBuckets}')),
 'raw counting remains sampled, unavailable counters are explicit':('const std::uint32_t* counters[4]={};if(mirrorState.rawCounting)' in effects
    and 'mirrorState.rawCounting=sampled()&&NorthlightRenderThreadProbe::profiling();' in r
    and 'counted?draws:-1,unsigned(counted)' in r and 'counted?int(effectsBuckets.drawCalls(b)):-1' in r),
 'off: begin and mark read no clock':'on_=on&&clock;if(!on_)return;' in h and 'void mark(Bucket ended){if(!on_)return;' in h,
 # 0.3.209 (task 29): +2 = the two per-source actor sites run once per source. Worst case: begin, the world's single-use sites, 4 cascades (two exclusive sites each pass), every renderEffects site, end
 'at most 30 clock reads a frame (begin, the marks, end)':render.count('bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunNear+source*2+cascade))')==2
    and 1+(world_marks-2)+4+2+outer_marks+1<=30 and 'constexpr unsigned MaxReads=30;' in h,
 'the world marks through bucket() only (the renderer\'s frame pointer)':'void bucket(NorthlightEffectsBuckets::Bucket b){if(effectsBuckets)effectsBuckets->mark(b);}' in w and 'world->setEffectsBuckets(&effectsBuckets);' in r,
 'sampled or >=8ms, device/frame provenance, emitted before reset and outside sampled-only report':('if(NorthlightEffectsBuckets::report(sampledFrame,effectsMs))' in r
    and 'frame=%u device=%ld sampled=%u slow=%u applied=%d' in r and 'drawCountsKnown=%u' in r
    and r.index('const bool sampledFrame=sampled(),frameApplied=applied;')<r.index('logf("EFFECTS buckets')<r.index('effectsBucketed=false;',r.index('logf("EFFECTS buckets'))<r.index('{CpuScope cpu(sampledFrame?&cleanup:nullptr);clearFrame();}')
    and r.index('logf("EFFECTS buckets')<r.index('logf("CPU profile frame=')),
}
print(f'world marks {world_marks}, renderEffects marks {outer_marks}')
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-effects-buckets-') as tmp:
    for label,flags in [('O2',['-O2']),('sanitizers',['-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/('test-'+label)
        compiler=[str(fp.zig()),'c++','-target','x86_64-linux-musl'] if os.name=='nt' else ['clang++']
        subprocess.run([*compiler,'-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(HERE/'test_effects_buckets.cpp'),'-o',str(exe)],env=fp.zig_env() if os.name=='nt' else None,check=True)
        run=['wsl','--exec','/mnt/host/'+exe.drive[0].lower()+'/'+exe.relative_to(exe.anchor).as_posix()] if os.name=='nt' else [str(exe)]
        print(label,subprocess.check_output(run,text=True),end='',flush=True)
print('PASS effects buckets: model and wiring')
