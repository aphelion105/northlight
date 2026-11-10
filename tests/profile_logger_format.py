# northlight-test: requires=windows-exe,zig manual timing
"""Compare the actual logger's FILE formatting with a frozen git version. No game is started."""
from pathlib import Path
import argparse,hashlib,json,re,statistics,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT));import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--reference',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);source=fp.src('renderer.cpp')
for name,text in [('Current',source.read_text(encoding='utf-8')),('Before',subprocess.check_output(['git','show',a.reference+':src/proxy/renderer.cpp']).decode('utf-8'))]:
 section=text[text.index('static SRWLOCK logLock='):text.index('static void reportLogCost()')]
 (out/(name+'.inc')).write_text('namespace '+name+' { static FILE* logFile=nullptr;static wchar_t rootPath[MAX_PATH]={};static void rotatePreviousLog(){}\n'+section+'}\n',encoding='utf-8')
cpp=r'''
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cassert>
#include <string>
#include <chrono>
#include <limits>
#include <cstring>
#include <io.h>
#include <fcntl.h>
#include "Current.inc"
#include "Before.inc"
static std::string read(FILE* f){assert(std::fflush(f)==0&&std::fseek(f,0,SEEK_SET)==0);std::string result;char buffer[4096];size_t n;while((n=std::fread(buffer,1,sizeof buffer,f)))result.append(buffer,n);assert(!std::ferror(f)&&std::fseek(f,0,SEEK_END)==0);return result;}
template<class Log>static void lines(Log log){for(unsigned i=0;i<48;++i)log("EQUIPMENT source id=%u position=(%.3f %.3f %.3f) nativeRGB=(%.4f %.4f %.4f) denominator=(%.4f %.4f %.4f) rgb=(%.4f %.4f %.4f) range=%.2f gain=%.3f cameraYards=%.2f selfYards=%.2f",i,5898.26,658.99,646.60,.0201,.0004,0.,0.,.7,.03,.0275,.0005,0.,12.,1.,26.22,21.55);}
template<class Log>static void examples(Log log){
 log("");log("%s","literal %n %p %s");log("nul=%c:end",0);log("wide=%ls utf8=%s",L"hello","\xD0\xB4\xD0\xBE\xD0\xB6\xD0\xB4\xD1\x8C");
 for(size_t n:{size_t(2047),size_t(2048),size_t(2049),size_t(32768)}){const std::string value(n,'x');log("length=%s",value.c_str());}
 for(double v:{0.,-0.,1e-30,-1e30,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})log("float=%+.17g %.6f",v,v);
}
int main(int argc,char**){
 for(bool text:{false,true}){Before::logFile=std::tmpfile();Current::logFile=std::tmpfile();assert(Before::logFile&&Current::logFile);
  assert(_setmode(_fileno(Before::logFile),text?_O_TEXT:_O_BINARY)!=-1&&_setmode(_fileno(Current::logFile),text?_O_TEXT:_O_BINARY)!=-1);
  {Before::LogFlushBatch batch;examples(Before::logf);lines(Before::logf);}{Current::LogFlushBatch batch;examples(Current::logf);lines(Current::logf);}
  assert(read(Before::logFile)==read(Current::logFile));assert(Before::logCost.calls==Current::logCost.calls&&Before::logCost.formattedBytes==Current::logCost.formattedBytes&&!Current::logCost.ioErrors);
  std::fclose(Before::logFile);std::fclose(Current::logFile);Before::logFile=Current::logFile=nullptr;
 }puts("LOGGER_FORMAT_PASS exact_binary_text embedded_nul utf8_wide finite_nonfinite long_lines counters flush_boundary");
 if(argc>1)for(unsigned block=0;block<20;++block){const bool old=block%4==0||block%4==3;FILE* f=std::tmpfile();assert(f);if(old)Before::logFile=f;else Current::logFile=f;
  const auto start=std::chrono::steady_clock::now();for(unsigned frame=0;frame<64;++frame){if(old){Before::LogFlushBatch batch;lines(Before::logf);}else{Current::LogFlushBatch batch;lines(Current::logf);}}
  const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/64;
  const auto output=read(f);assert(!output.empty());std::fclose(f);Before::logFile=Current::logFile=nullptr;
  printf("LOGGER_FORMAT_TIMING old=%u warm=%u cpuMs=%.9f lines=48\n",unsigned(old),unsigned(block<4),ms);
 }
}
'''
(out/'test.cpp').write_text(cpp,encoding='utf-8');rows=[]
for opt in ('-O0','-O2'):
 exe=out/(opt[1:]+'.exe');subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu',opt,'-UNDEBUG','-std=c++17',str(out/'test.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
 run=subprocess.run([str(exe),*(['bench'] if opt=='-O2' else [])],capture_output=True,text=True,check=True);(out/(opt[1:]+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
 assert 'LOGGER_FORMAT_PASS' in run.stdout
 if opt=='-O2':rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('LOGGER_FORMAT_TIMING ') and 'warm=0 ' in line]
assert len(rows)==16
timing={name:statistics.median(float(r['cpuMs']) for r in rows if int(r['old'])==old) for name,old in [('before_ms',1),('after_ms',0)]}
report=dict(pass_=True,reference=a.reference,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),timing=timing,runs=rows,scope='48 equipment-light diagnostic lines; actual Windows logger, file writes and final flush included; same executable; no gameplay FPS claim')
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))
