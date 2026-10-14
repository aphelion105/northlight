#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Native async mailbox plus extracted production Win32 diagnostic walker."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import hashlib,json,subprocess,tempfile

HERE=Path(__file__).resolve().parent
OUT=fp.output_dir()
FILES=['async_memory_diagnostics.h','renderer.cpp','test_async_memory_diagnostics.cpp','test_async_memory_diagnostics.py']
hashes={n:hashlib.sha256(fp.tracked(n).read_bytes()).hexdigest() for n in FILES}
source=fp.src('renderer.cpp').read_text()
begin=source.index('static NorthlightMemoryDiagnostics::Sample queryMemoryDiagnostic(')
end=source.index('template<class T> static void drop',begin)
walker=source[begin:end]
fixture=r'''
#include "async_memory_diagnostics.h"
#include "geometry_memory.h"
#include <algorithm>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <vector>
#undef UINTPTR_MAX
#define UINTPTR_MAX 0xffffffffu
using DWORD=std::uint32_t;
struct MEMORYSTATUSEX {DWORD dwLength;std::uint64_t ullAvailVirtual=0,ullTotalVirtual=0,ullAvailPhys=0;};
struct MEMORY_BASIC_INFORMATION {void* BaseAddress;std::uint32_t RegionSize;unsigned State;};
constexpr unsigned MEM_FREE=1,MEM_COMMIT=2;
static bool fail=false;static unsigned calls=0;static std::uint64_t apiAvailable=9000;
struct Region{std::uint32_t base,size;unsigned state;};
static std::vector<Region> regions;
static int GlobalMemoryStatusEx(MEMORYSTATUSEX* memory){if(fail)return 0;memory->ullAvailVirtual=apiAvailable;memory->ullTotalVirtual=10000;memory->ullAvailPhys=8000;return 1;}
static size_t VirtualQuery(const void* address,MEMORY_BASIC_INFORMATION* output,size_t bytes){
 ++calls;const auto at=reinterpret_cast<uintptr_t>(address);
 for(const auto& r:regions)if(r.base==at){output->BaseAddress=reinterpret_cast<void*>(uintptr_t(r.base));output->RegionSize=r.size;output->State=r.state;return bytes;}
 return 0;
}
static DWORD GetTickCount(){return 1234;}
'''+walker+r'''
int main(){
 regions={{0,100,MEM_FREE},{100,50,MEM_COMMIT},{150,200,MEM_FREE}};
 auto sample=queryMemoryDiagnostic(nullptr);assert(sample.valid&&sample.availableVirtual==9000&&sample.totalVirtual==10000&&sample.availablePhysical==8000);
 assert(sample.largestFree==200&&sample.totalFree==300&&sample.regions==3&&sample.tick==1234&&calls==4);
 calls=0;regions={{0,0xffffffffu,MEM_FREE},{0xffffffffu,1,MEM_FREE}};
 sample=queryMemoryDiagnostic(nullptr);assert(sample.valid&&sample.regions==2&&sample.totalFree==0x100000000ull&&sample.largestFree==0xffffffffu&&calls==2);
 calls=0;regions={{0,0,MEM_COMMIT}};sample=queryMemoryDiagnostic(nullptr);assert(sample.valid&&sample.regions==1&&sample.totalFree==0&&calls==1);
 // task 42: Wine's API value ignores reserve-only regions; the walked free total wins once the overstatement passes BiasFloor.
 calls=0;apiAvailable=2656ull<<20;regions={{0,389u<<20,MEM_FREE},{389u<<20,1900u<<20,MEM_COMMIT}};sample=queryMemoryDiagnostic(nullptr);
 assert(sample.valid&&sample.availableVirtual==(389ull<<20)&&NorthlightGeometryMemory::corrected(2656ull<<20)==(389ull<<20));
 apiAvailable=(389ull<<20)+(32ull<<20);sample=queryMemoryDiagnostic(nullptr);
 assert(sample.availableVirtual==apiAvailable&&NorthlightGeometryMemory::corrected(apiAvailable)==apiAvailable);
 apiAvailable=9000;
 calls=0;fail=true;sample=queryMemoryDiagnostic(nullptr);assert(!sample.valid&&sample.regions==0&&calls==0);
 std::puts("PASS extracted production memory query: exact mixed-region sum/max; 32-bit address overflow; non-progressing range; query failure; diagnostic timestamps.");
}
'''
report={'scope':'Native actual asynchronous sampler and production query function with simulated Win32 region API. No game, Wine or graphics execution. Allocation-admission paths unchanged.','source_sha256':hashes,'runs':[]}
with tempfile.TemporaryDirectory(prefix='async-memory-') as folder:
    folder=Path(folder);(folder/'query.cpp').write_text(fixture)
    for label,path in [('mailbox',HERE/'test_async_memory_diagnostics.cpp'),('walker',folder/'query.cpp')]:
        for mode,flags in [('O2',['-O2']),('san',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]:
            executable=folder/(label+'-'+mode)
            command=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread',*flags,*fp.test_include_flags(),str(path),'-o',str(executable)]
            subprocess.run(command,check=True)
            result=subprocess.run([str(executable)],capture_output=True,text=True,timeout=30)
            print(result.stdout,result.stderr,flush=True);result.check_returncode()
            report['runs'].append({'label':label,'mode':mode,'compile_command':command,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
for name,digest in hashes.items():assert hashlib.sha256(fp.tracked(name).read_bytes()).hexdigest()==digest,name
OUT.mkdir(exist_ok=True)
(OUT/'async-memory-validation.json').write_text(json.dumps(report,indent=2)+'\n')
