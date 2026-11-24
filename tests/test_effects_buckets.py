#!/usr/bin/env python3
# northlight-test: requires=zig
"""Coarse effects attribution on sampled and slow applying frames; no game or GPU."""

from source_units import source_text, world_render_path
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import os,re,subprocess
HERE=Path(__file__).resolve().parent
r=source_text(fp.src('renderer.cpp'), encoding='utf-8');w=source_text(fp.src('world_renderer.h'), encoding='utf-8');h=fp.src('effects_buckets.h').read_text(encoding='utf-8')
effects_start=r.index('    void renderEffects() {')
effects=r[effects_start:r.index('\n    }\n',effects_start)]
render=world_render_path(w)
contact=fp.src('contact_ao_pass.h').read_text(encoding='utf-8')
screen_resolve=fp.src('world_soft_shadows.inl').read_text(encoding='utf-8')
screen_lighting=fp.src('world_screen_lighting.inl').read_text(encoding='utf-8')
local_lighting=fp.src('world_local_lighting.inl').read_text(encoding='utf-8')
local_owner=fp.src('world_local_lighting.h').read_text(encoding='utf-8')
fog=fp.src('world_atmospheric_fog.inl').read_text(encoding='utf-8')
world_marks=len(re.findall(r'\b(?:bucket\(|buckets->mark\()NorthlightEffectsBuckets::',render+screen_resolve+screen_lighting+local_lighting+local_owner+fog));outer_marks=effects.count('effectsBuckets.mark(')+contact.count('effectsBuckets.mark(')
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
 'at most 31 clock reads a frame (begin, the marks, end)':render.count('bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunNear+source*2+cascade))')==2
    and effects.count('NorthlightContactAO::render<CpuScope>')==1
    and 1+(world_marks-2)+4+2+outer_marks+1<=31 and 'constexpr unsigned MaxReads=31;' in h,
 'atmosphere work and join are attributed before the point-shadow pass':render.index('replayJobs_.wait(atmosphereDone);else atmosphereWork(')<render.index('bucket(NorthlightEffectsBuckets::Selection); // Atmosphere')<render.index('renderPointShadows(atmosphere.lights,'),
 'the world marks through bucket() only (the renderer\'s frame pointer)':'void bucket(NorthlightEffectsBuckets::Bucket b){if(effectsBuckets)effectsBuckets->mark(b);}' in w and 'world->setEffectsBuckets(&effectsBuckets);' in r,
 'sampled or >=8ms, device/frame provenance, emitted before reset and outside sampled-only report':('if(NorthlightEffectsBuckets::report(sampledFrame,effectsMs))' in r
    and 'frame=%u device=%ld sampled=%u slow=%u applied=%d' in r and 'drawCountsKnown=%u' in r
    and r.index('const bool sampledFrame=sampled(),frameApplied=applied;')<r.index('logf("EFFECTS buckets')<r.index('effectsBucketed=false;',r.index('logf("EFFECTS buckets'))<r.index('{CpuScope cpu(sampledFrame?&cleanup:nullptr);clearFrame();}')
    and r.index('logf("EFFECTS buckets')<r.index('logf("CPU profile frame=')),
}
print(f'world marks {world_marks}, renderEffects marks {outer_marks}')
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
folder=fp.output_dir()/'effects-buckets';folder.mkdir(parents=True,exist_ok=True)
compiler=[str(fp.zig()),'c++']
for label,flags in [('O2',['-O2']),('sanitizers',['-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=all'])]:
    exe=folder/('test-'+label+('.exe' if os.name=='nt' else ''))
    subprocess.run([*compiler,'-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(HERE/'test_effects_buckets.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS effects buckets: model and wiring')
