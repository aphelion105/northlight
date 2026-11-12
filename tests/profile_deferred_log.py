# northlight-test: requires=windows-exe,zig manual timing
"""Check and time actual deferred formatting against a frozen git version. No DLL or game is run."""
from pathlib import Path
import argparse,hashlib,json,re,statistics,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT));import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--reference',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
source=fp.src('world_renderer.h');current=source.read_text(encoding='utf-8');before=subprocess.check_output(['git','show',a.reference+':src/world/world_renderer.h']).decode('utf-8')
def method(text):return text[text.index('    __attribute__((format(printf,2,3))) void deferLogf('):text.index('    void flushDeferredLogs()')].replace('std::vsnprintf','countedFormat')
for name,text in [('Current',current),('Before',before)]:
 (out/(name+'.h')).write_text('struct '+name+' { std::vector<std::string> deferredLines;\n'+method(text)+'};\n',encoding='utf-8')
cpp=r'''
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <chrono>
#include <limits>
#include <new>
#include <cstdlib>
static unsigned formats=0;static bool badFormat=false,badAllocation=false;
void* operator new(size_t n){if(badAllocation)throw std::bad_alloc();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,size_t)noexcept{std::free(p);}
static int countedFormat(char* out,size_t n,const char* format,va_list args){++formats;return badFormat?-1:std::vsnprintf(out,n,format,args);}
#include "Current.h"
#include "Before.h"
template<class Log>static void lines(Log& log){for(unsigned i=0;i<48;++i)log.deferLogf("WORLD selection id=%u position=(%.3f %.3f %.3f) rgb=(%.4f %.4f %.4f) distance=%.2f budget=%llu candidate=%u",i,5898.26,658.99,646.60,.0201,.0004,0.,26.22,1234567ull,76u);}
int main(int argc,char**){Before before;Current current;
 for(size_t size:{size_t(0),size_t(1),size_t(2047),size_t(2048),size_t(2049),size_t(8192)}){std::string value(size,'x');before.deferLogf("%s",value.c_str());current.deferLogf("%s",value.c_str());}
 before.deferLogf("%s","utf8: \xD0\xB4\xD0\xBE\xD0\xB6\xD0\xB4\xD1\x8C %n %p %f");current.deferLogf("%s","utf8: \xD0\xB4\xD0\xBE\xD0\xB6\xD0\xB4\xD1\x8C %n %p %f");
 for(double value:{0.,-0.,1e-30,-1e30,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){before.deferLogf("value=%+.17g %.6f",value,value);current.deferLogf("value=%+.17g %.6f",value,value);}
 assert(before.deferredLines==current.deferredLines);const auto size=current.deferredLines.size();badFormat=true;before.deferLogf("bad %u",17u);current.deferLogf("bad %u",17u);badFormat=false;
 std::string longValue(8192,'q');badAllocation=true;before.deferLogf("%s",longValue.c_str());current.deferLogf("%s",longValue.c_str());badAllocation=false;
 assert(current.deferredLines.size()==size&&before.deferredLines==current.deferredLines);
 formats=0;before.deferredLines.clear();lines(before);assert(formats==96);formats=0;current.deferredLines.clear();lines(current);assert(formats==48&&before.deferredLines==current.deferredLines);
 puts("DEFERRED_FORMAT_PASS exact_bytes empty_short_long_boundary utf8_percent finite_nonfinite allocation_and_format_failure formats=96->48");
 if(argc>1)for(unsigned block=0;block<20;++block){const bool old=block%4==0||block%4==3;const auto start=std::chrono::steady_clock::now();
  for(unsigned frame=0;frame<128;++frame){if(old){before.deferredLines.clear();lines(before);}else{current.deferredLines.clear();lines(current);}}
  const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/128;
  printf("DEFERRED_FORMAT_TIMING old=%u warm=%u cpuMs=%.9f lines=48\n",unsigned(old),unsigned(block<4),ms);
 }
}
'''
(out/'test.cpp').write_text(cpp,encoding='utf-8');rows=[]
for opt in ('-O0','-O2'):
 exe=out/(opt[1:]+'.exe');subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu',opt,'-UNDEBUG','-std=c++17',str(out/'test.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
 run=subprocess.run([str(exe),*(['bench'] if opt=='-O2' else [])],capture_output=True,text=True,check=True)
 (out/(opt[1:]+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert 'DEFERRED_FORMAT_PASS' in run.stdout
 if opt=='-O2':rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('DEFERRED_FORMAT_TIMING ') and 'warm=0 ' in line]
assert len(rows)==16
timing={name:statistics.median(float(r['cpuMs']) for r in rows if int(r['old'])==old) for name,old in [('before_ms',1),('after_ms',0)]}
report=dict(pass_=True,reference=a.reference,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),timing=timing,runs=rows,scope='48 deferred diagnostic lines, formatting/allocation only; no file I/O or game FPS claim')
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))
