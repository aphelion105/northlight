#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exercise the actual logger's flush boundary, output, errors and thread isolation."""
from pathlib import Path
import argparse,hashlib,json,re,statistics,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--benchmark',action='store_true');args=parser.parse_args()
out=fp.out()/'frame-logging-batch';out.mkdir(exist_ok=True)
source=fp.src('renderer.cpp').read_text()
effects=source[source.index('    void renderEffects() {'):]
assert effects.index('struct EffectsSpan')<effects.index('LogFlushBatch logBatch;')
assert effects.index('CpuScope frameCostScope')<effects.index('LogFlushBatch logBatch;')<effects.index('rainMrtUnbind();')
begin=source.index('static SRWLOCK logLock=')
end=source.index('static void reportLogCost()',begin)
(out/'logger.inc').write_text(source[begin:end])
cpp=r'''
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <cassert>
#include <chrono>
#include <io.h>
#include <fcntl.h>
static FILE* logFile=nullptr;
static wchar_t rootPath[MAX_PATH]={};
static void rotatePreviousLog(){}
static std::atomic<unsigned> flushCalls{0};
static int countedFlush(FILE* file){++flushCalls;return std::fflush(file);}
#define fflush countedFlush
#include "logger.inc"
#undef fflush
static std::string persisted(){
    const HANDLE h=reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(logFile)));
    LARGE_INTEGER size{},zero{};assert(GetFileSizeEx(h,&size)&&size.QuadPart<1000000);
    assert(SetFilePointerEx(h,zero,nullptr,FILE_BEGIN));std::string data(size_t(size.QuadPart),'\0');DWORD got=0;
    assert(ReadFile(h,data.data(),DWORD(data.size()),&got,nullptr)&&got==data.size());
    assert(std::fseek(logFile,0,SEEK_END)==0);return data;
}
static void lightDiagnostics(){for(unsigned i=0;i<48;++i)logf("EQUIPMENT source id=%u position=(%.3f %.3f %.3f) nativeRGB=(%.4f %.4f %.4f) denominator=(%.4f %.4f %.4f) rgb=(%.4f %.4f %.4f) range=%.2f gain=%.3f cameraYards=%.2f selfYards=%.2f",i,5898.26,658.99,646.60,.0201,.0004,0.,0.,.7,.03,.0275,.0005,0.,12.,1.,26.22,21.55);}
static void benchmark(){
    std::string expected;
    for(unsigned block=0;block<16;++block){const bool batch=block%4==1||block%4==2;
        logFile=std::tmpfile();assert(logFile);const unsigned before=flushCalls;
        if(batch){LogFlushBatch grouped;lightDiagnostics();}else lightDiagnostics();
        assert(flushCalls-before==(batch?1u:48u));
        const auto text=persisted();if(expected.empty())expected=text;else assert(text==expected);
        const unsigned first=flushCalls;const auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<64;++frame){if(batch){LogFlushBatch grouped;lightDiagnostics();}else lightDiagnostics();}
        const double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/64;
        assert(flushCalls-first==64*(batch?1u:48u)&&!logFlushPending&&!logFlushBatchDepth);
        std::fclose(logFile);logFile=nullptr;
        std::printf("LIGHT_LOG_TIMING block=%u batch=%u warm=%u cpuMs=%.9f flushes=%u lines=48\n",block,unsigned(batch),unsigned(block<4),cpu,batch?1u:48u);
    }
}
int main(int argc,char** argv){assert(argc==2||argc==3);
    for(bool text:{false,true}){
        logFile=std::tmpfile();assert(logFile);assert(_setmode(_fileno(logFile),text?_O_TEXT:_O_BINARY)!=-1);
        logCost={};const auto lines=threadLogLines;std::string expected;
        const std::string nl=text?"\r\n":"\n";
        {LogFlushBatch empty;}assert(!logFlushBatchDepth&&!logFlushPending&&!logCost.calls);
        try{LogFlushBatch batch;logf("value=%d rgba=%.2f",3,1.25);expected+="value=3 rgba=1.25"+nl;
            {LogFlushBatch nested;logf("%s","percent %n %p %s");expected+="percent %n %p %s"+nl;}
            logf("%s",static_cast<const char*>(nullptr));expected+="(null)"+nl;
            const std::string longLine(32768,'x');logf("%s",longLine.c_str());expected+=longLine+nl;
            throw 1;
        }catch(int){}
        assert(!logFlushBatchDepth&&!logFlushPending&&persisted()==expected&&logCost.calls==4&&!logCost.ioErrors&&threadLogLines==lines+4);
        const auto bytesBefore=logCost.formattedBytes;std::thread threads[3];
        for(unsigned t=0;t<3;++t)threads[t]=std::thread([t]{for(unsigned i=0;i<100;++i){LogFlushBatch batch;logf("thread-%u-%u",t,i);}assert(!logFlushBatchDepth&&!logFlushPending);});
        for(auto& t:threads)t.join();auto data=persisted();size_t expectedBytes=bytesBefore;
        for(unsigned t=0;t<3;++t)for(unsigned i=0;i<100;++i){auto line="thread-"+std::to_string(t)+"-"+std::to_string(i);assert(data.find(line+nl)!=std::string::npos);expectedBytes+=line.size()+1;}
        assert(logCost.calls==304&&logCost.formattedBytes==expectedBytes&&!logCost.ioErrors);std::fclose(logFile);
    }
    logFile=std::fopen(argv[1],"rb");assert(logFile);const auto errors=logCost.ioErrors;
    {LogFlushBatch batch;logf("%s","write refused");}assert(logCost.ioErrors>errors);std::fclose(logFile);logFile=nullptr;
    std::puts("FRAME_LOG_BATCH_PASS binary_text exact_output long_null_percent nested_exception concurrent_thread_flush io_errors");
    if(argc==3)benchmark();
}
'''
(out/'test.cpp').write_text(cpp)
sentinel=out/'read-only-input.txt';sentinel.write_bytes(b'unchanged')
for opt in ('-O0','-O2'):
    exe=out/(opt[1:]+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu',opt,'-UNDEBUG','-std=c++17',str(out/'test.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    run=subprocess.run([str(exe),str(sentinel),*(['benchmark'] if args.benchmark and opt=='-O2' else [])],capture_output=True,text=True,check=True)
    print(run.stdout,end='');(out/(opt[1:]+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
    if args.benchmark and opt=='-O2':
        rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('LIGHT_LOG_TIMING ') and 'warm=0 ' in line]
        timings={}
        for batch in (0,1):
            values=[float(row['cpuMs']) for row in rows if row['batch']==str(batch)];assert len(values)==6
            timings['batched_ms' if batch else 'before_ms']=statistics.median(values)
        report={'pass':True,'timing':timings,'source_sha256':hashlib.sha256(fp.src('renderer.cpp').read_bytes()).hexdigest(),'scope':'48 authored diagnostic light lines; actual Windows logger and temporary FILE; CPU includes final flush; no gameplay FPS claim'}
        (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
assert sentinel.read_bytes()==b'unchanged'
