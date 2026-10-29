#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,world-cache manual
"""Real x86 scene replacement with a retained BVH at 434/395 MiB free VA.

Replay the existing seven-region production pipeline, including page creation
and 8192 rays per region. Reserve address space only in the private test process.
"""
import argparse
import io
import json
import subprocess
import struct
import sys
import zipfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--baseline-ref', default='240f7c41d61744e602429df2411e31bd142dc4a9')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source = (fp.TESTS/'validate_shared_scene_pipeline.cpp').read_text()
source = '#include <windows.h>\n#include "geometry_memory.h"\n'+source
source = source.replace('h.bytes(s.vertices.data(),s.vertices.size()*sizeof(WorldVertex));h.bytes(s.triangles.data(),s.triangles.size()*sizeof(WorldTriangle));',
                        'for(const auto& v:s.vertices)h.bytes(&v,sizeof v);for(const auto& t:s.triangles)h.bytes(&t,sizeof t);')
source = source.replace('int main(', r'''
static const char* stage="entry";
static LONG WINAPI fault(EXCEPTION_POINTERS* e){
    std::fprintf(stderr,"FAULT stage=%s code=%08lx address=%p\n",stage,e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}
static NorthlightGeometryMemory::Sample sample(){
    uint64_t available=0,largest=0;MEMORY_BASIC_INFORMATION m={};uintptr_t at=0;
    while(VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof m)){
        if(m.State==MEM_FREE){available+=m.RegionSize;largest=std::max(largest,uint64_t(m.RegionSize));}
        uintptr_t next=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;if(next<=at)break;at=next;
    }
    return {available,largest,true};
}
struct Ballast{
    std::vector<void*> blocks;
    ~Ballast(){for(void* b:blocks)assert(VirtualFree(b,0,MEM_RELEASE));}
    void set(uint64_t target){
        while(sample().available>target+65536){
            const auto s=sample();uint64_t n=std::min(s.available-target,(s.largest/2)&~uint64_t(65535));
            assert(n>=65536);void* p=VirtualAlloc(nullptr,size_t(n),MEM_RESERVE,PAGE_NOACCESS);assert(p);blocks.push_back(p);
        }
    }
};
int main(''')
source = source.replace('const Vec3 moves[]=', 'std::unique_ptr<BVH> held;\n    const Vec3 moves[]=')
source = source.replace('std::vector<std::string> nearFiles,farFiles;', r'''
        std::vector<std::string> nearFiles,farFiles;
        Ballast ballast;if(PRESSURE_MIB)ballast.set(uint64_t(PRESSURE_MIB)<<20);
        auto entry=sample();uint64_t minimum=entry.available;unsigned admissions=0;
        auto admit=[&](uint64_t bytes){
            auto s=sample();minimum=std::min(minimum,s.available);++admissions;
            bool ok=NorthlightGeometryMemory::admits(s,NorthlightGeometryMemory::coldInBuildBudget(bytes));
            if(!ok)std::fprintf(stderr,"ADMISSION refused bytes=%llu availableMiB=%.3f largestMiB=%.3f\n",(unsigned long long)bytes,double(s.available)/1048576,double(s.largest)/1048576);
            return ok;
        };
        if(PRESSURE_MIB){assert(entry.available<480ull<<20);assert(entry.available>=392ull<<20);}
''')
source = source.replace('assert(argc==3||argc==4);', 'SetUnhandledExceptionFilter(fault);assert(argc==3||argc==4);')
for before, label in [('double localMs=ms(t);', 'terrain'), ('double terrainMs=ms(t);', 'plan'), ('double planMs=ms(t);', 'pages'), ('Hash localHash', 'hashes')]:
    source = source.replace(before, 'stage="'+label+'";std::fprintf(stderr,"STAGE '+label+'\\n");'+before)
source = source.replace('local,error,{},true,provider,revision)', 'local,error,admit,true,provider,revision,BOUNDED_LOCAL)')
source = source.replace('terrain,error,1,{},terrainFilter', 'terrain,error,1,admit,terrainFilter')
source = source.replace(',true\n#endif', ',true,BOUNDED_LOCAL\n#endif')
source = source.replace('plan,error,{},radius,{},true,&local,&preparation)', 'plan,error,admit,radius,{},true,&local,&preparation)')
source = source.replace('assert(NorthlightShadowTerrain::build(local.scene(),terrain,c,plan,error,admit,radius,{},true,&local,&preparation));',
                        'assert(BOUNDED_LOCAL?NorthlightShadowTerrain::buildConsuming(local.scene(),terrain,c,plan,error,admit,radius,{},&local):NorthlightShadowTerrain::build(local.scene(),terrain,c,plan,error,admit,radius,{},true,&local,&preparation));')
source = source.replace('pages,error,{},options)', 'pages,error,admit,options)')
source = source.replace('planHash.word(plan.indices.size());planHash.bytes(plan.indices.data(),plan.indices.size()*4);',
                        'planHash.word(plan.pagingIndexCount());for(size_t i=0;i<plan.pagingIndexCount();++i){auto index=plan.pagingIndex(i);planHash.bytes(&index,4);}')
source = source.replace('        std::puts("");', r'''
        minimum=std::min(minimum,sample().available);
        std::printf(" entryMiB=%.3f minMiB=%.3f admissions=%u held=%u bounded=%u",double(entry.available)/1048576,double(minimum)/1048576,admissions,unsigned(bool(held)),unsigned(BOUNDED_LOCAL));
        if(PRESSURE_MIB)assert(minimum>=NorthlightGeometryMemory::ProcessReserve);
        held=std::make_unique<BVH>(std::move(local));
        std::puts("");''')
cpp = args.output/'streaming_pressure.cpp'
cpp.write_text(source)
results = {}
baseline = args.output/'baseline-source'
archive = subprocess.check_output(['git', 'archive', '--format=zip', args.baseline_ref, 'src'], cwd=fp.REPO)
with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
    for entry in zipped.infolist():
        target = baseline/entry.filename
        assert target.resolve().is_relative_to(baseline.resolve())
        if entry.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(zipped.read(entry))
for label, bounded, pressure in [('flat', 0, 0), ('bounded434', 1, 434), ('bounded395', 1, 395)]:
    exe = args.output/(label+'.exe')
    tree = baseline if label == 'flat' else fp.REPO
    includes = [flag for folder in sorted((tree/'src').iterdir()) if folder.is_dir() for flag in ['-I', str(folder)]]
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-g', '-O2', '-UNDEBUG',
                    '-DSHARED_SCENE_PIPELINE=1', '-DCURRENT_TERRAIN_CHUNKS=1',
                    '-DCURRENT_MEMORY_PROFILE=1', '-DBOUNDED_LOCAL='+str(bounded), '-DPRESSURE_MIB='+str(pressure),
                    *includes, str(cpp), str(tree/'src/gi/world_gi.cpp'), str(tree/'src/world/world_mesh_plan.cpp'),
                    '-o', str(exe)], env=fp.zig_env(), check=True)
    data = bytearray(exe.read_bytes())
    flags = struct.unpack_from('<I', data, 0x3c)[0]+22
    struct.pack_into('<H', data, flags, struct.unpack_from('<H', data, flags)[0] | 0x20)
    exe.write_bytes(data)
    run = subprocess.run([str(exe), str(fp.world_cache()), 'Northrend'], text=True, capture_output=True)
    (args.output/(label+'.log')).write_text(run.stdout+run.stderr)
    assert run.returncode == 0, (label, run.returncode, run.stderr[-1500:])
    results[label] = [dict(part.split('=', 1) for part in line.split()[1:]) for line in run.stdout.splitlines() if line.startswith('PIPELINE ')]
    assert len(results[label]) == 7
    print(label, 'complete', flush=True)
for label in ['bounded434', 'bounded395']:
    for old, new in zip(results['flat'], results[label]):
        for key in ['plan', 'pagesHash', 'rays', 'localTriangles', 'planTriangles', 'pages', 'batches']:
            assert old[key] == new[key], (label, new['step'], key, old[key], new[key])
(args.output/'validation.json').write_text(json.dumps({'all_equal': True, 'baseline_ref': args.baseline_ref, 'results': results}, indent=2)+'\n')
print('PASS retained scene replacement at 434/395 MiB, unchanged page/ray results and process reserve')
